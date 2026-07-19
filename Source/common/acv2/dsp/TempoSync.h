#pragma once
// acv2/dsp/TempoSync.h — frozen v2 tempo-sync division table + math.
// Header-only, C++17, std-only. Safe inside JUCE-free DSP cores.
//
// ── APVTS wiring: the frozen THREE-PARAM sync convention (versionHint = 2) ──
// Per time-based control <id>, create exactly three stable parameters, each with
// juce::ParameterID { "<id>...", /*versionHint*/ 2 }:
//
//   <id>Sync : AudioParameterBool   — ms mode (false) vs beat mode (true)
//   <id>Ms   : AudioParameterFloat  — free time in ms (skewed range)
//   <id>Div  : AudioParameterChoice — StringArray built from acv2::tempo::labels(),
//                                     i.e. the FROZEN 16-division list below.
//
// NEVER reorder, remove, or append to the choice list after release: the
// normalized value is index/(N-1), so even appending breaks saved automation.
// UI shows ONE knob that swaps its attachment between <id>Ms and <id>Div, plus a
// ms/beats toggle chip attached to <id>Sync (see acv2/ui/LnfV2.h chips).
//
// Tempo convention: hosts report bpm in QUARTER NOTES per minute (JUCE
// getPosition()->getBpm(); cache last-known value, default 120). Note-value
// durations therefore depend only on bpm — the time signature does NOT change
// them; the signature enters only through the bar-relative helpers
// (barQuarters/barMs) and ppq→bar phase for beat-locked LFOs.

#include <cmath>
#include <string_view>

namespace acv2 { namespace tempo {

enum class Modifier : int { Straight = 0, Dotted = 1, Triplet = 2 };

struct Division
{
    const char* label;  // frozen UI label
    int         num;    // base note value = num/den whole notes (before modifier)
    int         den;
    Modifier    mod;    // dotted = 1.5x, triplet = 2/3x of the base duration
};

// FROZEN 16-division list (TECH_BRIEF §2). Do not mutate after release.
inline constexpr Division kDivisions[] = {
    { "1/32",  1, 32, Modifier::Straight },
    { "1/16T", 1, 16, Modifier::Triplet  },
    { "1/16",  1, 16, Modifier::Straight },
    { "1/8T",  1,  8, Modifier::Triplet  },
    { "1/16.", 1, 16, Modifier::Dotted   },
    { "1/8",   1,  8, Modifier::Straight },
    { "1/4T",  1,  4, Modifier::Triplet  },
    { "1/8.",  1,  8, Modifier::Dotted   },
    { "1/4",   1,  4, Modifier::Straight },
    { "1/2T",  1,  2, Modifier::Triplet  },
    { "1/4.",  1,  4, Modifier::Dotted   },
    { "1/2",   1,  2, Modifier::Straight },
    { "1/2.",  1,  2, Modifier::Dotted   },
    { "1/1",   1,  1, Modifier::Straight },
    { "2/1",   2,  1, Modifier::Straight },
    { "4/1",   4,  1, Modifier::Straight },
};

inline constexpr int kNumDivisions = int (sizeof (kDivisions) / sizeof (kDivisions[0]));
static_assert (kNumDivisions == 16, "the division list is frozen at 16 entries");

inline constexpr int kDefaultDivision = 8; // "1/4"

constexpr double modifierScale (Modifier m) noexcept
{
    return m == Modifier::Dotted ? 1.5 : (m == Modifier::Triplet ? 2.0 / 3.0 : 1.0);
}

constexpr bool isValidIndex (int i) noexcept { return i >= 0 && i < kNumDivisions; }

constexpr int clampIndex (int i) noexcept
{
    return i < 0 ? 0 : (i >= kNumDivisions ? kNumDivisions - 1 : i);
}

// Duration of division i in whole notes / quarter notes (musical, tempo-free).
constexpr double wholeNotes (int i) noexcept
{
    return (double (kDivisions[clampIndex (i)].num) / double (kDivisions[clampIndex (i)].den))
           * modifierScale (kDivisions[clampIndex (i)].mod);
}

constexpr double quarterNotes (int i) noexcept { return wholeNotes (i) * 4.0; }

inline double quarterNoteMs (double bpm) noexcept
{
    return 60000.0 / (bpm > 0.0 ? bpm : 120.0);
}

// Division → milliseconds. Signature args accepted for uniform call sites and
// future-proofing; per the frozen convention above they do not alter note-value
// durations (bpm is quarter-note-based).
inline double divisionToMs (int index, double bpm, int /*sigNum*/ = 4, int /*sigDen*/ = 4) noexcept
{
    return quarterNotes (index) * quarterNoteMs (bpm);
}

inline double msToHz (double ms) noexcept { return ms > 0.0 ? 1000.0 / ms : 0.0; }
inline double hzToMs (double hz) noexcept { return hz > 0.0 ? 1000.0 / hz : 0.0; }

inline double divisionToHz (int index, double bpm) noexcept
{
    return msToHz (divisionToMs (index, bpm));
}

// Bar-relative helpers — this is where the time signature matters.
inline double barQuarters (int sigNum, int sigDen) noexcept
{
    if (sigNum <= 0 || sigDen <= 0) { sigNum = 4; sigDen = 4; }
    return double (sigNum) * 4.0 / double (sigDen);
}

inline double barMs (double bpm, int sigNum, int sigDen) noexcept
{
    return barQuarters (sigNum, sigDen) * quarterNoteMs (bpm);
}

// Phase in [0,1) of a beat-locked cycle of the given division, from the host
// ppq position (quarter notes). Negative ppq (pre-roll) handled.
inline double phaseFromPpq (double ppq, int divIndex) noexcept
{
    const double q = quarterNotes (divIndex);
    if (q <= 0.0) return 0.0;
    double p = std::fmod (ppq / q, 1.0);
    return p < 0.0 ? p + 1.0 : p;
}

// Same, for a free-running rate in Hz locked to the transport.
inline double phaseFromPpqFreeHz (double ppq, double bpm, double hz) noexcept
{
    const double seconds = ppq * (60.0 / (bpm > 0.0 ? bpm : 120.0));
    double p = std::fmod (seconds * hz, 1.0);
    return p < 0.0 ? p + 1.0 : p;
}

inline const char* label (int index) noexcept { return kDivisions[clampIndex (index)].label; }

inline int indexOfLabel (std::string_view s) noexcept
{
    for (int i = 0; i < kNumDivisions; ++i)
        if (s == kDivisions[i].label)
            return i;
    return -1;
}

// Convenience for building the (frozen) choice list for an AudioParameterChoice.
template <typename StringArrayLike>
inline StringArrayLike labels()
{
    StringArrayLike out;
    for (int i = 0; i < kNumDivisions; ++i)
        out.add (kDivisions[i].label);
    return out;
}

} } // namespace acv2::tempo
