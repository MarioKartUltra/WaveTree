// =============================================================================
//  WaveTree II — PluginEditor.cpp  (custom components + foleys GuiItem glue)
// =============================================================================
#include "PluginEditor.h"

namespace
{
    const auto kBg      = juce::Colour (acv2::theme::kWaveTree.hero);
    const auto kPanel   = juce::Colour (acv2::theme::kWaveTree.panel);
    const auto kOutline = juce::Colour (acv2::theme::kWaveTree.outline);
    const auto kAccent  = juce::Colour (acv2::theme::kWaveTree.accent);
    const auto kAccDim  = juce::Colour (acv2::theme::kWaveTree.accentDim);
    const auto kText    = juce::Colour (acv2::theme::kWaveTree.textPrimary);
    const auto kText2   = juce::Colour (acv2::theme::kWaveTree.textSecondary);

    juce::Font monoFont (float h)
    { return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain)); }
}

// =============================================================================
//  CoeffTreeView
// =============================================================================
CoeffTreeView::CoeffTreeView (WaveTreeProcessor* p) : proc (p)
{
    mags.assign ((size_t) kFrame, 0.0f);
    setTooltip ("Coefficient tree: rows are octave bands (coarse at the bottom). "
                "Drag in the left gutter to set a band gain (drawbar). Paint in a row to draw "
                "the level of each node: the bar follows the mouse, the bottom of the row is "
                "silence, and a stroke stays in the row it started in. Right-drag erases. "
                "Double-click the gutter to reset a band, double-click the grid to reset everything.");
    startTimerHz (30);
}

juce::Rectangle<float> CoeffTreeView::rowArea (int b) const
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float rh = r.getHeight() / (float) kBands;
    return { r.getX(), r.getBottom() - rh * (float) (b + 1), r.getWidth(), rh };
}

juce::Rectangle<float> CoeffTreeView::gridArea (int b) const
{
    auto row = rowArea (b);
    row.removeFromLeft (kGutterW);
    return row.reduced (1.0f, 1.0f);
}

juce::Rectangle<float> CoeffTreeView::nodeArea (int b, int k) const
{
    const auto grid = gridArea (b);
    const float cw = grid.getWidth() / (float) bandSize (b);
    return { grid.getX() + cw * (float) k, grid.getY(), cw, grid.getHeight() };
}

// One scale for the whole tree, so a bar's height means the same in every row
// and a painted bar is as high as it was drawn.
float CoeffTreeView::shownLevel (int b, int k) const
{
    using wavetree::TreeEdit;
    if (proc == nullptr) return 0.0f;
    const int   i     = bandOff (b) + k;
    const auto& tree  = proc->treeEdit();
    const float level = tree.getNodeLevel (i);
    if (level >= 0.0f)
        return level < (float) TreeEdit::kMuteBelow ? 0.0f : level;

    return (float) TreeEdit::magnitudeToLevel ((double) mags[(size_t) i] * (double) tree.getNodeGain (i),
                                               (double) treePeak);
}

int CoeffTreeView::bandAtY (float y) const
{
    for (int b = 0; b < kBands; ++b)
        if (rowArea (b).getVerticalRange().contains (y)) return b;
    return -1;
}

void CoeffTreeView::timerCallback()
{
    if (proc == nullptr) return;
    const auto stamp = proc->coeffStamp();
    if (stamp != lastStamp)
    {
        lastStamp = stamp;
        proc->copyCoeffMags (mags);
        treePeak = 0.0f;
        for (auto m : mags) treePeak = std::max (treePeak, m);
    }
    repaint();   // gains can change without a rebuild stamp (live painting)
}

