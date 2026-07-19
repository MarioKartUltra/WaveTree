#pragma once
// acv2/ui/ModRing.h — animated LFO-depth ring around any knob. Header-only.
//
// JUCE-side, guarded (see LnfV2.h note) so the vendored copy is inert outside
// a JUCE target.
//
// Wiring: the processor publishes the CURRENT LFO output (bipolar −1..1) into a
// std::atomic<float> once per block (see the Lfo.h header comment); this
// component polls it at UI rate (30 Hz) and repaints only on change. Place it
// as an overlay exactly covering the knob's bounds (it ignores mouse events);
// the LnfV2 rotary reserves the outer margin for it. The ring shows the depth
// arc from the knob's current value angle, plus a travelling dot at the
// instantaneous modulated position.

#if __has_include(<juce_gui_basics/juce_gui_basics.h>)

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include <atomic>

namespace acv2 { namespace ui {

class ModRing : public juce::Component, private juce::Timer
{
public:
    // lfoValue: atomic the processor writes at block rate (bipolar −1..1).
    // slider: the knob this ring decorates (for its current position + sweep).
    ModRing (std::atomic<float>& lfoValue, juce::Slider& slider,
             const theme::Palette& palette = theme::kScaloSculpt)
        : value (lfoValue), knob (slider), pal (palette)
    {
        setInterceptsMouseClicks (false, false);
        setPaintingIsUnclipped (true);
        startTimerHz (30);
    }

    void setDepth (float d) noexcept  { depth = juce::jlimit (-1.0f, 1.0f, d); repaint(); }
    float getDepth() const noexcept   { return depth; }
    void setPalette (const theme::Palette& p) { pal = p; repaint(); }

    void paint (juce::Graphics& g) override
    {
        if (std::abs (depth) < 0.001f)
            return;

        const auto  bounds = getLocalBounds().toFloat().reduced (theme::kSpacing.modRingStrokePx);
        const auto  centre = bounds.getCentre();
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;

        const float a0 = theme::kKnobStartAngle, a1 = theme::kKnobEndAngle;
        const auto  range = knob.getRange();
        const float pos   = range.getLength() > 0.0
                              ? (float) ((knob.getValue() - range.getStart()) / range.getLength())
                              : 0.0f;
        const float valueAngle = a0 + pos * (a1 - a0);

        // depth arc (full modulation span, at rest)
        const float depthAngle = juce::jlimit (a0, a1, valueAngle + depth * (a1 - a0));
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                           valueAngle, depthAngle, true);
        g.setColour (juce::Colour (pal.modRing).withAlpha (0.45f));
        g.strokePath (arc, { theme::kSpacing.modRingStrokePx,
                             juce::PathStrokeType::curved,
                             juce::PathStrokeType::rounded });

        // travelling dot at the instantaneous modulated position
        const float mod = juce::jlimit (a0, a1,
                                        valueAngle + depth * lastPainted * (a1 - a0));
        const auto  dot = centre.getPointOnCircumference (radius, mod);
        const float dr  = theme::kSpacing.modRingStrokePx * 1.6f;
        g.setColour (juce::Colour (pal.modRing));
        g.fillEllipse (dot.x - dr, dot.y - dr, dr * 2.0f, dr * 2.0f);
    }

private:
    void timerCallback() override
    {
        const float v = value.load (std::memory_order_relaxed);
        if (std::abs (v - lastPainted) > 0.004f && std::abs (depth) > 0.001f)
        {
            lastPainted = v;
            repaint();
        }
    }

    std::atomic<float>& value;
    juce::Slider&       knob;
    theme::Palette      pal;
    float               depth = 0.0f;
    float               lastPainted = 0.0f;
};

} } // namespace acv2::ui

#endif // __has_include(<juce_gui_basics/juce_gui_basics.h>)
