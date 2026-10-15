// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// =============================================================================
//  WaveTree II — offline editor screenshot tool (NOT part of the plugin build).
//  Links the juce_add_plugin shared-code target, constructs the processor and
//  its foleys editor entirely offscreen, drives audio/MIDI so every analyser has
//  real content, and writes one 2x PNG per UI state.
//
//  NOTE on the Mode fascia panels: magic.xml puts visibility="viewBands" /
//  "viewCascade" / "viewAtoms" on <View> nodes, but foleys::Container::
//  getWrappedComponent() returns nullptr and GuiItem::configureComponent()
//  early-returns on nullptr BEFORE the visibility referTo (Layout/
//  foleys_GuiItem.cpp:127-152). The Value is therefore never connected and the
//  three fascia panels are permanently visible, in this tool AND in any host.
//  These shots are faithful to that; do not "fix" them by faking visibility.
//
//  Builds no bundle, installs nothing, touches no plugin folder.
//    cmake --build build --target wavetree_shot -j4
//    ./build/wavetree_shot [outdir]
// =============================================================================
#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <CoreFoundation/CoreFoundation.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>
#include <cstdlib>
#include <typeinfo>

// -----------------------------------------------------------------------------
// JUCE_MODAL_LOOPS_PERMITTED is 0 in this build, so MessageManager::
// runDispatchLoopUntil does not exist. JUCE's macOS message queue is a
// CFRunLoopSource on the main run loop, and juce::Timer callbacks arrive through
// it, so pumping CFRunLoop is equivalent.
static void pump (double seconds)
{
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    while (juce::Time::getMillisecondCounterHiRes() - t0 < seconds * 1000.0)
        CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.02, false);
}

// -----------------------------------------------------------------------------
struct Stats { double stddev = 0.0; int distinctBuckets = 0; };

static Stats measure (const juce::Image& img)
{
    Stats s;
    if (! img.isValid()) return s;

    const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
    const int stepX = juce::jmax (1, img.getWidth()  / 400);
    const int stepY = juce::jmax (1, img.getHeight() / 400);

    std::vector<int> hist (256, 0);
    double sum = 0.0, sum2 = 0.0;
    long   n = 0;

    for (int y = 0; y < img.getHeight(); y += stepY)
        for (int x = 0; x < img.getWidth(); x += stepX)
        {
            const auto c = bd.getPixelColour (x, y);
            const double l = 0.2126 * c.getRed() + 0.7152 * c.getGreen() + 0.0722 * c.getBlue();
            sum += l; sum2 += l * l; ++n;
            ++hist[(size_t) juce::jlimit (0, 255, (int) l)];
        }

    if (n > 0)
    {
        const double mean = sum / (double) n;
        s.stddev = std::sqrt (juce::jmax (0.0, sum2 / (double) n - mean * mean));
    }
    for (int b = 0; b < 256; ++b)
        if (hist[(size_t) b] > n / 2000)      // ignore single stray pixels
            ++s.distinctBuckets;
    return s;
}

// -----------------------------------------------------------------------------
static void setP (juce::AudioProcessorValueTreeState& apvts, const char* id, float realValue)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
    else
        std::printf ("  WARN unknown parameter '%s'\n", id);
}

static juce::String slugify (const juce::String& name)
{
    juce::String out;
    bool lastDash = false;
    for (auto c : name)
    {
        if (juce::CharacterFunctions::isLetterOrDigit ((juce::juce_wchar) c))
        {
            out << juce::String::charToString (c).toLowerCase();
            lastDash = false;
        }
        else if (! lastDash)
        {
            out << '-';
            lastDash = true;
        }
    }
    while (out.endsWithChar ('-'))   out = out.dropLastCharacters (1);
    while (out.startsWithChar ('-')) out = out.substring (1);
    return out;
}

// -----------------------------------------------------------------------------
struct State
{
    const char*                                  name;
    std::function<void (WaveTreeProcessor&)>     configure;   // params, before prepare
    std::function<void (WaveTreeProcessor&)>     preNotes;    // tree painting etc.
    bool                                         drive = true;
};

// Diagnostic: dump the editor's component tree (visibility + bounds + C++ type).
static void dumpTree (juce::Component& c, int depth = 0)
{
    juce::String pad;
    for (int i = 0; i < depth; ++i) pad << "  ";
    std::printf ("  %s[%s] %s b=%d,%d,%dx%d\n", pad.toRawUTF8(),
                 c.isVisible() ? "V" : " ", typeid (c).name(),
                 c.getX(), c.getY(), c.getWidth(), c.getHeight());
    if (depth < 40)
        for (int i = 0; i < c.getNumChildComponents(); ++i)
            dumpTree (*c.getChildComponent (i), depth + 1);
}

// -----------------------------------------------------------------------------
//  Control wiring self-test (--selftest): operate every control that is on
//  screen and check that exactly one parameter follows it. Repeated per Mode
//  and with every sync chip flipped, so hidden panels and the beat-division
//  knobs are covered too. Finishes by listing parameters no control reached.
// -----------------------------------------------------------------------------
// The editor stays offscreen (no peer), so isShowing() is always false: walk
// only through visible components instead.
template <typename T>
static void collectShowing (juce::Component& c, std::vector<T*>& out)
{
    if (! c.isVisible()) return;
    if (auto* t = dynamic_cast<T*> (&c))
        if (t->getWidth() >= 12 && t->getHeight() >= 12)
            out.push_back (t);
    for (int i = 0; i < c.getNumChildComponents(); ++i)
        collectShowing (*c.getChildComponent (i), out);
}

struct WiringTest
{
    WaveTreeProcessor& proc;
    juce::StringArray  reached;
    int                failures = 0;

    std::vector<float> snapshot() const
    {
        std::vector<float> v;
        for (auto* p : proc.getParameters()) v.push_back (p->getValue());
        return v;
    }

    // returns the IDs of the parameters that differ from the snapshot
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

    void expectOne (const juce::String& what, const juce::StringArray& changed)
    {
        if (changed.size() == 1)
        {
            reached.addIfNotAlreadyThere (changed[0]);
            return;
        }
        ++failures;
        std::printf ("  FAIL %s changed %d parameters [%s]\n", what.toRawUTF8(), changed.size(),
                     changed.joinIntoString (", ").toRawUTF8());
    }

