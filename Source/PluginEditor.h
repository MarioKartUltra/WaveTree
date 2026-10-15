// =============================================================================
//  WaveTree II — PluginEditor.h
//  The GUI is foleys_gui_magic driven (MagicProcessor creates the editor; the
//  layout ships as Resources/magic.xml and is editable in the WYSIWYG palette).
//  This file provides the custom painted components + their GuiItem factories:
//
//    TreeView    — THE hero: interactive wavelet coefficient-tree editor.
//                  Rows = octave bands (coarse at the bottom), cells = nodes.
//                  Left gutter drag = band gain (drawbar mute/boost). Painting
//                  in a row sets each node's LEVEL: the bar is drawn where the
//                  mouse is, on one decibel scale for the whole tree (top =
//                  twice the loudest generated coefficient, bottom = silence),
//                  so a node the generator left at zero can be painted in. A
//                  stroke stays in the row it started in. Right-drag erases;
//                  double-click gutter resets the band. Edits land in the
//                  lock-free TreeEdit overlay and regenerate the playing voices
//                  through a crossfade.
//    WaveScope   — current wavelet wavetable (one cycle) + output scope ghost.
//    DhSpectrum  — D(h) multifractal (Legendre) spectrum of the current tree.
//    SeedPanel   — seed readout + dice (roll) + lock (freeze the last roll).
//    PresetBar   — factory + user preset picker, save-as.
//    SyncKnob    — one knob per tempo-syncable time param, with an ms/beat chip
//                  (attribute base="attack" binds attackSync/attackMs/attackDiv).
//    Sigil       — abstract dyadic-tree mark + BETA tag (no title text — the
//                  stealth-beta rule; identity = accent + sigil only).
//
//  All components poll the processor's message-thread-safe feeds with Timers;
//  nothing here touches the audio thread directly.
// =============================================================================
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <foleys_gui_magic/foleys_gui_magic.h>
#include "PluginProcessor.h"
#include "common/acv2/ui/LnfV2.h"
#include "dsp/Mfa.h"

// Registers all custom GuiItems with the builder (called by the processor).
void registerWaveTreeGuiItems (foleys::MagicGUIBuilder& builder);

// =============================================================================
//  CoeffTreeView — interactive coefficient tree editor (hero visual)
// =============================================================================
class CoeffTreeView : public juce::Component,
                      public juce::SettableTooltipClient,
                      private juce::Timer
{
public:
    explicit CoeffTreeView (WaveTreeProcessor* p);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // fixed dyadic layout (kFrame = 2048, kLevels = 10 -> 11 bands)
    static constexpr int kFrame  = wavetree::WaveletVoice::kFrame;
    static constexpr int kLevels = wavetree::WaveletVoice::kLevels;
    static constexpr int kBands  = kLevels + 1;
    static int bandSize (int b) { return b == 0 ? (kFrame >> kLevels) : (kFrame >> (kLevels - b + 1)); }
    static int bandOff  (int b) { int o = kFrame >> kLevels; for (int i = 1; i < b; ++i) o += bandSize (i); return b == 0 ? 0 : o; }

    // Where node k of band b is drawn (the full height of its row), and how
    // tall its bar is as a fraction of that height.
    juce::Rectangle<float> nodeArea (int b, int k) const;
    float shownLevel (int b, int k) const;
    juce::Rectangle<float> rowArea (int b) const;   // band row (bottom = coarse)
    juce::Rectangle<float> gridArea (int b) const;  // the row without its gutter

private:
    void timerCallback() override;
    void applyEdit (const juce::MouseEvent&, bool erase);
    void extendBone (const juce::MouseEvent&, bool erase);
    int bandAtY (float y) const;

    WaveTreeProcessor* proc = nullptr;
    std::vector<float> mags;
    float treePeak = 0.0f;               // the largest generated magnitude: the scale's reference
    int   treePeakBand = 0;              // and the band it sits in
    std::uint32_t lastStamp = 0xFFFFFFFF;
    static constexpr float kGutterW = 58.0f;

    // the stroke in progress
    int   strokeBand = -1;               // the row it started in; -1 = none
    bool  strokeInGutter = false;
    int   lastNode = -1;                 // where the previous mouse event painted
    float lastLevel = 0.0f;
    bool  boneMode = false;              // it has climbed into finer rows: one node per row from now on
    int   boneTop = -1;                  // the finest row it holds a node in
    std::array<int, kBands> boneAt {};   // the one node it holds in each row (strokeBand..boneTop)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CoeffTreeView)
};

