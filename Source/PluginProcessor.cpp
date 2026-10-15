// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// =============================================================================
//  WaveTree II — PluginProcessor.cpp
// =============================================================================
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

using APVTS = juce::AudioProcessorValueTreeState;
using namespace wavetree;
namespace units = acv2::units;
namespace tempo = acv2::tempo;

// -----------------------------------------------------------------------------
//  Parameter layout (all versionHint = 2; every readout in real units)
// -----------------------------------------------------------------------------
namespace
{
juce::String svToJuce (const std::string& s) { return juce::String (s); }

auto strPercent = [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
auto valPercent = [] (const juce::String& t) { return (float) (t.getFloatValue() / 100.0f); };
auto strSigned  = [] (float v, int) { return svToJuce (units::percentToText (v, true)); };
auto strMs      = [] (float v, int) { return svToJuce (units::msToText (v)); };
auto valMs      = [] (const juce::String& t) { return (float) units::textToMs (t.toStdString()); };
auto strHz      = [] (float v, int) { return svToJuce (units::hzToText (v)); };
auto valHz      = [] (const juce::String& t) { return (float) units::textToHz (t.toStdString()); };
auto strDb      = [] (float v, int) { return v <= 0.001f ? juce::String ("-inf dB")
                                                         : svToJuce (units::dbToText (20.0 * std::log10 (v))); };
auto valDb      = [] (const juce::String& t) { return (float) std::pow (10.0, units::textToDb (t.toStdString()) / 20.0); };
auto strDeg     = [] (float v, int) { return juce::String (juce::roundToInt (v * 360.0f)) + juce::String::fromUTF8 ("\xc2\xb0"); };
auto valDeg     = [] (const juce::String& t) { return (float) (t.getFloatValue() / 360.0f); };

const juce::StringArray kShapeNames  { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Smooth Rnd", "Drift" };
const juce::StringArray kRetrigNames { "Free", "Key", "Transport" };
const char* kModDests[] = { pid::hurst, pid::interm, pid::tilt, pid::density, pid::output };

// factory presets: sparse {paramID, real value} on top of defaults
struct FactoryPreset { const char* name; std::vector<std::pair<const char*, float>> v; };
const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> k = {
        { "Init", {} },
        { "Fractal Pad", { { pid::mode, 1 }, { pid::hurst, 0.78f }, { pid::interm, 0.22f },
            { pid::attackMs, 420.0f }, { pid::releaseMs, 1400.0f }, { pid::sustain, 0.85f },
            { pid::cutoffHz, 8500.0f }, { pid::morphOn, 1 }, { pid::morphMs, 2600.0f },
            { "hurstModA", 0.35f }, { "lfoARateMs", 3200.0f }, { pid::output, 0.55f } } },
        { "Grit Cascade", { { pid::mode, 1 }, { pid::hurst, 0.32f }, { pid::interm, 0.85f },
            { pid::decayMs, 380.0f }, { pid::sustain, 0.45f }, { pid::cutoffHz, 6200.0f },
            { pid::reso, 0.4f }, { pid::wavelet, 1 }, { "intermittencyModB", 0.3f },
            { "lfoBShape", 6 }, { "lfoBRateMs", 900.0f } } },
        { "Drawbar Organ", { { pid::mode, 0 }, { pid::tilt, 0.45f }, { pid::formant, 0.35f },
            { pid::bandQ, 0.72f }, { pid::attackMs, 8.0f }, { pid::sustain, 0.9f },
            { pid::releaseMs, 140.0f }, { pid::cutoffHz, 12000.0f }, { pid::wavelet, 3 } } },
        { "Formant Bell", { { pid::mode, 0 }, { pid::tilt, 0.6f }, { pid::formant, 0.75f },
            { pid::bandQ, 0.28f }, { pid::decayMs, 1900.0f }, { pid::sustain, 0.0f },
            { pid::releaseMs, 1200.0f }, { pid::seedMode, 1 }, { "tiltModA", 0.25f },
            { "lfoARateSync", 1 }, { "lfoARateDiv", 11 } } },
        { "Atom Rain", { { pid::mode, 2 }, { pid::density, 0.12f }, { pid::morphOn, 1 },
            { pid::morphSync, 1 }, { pid::morphDiv, 8 }, { pid::seedMode, 2 },
            { "densityModA", 0.5f }, { "lfoAShape", 5 }, { "lfoARateSync", 1 },
            { "lfoARateDiv", 5 }, { pid::releaseMs, 600.0f } } },
        { "Sub Fractal", { { pid::mode, 1 }, { pid::hurst, 0.92f }, { pid::interm, 0.1f },
            { pid::cutoffHz, 1900.0f }, { pid::output, 0.9f }, { pid::attackMs, 30.0f },
            { pid::wavelet, 0 }, { pid::voices, 4 } } },
        { "Violet Choir", { { pid::mode, 1 }, { pid::hurst, 0.65f }, { pid::interm, 0.4f },
            { pid::seedMode, 1 }, { pid::attackMs, 260.0f }, { pid::releaseMs, 900.0f },
            { pid::sustain, 0.8f }, { "hurstModA", 0.3f }, { "tiltModB", -0.2f },
            { "lfoBShape", 7 }, { "lfoBRateMs", 5000.0f }, { pid::morphOn, 1 },
            { pid::morphMs, 4000.0f }, { pid::cutoffHz, 9000.0f } } },
    };
    return k;
}
} // namespace

APVTS::ParameterLayout WaveTreeProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    using FA = juce::AudioParameterFloatAttributes;

