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

// One scale for the whole tree, tied to the generated peak, so the bars fall
// with the tree's decay toward the fine rows. A painted bar is as high as it
// was drawn, and moves with the tree when the slope changes (paintedMagnitude).
float CoeffTreeView::shownLevel (int b, int k) const
{
    using wavetree::TreeEdit;
    if (proc == nullptr) return 0.0f;
    const int   i     = bandOff (b) + k;
    const auto& tree  = proc->treeEdit();
    const float level = tree.getNodeLevel (i);
    if (level >= 0.0f)
        return (float) TreeEdit::magnitudeToLevel (wavetree::paintedMagnitude (level, tree.getNodeSlope (i), (double) treePeak,
                                                                               treePeakBand, b, proc->treeSlope()),
                                                   (double) treePeak);

    return (float) TreeEdit::magnitudeToLevel ((double) mags[(size_t) i] * (double) tree.getNodeGain (i), (double) treePeak);
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
        treePeak = 0.0f; treePeakBand = 0;
        for (int b = 0; b < kBands; ++b)
            for (int k = 0; k < bandSize (b); ++k)
                if (mags[(size_t) (bandOff (b) + k)] > treePeak) { treePeak = mags[(size_t) (bandOff (b) + k)]; treePeakBand = b; }
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

// A stroke belongs to the row it started in while the pointer stays near it:
// the rows are thin and the mouse overshoots, so a little above the row is the
// top of the scale and a little below it is silence. Further than that, the
// stroke is tracing a BONE: in every other row it passes, the single node under
// the pointer takes the stroke's level — click a coarse node and drag up
// through the finer scales to build a branch.
void CoeffTreeView::applyEdit (const juce::MouseEvent& e, bool erase)
{
    if (proc == nullptr || strokeBand < 0) return;
    auto& tree = proc->treeEdit();
    const int b = strokeBand;

    // Only a climb into the FINER rows above leaves the row the stroke started in
    // (and only past 40 % of the next row, so a small overshoot is still "the
    // top"). Below its row a stroke stays put: the level is 0, so a drag to the
    // bottom of the view mutes every node it sweeps.
    if (! strokeInGutter && b + 1 < kBands)
    {
        const auto home = rowArea (b);
        if (boneMode || e.position.y < home.getY() - 0.4f * home.getHeight())
        {
            extendBone (e, erase);
            return;
        }
    }

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
            else       tree.setNodeLevel (off + node, fromLevel + t * (level - fromLevel), (float) proc->treeSlope());
        }
        lastNode  = k;
        lastLevel = level;
    }
    tree.bump();
}

// A bone: from the node the stroke left its row at, ONE node per finer row,
// each the child of the node below it nearest the pointer (a branch of the
// dyadic tree). Each row the bone reaches holds only that node until the
// button is released; while the pointer is in one of those rows, that row's
// node follows the pointer's height (so a climb through a row leaves its node
// at the top, and coming back down adjusts the same node). Rows a fast drag
// skipped take the level of the node below them. Below the start row the
// start node's level is 0, as for any stroke.
void CoeffTreeView::extendBone (const juce::MouseEvent& e, bool erase)
{
    auto& tree = proc->treeEdit();
    const int   b     = strokeBand;
    const float slope = (float) proc->treeSlope();
    if (! boneMode)
    {
        boneMode = true;
        boneTop  = b;
        boneAt.fill (-1);
        boneAt[(size_t) b] = juce::jmax (0, lastNode);     // the start node keeps the level it has
    }

    int row = bandAtY ((float) e.position.y);
    if (row < 0) row = e.position.y < rowArea (kBands - 1).getY() ? kBands - 1 : b;   // above the view: the finest row
    row = juce::jmax (row, b);

    auto heightIn = [this, &e] (int nb)
    {
        const auto grid = gridArea (nb);
        return juce::jlimit (0.0f, 1.0f, (grid.getBottom() - e.position.y) / grid.getHeight());
    };
    auto put = [&] (int nb, float level)
    {
        const int i = bandOff (nb) + boneAt[(size_t) nb];
        if (erase) tree.clearNode (i);
        else       tree.setNodeLevel (i, level, slope);
    };

    for (int nb = boneTop + 1; nb <= row; ++nb)           // claim the rows reached since the last event
    {
        const int  n    = bandSize (nb), r = n / bandSize (nb - 1);
        const int  up   = boneAt[(size_t) (nb - 1)];
        const auto grid = gridArea (nb);
        const int  at   = (int) std::floor ((e.position.x - grid.getX()) / grid.getWidth() * (float) n);
        boneAt[(size_t) nb] = juce::jlimit (up * r, up * r + r - 1, at);
        const float below = tree.getNodeLevel (bandOff (nb - 1) + up);
        put (nb, nb == row ? heightIn (nb) : juce::jmax (0.0f, below));
        boneTop = nb;
    }
    put (row, heightIn (row));                            // the row under the pointer: its one node follows
    tree.bump();
}

