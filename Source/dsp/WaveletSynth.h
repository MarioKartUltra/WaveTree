// =============================================================================
//  WaveTree II — WaveletSynth.h : polyphony over WaveletVoice + the shared
//  TreeEdit overlay + the v2 seed policy (Fixed / Per-Key / Random-on-strike).
//  Regens (tree edits, param changes, morph evolution, reseeds) are budgeted:
//  at most ONE voice rebuilds its table per block, round-robin, always through
//  a crossfade. JUCE-free, header-only.
// =============================================================================
#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>
#include "WaveletVoice.h"
#include "Params.h"
#include "TreeEdit.h"
#include "acv2/dsp/SeedPolicy.h"

namespace wavetree
{
class WaveletSynth
{
public:
    static constexpr int kMaxVoices = 16;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& v : voices) v.prepare (sampleRate, &tree);
        scope.assign (scopeSize, 0.0f); scopePos = 0;
        lastTreeVersion = tree.version.load (std::memory_order_acquire);
    }

    // Change detection lives HERE: voices only regen when the synth asks.
    void setParameters (const WaveTreeParams& p)
    {
        const bool genChanged =
               p.mode != params.mode
            || changed (p.tilt, params.tilt)       || changed (p.formant, params.formant)
            || changed (p.bandQ, params.bandQ)     || changed (p.hurst, params.hurst)
            || changed (p.intermittency, params.intermittency)
            || changed (p.density, params.density);
        const bool seedChanged = p.seed != params.seed || p.seedMode != params.seedMode;

        params = p;
        policy.setMode ((acv2::SeedMode) (int) p.seedMode);
        // Only (re)seed the strike RNG on an actual change — setGlobalSeed resets the
        // strike stream, so calling it every block made Random-on-strike roll the SAME
        // seed on every strike. Fixed/PerKey ignore the strike stream, so keeping
        // global_ synced each block stays safe for them.
        if (seedChanged || p.seedMode != SeedModeP::Random)
            policy.setGlobalSeed ((std::uint64_t) p.seed);

        for (auto& v : voices)
            if (v.isActive())
            {
                v.updateParameters (params);
                if (genChanged) v.requestRegen (0.03);
                if (seedChanged && p.seedMode != SeedModeP::Random)
                    v.reseed (resolveSeed (v.midiNote()));
            }
    }
    const WaveTreeParams& parameters() const { return params; }

    void noteOn (int note, double vel)
    {
        const int maxV = std::min (std::max (params.numVoices, 1), kMaxVoices);
        int slot = -1;
        for (int i = 0; i < maxV; ++i) if (! voices[i].isActive()) { slot = i; break; }
        if (slot < 0) { for (int i = 0; i < maxV; ++i) if (voices[i].isReleased()) { slot = i; break; } }
        if (slot < 0) slot = 0;

        std::uint64_t s64; std::uint32_t shown;
        if (params.seedMode == SeedModeP::Random)
        {
            const std::uint32_t roll = (std::uint32_t) (policy.onNoteOn (note) % 10000ull);
            s64 = roll; shown = roll;               // lockable: Fixed mode + seed=roll reproduces it
        }
        else
        {
            s64 = params.seedMode == SeedModeP::PerKey
                    ? acv2::perKeySeed ((std::uint64_t) params.seed, note)
                    : (std::uint64_t) params.seed;
            shown = (std::uint32_t) params.seed;
        }
        shownSeed.store (shown, std::memory_order_relaxed);

        voices[slot].setPitchBend (bend);
        voices[slot].noteOn (note, vel, params, s64);
        lastVoice = slot;
    }

    void noteOff (int note)
    { for (auto& v : voices) if (v.isActive() && ! v.isReleased() && v.midiNote() == note) v.noteOff(); }

    void pitchBend (double semis) { bend = semis; for (auto& v : voices) v.setPitchBend (semis); }
    void panic() { for (auto& v : voices) v.panic(); }

    void renderBlock (float* L, float* R, int n)
    {
        // tree edits since last block? -> crossfaded regen for every active voice
        const std::uint32_t tv = tree.version.load (std::memory_order_acquire);
        if (tv != lastTreeVersion)
        {
            lastTreeVersion = tv;
            for (auto& v : voices) if (v.isActive()) v.requestRegen (0.03);
        }

        // regen budget: at most one table rebuild per block, round-robin
        for (int i = 0; i < kMaxVoices; ++i)
        {
            const int idx = (regenCursor + i) % kMaxVoices;
            if (voices[(size_t) idx].hasPendingRegen())
            { voices[(size_t) idx].serviceRegen(); regenCursor = (idx + 1) % kMaxVoices; break; }
        }

        const double gain = params.output;
        for (int i = 0; i < n; ++i)
        {
            double mix = 0.0;
            for (auto& v : voices) if (v.isActive()) mix += v.renderSample();
            mix *= gain;
            if (! std::isfinite (mix)) mix = 0.0;
            mix = std::tanh (mix);                        // gentle safety saturation
            const float f = (float) mix;
            if (L) L[i] = f; if (R) R[i] = f;
            scope[(size_t) scopePos] = f; scopePos = (scopePos + 1) % scopeSize;
        }
    }

    int activeVoices() const { int c = 0; for (auto& v : voices) if (v.isActive()) ++c; return c; }

    // ---- UI feeds (message thread; torn reads are display-only) ----
    void copyScope (std::vector<float>& dst) const
    { dst.resize (scopeSize); for (int i = 0; i < scopeSize; ++i) dst[(size_t) i] = scope[(size_t) ((scopePos + i) % scopeSize)]; }

    void copyWavetable (std::vector<float>& dst) const
    {
        const auto& v = voices[(size_t) (lastVoice >= 0 ? lastVoice : 0)];
        dst.resize (WaveletVoice::kFrame);
        std::memcpy (dst.data(), v.wavetable(), sizeof (float) * WaveletVoice::kFrame);
    }

    void copyCoeffMags (std::vector<float>& dst) const
    {
        const auto& v = voices[(size_t) (lastVoice >= 0 ? lastVoice : 0)];
        dst.resize (WaveletVoice::kFrame);
        std::memcpy (dst.data(), v.coeffMags(), sizeof (float) * WaveletVoice::kFrame);
    }

    std::uint32_t coeffStamp() const
    { return voices[(size_t) (lastVoice >= 0 ? lastVoice : 0)].buildStamp(); }

    std::uint32_t lastShownSeed() const { return shownSeed.load (std::memory_order_relaxed); }

    TreeEdit&       treeEdit()       { return tree; }
    const TreeEdit& treeEdit() const { return tree; }

private:
    static bool changed (double a, double b) { return std::fabs (a - b) > 1.0e-4; }
    bool anyActive() const { for (auto& v : voices) if (v.isActive()) return true; return false; }

    std::uint64_t resolveSeed (int note) const
    {
        return params.seedMode == SeedModeP::PerKey
                 ? acv2::perKeySeed ((std::uint64_t) params.seed, note)
                 : (std::uint64_t) params.seed;
    }

    double sr = 48000.0;
    WaveTreeParams params;
    std::array<WaveletVoice, kMaxVoices> voices;
    TreeEdit tree;
    acv2::SeedPolicy policy;
    std::atomic<std::uint32_t> shownSeed { 1 };
    double bend = 0.0;
    int lastVoice = -1;
    int regenCursor = 0;
    std::uint32_t lastTreeVersion = 0;
    static constexpr int scopeSize = 4096;
    std::vector<float> scope; int scopePos = 0;
};
} // namespace wavetree