    auto f = [&] (const juce::String& id, const juce::String& nm, juce::NormalisableRange<float> r,
                  float d, auto strFn, auto valFn)
    { p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 2 }, nm, r, d,
          FA().withStringFromValueFunction (strFn).withValueFromStringFunction (valFn))); };
    auto u = [&] (const juce::String& id, const juce::String& nm, float d)
    { f (id, nm, { 0.0f, 1.0f, 0.0001f }, d, strPercent, valPercent); };
    auto c = [&] (const juce::String& id, const juce::String& nm, const juce::StringArray& ch, int d)
    { p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 2 }, nm, ch, d)); };
    auto b = [&] (const juce::String& id, const juce::String& nm, bool d)
    { p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 2 }, nm, d)); };
    auto syncGroup = [&] (const juce::String& base, const juce::String& nm,
                          juce::NormalisableRange<float> msRange, float msDef, int divDef)
    {
        b (base + "Sync", nm + " Sync", false);
        f (base + "Ms",   nm + " Time", msRange, msDef, strMs, valMs);
        c (base + "Div",  nm + " Beat", tempo::labels<juce::StringArray>(), divDef);
    };

    c (pid::wavelet,  "Wavelet", { "Haar", "Db2", "Db4", "Db6", "Db10", "Sym4", "Sym8", "Coif2", "Coif4",
                                   "Bior2.2", "Bior4.4", "Bior6.8" }, 2);
    c (pid::mode,     "Mode", { "Bands", "Cascade", "Atoms" }, 1);
    p.push_back (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { pid::seed, 2 }, "Seed", 0, 9999, 1));
    c (pid::seedMode, "Seed Policy", { "Fixed", "Per-Key", "Random" }, 1);
    b (pid::antiAlias, "Anti-Alias", true);
    b (pid::morphOn,   "Morph", false);
    syncGroup ("morph", "Morph", { 100.0f, 8000.0f, 0.0f, 0.4f }, 1200.0f, 13);

    u (pid::tilt,    "Tilt", 0.5f);
    u (pid::formant, "Formant", 0.5f);
    u (pid::bandQ,   "Band Q", 0.5f);
    u (pid::hurst,   "Hurst", 0.6f);
    u (pid::interm,  "Intermittency", 0.4f);
    u (pid::density, "Density", 0.4f);

    syncGroup ("attack",  "Attack",  { 1.0f, 2000.0f, 0.0f, 0.35f }, 5.0f, 2);
    syncGroup ("decay",   "Decay",   { 5.0f, 3000.0f, 0.0f, 0.35f }, 200.0f, 8);
    u (pid::sustain, "Sustain", 0.7f);
    syncGroup ("release", "Release", { 5.0f, 4000.0f, 0.0f, 0.35f }, 300.0f, 8);

    f (pid::cutoffHz, "Cutoff", { 40.0f, 20000.0f, 0.0f, 0.25f }, 12000.0f, strHz, valHz);
    u (pid::reso,     "Resonance", 0.15f);
    f (pid::output,   "Output", { 0.0f, 2.0f, 0.001f }, 0.7f, strDb, valDb);
    p.push_back (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { pid::voices, 2 }, "Polyphony", 1, 16, 8));
    syncGroup ("glide", "Glide", { 0.0f, 2000.0f, 0.0f, 0.4f }, 0.0f, 8);
    b (pid::legato, "Legato", false);

    for (auto* prefix : { "lfoA", "lfoB" })
    {
        const juce::String pre (prefix);
        const juce::String nm = pre == "lfoA" ? "LFO A " : "LFO B ";
        c (pre + "Shape", nm + "Shape", kShapeNames, 0);
        syncGroup (pre + "Rate", nm + "Rate", { 20.0f, 10000.0f, 0.0f, 0.35f }, 500.0f, 8);
        f (pre + "Phase", nm + "Phase", { 0.0f, 1.0f, 0.001f }, 0.0f, strDeg, valDeg);
        c (pre + "Retrig", nm + "Retrig", kRetrigNames, 0);
        b (pre + "Bipolar", nm + "Bipolar", true);
    }

    for (auto* dst : kModDests)
        for (auto* src : { "ModA", "ModB" })
            f (juce::String (dst) + src, juce::String (dst) + " " + src,
               { -1.0f, 1.0f, 0.001f }, 0.0f, strSigned, valPercent);

    return { p.begin(), p.end() };
}

