// =============================================================================
//  WaveTree — Dwt.h
//  Periodic, multi-level orthonormal DWT / IDWT over a fixed frame of length
//  N = 2^L. One analysis level applies the orthogonal matrix W (downsampled
//  convolution with lo/hi); the synthesis level applies its adjoint W^T (the
//  transposed scatter-add). For orthonormal filters W^T = W^{-1}, so
//  IDWT(DWT(x)) == x exactly (verified in the offline PR test). Periodic
//  boundaries => the reconstructed frame is naturally periodic, so it loops
//  cleanly as a wavetable.  JUCE-free, header-only.
//
//  Coefficient packing (flat length N):
//    [ approx(L) | detail(L) | detail(L-1) | ... | detail(1) ]
//    approx & detail(L) length = N>>L ; detail(j) length = N>>j.
// =============================================================================
#pragma once

#include <vector>
#include "Wavelets.h"

namespace wavetree
{
class Dwt
{
public:
    void configure(const Wavelet& wav, int frameSize, int levels)
    {
        w = wav;
        N = frameSize;
        L = levels;
        tmp.assign((size_t) N, 0.0);
        scratch.assign((size_t) N, 0.0);   // approx work buffer (sized once)
        // Precompute band offsets/lengths.
        bandOffset.clear(); bandLen.clear();
        int approxLen = N >> L;
        bandOffset.push_back(0); bandLen.push_back(approxLen);          // approx(L)
        int off = approxLen;
        for (int j = L; j >= 1; --j) { const int len = N >> j; bandOffset.push_back(off); bandLen.push_back(len); off += len; }
    }

    int frameSize() const { return N; }
    int levels()    const { return L; }
    int numBands()  const { return (int) bandLen.size(); }       // = L+1 (approx + L details)
    int bandOff(int b) const { return bandOffset[(size_t) b]; }
    int bandSize(int b) const { return bandLen[(size_t) b]; }

    // Forward transform x (length N) -> coeffs (length N, packed).
    void forward(const double* x, double* coeffs)
    {
        std::copy(x, x + N, tmp.begin());
        int len = N;
        int detailOff = N;  // details fill from the right end inward
        for (int lvl = 0; lvl < L; ++lvl)
        {
            const int half = len / 2;
            detailOff -= half;
            analysisStep(tmp.data(), len, aBuf(half), coeffs + detailOff);
            std::copy(aBuf(half), aBuf(half) + half, tmp.begin());  // recurse on approx
            len = half;
        }
        std::copy(tmp.begin(), tmp.begin() + len, coeffs);          // final approx
    }

    // Inverse transform coeffs (length N, packed) -> x (length N).
    void inverse(const double* coeffs, double* x)
    {
        int len = N >> L;
        std::copy(coeffs, coeffs + len, tmp.begin());               // approx
        int detailOff = N >> L;
        for (int lvl = 0; lvl < L; ++lvl)
        {
            const int full = len * 2;
            synthesisStep(tmp.data(), coeffs + detailOff, len, x);  // -> x (length full)
            std::copy(x, x + full, tmp.begin());
            detailOff += len;
            len = full;
        }
        std::copy(tmp.begin(), tmp.begin() + N, x);
    }

private:
    double* aBuf(int) { return scratch.data(); }

    // One analysis level: signal (length `len`) -> approx a, detail d (length len/2).
    void analysisStep(const double* sig, int len, double* a, double* d)
    {
        const int half = len / 2;
        const int K = w.taps();
        for (int n = 0; n < half; ++n)
        {
            double sa = 0.0, sd = 0.0;
            for (int k = 0; k < K; ++k)
            {
                const double v = sig[(2 * n + k) % len];
                sa += w.lo[(size_t) k] * v;
                sd += w.hi[(size_t) k] * v;
            }
            a[n] = sa; d[n] = sd;
        }
    }

    // One synthesis level (adjoint): approx a, detail d (length `half`) -> sig (length 2*half).
    void synthesisStep(const double* a, const double* d, int half, double* sig)
    {
        const int len = half * 2;
        const int K = w.taps();
        for (int i = 0; i < len; ++i) sig[i] = 0.0;
        for (int n = 0; n < half; ++n)
        {
            const double av = a[n], dv = d[n];
            for (int k = 0; k < K; ++k)
            {
                const int idx = (2 * n + k) % len;
                sig[idx] += w.lo[(size_t) k] * av + w.hi[(size_t) k] * dv;
            }
        }
    }

    Wavelet w;
    int N = 0, L = 0;
    std::vector<double> tmp, scratch;
    std::vector<int> bandOffset, bandLen;
};
} // namespace wavetree
