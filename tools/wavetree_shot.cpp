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

    // 2. a drag stays in the row it started in: going past the top is the top
    const int b2 = silent[1];
    const auto above = view->nodeArea (band, b2).getY() - 60.0f;
    drag (pointAt (b2, 0.4f), { view->nodeArea (band, b2).getCentreX(), above });
    expect (tree.getNodeLevel (index (b2)) > 0.99f, "dragging above the row paints the top of that row");
    bool neighboursUntouched = true;
    for (int nb = band + 1; nb <= band + 3 && nb < CoeffTreeView::kBands; ++nb)
        for (int k = 0; k < CoeffTreeView::bandSize (nb); ++k)
            if (tree.getNodeLevel (CoeffTreeView::bandOff (nb) + k) >= 0.0f
                || std::abs (tree.getNodeGain (CoeffTreeView::bandOff (nb) + k) - 1.0f) > 1.0e-6f)
                neighboursUntouched = false;
    expect (neighboursUntouched, "and leaves the rows above alone");

    // 3. past the bottom mutes
    const int c = silent[2];
    const auto below = view->nodeArea (band, c).getBottom() + 60.0f;
    drag (pointAt (c, 0.5f), { view->nodeArea (band, c).getCentreX(), below });
    expect (tree.getNodeLevel (index (c)) >= 0.0f && tree.getNodeLevel (index (c)) < 0.01f,
            "dragging below the row paints the bottom: muted");

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

static int runSelfTest()
{
    const int paintingFailures = testTreePainting();

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

    for (auto* id : { "morphSync", "attackSync", "decaySync", "releaseSync", "lfoARateSync", "lfoBRateSync" })
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

    const juce::String defaultOut =
        "/private/tmp/claude-501/-Users-amasaryk-projects/"
        "fc9277dc-03c0-456c-8d90-535220843ea7/scratchpad/shots/WaveTree";

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
            t.setBandGain (1, 0.0f);
            t.setBandGain (2, 1.85f);
            t.setBandGain (5, 0.35f);
            t.setBandGain (8, 1.55f);
            for (int k = 0; k < 64; ++k)
                t.setNodeLevel (64 + k, 0.55f + 0.35f * std::sin ((float) k / 64.0f * 6.2831853f));
            for (int k = 110; k <= 199; ++k)
                t.setNodeLevel (512 + k, 0.0f);
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
