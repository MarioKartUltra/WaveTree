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

    // The keyboard keeps a stack of held keys (oldest first). A key with no free
    // voice takes the voice of the OLDEST held key; when the taker is released,
    // its voice goes back to the most recent held key that lost its voice — no
    // new key press needed. Legato makes both moves keep envelope and table.
    void noteOn (int note, double vel)
    {
        const int maxV = std::min (std::max (params.numVoices, 1), kMaxVoices);

        std::uint64_t s64; std::uint32_t shown;
        if (params.seedMode == SeedModeP::Random)
        {
            const std::uint32_t roll = (std::uint32_t) (policy.onNoteOn (note) % 10000ull);
            s64 = roll; shown = roll;               // lockable: Fixed mode + seed=roll reproduces it
        }
        else
        {
            s64 = resolveSeed (note);
            shown = (std::uint32_t) params.seed;
        }
        shownSeed.store (shown, std::memory_order_relaxed);
        holdKey (note, vel, s64);

        int slot = -1;
        for (int i = 0; i < maxV; ++i) if (! voices[(size_t) i].isActive()) { slot = i; break; }
        if (slot < 0) { for (int i = 0; i < maxV; ++i) if (voices[(size_t) i].isReleased()) { slot = i; break; } }
        if (slot < 0)
        {
            slot = oldestVoice (maxV);
            if (params.legato)
            {
                voices[(size_t) slot].setPitchBend (bend);
                voices[(size_t) slot].legatoTo (note, params, legatoSeed (voices[(size_t) slot], note));
                lastVoice = slot; lastHz = WaveletVoice::hzOf (note);
                return;
            }
        }

        // glide starts where the voice is (a retrigger mid-slide stays smooth) or
        // at the previous key's pitch for a fresh voice
        const double from = voices[(size_t) slot].isActive() ? voices[(size_t) slot].currentHz() : lastHz;
        voices[(size_t) slot].setPitchBend (bend);
        voices[(size_t) slot].noteOn (note, vel, params, s64, from);
        lastVoice = slot; lastHz = WaveletVoice::hzOf (note);
    }

    void noteOff (int note)
    {
        releaseKey (note);
        for (auto& v : voices)
            if (v.isActive() && ! v.isReleased() && v.midiNote() == note)
            {
                const int back = waitingKey();
                if (back < 0) { v.noteOff(); continue; }
                if (params.legato) v.legatoTo (held[(size_t) back].note, params, legatoSeed (v, held[(size_t) back].note));
                else               v.noteOn (held[(size_t) back].note, held[(size_t) back].vel, params, held[(size_t) back].seed, v.currentHz());
            }
    }

    void pitchBend (double semis) { bend = semis; for (auto& v : voices) v.setPitchBend (semis); }
    void panic() { for (auto& v : voices) v.panic(); numHeld = 0; }

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
    int heldKeys() const { return numHeld; }
    const WaveletVoice& voice (int i) const { return voices[(size_t) i]; }

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

    // ---- the chord: every sounding voice's tree (the last voice when none sounds) ----
    struct VoiceSpectrumInfo { double hurst = 0.6, intermittency = 0.4; Mode mode = Mode::Cascade; double slope = 1.31; };

    // Flat copy, kFrame magnitudes per voice; returns how many voices.
    int copyChordCoeffMags (std::vector<float>& dst, std::vector<VoiceSpectrumInfo>& info) const
    {
        dst.clear(); info.clear();
        int n = 0;
        for (const auto& v : voices)
            if (v.isActive())
            {
                dst.insert (dst.end(), v.coeffMags(), v.coeffMags() + WaveletVoice::kFrame);
                info.push_back ({ v.generatedHurst(), v.generatedIntermittency(), v.generatedMode(), v.generatedSlope() });
                ++n;
            }
        if (n == 0)
        {
            const auto& v = voices[(size_t) (lastVoice >= 0 ? lastVoice : 0)];
            dst.insert (dst.end(), v.coeffMags(), v.coeffMags() + WaveletVoice::kFrame);
            info.push_back ({ v.generatedHurst(), v.generatedIntermittency(), v.generatedMode(), v.generatedSlope() });
            n = 1;
        }
        return n;
    }

    // Changes whenever a voice starts, stops or rebuilds its tree.
    std::uint64_t chordStamp() const
    {
        std::uint64_t h = 1469598103934665603ull;
        for (size_t i = 0; i < voices.size(); ++i)
        {
            const std::uint64_t x = voices[i].isActive() ? (0x100000000ull | voices[i].buildStamp()) : 0ull;
            h ^= x + i; h *= 1099511628211ull;
        }
        return h;
    }

    VoiceSpectrumInfo shownVoiceInfo() const
    {
        const auto& v = voices[(size_t) (lastVoice >= 0 ? lastVoice : 0)];
        return { v.generatedHurst(), v.generatedIntermittency(), v.generatedMode(), v.generatedSlope() };
    }

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

    // a legato move keeps the voice's table unless the key defines it (Per-Key)
    std::uint64_t legatoSeed (const WaveletVoice& v, int note) const
    { return params.seedMode == SeedModeP::PerKey ? resolveSeed (note) : v.currentSeed(); }

    // ---- the held-key stack ----
    static constexpr int kMaxHeld = 32;
    struct HeldKey { int note = 0; double vel = 1.0; std::uint64_t seed = 1; };

    void holdKey (int note, double vel, std::uint64_t seed)
    {
        releaseKey (note);                                        // re-pressed while held: moves to the top
        if (numHeld == kMaxHeld) { std::memmove (held.data(), held.data() + 1, sizeof (HeldKey) * (kMaxHeld - 1)); --numHeld; }
        held[(size_t) numHeld++] = { note, vel, seed };
    }

    void releaseKey (int note)
    {
        for (int i = 0; i < numHeld; ++i)
            if (held[(size_t) i].note == note)
            {
                std::memmove (held.data() + i, held.data() + i + 1, sizeof (HeldKey) * (size_t) (numHeld - i - 1));
                --numHeld; --i;
            }
    }

    bool sounding (int note) const
    {
        for (const auto& v : voices) if (v.isActive() && ! v.isReleased() && v.midiNote() == note) return true;
        return false;
    }

    // the voice of the key pressed earliest (all voices busy and not released)
    int oldestVoice (int maxV) const
    {
        for (int k = 0; k < numHeld; ++k)
            for (int i = 0; i < maxV; ++i)
                if (voices[(size_t) i].isActive() && ! voices[(size_t) i].isReleased()
                    && voices[(size_t) i].midiNote() == held[(size_t) k].note) return i;
        return 0;
    }

    // the most recent held key with no voice of its own (-1: none waiting)
    int waitingKey() const
    {
        for (int k = numHeld - 1; k >= 0; --k)
            if (! sounding (held[(size_t) k].note)) return k;
        return -1;
    }

    double sr = 48000.0;
    WaveTreeParams params;
    std::array<WaveletVoice, kMaxVoices> voices;
    TreeEdit tree;
    acv2::SeedPolicy policy;
    std::atomic<std::uint32_t> shownSeed { 1 };
    double bend = 0.0, lastHz = 0.0;
    int lastVoice = -1;
    std::array<HeldKey, kMaxHeld> held {};
    int numHeld = 0;
    int regenCursor = 0;
    std::uint32_t lastTreeVersion = 0;
    static constexpr int scopeSize = 4096;
    std::vector<float> scope; int scopePos = 0;
};
} // namespace wavetree