    void run (juce::Component& editor, const char* pass)
    {
        std::vector<juce::ToggleButton*> toggles;
        std::vector<juce::Slider*>       sliders;
        std::vector<juce::ComboBox*>     combos;
        collectShowing (editor, toggles);
        collectShowing (editor, sliders);
        collectShowing (editor, combos);
        std::printf ("  [%s] on screen: %d toggles, %d knobs, %d menus\n", pass,
                     (int) toggles.size(), (int) sliders.size(), (int) combos.size());

        const bool verbose = std::getenv ("WAVETREE_SHOT_DEBUG") != nullptr;

        for (auto* t : toggles)
        {
            const auto label  = "toggle '" + t->getButtonText() + "'";
            const bool wasOn  = t->getToggleState();
            if (verbose) std::printf ("    %s\n", label.toRawUTF8());
            auto before = snapshot();
            t->triggerClick();  pump (0.06);
            expectOne (label, changedSince (before));
            if (t->getToggleState() == wasOn)
            { ++failures; std::printf ("  FAIL %s did not change state when clicked\n", label.toRawUTF8()); }

            t->triggerClick();  pump (0.06);                  // and back again
            if (! changedSince (before).isEmpty() || t->getToggleState() != wasOn)
            { ++failures; std::printf ("  FAIL %s did not return to its old state\n", label.toRawUTF8()); }
        }

        for (auto* s : sliders)
        {
            const auto   range = s->getRange();
            const double old   = s->getValue();
            const bool   low   = old < range.getStart() + 0.5 * range.getLength();
            if (verbose) std::printf ("    knob '%s'\n", s->getTextFromValue (old).toRawUTF8());
            auto before = snapshot();
            s->setValue (range.getStart() + (low ? 0.8 : 0.2) * range.getLength(), juce::sendNotificationSync);
            pump (0.03);
            expectOne ("knob showing '" + s->getTextFromValue (old) + "'", changedSince (before));
            s->setValue (old, juce::sendNotificationSync);
            pump (0.03);
        }

        for (auto* c : combos)
        {
            if (c->getNumItems() < 2) continue;
            const int  old = c->getSelectedItemIndex();
            if (old < 0) continue;                            // the preset picker: nothing selected
            if (verbose) std::printf ("    menu '%s'\n", c->getItemText (old).toRawUTF8());
            auto before = snapshot();
            c->setSelectedItemIndex ((old + 1) % c->getNumItems(), juce::sendNotificationSync);
            pump (0.06);
            expectOne ("menu showing '" + c->getItemText (old) + "'", changedSince (before));
            c->setSelectedItemIndex (old, juce::sendNotificationSync);
            pump (0.06);
        }
    }
};

// -----------------------------------------------------------------------------
//  Painting the coefficient tree with the mouse.
// -----------------------------------------------------------------------------
static CoeffTreeView* findTree (juce::Component& c)
{
    if (auto* t = dynamic_cast<CoeffTreeView*> (&c)) return t;
    for (int i = 0; i < c.getNumChildComponents(); ++i)
        if (auto* t = findTree (*c.getChildComponent (i))) return t;
    return nullptr;
}

