// =============================================================================
//  WaveTree II — Mfa.h
//  D(h) multifractal (Legendre) spectrum estimator over the DWT detail bands of
//  the current coefficient tree. Message-thread / offline use only (it is cheap,
//  but the synth never calls it on the audio thread). JUCE-free, std-only.
//
//  Method (terse; see docs/MATH.md):
//    S(q, j)  = (1/n_j) Σ_k |d_{j,k}|^q          (detail band at depth index j)
//    ζ(q)     = − slope of log2 S(q, j) vs j     (least squares over usable j)
//    h(q)     = dζ/dq (central differences),  D(h) = 1 + q·h(q) − ζ(q)
//  With the Cascade generator (log-normal weights) ζ(q) is parabolic: the D(h)
//  arch widens with intermittency σ and collapses to a point when σ = 0.
//  Convention: j = b − 1 for detail band b (1 = coarsest), matching the
//  2^{−j·slope} amplitude law used by CoeffGen.
// =============================================================================
#pragma once

#include <cmath>
#include <cstddef>

namespace wavetree
{
struct MfaResult
{
    static constexpr int kMaxQ = 16;
    int    count = 0;          // valid points
    double h[kMaxQ]  = {};     // Hölder exponents
    double Dh[kMaxQ] = {};     // spectrum values (≤ 1)
    double hMin = 0.0, hMax = 0.0, hPeak = 0.0;   // support + argmax
    bool   valid = false;
};

// bandOff/bandSize describe the packed coefficient layout (see Dwt.h): band 0 is
// the approximation, bands 1..numBands-1 are details, coarse -> fine.
template <typename OffFn, typename SizeFn>
inline MfaResult computeDh (const float* coeffs, int numBands, OffFn bandOff, SizeFn bandSize)
{
    MfaResult r;

    // q grid, avoiding q = 0 (h and D are recovered around it by the transform).
    static constexpr double qs[] = { -2.0, -1.5, -1.0, -0.5, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0 };
    constexpr int nq = int (sizeof (qs) / sizeof (qs[0]));
    static_assert (nq + 2 <= MfaResult::kMaxQ, "q grid exceeds result capacity");

    // Usable detail bands: need enough coefficients for a q-moment (n >= 4).
    int jLo = -1, jHi = -1;
    for (int b = 1; b < numBands; ++b)
        if (bandSize (b) >= 4) { if (jLo < 0) jLo = b; jHi = b; }
    if (jLo < 0 || jHi - jLo + 1 < 3)
        return r;

    // log2 moments per (q, j); tiny floor keeps negative q finite.
    double zeta[nq];
    const int nj = jHi - jLo + 1;
    for (int qi = 0; qi < nq; ++qi)
    {
        // least-squares slope of y = log2 S(q, j) against x = j
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (int b = jLo; b <= jHi; ++b)
        {
            const int off = bandOff (b), n = bandSize (b);
            double s = 0.0;
            for (int k = 0; k < n; ++k)
            {
                const double a = std::fabs ((double) coeffs[off + k]) + 1.0e-12;
                s += std::pow (a, qs[qi]);
            }
            const double y = std::log2 (s / (double) n);
            const double x = (double) (b - 1);
            sx += x; sy += y; sxx += x * x; sxy += x * y;
        }
        const double denom = (double) nj * sxx - sx * sx;
        zeta[qi] = denom != 0.0 ? -(((double) nj * sxy - sx * sy) / denom) : 0.0;
    }

    // Legendre transform: h = dζ/dq (finite differences), D = 1 + q·h − ζ.
    for (int qi = 0; qi < nq; ++qi)
    {
        double hq;
        if (qi == 0)            hq = (zeta[1] - zeta[0]) / (qs[1] - qs[0]);
        else if (qi == nq - 1)  hq = (zeta[nq-1] - zeta[nq-2]) / (qs[nq-1] - qs[nq-2]);
        else                    hq = (zeta[qi+1] - zeta[qi-1]) / (qs[qi+1] - qs[qi-1]);
        r.h[r.count]  = hq;
        r.Dh[r.count] = 1.0 + qs[qi] * hq - zeta[qi];
        ++r.count;
    }

    r.hMin = r.h[0]; r.hMax = r.h[0]; r.hPeak = r.h[0];
    double dBest = r.Dh[0];
    for (int i = 1; i < r.count; ++i)
    {
        if (r.h[i] < r.hMin) r.hMin = r.h[i];
        if (r.h[i] > r.hMax) r.hMax = r.h[i];
        if (r.Dh[i] > dBest) { dBest = r.Dh[i]; r.hPeak = r.h[i]; }
    }
    r.valid = std::isfinite (r.hMin) && std::isfinite (r.hMax);
    return r;
}
} // namespace wavetree
