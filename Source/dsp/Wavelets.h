// =============================================================================
//  WaveTree — Wavelets.h
//  Orthonormal wavelet filter banks (Haar + Daubechies). We store the analysis
//  low-pass h (sum = sqrt(2)); the high-pass g is the standard alternating flip
//  g[k] = (-1)^k h[K-1-k]. Because these are ORTHONORMAL, the synthesis stage is
//  the exact adjoint of analysis, which guarantees perfect reconstruction (see
//  Dwt.h). JUCE-free, header-only, std-only.
// =============================================================================
#pragma once

#include <array>
#include <string>
#include <vector>

namespace wavetree
{
enum class WaveletType { Haar = 0, Db2 = 1, Db4 = 2, Db6 = 3 };

struct Wavelet
{
    std::vector<double> lo;   // analysis low-pass h (decomposition)
    std::vector<double> hi;   // analysis high-pass g = (-1)^k h[K-1-k]
    std::string name;

    int taps() const { return (int) lo.size(); }
};

inline Wavelet makeWavelet(WaveletType t)
{
    Wavelet w;
    switch (t)
    {
        case WaveletType::Haar:
            w.name = "Haar";
            w.lo = { 0.7071067811865476, 0.7071067811865476 };
            break;
        case WaveletType::Db2:
            w.name = "Daubechies-2";
            w.lo = { 0.48296291314469025, 0.836516303737469,
                     0.22414386804185735, -0.12940952255092145 };
            break;
        case WaveletType::Db4:
            w.name = "Daubechies-4";
            w.lo = { 0.23037781330885523, 0.7148465705525415, 0.6308807679295904,
                    -0.02798376941698385, -0.18703481171888114, 0.030841381835986965,
                     0.032883011666982945, -0.010597401784997278 };
            break;
        case WaveletType::Db6:
        default:
            w.name = "Daubechies-6";
            w.lo = { 0.11154074335008017, 0.4946238903983854, 0.7511339080215775,
                     0.3152503517092432, -0.22626469396516913, -0.12976686756709563,
                     0.09750160558707936, 0.02752286553001629, -0.031582039318031156,
                     0.0005538422009938016, 0.004777257511010651, -0.001077301085308479 };
            break;
    }
    const int K = (int) w.lo.size();
    w.hi.resize((size_t) K);
    for (int k = 0; k < K; ++k)
        w.hi[(size_t) k] = ((k & 1) ? -1.0 : 1.0) * w.lo[(size_t) (K - 1 - k)];
    return w;
}

inline const char* waveletName(WaveletType t)
{
    switch (t) { case WaveletType::Haar: return "Haar"; case WaveletType::Db2: return "Db2";
                 case WaveletType::Db4: return "Db4"; default: return "Db6"; }
}
} // namespace wavetree