static int testTreePainting()
{
    using wavetree::TreeEdit;
    int failures = 0;
    auto expect = [&] (bool ok, const juce::String& what)
    {
        if (ok) return;
        ++failures;
        std::printf ("  FAIL %s\n", what.toRawUTF8());
    };
    std::printf ("  [tree painting]\n");

    WaveTreeProcessor proc;
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    setP (proc.state(), "mode", 2.0f);                         // Atoms: most nodes are zero
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setVisible (true);
    pump (0.5);

    auto* view = findTree (*ed);
    expect (view != nullptr, "the tree view is in the editor");
    if (view == nullptr) return failures;

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    auto blocks = [&] (int n) { for (int i = 0; i < n; ++i) { proc.processBlock (buf, midi); midi.clear(); } };
    midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 0);
    blocks (8);
    pump (0.3);                                                // the view copies the magnitudes

    std::vector<float> mags;
    proc.copyCoeffMags (mags);
    const int band = 5;                                        // 32 nodes: wide enough to aim at
    std::vector<int> silent;
    for (int k = 0; k < CoeffTreeView::bandSize (band); ++k)
        if (mags[(size_t) (CoeffTreeView::bandOff (band) + k)] <= 0.0f) silent.push_back (k);
    expect (silent.size() >= 3, "the test band has nodes the generator left at zero");
    if (silent.size() < 3) return failures;

    auto& tree = proc.treeEdit();
    auto index = [&] (int k) { return CoeffTreeView::bandOff (band) + k; };

    // 0. the picture keeps the tree's decay: one scale for the whole tree, so the
    // fine rows are drawn lower than the coarse ones (Atoms: 0.7 octave per band,
    // seven bands = 29 dB = about half a row)
    auto tallest = [&] (int bnd)
    {
        float m = 0.0f;
        for (int k = 0; k < CoeffTreeView::bandSize (bnd); ++k) m = std::max (m, view->shownLevel (bnd, k));
        return m;
    };
    expect (tallest (9) < tallest (2) - 0.3f,
            "fine rows are drawn lower than coarse rows (d9 " + juce::String (tallest (9), 2)
            + ", d2 " + juce::String (tallest (2), 2) + ")");

    const auto src = juce::Desktop::getInstance().getMainMouseSource();
    auto event = [&] (juce::Point<float> at, juce::Point<float> from, bool right, bool dragged)
    {
        const juce::ModifierKeys mods (right ? juce::ModifierKeys::rightButtonModifier
                                             : juce::ModifierKeys::leftButtonModifier);
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (src, at, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, view, view, now, from, now, 1, dragged);
    };
    auto click = [&] (juce::Point<float> at, bool right = false)
    {
        view->mouseDown (event (at, at, right, false));
        view->mouseUp (event (at, at, right, false));
    };
    auto drag = [&] (juce::Point<float> from, juce::Point<float> to)
    {
        view->mouseDown (event (from, from, false, false));
        view->mouseDrag (event (to, from, false, true));
        view->mouseUp (event (to, from, false, true));
    };
    // a point in a node's cell, a fraction of the row's height up from the bottom
    auto pointAt = [&] (int k, float heightFraction)
    {
        const auto cell = view->nodeArea (band, k);
        return juce::Point<float> (cell.getCentreX(), cell.getBottom() - heightFraction * cell.getHeight());
    };

    // 1. a node at zero can be painted in, and it is heard
    std::vector<float> before, after;
    proc.copyWavetable (before);
    const int a = silent[0];
    expect (view->shownLevel (band, a) < 0.01f, "a node at zero shows no bar");
    click (pointAt (a, 0.75f));
    expect (std::abs (tree.getNodeLevel (index (a)) - 0.75f) < 0.03f,
            "clicking three quarters up a row paints that level (got " + juce::String (tree.getNodeLevel (index (a)), 3) + ")");
    expect (std::abs (view->shownLevel (band, a) - 0.75f) < 0.03f, "the bar is drawn as high as it was painted");
    blocks (12);                                               // the playing voice rebuilds its table
    proc.copyWavetable (after);
    float change = 0.0f;
    for (size_t i = 0; i < before.size() && i < after.size(); ++i)
        change = std::max (change, std::abs (after[i] - before[i]));
    expect (change > 0.01f, "painting a node that was at zero changes the sound");

    // 1b. the slope knobs move a painted bar with the tree: steeper tilt, a fine node goes down
    {
        const float drawn = view->shownLevel (band, a);
        setP (proc.state(), "tilt", 1.0f);                     // 1.1 octave per band steeper
        blocks (12);
        pump (0.3);
        const float steeper = view->shownLevel (band, a);
        expect (std::abs (tree.getNodeLevel (index (a)) - 0.75f) < 0.03f, "turning tilt keeps the painted level itself");
        expect (steeper < drawn - 0.2f, "a steeper tilt lowers a painted fine node with the tree ("
                                        + juce::String (drawn, 2) + " -> " + juce::String (steeper, 2) + ")");
        setP (proc.state(), "tilt", 0.5f);
        blocks (12);
        pump (0.3);
        expect (std::abs (view->shownLevel (band, a) - drawn) < 0.03f, "tilt back: the bar is where it was painted");
    }

    // 2. a small overshoot stays in the row: just past the top is the top
    const int b2 = silent[1];
    const float rowH = view->nodeArea (band, b2).getHeight();
    const auto above = view->nodeArea (band, b2).getY() - 0.25f * rowH;
    drag (pointAt (b2, 0.4f), { view->nodeArea (band, b2).getCentreX(), above });
    expect (tree.getNodeLevel (index (b2)) > 0.99f, "dragging a little above the row paints the top of that row");
    bool neighboursUntouched = true;
    for (int nb = band + 1; nb <= band + 3 && nb < CoeffTreeView::kBands; ++nb)
        for (int k = 0; k < CoeffTreeView::bandSize (nb); ++k)
            if (tree.getNodeLevel (CoeffTreeView::bandOff (nb) + k) >= 0.0f
                || std::abs (tree.getNodeGain (CoeffTreeView::bandOff (nb) + k) - 1.0f) > 1.0e-6f)
                neighboursUntouched = false;
    expect (neighboursUntouched, "and leaves the rows above alone");

    // 3. a little past the bottom mutes
    const int c = silent[2];
    const auto below = view->nodeArea (band, c).getBottom() + 0.25f * rowH;
    drag (pointAt (c, 0.5f), { view->nodeArea (band, c).getCentreX(), below });
    expect (tree.getNodeLevel (index (c)) >= 0.0f && tree.getNodeLevel (index (c)) < 0.01f,
            "dragging a little below the row paints the bottom: muted");

    // 3b. a drag UP into finer rows traces a bone: one node per finer row, each the child of
    // the one below nearest the pointer; every row holds only that node until the button is
    // let go, and that node's level follows the pointer's height while the pointer is in its row
    {
        const int top = std::min (CoeffTreeView::kBands - 1, band + 3);
        auto clearAbove = [&]
        {
            for (int nb = band + 1; nb < CoeffTreeView::kBands; ++nb)
                for (int k = 0; k < CoeffTreeView::bandSize (nb); ++k) tree.clearNode (CoeffTreeView::bandOff (nb) + k);
            tree.bump();
        };
        // which node a row holds (-1 none, -2 more than one)
        auto heldIn = [&] (int nb)
        {
            int at = -1;
            for (int k = 0; k < CoeffTreeView::bandSize (nb); ++k)
                if (tree.getNodeLevel (CoeffTreeView::bandOff (nb) + k) >= 0.0f) at = (at == -1 ? k : -2);
            return at;
        };
        auto levelOf = [&] (int nb, int k) { return k < 0 ? -1.0f : tree.getNodeLevel (CoeffTreeView::bandOff (nb) + k); };
        auto childNearest = [&] (int nb, int parent, float px)
        {
            const int n = CoeffTreeView::bandSize (nb), r = n / CoeffTreeView::bandSize (nb - 1);
            const auto g = view->gridArea (nb);
            const int under = (int) std::floor ((px - g.getX()) / g.getWidth() * (float) n);
            return juce::jlimit (parent * r, parent * r + r - 1, under);
        };
        auto heightIn = [&] (int nb, float y) { const auto g = view->gridArea (nb); return (g.getBottom() - y) / g.getHeight(); };

        // (i) the user's gesture: drag the start node to the top of its row, cross into the next
        // row, then wander sideways there, then climb on and wander in the last row
        const int start = silent[0];
        const auto cell = view->nodeArea (band, start);
        const juce::Point<float> from { cell.getCentreX(), cell.getBottom() - 0.3f * cell.getHeight() };
        const float farRight = view->gridArea (band).getRight() - 2.0f;
        const float step = 0.1f * cell.getHeight();
        const float y1 = view->rowArea (band + 1).getY() + 0.3f * view->rowArea (band + 1).getHeight();   // 70 % up row +1
        view->mouseDown (event (from, from, false, false));
        for (float y = from.y; y > y1; y -= step) view->mouseDrag (event ({ from.x, y }, from, false, true));
        view->mouseDrag (event ({ from.x, y1 }, from, false, true));
        for (float px = from.x; px <= farRight; px += 3.0f)                    // sideways in a claimed row
            view->mouseDrag (event ({ px, y1 }, from, false, true));
        const float startAfterCrossing = levelOf (band, start);
        const float nextAfterWander = levelOf (band + 1, heldIn (band + 1));
        const float yTop = view->rowArea (top).getCentreY();
        for (float y = y1; y > yTop; y -= step) view->mouseDrag (event ({ farRight, y }, from, false, true));
        for (float px = farRight; px >= view->gridArea (top).getX(); px -= 3.0f)   // sideways in the last one
            view->mouseDrag (event ({ px, yTop }, from, false, true));
        view->mouseUp (event (from, from, false, true));

        bool onePerRow = true, branch = true;
        int parent = start;
        for (int nb = band + 1; nb <= top; ++nb)
        {
            const int at = heldIn (nb);
            onePerRow = onePerRow && at >= 0;
            branch = branch && at == childNearest (nb, parent, nb == band + 1 ? from.x : farRight);
            parent = at < 0 ? 0 : at;
        }
        expect (onePerRow, "a bone paints exactly one node per finer row, however the pointer wanders");
        expect (branch, "each is the child of the one below nearest the pointer: the stroke follows a branch");
        expect (startAfterCrossing > 0.97f && levelOf (band, start) > 0.97f,
                "the start node dragged to the top of its row stays at the top when the stroke crosses on ("
                    + juce::String (startAfterCrossing, 2) + ")");
        expect (std::abs (nextAfterWander - heightIn (band + 1, y1)) < 0.03f,
                "the next row's node is at the pointer's height in that row, not collapsed ("
                    + juce::String (nextAfterWander, 2) + ")");
        expect (levelOf (band + 1, heldIn (band + 1)) > 0.85f && levelOf (band + 2, heldIn (band + 2)) > 0.85f,
                "rows climbed all the way through end at their top");
        expect (std::abs (levelOf (top, heldIn (top)) - heightIn (top, yTop)) < 0.03f, "the last row's node is where the pointer stopped");

        // (ii) back down into a row the bone already holds: the SAME node takes the new height
        {
            const int held = heldIn (band + 1);
            const juce::Point<float> f2 = from;
            view->mouseDown (event (f2, f2, false, false));
            for (float y = f2.y; y > yTop; y -= step) view->mouseDrag (event ({ f2.x, y }, f2, false, true));
            const float yLow = view->rowArea (band + 1).getBottom() - 0.2f * view->rowArea (band + 1).getHeight();
            for (float y = yTop; y < yLow; y += step) view->mouseDrag (event ({ farRight, y }, f2, false, true));
            view->mouseDrag (event ({ farRight, yLow }, f2, false, true));
            view->mouseUp (event (f2, f2, false, true));
            expect (heldIn (band + 1) == childNearest (band + 1, start, f2.x) && held >= 0,
                    "coming back down, the row still holds only its one node");
            expect (std::abs (levelOf (band + 1, heldIn (band + 1)) - heightIn (band + 1, yLow)) < 0.03f,
                    "and that node follows the pointer down");
        }
        clearAbove();

        // (iii) a jump over rows in one mouse event: an unbroken branch, the skipped rows at the level below them
        view->mouseDown (event (from, from, false, false));
        view->mouseDrag (event ({ from.x, yTop }, from, false, true));
        view->mouseUp (event (from, from, false, true));
        bool unbroken = true, carried = true;
        parent = start;
        for (int nb = band + 1; nb <= top; ++nb)
        {
            const int at = heldIn (nb);
            unbroken = unbroken && at == childNearest (nb, parent, from.x);
            if (nb < top) carried = carried && std::abs (levelOf (nb, at) - levelOf (band, start)) < 0.03f;
            parent = at < 0 ? 0 : at;
        }
        expect (unbroken, "a fast drag that skips rows still paints one node in each, along the branch");
        expect (carried, "the skipped rows take the level of the node below them");
        clearAbove();
    }

    // 3c. a drag DOWN never leaves the row it started in: past the bottom of the view it mutes
    {
        const int fine = band + 2, n = CoeffTreeView::bandSize (fine), off = CoeffTreeView::bandOff (fine);
        auto paintedBelow = [&]
        {
            int c = 0;
            for (int i = 0; i < CoeffTreeView::bandOff (fine); ++i) if (tree.getNodeLevel (i) >= 0.0f) ++c;
            return c;
        };
        const int before = paintedBelow();
        const int k0 = n / 4, k1 = k0 + 6;
        const auto c0 = view->nodeArea (fine, k0), c1 = view->nodeArea (fine, k1);
        const juce::Point<float> from { c0.getCentreX(), c0.getBottom() - 0.5f * c0.getHeight() };
        const float floorY = (float) view->getHeight() + 40.0f;
        view->mouseDown (event (from, from, false, false));
        for (float y = from.y; y < floorY; y += 6.0f) view->mouseDrag (event ({ from.x, y }, from, false, true));
        for (float px = from.x; px <= c1.getCentreX(); px += 2.0f) view->mouseDrag (event ({ px, floorY }, from, false, true));
        view->mouseUp (event (from, from, false, true));
        bool muted = true;
        for (int k = k0; k <= k1; ++k)
            muted = muted && tree.getNodeLevel (off + k) >= 0.0f && tree.getNodeLevel (off + k) < 0.01f;
        expect (muted, "a drag from a fine row to below the view mutes every node of that row it sweeps");
        expect (paintedBelow() == before, "and paints nothing in the coarser rows it passed over");
        for (int k = 0; k < n; ++k) tree.clearNode (off + k);
        tree.bump();
    }

    // 4. a drag across the row paints every node it passes
    drag (pointAt (silent[0], 0.6f), pointAt (silent[2], 0.6f));
    expect (std::abs (tree.getNodeLevel (index (silent[2])) - 0.6f) < 0.03f, "a drag along a row paints where it ends");
    bool between = true;
    for (int k = std::min (silent[0], silent[2]); k <= std::max (silent[0], silent[2]); ++k)
        between = between && std::abs (tree.getNodeLevel (index (k)) - 0.6f) < 0.03f;
    expect (between, "and every node it passed on the way");

    // 5. the tree's own peak is drawn at 90% of its row
    int peakBand = 1, peakK = 0;
    float peak = 0.0f;
    for (int bnd = 1; bnd < CoeffTreeView::kBands; ++bnd)
        for (int k = 0; k < CoeffTreeView::bandSize (bnd); ++k)
            if (mags[(size_t) (CoeffTreeView::bandOff (bnd) + k)] > peak)
            { peak = mags[(size_t) (CoeffTreeView::bandOff (bnd) + k)]; peakBand = bnd; peakK = k; }
    if (tree.getNodeLevel (CoeffTreeView::bandOff (peakBand) + peakK) < 0.0f)
        expect (std::abs (view->shownLevel (peakBand, peakK) - 0.9f) < 0.02f,
                "the largest generated coefficient is drawn at 90% of its row");

    // 6. right-click erases
    click (pointAt (a, 0.5f), true);
    expect (tree.getNodeLevel (index (a)) < 0.0f && std::abs (tree.getNodeGain (index (a)) - 1.0f) < 1.0e-6f,
            "a right-click gives the node back to the generator");

    // 7. painted levels are saved with the project
    click (pointAt (a, 0.3f));
    juce::MemoryBlock saved;
    proc.getStateInformation (saved);
    {
        WaveTreeProcessor other;
        other.setStateInformation (saved.getData(), (int) saved.getSize());
        pump (0.2);
        expect (std::abs (other.treeEdit().getNodeLevel (index (a)) - tree.getNodeLevel (index (a))) < 0.002f,
                "a painted level comes back when the project is reopened");
        bool all = true;
        int painted = 0;
        for (int i = 0; i < TreeEdit::kMaxNodes; ++i)
        {
            const float was = tree.getNodeLevel (i), is = other.treeEdit().getNodeLevel (i);
            if (was >= 0.0f) ++painted;
            all = all && ((was < 0.0f && is < 0.0f) || std::abs (was - is) < 0.002f);
        }
        expect (all && painted >= 3, "and so does every other one");
        expect (std::abs (other.treeEdit().getNodeSlope (index (a)) - tree.getNodeSlope (index (a))) < 0.002f,
                "and the slope it was painted at");
    }

    // 7b. a level saved before paint slopes existed loads at the slope the project opens with
    {
        WaveTreeProcessor old;
        old.magicStateRef().getPropertyRoot().setProperty ("treeGains", "L,100,0.500;", nullptr);
        juce::MemoryBlock oldState;
        old.getStateInformation (oldState);
        WaveTreeProcessor other;
        other.setStateInformation (oldState.getData(), (int) oldState.getSize());
        pump (0.2);
        expect (std::abs (other.treeEdit().getNodeLevel (100) - 0.5f) < 0.002f
                    && std::abs (other.treeEdit().getNodeSlope (100) - (float) wavetree::cascadeSlope (0.6)) < 0.002f,
                "an older painted level keeps its height and takes the project's slope ("
                    + juce::String (other.treeEdit().getNodeSlope (100), 3) + ")");
    }

    // 8. gains written by the version before still load as gains
    {
        WaveTreeProcessor old;
        old.treeEdit().setNodeGain (40, 0.5f);
        old.treeEdit().setBandGain (3, 1.5f);
        old.treeEditGesture();
        juce::MemoryBlock oldState;
        old.getStateInformation (oldState);
        WaveTreeProcessor other;
        other.setStateInformation (oldState.getData(), (int) oldState.getSize());
        pump (0.2);
        expect (std::abs (other.treeEdit().getNodeGain (40) - 0.5f) < 0.002f
                    && std::abs (other.treeEdit().getBandGain (3) - 1.5f) < 0.002f
                    && other.treeEdit().getNodeLevel (40) < 0.0f,
                "a project from before painted levels loads its gains");
    }

    ed.reset();
    return failures;
}


