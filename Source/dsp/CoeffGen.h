// =============================================================================
//  WaveTree II — CoeffGen.h
//  Fills the DWT coefficient tree per synthesis Mode, then the voice runs the
//  inverse DWT to get a "wavelet wavetable". Bands = a wavelet drawbar/formant
//  over octave scales; Cascade = a multiplicative multifractal cascade on the
//  dyadic tree (fBm slope from Hurst, breadth from intermittency); Atoms = sparse
//  wavelet grains.  JUCE-free, header-only.
//
//  v2: allocation-free — the caller passes a CoeffScratch sized once in
//  prepare(); generateCoeffs never allocates (it runs on the audio thread at
//  note-on and at tree-edit regen).
//
//  Band index b: 0 = approx (coarsest/DC). b=1..L = detail levels, coarse -> fine
//  (band b size doubles each step); frequency rises with b.
// =============================================================================
#pragma once

#include <cmath>
#include <vector>
#include "Dwt.h"
#include "Mfa.h"
#include "Params.h"
#include "Rng.h"

namespace wavetree
{
// Preallocated work space for the cascade generator (per voice, sized once).
struct CoeffScratch
{
    std::vector<double> prev, cur;
    void prepare (int frameSize)
    {
        prev.assign ((size_t) frameSize, 0.0);
        cur.assign ((size_t) frameSize, 0.0);
    }
};

// Tilt is a spectral slope in every mode: Bands has always used tilt x 2.2 as
// its slope; Cascade and Atoms add (tilt - 0.5) x 2.2 to their own, so 50 % is
// exactly what they did before tilt reached them. treeSlope() is the slope the
// painted levels of the TreeEdit overlay follow (TreeEdit.h bandReference).
inline double tiltOffset (const WaveTreeParams& p) { return (p.tilt - 0.5) * 2.2; }
inline double treeSlope (const WaveTreeParams& p)
{
    if (p.mode == Mode::Bands)   return p.tilt * 2.2;
    if (p.mode == Mode::Cascade) return cascadeSlope (p.hurst) + tiltOffset (p);
    return 0.7 + tiltOffset (p);
}

inline void generateCoeffs (double* coeffs, Dwt& dwt, const WaveTreeParams& p,
                            Rng& rng, CoeffScratch& scratch)
{
    const int N = dwt.frameSize();
    const int nb = dwt.numBands();            // = L + 1
    const int L  = dwt.levels();
    for (int i = 0; i < N; ++i) coeffs[i] = 0.0;

    if (p.mode == Mode::Bands)
    {
        const double tiltSlope   = p.tilt * 2.2;
        const double formantBand = 1.0 + p.formant * (L - 1);
        const double width       = 0.4 + p.bandQ * (L * 0.6);
        for (int b = 1; b < nb; ++b)
        {
            const double octave = (double) (b - 1);
            const double db = (b - formantBand) / width;
            const double w  = std::pow (2.0, -octave * tiltSlope) * std::exp (-0.5 * db * db);
            const int off = dwt.bandOff (b), sz = dwt.bandSize (b);
            for (int k = 0; k < sz; ++k)
                coeffs[off + k] = w * ((k & 1) ? -1.0 : 1.0);   // constant-magnitude band
        }
    }
    else if (p.mode == Mode::Cascade)
    {
        const double slope = cascadeSlope (p.hurst) + tiltOffset (p);   // the Hurst slope (Mfa.h), tilted
        const double sigma = cascadeSigma (p.intermittency);    // log-normal cascade width
        double* prev = scratch.prev.data();
        double* cur  = scratch.cur.data();
        for (int b = 1; b < nb; ++b)
        {
            const int off = dwt.bandOff (b), sz = dwt.bandSize (b);
            const double A = std::pow (2.0, -(double) (b - 1) * slope);
            for (int k = 0; k < sz; ++k)
            {
                const double parent = (b == 1) ? 0.0 : prev[(size_t) (k / 2)];
                const double lm = parent + sigma * rng.nextGaussian();  // tree-correlated multiplier
                cur[(size_t) k] = lm;
                coeffs[off + k] = A * std::exp (lm) * rng.nextSign();
            }
            std::swap (prev, cur);
        }
    }
    else // Atoms
    {
        for (int b = 1; b < nb; ++b)
        {
            const int off = dwt.bandOff (b), sz = dwt.bandSize (b);
            const double A = std::pow (2.0, -(double) (b - 1) * (0.7 + tiltOffset (p)));
            const int nnz = std::max (1, (int) (p.density * sz));
            for (int j = 0; j < nnz; ++j)
            {
                const int k = (int) (rng.nextUnit() * sz) % sz;
                coeffs[off + k] = A * rng.nextSign() * (0.5 + rng.nextUnit());
            }
        }
    }
    // Leave the approx band (b=0) at zero -> no DC; the safety HP handles the rest.
}
} // namespace wavetree