void CoeffTreeView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (kBg);
    g.fillRoundedRectangle (r, 6.0f);

    if (proc == nullptr) { g.setColour (kAccDim); g.drawText ("tree", r, juce::Justification::centred); return; }
    auto& tree = proc->treeEdit();

    for (int b = 0; b < kBands; ++b)
    {
        auto row = rowArea (b);
        auto gutter = row.removeFromLeft (kGutterW).reduced (4.0f, 3.0f);
        const float bg = tree.getBandGain (b);

        // gutter: band gain bar (0..2, hairline at 1)
        g.setColour (kPanel);
        g.fillRoundedRectangle (gutter, 3.0f);
        auto fill = gutter.reduced (2.0f);
        fill = fill.removeFromLeft (fill.getWidth() * juce::jlimit (0.0f, 1.0f, bg * 0.5f));
        g.setColour (bg < 0.02f ? kOutline : (bg > 1.02f ? kAccent : kAccDim.brighter (0.2f)));
        g.fillRoundedRectangle (fill, 2.0f);
        g.setColour (kOutline);
        const float oneX = gutter.getX() + 2.0f + (gutter.getWidth() - 4.0f) * 0.5f;
        g.drawVerticalLine ((int) oneX, gutter.getY(), gutter.getBottom());
        g.setColour (kText2);
        g.setFont (monoFont (9.0f));
        g.drawText (b == 0 ? "A" : "d" + juce::String (b), gutter, juce::Justification::centred, false);

        // nodes: bar height = level on the tree's scale
        row.reduce (1.0f, 1.0f);
        const int off = bandOff (b), n = bandSize (b);
        const float cw = row.getWidth() / (float) n;
        for (int k = 0; k < n; ++k)
        {
            const float shown  = shownLevel (b, k);
            const bool  edited = tree.getNodeLevel (off + k) >= 0.0f
                                 || std::abs (tree.getNodeGain (off + k) - 1.0f) > 0.01f;
            const float h = std::max (1.5f, row.getHeight() * shown);
            juce::Rectangle<float> cell (row.getX() + cw * (float) k, row.getBottom() - h,
                                         std::max (1.0f, cw - (cw > 3.0f ? 1.0f : 0.0f)), h);
            auto col = kAccent.withAlpha (0.25f + 0.65f * shown);
            if (edited && shown <= 0.0f) col = kOutline.withAlpha (0.5f);                 // muted by hand
            else if (edited)             col = kAccent.brighter (0.25f).withAlpha (0.4f + 0.6f * shown);
            g.setColour (col.withMultipliedAlpha (bg < 0.02f ? 0.25f : 1.0f));
            g.fillRect (cell);
        }

        // row hairline
        g.setColour (kOutline.withAlpha (0.35f));
        g.drawHorizontalLine ((int) row.getBottom(), row.getX(), row.getRight());
    }
}

// A stroke belongs to the row it started in. The rows are thin, so the mouse
// leaves them all the time: above the row is the top of the scale, below it is
// silence, and the neighbouring rows are left alone.
void CoeffTreeView::applyEdit (const juce::MouseEvent& e, bool erase)
{
    if (proc == nullptr || strokeBand < 0) return;
    auto& tree = proc->treeEdit();
    const int b = strokeBand;

    if (strokeInGutter)                           // gutter: band gain 0..2
    {
        auto gutter = rowArea (b).removeFromLeft (kGutterW).reduced (6.0f, 3.0f);
        const float g01 = juce::jlimit (0.0f, 1.0f, (e.position.x - gutter.getX()) / gutter.getWidth());
        tree.setBandGain (b, erase ? 1.0f : g01 * 2.0f);
    }
    else                                          // grid: draw the level of each node
    {
        const auto grid = gridArea (b);
        const int  n    = bandSize (b), off = bandOff (b);
        const int  k    = juce::jlimit (0, n - 1, (int) std::floor ((e.position.x - grid.getX()) / grid.getWidth() * (float) n));
        const float level = juce::jlimit (0.0f, 1.0f, (grid.getBottom() - e.position.y) / grid.getHeight());

        // the mouse skips nodes when it moves fast: fill in from the last one
        const int   from      = lastNode < 0 ? k : lastNode;
        const float fromLevel = lastNode < 0 ? level : lastLevel;
        const int   steps     = std::abs (k - from);
        for (int s = 0; s <= steps; ++s)
        {
            const int   node = from + (k >= from ? s : -s);
            const float t    = steps == 0 ? 1.0f : (float) s / (float) steps;
            if (erase) tree.clearNode (off + node);
            else       tree.setNodeLevel (off + node, fromLevel + t * (level - fromLevel));
        }
        lastNode  = k;
        lastLevel = level;
    }
    tree.bump();
}

