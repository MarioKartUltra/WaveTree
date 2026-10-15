// =============================================================================
//  WaveTree II — TreeEdit.h
//  Shared, lock-free edit overlay for the wavelet coefficient tree: a per-band
//  gain (octave drawbar mute/boost) plus a per-node edit (individual atoms).
//  The UI (message thread) writes single atomics and bumps `version`; voices
//  read them inside buildTable() on the audio thread (relaxed loads, no
//  locks, no allocation).
//
//  A node is edited one of two ways:
//    - a painted LEVEL, which sets the coefficient's magnitude outright, so a
//      coefficient the generator left at or near zero can be painted in. The
//      level is a height in a row, 0..1, on ONE decibel scale for the whole
//      tree, tied to the largest generated magnitude (the "tree peak"): the top
//      of a row is kTopDb above the peak, the bottom kRangeDb below the top.
//      What is drawn is what is heard. The node also keeps the tree's slope at
//      the moment it was painted, so when Tilt / Hurst / mode change the slope
//      it moves with the tree's own decay at its scale (paintedMagnitude).
//    - a GAIN in [0, kMaxGain], a plain multiplier (1 = off). This is what
//      painting wrote before levels existed; projects saved then still load.
//  JUCE-free, header-only, std-only.
// =============================================================================
#pragma once

#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>

namespace wavetree
{
struct TreeEdit
{
    static constexpr int    kMaxNodes = 2048;   // == WaveletVoice::kFrame
    static constexpr int    kMaxBands = 12;     // approx + up to 11 detail bands
    static constexpr double kMaxGain  = 2.0;

    static constexpr float  kUnset     = -1.0f;  // a node with no painted level
    static constexpr double kTopDb     = 6.020599913279624;   // top of a row: exactly twice the tree peak
    static constexpr double kRangeDb   = 10.0 * kTopDb;       // bottom of a row, about 60 dB below the top;
                                                              // puts the tree peak at 90% of the row
    static constexpr double kMuteBelow = 0.03;   // painting this close to the bottom mutes

    std::array<std::atomic<float>, kMaxNodes> nodeGain;
    std::array<std::atomic<float>, kMaxNodes> nodeLevel;
    std::array<std::atomic<float>, kMaxNodes> nodeSlope;   // the tree's slope when the node was painted
    std::array<std::atomic<float>, kMaxBands> bandGain;
    std::atomic<std::uint32_t>                version { 0 };

    TreeEdit() { reset(); }

    void reset()
    {
        for (auto& g : nodeGain)  g.store (1.0f, std::memory_order_relaxed);
        for (auto& l : nodeLevel) l.store (kUnset, std::memory_order_relaxed);
        for (auto& s : nodeSlope) s.store (0.0f, std::memory_order_relaxed);
        for (auto& g : bandGain)  g.store (1.0f, std::memory_order_relaxed);
        bump();
    }

    void bump() { version.fetch_add (1, std::memory_order_release); }

    void setNodeGain (int i, float g)
    {
        if (i >= 0 && i < kMaxNodes)
            nodeGain[(size_t) i].store (clampGain (g), std::memory_order_relaxed);
    }

    void setBandGain (int b, float g)
    {
        if (b >= 0 && b < kMaxBands)
            bandGain[(size_t) b].store (clampGain (g), std::memory_order_relaxed);
    }

    float getNodeGain (int i) const
    { return (i >= 0 && i < kMaxNodes) ? nodeGain[(size_t) i].load (std::memory_order_relaxed) : 1.0f; }

    float getBandGain (int b) const
    { return (b >= 0 && b < kMaxBands) ? bandGain[(size_t) b].load (std::memory_order_relaxed) : 1.0f; }

    // Paint a node: its magnitude becomes this height on the tree's scale, at
    // the tree slope `paintSlope` (what treeSlope() was when it was painted).
    void setNodeLevel (int i, float level01, float paintSlope)
    {
        if (i < 0 || i >= kMaxNodes) return;
        nodeLevel[(size_t) i].store (level01 < 0.0f ? 0.0f : (level01 > 1.0f ? 1.0f : level01),
                                     std::memory_order_relaxed);
        nodeSlope[(size_t) i].store (paintSlope, std::memory_order_relaxed);
        nodeGain[(size_t) i].store (1.0f, std::memory_order_relaxed);
    }

    float getNodeSlope (int i) const
    { return (i >= 0 && i < kMaxNodes) ? nodeSlope[(size_t) i].load (std::memory_order_relaxed) : 0.0f; }

