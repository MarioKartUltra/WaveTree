// =============================================================================
//  WaveTree II — WaveletVoice.h
//  One voice: fill the DWT coefficient tree (CoeffGen) -> apply the TreeEdit
//  overlay (band/node gains from the interactive tree editor) -> inverse DWT ->
//  a bank of band-limited "wavelet wavetables" (mip m = the m finest detail
//  bands zeroed BEFORE the IDWT — anti-aliasing done in the wavelet domain) ->
//  a resampling oscillator reads the mip matching the playback rate. ADSR amp
//  envelope + TPT low-pass. Table swaps always go through a crossfade (morph
//  evolution, seed changes, tree edits and parameter regens all de-clicked).
//
//  RT rules: everything is sized in prepare(); noteOn/regen never allocate
//  (wavelet filter banks are pre-cached, Dwt buffers are capacity-warmed).
//  Regens are requested by the synth and serviced at most one voice per block.
//  JUCE-free, header-only.
// =============================================================================
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>
#include "Dwt.h"
#include "CoeffGen.h"
#include "Params.h"
#include "Rng.h"
#include "TreeEdit.h"

namespace wavetree
{
struct Svf   // TPT state-variable low-pass
{
    double ic1 = 0, ic2 = 0, g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    void reset() { ic1 = ic2 = 0; }
    void set (double fcHz, double sr, double reso)
    {
        fcHz = std::min (std::max (fcHz, 20.0), sr * 0.45);
        g = std::tan (M_PI * fcHz / sr);
        k = 2.0 - 1.9 * std::min (std::max (reso, 0.0), 1.0);
        a1 = 1.0 / (1.0 + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    inline double process (double v0)
    { const double v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
      ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; return v2; }
};

class WaveletVoice
{
public:
    static constexpr int kFrame  = 2048;
    static constexpr int kLevels = 10;
    static constexpr int kMips   = 8;    // mip m zeroes the m finest detail bands

    void prepare (double sampleRate, const TreeEdit* sharedTree)
    {
        sr = sampleRate;
        tree = sharedTree;
        table.assign ((size_t) kFrame * kMips, 0.0f);
        altTable.assign ((size_t) kFrame * kMips, 0.0f);
        coeffBuf.assign (kFrame, 0.0);
        gainBuf.assign (kFrame, 0.0);
        idwtBuf.assign (kFrame, 0.0);
        dispMags.assign (kFrame, 0.0f);
        scratch.prepare (kFrame);
        for (int i = 0; i < 4; ++i) wavelets[i] = makeWavelet ((WaveletType) i);
        dwt.configure (wavelets[3], kFrame, kLevels);   // Db6 first: warms max capacity
        dwt.configure (wavelets[(int) curWavelet], kFrame, kLevels);
        reset();
    }

    void setPitchBend (double semis) { bend = std::pow (2.0, semis / 12.0); if (active) setFreq (f0Hz); }

    void noteOn (int midiNote, double vel, const WaveTreeParams& params, std::uint64_t seed)
    {
        p_ = params;
        note = midiNote; velocity = vel;
        seed64 = seed;
        configureDwt (p_.wavelet);
        buildTable (table.data(), seed64);
        morphXf = 1.0; morphing = false; regenPending = false; pendingXfSec = 0.03;
        setMorphTimer();

        f0Hz = 440.0 * std::pow (2.0, (midiNote - 69) / 12.0);
        phase = 0.0; setFreq (f0Hz);
        env = 0.0; stage = Attack; released = false; active = true;
        svf.reset();
    }

    void noteOff() { released = true; stage = Release; }

    // Store the latest params; the SYNTH decides when a regen is needed
    // (it owns the change detection + the one-rebuild-per-block budget).
    void updateParameters (const WaveTreeParams& params)
    {
        const bool aaChanged = params.antiAlias != p_.antiAlias;
        p_ = params;
        if (active && p_.wavelet != curWavelet) requestRegen (0.03);
        if (active && aaChanged) setFreq (f0Hz);   // re-pick the mip level
    }

    // Ask for a de-clicked table rebuild (tree edit / param change / seed).
    void requestRegen (double xfSeconds = 0.03)
    { if (active) { regenPending = true; pendingXfSec = xfSeconds; } }

    void reseed (std::uint64_t seed)
    { seed64 = seed; requestRegen (0.03); }

    // Called by the synth (audio thread), at most one voice per block: build the
    // pending table into the alternate slab and start the crossfade.
    bool serviceRegen()
    {
        if (! active || ! regenPending || morphing) return false;
        regenPending = false;
        configureDwt (p_.wavelet);
        buildTable (altTable.data(), seed64);
        morphing = true; morphXf = 0.0;
        xfStep = 1.0 / (std::max (0.005, pendingXfSec) * sr);
        return true;
    }

    inline float renderSample()
    {
        if (! active) return 0.0f;

        // ---- morph: periodically evolve the seed (serviced by the synth) ----
        if (p_.morphOn && ! morphing && ! regenPending && --morphCounter <= 0)
        {
            seed64 += 0x9E3779B97F4A7C15ull;
            requestRegen (std::min (0.35, 0.5 * std::max (0.05, p_.morphSeconds)));
        }

        // ---- oscillator (cubic-interpolated read of the active mip) ----
        const float* tb  = table.data()    + (size_t) mip * kFrame;
        const float* tb2 = altTable.data() + (size_t) mip * kFrame;
        const double pos = phase * kFrame;
        const int i1 = (int) pos; const double f = pos - i1;
        auto rd = [] (const float* t, int i) { return (double) t[(size_t) ((i % kFrame + kFrame) % kFrame)]; };
        auto cubic = [&] (const float* t) {
            const double y0 = rd (t, i1 - 1), y1 = rd (t, i1), y2 = rd (t, i1 + 1), y3 = rd (t, i1 + 2);
            const double a = y3 - y2 - y0 + y1, b = y0 - y1 - a, c = y2 - y0;
            return ((a * f + b) * f + c) * f + y1; };
        double s = cubic (tb);
        if (morphing)
        {
            const double s2 = cubic (tb2);
            s = s + morphXf * (s2 - s);
            morphXf += xfStep;
            if (morphXf >= 1.0) { table.swap (altTable); morphing = false; setMorphTimer(); }
        }

        phase += phaseInc; if (phase >= 1.0) phase -= 1.0;

        // ---- amp envelope ----
        advanceEnv();

        // ---- filter + level ----
        svf.set (p_.cutoffHz, sr, p_.reso);
        double out = svf.process (s) * env * velocity;

        // safety HP / DC block
        const double dc = out - dcX1 + 0.999 * dcY1; dcX1 = out; dcY1 = dc;
        out = dc;
        if (! std::isfinite (out)) out = 0.0;
        return (float) out;
    }

    bool isActive()   const { return active; }
    bool isReleased() const { return released; }
    bool hasPendingRegen() const { return active && regenPending && ! morphing; }
    int  midiNote()   const { return note; }
    std::uint64_t currentSeed() const { return seed64; }
    void reset() { phase = 0; env = 0; stage = Idle; active = false; released = false;
                   morphing = false; regenPending = false; svf.reset(); dcX1 = dcY1 = 0; }
    void panic() { reset(); }

    // UI feeds (message-thread reads; torn reads are display-only)
    const float*  wavetable() const  { return table.data(); }           // mip 0
    const float*  coeffMags() const  { return dispMags.data(); }        // pre-gain |coeffs|
    std::uint32_t buildStamp() const { return stamp; }
    const Dwt&    layout() const     { return dwt; }

private:
    enum Stage { Idle, Attack, Decay, Sustain, Release };

    void configureDwt (WaveletType wt)
    {
        if (wt != curWavelet || dwt.frameSize() != kFrame)
        { dwt.configure (wavelets[(int) wt], kFrame, kLevels); curWavelet = wt; }
    }

    void buildTable (float* dst, std::uint64_t seed)
    {
        Rng rng (seed);
        generateCoeffs (coeffBuf.data(), dwt, p_, rng, scratch);

        // publish pre-gain magnitudes for the tree display
        for (int i = 0; i < kFrame; ++i) dispMags[(size_t) i] = (float) std::fabs (coeffBuf[(size_t) i]);
        ++stamp;

        // TreeEdit overlay: per-band and per-node gains (lock-free reads)
        const int nb = dwt.numBands();
        if (tree != nullptr && ! tree->isIdentity())
        {
            for (int b = 0; b < nb; ++b)
            {
                const int off = dwt.bandOff (b), n = dwt.bandSize (b);
                const double bg = (double) tree->getBandGain (b);
                for (int k = 0; k < n; ++k)
                    gainBuf[(size_t) (off + k)] = coeffBuf[(size_t) (off + k)] * bg
                                                  * (double) tree->getNodeGain (off + k);
            }
        }
        else
            std::copy (coeffBuf.begin(), coeffBuf.end(), gainBuf.begin());

        // mip chain: IDWT, then zero the finest remaining detail band and repeat
        double pk = 1.0e-9;
        for (int m = 0; m < kMips; ++m)
        {
            dwt.inverse (gainBuf.data(), idwtBuf.data());
            if (m == 0)
                for (int i = 0; i < kFrame; ++i) pk = std::max (pk, std::fabs (idwtBuf[(size_t) i]));
            const double inv = 1.0 / pk;
            float* slab = dst + (size_t) m * kFrame;
            for (int i = 0; i < kFrame; ++i) slab[i] = (float) (idwtBuf[(size_t) i] * inv);

            const int bz = nb - 1 - m;                 // finest remaining detail band
            if (bz < 1) { for (int mm = m + 1; mm < kMips; ++mm)
                              std::copy (slab, slab + kFrame, dst + (size_t) mm * kFrame);
                          break; }
            const int off = dwt.bandOff (bz), n = dwt.bandSize (bz);
            for (int k = 0; k < n; ++k) gainBuf[(size_t) (off + k)] = 0.0;
        }
    }

    void setFreq (double f0)
    {
        phaseInc = f0 * bend / sr;
        // band-limit: keep max retained cycles/frame under Nyquist for this rate
        int m = 0;
        if (p_.antiAlias && phaseInc > 0.0)
        {
            const double x = std::log2 (phaseInc) + 11.0;   // = log2(inc * 2^11)
            m = (int) std::ceil (x);
            m = m < 0 ? 0 : (m >= kMips ? kMips - 1 : m);
        }
        mip = m;
    }

    void setMorphTimer()
    { morphCounter = (int) (std::max (0.05, p_.morphSeconds) * sr); }

    void advanceEnv()
    {
        switch (stage)
        {
            case Attack:  env += 1.0 / std::max (1.0e-4, p_.attack) / sr; if (env >= 1.0) { env = 1.0; stage = Decay; } break;
            case Decay:   env -= (1.0 - p_.sustain) / std::max (1.0e-4, p_.decay) / sr; if (env <= p_.sustain) { env = p_.sustain; stage = Sustain; } break;
            case Sustain: break;
            case Release: env -= p_.sustain > 0 ? (p_.sustain / std::max (1.0e-4, p_.release) / sr) : (1.0 / std::max (1.0e-4, p_.release) / sr);
                          if (env <= 0.0) { env = 0.0; active = false; stage = Idle; } break;
            default: break;
        }
    }

    double sr = 48000.0;
    Dwt dwt;
    Wavelet wavelets[4];
    WaveletType curWavelet = WaveletType::Db4;
    const TreeEdit* tree = nullptr;

    std::vector<float>  table, altTable;      // kMips slabs of kFrame each
    std::vector<double> coeffBuf, gainBuf, idwtBuf;
    std::vector<float>  dispMags;
    CoeffScratch scratch;
    std::uint32_t stamp = 0;

    WaveTreeParams p_;
    int    note = 60; double velocity = 1.0, bend = 1.0, f0Hz = 261.63;
    double phase = 0.0, phaseInc = 0.0;
    int    mip = 0;
    double env = 0.0; Stage stage = Idle;
    bool   active = false, released = false;
    Svf    svf;
    double dcX1 = 0, dcY1 = 0;

    std::uint64_t seed64 = 1;
    int    morphCounter = 1 << 30;
    bool   morphing = false, regenPending = false;
    double morphXf = 1.0, xfStep = 0.0, pendingXfSec = 0.03;
};
} // namespace wavetree