void CoeffTreeView::mouseDown (const juce::MouseEvent& e)
{
    strokeBand = bandAtY ((float) e.position.y);
    strokeInGutter = strokeBand >= 0 && e.position.x < rowArea (strokeBand).getX() + kGutterW;
    lastNode = -1;
    applyEdit (e, e.mods.isRightButtonDown() || e.mods.isAltDown());
}

void CoeffTreeView::mouseDrag (const juce::MouseEvent& e) { applyEdit (e, e.mods.isRightButtonDown() || e.mods.isAltDown()); }

void CoeffTreeView::mouseUp (const juce::MouseEvent&)
{
    strokeBand = -1;
    lastNode = -1;
    if (proc != nullptr) proc->treeEditGesture();
}

void CoeffTreeView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (proc == nullptr) return;
    auto& tree = proc->treeEdit();
    const int b = bandAtY ((float) e.position.y);
    if (b >= 0 && e.position.x < rowArea (b).getX() + kGutterW)
    {
        tree.setBandGain (b, 1.0f);
        const int off = bandOff (b), n = bandSize (b);
        for (int k = 0; k < n; ++k) tree.clearNode (off + k);
    }
    else
        tree.reset();
    tree.bump();
    proc->treeEditGesture();
}

// =============================================================================
//  WaveScopeView
// =============================================================================
WaveScopeView::WaveScopeView (WaveTreeProcessor* p) : proc (p)
{
    setTooltip ("Current wavelet wavetable (one cycle, violet) over the live output scope (dim).");
    startTimerHz (30);
}

void WaveScopeView::timerCallback()
{
    if (proc == nullptr) return;
    proc->copyWavetable (table);
    proc->copyScope (scope);
    repaint();
}

void WaveScopeView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (kBg);
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (kOutline.withAlpha (0.6f));
    g.drawHorizontalLine ((int) b.getCentreY(), b.getX() + 2.0f, b.getRight() - 2.0f);

    auto drawWave = [&] (const std::vector<float>& v, juce::Colour col, float amp, float thick)
    {
        const int n = (int) v.size(); if (n < 2) return;
        juce::Path pth;
        const float w = b.getWidth() - 8.0f, midY = b.getCentreY(), h = b.getHeight() * amp;
        pth.startNewSubPath (b.getX() + 4.0f, midY - v[0] * h);
        const int step = std::max (1, n / 512);
        for (int i = step; i < n; i += step)
            pth.lineTo (b.getX() + 4.0f + w * (float) i / (float) (n - 1), midY - v[(size_t) i] * h);
        g.setColour (col);
        g.strokePath (pth, juce::PathStrokeType (thick, juce::PathStrokeType::curved));
    };
    drawWave (scope, kAccDim.withAlpha (0.55f), 0.44f, 1.0f);
    drawWave (table, kAccent, 0.40f, 1.8f);
}

// =============================================================================
//  DhSpectrumView
// =============================================================================
DhSpectrumView::DhSpectrumView (WaveTreeProcessor* p) : proc (p)
{
    setTooltip ("D(h): multifractal Legendre spectrum of the current coefficient tree. "
                "The arch widens with Intermittency; Hurst moves its centre.");
    startTimerHz (10);
}

void DhSpectrumView::timerCallback()
{
    if (proc == nullptr) return;
    const auto stamp = proc->coeffStamp();
    if (stamp == lastStamp) return;
    lastStamp = stamp;
    proc->copyCoeffMags (mags);

    constexpr int kFrame  = wavetree::WaveletVoice::kFrame;
    constexpr int kLevels = wavetree::WaveletVoice::kLevels;
    auto bandSize = [] (int b) { return b == 0 ? (kFrame >> kLevels) : (kFrame >> (kLevels - b + 1)); };
    auto bandOff  = [&] (int b) { int o = kFrame >> kLevels; for (int i = 1; i < b; ++i) o += bandSize (i); return b == 0 ? 0 : o; };
    mfa = wavetree::computeDh (mags.data(), kLevels + 1, bandOff, bandSize);
    repaint();
}

