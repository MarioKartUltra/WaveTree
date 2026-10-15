// =============================================================================
//  WaveTree II — Params.h   (plain seam between JUCE and the DSP core)
//  The JUCE layer resolves everything to REAL units before it lands here:
//  times in seconds, cutoff in Hz, LFO modulation already applied as bounded
//  offsets. The DSP core never sees JUCE, the host clock, or the mod matrix.
// =============================================================================
#pragma once
#include "Wavelets.h"

namespace wavetree
{
enum class Mode { Bands = 0, Cascade = 1, Atoms = 2 };

// Seeding policy for the stochastic tree content (cascade + atoms).
// Fixed  : every key plays the same table (global seed).
// PerKey : seed = hash(global, midiNote) — v1 behaviour, each key its own
//          reproducible character.
// Random : fresh 0..9999 roll per note-on; the roll is reported back so a good
//          one can be locked into the global seed.
enum class SeedModeP { Fixed = 0, PerKey = 1, Random = 2 };

struct WaveTreeParams
{
    // ---- wavelet / synthesis ----
    WaveletType wavelet = WaveletType::Db4;
    Mode        mode    = Mode::Cascade;
    unsigned    seed    = 1;                  // global seed, 0..9999
    SeedModeP   seedMode = SeedModeP::PerKey;

    bool   morphOn      = false;   // periodically regenerate + crossfade
    double morphSeconds = 1.2;     // interval between regenerations

    bool   antiAlias    = true;    // mip-mapped (band-limited) oscillator read

    // ---- Bands mode (a wavelet "drawbar/formant" over octave scales) ----
    double tilt    = 0.5;   // spectral slope across scales
    double formant = 0.5;   // which scale-band is emphasised
    double bandQ   = 0.5;   // emphasis width

    // ---- Cascade mode (multifractal: fBm slope + intermittency) ----
    double hurst        = 0.6;   // Hurst exponent H -> spectral slope
    double intermittency = 0.4;  // log-normal cascade width -> multifractal breadth

    // ---- Atoms mode (sparse wavelet grains) ----
    double density = 0.4;   // fraction of nonzero coefficients

    // ---- amp envelope (seconds; host-synced values resolved upstream) ----
    double attack  = 0.005;
    double decay   = 0.2;
    double sustain = 0.7;
    double release = 0.3;

    // ---- tone shaping / out ----
    double cutoffHz = 12000.0;  // output low-pass, real Hz (clamped to 0.45*sr)
    double reso     = 0.2;
    double output   = 0.7;
    int    numVoices = 8;

    // ---- keyboard ----
    double glideSeconds = 0.0;   // pitch slide from the previous note (0 = off), constant time per slide
    bool   legato       = false; // a key that takes over a sounding voice keeps its envelope and table
};
} // namespace wavetree