// -----------------------------------------------------------------------------
WaveTreeProcessor::WaveTreeProcessor()
    : foleys::MagicProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "WAVETREE2", createLayout())
{
    FOLEYS_SET_SOURCE_PATH (__FILE__);
    cacheRawPointers();
    magicState.createAndAddObject<foleys::MagicLevelSource> ("output");
    apvts.addParameterListener (pid::mode, this);
    handleAsyncUpdate();                         // initial view visibility, set before any editor exists
}

WaveTreeProcessor::~WaveTreeProcessor()
{
    apvts.removeParameterListener (pid::mode, this);
    cancelPendingUpdate();
}

// -----------------------------------------------------------------------------
float WaveTreeProcessor::rawValue (const juce::String& id) const
{
    if (auto* v = apvts.getRawParameterValue (id)) return v->load();
    return 0.0f;
}

void WaveTreeProcessor::cacheRawPointers()
{
    auto g = [this] (const juce::String& id)
    {
        auto* p = apvts.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };
    raw.wavelet = g (pid::wavelet);   raw.mode = g (pid::mode);
    raw.seed = g (pid::seed);         raw.seedMode = g (pid::seedMode);
    raw.antiAlias = g (pid::antiAlias);
    raw.morphOn = g (pid::morphOn);   raw.morphSync = g (pid::morphSync);
    raw.morphMs = g (pid::morphMs);   raw.morphDiv = g (pid::morphDiv);
    raw.tilt = g (pid::tilt);         raw.formant = g (pid::formant);
    raw.bandQ = g (pid::bandQ);       raw.hurst = g (pid::hurst);
    raw.interm = g (pid::interm);     raw.density = g (pid::density);
    raw.attackSync = g (pid::attackSync); raw.attackMs = g (pid::attackMs); raw.attackDiv = g (pid::attackDiv);
    raw.decaySync = g (pid::decaySync);   raw.decayMs = g (pid::decayMs);   raw.decayDiv = g (pid::decayDiv);
    raw.sustain = g (pid::sustain);
    raw.releaseSync = g (pid::releaseSync); raw.releaseMs = g (pid::releaseMs); raw.releaseDiv = g (pid::releaseDiv);
    raw.cutoffHz = g (pid::cutoffHz); raw.reso = g (pid::reso);
    raw.output = g (pid::output);     raw.voices = g (pid::voices);
    raw.glideSync = g (pid::glideSync); raw.glideMs = g (pid::glideMs); raw.glideDiv = g (pid::glideDiv);
    raw.legato = g (pid::legato);

    const char* pres[2] = { "lfoA", "lfoB" };
    for (int i = 0; i < 2; ++i)
    {
        const juce::String pre (pres[i]);
        raw.lfo[i] = { g (pre + "Shape"), g (pre + "RateSync"), g (pre + "RateMs"),
                       g (pre + "RateDiv"), g (pre + "Phase"), g (pre + "Retrig"),
                       g (pre + "Bipolar") };
    }
    for (int d = 0; d < 5; ++d)
    {
        raw.mod[d][0] = g (juce::String (kModDests[d]) + "ModA");
        raw.mod[d][1] = g (juce::String (kModDests[d]) + "ModB");
    }
}