void DhSpectrumView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (kBg);
    g.fillRoundedRectangle (b, 6.0f);
    auto plot = b.reduced (10.0f, 8.0f);
    plot.removeFromTop (10.0f);

    g.setColour (kText2);
    g.setFont (monoFont (9.5f));
    g.drawText ("D(h)", b.reduced (8.0f, 4.0f), juce::Justification::topLeft, false);

    if (! mfa.valid || mfa.count < 3)
    { g.setColour (kAccDim); g.drawText ("--", plot, juce::Justification::centred, false); return; }

    const double hLo = mfa.hMin - 0.05, hHi = mfa.hMax + 0.05;
    const double span = std::max (0.1, hHi - hLo);
    auto X = [&] (double h) { return plot.getX() + (float) ((h - hLo) / span) * plot.getWidth(); };
    auto Y = [&] (double d) { return plot.getBottom() - (float) juce::jlimit (0.0, 1.0, d) * plot.getHeight(); };

    g.setColour (kOutline.withAlpha (0.5f));
    g.drawHorizontalLine ((int) Y (1.0), plot.getX(), plot.getRight());

    juce::Path pth;
    bool started = false;
    for (int i = 0; i < mfa.count; ++i)
    {
        const float x = X (mfa.h[i]), y = Y (mfa.Dh[i]);
        if (! started) { pth.startNewSubPath (x, y); started = true; }
        else pth.lineTo (x, y);
    }
    g.setColour (kAccent);
    g.strokePath (pth, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));

    g.setColour (kText);
    g.setFont (monoFont (9.5f));
    g.drawText (juce::String::formatted ("h* %.2f  [%.2f, %.2f]", mfa.hPeak, mfa.hMin, mfa.hMax),
                b.reduced (8.0f, 4.0f), juce::Justification::topRight, false);
}

// =============================================================================
//  SeedPanel
// =============================================================================
SeedPanel::SeedPanel (WaveTreeProcessor* p) : proc (p)
{
    addAndMakeVisible (rollBtn);
    addAndMakeVisible (lockBtn);
    rollBtn.setTooltip ("Dice: roll a random seed into the Seed parameter.");
    lockBtn.setTooltip ("Lock: freeze the last-played seed (sets Seed and switches the policy to Fixed).");
    rollBtn.onClick = [this] { if (proc != nullptr) proc->rollSeed(); };
    lockBtn.onClick = [this] { if (proc != nullptr) proc->lockShownSeed(); };
    startTimerHz (10);
}

void SeedPanel::timerCallback()
{
    if (proc == nullptr) return;
    const auto s = proc->shownSeed();
    if (s != shown) { shown = s; repaint(); }
}

void SeedPanel::resized()
{
    auto r = getLocalBounds().reduced (4);
    auto right = r.removeFromRight (r.getWidth() / 2);
    rollBtn.setBounds (right.removeFromTop (right.getHeight() / 2).reduced (2));
    lockBtn.setBounds (right.reduced (2));
}

void SeedPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (kPanel);
    g.fillRoundedRectangle (r, 6.0f);
    auto left = r.removeFromLeft (r.getWidth() * 0.5f).reduced (8.0f, 4.0f);
    g.setColour (kText2);
    g.setFont (monoFont (9.0f));
    g.drawText ("LAST SEED", left.removeFromTop (12.0f), juce::Justification::centredLeft, false);
    g.setColour (kAccent);
    g.setFont (monoFont (18.0f));
    g.drawText (juce::String (shown).paddedLeft ('0', 4), left, juce::Justification::centredLeft, false);
}

// =============================================================================
//  PresetBar
// =============================================================================
PresetBar::PresetBar (WaveTreeProcessor* p) : proc (p)
{
    addAndMakeVisible (box);
    addAndMakeVisible (saveBtn);
    box.setTextWhenNothingSelected ("presets...");
    box.setTooltip ("Factory + user presets.");
    saveBtn.setTooltip ("Save the current sound (all parameters + tree edits) as a named user preset.");
    refresh();
    box.onChange = [this]
    {
        const int idx = box.getSelectedId() - 1;
        if (idx >= 0 && proc != nullptr) proc->applyPreset (idx);
    };
    saveBtn.onClick = [this]
    {
        if (proc == nullptr) return;
        auto* w = new juce::AlertWindow ("Save preset", "Preset name:", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", "", {});
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
        {
            if (result == 1 && proc != nullptr)
                proc->saveUserPreset (w->getTextEditorContents ("name"));
            refresh();
        }), true);
    };
}

