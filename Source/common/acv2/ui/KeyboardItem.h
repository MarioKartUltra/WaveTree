// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// =============================================================================
//  acv2::KeyboardItem — the on-screen MIDI keyboard for the foleys layouts.
//
//  The same component and properties as foleys' KeyboardComponent, with one
//  difference in who owns the computer keyboard. Hosted in a DAW the item
//  never takes keyboard focus, so a click on it does not swallow the host's
//  typed keys (Ableton's computer-keyboard MIDI stops the moment a plugin
//  grabs focus). As a standalone app it takes focus as soon as the window
//  shows, so typing plays notes without a click first.
//
//  Register:  builder.registerFactory ("Keyboard", &acv2::KeyboardItem::factory);
//  Layout:    <Keyboard key-down-color="..." mouse-over-color="..."/>
//             properties: key-width, orientation, and the five colours.
// =============================================================================
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <foleys_gui_magic/foleys_gui_magic.h>

namespace acv2
{

class KeyboardItem : public foleys::GuiItem,
                     private juce::Timer
{
public:
    static std::unique_ptr<foleys::GuiItem> factory (foleys::MagicGUIBuilder& builder, const juce::ValueTree& node)
    {
        return std::make_unique<KeyboardItem> (builder, node);
    }

    KeyboardItem (foleys::MagicGUIBuilder& builder, const juce::ValueTree& node)
      : foleys::GuiItem (builder, node),
        keyboard (getMagicState().getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        setColourTranslation ({
            { "white-note-color",         juce::MidiKeyboardComponent::whiteNoteColourId },
            { "black-note-color",         juce::MidiKeyboardComponent::blackNoteColourId },
            { "key-separator-line-color", juce::MidiKeyboardComponent::keySeparatorLineColourId },
            { "mouse-over-color",         juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId },
            { "key-down-color",           juce::MidiKeyboardComponent::keyDownOverlayColourId },
        });

        standalone = juce::JUCEApplicationBase::isStandaloneApp();
        keyboard.setWantsKeyboardFocus (standalone);
        addAndMakeVisible (keyboard);
        if (standalone)
            startTimerHz (10);                   // grab focus once the window is on screen
    }

    void update() override
    {
        const auto size = getProperty ("key-width");
        keyboard.setKeyWidth (size.isVoid() ? 50.0f : float (size));

        const auto orientation = getProperty ("orientation").toString();
        if (orientation == "vertical-left")
            keyboard.setOrientation (juce::MidiKeyboardComponent::verticalKeyboardFacingLeft);
        else if (orientation == "vertical-right")
            keyboard.setOrientation (juce::MidiKeyboardComponent::verticalKeyboardFacingRight);
        else
            keyboard.setOrientation (juce::MidiKeyboardComponent::horizontalKeyboard);
    }

    juce::Component* getWrappedComponent() override { return &keyboard; }

    // What the item decided: true only as a standalone app.
    bool takesKeyboardFocus() const { return keyboard.getWantsKeyboardFocus(); }

private:
    void timerCallback() override
    {
        if (! keyboard.isShowing())
            return;
        keyboard.grabKeyboardFocus();
        stopTimer();
    }

    juce::MidiKeyboardComponent keyboard;
    bool standalone = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyboardItem)
};

} // namespace acv2