double WaveTreeProcessor::resolveMs (PF sync, PF ms, PF div, double bpm)
{
    if (sync->load() > 0.5f)
        return tempo::divisionToMs ((int) div->load(), bpm);
    return (double) ms->load();
}

void WaveTreeProcessor::configureLfo (acv2::Lfo& lfo, const LfoPtrs& p, double bpm) const
{
    lfo.setShape ((acv2::LfoShape) (int) p.shape->load());
    lfo.setBipolar (p.bipolar->load() > 0.5f);
    lfo.setPhaseOffset ((double) p.phase->load());
    lfo.setRetrigMode ((acv2::LfoRetrig) (int) p.retrig->load());
    lfo.setBpm (bpm);
    if (p.rateSync->load() > 0.5f)
        lfo.setSyncedDivision ((int) p.rateDiv->load());
    else
        lfo.setRateHz (tempo::msToHz ((double) p.rateMs->load()));
}

WaveTreeParams WaveTreeProcessor::paramsFromState (float a, float bv, double bpm) const
{
    // mod index order matches kModDests: hurst, interm, tilt, density, output
    auto modded = [&] (PF base, int d, double lo, double hi)
    {
        const double v = (double) base->load()
                       + (double) raw.mod[d][0]->load() * (double) a
                       + (double) raw.mod[d][1]->load() * (double) bv;
        return juce::jlimit (lo, hi, v);
    };

    WaveTreeParams m;
    m.wavelet  = (WaveletType) (int) raw.wavelet->load();
    m.mode     = (Mode) (int) raw.mode->load();
    m.seed     = (unsigned) raw.seed->load();
    m.seedMode = (SeedModeP) (int) raw.seedMode->load();
    m.antiAlias = raw.antiAlias->load() > 0.5f;
    m.morphOn   = raw.morphOn->load() > 0.5f;
    m.morphSeconds = resolveMs (raw.morphSync, raw.morphMs, raw.morphDiv, bpm) * 0.001;

    m.tilt    = modded (raw.tilt, 2, 0.0, 1.0);
    m.formant = raw.formant->load(); m.bandQ = raw.bandQ->load();
    m.hurst   = modded (raw.hurst, 0, 0.0, 1.0);
    m.intermittency = modded (raw.interm, 1, 0.0, 1.0);
    m.density = modded (raw.density, 3, 0.02, 1.0);

    m.attack  = resolveMs (raw.attackSync, raw.attackMs, raw.attackDiv, bpm) * 0.001;
    m.decay   = resolveMs (raw.decaySync, raw.decayMs, raw.decayDiv, bpm) * 0.001;
    m.sustain = raw.sustain->load();
    m.release = resolveMs (raw.releaseSync, raw.releaseMs, raw.releaseDiv, bpm) * 0.001;

    m.cutoffHz = raw.cutoffHz->load();
    m.reso     = raw.reso->load();
    m.output   = modded (raw.output, 4, 0.0, 2.0);
    m.numVoices = (int) raw.voices->load();
    m.glideSeconds = resolveMs (raw.glideSync, raw.glideMs, raw.glideDiv, bpm) * 0.001;
    m.legato = raw.legato->load() > 0.5f;
    return m;
}

// -----------------------------------------------------------------------------
void WaveTreeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    synth.prepare (sampleRate);
    lfoA.prepare (sampleRate);
    lfoB.prepare (sampleRate);
    lfoA.reseed (0x51A5EEDAull);                 // deterministic random shapes
    lfoB.reseed (0x51A5EEDBull);
    synth.setParameters (paramsFromState (lfoA.value(), lfoB.value(), lastBpm));

    magicState.prepareToPlay (sampleRate, samplesPerBlock);
    if (auto* lvl = magicState.getObjectWithType<foleys::MagicLevelSource> ("output"))
        lvl->setupSource (getTotalNumOutputChannels(), sampleRate, 500);
}

