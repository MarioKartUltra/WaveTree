#pragma once
// acv2/dsp/Units.h — value↔text for v2 parameter readouts.
// Header-only, C++17, std-only. All v2 sliders MUST display real units — never
// bare 0..1. Wire these into APVTS parameter textFromValue/valueFromText
// lambdas (or juce::Slider::textFromValueFunction) at parameter-creation time.
//
// Covered: ms (adaptive precision, seconds above 1000 ms), Hz (kHz above 1000),
// MIDI note names (C4 = 60), beat-division labels (frozen list in TempoSync.h),
// percent (signed option for ±depth), dB, semitones.

#include "TempoSync.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace acv2 { namespace units {

namespace detail
{
    template <typename... Args>
    inline std::string fmt (const char* f, Args... args)
    {
        char buf[64];
        std::snprintf (buf, sizeof (buf), f, args...);
        return std::string (buf);
    }

    inline double parseNumber (std::string_view s, const char** endOut = nullptr)
    {
        // strtod on a bounded copy (string_view need not be null-terminated)
        char buf[64];
        const size_t n = s.size() < sizeof (buf) - 1 ? s.size() : sizeof (buf) - 1;
        s.copy (buf, n);
        buf[n] = '\0';
        char* end = nullptr;
        const double v = std::strtod (buf, &end);
        if (endOut != nullptr)
            *endOut = s.data() + (end - buf);
        return v;
    }

    inline std::string_view trim (std::string_view s)
    {
        while (! s.empty() && std::isspace ((unsigned char) s.front())) s.remove_prefix (1);
        while (! s.empty() && std::isspace ((unsigned char) s.back()))  s.remove_suffix (1);
        return s;
    }

    inline bool contains (std::string_view s, char c)
    {
        return s.find (c) != std::string_view::npos;
    }
}

// ── milliseconds (adaptive precision; ≥ 1 s shown in seconds) ──────────────
inline std::string msToText (double ms)
{
    if (ms >= 1000.0) return detail::fmt ("%.2f s", ms / 1000.0);
    if (ms >= 100.0)  return detail::fmt ("%.0f ms", ms);
    if (ms >= 10.0)   return detail::fmt ("%.1f ms", ms);
    return detail::fmt ("%.2f ms", ms);
}

inline double textToMs (std::string_view text)
{
    const char* end = nullptr;
    const double v = detail::parseNumber (text, &end);
    std::string_view suffix = detail::trim (text.substr (size_t (end - text.data())));
    const bool seconds = ! suffix.empty()
                         && (suffix[0] == 's' || suffix[0] == 'S')
                         && ! (suffix.size() > 1 && (suffix[1] == 'm' || suffix[1] == 'M')); // "s" not "ms"
    return seconds ? v * 1000.0 : v; // bare numbers are ms
}

// ── Hertz (kHz above 1000) ─────────────────────────────────────────────────
inline std::string hzToText (double hz)
{
    if (hz >= 1000.0) return detail::fmt ("%.2f kHz", hz / 1000.0);
    if (hz >= 100.0)  return detail::fmt ("%.1f Hz", hz);
    return detail::fmt ("%.2f Hz", hz);
}

inline double textToHz (std::string_view text)
{
    const char* end = nullptr;
    const double v = detail::parseNumber (text, &end);
    std::string_view suffix = detail::trim (text.substr (size_t (end - text.data())));
    const bool kilo = ! suffix.empty() && (suffix[0] == 'k' || suffix[0] == 'K');
    return kilo ? v * 1000.0 : v;
}

// ── MIDI note names (C4 = 60) ──────────────────────────────────────────────
inline std::string noteName (int midiNote)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F",
                                     "F#", "G", "G#", "A", "A#", "B" };
    const int n = midiNote < 0 ? 0 : (midiNote > 127 ? 127 : midiNote);
    return std::string (names[n % 12]) + std::to_string (n / 12 - 1);
}

// "A4" → 69, "C#-1" → 1, flats accepted ("Db3"); returns -1 if unparsable.
inline int noteNameToMidi (std::string_view text)
{
    std::string_view s = detail::trim (text);
    if (s.empty()) return -1;

    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    const char c = char (std::toupper ((unsigned char) s[0]));
    if (c < 'A' || c > 'G') return -1;
    int semis = base[c - 'A'];
    s.remove_prefix (1);

    if (! s.empty() && (s[0] == '#' || s[0] == 'b'))
    {
        semis += (s[0] == '#') ? 1 : -1;
        s.remove_prefix (1);
    }
    if (s.empty()) return -1;

    const char* end = nullptr;
    const double oct = detail::parseNumber (s, &end);
    if (end == s.data()) return -1;

    const int midi = (int (oct) + 1) * 12 + semis;
    return (midi >= 0 && midi <= 127) ? midi : -1;
}

// ── beat divisions (frozen list — see TempoSync.h) ─────────────────────────
inline std::string beatLabel (int divIndex)          { return tempo::label (divIndex); }
inline int         beatLabelToIndex (std::string_view s) { return tempo::indexOfLabel (detail::trim (s)); }

// ── percent (signedPercent for ±mod-depth readouts) ────────────────────────
inline std::string percentToText (double fraction01, bool signedPercent = false)
{
    return detail::fmt (signedPercent ? "%+.0f %%" : "%.0f %%", fraction01 * 100.0);
}

inline double textToPercent (std::string_view text) // → fraction (−1..1 typical)
{
    return detail::parseNumber (text) / 100.0;
}

// ── decibels ───────────────────────────────────────────────────────────────
inline std::string dbToText (double db, double floorDb = -96.0)
{
    if (db <= floorDb) return "-inf dB";
    return detail::fmt ("%.1f dB", db);
}

inline double textToDb (std::string_view text, double floorDb = -96.0)
{
    if (detail::contains (text, 'i') || detail::contains (text, 'I')) return floorDb; // "-inf"
    return detail::parseNumber (text);
}

// ── semitones ──────────────────────────────────────────────────────────────
inline std::string semitonesToText (double st) { return detail::fmt ("%+.1f st", st); }
inline double      textToSemitones (std::string_view text) { return detail::parseNumber (text); }

} } // namespace acv2::units