static DhSpectrumView* findDh (juce::Component& c)
{
    if (auto* t = dynamic_cast<DhSpectrumView*> (&c)) return t;
    for (int i = 0; i < c.getNumChildComponents(); ++i)
        if (auto* t = findDh (*c.getChildComponent (i))) return t;
    return nullptr;
}

// The D(h) view: KEY shows the last key, CHORD every sounding voice, FIT the analytic parabola.
static int testChordSpectrum()
{
    int failures = 0;
    auto expect = [&] (bool ok, const juce::String& what)
    {
        if (ok) return;
        ++failures;
        std::printf ("  FAIL %s\n", what.toRawUTF8());
    };
    std::printf ("  [chord spectrum]\n");

    WaveTreeProcessor proc;
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    setP (proc.state(), "mode", 1.0f);                         // Cascade: the analytic form exists
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setVisible (true);
    pump (0.5);
    auto* view = findDh (*ed);
    expect (view != nullptr, "the D(h) view is in the editor");
    if (view == nullptr) return failures;
    expect (! view->chordMode() && ! view->analyticShown(), "KEY and no FIT by default");

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    auto blocks = [&] (int n) { for (int i = 0; i < n; ++i) { proc.processBlock (buf, midi); midi.clear(); } };
    midi.addEvent (juce::MidiMessage::noteOn (1, 48, 0.9f), 0);
    midi.addEvent (juce::MidiMessage::noteOn (1, 55, 0.9f), 0);
    midi.addEvent (juce::MidiMessage::noteOn (1, 64, 0.9f), 0);
    blocks (8);
    pump (0.4);
    expect (view->result().valid && view->voicesQuantified() == 1, "KEY: one tree quantified");
    const auto key = view->result();

    view->setChordMode (true);
    pump (0.4);
    expect (view->chordMode(), "CHORD chosen");
    expect (proc.magicStateRef().getPropertyAsValue (DhSpectrumView::kChordProp).getValue().toString() == "1",
            "the choice is a state property");
    expect (view->voicesQuantified() == 3, "CHORD: three sounding voices quantified (" + juce::String (view->voicesQuantified()) + ")");
    expect (view->result().valid, "the chord spectrum is valid");
    // equal parameters per key: the chord's arch sits where the single key's does (same generator, other seeds)
    expect (std::abs (view->result().hPeak - key.hPeak) < 0.35,
            "voices with equal Hurst/Intermittency give the same arch: h* " + juce::String (view->result().hPeak, 2)
            + " vs " + juce::String (key.hPeak, 2));

    // FIT draws the parabola: more lit pixels in the plot once it is on
    auto lit = [&]
    {
        const auto img = view->createComponentSnapshot (view->getLocalBounds(), true, 1.0f);
        int n = 0;
        for (int y = 20; y < img.getHeight() - 12; ++y)
            for (int x = 16; x < img.getWidth(); ++x)
                if (img.getPixelAt (x, y).getBrightness() > 0.45f) ++n;
        return n;
    };
    const int before = lit();
    view->setAnalyticShown (true);
    pump (0.3);
    expect (view->analyticShown(), "FIT chosen");
    const int after = lit();
    expect (after > before + 40, "the dashed parabola adds to the picture (" + juce::String (before) + " -> " + juce::String (after) + ")");

    // FIT follows everything that sets the Cascade slope, Tilt included: at tilt 100 % the
    // measured arch moves 1.1 to the right and the parabola must move with it
    view->setChordMode (false);
    for (float tilt : { 0.5f, 1.0f, 0.2f })
    {
        setP (proc.state(), "tilt", tilt);
        blocks (16);
        pump (0.4);
        expect (std::abs (view->fitCentre() - view->result().hPeak) < 0.35,
                "tilt " + juce::String (tilt, 1) + ": FIT is centred on the measured arch (FIT "
                    + juce::String (view->fitCentre(), 2) + ", h* " + juce::String (view->result().hPeak, 2) + ")");
    }
    setP (proc.state(), "tilt", 0.5f);
    blocks (16);

    midi.addEvent (juce::MidiMessage::noteOff (1, 48), 0);
    midi.addEvent (juce::MidiMessage::noteOff (1, 55), 0);
    blocks (4);
    view->setChordMode (false);
    view->setAnalyticShown (false);
    pump (0.2);
    return failures;
}

