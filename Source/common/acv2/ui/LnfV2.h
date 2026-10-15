#pragma once
// acv2/ui/LnfV2.h — shared v2 LookAndFeel (arc knobs + modulation ring + unit
// readout + toggle chips + section headers + BETA tag). Header-only.
//
// JUCE-side, but included via direct module headers (no JuceHeader.h) and
// guarded so this file is a no-op outside a JUCE target — the same vendored
// copy can sit next to the std-only dsp headers without breaking CLI tests.
//
// Usage: construct with a theme::Palette (Theme.h), setLookAndFeel(&lnf) on the
// editor. Knob mod rings: either add an acv2::ui::ModRing sibling component
// (animated dot — see ModRing.h) or set static depth via the Slider properties
// "modDepth" (−1..1); LnfV2 paints the depth arc around the knob. Unit
// readouts come from the slider textbox (TextBoxBelow) — wire real units with
// acv2::units (Units.h). Never bare 0..1.

#if __has_include(<juce_gui_basics/juce_gui_basics.h>)

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

namespace acv2 { namespace ui {

inline juce::Colour toColour (std::uint32_t argb) { return juce::Colour (argb); }

class LnfV2 : public juce::LookAndFeel_V4
{
public:
    explicit LnfV2 (const theme::Palette& p = theme::kScaloSculpt) { setPalette (p); }

