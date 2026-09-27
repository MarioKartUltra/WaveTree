// AudioCommonV2 dsp-header tests. Plain C++17, std-only — no JUCE.
// Usage: acv2_dsp_tests [tempo|lfo|seed|units|all]

#include "acv2/dsp/TempoSync.h"
#include "acv2/dsp/Lfo.h"
#include "acv2/dsp/SeedPolicy.h"
#include "acv2/dsp/Units.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

static int gChecks = 0, gFailures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++gChecks;                                                             \
        if (!(cond)) {                                                         \
            ++gFailures;                                                       \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                      \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                  \
    do {                                                                       \
        ++gChecks;                                                             \
        const double va_ = (double)(a), vb_ = (double)(b);                     \
        if (!(std::fabs(va_ - vb_) <= (tol))) {                                \
            ++gFailures;                                                       \
            std::printf("FAIL %s:%d  %s=%.12g vs %s=%.12g (tol %.3g)\n",       \
                        __FILE__, __LINE__, #a, va_, #b, vb_, (double)(tol));  \
        }                                                                      \
    } while (0)

#define CHECK_EQ_STR(a, b)                                                     \
    do {                                                                       \
        ++gChecks;                                                             \
        const std::string sa_ = (a), sb_ = (b);                                \
        if (sa_ != sb_) {                                                      \
            ++gFailures;                                                       \
            std::printf("FAIL %s:%d  \"%s\" != \"%s\"\n", __FILE__, __LINE__,  \
                        sa_.c_str(), sb_.c_str());                             \
        }                                                                      \
    } while (0)

static double circDist(double a, double b)
{
    double d = std::fabs(a - b);
    d -= std::floor(d);
    return std::min(d, 1.0 - d);
}

// ───────────────────────────── tempo ─────────────────────────────────────────
static void testTempo()
{
    using namespace acv2::tempo;

    // Frozen list: order and labels.
    static const char* frozen[16] = { "1/32", "1/16T", "1/16", "1/8T", "1/16.",
                                      "1/8",  "1/4T",  "1/8.", "1/4",  "1/2T",
                                      "1/4.", "1/2",   "1/2.", "1/1",  "2/1", "4/1" };
    CHECK(kNumDivisions == 16);
    for (int i = 0; i < 16; ++i)
        CHECK_EQ_STR(label(i), frozen[i]);

    // Expected ms at 120 bpm (quarter = 500 ms, whole = 2000 ms).
    static const double ms120[16] = { 62.5,   2000.0 / 24.0, 125.0, 2000.0 / 12.0,
                                      187.5,  250.0,  2000.0 / 6.0, 375.0,
                                      500.0,  2000.0 / 3.0,  750.0, 1000.0,
                                      1500.0, 2000.0, 4000.0, 8000.0 };
    for (int i = 0; i < 16; ++i)
        CHECK_NEAR(divisionToMs(i, 120.0), ms120[i], 1e-9);

    // Several BPMs: pure 1/bpm scaling.
    for (double bpm : { 60.0, 90.0, 128.0, 174.0 })
        for (int i = 0; i < 16; ++i)
            CHECK_NEAR(divisionToMs(i, bpm), ms120[i] * 120.0 / bpm, 1e-9);

    // Dotted = 1.5x straight, triplet = 2/3x straight (via label pairs).
    struct Pair { const char* mod; const char* straight; double scale; };
    static const Pair pairs[] = { { "1/16.", "1/16", 1.5 }, { "1/8.", "1/8", 1.5 },
                                  { "1/4.", "1/4", 1.5 },   { "1/2.", "1/2", 1.5 },
                                  { "1/16T", "1/16", 2.0 / 3.0 }, { "1/8T", "1/8", 2.0 / 3.0 },
                                  { "1/4T", "1/4", 2.0 / 3.0 },   { "1/2T", "1/2", 2.0 / 3.0 } };
    for (const auto& p : pairs)
    {
        const int im = indexOfLabel(p.mod), is = indexOfLabel(p.straight);
        CHECK(im >= 0 && is >= 0);
        CHECK_NEAR(divisionToMs(im, 97.0), divisionToMs(is, 97.0) * p.scale, 1e-9);
    }

    // Strictly ascending durations.
    for (int i = 1; i < 16; ++i)
        CHECK(wholeNotes(i) > wholeNotes(i - 1));

    // Frozen convention: signature does NOT alter note-value durations
    // (host bpm is quarter-note-based) — verified at several signatures...
    for (int i = 0; i < 16; ++i)
    {
        CHECK_NEAR(divisionToMs(i, 120.0, 6, 8), divisionToMs(i, 120.0, 4, 4), 1e-12);
        CHECK_NEAR(divisionToMs(i, 120.0, 3, 4), divisionToMs(i, 120.0, 4, 4), 1e-12);
        CHECK_NEAR(divisionToMs(i, 120.0, 7, 8), divisionToMs(i, 120.0, 4, 4), 1e-12);
    }
    // ...while bar-relative math DOES depend on it.
    CHECK_NEAR(barQuarters(4, 4), 4.0, 1e-12);
    CHECK_NEAR(barQuarters(6, 8), 3.0, 1e-12);
    CHECK_NEAR(barQuarters(7, 8), 3.5, 1e-12);
    CHECK_NEAR(barMs(120.0, 4, 4), 2000.0, 1e-9);
    CHECK_NEAR(barMs(120.0, 6, 8), 1500.0, 1e-9);
    CHECK_NEAR(barMs(120.0, 3, 4), 1500.0, 1e-9);
    CHECK_NEAR(barMs(120.0, 7, 8), 1750.0, 1e-9);
    CHECK_NEAR(barMs(90.0, 4, 4), 8000.0 / 3.0, 1e-9);

    // Hz conversions.
    CHECK_NEAR(msToHz(500.0), 2.0, 1e-12);
    CHECK_NEAR(hzToMs(2.0), 500.0, 1e-12);
    CHECK_NEAR(divisionToHz(indexOfLabel("1/4"), 120.0), 2.0, 1e-9);
    CHECK_NEAR(divisionToHz(indexOfLabel("1/8"), 120.0), 4.0, 1e-9);

    // ppq → phase (beat-locked LFO grid).
    CHECK_NEAR(phaseFromPpq(5.25, indexOfLabel("1/4")), 0.25, 1e-12);
    CHECK_NEAR(phaseFromPpq(3.25, indexOfLabel("1/8")), 0.5, 1e-12);
    CHECK_NEAR(phaseFromPpq(6.0, indexOfLabel("1/1")), 0.5, 1e-12);
    CHECK_NEAR(phaseFromPpq(2.0, indexOfLabel("1/4T")), 0.0, 1e-9);
    CHECK_NEAR(phaseFromPpq(-0.25, indexOfLabel("1/4")), 0.75, 1e-12); // pre-roll
    CHECK_NEAR(phaseFromPpqFreeHz(1.5, 120.0, 2.0), 0.5, 1e-12);

    // Label round-trip + rejection.
    for (int i = 0; i < 16; ++i)
        CHECK(indexOfLabel(label(i)) == i);
    CHECK(indexOfLabel("1/128") == -1);
}

// ───────────────────────────── lfo ───────────────────────────────────────────
static acv2::Lfo makeLfo(acv2::LfoShape shape, double sr, double hz, std::uint64_t seed)
{
    acv2::Lfo l;
    l.prepare(sr);
    l.setShape(shape);
    l.setRateHz(hz);
    l.reseed(seed);
    l.reset();
    return l;
}

static void testLfo()
{
    using namespace acv2;

    static const LfoShape all[] = { LfoShape::Sine,       LfoShape::Triangle,
                                    LfoShape::SawUp,      LfoShape::SawDown,
                                    LfoShape::Square,     LfoShape::SampleHold,
                                    LfoShape::SmoothRandom, LfoShape::Drift };

    // Bounds: every shape, bipolar in [-1,1] and unipolar in [0,1].
    for (auto s : all)
    {
        auto l = makeLfo(s, 48000.0, 3.7, 1234);
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < 20000; ++i)
        {
            const float v = l.processSample();
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        CHECK(lo >= -1.0f && hi <= 1.0f);

        auto u = makeLfo(s, 48000.0, 3.7, 1234);
        u.setBipolar(false);
        lo = 1e9f; hi = -1e9f;
        for (int i = 0; i < 20000; ++i)
        {
            const float v = u.processSample();
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        CHECK(lo >= 0.0f && hi <= 1.0f);
    }

    // Period correctness: 100 Hz @ 48 kHz → 480-sample period.
    const int P = 480, NP = 10;
    for (auto s : { LfoShape::Sine, LfoShape::Triangle, LfoShape::SawUp,
                    LfoShape::SawDown, LfoShape::Square })
    {
        auto l = makeLfo(s, 48000.0, 100.0, 7);
        std::vector<float> v(P * NP);
        for (auto& x : v) x = l.processSample();

        // phase returns to ~0 after each period
        CHECK(circDist(l.phase(), 0.0) < 1e-6);

        if (s == LfoShape::Sine || s == LfoShape::Triangle)
        {
            double maxd = 0.0;
            for (int i = 0; i + P < (int)v.size(); ++i)
                maxd = std::max(maxd, (double)std::fabs(v[i] - v[i + P]));
            CHECK(maxd < 1e-6);
        }
        else // discontinuous: measure edge spacing
        {
            std::vector<int> edges;
            for (int i = 0; i + 1 < (int)v.size(); ++i)
                if (std::fabs(v[i + 1] - v[i]) > 1.5f)
                    edges.push_back(i);
            if (s == LfoShape::Square)
            {
                CHECK((int)edges.size() >= 2 * NP - 2);
                for (size_t k = 2; k < edges.size(); ++k)
                    CHECK(std::abs(edges[k] - edges[k - 2] - P) <= 1);
            }
            else
            {
                CHECK((int)edges.size() >= NP - 1);
                for (size_t k = 1; k < edges.size(); ++k)
                    CHECK(std::abs(edges[k] - edges[k - 1] - P) <= 1);
            }
        }
    }

    // Sine frequency: rising zero crossings ≈ cycles.
    {
        auto l = makeLfo(LfoShape::Sine, 48000.0, 100.0, 7);
        int crossings = 0;
        float prev = l.processSample();
        for (int i = 1; i < 48000; ++i)
        {
            const float v = l.processSample();
            if (prev < 0.0f && v >= 0.0f) ++crossings;
            prev = v;
        }
        CHECK(std::abs(crossings - 100) <= 1);
    }

    // Square pulse width: high fraction ≈ pw.
    {
        auto l = makeLfo(LfoShape::Square, 48000.0, 100.0, 7);
        l.setPulseWidth(0.25f);
        int high = 0;
        for (int i = 0; i < 48000; ++i)
            if (l.processSample() > 0.0f) ++high;
        CHECK_NEAR(high / 48000.0, 0.25, 0.01);
    }

    // S&H: constant within a cycle, new value at (nearly) every wrap.
    {
        auto l = makeLfo(LfoShape::SampleHold, 48000.0, 10.0, 99); // period 4800
        std::vector<int> changes;
        std::set<float> distinct;
        float prev = l.processSample();
        distinct.insert(prev);
        for (int i = 1; i < 4800 * 10; ++i)
        {
            const float v = l.processSample();
            if (v != prev) { changes.push_back(i); distinct.insert(v); }
            prev = v;
        }
        CHECK((int)changes.size() >= 8 && (int)changes.size() <= 10);
        for (size_t k = 1; k < changes.size(); ++k)
            CHECK(std::abs(changes[k] - changes[k - 1] - 4800) <= 1);
        CHECK((int)distinct.size() >= 8);
    }

    // SmoothRandom: continuous (one-pole) — small per-sample steps.
    {
        auto l = makeLfo(LfoShape::SmoothRandom, 48000.0, 10.0, 99);
        float prev = l.processSample();
        double maxStep = 0.0;
        for (int i = 1; i < 48000; ++i)
        {
            const float v = l.processSample();
            maxStep = std::max(maxStep, (double)std::fabs(v - prev));
            prev = v;
        }
        CHECK(maxStep < 0.05);
        CHECK(maxStep > 0.0);
    }

    // Drift: slow wander — tiny steps, but not frozen.
    {
        auto l = makeLfo(LfoShape::Drift, 48000.0, 10.0, 4242);
        float prev = l.processSample(), lo = prev, hi = prev;
        double maxStep = 0.0;
        for (int i = 1; i < 48000; ++i)
        {
            const float v = l.processSample();
            maxStep = std::max(maxStep, (double)std::fabs(v - prev));
            lo = std::min(lo, v);
            hi = std::max(hi, v);
            prev = v;
        }
        CHECK(maxStep < 0.005);
        CHECK(hi - lo > 1e-3);
    }

    // Determinism: same seed ⇒ identical random-shape output.
    for (auto s : { LfoShape::SampleHold, LfoShape::SmoothRandom, LfoShape::Drift })
    {
        auto a = makeLfo(s, 48000.0, 25.0, 777);
        auto b = makeLfo(s, 48000.0, 25.0, 777);
        bool same = true;
        for (int i = 0; i < 10000; ++i)
            same = same && (a.processSample() == b.processSample());
        CHECK(same);

        auto c = makeLfo(s, 48000.0, 25.0, 778); // different seed ⇒ different
        auto d = makeLfo(s, 48000.0, 25.0, 777);
        bool differ = false;
        for (int i = 0; i < 10000; ++i)
            differ = differ || (c.processSample() != d.processSample());
        CHECK(differ);
    }

    // Block-rate vs per-sample: same phase trajectory.
    for (auto s : { LfoShape::Sine, LfoShape::SmoothRandom })
    {
        auto a = makeLfo(s, 48000.0, 3.3, 5150);
        auto b = makeLfo(s, 48000.0, 3.3, 5150);
        const float startA = a.value();
        const float startB = b.processBlock(512); // returns value at block START
        CHECK_NEAR(startA, startB, 1e-12);
        for (int i = 0; i < 512; ++i) a.processSample();
        CHECK(circDist(a.phase(), b.phase()) < 1e-9);
        CHECK_NEAR(a.value(), b.value(), 1e-4);
    }

    // Synced rate: 1/4 @ 120 bpm = 2 Hz; 1/8T @ 120 = 6 Hz.
    {
        acv2::Lfo l;
        l.prepare(48000.0);
        l.setBpm(120.0);
        l.setSyncedDivision(acv2::tempo::indexOfLabel("1/4"));
        CHECK_NEAR(l.rateHz(), 2.0, 1e-9);
        l.setSyncedDivision(acv2::tempo::indexOfLabel("1/8T"));
        CHECK_NEAR(l.rateHz(), 6.0, 1e-9);
    }

    // Sync'd LFO phase from ppq (transport/beat-locked retrig).
    {
        acv2::Lfo l;
        l.prepare(48000.0);
        l.setShape(LfoShape::SawUp);
        l.setBpm(120.0);
        l.setRetrigMode(LfoRetrig::Transport);
        l.setSyncedDivision(acv2::tempo::indexOfLabel("1/4"));
        l.setTransportPpq(5.25);
        CHECK_NEAR(l.phase(), 0.25, 1e-12);
        CHECK_NEAR(l.value(), -0.5, 1e-9); // saw↑ bipolar at phase 0.25

        l.setSyncedDivision(acv2::tempo::indexOfLabel("1/8"));
        l.setTransportPpq(3.25);
        CHECK_NEAR(l.phase(), 0.5, 1e-12);

        l.setSyncedDivision(acv2::tempo::indexOfLabel("1/1"));
        l.setTransportPpq(6.0);
        CHECK_NEAR(l.phase(), 0.5, 1e-12);

        // free-rate transport lock: 2 Hz @ 120 bpm, ppq 1.5 = 0.75 s → phase 0.5
        acv2::Lfo f;
        f.prepare(48000.0);
        f.setBpm(120.0);
        f.setRetrigMode(LfoRetrig::Transport);
        f.setRateHz(2.0);
        f.setTransportPpq(1.5);
        CHECK_NEAR(f.phase(), 0.5, 1e-12);

        // non-transport modes ignore ppq
        acv2::Lfo g;
        g.prepare(48000.0);
        g.setRetrigMode(LfoRetrig::Free);
        g.setRateHz(2.0);
        g.setTransportPpq(1.5);
        CHECK_NEAR(g.phase(), 0.0, 1e-12);
    }

    // Key retrig resets phase; free mode does not.
    {
        auto l = makeLfo(LfoShape::SawUp, 48000.0, 100.0, 1);
        l.setRetrigMode(LfoRetrig::Key);
        for (int i = 0; i < 100; ++i) l.processSample();
        CHECK(l.phase() > 0.1);
        l.noteOn();
        CHECK_NEAR(l.phase(), 0.0, 1e-12);

        auto f = makeLfo(LfoShape::SawUp, 48000.0, 100.0, 1);
        f.setRetrigMode(LfoRetrig::Free);
        for (int i = 0; i < 100; ++i) f.processSample();
        const double ph = f.phase();
        f.noteOn();
        CHECK_NEAR(f.phase(), ph, 1e-12);
    }

    // Phase offset shifts evaluation.
    {
        auto l = makeLfo(LfoShape::SawUp, 48000.0, 1.0, 1);
        l.setPhaseOffset(0.5);
        CHECK_NEAR(l.value(), 0.0, 1e-9); // saw↑ at phase 0.5
    }
}

// ───────────────────────────── seed ──────────────────────────────────────────
static void testSeed()
{
    using namespace acv2;

    // Fixed: stable across notes and strikes.
    {
        SeedPolicy p;
        p.setMode(SeedMode::Fixed);
        p.setGlobalSeed(999);
        CHECK(p.onNoteOn(60) == 999u);
        CHECK(p.onNoteOn(72) == 999u);
        for (int i = 0; i < 50; ++i)
            CHECK(p.onNoteOn(i) == 999u);
        CHECK(p.lastUsedSeed() == 999u);
    }

    // Per-Key: same (seed, note) ⇒ same value; different notes differ.
    {
        SeedPolicy a, b;
        a.setMode(SeedMode::PerKey);
        b.setMode(SeedMode::PerKey);
        a.setGlobalSeed(0xDEADBEEF);
        b.setGlobalSeed(0xDEADBEEF);
        CHECK(a.onNoteOn(60) == b.onNoteOn(60));
        CHECK(a.onNoteOn(60) == a.onNoteOn(60)); // stable across strikes

        std::set<std::uint64_t> seen;
        for (int n = 0; n < 128; ++n)
            seen.insert(a.onNoteOn(n));
        CHECK(seen.size() == 128); // all notes pairwise distinct

        // different global seed ⇒ different per-key character
        SeedPolicy c;
        c.setMode(SeedMode::PerKey);
        c.setGlobalSeed(0xDEADBEF0);
        CHECK(c.onNoteOn(60) != a.onNoteOn(60));

        // matches the documented stateless hash
        CHECK(a.onNoteOn(64) == perKeySeed(0xDEADBEEF, 64));
    }

    // Random-on-strike: successive strikes differ; reported seed reproduces
    // the voice.
    {
        SeedPolicy p;
        p.setMode(SeedMode::RandomOnStrike);
        p.setGlobalSeed(31337);

        std::set<std::uint64_t> seeds;
        std::uint64_t prev = 0;
        for (int i = 0; i < 100; ++i)
        {
            const std::uint64_t s = p.onNoteOn(60);
            CHECK(i == 0 || s != prev);            // successive strikes differ
            CHECK(p.lastUsedSeed() == s);          // reported back to the UI
            seeds.insert(s);
            prev = s;
        }
        CHECK(seeds.size() == 100);

        // "voice" = everything derived from the seed; the reported seed must
        // reproduce it exactly.
        const std::uint64_t locked = p.onNoteOn(60);
        rng::SplitMix64 voice1 { locked };
        float rendered[8];
        for (auto& x : rendered) x = voice1.nextBipolar();

        rng::SplitMix64 voice2 { p.lastUsedSeed() };
        for (const auto& x : rendered)
            CHECK(x == voice2.nextBipolar());

        // ...including through an Lfo reseeded with the reported seed
        acv2::Lfo la, lb;
        la.prepare(48000.0); lb.prepare(48000.0);
        la.setShape(acv2::LfoShape::SmoothRandom);
        lb.setShape(acv2::LfoShape::SmoothRandom);
        la.setRateHz(30.0); lb.setRateHz(30.0);
        la.reseed(locked);  lb.reseed(p.lastUsedSeed());
        la.reset();         lb.reset();
        bool same = true;
        for (int i = 0; i < 5000; ++i)
            same = same && (la.processSample() == lb.processSample());
        CHECK(same);

        // strike stream is reproducible from the global seed (entropy optional)
        SeedPolicy q, r;
        q.setMode(SeedMode::RandomOnStrike);
        r.setMode(SeedMode::RandomOnStrike);
        q.setGlobalSeed(31337);
        r.setGlobalSeed(31337);
        bool sameStream = true;
        for (int i = 0; i < 32; ++i)
            sameStream = sameStream && (q.onNoteOn(60) == r.onNoteOn(60));
        CHECK(sameStream);

        r.setStrikeEntropy(0x12345);
        bool diverged = false;
        for (int i = 0; i < 32; ++i)
            diverged = diverged || (q.onNoteOn(60) != r.onNoteOn(60));
        CHECK(diverged);
    }
}

// ───────────────────────────── units ─────────────────────────────────────────
static void testUnits()
{
    using namespace acv2::units;

    // ms ↔ text (adaptive precision).
    CHECK_EQ_STR(msToText(2.5), "2.50 ms");
    CHECK_EQ_STR(msToText(25.0), "25.0 ms");
    CHECK_EQ_STR(msToText(250.0), "250 ms");
    CHECK_EQ_STR(msToText(999.0), "999 ms");
    CHECK_EQ_STR(msToText(1500.0), "1.50 s");
    for (double v : { 0.75, 2.5, 7.77, 25.0, 99.9, 250.0, 999.0, 1500.0, 12340.0 })
    {
        const double back = textToMs(msToText(v));
        CHECK_NEAR(back, v, v * 0.006 + 0.006); // within printed precision
    }
    CHECK_NEAR(textToMs("500"), 500.0, 1e-12);      // bare number = ms
    CHECK_NEAR(textToMs("0.5 s"), 500.0, 1e-12);
    CHECK_NEAR(textToMs("12 ms"), 12.0, 1e-12);
    CHECK_NEAR(textToMs("1.5s"), 1500.0, 1e-12);

    // Hz ↔ text.
    CHECK_EQ_STR(hzToText(440.0), "440.0 Hz");
    CHECK_EQ_STR(hzToText(55.5), "55.50 Hz");
    CHECK_EQ_STR(hzToText(1250.0), "1.25 kHz");
    for (double v : { 0.25, 5.5, 55.5, 440.0, 999.0, 1250.0, 18500.0 })
    {
        const double back = textToHz(hzToText(v));
        CHECK_NEAR(back, v, v * 0.006 + 0.006);
    }
    CHECK_NEAR(textToHz("2 kHz"), 2000.0, 1e-12);
    CHECK_NEAR(textToHz("440"), 440.0, 1e-12);

    // Note names (C4 = 60): full round trip.
    CHECK_EQ_STR(noteName(60), "C4");
    CHECK_EQ_STR(noteName(69), "A4");
    CHECK_EQ_STR(noteName(0), "C-1");
    CHECK_EQ_STR(noteName(61), "C#4");
    for (int n = 0; n <= 127; ++n)
        CHECK(noteNameToMidi(noteName(n)) == n);
    CHECK(noteNameToMidi("Db3") == 49); // flats accepted
    CHECK(noteNameToMidi(" a4 ") == 69); // case/space tolerant
    CHECK(noteNameToMidi("H2") == -1);
    CHECK(noteNameToMidi("") == -1);

    // Beat labels ↔ frozen division indices.
    for (int i = 0; i < acv2::tempo::kNumDivisions; ++i)
        CHECK(beatLabelToIndex(beatLabel(i)) == i);
    CHECK(beatLabelToIndex(" 1/8T ") == 3);
    CHECK(beatLabelToIndex("3/7") == -1);

    // Percent (mod depths shown ±%).
    CHECK_EQ_STR(percentToText(0.45), "45 %");
    CHECK_EQ_STR(percentToText(0.45, true), "+45 %");
    CHECK_EQ_STR(percentToText(-0.30, true), "-30 %");
    // A value that rounds to zero carries no sign: a depth knob at rest reads "0 %".
    CHECK_EQ_STR(percentToText(0.0, true), "0 %");
    CHECK_EQ_STR(percentToText(-0.0, true), "0 %");
    CHECK_EQ_STR(percentToText(-1.0e-7, true), "0 %");
    CHECK_EQ_STR(percentToText(0.004, true), "0 %");
    CHECK_EQ_STR(percentToText(-0.004, false), "0 %");
    CHECK_EQ_STR(percentToText(0.006, true), "+1 %");
    CHECK_EQ_STR(percentToText(-0.006, true), "-1 %");
    CHECK_NEAR(textToPercent("45 %"), 0.45, 1e-12);
    CHECK_NEAR(textToPercent("-30 %"), -0.30, 1e-12);

    // dB.
    CHECK_EQ_STR(dbToText(-6.0), "-6.0 dB");
    CHECK_EQ_STR(dbToText(0.0), "0.0 dB");
    CHECK_EQ_STR(dbToText(-120.0), "-inf dB");
    CHECK_NEAR(textToDb("-6.0 dB"), -6.0, 1e-12);
    CHECK_NEAR(textToDb("-inf dB"), -96.0, 1e-12);

    // Semitones.
    CHECK_EQ_STR(semitonesToText(3.0), "+3.0 st");
    CHECK_EQ_STR(semitonesToText(-1.5), "-1.5 st");
    CHECK_NEAR(textToSemitones("+3.0 st"), 3.0, 1e-12);
}

// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char** argv)
{
    const std::string suite = argc > 1 ? argv[1] : "all";

    if (suite == "tempo" || suite == "all") testTempo();
    if (suite == "lfo"   || suite == "all") testLfo();
    if (suite == "seed"  || suite == "all") testSeed();
    if (suite == "units" || suite == "all") testUnits();

    if (gChecks == 0)
    {
        std::printf("unknown suite '%s'\n", suite.c_str());
        return 2;
    }
    std::printf("[%s] %d checks, %d failures\n", suite.c_str(), gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