static SyncKnob* findSyncKnob (juce::Component& c, const juce::String& base)
{
    if (auto* k = dynamic_cast<SyncKnob*> (&c)) if (k->baseId() == base) return k;
    for (int i = 0; i < c.getNumChildComponents(); ++i)
        if (auto* k = findSyncKnob (*c.getChildComponent (i), base)) return k;
    return nullptr;
}

// In beats the knob walks ONE family of divisions, shortest first — turning it
// up from 1/16 reaches 1/8, never 1/8T or 1/16. (the parameter's table order
// interleaves the three families, which is what the user heard as "multiples
// of 2 trigger triplets")
static int testBeatFamilies()
{
    int failures = 0;
    auto expect = [&] (bool ok, const juce::String& what)
    {
        if (ok) return;
        ++failures;
        std::printf ("  FAIL %s\n", what.toRawUTF8());
    };
    std::printf ("  [beat families]\n");
    using namespace acv2::tempo;

    WaveTreeProcessor proc;
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setVisible (true);
    pump (0.4);
    auto* knob = findSyncKnob (*ed, "lfoARate");
    expect (knob != nullptr, "the LFO A rate knob is a SyncKnob");
    if (knob == nullptr) return failures;

    setP (proc.state(), "lfoARateSync", 1.0f);
    pump (0.2);
    expect (knob->beatKnob().isVisible(), "beats shows the division knob");
    auto divNow = [&] { return (int) proc.state().getRawParameterValue ("lfoARateDiv")->load(); };

    const Family families[] = { Family::Straight, Family::Triplet, Family::Dotted, Family::All };
    for (auto f : families)
    {
        knob->setFamily ((int) f);
        pump (0.05);
        int order[64];
        const int n = familyOrder (f, order, 64);
        expect (juce::roundToInt (knob->beatKnob().getMaximum()) == n - 1,
                juce::String (familyName (f)) + ": the knob has one step per member (" + juce::String (n) + ")");
        bool onlyFamily = true, ordered = true;
        double last = -1.0;
        for (int k = 0; k < n; ++k)
        {
            knob->beatKnob().setValue ((double) k, juce::sendNotificationSync);
            pump (0.02);
            const int d = divNow();
            if (! inFamily (d, f)) onlyFamily = false;
            if (wholeNotes (d) <= last) ordered = false;
            last = wholeNotes (d);
        }
        expect (onlyFamily, juce::String (familyName (f)) + ": every step lands inside the family");
        expect (ordered, juce::String (familyName (f)) + ": steps go from short to long");
    }

    // straight family, from 1/16 one step up is 1/8 — not 1/8T, not 1/16.
    knob->setFamily ((int) Family::Straight);
    setP (proc.state(), "lfoARateDiv", (float) indexOfLabel ("1/16"));
    pump (0.1);
    const int at16 = juce::roundToInt (knob->beatKnob().getValue());
    knob->beatKnob().setValue ((double) at16 + 1.0, juce::sendNotificationSync);
    pump (0.05);
    expect (divNow() == indexOfLabel ("1/8"), "straight: one step up from 1/16 is 1/8 (got " + juce::String (label (divNow())) + ")");

    // the host moving the parameter moves the knob to the nearest member
    setP (proc.state(), "lfoARateDiv", (float) indexOfLabel ("1/4T"));
    pump (0.1);
    expect (knob->familyMode() == (int) Family::Straight, "the family is a display choice, the parameter can hold any division");
    expect (knob->beatKnob().getValue() >= (double) at16 && knob->beatKnob().getValue() <= (double) at16 + 1.0,
            "a host move to 1/4T puts the knob on the nearest straight member (between 1/16 and 1/8)");
    expect (proc.magicStateRef().getPropertyAsValue ("lfoARateFamily").getValue().toString() == "1",
            "the family is remembered as a state property");

    // the table reaches 1/32 and beyond
    expect (indexOfLabel ("1/32") >= 0 && indexOfLabel ("1/64") >= 0 && indexOfLabel ("1/128") >= 0, "divisions down to 1/128 exist");
    ed.reset();
    return failures;
}

