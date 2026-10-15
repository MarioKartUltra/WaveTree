// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
#pragma once
// acv2/test/WiringTest.h — control wiring self-test for the v2 plugin editors.
// Header-only, JUCE-side, used by each plugin's offscreen tools/<name>_shot tool
// (vendored next to it as tools/WiringTest.h).
//
// run() operates every toggle, knob and menu that is on screen and checks that
// exactly one parameter follows it, then puts the control back. Call it once per
// UI state (mode, sync on/off, ...) so controls that only appear in some states
// are covered, then report() lists the parameters no control reached.
//
// The editor must stay OFFSCREEN (no addToDesktop): with a native peer every
// message-loop pump waits on a display refresh and the test takes minutes.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace acv2 { namespace test {

struct WiringTest
{
    juce::AudioProcessor&     proc;
    std::function<void (int)> pumpMs;      // runs the message loop for N milliseconds
    juce::StringArray         reached;
    int                       failures = 0;

    // No peer means isShowing() is always false: walk only through visible components.
    template <typename T>
    static void collectShowing (juce::Component& c, std::vector<T*>& out)
    {
        if (! c.isVisible()) return;
        if (auto* t = dynamic_cast<T*> (&c))
            if (t->isEnabled() && t->getWidth() >= 12 && t->getHeight() >= 12)
                out.push_back (t);
        for (int i = 0; i < c.getNumChildComponents(); ++i)
            collectShowing (*c.getChildComponent (i), out);
    }

    std::vector<float> snapshot() const
    {
        std::vector<float> v;
        for (auto* p : proc.getParameters()) v.push_back (p->getValue());
        return v;
    }

    // IDs of the parameters that differ from the snapshot
    juce::StringArray changedSince (const std::vector<float>& before) const
    {
        juce::StringArray ids;
        const auto& params = proc.getParameters();
        for (int i = 0; i < params.size(); ++i)
            if (std::abs (params[i]->getValue() - before[(size_t) i]) > 1.0e-6f)
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (params[i]))
                    ids.add (withId->paramID);
        return ids;
    }

    void fail (const juce::String& what, const juce::String& why)
    {
        ++failures;
        std::printf ("  FAIL %s %s\n", what.toRawUTF8(), why.toRawUTF8());
    }

    void expectOne (const juce::String& what, const juce::StringArray& changed)
    {
        if (changed.size() == 1) { reached.addIfNotAlreadyThere (changed[0]); return; }
        fail (what, "changed " + juce::String (changed.size()) + " parameters ["
                        + changed.joinIntoString (", ") + "]");
    }

    void run (juce::Component& editor, const char* pass)
    {
        std::vector<juce::Button*>   buttons, toggles, radios;
        std::vector<juce::Slider*>   sliders;
        std::vector<juce::ComboBox*> combos;
        collectShowing (editor, buttons);
        collectShowing (editor, sliders);
        collectShowing (editor, combos);
        for (auto* b : buttons)
        {
            if (b->getRadioGroupId() != 0)           radios.push_back (b);
            else if (b->getClickingTogglesState())   toggles.push_back (b);
            // anything else (save, dice, lock, export) only fires an action
        }
        std::printf ("  [%s] on screen: %d toggles, %d radio chips, %d knobs, %d menus\n", pass,
                     (int) toggles.size(), (int) radios.size(), (int) sliders.size(), (int) combos.size());

        for (auto* t : toggles)
        {
            const auto label = "toggle '" + t->getButtonText() + "'";
            const bool wasOn = t->getToggleState();
            auto before = snapshot();
            t->triggerClick();  pumpMs (60);
            expectOne (label, changedSince (before));
            if (t->getToggleState() == wasOn) fail (label, "did not change state when clicked");

            t->triggerClick();  pumpMs (60);         // and back again
            if (! changedSince (before).isEmpty() || t->getToggleState() != wasOn)
                fail (label, "did not return to its old state");
        }

        // a radio chip may legitimately move more than one parameter (unit + sync flag).
        // There can be several groups of chips on screen (one per control): each
        // is put back to the chip it started on.
        std::vector<juce::Button*> onAtStart;
        for (auto* r : radios) if (r->getToggleState()) onAtStart.push_back (r);
        for (auto* r : radios)
        {
            if (r->getToggleState()) continue;
            auto before = snapshot();
            r->triggerClick();  pumpMs (80);
            const auto changed = changedSince (before);
            if (changed.isEmpty()) fail ("radio chip '" + r->getButtonText() + "'", "changed no parameter");
            for (auto& id : changed) reached.addIfNotAlreadyThere (id);
        }
        for (auto* r : onAtStart)
            if (! r->getToggleState()) { r->triggerClick(); pumpMs (80); }

        for (auto* s : sliders)
        {
            const auto   range = s->getRange();
            if (range.getLength() <= 0.0) continue;
            const double old = s->getValue();
            const bool   low = old < range.getStart() + 0.5 * range.getLength();
            auto before = snapshot();
            s->setValue (range.getStart() + (low ? 0.8 : 0.2) * range.getLength(), juce::sendNotificationSync);
            pumpMs (30);
            expectOne ("knob showing '" + s->getTextFromValue (old) + "'", changedSince (before));
            s->setValue (old, juce::sendNotificationSync);
            pumpMs (30);
        }

        for (auto* c : combos)
        {
            if (c->getNumItems() < 2) continue;
            const int old = c->getSelectedItemIndex();
            if (old < 0) continue;                   // a preset picker: nothing selected
            auto before = snapshot();
            c->setSelectedItemIndex ((old + 1) % c->getNumItems(), juce::sendNotificationSync);
            pumpMs (60);
            expectOne ("menu showing '" + c->getItemText (old) + "'", changedSince (before));
            c->setSelectedItemIndex (old, juce::sendNotificationSync);
            pumpMs (60);
        }
    }

    // `customControls`: parameters driven by a bespoke component the generic walk
    // cannot operate (a list browser, dice/lock buttons, a drag surface).
    int report (const juce::StringArray& customControls = {})
    {
        juce::StringArray unreached, viaCustom;
        for (auto* p : proc.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
                if (! reached.contains (withId->paramID))
                    (customControls.contains (withId->paramID) ? viaCustom : unreached).add (withId->paramID);

        std::printf ("\nparameters driven by a knob/toggle/menu: %d of %d\n",
                     reached.size(), proc.getParameters().size());
        if (! viaCustom.isEmpty())
            std::printf ("driven by a custom control instead:       %s\n", viaCustom.joinIntoString (", ").toRawUTF8());
        std::printf ("parameters with NO control:              %s\n",
                     unreached.isEmpty() ? "none" : unreached.joinIntoString (", ").toRawUTF8());
        if (! unreached.isEmpty()) ++failures;
        std::printf ("=== selftest %s (%d failures) ===\n", failures == 0 ? "PASSED" : "FAILED", failures);
        return failures == 0 ? 0 : 1;
    }
};

} } // namespace acv2::test
