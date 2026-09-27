// =============================================================================
//  WaveTree II — PluginEditor.h
//  The GUI is foleys_gui_magic driven (MagicProcessor creates the editor; the
//  layout ships as Resources/magic.xml and is editable in the WYSIWYG palette).
//  This file provides the custom painted components + their GuiItem factories:
//
//    TreeView    — THE hero: interactive wavelet coefficient-tree editor.
//                  Rows = octave bands (coarse at the bottom), cells = nodes.
//                  Left gutter drag = band gain (drawbar mute/boost); painting
//                  in a row sets per-node gains (top = 2x, bottom = mute);
//                  right-drag erases (gain -> 1); double-click gutter resets
//                  the band. Edits land in the lock-free TreeEdit overlay and
//                  regenerate the playing voices through a crossfade.
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

private:
    void timerCallback() override;
    void applyEdit (const juce::MouseEvent&, bool erase);

    // fixed dyadic layout (kFrame = 2048, kLevels = 10 -> 11 bands)
    static constexpr int kFrame  = wavetree::WaveletVoice::kFrame;
    static constexpr int kLevels = wavetree::WaveletVoice::kLevels;
    static constexpr int kBands  = kLevels + 1;
    static int bandSize (int b) { return b == 0 ? (kFrame >> kLevels) : (kFrame >> (kLevels - b + 1)); }
    static int bandOff  (int b) { int o = kFrame >> kLevels; for (int i = 1; i < b; ++i) o += bandSize (i); return b == 0 ? 0 : o; }

    juce::Rectangle<float> rowArea (int b) const;   // band row (bottom = coarse)
    int bandAtY (float y) const;

    WaveTreeProcessor* proc = nullptr;
    std::vector<float> mags;
    std::array<float, (size_t) kBands> bandPeak {};
    std::uint32_t lastStamp = 0xFFFFFFFF;
    static constexpr float kGutterW = 58.0f;

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
//  DhSpectrumView — multifractal D(h) readout of the current tree
// =============================================================================
class DhSpectrumView : public juce::Component,
                       public juce::SettableTooltipClient,
                       private juce::Timer
{
public:
    explicit DhSpectrumView (WaveTreeProcessor* p);
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    WaveTreeProcessor* proc = nullptr;
    std::vector<float> mags;
    wavetree::MfaResult mfa;
    std::uint32_t lastStamp = 0xFFFFFFFF;
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
