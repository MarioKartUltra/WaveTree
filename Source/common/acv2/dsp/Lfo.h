// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
#pragma once
// acv2/dsp/Lfo.h — v2 modulation LFO. Header-only, C++17, std-only.
//
// ── APVTS wiring: the v2 mod-matrix convention ─────────────────────────────
// The LFO is evaluated at BLOCK RATE inside processBlock (per-sample optional),
// and its output is applied as a BOUNDED OFFSET to the APVTS-read value BEFORE
// that value enters the plain Params struct — so the JUCE-free DSP core never
// sees JUCE, the LFO, or the mod matrix. Sketch (audio thread, no allocation):
//
//   // 1. transport (cache last-known, default 120 — see TempoSync.h header)
//   if (auto* ph = getPlayHead())
//       if (auto pos = ph->getPosition()) {
//           if (auto b = pos->getBpm())         lfoA.setBpm (*b);
//           if (auto ppq = pos->getPpqPosition()) lfoA.setTransportPpq (*ppq);
//       }
//   // 2. one block-rate value per LFO (smoothing downstream is fine for v2)
//   const float a = lfoA.processBlock (buffer.getNumSamples());   // in [-1,1]
//   // 3. per destination: bounded offset BEFORE the Params struct
//   const float raw   = apvts.getRawParameterValue ("cutoff")->load();
//   const float depth = apvts.getRawParameterValue ("cutoffModA")->load(); // −1..+1
//   params.cutoff = std::clamp (raw + depth * a * kCutoffModRangeHz, kMin, kMax);
//   core.process (params, ...);                                   // JUCE-free
//
// Per-LFO parameters (all ParameterID { id, /*versionHint*/ 2 }): shape, rate
// (the three-param sync convention of TempoSync.h: <id>Sync/<id>Ms/<id>Div),
// phase, retrig mode, unipolar/bipolar. Each destination gets a <dst>ModA /
// <dst>ModB depth parameter in −1..+1, displayed as ±% (see Units.h).
// UI feedback: publish value() into a std::atomic<float> after each block; the
// acv2::ui::ModRing polls it at UI rate to animate the depth ring's dot.

#include "SeedPolicy.h"
#include "TempoSync.h"
#include <algorithm>
#include <cmath>

namespace acv2 {

enum class LfoShape : int
{
    Sine = 0, Triangle, SawUp, SawDown, Square, SampleHold, SmoothRandom, Drift
};
inline constexpr int kNumLfoShapes = 8;

inline const char* lfoShapeName (LfoShape s) noexcept
{
    switch (s)
    {
        case LfoShape::Sine:         return "Sine";
        case LfoShape::Triangle:     return "Triangle";
        case LfoShape::SawUp:        return "Saw Up";
        case LfoShape::SawDown:      return "Saw Down";
        case LfoShape::Square:       return "Square";
        case LfoShape::SampleHold:   return "S&H";
        case LfoShape::SmoothRandom: return "Smooth Rnd";
        case LfoShape::Drift:        return "Drift";
    }
    return "?";
}

enum class LfoRetrig : int { Free = 0, Key = 1, Transport = 2 };

class Lfo
{
public:
    // ── setup ──────────────────────────────────────────────────────────────
    void prepare (double sampleRate) noexcept
    {
        sr_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        recalc();
    }

    void setShape (LfoShape s) noexcept       { shape_ = s; }
    void setBipolar (bool b) noexcept         { bipolar_ = b; }
    void setPulseWidth (float w) noexcept     { pw_ = std::clamp (w, 0.02f, 0.98f); }
    void setPhaseOffset (double p01) noexcept { phaseOffset_ = wrap01 (p01); }
    void setRetrigMode (LfoRetrig m) noexcept { retrig_ = m; }

    void setRateHz (double hz) noexcept       { synced_ = false; rateHz_ = std::clamp (hz, 0.0, 200.0); recalc(); }
    void setSyncedDivision (int divIndex) noexcept { synced_ = true; div_ = tempo::clampIndex (divIndex); recalc(); }
    void setBpm (double bpm) noexcept         { bpm_ = bpm > 0.0 ? bpm : 120.0; recalc(); }

    bool   isSynced() const noexcept { return synced_; }
    double rateHz() const noexcept   { return hz_; }
    double phase() const noexcept    { return phase_; }

    // Seed all random shapes (S&H / smoothed random / drift). Deterministic:
    // same seed + same stepping ⇒ same output (wire to SeedPolicy::onNoteOn).
    void reseed (std::uint64_t seed) noexcept
    {
        rng_.state = seed;
        initRandomState();
    }

    void reset() noexcept { phase_ = 0.0; initRandomState(); }

    // Key retrig: call from the note-on path (audio thread).
    void noteOn() noexcept
    {
        if (retrig_ == LfoRetrig::Key)
        {
            phase_ = 0.0;
            initRandomState();
        }
    }

