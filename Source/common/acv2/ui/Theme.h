// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
#pragma once
// acv2/ui/Theme.h — v2 design tokens ("laboratory instrument, not SaaS panel").
// Plain std-only structs (colours as 0xAARRGGBB) so the tokens are usable from
// any code; LnfV2.h / ModRing.h turn them into juce::Colour. Committed dark
// theme: deep near-black with a per-plugin hue cast + ONE saturated accent
// carrying 30–60% of the visual weight. Text contrast ≥ 4.5:1 everywhere.

#include <cstdint>
#include <string_view>

namespace acv2 { namespace theme {

struct Palette
{
    const char*   name;          // v2 product name
    std::uint32_t background;    // window: near-black with hue cast
    std::uint32_t hero;          // scientific-display area (darkest; the hero gets the space)
    std::uint32_t panel;         // instrument fascia around the hero
    std::uint32_t outline;       // hairlines / knob tracks (low-key)
    std::uint32_t accent;        // THE saturated accent (value arcs, chips-on, BETA tag)
    std::uint32_t accentDim;     // accent at rest (ring tracks, section rules)
    std::uint32_t textPrimary;   // ≥ 4.5:1 on background AND panel
    std::uint32_t textSecondary; // labels; still ≥ 4.5:1
    std::uint32_t modRing;       // modulation-depth ring (usually = accent)
};

// The six per-plugin accents (V2_CONVENTIONS "Design language"):

inline constexpr Palette kMatterTone {   // molten-copper on graphite warm-black
    "MatterTone II",
    0xFF171209, 0xFF100C06, 0xFF221A11, 0xFF3D3122,
    0xFFE0813E, 0xFF80491F, 0xFFF4EDE3, 0xFFB5A895, 0xFFE0813E };

inline constexpr Palette kSoundNuts {    // acid-chartreuse on blue-black
    "SoundNuts II",
    0xFF0C1018, 0xFF080B11, 0xFF141A26, 0xFF283349,
    0xFFC6E93B, 0xFF66771F, 0xFFEEF2E9, 0xFF9AA6B4, 0xFFC6E93B };

inline constexpr Palette kAttractorScope { // phosphor teal-cyan on green-black (scope homage)
    "AttractorScope II",
    0xFF081210, 0xFF050D0B, 0xFF0F1D1A, 0xFF1F3630,
    0xFF35E8CC, 0xFF19746A, 0xFFE6F4F0, 0xFF93B1AA, 0xFF35E8CC };

inline constexpr Palette kWaveTree {     // violet on cold near-black
    "WaveTree II",
    0xFF0E0D17, 0xFF0A0910, 0xFF171526, 0xFF2C2942,
    0xFFA275F5, 0xFF544080, 0xFFEFECF7, 0xFFA6A0BA, 0xFFA275F5 };

inline constexpr Palette kWaveGrain {    // amber-gold on brown-black
    "WaveGrain II",
    0xFF15100A, 0xFF0F0B06, 0xFF201810, 0xFF3B301F,
    0xFFE9B44C, 0xFF7A5E28, 0xFFF5EFE3, 0xFFB2A68F, 0xFFE9B44C };

inline constexpr Palette kScaloSculpt {  // glacier blue on neutral black
    "ScaloSculpt",
    0xFF0E0F11, 0xFF0A0B0C, 0xFF16181B, 0xFF2B2F35,
    0xFF86C9EC, 0xFF476A7E, 0xFFF0F3F5, 0xFFA1AAB1, 0xFF86C9EC };

inline constexpr const Palette* kAllPalettes[] = {
    &kMatterTone, &kSoundNuts, &kAttractorScope,
    &kWaveTree, &kWaveGrain, &kScaloSculpt };

inline const Palette* paletteByName (std::string_view name) noexcept
{
    for (auto* p : kAllPalettes)
        if (name == p->name)
            return p;
    return nullptr;
}

// ── spacing / radii (px at 1x) ─────────────────────────────────────────────
struct Spacing
{
    int   unit = 8;                          // base grid
    int   xs = 4, s = 8, m = 12, l = 16, xl = 24, xxl = 40;
    float cornerS = 4.0f;                    // chips, readouts
    float cornerM = 8.0f;                    // panels
    float hairline = 1.0f;
    float knobStrokePx = 3.0f;               // arc-knob track/value stroke
    float modRingStrokePx = 2.0f;            // modulation ring stroke
    float modRingGapPx = 3.0f;               // gap between knob arc and mod ring
};
inline constexpr Spacing kSpacing {};

// ── type scale (heights in px; values/readouts use tabular/mono figures) ───
struct TypeScale
{
    float display = 26.0f;   // plugin name in the header
    float section = 12.5f;   // section headers (tracked small caps)
    float label   = 12.0f;   // knob/param labels
    float value   = 13.0f;   // unit readouts under knobs — monospaced digits
    float chip    = 11.0f;   // toggle chips (ms/beats, etc.)
    float beta    = 10.0f;   // the BETA tag
};
inline constexpr TypeScale kType {};

// Rotary geometry shared by LnfV2 knobs and ModRing (JUCE default rotary
// range: 1.2π .. 2.8π, i.e. a 288° sweep opening downward).
inline constexpr float kKnobStartAngle = 3.76991118f; // 1.2 * pi
inline constexpr float kKnobEndAngle   = 8.79645943f; // 2.8 * pi

} } // namespace acv2::theme