void CoeffTreeView::mouseDown (const juce::MouseEvent& e)
{
    strokeBand = bandAtY ((float) e.position.y);
    strokeInGutter = strokeBand >= 0 && e.position.x < rowArea (strokeBand).getX() + kGutterW;
    lastNode = -1;
    boneMode = false; boneTop = -1;
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
    setTooltip ("D(h): multifractal Legendre spectrum of the coefficient tree. "
                "The arch widens with Intermittency; Hurst moves its centre. KEY: the last key played. "
                "CHORD: every sounding voice's tree together (moments added per band). "
                "FIT: the Cascade generator's closed-form parabola, dashed.");
    readProps();
    startTimerHz (10);
}

void DhSpectrumView::readProps()
{
    if (proc == nullptr) return;
    auto& st = proc->magicStateRef();
    const bool c = st.getPropertyAsValue (kChordProp).getValue().toString() == "1";
    const bool a = st.getPropertyAsValue (kAnalyticProp).getValue().toString() == "1";
    if (c != chord || a != analytic) { chord = c; analytic = a; lastStamp = 0xFFFFFFFFFFFFFFFFull; repaint(); }
}

void DhSpectrumView::setChordMode (bool on)
{
    if (proc != nullptr) proc->magicStateRef().getPropertyAsValue (kChordProp).setValue (on ? "1" : "0");
    readProps();
}

void DhSpectrumView::setAnalyticShown (bool on)
{
    if (proc != nullptr) proc->magicStateRef().getPropertyAsValue (kAnalyticProp).setValue (on ? "1" : "0");
    readProps();
}

void DhSpectrumView::recompute()
{
    constexpr int kFrame  = wavetree::WaveletVoice::kFrame;
    constexpr int kLevels = wavetree::WaveletVoice::kLevels;
    auto bandSize = [] (int b) { return b == 0 ? (kFrame >> kLevels) : (kFrame >> (kLevels - b + 1)); };
    auto bandOff  = [&] (int b) { int o = kFrame >> kLevels; for (int i = 1; i < b; ++i) o += bandSize (i); return b == 0 ? 0 : o; };
    if (chord)
    {
        numVoices = proc->copyChordCoeffMags (mags, info);
        std::vector<const float*> sets;
        for (int v = 0; v < numVoices; ++v) sets.push_back (mags.data() + (size_t) v * kFrame);
        mfa = wavetree::computeDhMulti (sets.data(), numVoices, kLevels + 1, bandOff, bandSize);
    }
    else
    {
        proc->copyCoeffMags (mags);
        info.assign (1, proc->shownVoiceInfo());
        numVoices = 1;
        mfa = wavetree::computeDh (mags.data(), kLevels + 1, bandOff, bandSize);
    }
    repaint();
}

void DhSpectrumView::timerCallback()
{
    if (proc == nullptr) return;
    readProps();
    const std::uint64_t stamp = chord ? proc->chordStamp() : (std::uint64_t) proc->coeffStamp();
    if (stamp == lastStamp) return;
    lastStamp = stamp;
    recompute();
}