    // Transport/beat-locked: call once per block with the host ppq position
    // (quarter notes). Synced mode locks the division grid to the bar line;
    // free mode locks hz cycles to transport seconds.
    void setTransportPpq (double ppq) noexcept
    {
        if (retrig_ != LfoRetrig::Transport)
            return;
        const double p = synced_ ? tempo::phaseFromPpq (ppq, div_)
                                 : tempo::phaseFromPpqFreeHz (ppq, bpm_, hz_);
        if (p < phase_) // wrapped since last block → new random targets
            onCycleWrap();
        phase_ = p;
    }

    // ── evaluation ─────────────────────────────────────────────────────────
    // Block-rate: value at the START of the block, then advance the full block.
    float processBlock (int numSamples) noexcept
    {
        const float v = value();
        advance (numSamples);
        return v;
    }

    // Per-sample option (e.g. audio-rate destinations).
    float processSample() noexcept
    {
        const float v = value();
        advance (1);
        return v;
    }

    // Current output without advancing. Bipolar in [-1,1]; unipolar in [0,1].
    float value() const noexcept
    {
        const float v = std::clamp (raw (wrap01 (phase_ + phaseOffset_)), -1.0f, 1.0f);
        return bipolar_ ? v : 0.5f * (v + 1.0f);
    }

private:
    static double wrap01 (double p) noexcept
    {
        p -= std::floor (p);
        return p < 1.0 ? p : 0.0;
    }

    void recalc() noexcept
    {
        hz_  = synced_ ? tempo::divisionToHz (div_, bpm_) : rateHz_;
        hz_  = std::clamp (hz_, 0.0, sr_ * 0.45);
        inc_ = hz_ / sr_;
    }

    void initRandomState() noexcept
    {
        shHeld_       = rng_.nextBipolar();
        smoothTarget_ = rng_.nextBipolar();
        smoothState_  = smoothTarget_;
        driftTarget_  = rng_.nextBipolar();
        driftState_   = 0.0f;
    }

    void onCycleWrap() noexcept
    {
        shHeld_       = rng_.nextBipolar();
        smoothTarget_ = rng_.nextBipolar();
        driftTarget_  = rng_.nextBipolar();
    }

    // One-pole toward target, closed form over n samples (a = per-sample rate).
    static float slew (float state, float target, double a, double n) noexcept
    {
        return target + (state - target) * float (std::exp (-a * n));
    }

    void advance (int numSamples) noexcept
    {
        if (numSamples <= 0)
            return;

        // Smoothing rates: SmoothRandom settles within ~1/3 cycle; Drift
        // wanders over ~8 cycles. Both are convex ⇒ stay inside [-1,1].
        const double aSmooth = 2.0 * 3.14159265358979323846 * (hz_ * 3.0) / sr_;
        const double aDrift  = 2.0 * 3.14159265358979323846 * (hz_ * 0.125) / sr_;

        double remaining = double (numSamples);
        int    guard     = 0;

        while (remaining > 0.0 && guard++ < 64)
        {
            const double toWrap = inc_ > 0.0 ? (1.0 - phase_) / inc_ : remaining + 1.0;

            if (toWrap <= remaining) // wrap inside (or exactly at the end of) this span
            {
                smoothState_ = slew (smoothState_, smoothTarget_, aSmooth, toWrap);
                driftState_  = slew (driftState_,  driftTarget_,  aDrift,  toWrap);
                remaining -= toWrap;
                phase_ = 0.0;
                onCycleWrap();
            }
            else
            {
                smoothState_ = slew (smoothState_, smoothTarget_, aSmooth, remaining);
                driftState_  = slew (driftState_,  driftTarget_,  aDrift,  remaining);
                phase_ += inc_ * remaining;
                remaining = 0.0;
            }
        }
        phase_ = wrap01 (phase_ + inc_ * remaining); // guard-exhaustion safety
    }

    // Bipolar core shapes as a function of phase in [0,1).
    float raw (double p) const noexcept
    {
        switch (shape_)
        {
            case LfoShape::Sine:         return float (std::sin (2.0 * 3.14159265358979323846 * p));
            case LfoShape::Triangle:     return float (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);
            case LfoShape::SawUp:        return float (2.0 * p - 1.0);
            case LfoShape::SawDown:      return float (1.0 - 2.0 * p);
            case LfoShape::Square:       return p < double (pw_) ? 1.0f : -1.0f;
            case LfoShape::SampleHold:   return shHeld_;
            case LfoShape::SmoothRandom: return smoothState_;
            case LfoShape::Drift:        return driftState_;
        }
        return 0.0f;
    }

    double sr_ = 44100.0, rateHz_ = 1.0, bpm_ = 120.0, hz_ = 1.0, inc_ = 1.0 / 44100.0;
    double phase_ = 0.0, phaseOffset_ = 0.0;
    int    div_ = tempo::kDefaultDivision;
    bool   synced_ = false, bipolar_ = true;
    float  pw_ = 0.5f;
    LfoShape  shape_  = LfoShape::Sine;
    LfoRetrig retrig_ = LfoRetrig::Free;

    rng::SplitMix64 rng_ { 0x5EEDF00Dull };
    float shHeld_ = 0.0f, smoothTarget_ = 0.0f, smoothState_ = 0.0f;
    float driftTarget_ = 0.0f, driftState_ = 0.0f;
};

} // namespace acv2
