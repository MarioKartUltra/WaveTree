// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// =============================================================================
//  WaveTree II — PluginProcessor.h
//  A synthesizer whose premise is the discrete wavelet transform: fill a wavelet
//  coefficient tree (Bands / multifractal Cascade / Atoms), sculpt it in the
//  interactive tree editor, inverse-DWT it into a bank of band-limited "wavelet
//  wavetables", and play it with a resampling oscillator.
//
//  v2: foleys::MagicProcessor (WYSIWYG GUI layer), LFO A/B mod matrix resolved
//  HERE (block rate, bounded offsets before the Params struct — the DSP core
//  stays JUCE-free), the three-param tempo-sync convention on every time param,
//  seed policy (Fixed / Per-Key / Random-on-strike) with dice + lock, and a
//  named-preset system. versionHint = 2 on every parameter.
// =============================================================================
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <foleys_gui_magic/foleys_gui_magic.h>
#include "dsp/WaveletSynth.h"
#include "common/acv2/dsp/Lfo.h"
#include "common/acv2/dsp/TempoSync.h"
#include "common/acv2/dsp/Units.h"

namespace pid
{
    // synthesis
    inline constexpr auto wavelet   = "wavelet";
    inline constexpr auto mode      = "mode";
    inline constexpr auto seed      = "seed";
    inline constexpr auto seedMode  = "seedMode";
    inline constexpr auto antiAlias = "antiAlias";
    inline constexpr auto morphOn   = "morphOn";
    inline constexpr auto morphSync = "morphSync";
    inline constexpr auto morphMs   = "morphMs";
    inline constexpr auto morphDiv  = "morphDiv";
    // bands / cascade / atoms
    inline constexpr auto tilt      = "tilt";
    inline constexpr auto formant   = "formant";
    inline constexpr auto bandQ     = "bandQ";
    inline constexpr auto hurst     = "hurst";
    inline constexpr auto interm    = "intermittency";
    inline constexpr auto density   = "density";
    // envelope (three-param sync convention per time param)
    inline constexpr auto attackSync  = "attackSync";
    inline constexpr auto attackMs    = "attackMs";
    inline constexpr auto attackDiv   = "attackDiv";
    inline constexpr auto decaySync   = "decaySync";
    inline constexpr auto decayMs     = "decayMs";
    inline constexpr auto decayDiv    = "decayDiv";
    inline constexpr auto sustain     = "sustain";
    inline constexpr auto releaseSync = "releaseSync";
    inline constexpr auto releaseMs   = "releaseMs";
    inline constexpr auto releaseDiv  = "releaseDiv";
    // tone / out
    inline constexpr auto cutoffHz  = "cutoffHz";
    inline constexpr auto reso      = "reso";
    inline constexpr auto output    = "output";
    inline constexpr auto voices    = "voices";
    inline constexpr auto glideSync = "glideSync";
    inline constexpr auto glideMs   = "glideMs";
    inline constexpr auto glideDiv  = "glideDiv";
    inline constexpr auto legato    = "legato";
    // LFO prefixes ("lfoA" / "lfoB" + suffixes Shape, RateSync/RateMs/RateDiv,
    // Phase, Retrig, Bipolar) — built by name in createLayout/paramsFromState.
    // mod-depth params: <dst>ModA / <dst>ModB for dst in
    //   hurst, intermittency, tilt, density, output
}

class WaveTreeProcessor : public foleys::MagicProcessor,
                          private juce::AudioProcessorValueTreeState::Listener,
                          private juce::AsyncUpdater
{
public:
    WaveTreeProcessor();
    ~WaveTreeProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    void initialiseBuilder (foleys::MagicGUIBuilder& builder) override;
    juce::ValueTree createGuiValueTree() override;
    void postSetStateInformation() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // ---- UI hooks (message thread) ----
    juce::AudioProcessorValueTreeState& state() { return apvts; }
    wavetree::TreeEdit& treeEdit()              { return synth.treeEdit(); }
    void treeEditGesture();                     // call after edits: persist to state
    void panic()                                { synth.panic(); }
    void copyScope (std::vector<float>& d) const     { synth.copyScope (d); }
    void copyWavetable (std::vector<float>& d) const { synth.copyWavetable (d); }
    void copyCoeffMags (std::vector<float>& d) const { synth.copyCoeffMags (d); }
    std::uint32_t coeffStamp() const            { return synth.coeffStamp(); }
    int copyChordCoeffMags (std::vector<float>& d, std::vector<wavetree::WaveletSynth::VoiceSpectrumInfo>& i) const
    { return synth.copyChordCoeffMags (d, i); }
    std::uint64_t chordStamp() const            { return synth.chordStamp(); }
    wavetree::WaveletSynth::VoiceSpectrumInfo shownVoiceInfo() const { return synth.shownVoiceInfo(); }
    foleys::MagicProcessorState& magicStateRef() { return magicState; }
    double treeSlope() const { return wavetree::treeSlope (synth.parameters()); }   // what painted levels follow
    int  activeVoices() const                   { return synth.activeVoices(); }
    std::uint32_t shownSeed() const             { return synth.lastShownSeed(); }
    void rollSeed();                            // dice: random 0..9999 into the seed param
    void lockShownSeed();                       // lock: seed = shown, mode = Fixed

    std::atomic<float> lfoAValue { 0.0f }, lfoBValue { 0.0f };   // for mod rings

    // ---- presets ----
    juce::StringArray getPresetNames() const;    // factory first, then user files
    int  numFactoryPresets() const;
    void applyPreset (int index);                // index into getPresetNames()
    bool saveUserPreset (const juce::String& name);
    static juce::File presetDirectory();

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;           // mode-dependent view visibility

    // Cached APVTS atomics — resolved ONCE in the constructor so the audio
    // thread never does string-keyed lookups (no juce::String allocation).
    using PF = std::atomic<float>*;
    struct LfoPtrs { PF shape, rateSync, rateMs, rateDiv, phase, retrig, bipolar; };
    struct RawPtrs
    {
        PF wavelet, mode, seed, seedMode, antiAlias, morphOn, morphSync, morphMs, morphDiv;
        PF tilt, formant, bandQ, hurst, interm, density;
        PF attackSync, attackMs, attackDiv, decaySync, decayMs, decayDiv;
        PF sustain, releaseSync, releaseMs, releaseDiv;
        PF cutoffHz, reso, output, voices;
        PF glideSync, glideMs, glideDiv, legato;
        LfoPtrs lfo[2];
        PF mod[5][2];    // [hurst, interm, tilt, density, output] x [A, B]
    };
    void cacheRawPointers();

    wavetree::WaveTreeParams paramsFromState (float lfoA01, float lfoB01, double bpm) const;
    void configureLfo (acv2::Lfo& lfo, const LfoPtrs& p, double bpm) const;
    static double resolveMs (PF sync, PF ms, PF div, double bpm);
    float  rawValue (const juce::String& id) const;
    void   applyPresetValues (const juce::XmlElement& xml);
    void   saveTreeToState();
    void   loadTreeFromState();

    double sr = 48000.0;
    double lastBpm = 120.0;
    juce::AudioProcessorValueTreeState apvts;
    RawPtrs raw {};
    wavetree::WaveletSynth synth;
    acv2::Lfo lfoA, lfoB;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveTreeProcessor)
};
