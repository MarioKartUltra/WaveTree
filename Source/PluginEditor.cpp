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
                "Drag in the left gutter to set a band gain (drawbar). Paint in a row to set "
                "node gains (top = 2x, bottom = mute). Right-drag erases. Double-click the "
                "gutter to reset a band, double-click the grid to reset everything.");
    startTimerHz (30);
}

juce::Rectangle<float> CoeffTreeView::rowArea (int b) const
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float rh = r.getHeight() / (float) kBands;
    return { r.getX(), r.getBottom() - rh * (float) (b + 1), r.getWidth(), rh };
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
        for (int b = 0; b < kBands; ++b)
        {
            float pk = 1.0e-9f;
            const int off = bandOff (b), n = bandSize (b);
            for (int k = 0; k < n; ++k) pk = std::max (pk, mags[(size_t) (off + k)]);
            bandPeak[(size_t) b] = pk;
        }
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

        // nodes
        row.reduce (1.0f, 1.0f);
        const int off = bandOff (b), n = bandSize (b);
        const float cw = row.getWidth() / (float) n;
        const float pk = bandPeak[(size_t) b];
        for (int k = 0; k < n; ++k)
        {
            const float ng  = tree.getNodeGain (off + k);
            const float mag = juce::jlimit (0.0f, 1.0f, mags[(size_t) (off + k)] / pk);
            const float h   = std::max (1.5f, row.getHeight() * mag * juce::jlimit (0.0f, 1.0f, ng * 0.5f + 0.06f));
            juce::Rectangle<float> cell (row.getX() + cw * (float) k, row.getBottom() - h,
                                         std::max (1.0f, cw - (cw > 3.0f ? 1.0f : 0.0f)), h);
            const bool edited = std::abs (ng - 1.0f) > 0.01f;
            auto col = kAccent.withAlpha (0.25f + 0.65f * mag);
            if (ng < 0.02f)      col = kOutline.withAlpha (0.5f);
            else if (edited)     col = kAccent.brighter (0.25f).withAlpha (0.4f + 0.6f * juce::jlimit (0.0f, 1.0f, ng * 0.5f));
            g.setColour (col.withMultipliedAlpha (bg < 0.02f ? 0.25f : 1.0f));
            g.fillRect (cell);
        }

        // row hairline
        g.setColour (kOutline.withAlpha (0.35f));
        g.drawHorizontalLine ((int) row.getBottom(), row.getX(), row.getRight());
    }
}

void CoeffTreeView::applyEdit (const juce::MouseEvent& e, bool erase)
{
    if (proc == nullptr) return;
    auto& tree = proc->treeEdit();
    const int b = bandAtY ((float) e.position.y);
    if (b < 0) return;
    auto row = rowArea (b);

    if (e.position.x < row.getX() + kGutterW)     // gutter: band gain 0..2
    {
        auto gutter = row.removeFromLeft (kGutterW).reduced (6.0f, 3.0f);
        const float g01 = juce::jlimit (0.0f, 1.0f, (e.position.x - gutter.getX()) / gutter.getWidth());
        tree.setBandGain (b, erase ? 1.0f : g01 * 2.0f);
    }
    else                                          // grid: per-node gain paint
    {
        row.removeFromLeft (kGutterW);
        const int n = bandSize (b);
        const int k = juce::jlimit (0, n - 1, (int) ((e.position.x - row.getX()) / row.getWidth() * (float) n));
        const float yfrac = juce::jlimit (0.0f, 1.0f, (e.position.y - row.getY()) / row.getHeight());
        tree.setNodeGain (bandOff (b) + k, erase ? 1.0f : (1.0f - yfrac) * 2.0f);
    }
    tree.bump();
}

void CoeffTreeView::mouseDown (const juce::MouseEvent& e) { applyEdit (e, e.mods.isRightButtonDown() || e.mods.isAltDown()); }
void CoeffTreeView::mouseDrag (const juce::MouseEvent& e) { applyEdit (e, e.mods.isRightButtonDown() || e.mods.isAltDown()); }
void CoeffTreeView::mouseUp   (const juce::MouseEvent&)   { if (proc != nullptr) proc->treeEditGesture(); }

void CoeffTreeView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (proc == nullptr) return;
    auto& tree = proc->treeEdit();
    const int b = bandAtY ((float) e.position.y);
    if (b >= 0 && e.position.x < rowArea (b).getX() + kGutterW)
    {
        tree.setBandGain (b, 1.0f);
        const int off = bandOff (b), n = bandSize (b);
        for (int k = 0; k < n; ++k) tree.setNodeGain (off + k, 1.0f);
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
//  GuiItem wrappers + registration
// =============================================================================
namespace
{
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
}