void PresetBar::refresh()
{
    if (proc == nullptr) return;
    box.clear (juce::dontSendNotification);
    int id = 1;
    const int nf = proc->numFactoryPresets();
    for (auto& n : proc->getPresetNames())
    {
        box.addItem (n, id);
        if (id == nf) box.addSeparator();
        ++id;
    }
}

void PresetBar::resized()
{
    auto r = getLocalBounds().reduced (4);
    saveBtn.setBounds (r.removeFromRight (64).reduced (2));
    box.setBounds (r.reduced (2));
}

// =============================================================================
//  SigilView — dyadic-tree glyph + BETA (identity without any title text)
// =============================================================================
void SigilView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (6.0f);
    auto beta = r.removeFromRight (44.0f).withSizeKeepingCentre (40.0f, 18.0f);
    auto mark = r.withSizeKeepingCentre (std::min (r.getWidth(), 90.0f), r.getHeight());

    // dyadic branching glyph: root splitting into scaled halves, 4 generations
    g.setColour (kAccent);
    const float x0 = mark.getX(), w = mark.getWidth();
    const float top = mark.getY() + 3.0f, bot = mark.getBottom() - 3.0f;
    const int gens = 4;
    for (int d = 0; d < gens; ++d)
    {
        const int n = 1 << d;
        const float y  = top + (bot - top) * (float) d / (float) (gens - 1);
        const float yN = top + (bot - top) * (float) (d + 1) / (float) (gens - 1);
        for (int k = 0; k < n; ++k)
        {
            const float cx = x0 + w * ((float) k + 0.5f) / (float) n;
            g.fillEllipse (cx - 2.0f, y - 2.0f, 4.0f, 4.0f);
            if (d < gens - 1)
                for (int c = 0; c < 2; ++c)
                {
                    const float nx = x0 + w * ((float) (2 * k + c) + 0.5f) / (float) (2 * n);
                    g.drawLine (cx, y, nx, yN, d == 0 ? 1.6f : 1.0f);
                }
        }
    }

    // BETA tag
    g.setColour (kAccent);
    g.fillRoundedRectangle (beta, 4.0f);
    g.setColour (kBg);
    g.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Bold")));
    g.drawText ("BETA", beta, juce::Justification::centred, false);
}

// =============================================================================
//  SyncKnob — the three-param tempo-sync convention (<base>Sync / <base>Ms /
//  <base>Div) as ONE knob: the chip underneath switches between free time and
//  host-tempo beat divisions, and the knob swaps its attachment to match.
// =============================================================================
namespace
{
class SyncKnob : public juce::Component,
                 public juce::SettableTooltipClient,
                 private juce::AudioProcessorValueTreeState::Listener
{
public:
    static constexpr int kReadoutH = 16;
    static constexpr int kChipH    = 18;

    SyncKnob (WaveTreeProcessor* p, const juce::String& baseId) : proc (p), base (baseId)
    {
        for (auto* s : { &msSlider, &divSlider })
        {
            s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, kReadoutH);
        }
        addAndMakeVisible (msSlider);
        addChildComponent (divSlider);
        addAndMakeVisible (chip);
        chip.setTooltip ("Switch between free time (ms) and host-tempo beat divisions.");

        if (proc == nullptr) return;
        auto& st = proc->state();
        for (auto* suffix : { "Sync", "Ms", "Div" })
            if (st.getParameter (base + suffix) == nullptr) return;

        using APVTS = juce::AudioProcessorValueTreeState;
        msAtt   = std::make_unique<APVTS::SliderAttachment> (st, base + "Ms",   msSlider);
        divAtt  = std::make_unique<APVTS::SliderAttachment> (st, base + "Div",  divSlider);
        chipAtt = std::make_unique<APVTS::ButtonAttachment> (st, base + "Sync", chip);
        st.addParameterListener (base + "Sync", this);
        listening = true;
        showBeats (st.getRawParameterValue (base + "Sync")->load() > 0.5f);
    }