bool WaveTreeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void WaveTreeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    // transport (cache last-known bpm, default 120)
    double bpm = lastBpm, ppq = 0.0; bool hasPpq = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bb = pos->getBpm())          bpm = lastBpm = *bb;
            if (auto q  = pos->getPpqPosition()) { ppq = *q; hasPpq = true; }
        }

    configureLfo (lfoA, raw.lfo[0], bpm);
    configureLfo (lfoB, raw.lfo[1], bpm);
    if (hasPpq) { lfoA.setTransportPpq (ppq); lfoB.setTransportPpq (ppq); }

    // inject on-screen keyboard notes, then route MIDI (block-granular timing)
    magicState.processMidiBuffer (midi, n, true);
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())            { synth.noteOn (m.getNoteNumber(), m.getFloatVelocity());
                                       lfoA.noteOn(); lfoB.noteOn(); }
        else if (m.isNoteOff())      synth.noteOff (m.getNoteNumber());
        else if (m.isPitchWheel())   synth.pitchBend ((m.getPitchWheelValue() - 8192) / 8192.0 * 2.0);
        else if (m.isAllNotesOff() || m.isAllSoundOff()) synth.panic();
    }

    // one block-rate value per LFO, published for the mod rings
    const float a  = lfoA.processBlock (n);
    const float bv = lfoB.processBlock (n);
    lfoAValue.store (a, std::memory_order_relaxed);
    lfoBValue.store (bv, std::memory_order_relaxed);

    synth.setParameters (paramsFromState (a, bv, bpm));

    buffer.clear();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    synth.renderBlock (L, R, n);

    if (auto* lvl = magicState.getObjectWithType<foleys::MagicLevelSource> ("output"))
        lvl->pushSamples (buffer);
}

// -----------------------------------------------------------------------------
//  foleys GUI
// -----------------------------------------------------------------------------
void WaveTreeProcessor::initialiseBuilder (foleys::MagicGUIBuilder& builder)
{
    builder.registerJUCEFactories();
    builder.registerJUCELookAndFeels();
    builder.registerLookAndFeel ("LnfV2", std::make_unique<acv2::ui::LnfV2> (acv2::theme::kWaveTree));
    registerWaveTreeGuiItems (builder);          // PluginEditor.h factories
}

juce::ValueTree WaveTreeProcessor::createGuiValueTree()
{
    const auto xml = juce::String::fromUTF8 (BinaryData::magic_xml, BinaryData::magic_xmlSize);
    auto tree = juce::ValueTree::fromXml (xml);
    return tree.isValid() ? tree : MagicProcessor::createGuiValueTree();
}

void WaveTreeProcessor::postSetStateInformation()
{
    loadTreeFromState();
    triggerAsyncUpdate();
}

void WaveTreeProcessor::parameterChanged (const juce::String&, float) { triggerAsyncUpdate(); }

void WaveTreeProcessor::handleAsyncUpdate()
{
    const int m = (int) rawValue (pid::mode);
    magicState.getPropertyAsValue ("viewBands").setValue (m == 0);
    magicState.getPropertyAsValue ("viewCascade").setValue (m == 1);
    magicState.getPropertyAsValue ("viewAtoms").setValue (m == 2);
}

// -----------------------------------------------------------------------------
//  seed dice / lock
// -----------------------------------------------------------------------------
static void setParamReal (APVTS& apvts, const char* id, float realValue)
{
    if (auto* par = apvts.getParameter (id))
    {
        par->beginChangeGesture();
        par->setValueNotifyingHost (par->convertTo0to1 (realValue));
        par->endChangeGesture();
    }
}

void WaveTreeProcessor::rollSeed()
{ setParamReal (apvts, pid::seed, (float) juce::Random::getSystemRandom().nextInt (10000)); }

void WaveTreeProcessor::lockShownSeed()
{
    setParamReal (apvts, pid::seed, (float) shownSeed());
    setParamReal (apvts, pid::seedMode, 0.0f);   // Fixed
}

// -----------------------------------------------------------------------------
//  TreeEdit persistence (compact sparse string in the magicState property root)
// -----------------------------------------------------------------------------
void WaveTreeProcessor::treeEditGesture() { saveTreeToState(); }

void WaveTreeProcessor::saveTreeToState()
{
    auto& t = synth.treeEdit();
    juce::String s;
    for (int b = 0; b < wavetree::TreeEdit::kMaxBands; ++b)
        if (std::abs (t.getBandGain (b) - 1.0f) > 0.001f)
            s << "B," << b << "," << juce::String (t.getBandGain (b), 3) << ";";
    for (int i = 0; i < wavetree::TreeEdit::kMaxNodes; ++i)
    {
        if (t.getNodeLevel (i) >= 0.0f)                       // L = a painted level, and the slope it was painted at
            s << "L," << i << "," << juce::String (t.getNodeLevel (i), 3) << "," << juce::String (t.getNodeSlope (i), 3) << ";";
        else if (std::abs (t.getNodeGain (i) - 1.0f) > 0.001f)   // N = a gain, from older projects
            s << "N," << i << "," << juce::String (t.getNodeGain (i), 3) << ";";
    }
    magicState.getPropertyRoot().setProperty ("treeGains", s, nullptr);
}