juce::Rectangle<float> DhSpectrumView::chipKey() const   { return { 40.0f, 3.0f, 30.0f, 13.0f }; }
juce::Rectangle<float> DhSpectrumView::chipChord() const { return { 72.0f, 3.0f, 40.0f, 13.0f }; }
juce::Rectangle<float> DhSpectrumView::chipFit() const   { return { 118.0f, 3.0f, 26.0f, 13.0f }; }

void DhSpectrumView::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    if (chipKey().contains (p))        setChordMode (false);
    else if (chipChord().contains (p)) setChordMode (true);
    else if (chipFit().contains (p))   setAnalyticShown (! analytic);
}

double DhSpectrumView::fitCentre() const
{
    for (const auto& vi : info)
        if (vi.mode == wavetree::Mode::Cascade) return vi.slope;
    return std::numeric_limits<double>::quiet_NaN();
}

void DhSpectrumView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (kBg);
    g.fillRoundedRectangle (b, 6.0f);

    // fixed axes, so arches of different trees can be compared by eye. The
    // Cascade generator puts the arch's centre at h* = 0.35 + 1.6 Hurst
    // (CoeffGen.h), so h runs from -1 to 3; D(h) from 0 to 1.2 (never above 1).
    constexpr double kHLo = -1.0, kHHi = 3.0, kDLo = 0.0, kDHi = 1.2;
    auto plot = b.reduced (10.0f, 8.0f);
    plot.removeFromTop (10.0f);                      // the title row: D(h) and the chips
    const auto readoutRow = plot.removeFromTop (12.0f);   // the numbers, on their own line
    plot.removeFromBottom (11.0f);                   // room for the h ticks
    plot.removeFromLeft (14.0f);                     // room for the D ticks
    const auto hLabel = plot.removeFromRight (12.0f).removeFromBottom (11.0f).translated (0.0f, 11.0f);

    g.setFont (monoFont (9.5f));
    g.setColour (kText2);
    g.drawText ("D(h)", b.reduced (8.0f, 4.0f), juce::Justification::topLeft, false);

    // the chips: KEY | CHORD, FIT
    auto chip = [&] (juce::Rectangle<float> r, const char* text, bool on)
    {
        g.setColour (on ? kAccent : kOutline.withAlpha (0.6f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (on ? kBg : kText2);
        g.setFont (monoFont (8.0f));
        g.drawText (text, r, juce::Justification::centred, false);
    };
    chip (chipKey(), "KEY", ! chord);
    chip (chipChord(), "CHORD", chord);
    chip (chipFit(), "FIT", analytic);

    auto X = [&] (double h) { return plot.getX() + (float) ((h - kHLo) / (kHHi - kHLo)) * plot.getWidth(); };
    auto Y = [&] (double d) { return plot.getBottom() - (float) ((d - kDLo) / (kDHi - kDLo)) * plot.getHeight(); };

    // grid and ticks: h = -1 .. 3 along the bottom; D = 0, 1 at the left
    g.setFont (monoFont (8.0f));
    for (int hi = -1; hi <= 3; ++hi)
    {
        const float x = X (hi);
        g.setColour (kOutline.withAlpha (hi == 0 ? 0.7f : 0.35f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (kText2);
        g.drawText (juce::String (hi), juce::Rectangle<float> (x - 10.0f, plot.getBottom() + 1.0f, 20.0f, 10.0f),
                    juce::Justification::centred, false);
    }
    for (int di = 0; di <= 1; ++di)
    {
        const float y = Y (di);
        g.setColour (kOutline.withAlpha (di == 1 ? 0.7f : 0.35f));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (kText2);
        g.drawText (juce::String (di), juce::Rectangle<float> (plot.getX() - 14.0f, y - 5.0f, 12.0f, 10.0f),
                    juce::Justification::centredRight, false);
    }
    g.setColour (kText2);
    g.drawText ("h", hLabel, juce::Justification::centredRight, false);

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());        // a spectrum outside the window is cut, not stretched

    // the analytic parabola(s), dashed: one per voice in the chord (their envelope is the chord's)
    if (analytic)
    {
        const float dashes[] = { 3.0f, 3.0f };
        for (const auto& vi : info)
        {
            if (vi.mode != wavetree::Mode::Cascade) continue;
            juce::Path pth;
            bool started = false;
            for (int i = 0; i <= 160; ++i)
            {
                const double h = kHLo + (kHHi - kHLo) * i / 160.0;
                const double d = wavetree::analyticDhAt (vi.slope, vi.intermittency, h);
                if (d < kDLo - 0.05) { started = false; continue; }
                const float x = X (h), y = Y (juce::jlimit (kDLo, kDHi, d));
                if (! started) { pth.startNewSubPath (x, y); started = true; } else pth.lineTo (x, y);
            }
            juce::Path dashed;
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, pth, dashes, 2);
            g.setColour (kAccent.withAlpha (0.55f));
            g.fillPath (dashed);
        }
    }

    if (! mfa.valid || mfa.count < 3)
    {
        g.restoreState();
        g.setColour (kAccDim); g.setFont (monoFont (9.5f)); g.drawText ("--", plot, juce::Justification::centred, false);
        return;
    }

    juce::Path pth;
    bool started = false;
    for (int i = 0; i < mfa.count; ++i)
    {
        const float x = X (mfa.h[i]), y = Y (juce::jlimit (kDLo, kDHi, mfa.Dh[i]));
        if (! started) { pth.startNewSubPath (x, y); started = true; }
        else pth.lineTo (x, y);
    }
    g.setColour (kAccent);
    g.strokePath (pth, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));
    g.restoreState();

    g.setColour (kText);
    g.setFont (monoFont (9.0f));
    juce::String read = juce::String::formatted ("h* %.2f  [%.2f, %.2f]", mfa.hPeak, mfa.hMin, mfa.hMax);
    if (chord) read << "  " << numVoices << (numVoices == 1 ? " voice" : " voices");
    g.drawText (read, readoutRow, juce::Justification::centredRight, false);
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
//  SyncKnob (declared in PluginEditor.h)
// =============================================================================
SyncKnob::SyncKnob (WaveTreeProcessor* p, const juce::String& baseId) : proc (p), base (baseId)
{
    for (auto* sl : { &msSlider, &divSlider })
    {
        sl->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        sl->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, kReadoutH);
    }
    addAndMakeVisible (msSlider);
    addChildComponent (divSlider);
    addAndMakeVisible (chip);
    addChildComponent (familyChip);
    chip.setTooltip ("Switch between free time (ms) and host-tempo beat divisions.");
    familyChip.setTooltip ("Which beat values the knob steps through, in order of length: all, straight (1/n), triplets (T) or dotted (.)");
    familyChip.onClick = [this] { setFamily ((family + 1) % 4); };

    // in beats the knob walks a FAMILY of divisions in duration order, not the
    // parameter's table order (which interleaves straight, triplet and dotted)
    divSlider.setRange (0.0, 1.0, 1.0);
    divSlider.textFromValueFunction = [this] (double v)
    {
        const int k = juce::jlimit (0, juce::jmax (0, familyCount - 1), (int) std::lround (v));
        return familyCount > 0 ? juce::String (acv2::tempo::label (familyIdx[k])) : juce::String();
    };
    divSlider.valueFromTextFunction = [this] (const juce::String& t)
    {
        const int idx = acv2::tempo::indexOfLabel (t.trim().toStdString());
        return idx < 0 ? divSlider.getValue() : (double) acv2::tempo::familyPosition ((acv2::tempo::Family) family, idx);
    };
    divSlider.onValueChange = [this]
    {
        if (settingDiv || proc == nullptr || familyCount == 0) return;
        const int k = juce::jlimit (0, familyCount - 1, (int) std::lround (divSlider.getValue()));
        if (auto* par = proc->state().getParameter (base + "Div"))
        {
            par->beginChangeGesture();
            par->setValueNotifyingHost (par->convertTo0to1 ((float) familyIdx[k]));
            par->endChangeGesture();
        }
    };

    if (proc == nullptr) return;
    auto& st = proc->state();
    for (auto* suffix : { "Sync", "Ms", "Div" })
        if (st.getParameter (base + suffix) == nullptr) return;

    using APVTS = juce::AudioProcessorValueTreeState;
    msAtt   = std::make_unique<APVTS::SliderAttachment> (st, base + "Ms",   msSlider);
    chipAtt = std::make_unique<APVTS::ButtonAttachment> (st, base + "Sync", chip);
    st.addParameterListener (base + "Sync", this);
    st.addParameterListener (base + "Div", this);
    listening = true;
    family = juce::jlimit (0, 3, (int) proc->magicStateRef().getPropertyAsValue (base + "Family").getValue());
    rebuildFamily();
    showBeats (st.getRawParameterValue (base + "Sync")->load() > 0.5f);
}

