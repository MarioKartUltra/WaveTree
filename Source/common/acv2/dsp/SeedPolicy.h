#pragma once
// acv2/dsp/SeedPolicy.h — v2 randomness seeding policy for synth voices.
// Header-only, C++17, std-only. Safe inside JUCE-free DSP cores.
//
// Modes:
//   Fixed          — one global seed; every note-on uses it. Fully stable.
//   PerKey         — seed = hash(globalSeed, midiNote); each key has its own
//                    reproducible character, stable across strikes and sessions.
//   RandomOnStrike — a fresh seed per note-on; the last-used seed is reported
//                    back (lastUsedSeed()) so the UI can display it and let the
//                    user LOCK a good one (copy it into the Fixed-mode seed).
//
// RNG: uses the project's existing Rng.h where present; otherwise the
// splitmix64 below (public-domain constants) is the reference generator. A
// voice "reproduces" iff it derives all of its randomness from the seed
// returned by onNoteOn() (e.g. by constructing rng::SplitMix64 { seed }).

#include <cstdint>

namespace acv2 { namespace rng {

// splitmix64 (Sebastiano Vigna, public domain) — tiny, fast, good diffusion.
struct SplitMix64
{
    std::uint64_t state = 0x9E3779B97F4A7C15ull;

    std::uint64_t next() noexcept
    {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    // uniform in [0,1)
    double nextDouble01() noexcept { return double (next() >> 11) * 0x1.0p-53; }

    // uniform in [-1,1]
    float nextBipolar() noexcept { return float (nextDouble01() * 2.0 - 1.0); }
};

// Stateless hash of (seed, key) — one splitmix64 scramble of a mixed word.
inline std::uint64_t hashCombine (std::uint64_t seed, std::uint64_t v) noexcept
{
    SplitMix64 g { seed ^ (v * 0x9E3779B97F4A7C15ull + 0xD1B54A32D192ED03ull) };
    return g.next();
}

} // namespace rng

enum class SeedMode : int { Fixed = 0, PerKey = 1, RandomOnStrike = 2 };

inline std::uint64_t perKeySeed (std::uint64_t globalSeed, int midiNote) noexcept
{
    return rng::hashCombine (globalSeed, std::uint64_t (std::uint32_t (midiNote)));
}

class SeedPolicy
{
public:
    void setMode (SeedMode m) noexcept { mode_ = m; }
    SeedMode mode() const noexcept     { return mode_; }

    // Also (re)initialises the random-on-strike stream deterministically from
    // the global seed; call setStrikeEntropy() afterwards to decorrelate
    // sessions (e.g. with a time stamp) if desired.
    void setGlobalSeed (std::uint64_t s) noexcept
    {
        global_ = s;
        strike_.state = s ^ 0xA0761D6478BD642Full;
    }

    std::uint64_t globalSeed() const noexcept { return global_; }

    // Mix external entropy (wall clock, etc.) into the strike stream. Optional.
    void setStrikeEntropy (std::uint64_t e) noexcept { strike_.state ^= e; }

    // Call on every note-on; the returned seed drives ALL of the voice's
    // randomness. Records the seed so the UI can read it back.
    std::uint64_t onNoteOn (int midiNote) noexcept
    {
        switch (mode_)
        {
            case SeedMode::PerKey:         last_ = perKeySeed (global_, midiNote); break;
            case SeedMode::RandomOnStrike: last_ = strike_.next();                 break;
            case SeedMode::Fixed:
            default:                       last_ = global_;                        break;
        }
        return last_;
    }

    // Last seed handed to a voice — poll from the UI (message thread) to show
    // it and offer "lock this seed" (i.e. setGlobalSeed(lastUsedSeed()) +
    // SeedMode::Fixed).
    std::uint64_t lastUsedSeed() const noexcept { return last_; }

private:
    SeedMode        mode_   = SeedMode::Fixed;
    std::uint64_t   global_ = 0x00C0FFEE5EEDull;
    std::uint64_t   last_   = 0;
    rng::SplitMix64 strike_ { 0x00C0FFEE5EEDull ^ 0xA0761D6478BD642Full };
};

} // namespace acv2
