// =============================================================================
//  WaveTree II — TreeEdit.h
//  Shared, lock-free edit overlay for the wavelet coefficient tree: a per-band
//  gain (octave drawbar mute/boost) plus a per-node gain (individual atoms).
//  The UI (message thread) writes single atomics and bumps `version`; voices
//  read the gains inside buildTable() on the audio thread (relaxed loads, no
//  locks, no allocation). Gains are plain multipliers in [0, kMaxGain]; 1 = off.
//  JUCE-free, header-only, std-only.
// =============================================================================
#pragma once

#include <atomic>
#include <array>
#include <cstdint>

namespace wavetree
{
struct TreeEdit
{
    static constexpr int    kMaxNodes = 2048;   // == WaveletVoice::kFrame
    static constexpr int    kMaxBands = 12;     // approx + up to 11 detail bands
    static constexpr double kMaxGain  = 2.0;

    std::array<std::atomic<float>, kMaxNodes> nodeGain;
    std::array<std::atomic<float>, kMaxBands> bandGain;
    std::atomic<std::uint32_t>                version { 0 };

    TreeEdit() { reset(); }

    void reset()
    {
        for (auto& g : nodeGain) g.store (1.0f, std::memory_order_relaxed);
        for (auto& g : bandGain) g.store (1.0f, std::memory_order_relaxed);
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

    bool isIdentity() const
    {
        for (auto& g : nodeGain) if (g.load (std::memory_order_relaxed) != 1.0f) return false;
        for (auto& g : bandGain) if (g.load (std::memory_order_relaxed) != 1.0f) return false;
        return true;
    }

    static float clampGain (float g)
    { return g < 0.0f ? 0.0f : (g > (float) kMaxGain ? (float) kMaxGain : g); }
};
} // namespace wavetree