SyncKnob::~SyncKnob()
{
    if (listening)
    {
        proc->state().removeParameterListener (base + "Sync", this);
        proc->state().removeParameterListener (base + "Div", this);
    }
}

void SyncKnob::setTooltip (const juce::String& tip)
{
    juce::SettableTooltipClient::setTooltip (tip);
    msSlider.setTooltip (tip);
    divSlider.setTooltip (tip);
}

void SyncKnob::resized()
{
    auto r = getLocalBounds();
    auto chipRow = r.removeFromBottom (kChipH);
    const int w = juce::jmin (52, chipRow.getWidth() / 2 - 2);
    auto pair = chipRow.withSizeKeepingCentre (juce::jmin (chipRow.getWidth(), 2 * w + 4), kChipH - 2);
    chip.setBounds (pair.removeFromLeft (w));
    familyChip.setBounds (pair.removeFromRight (w));
    msSlider.setBounds (r);
    divSlider.setBounds (r);
}

int  SyncKnob::familyMode() const { return family; }

void SyncKnob::setFamily (int f)
{
    family = juce::jlimit (0, 3, f);
    if (proc != nullptr) proc->magicStateRef().getPropertyAsValue (base + "Family").setValue (family);
    rebuildFamily();
}

// the knob's steps: the family's divisions, shortest first
void SyncKnob::rebuildFamily()
{
    familyCount = acv2::tempo::familyOrder ((acv2::tempo::Family) family, familyIdx, kMaxFamily);
    familyChip.setButtonText (acv2::tempo::familyName ((acv2::tempo::Family) family));
    const juce::ScopedValueSetter<bool> guard (settingDiv, true);
    divSlider.setRange (0.0, (double) juce::jmax (0, familyCount - 1), 1.0);
    followDiv();
    divSlider.updateText();
}

// the parameter moved (host, preset, our own write): put the knob on it
void SyncKnob::followDiv()
{
    if (proc == nullptr || familyCount == 0) return;
    const int idx = (int) proc->state().getRawParameterValue (base + "Div")->load();
    const int k = acv2::tempo::familyPosition ((acv2::tempo::Family) family, idx);
    const juce::ScopedValueSetter<bool> guard (settingDiv, true);
    divSlider.setValue ((double) k, juce::dontSendNotification);
    divSlider.updateText();
}

void SyncKnob::parameterChanged (const juce::String& id, float v)
{
    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<SyncKnob> (this), id, v]
    {
        if (sp == nullptr) return;
        if (id.endsWith ("Sync")) sp->showBeats (v > 0.5f);
        else                      sp->followDiv();
    });
}

void SyncKnob::showBeats (bool beats)
{
    msSlider.setVisible (! beats);
    divSlider.setVisible (beats);
    familyChip.setVisible (beats);
    chip.setButtonText (beats ? "beat" : "ms");
}

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
