// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
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

// The Cascade generator's ensemble spectrum in closed form (CoeffGen.h makes
// |d_j| = 2^{-j s} e^{lambda_j} with lambda a Gaussian walk of variance j sigma^2):
//   zeta(q) = q s - q^2 sigma^2 / (2 ln 2),  D(h) = 1 - (h - s)^2 ln 2 / (2 sigma^2)
// a parabola centred on s, collapsing to the point h = s when sigma = 0.
inline double cascadeSlope (double hurst)        { return 0.35 + hurst * 1.6; }
inline double cascadeSigma (double intermittency) { return intermittency * 0.9; }
// centred on the slope the tree was generated with (Hurst slope plus the Tilt offset)
inline double analyticDhAt (double slope, double intermittency, double h)
{
    const double s = slope, sigma = cascadeSigma (intermittency);
    if (sigma < 1.0e-6) return std::fabs (h - s) < 1.0e-9 ? 1.0 : -1.0e300;
    const double d = h - s;
    return 1.0 - d * d * std::log (2.0) / (2.0 * sigma * sigma);
}
inline double analyticDh (double hurst, double intermittency, double h)   // at tilt 50 %
{ return analyticDhAt (cascadeSlope (hurst), intermittency, h); }

// bandOff/bandSize describe the packed coefficient layout (see Dwt.h): band 0 is
// the approximation, bands 1..numBands-1 are details, coarse -> fine.
// Several coefficient sets (a chord: one tree per voice) are quantified
// together by ADDING their moments per band, S(q, j) = mean over every
// coefficient of every set at depth j: the spectrum of the chord's total
// coefficient measure. For voices with equal parameters it is the single-voice
// spectrum; for different ones, asymptotically the concave hull of the union.
template <typename OffFn, typename SizeFn>
inline MfaResult computeDhMulti (const float* const* sets, int numSets, int numBands, OffFn bandOff, SizeFn bandSize)
{
    MfaResult r;
    if (sets == nullptr || numSets < 1) return r;

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
            for (int v = 0; v < numSets; ++v)
                for (int k = 0; k < n; ++k)
                {
                    const double a = std::fabs ((double) sets[v][off + k]) + 1.0e-12;
                    s += std::pow (a, qs[qi]);
                }
            const double y = std::log2 (s / (double) (n * numSets));
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

template <typename OffFn, typename SizeFn>
inline MfaResult computeDh (const float* coeffs, int numBands, OffFn bandOff, SizeFn bandSize)
{
    const float* one[1] = { coeffs };
    return computeDhMulti (one, 1, numBands, bandOff, bandSize);
}
} // namespace wavetree
