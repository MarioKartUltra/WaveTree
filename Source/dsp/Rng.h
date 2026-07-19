// WaveTree — Rng.h : deterministic SplitMix64 PRNG (seeded, allocation-free).
#pragma once
#include <cstdint>
namespace wavetree
{
class Rng
{
public:
    explicit Rng(std::uint64_t seed = 1) { reseed(seed); }
    void reseed(std::uint64_t seed) { state = seed ? seed : 0x9E3779B97F4A7C15ull; }
    inline double nextUnit()
    {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z =  z ^ (z >> 31);
        return (z >> 11) * (1.0 / 9007199254740992.0);
    }
    inline double nextGaussian()   // Irwin–Hall(12): mean 0, var 1, bounded
    { double s = 0.0; for (int i = 0; i < 12; ++i) s += nextUnit(); return s - 6.0; }
    inline double nextSign() { return nextUnit() < 0.5 ? -1.0 : 1.0; }
private:
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
};
} // namespace wavetree