static int runSelfTest()
{
    const int paintingFailures = testTreePainting() + testChordSpectrum() + testBeatFamilies();

    WaveTreeProcessor proc;
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    if (ed == nullptr) { std::printf ("FAIL no editor\n"); return 1; }
    ed->setVisible (true);
    pump (0.4);

    WiringTest test { proc, {}, 0 };
    test.failures += paintingFailures;
    auto& apvts = proc.state();

    const char* modeNames[] = { "Bands", "Cascade", "Atoms" };
    for (int m = 0; m < 3; ++m)
    {
        setP (apvts, "mode", (float) m);
        pump (0.2);
        test.run (*ed, modeNames[m]);
    }

    for (auto* id : { "morphSync", "attackSync", "decaySync", "releaseSync", "glideSync", "lfoARateSync", "lfoBRateSync" })
        setP (apvts, id, 1.0f);
    pump (0.2);
    test.run (*ed, "all time knobs in beats");

    juce::StringArray unreached;
    for (auto* p : proc.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            if (! test.reached.contains (withId->paramID))
                unreached.add (withId->paramID);

    std::printf ("\nparameters driven by a control: %d of %d\n", test.reached.size(), proc.getParameters().size());
    std::printf ("parameters with no knob/toggle/menu: %s\n",
                 unreached.isEmpty() ? "none" : unreached.joinIntoString (", ").toRawUTF8());
    std::printf ("=== selftest %s (%d failures) ===\n", test.failures == 0 ? "PASSED" : "FAILED", test.failures);

    ed.reset();
    return test.failures == 0 ? 0 : 1;
}

static bool renderState (int index, const State& st, const juce::File& outDir, juce::String& outPath)
{
    std::printf ("--- [%02d] %s\n", index, st.name);

    WaveTreeProcessor proc;

    if (st.configure) st.configure (proc);
    pump (0.15);                       // parameterChanged -> async -> view visibility props

    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    if (st.preNotes) st.preNotes (proc);

    juce::AudioBuffer<float> buf (2, 512);

    if (st.drive)
    {
        juce::MidiBuffer midi;
        for (int n : { 41, 48, 55, 60, 64, 67 })
            midi.addEvent (juce::MidiMessage::noteOn (1, n, 0.85f), 0);
        buf.clear();
        proc.processBlock (buf, midi);
        for (int i = 0; i < 60; ++i)
        {
            juce::MidiBuffer empty;
            buf.clear();
            proc.processBlock (buf, empty);
        }
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    if (ed == nullptr) { std::printf ("  FAIL no editor\n"); return false; }

    ed->setVisible (true);
    if (auto* size = std::getenv ("WAVETREE_SHOT_SIZE"))     // e.g. 1000x660 to check the minimum size
    {
        const auto wh = juce::StringArray::fromTokens (juce::String (size), "x", {});
        if (wh.size() == 2) ed->setSize (wh[0].getIntValue(), wh[1].getIntValue());
    }
    pump (0.35);                       // foleys builds the GUI from magic.xml; timers fire

    if (std::getenv ("WAVETREE_SHOT_DEBUG") != nullptr)
        dumpTree (*ed);

    if (st.drive)
    {
        for (int i = 0; i < 20; ++i)
        {
            juce::MidiBuffer empty;
            buf.clear();
            proc.processBlock (buf, empty);
        }
        pump (0.25);
    }
    else
    {
        pump (0.25);
    }

    const auto bounds = ed->getLocalBounds();
    if (bounds.isEmpty()) { std::printf ("  FAIL editor size is empty\n"); return false; }

    const auto img = ed->createComponentSnapshot (bounds, true, 2.0f);
    const auto s   = measure (img);

    const juce::String file = juce::String::formatted ("%02d_", index) + slugify (st.name) + ".png";
    const auto dest = outDir.getChildFile (file);
    dest.deleteFile();

    bool wrote = false;
    {
        juce::FileOutputStream os (dest);
        if (os.openedOk())
        {
            juce::PNGImageFormat png;
            wrote = png.writeImageToStream (img, os);
        }
    }

    outPath = dest.getFullPathName();
    const bool ok = wrote && s.stddev > 2.0 && s.distinctBuckets > 6;

    std::printf ("STATE %s %dx%d variance=%.2f buckets=%d bytes=%lld -> %s\n",
                 st.name, img.getWidth(), img.getHeight(), s.stddev, s.distinctBuckets,
                 (long long) dest.getSize(), dest.getFullPathName().toRawUTF8());
    if (! ok)
        std::printf ("FAIL %s appears uniform/blank (stddev=%.2f buckets=%d)\n",
                     st.name, s.stddev, s.distinctBuckets);

    ed.reset();                        // editor dies well before the 10 s foleys autosave
    return ok;
}

// -----------------------------------------------------------------------------
int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);     // keep the log if the run is killed
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc > 1 && juce::String (argv[1]) == "--selftest")
        return runSelfTest();

    // default output: ./shots under the current directory (pass a path to change it)
    const juce::String defaultOut = juce::File::getCurrentWorkingDirectory().getChildFile ("shots").getFullPathName();

    juce::File outDir (argc > 1 ? juce::String (juce::CharPointer_UTF8 (argv[1])) : defaultOut);
    outDir.createDirectory();
    std::printf ("outdir: %s\n", outDir.getFullPathName().toRawUTF8());

    std::vector<State> states;

    // 1 ------------------------------------------------------------- Cascade
    states.push_back ({
        "Cascade - the default hero (multifractal)",
        [] (WaveTreeProcessor& p)
        {
            auto& a = p.state();
            setP (a, "mode", 1.0f);
            setP (a, "wavelet", 2.0f);
            setP (a, "hurst", 0.62f);
            setP (a, "intermittency", 0.55f);
            setP (a, "morphOn", 1.0f);
            setP (a, "morphMs", 2600.0f);
            setP (a, "cutoffHz", 9000.0f);
            setP (a, "sustain", 0.85f);
            setP (a, "attackMs", 300.0f);
            setP (a, "releaseMs", 1400.0f);
            setP (a, "output", 0.7f);
            setP (a, "hurstModA", 0.35f);
            setP (a, "lfoARateMs", 3200.0f);
        }, nullptr, true });

    // 1b ------------------------------------------- Cascade, chord spectrum with the analytic fit
    states.push_back ({
        "Cascade - chord D(h) with the analytic fit",
        [] (WaveTreeProcessor& p)
        {
            auto& a = p.state();
            setP (a, "mode", 1.0f);
            setP (a, "wavelet", 10.0f);               // Bior4.4: symmetric
            setP (a, "hurst", 0.5f);
            setP (a, "intermittency", 0.6f);
            setP (a, "sustain", 0.9f);
            setP (a, "output", 0.7f);
            p.magicStateRef().getPropertyAsValue (DhSpectrumView::kChordProp).setValue ("1");
            p.magicStateRef().getPropertyAsValue (DhSpectrumView::kAnalyticProp).setValue ("1");
        }, nullptr, true });

    // 2 --------------------------------------------------------------- Bands
    states.push_back ({
        "Bands - drawbar formant mode",
        [] (WaveTreeProcessor& p)
        {
            auto& a = p.state();
            setP (a, "mode", 0.0f);
            setP (a, "tilt", 0.45f);
            setP (a, "formant", 0.35f);
            setP (a, "bandQ", 0.72f);
            setP (a, "wavelet", 3.0f);
            setP (a, "attackMs", 8.0f);
            setP (a, "sustain", 0.9f);
            setP (a, "releaseMs", 140.0f);
            setP (a, "cutoffHz", 12000.0f);
        }, nullptr, true });

    // 3 --------------------------------------------------------------- Atoms
    states.push_back ({
        "Atoms - sparse grains",
        [] (WaveTreeProcessor& p)
        {
            auto& a = p.state();
            setP (a, "mode", 2.0f);
            setP (a, "density", 0.12f);
            setP (a, "morphOn", 1.0f);
            setP (a, "morphSync", 1.0f);
            setP (a, "morphDiv", 8.0f);
            setP (a, "seedMode", 2.0f);
            setP (a, "densityModA", 0.5f);
            setP (a, "lfoAShape", 5.0f);
            setP (a, "lfoARateSync", 1.0f);
            setP (a, "lfoARateDiv", 5.0f);
            setP (a, "releaseMs", 600.0f);
            setP (a, "decayMs", 250.0f);
            setP (a, "sustain", 0.3f);
        }, nullptr, true });

    // 4 -------------------------------------------------------- Painted tree
    states.push_back ({
        "Painted tree - hero interaction state",
        [] (WaveTreeProcessor& p)
        {
            auto& a = p.state();
            setP (a, "mode", 1.0f);
            setP (a, "wavelet", 2.0f);
            setP (a, "hurst", 0.62f);
            setP (a, "intermittency", 0.55f);
            setP (a, "morphOn", 1.0f);
            setP (a, "morphMs", 2600.0f);
            setP (a, "cutoffHz", 9000.0f);
            setP (a, "sustain", 0.85f);
            setP (a, "attackMs", 300.0f);
            setP (a, "releaseMs", 1400.0f);
        },
        [] (WaveTreeProcessor& p)
        {
            auto& t = p.treeEdit();
            const float slope = (float) p.treeSlope();         // painted as if by hand, at the patch's slope
            t.setBandGain (1, 0.0f);
            t.setBandGain (2, 1.85f);
            t.setBandGain (5, 0.35f);
            t.setBandGain (8, 1.55f);
            for (int k = 0; k < 64; ++k)
                t.setNodeLevel (64 + k, 0.55f + 0.35f * std::sin ((float) k / 64.0f * 6.2831853f), slope);
            for (int k = 110; k <= 199; ++k)
                t.setNodeLevel (512 + k, 0.0f, slope);
            t.bump();
            p.treeEditGesture();
        }, true });

    // 5 ---------------------------------------------------- Fully modulated
    states.push_back ({
        "Fully modulated - right column lit",
        [] (WaveTreeProcessor& p)
        {
            auto& a = p.state();
            setP (a, "mode", 1.0f);
            setP (a, "hurst", 0.78f);
            setP (a, "intermittency", 0.72f);
            setP (a, "attackSync", 1.0f);   setP (a, "attackDiv", 2.0f);
            setP (a, "decaySync", 1.0f);    setP (a, "decayDiv", 8.0f);
            setP (a, "releaseSync", 1.0f);  setP (a, "releaseDiv", 11.0f);
            setP (a, "morphOn", 1.0f);
            setP (a, "morphSync", 1.0f);    setP (a, "morphDiv", 6.0f);
            setP (a, "lfoARateSync", 1.0f); setP (a, "lfoARateDiv", 5.0f);
            setP (a, "lfoBRateSync", 1.0f); setP (a, "lfoBRateDiv", 11.0f);
            setP (a, "lfoAShape", 6.0f);
            setP (a, "lfoBShape", 5.0f);
            setP (a, "lfoAPhase", 0.25f);
            setP (a, "lfoBBipolar", 0.0f);
            setP (a, "hurstModA", 0.62f);
            setP (a, "intermittencyModA", -0.4f);
            setP (a, "tiltModA", 0.35f);
            setP (a, "densityModA", 0.5f);
            setP (a, "outputModA", 0.2f);
            setP (a, "hurstModB", -0.3f);
            setP (a, "intermittencyModB", 0.55f);
            setP (a, "tiltModB", -0.45f);
            setP (a, "densityModB", 0.3f);
            setP (a, "outputModB", -0.25f);
            setP (a, "voices", 16.0f);
            setP (a, "reso", 0.75f);
            setP (a, "antiAlias", 0.0f);
        }, nullptr, true });

    // 6 ----------------------------------------------------- Cold empty state
    states.push_back ({
        "Cold empty state - no notes played",
        nullptr, nullptr, false });

    int failures = 0;
    juce::StringArray written;
    for (int i = 0; i < (int) states.size(); ++i)
    {
        juce::String path;
        const bool ok = renderState (i + 1, states[(size_t) i], outDir, path);
        if (! ok) ++failures;
        if (path.isNotEmpty()) written.add (path);
        pump (0.05);
    }

    std::printf ("\n=== %d/%d states ok ===\n", (int) states.size() - failures, (int) states.size());
    for (auto& w : written) std::printf ("%s\n", w.toRawUTF8());
    return failures == 0 ? 0 : 1;
}