    ~SyncKnob() override
    {
        if (listening) proc->state().removeParameterListener (base + "Sync", this);
    }

    void setTooltip (const juce::String& tip) override
    {
        juce::SettableTooltipClient::setTooltip (tip);
        msSlider.setTooltip (tip);
        divSlider.setTooltip (tip);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto chipRow = r.removeFromBottom (kChipH);
        chip.setBounds (chipRow.withSizeKeepingCentre (juce::jmin (52, chipRow.getWidth()), kChipH - 2));
        msSlider.setBounds (r);
        divSlider.setBounds (r);
    }

private:
    void parameterChanged (const juce::String&, float v) override
    {
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<SyncKnob> (this), v]
            { if (sp != nullptr) sp->showBeats (v > 0.5f); });
    }

    void showBeats (bool beats)
    {
        msSlider.setVisible (! beats);
        divSlider.setVisible (beats);
        chip.setButtonText (beats ? "beat" : "ms");
    }

    WaveTreeProcessor* proc = nullptr;
    juce::String base;
    bool listening = false;
    juce::Slider msSlider, divSlider;
    juce::ToggleButton chip { "ms" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> msAtt, divAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> chipAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SyncKnob)
};
} // namespace

// =============================================================================
//  GuiItem wrappers + registration
// =============================================================================
namespace
{
class SyncKnobItem : public foleys::GuiItem
{
public:
    FOLEYS_DECLARE_GUI_FACTORY (SyncKnobItem)

    SyncKnobItem (foleys::MagicGUIBuilder& builder, const juce::ValueTree& node)
        : foleys::GuiItem (builder, node) { update(); }

    juce::Component* getWrappedComponent() override { return knob.get(); }

    void update() override
    {
        const auto base = configNode.getProperty ("base", "attack").toString();
        if (knob != nullptr && base == currentBase) return;

        currentBase = base;
        knob = std::make_unique<SyncKnob> (
            dynamic_cast<WaveTreeProcessor*> (magicBuilder.getMagicState().getProcessor()), base);
        addAndMakeVisible (*knob);
        resized();
    }

    std::vector<foleys::SettableProperty> getSettableProperties() const override
    {
        return { { configNode, "base", foleys::SettableProperty::Text, "attack", {} } };
    }

private:
    std::unique_ptr<SyncKnob> knob;
    juce::String currentBase;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SyncKnobItem)
};

template <typename CompT>
class WtItem : public foleys::GuiItem
{
public:
    FOLEYS_DECLARE_GUI_FACTORY (WtItem)

    WtItem (foleys::MagicGUIBuilder& builder, const juce::ValueTree& node)
        : foleys::GuiItem (builder, node),
          comp (dynamic_cast<WaveTreeProcessor*> (builder.getMagicState().getProcessor()))
    {
        addAndMakeVisible (comp);
    }

    juce::Component* getWrappedComponent() override { return &comp; }
    void update() override {}

private:
    CompT comp;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WtItem)
};

// SigilView takes no processor
class SigilItem : public foleys::GuiItem
{
public:
    FOLEYS_DECLARE_GUI_FACTORY (SigilItem)
    SigilItem (foleys::MagicGUIBuilder& builder, const juce::ValueTree& node)
        : foleys::GuiItem (builder, node) { addAndMakeVisible (comp); }
    juce::Component* getWrappedComponent() override { return &comp; }
    void update() override {}
private:
    SigilView comp;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SigilItem)
};
} // namespace

void registerWaveTreeGuiItems (foleys::MagicGUIBuilder& builder)
{
    builder.registerFactory ("TreeView",   &WtItem<CoeffTreeView>::factory);
    builder.registerFactory ("WaveScope",  &WtItem<WaveScopeView>::factory);
    builder.registerFactory ("DhSpectrum", &WtItem<DhSpectrumView>::factory);
    builder.registerFactory ("SeedPanel",  &WtItem<SeedPanel>::factory);
    builder.registerFactory ("PresetBar",  &WtItem<PresetBar>::factory);
    builder.registerFactory ("Sigil",      &SigilItem::factory);
    builder.registerFactory ("SyncKnob",   &SyncKnobItem::factory);
}