    void setPalette (const theme::Palette& p)
    {
        pal = p;
        const auto bg      = toColour (p.background);
        const auto panel   = toColour (p.panel);
        const auto accent  = toColour (p.accent);
        const auto outline = toColour (p.outline);
        const auto text    = toColour (p.textPrimary);
        const auto text2   = toColour (p.textSecondary);

        setColourScheme ({ bg, panel, panel, outline, text, accent, accent,
                           accent, text });

        setColour (juce::ResizableWindow::backgroundColourId, bg);
        setColour (juce::Slider::rotarySliderFillColourId, accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, outline);
        setColour (juce::Slider::thumbColourId, accent);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, text2);
        setColour (juce::ToggleButton::textColourId, text2);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.25f));
        setColour (juce::TooltipWindow::backgroundColourId, panel);
    }

    const theme::Palette& palette() const noexcept { return pal; }

    // ── arc knob with modulation-amount ring ───────────────────────────────
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float pos, float startAngle, float endAngle,
                           juce::Slider& s) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y,
                                              (float) width, (float) height)
                          .reduced (theme::kSpacing.modRingStrokePx
                                    + theme::kSpacing.modRingGapPx);
        const auto  centre = bounds.getCentre();
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f
                             - theme::kSpacing.knobStrokePx;
        const float angle  = startAngle + pos * (endAngle - startAngle);
        const float stroke = theme::kSpacing.knobStrokePx;

        // track
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                             startAngle, endAngle, true);
        g.setColour (toColour (pal.outline));
        g.strokePath (track, { stroke, juce::PathStrokeType::curved,
                               juce::PathStrokeType::rounded });

        // value arc
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                             startAngle, angle, true);
        g.setColour (toColour (pal.accent));
        g.strokePath (value, { stroke, juce::PathStrokeType::curved,
                               juce::PathStrokeType::rounded });

        // static modulation-depth ring (animated version: ModRing.h)
        const float depth = (float) (double) s.getProperties()
                                .getWithDefault ("modDepth", 0.0);
        if (std::abs (depth) > 0.001f)
            drawModArc (g, centre.x, centre.y,
                        radius + stroke + theme::kSpacing.modRingGapPx,
                        angle, depth, startAngle, endAngle);

        // pointer
        const float inner = radius * 0.55f;
        juce::Path pointer;
        pointer.startNewSubPath (centre.getPointOnCircumference (inner, angle));
        pointer.lineTo (centre.getPointOnCircumference (radius - stroke, angle));
        g.setColour (toColour (pal.textPrimary));
        g.strokePath (pointer, { stroke * 0.8f, juce::PathStrokeType::curved,
                                 juce::PathStrokeType::rounded });
    }

    // Depth arc from the current value angle, clamped to the knob sweep.
    void drawModArc (juce::Graphics& g, float cx, float cy, float r,
                     float fromAngle, float depth,
                     float startAngle = theme::kKnobStartAngle,
                     float endAngle   = theme::kKnobEndAngle) const
    {
        const float target = juce::jlimit (startAngle, endAngle,
                                           fromAngle + depth * (endAngle - startAngle));
        juce::Path arc;
        arc.addCentredArc (cx, cy, r, r, 0.0f, fromAngle, target, true);
        g.setColour (toColour (pal.modRing).withAlpha (0.85f));
        g.strokePath (arc, { theme::kSpacing.modRingStrokePx,
                             juce::PathStrokeType::curved,
                             juce::PathStrokeType::rounded });
    }

    // ── unit readout under each knob (slider textbox) ──────────────────────
    // A real text box: one click opens it for typing (JUCE's default for a
    // slider's value box), and it says so. A hairline under the number marks
    // it as a field; cmd/ctrl while dragging the knob gives fine control (JUCE).
    struct ValueBox final : juce::Label
    {
        ValueBox() : juce::Label ({}, {}) {}
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override {}
        juce::String getTooltip() override
        {
            const auto own = juce::Label::getTooltip();
            const juce::String hint = "Click the value to type it. Hold cmd (ctrl) while dragging the knob for fine control.";
            return own.isEmpty() ? hint : own + "\n" + hint;
        }
        void paint (juce::Graphics& g) override
        {
            juce::Label::paint (g);
            if (isBeingEdited()) return;
            const auto b = getLocalBounds().toFloat();
            const float w = juce::jmin (b.getWidth() - 8.0f, juce::GlyphArrangement::getStringWidth (getFont(), getText()) + 6.0f);
            g.setColour (findColour (juce::Label::outlineColourId));
            g.fillRect (b.getCentreX() - 0.5f * w, b.getBottom() - 1.5f, w, 1.0f);
        }
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
        { return createIgnoredAccessibilityHandler (*this); }
    };

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = new ValueBox();
        l->setJustificationType (juce::Justification::centred);
        l->setKeyboardType (juce::TextInputTarget::decimalKeyboard);
        l->setFont (valueFont());
        l->setColour (juce::Label::textColourId, toColour (pal.textPrimary));
        l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        l->setColour (juce::Label::outlineColourId, toColour (pal.outline).withAlpha (0.7f));   // the hairline
        l->setColour (juce::Label::outlineWhenEditingColourId, toColour (pal.accentDim));
        l->setColour (juce::TextEditor::textColourId, toColour (pal.textPrimary));
        l->setColour (juce::TextEditor::backgroundColourId, toColour (pal.hero));
        l->setColour (juce::TextEditor::highlightColourId, toColour (pal.accentDim));
        juce::ignoreUnused (s);
        return l;
    }

    // ── toggle chips (ms/beats, uni/bi, retrig modes...) ───────────────────
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                           bool highlighted, bool /*down*/) override
    {
        const auto r  = b.getLocalBounds().toFloat().reduced (1.0f);
        const bool on = b.getToggleState();

        g.setColour (on ? toColour (pal.accent)
                        : toColour (pal.panel).brighter (highlighted ? 0.15f : 0.0f));
        g.fillRoundedRectangle (r, theme::kSpacing.cornerS);
        if (! on)
        {
            g.setColour (toColour (pal.outline));
            g.drawRoundedRectangle (r, theme::kSpacing.cornerS,
                                    theme::kSpacing.hairline);
        }
        g.setColour (on ? toColour (pal.background) : toColour (pal.textSecondary));
        g.setFont (chipFont());
        g.drawText (b.getButtonText(), r, juce::Justification::centred, false);
    }

    // ── section header: tracked small-caps label + accent rule ─────────────
    void drawSectionHeader (juce::Graphics& g, juce::Rectangle<int> area,
                            const juce::String& title) const
    {
        auto r = area.toFloat();
        g.setColour (toColour (pal.textSecondary));
        g.setFont (sectionFont());
        const auto text  = title.toUpperCase();
        const float tw   = juce::GlyphArrangement::getStringWidth (sectionFont(), text) + 8.0f;
        g.drawText (text, r.removeFromLeft (tw), juce::Justification::centredLeft, false);
        g.setColour (toColour (pal.accentDim));
        g.fillRect (r.withHeight (theme::kSpacing.hairline)
                     .withY (r.getCentreY()));
    }

    // ── BETA tag (every v2 header) ─────────────────────────────────────────
    void drawBetaTag (juce::Graphics& g, juce::Rectangle<int> area) const
    {
        const auto r = area.toFloat();
        g.setColour (toColour (pal.accent));
        g.fillRoundedRectangle (r, theme::kSpacing.cornerS);
        g.setColour (toColour (pal.background));
        g.setFont (betaFont());
        g.drawText ("BETA", r, juce::Justification::centred, false);
    }

    // ── fonts (tabular figures for readouts) ───────────────────────────────
    juce::Font displayFont() const { return juce::Font (juce::FontOptions (theme::kType.display).withStyle ("Bold")); }
    juce::Font sectionFont() const { return juce::Font (juce::FontOptions (theme::kType.section).withStyle ("Bold")); }
    juce::Font labelFont()   const { return juce::Font (juce::FontOptions (theme::kType.label)); }
    juce::Font chipFont()    const { return juce::Font (juce::FontOptions (theme::kType.chip).withStyle ("Bold")); }
    juce::Font betaFont()    const { return juce::Font (juce::FontOptions (theme::kType.beta).withStyle ("Bold")); }
    juce::Font valueFont()   const
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                              theme::kType.value, juce::Font::plain));
    }

    juce::Font getLabelFont (juce::Label&) override { return labelFont(); }

private:
    theme::Palette pal = theme::kScaloSculpt;
};

} } // namespace acv2::ui

#endif // __has_include(<juce_gui_basics/juce_gui_basics.h>)