// =============================================================================
//  WaveScopeView — wavetable cycle + output scope ghost
// =============================================================================
class WaveScopeView : public juce::Component,
                      public juce::SettableTooltipClient,
                      private juce::Timer
{
public:
    explicit WaveScopeView (WaveTreeProcessor* p);
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    WaveTreeProcessor* proc = nullptr;
    std::vector<float> table, scope;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveScopeView)
};

// =============================================================================
//  SyncKnob — the three-param tempo-sync convention (<base>Sync / <base>Ms /
//  <base>Div) as ONE knob: the chip underneath switches between free time and
//  host-tempo beat divisions. In beats the knob steps through one FAMILY of
//  divisions (all / straight / triplet / dotted, a second chip) in order of
//  length, so turning it goes 1/16 -> 1/8 -> 1/4, never 1/8 -> 1/4T -> 1/8.
//  The family is a state property, <base>Family.
// =============================================================================
class SyncKnob : public juce::Component,
                 public juce::SettableTooltipClient,
                 private juce::AudioProcessorValueTreeState::Listener
{
public:
    static constexpr int kReadoutH = 16;
    static constexpr int kChipH    = 18;
    static constexpr int kMaxFamily = 64;

    SyncKnob (WaveTreeProcessor* p, const juce::String& baseId);
    ~SyncKnob() override;

    void setTooltip (const juce::String& tip) override;
    void resized() override;

    int  familyMode() const;                 // 0 all, 1 straight, 2 triplet, 3 dotted
    void setFamily (int f);
    juce::Slider& beatKnob() { return divSlider; }
    const juce::String& baseId() const { return base; }

private:
    void parameterChanged (const juce::String&, float v) override;
    void showBeats (bool beats);
    void rebuildFamily();
    void followDiv();

    WaveTreeProcessor* proc = nullptr;
    juce::String base;
    bool listening = false;
    juce::Slider msSlider, divSlider;
    juce::ToggleButton chip { "ms" };
    juce::TextButton   familyChip { "all" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> msAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> chipAtt;
    int  family = 0, familyCount = 0;
    int  familyIdx[kMaxFamily] {};
    bool settingDiv = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SyncKnob)
};

// =============================================================================
//  DhSpectrumView — multifractal D(h) readout of the current tree
// =============================================================================
//  KEY shows the last key's tree; CHORD quantifies every sounding voice's tree
//  together (moments added per band: the spectrum of the chord's total
//  coefficient measure — NOT the mixed signal's, interference is ignored).
//  FIT overlays the Cascade generator's closed-form parabola (per voice, or the
//  envelope over the chord's voices). Both choices are state properties.
class DhSpectrumView : public juce::Component,
                       public juce::SettableTooltipClient,
                       private juce::Timer
{
public:
    explicit DhSpectrumView (WaveTreeProcessor* p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    bool chordMode() const    { return chord; }
    bool analyticShown() const { return analytic; }
    void setChordMode (bool on);
    void setAnalyticShown (bool on);
    int  voicesQuantified() const { return numVoices; }
    const wavetree::MfaResult& result() const { return mfa; }
    double fitCentre() const;            // where FIT's parabola peaks for the first Cascade voice (NaN: none)

    static constexpr const char* kChordProp = "dh:chord";
    static constexpr const char* kAnalyticProp = "dh:analytic";
private:
    void timerCallback() override;
    void readProps();
    void recompute();
    juce::Rectangle<float> chipKey() const, chipChord() const, chipFit() const;
    WaveTreeProcessor* proc = nullptr;
    std::vector<float> mags;
    std::vector<wavetree::WaveletSynth::VoiceSpectrumInfo> info;
    wavetree::MfaResult mfa;
    std::uint64_t lastStamp = 0xFFFFFFFFFFFFFFFFull;
    bool chord = false, analytic = false;
    int  numVoices = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DhSpectrumView)
};

// =============================================================================
//  SeedPanel — seed readout + dice + lock
// =============================================================================
class SeedPanel : public juce::Component, private juce::Timer
{
public:
    explicit SeedPanel (WaveTreeProcessor* p);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    WaveTreeProcessor* proc = nullptr;
    juce::TextButton rollBtn { "ROLL" }, lockBtn { "LOCK" };
    juce::uint32 shown = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeedPanel)
};

// =============================================================================
//  PresetBar — preset combo + save
// =============================================================================
class PresetBar : public juce::Component
{
public:
    explicit PresetBar (WaveTreeProcessor* p);
    void resized() override;
private:
    void refresh();
    WaveTreeProcessor* proc = nullptr;
    juce::ComboBox box;
    juce::TextButton saveBtn { "SAVE" };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};

// =============================================================================
//  SigilView — abstract dyadic-tree mark + BETA tag (no title text anywhere)
// =============================================================================
class SigilView : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
};