    // Erase a node's edits: back to what the generator made.
    void clearNode (int i)
    {
        if (i < 0 || i >= kMaxNodes) return;
        nodeLevel[(size_t) i].store (kUnset, std::memory_order_relaxed);
        nodeGain[(size_t) i].store (1.0f, std::memory_order_relaxed);
    }

    float getNodeLevel (int i) const
    { return (i >= 0 && i < kMaxNodes) ? nodeLevel[(size_t) i].load (std::memory_order_relaxed) : kUnset; }

    bool isIdentity() const
    {
        for (auto& g : nodeGain)  if (g.load (std::memory_order_relaxed) != 1.0f) return false;
        for (auto& l : nodeLevel) if (l.load (std::memory_order_relaxed) >= 0.0f) return false;
        for (auto& g : bandGain)  if (g.load (std::memory_order_relaxed) != 1.0f) return false;
        return true;
    }

    static float clampGain (float g)
    { return g < 0.0f ? 0.0f : (g > (float) kMaxGain ? (float) kMaxGain : g); }

    // ---- the row's scale ----------------------------------------------------
    static double levelToMagnitude (double level01, double treePeak)
    {
        if (level01 < kMuteBelow) return 0.0;
        const double l = level01 > 1.0 ? 1.0 : level01;
        return treePeak * std::pow (10.0, (kTopDb - (1.0 - l) * kRangeDb) / 20.0);
    }

    static double magnitudeToLevel (double magnitude, double treePeak)
    {
        if (magnitude <= 0.0 || treePeak <= 0.0) return 0.0;
        const double l = 1.0 - (kTopDb - 20.0 * std::log10 (magnitude / treePeak)) / kRangeDb;
        return l < 0.0 ? 0.0 : (l > 1.0 ? 1.0 : l);
    }
};

inline double treePeakOf (const double* coeffs, int n)
{
    double pk = 0.0;
    for (int i = 0; i < n; ++i) pk = std::fmax (pk, std::fabs (coeffs[i]));
    return pk;
}

// The tree's own decay at band b: the peak of the GENERATED coefficients in the
// band that holds it, and from there an octave per unit of `slope` down per
// finer band (up per coarser one).
inline double bandReference (double peak, int peakBand, int band, double slope)
{
    return peak * std::pow (2.0, -(double) (band - peakBand) * slope);
}

// A painted node: its level on the whole-tree scale, moved by however much the
// tree's decay at its band has changed since it was painted (none while the
// slope is where it was, so the drawn height is exactly what is heard).
inline double paintedMagnitude (float level, float paintSlope, double peak, int peakBand, int band, double slope)
{
    return TreeEdit::levelToMagnitude ((double) level, peak)
         * bandReference (1.0, peakBand, band, slope) / bandReference (1.0, peakBand, band, (double) paintSlope);
}

template <typename BandOff, typename BandSize>
inline int peakBandOf (const double* in, int numBands, BandOff bandOff, BandSize bandSize, double& peakOut)
{
    double peak = 0.0; int peakBand = 0;
    for (int b = 0; b < numBands; ++b)
    {
        const int off = bandOff (b), n = bandSize (b);
        for (int k = 0; k < n; ++k)
            if (std::fabs (in[off + k]) > peak) { peak = std::fabs (in[off + k]); peakBand = b; }
    }
    peakOut = peak <= 0.0 ? 1.0 : peak;
    return peakBand;
}

// The edited tree: out = band gain x (painted magnitude with the generator's
// sign, or generated value x node gain). Painting never moves the scale: it is
// the generated peak. `slope` is the tree's slope now (treeSlope). Audio-thread
// safe: relaxed atomic loads, no allocation.
template <typename BandOff, typename BandSize>
inline void applyTreeEdit (const TreeEdit& t, const double* in, double* out, int numBands,
                           BandOff bandOff, BandSize bandSize, double slope = 0.0)
{
    double peak = 1.0;
    const int peakBand = peakBandOf (in, numBands, bandOff, bandSize, peak);

    for (int b = 0; b < numBands; ++b)
    {
        const int off = bandOff (b), n = bandSize (b);
        const double bg  = (double) t.getBandGain (b);
        for (int k = 0; k < n; ++k)
        {
            const double c     = in[off + k];
            const float  level = t.getNodeLevel (off + k);
            if (level >= 0.0f)
            {
                // a node the generator left at zero has no sign: alternate, as the Bands mode does
                const double sign = c < 0.0 ? -1.0 : (c > 0.0 ? 1.0 : ((k & 1) ? -1.0 : 1.0));
                out[off + k] = sign * paintedMagnitude (level, t.getNodeSlope (off + k), peak, peakBand, b, slope) * bg;
            }
            else
                out[off + k] = c * bg * (double) t.getNodeGain (off + k);
        }
    }
}
} // namespace wavetree