void WaveTreeProcessor::loadTreeFromState()
{
    auto& t = synth.treeEdit();
    t.reset();
    const auto s = magicState.getPropertyRoot().getProperty ("treeGains", juce::String()).toString();
    // a level saved before paint slopes existed was drawn on the flat scale: it
    // takes the slope the project opens with, so it sounds as it was saved
    const float nowSlope = (float) wavetree::treeSlope (paramsFromState (0.0f, 0.0f, lastBpm));
    for (auto& tok : juce::StringArray::fromTokens (s, ";", {}))
    {
        auto parts = juce::StringArray::fromTokens (tok, ",", {});
        if (parts.size() != 3 && ! (parts.size() == 4 && parts[0] == "L")) continue;
        const int idx = parts[1].getIntValue();
        const float g = parts[2].getFloatValue();
        if (parts[0] == "B") t.setBandGain (idx, g);
        else if (parts[0] == "N") t.setNodeGain (idx, g);
        else if (parts[0] == "L") t.setNodeLevel (idx, g, parts.size() == 4 ? parts[3].getFloatValue() : nowSlope);
    }
    t.bump();
}

// -----------------------------------------------------------------------------
//  presets
// -----------------------------------------------------------------------------
juce::File WaveTreeProcessor::presetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
             .getChildFile ("WaveTree II").getChildFile ("Presets");
}

int WaveTreeProcessor::numFactoryPresets() const { return (int) factoryPresets().size(); }

juce::StringArray WaveTreeProcessor::getPresetNames() const
{
    juce::StringArray names;
    for (auto& fp : factoryPresets()) names.add (fp.name);
    auto files = presetDirectory().findChildFiles (juce::File::findFiles, false, "*.xml");
    files.sort();
    for (auto& f : files) names.add (f.getFileNameWithoutExtension());
    return names;
}

void WaveTreeProcessor::applyPreset (int index)
{
    auto resetToDefaults = [this]
    {
        for (auto* param : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
                rp->setValueNotifyingHost (rp->getDefaultValue());
        synth.treeEdit().reset();
        saveTreeToState();
    };

    const auto& fps = factoryPresets();
    if (index >= 0 && index < (int) fps.size())
    {
        resetToDefaults();
        for (auto& [id, value] : fps[(size_t) index].v)
            setParamReal (apvts, id, value);
        return;
    }

    const int userIdx = index - (int) fps.size();
    auto files = presetDirectory().findChildFiles (juce::File::findFiles, false, "*.xml");
    files.sort();
    if (userIdx >= 0 && userIdx < files.size())
        if (auto xml = juce::XmlDocument::parse (files[userIdx]))
        {
            resetToDefaults();
            applyPresetValues (*xml);
        }
}

void WaveTreeProcessor::applyPresetValues (const juce::XmlElement& xml)
{
    for (int i = 0; i < xml.getNumAttributes(); ++i)
    {
        const auto id = xml.getAttributeName (i);
        if (id == "treeGains")
        {
            magicState.getPropertyRoot().setProperty ("treeGains", xml.getAttributeValue (i), nullptr);
            loadTreeFromState();
            continue;
        }
        if (auto* par = apvts.getParameter (id))
            par->setValueNotifyingHost (par->convertTo0to1 (xml.getAttributeValue (i).getFloatValue()));
    }
}

bool WaveTreeProcessor::saveUserPreset (const juce::String& name)
{
    auto safe = juce::File::createLegalFileName (name.trim());
    if (safe.isEmpty()) return false;
    auto dir = presetDirectory();
    dir.createDirectory();

    juce::XmlElement xml ("WTPRESET");
    for (auto* param : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
            xml.setAttribute (rp->paramID, rp->convertFrom0to1 (rp->getValue()));
    saveTreeToState();
    xml.setAttribute ("treeGains",
                      magicState.getPropertyRoot().getProperty ("treeGains", juce::String()).toString());
    return xml.writeTo (dir.getChildFile (safe + ".xml"));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new WaveTreeProcessor(); }
