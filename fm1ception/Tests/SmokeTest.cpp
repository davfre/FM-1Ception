/*
    Console smoke test for FM-1Ception, through the real processor.

      1. Engines come and go: each slot gets its own emulator thread, the count
         clamps to 1..8, and a slot switched off gives its engine back.
      2. State round-trips: engine count, clocks, MIDI channels, main volume.
      3. With a firmware (argv[1], $FM1C_TEST_FIRMWARE, or ~/Documents/FM1/FM-1.fwsc):
         it boots, a note sounds, the firmware survives a state round trip, and
         four engines run side by side in real time.

    Run: ./FM1CeptionTests [firmware.fwsc]   (exit code 0 = pass)
         ./FM1CeptionTests --snapshot DIR [firmware.fwsc]   (the editor as PNGs)
*/

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "EngineView.h"
#include "PluginEditor.h"
#include "FirmwarePrefs.h"
#include "CloudCatalog.h"
#include "FirmwareLibrary.h"
#include "LibraryWindow.h"

using namespace fm1c;

namespace
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 512;

int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (! ok)
        ++failures;
}

struct Stats
{
    float peak = 0.0f;
    bool finite = true;
};

/** Render `seconds` at the host's pace, a C4 struck at `noteAt` (s) if >= 0; stats from `from` (s) on. */
Stats render (FM1CeptionProcessor& p, double seconds, double noteAt = -1.0, double from = 0.0)
{
    juce::AudioBuffer<float> buf (2, kBlock);
    Stats s;
    const int blocks = (int) std::ceil (seconds * kSr / kBlock);
    const int noteBlock = noteAt >= 0.0 ? (int) (noteAt * kSr / kBlock) : -1;
    const int fromBlock = (int) (from * kSr / kBlock);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();

    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == noteBlock)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 110), 0);

        p.processBlock (buf, midi);

        if (b >= fromBlock)
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kBlock; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    s.finite = s.finite && std::isfinite (v);
                    s.peak = juce::jmax (s.peak, std::abs (v));
                }

        const double due = t0 + (b + 1) * kBlock * 1000.0 / kSr;
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (due > now)
            juce::Thread::sleep ((int) (due - now));
    }
    return s;
}

/** Render `seconds` into every output channel the host gave, a C4 struck at the start; each channel's peak
    from `from` (s) on. */
std::vector<float> renderChannels (FM1CeptionProcessor& p, double seconds, double from)
{
    juce::AudioBuffer<float> buf (p.getTotalNumOutputChannels(), kBlock);
    std::vector<float> peaks ((size_t) buf.getNumChannels(), 0.0f);
    const int blocks = (int) std::ceil (seconds * kSr / kBlock), fromBlock = (int) (from * kSr / kBlock);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 110), 0);
        p.processBlock (buf, midi);
        if (b >= fromBlock)
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                peaks[(size_t) ch] = juce::jmax (peaks[(size_t) ch], buf.getMagnitude (ch, 0, kBlock));
        const double due = t0 + (b + 1) * kBlock * 1000.0 / kSr;
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (due > now)
            juce::Thread::sleep ((int) (due - now));
    }
    return peaks;
}

/** A C4 on `channel` for `seconds` (note off after 1 s), at the host's pace: each engine's loudest moment. */
std::array<float, 8> play (FM1CeptionProcessor& p, int channel, double seconds)
{
    juce::AudioBuffer<float> buf (2, kBlock);
    std::array<float, 8> peaks {};
    for (int i = 0; i < 8; ++i)
        p.takeEnginePeaks (i);

    const int blocks = (int) std::ceil (seconds * kSr / kBlock), offBlock = (int) (kSr / kBlock);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (channel > 0 && b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (channel, 60, (juce::uint8) 110), 0);
        if (channel > 0 && b == offBlock)
            midi.addEvent (juce::MidiMessage::noteOff (channel, 60), 0);
        p.processBlock (buf, midi);
        for (int i = 0; i < 8; ++i)
        {
            const auto pk = p.takeEnginePeaks (i);
            peaks[(size_t) i] = juce::jmax (peaks[(size_t) i], pk[0], pk[1]);
        }
        const double due = t0 + (b + 1) * kBlock * 1000.0 / kSr;
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (due > now)
            juce::Thread::sleep ((int) (due - now));
    }
    return peaks;
}

juce::File findFirmware (int argc, char** argv)
{
    if (argc > 1)
        return juce::File (juce::String::fromUTF8 (argv[1]));
    if (const auto env = juce::SystemStats::getEnvironmentVariable ("FM1C_TEST_FIRMWARE", {}); env.isNotEmpty())
        return juce::File (env);
    const auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("FM1");
    for (const auto* name : { "omni-0.4.fwsc", "x0x-1.1.0.fwsc", "FM-1.fwsc" })
        if (dir.getChildFile (name).existsAsFile())
            return dir.getChildFile (name);
    return dir.getChildFile ("FM-1.fwsc");
}

Fm1eStatus statusOf (FM1CeptionProcessor& p, int slot)
{
    Fm1eStatus st {};
    if (auto h = p.engine (slot))
        fm1e_view (h->get(), nullptr, nullptr, &st);
    return st;
}

/** The editor as PNGs: one engine, then four (one left blank, one with its panel buttons up)
    wide and square. The engines run on in real time with no audio taken, so the screens move. */
int snapshots (const juce::File& dir, const juce::File& fw)
{
    dir.createDirectory();
    FM1CeptionProcessor p;
    p.prepareToPlay (kSr, kBlock);

    // every engine hears every channel, so a chord lights all the meters
    p.setMidiMode (FM1CeptionProcessor::MidiMode::custom);

    auto shoot = [&] (int n, int w, int h, const juce::String& file,
                      FM1CeptionProcessor::ViewLayout layout = FM1CeptionProcessor::ViewLayout::tiled, int options = 0)
    {
        p.setNumEngines (n);
        p.setViewLayout (layout);
        for (const auto o : { FM1CeptionProcessor::screensOnly, FM1CeptionProcessor::editButtons, FM1CeptionProcessor::largeKeyboard })
            p.setViewOption (o, (options & o) != 0);
        p.setSelected (n > 1 ? 2 : 0);
        for (int i = 0; i < n; ++i)
            if (auto e = p.engine (i); e != nullptr && ! e->hasFirmware() && (n == 1 || i != 1))
                p.loadFileNow (i, fw);
        juce::Thread::sleep (3000);

        juce::AudioBuffer<float> buf (2, kBlock);
        for (int b = 0; b < 48; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)
                for (const int note : { 48, 55, 60, 64 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 120), 0);
            p.processBlock (buf, midi);
            juce::Thread::sleep (10);
        }

        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (w, h);
        int k = 0;
        for (auto* c : ed->getChildren())
            if (auto* v = dynamic_cast<fm1c::EngineView*> (c))
            {
                v->refresh();
                v->setShiftHeld (n > 1 && layout == FM1CeptionProcessor::ViewLayout::tiled && k++ == 2);
            }
        const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        juce::FileOutputStream out (dir.getChildFile (file));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (img, out);
        std::cout << "  wrote " << dir.getChildFile (file).getFullPathName() << std::endl;
    };

    shoot (1, 480, 664, "one.png");
    shoot (4, 1300, 470, "four-wide.png");
    shoot (4, 760, 900, "four-square.png");
    shoot (4, 480, 688, "four-tabbed.png", FM1CeptionProcessor::ViewLayout::tabbed);
    using V = FM1CeptionProcessor;
    shoot (4, 1980, 540, "four-screens-only.png", V::ViewLayout::tiled, V::screensOnly);
    shoot (1, 492, 882, "one-edit-large-keys.png", V::ViewLayout::tiled, V::editButtons | V::largeKeyboard);
    shoot (1, 492, 660, "one-screen-large-keys.png", V::ViewLayout::tiled, V::screensOnly | V::largeKeyboard);
    shoot (4, 1000, 728, "four-hardware.png", V::ViewLayout::hardware, V::editButtons);
    shoot (2, 1000, 728, "two-hardware-blank.png", V::ViewLayout::hardware);

    // the firmware library, with a few firmwares rated, sorted and one with a source URL
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("fm1c-library-shot");
        tmp.deleteRecursively();
        const auto folder = tmp.getChildFile ("Documents/FM-1 Firmwares");
        folder.getChildFile ("Community").createDirectory();
        fw.copyFileTo (folder.getChildFile ("Community/SHRUB.fwsc"));
        fw.copyFileTo (folder.getChildFile ("Community/SHRUB-0.3.fwsc"));
        const juce::File rb ("/Users/someone/Documents/GitHub/FM1-RB338/fm1/build/rb338.fwsc");
        if (rb.existsAsFile())
            rb.copyFileTo (folder.getChildFile ("Community/rb338.fwsc"));
        folder.getChildFile ("broken.fwsc").replaceWithText ("x");
        FirmwareLibrary lib (tmp.getChildFile ("library.json"));
        lib.setRoot (folder);
        lib.waitForScan (30000);
        lib.setRating (folder.getChildFile ("Community/SHRUB.fwsc"), 5);
        lib.setCategory (folder.getChildFile ("Community/SHRUB.fwsc"), FirmwareLibrary::Category::synth, "FM");
        lib.setUrl (folder.getChildFile ("Community/SHRUB.fwsc"), "https://github.com/xfer/FM1ShrubOS");
        lib.setRating (folder.getChildFile ("Community/rb338.fwsc"), 3);
        lib.setCategory (folder.getChildFile ("Community/rb338.fwsc"), FirmwareLibrary::Category::groovebox);
        lib.setCustomName (folder.getChildFile ("Community/rb338.fwsc"), "OpenRB-338");
        LibraryView::Engines engines;
        engines.count = [] { return 1; };
        engines.selected = [] { return 0; };
        engines.label = [] (int) { return juce::String ("Engine 1"); };
        engines.load = [] (int, const juce::File&) {};
        LibraryView view (lib, engines);
        lib.dispatchPendingMessages();
        view.setSize (2000, 300);
        view.sortBy (LibraryView::rating, false);
        const auto img = view.createComponentSnapshot (view.getLocalBounds(), true, 2.0f);
        juce::FileOutputStream out (dir.getChildFile ("library.png"));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (img, out);
        std::cout << "  wrote " << dir.getChildFile ("library.png").getFullPathName() << std::endl;
        view.setSize (640, 300);
        const auto narrow = view.createComponentSnapshot (view.getLocalBounds(), true, 2.0f);
        juce::FileOutputStream out2 (dir.getChildFile ("library-narrow.png"));
        out2.setPosition (0);
        out2.truncate();
        juce::PNGImageFormat().writeImageToStream (narrow, out2);
        tmp.deleteRecursively();
    }

    // the Cloud, from fm1-emu's list as the repository has it: one in the library, an older one
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("fm1c-cloud-shot");
        tmp.deleteRecursively();
        const auto folder = tmp.getChildFile ("Firmwares");
        folder.getChildFile ("Cloud").createDirectory();
        fw.copyFileTo (folder.getChildFile ("matryoshka-1.7.fwsc"));
        fw.copyFileTo (folder.getChildFile ("Crepuscular Homunculus 0.26.19.fwsc"));
        FirmwareLibrary lib (tmp.getChildFile ("library.json"));
        lib.setRoot (folder);
        lib.waitForScan (30000);
        const auto list = juce::File (__FILE__).getParentDirectory().getParentDirectory().getChildFile ("third_party/fm1-emu/web/firmwares.json");
        CloudCatalog cloud (lib, tmp.getChildFile ("cloud.json"), juce::URL (list), {});
        const auto felucca = folder.getChildFile ("Cloud/Felucca 1.6.1.fwsc"), salt = folder.getChildFile ("Cloud/Felucca [Salt] 0.8.fwsc");
        fw.copyFileTo (felucca);
        fw.copyFileTo (salt);
        lib.adopt (felucca, "felucca@1.6.1", "https://github.com/hugelton/Felucca");
        lib.adopt (salt, "felucca-salt@0.8", "https://github.com/ChanceTheMaker/Felucca");
        lib.dispatchPendingMessages();
        LibraryView::Engines engines;
        engines.count = [] { return 1; };
        engines.selected = [] { return 0; };
        engines.label = [] (int) { return juce::String ("Engine 1"); };
        engines.load = [] (int, const juce::File&) {};
        ScreenCache screens (tmp.getChildFile ("Screens"));
        LibraryView view (lib, engines, &cloud, &screens);
        view.setSize (1480, 520);
        view.showCloud (true);
        cloud.waitForIdle (30000);
        cloud.dispatchPendingMessages();
        // the panel on Felucca, its screens captured as the panel does once it has been shown
        if (const auto* e = cloud.find ("felucca@1.6.1"))
        {
            const auto info = view.infoOf (*e);
            screens.capture (info.shotsKey, info.package);
            screens.waitForIdle (60000);
            view.infoPanel().show (info);
        }
        const auto img = view.createComponentSnapshot (view.getLocalBounds(), true, 2.0f);
        juce::FileOutputStream out (dir.getChildFile ("library-cloud.png"));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (img, out);
        std::cout << "  wrote " << dir.getChildFile ("library-cloud.png").getFullPathName() << std::endl;

        // and the library as the lists describe it, the panel on the one named for Matryoshka
        view.showCloud (false);
        lib.dispatchPendingMessages();
        view.setSize (1800, 520);
        for (int r = 0; r < view.numRows(); ++r)
            if (view.itemAt (r).file.getFileName().startsWith ("matryoshka"))
            {
                const auto info = view.infoOf (view.itemAt (r));
                screens.capture (info.shotsKey, info.package);
                screens.waitForIdle (60000);
                view.infoPanel().show (info);
            }
        const auto described = view.createComponentSnapshot (view.getLocalBounds(), true, 2.0f);
        juce::FileOutputStream out2 (dir.getChildFile ("library-described.png"));
        out2.setPosition (0);
        out2.truncate();
        juce::PNGImageFormat().writeImageToStream (described, out2);
        std::cout << "  wrote " << dir.getChildFile ("library-described.png").getFullPathName() << std::endl;
        tmp.deleteRecursively();
    }
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FirmwareLibrary::useDatabase (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("FirmwareLibrary.json"));

    if (argc > 2 && juce::String (argv[1]) == "--snapshot")
        return snapshots (juce::File (juce::String::fromUTF8 (argv[2])), findFirmware (argc - 2, argv + 2));

    std::cout << "=== engines come and go ===" << std::endl;
    {
        FM1CeptionProcessor p;
        p.prepareToPlay (kSr, kBlock);
        check (p.getNumEngines() == 1 && p.engine (0) != nullptr && p.engine (1) == nullptr, "one engine to start");
        check (render (p, 0.2).peak == 0.0f, "silent with no firmware");
        check (p.getLatencySamples() > 0, "reports its lead as latency (" + juce::String (p.getLatencySamples()) + " samples)");

        const int before = p.layoutVersion();
        p.setNumEngines (8);
        bool all = true;
        for (int i = 0; i < 8; ++i)
            all = all && p.engine (i) != nullptr;
        check (all && p.layoutVersion() != before, "eight engines, each its own thread");

        p.setNumEngines (3);
        check (p.engine (2) != nullptr && p.engine (3) == nullptr, "back to three");
        p.setNumEngines (0);
        check (p.getNumEngines() == 1, "the count clamps at 1");
        p.setNumEngines (99);
        check (p.getNumEngines() == 8, "and at 8");
        check (render (p, 0.2).peak == 0.0f, "eight blank engines are silent");

        p.releaseResources();
        p.prepareToPlay (kSr, kBlock);
        check (render (p, 0.2).peak == 0.0f, "deactivated and activated again, as a host does");
        p.releaseResources();
    }

    std::cout << "\n=== state round trip ===" << std::endl;
    {
        FM1CeptionProcessor p;
        p.prepareToPlay (kSr, kBlock);
        p.setNumEngines (3);
        p.setClock (1, 160);
        p.setReceiveChannel (1, 3);
        p.setFirmwareChannel (2, 5);
        p.setMidiMode (FM1CeptionProcessor::MidiMode::multiChannel);
        p.setSelected (2);
        p.setViewLayout (FM1CeptionProcessor::ViewLayout::tabbed);
        p.setViewOption (FM1CeptionProcessor::screensOnly, true);
        p.setViewOption (FM1CeptionProcessor::largeKeyboard, true);
        p.mainParam().setValueNotifyingHost (p.mainParam().convertTo0to1 (0.5f));
        p.setMuted (0, true);
        p.setSoloed (2, true);
        p.setMultiOut (true);

        juce::MemoryBlock state;
        p.getStateInformation (state);

        FM1CeptionProcessor q;
        q.prepareToPlay (kSr, kBlock);
        q.setStateInformation (state.getData(), (int) state.getSize());
        check (q.getNumEngines() == 3, "engine count");
        check (q.getClock (1) == 160 && q.getClock (0) == (int) fm1e_default_mhz(), "clocks");
        check (q.getReceiveChannel (1) == 3 && q.getFirmwareChannel (2) == 5, "MIDI channels");
        check (std::abs (q.mainParam().convertFrom0to1 (q.mainParam().getValue()) - 0.5f) < 1.0e-4f, "main volume");
        check (q.getMidiMode() == FM1CeptionProcessor::MidiMode::multiChannel, "MIDI mode");
        check (q.getSelected() == 2, "the selected engine");
        check (q.getViewLayout() == FM1CeptionProcessor::ViewLayout::tabbed, "the layout");
        check (q.getViewOptions() == (FM1CeptionProcessor::screensOnly | FM1CeptionProcessor::largeKeyboard), "the view options");
        check (q.isMuted (0) && ! q.isMuted (1) && q.isSoloed (2) && ! q.isSoloed (1), "mutes and solos");
        check (q.isMultiOut(), "multi-out");

        const char junk[] = "not a state at all";
        q.setStateInformation (junk, (int) sizeof (junk));
        check (q.getNumEngines() == 3, "a foreign state is ignored");

        q.setNumEngines (2);
        check (q.getSelected() == 1, "the selection follows the engine count down");
    }

    std::cout << "\n=== mute and solo ===" << std::endl;
    {
        FM1CeptionProcessor p;
        p.setNumEngines (3);
        check (p.isAudible (0) && p.isAudible (1) && p.isAudible (2), "every engine plays to start");
        p.setMuted (1, true);
        check (p.isAudible (0) && ! p.isAudible (1), "a muted engine is silent");
        p.setSoloed (2, true);
        check (! p.isAudible (0) && ! p.isAudible (1) && p.isAudible (2), "a solo silences the rest");
        p.setSoloed (1, true);
        check (! p.isAudible (1) && p.isAudible (2), "mute wins over solo");
        p.soloExclusive (0);
        check (p.isSoloed (0) && ! p.isSoloed (1) && ! p.isSoloed (2), "an exclusive solo clears the others");
        p.soloExclusive (0);
        check (! p.isSoloed (0) && p.isAudible (0) && p.isAudible (2), "and again clears it");
        p.setSoloed (2, true);
        p.setNumEngines (2);
        check (p.isAudible (0), "a solo on a switched-off engine silences nothing");
    }

    std::cout << "\n=== the tabbed window ===" << std::endl;
    {
        FM1CeptionProcessor p;
        p.setNumEngines (4);
        p.setViewLayout (FM1CeptionProcessor::ViewLayout::tabbed);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        // the one engine's cell fills the window under the header and tabs
        auto fills = [&ed, &p]
        {
            const float cw = (float) ed->getWidth() - 2.0f * FM1CeptionEditor::kPad;
            const float ch = (float) ed->getHeight() - (float) (FM1CeptionEditor::kHeaderH + FM1CeptionEditor::kTabsH + 2 * FM1CeptionEditor::kPad);
            const bool hw = p.getViewLayout() == FM1CeptionProcessor::ViewLayout::hardware;
            return std::abs (ch - cw * fm1c::EngineView::heightFor (p.getViewOptions(), hw) / fm1c::EngineView::widthFor (hw)) <= 1.5f;
        };
        const auto size = [&ed] { return juce::String (ed->getWidth()) + " x " + juce::String (ed->getHeight()); };
        check (fills(), "four engines, tabbed, open as one engine's shape (" + size() + ")");
        ed->getConstrainer()->setBoundsForComponent (ed.get(), { 0, 0, 1000, 400 }, false, false, false, true);
        check (fills(), "dragged wider, it keeps that shape (" + size() + ")");
        ed->getConstrainer()->setBoundsForComponent (ed.get(), { 0, 0, ed->getWidth(), 500 }, false, false, true, false);
        check (fills(), "and dragged shorter (" + size() + ")");
        check (ed->getConstrainer()->getFixedAspectRatio() > 0.0, "a host resizing the window keeps its ratio");

        using V = FM1CeptionProcessor;
        check (fm1c::EngineView::heightFor (V::screensOnly) == 500.0f
                   && fm1c::EngineView::heightFor (0) == fm1c::EngineView::kH
                   && fm1c::EngineView::heightFor (V::editButtons | V::largeKeyboard) > fm1c::EngineView::kH,
               "Screens Only takes the rows away, the edit buttons and large keyboard add theirs");
        for (const int options : { (int) V::screensOnly, V::editButtons | V::largeKeyboard, V::screensOnly | V::largeKeyboard })
        {
            for (const auto o : { V::screensOnly, V::editButtons, V::largeKeyboard })
                p.setViewOption (o, (options & o) != 0);
            ed.reset (p.createEditor());
            const bool opens = fills();
            ed->getConstrainer()->setBoundsForComponent (ed.get(), { 0, 0, 900, 300 }, false, false, false, true);
            check (opens && fills(), "with view options " + juce::String (options) + " too (" + size() + ")");
        }

        check (fm1c::EngineView::widthFor (true) == 640.0f && fm1c::EngineView::heightFor (V::editButtons, true) == 430.0f,
               "Hardware is the front panel at half size under the strip, whatever the view options");
        p.setViewLayout (V::ViewLayout::hardware);
        ed.reset (p.createEditor());
        check (fills(), "Hardware opens as the front panel's shape (" + size() + ")");
        ed->getConstrainer()->setBoundsForComponent (ed.get(), { 0, 0, 1200, 300 }, false, false, false, true);
        check (fills() && ed->getWidth() == 1200, "and keeps it dragged wider (" + size() + ")");
        ed->getConstrainer()->setBoundsForComponent (ed.get(), { 0, 0, ed->getWidth(), 600 }, false, false, true, false);
        check (fills(), "and shorter (" + size() + ")");
    }

    std::cout << "\n=== firmware library ===" << std::endl;
    {
        using Category = FirmwareLibrary::Category;
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("fm1c-library-test");
        tmp.deleteRecursively();
        const auto folder = tmp.getChildFile ("Firmwares");
        const auto db = tmp.getChildFile ("library.json");
        folder.getChildFile ("Synths/Deep").createDirectory();
        const auto real = findFirmware (argc, argv);
        const bool haveFw = real.existsAsFile();
        const auto shrub = folder.getChildFile ("Synths/Deep/shrub.fwsc");
        if (haveFw)
            real.copyFileTo (shrub);
        const auto junk = folder.getChildFile ("junk.fwsc"), moved = folder.getChildFile ("Synths/renamed.fwsc");
        junk.replaceWithText ("not a firmware");
        folder.getChildFile ("readme.txt").replaceWithText ("not one either");
        folder.getChildFile (".hidden.fwsc").replaceWithText ("hidden");
        const size_t expected = haveFw ? 2 : 1;

        auto itemFor = [] (const FirmwareLibrary& lib, const juce::File& f) -> std::optional<FirmwareLibrary::Item>
        {
            for (const auto& it : lib.items())
                if (it.file == f)
                    return it;
            return std::nullopt;
        };

        {
            FirmwareLibrary lib (db);
            check (lib.numItems() == 0 && lib.getRoot() == juce::File(), "a new library is empty, with no folder");
            lib.setRoot (folder);
            check (lib.waitForScan (30000), "it scans the folder");
            check (lib.items().size() == expected,
                   "it finds each .fwsc under it, subfolders and all, and nothing else (" + juce::String ((int) lib.items().size()) + ")");
            const auto j = itemFor (lib, junk);
            check (j.has_value() && ! j->isPackage && j->name() == "junk" && j->added.toMilliseconds() > 0,
                   "a file that is no package shows by its file's name");
            if (haveFw)
            {
                const auto s = itemFor (lib, shrub);
                check (s.has_value() && s->isPackage && s->identity.startsWith ("FM-1_") && s->digest.length() == 32,
                       "a package is read: " + (s ? s->identity + ", named \"" + s->name() + "\"" : juce::String ("missing")));
            }
            lib.setRating (junk, 9);
            check (itemFor (lib, junk)->rating == FirmwareLibrary::kMaxRating && FirmwareLibrary::kMaxRating == 5,
                   "a rating is out of 5 stars");
            lib.setRating (junk, 4);

            lib.setCategory (junk, Category::synth, "FM");
            const bool fm = itemFor (lib, junk)->subcategory == "FM";
            lib.setCategory (junk, Category::groovebox, "FM");
            const bool cleared = itemFor (lib, junk)->category == Category::groovebox && itemFor (lib, junk)->subcategory.isEmpty();
            check (fm && cleared && FirmwareLibrary::subcategoriesOf (Category::synth).contains ("Multi Engine"),
                   "a Synth has a sub category (" + FirmwareLibrary::subcategoriesOf (Category::synth).joinIntoString (", ")
                       + "), one another category has not is none");
            {
                juce::StringArray all;
                bool sorted = FirmwareLibrary::subcategoriesOf (Category::none).isEmpty();
                for (int i = 0; i < FirmwareLibrary::kCategories; ++i)
                {
                    auto subs = FirmwareLibrary::subcategoriesOf ((Category) i);
                    const auto shown = subs.joinIntoString (", ");
                    subs.sortNatural();
                    sorted = sorted && ! subs.isEmpty() && subs.joinIntoString (", ") == shown;
                    all.add (FirmwareLibrary::categoryName ((Category) i) + ": " + shown);
                }
                check (sorted && FirmwareLibrary::kCategories == 5 && FirmwareLibrary::categoryFromName ("Novelty") == Category::none
                           && FirmwareLibrary::subcategoriesOf (Category::misc).contains ("Novelty / Fun"),
                       "each category has sub categories A to Z, Novelty is one of Misc: " + all.joinIntoString ("; "));
            }
            lib.setCategory (junk, Category::none);
            lib.describe ([] (const FirmwareLibrary::Item&)
            {
                FirmwareLibrary::Listing l;
                l.how = FirmwareLibrary::Listing::How::name;
                l.ref = "list:x";
                l.name = "X";
                l.category = Category::synth;
                return l;
            });
            lib.setSubcategory (junk, "Organic / Pad");
            check (itemFor (lib, junk)->category == Category::synth && itemFor (lib, junk)->subcategory == "Organic / Pad",
                   "a sub category of the Hub's kind for it makes that kind its own category");
            lib.setSubcategory (junk, "Not One");
            check (itemFor (lib, junk)->subcategory.isEmpty() && itemFor (lib, junk)->category == Category::synth,
                   "and one its category has not clears it");
            lib.setSubcategory (junk, "Inst / Sample");
            lib.flush();
            {
                FirmwareLibrary again (db);
                check (itemFor (again, junk).has_value() && itemFor (again, junk)->subcategory == "Inst / Sample",
                       "the library keeps it");
            }
            lib.describe ([] (const FirmwareLibrary::Item&) { return FirmwareLibrary::Listing(); });
            lib.setCategory (junk, Category::misc);
            lib.setUrl (junk, "https://example.com/fw");
            lib.setCustomName (junk, "My Junk");
        }
        {
            const auto text = db.loadFileAsString();
            db.replaceWithText (text.replace ("\"category\": \"Misc\"", "\"category\": \"Novelty\""));
        }
        {
            FirmwareLibrary lib (db);
            const auto j = itemFor (lib, junk);
            check (lib.getRoot() == folder && j.has_value() && j->rating == 4 && j->category == Category::misc
                       && j->subcategory == "Novelty / Fun" && j->url == "https://example.com/fw" && j->name() == "My Junk",
                   "the folder, rating, category, URL and name are kept, an old Novelty as Misc / Novelty / Fun");

            junk.moveFileTo (moved);
            lib.rescan();
            lib.waitForScan (30000);
            const auto m = itemFor (lib, moved);
            check (m.has_value() && m->rating == 4 && m->name() == "My Junk" && m->added == j->added
                       && ! itemFor (lib, junk).has_value() && lib.items().size() == expected,
                   "moved and renamed in the folder, a firmware keeps what was said of it");

            const auto away = tmp.getChildFile ("Unplugged");
            folder.moveFileTo (away);
            lib.rescan();
            lib.waitForScan (30000);
            check (lib.isRootMissing() && lib.numItems() == 0, "a folder that is not there shows nothing");
            away.moveFileTo (folder);
            lib.rescan();
            lib.waitForScan (30000);
            check (lib.items().size() == expected && itemFor (lib, moved).has_value() && itemFor (lib, moved)->rating == 4,
                   "and forgets nothing: back, it is all there");

            LibraryView::Engines engines;
            engines.count = [] { return 2; };
            engines.selected = [] { return 0; };
            engines.label = [] (int i) { return "Engine " + juce::String (i + 1); };
            engines.load = [] (int, const juce::File&) {};
            LibraryView view (lib, engines);
            view.setSize (1000, 400);
            check (view.numRows() == (int) expected, "the table shows them");
            view.setSearch ("my JUNK");
            check (view.numRows() == 1 && view.itemAt (0).file == moved, "a search finds a firmware by its name");
            view.setSearch ("nothing like it");
            check (view.numRows() == 0, "and nothing that does not match");
            view.setSearch ({});
            if (haveFw)
            {
                view.sortBy (LibraryView::fileName, true);
                const bool up = view.itemAt (0).file == moved;
                view.sortBy (LibraryView::fileName, false);
                check (up && view.itemAt (0).file == shrub, "sorted by file name, either way");
                view.sortBy (LibraryView::rating, true);
                const bool ratedFirst = view.itemAt (0).file == moved;
                view.sortBy (LibraryView::rating, false);
                check (ratedFirst && view.itemAt (0).file == moved, "by rating, the unrated last either way");
            }

            moved.deleteFile();
            check (lib.trash (moved) && ! itemFor (lib, moved).has_value(), "a firmware deleted is forgotten");
            lib.dispatchPendingMessages();
            check (view.numRows() == (int) expected - 1, "and the table hears of it");
        }
        {
            using P = std::pair<juce::String, juce::String>;
            auto split = [] (const char* s) { return FirmwareLibrary::splitVersion (s); };
            check (split ("Crepuscular Homunculus 0.26.19") == P ("Crepuscular Homunculus", "0.26.19")
                       && split ("dub-1.4.3") == P ("dub", "1.4.3") && split ("bass-something-2-0.3") == P ("bass-something-2", "0.3")
                       && split ("felucca v1.8.0") == P ("felucca", "1.8.0") && split ("SHRUB V0.4") == P ("SHRUB", "0.4")
                       && split ("FM-1 Neverhood") == P ("FM-1 Neverhood", "") && split ("FM-1+VA-102") == P ("FM-1+VA-102", "")
                       && split ("os 3f9a2c41d0e7b") == P ("os", "3f9a2c4"),
                   "a name and its version come apart: \"" + split ("bass-something-2-0.3").first + "\" \""
                       + split ("bass-something-2-0.3").second + "\"");
            auto own = [] (const char* file, const char* banner)
            {
                FirmwareLibrary::Item it;
                it.file = juce::File ("/x").getChildFile (file);
                it.parsedName = banner;
                return it.ownName();
            };
            check (own ("shrub.fwsc", "SHRUB V0.4") == P ("SHRUB", "0.4") && own ("FM-1.fwsc", "FELUCCA v1.6.1") == P ("FELUCCA", "1.6.1")
                       && own ("TransitFM1 0.1.3.fwsc", "FM-1 Stock V15") == P ("TransitFM1", "0.1.3")
                       && own ("my_great_synth-2.0.fwsc", "") == P ("my great synth", "2.0"),
                   "its name is its file's, or what it calls itself when its file says no more: \""
                       + own ("FM-1.fwsc", "FELUCCA v1.6.1").first + "\", not a fork's \"" + own ("TransitFM1 0.1.3.fwsc", "FM-1 Stock V15").first + "\"");
        }
        check (LibraryView::openableUrl ("https://a.b/c") == "https://a.b/c" && LibraryView::openableUrl ("javascript:x").isEmpty()
                   && LibraryView::openableUrl ("file:///etc/passwd").isEmpty(),
               "a source URL opens only if it is http(s)");
        const auto font = juce::Font (juce::FontOptions (13.0f));
        const juce::String path ("/Users/someone/Documents/Firmwares/Synths/Deep/Somewhere/far/down");
        const auto cut = LibraryView::middleEllipsis (path, font, 160.0f);
        check (juce::GlyphArrangement::getStringWidth (font, cut) <= 160.0f && cut.startsWith ("/Users") && cut.endsWith ("down")
                   && cut.length() < path.length(),
               "a long path is cut in the middle: " + cut);
        {
            const auto dir = tmp.getChildFile ("Sorting");
            dir.createDirectory();
            FirmwareLibrary lib (tmp.getChildFile ("sorting.json"));
            for (const auto* n : { "Zeta", "Alpha", "Mid", "Beta", "Gamma" })
                dir.getChildFile (juce::String (n) + ".fwsc").replaceWithText (n);
            lib.setRoot (dir);
            lib.waitForScan (30000);
            auto f = [&dir] (const char* n) { return dir.getChildFile (juce::String (n) + ".fwsc"); };
            lib.setCategory (f ("Zeta"), Category::synth, "FM");
            lib.setCategory (f ("Alpha"), Category::synth, "VA / Retro");
            lib.setCategory (f ("Mid"), Category::synth, "FM");
            lib.setCategory (f ("Beta"), Category::synth);
            lib.setCategory (f ("Gamma"), Category::groovebox);
            LibraryView::Engines engines;
            engines.count = [] { return 1; };
            engines.selected = [] { return 0; };
            engines.label = [] (int) { return juce::String ("Engine 1"); };
            engines.load = [] (int, const juce::File&) {};
            LibraryView view (lib, engines);
            auto order = [&view]
            {
                juce::StringArray names;
                for (int r = 0; r < view.numRows(); ++r)
                    names.add (view.itemAt (r).name());
                return names.joinIntoString (" ");
            };
            view.sortBy (LibraryView::category, true);
            const auto up = order();
            view.sortBy (LibraryView::category, false);
            const auto down = order();
            check (up == "Gamma Mid Zeta Alpha Beta" && down == "Mid Zeta Alpha Beta Gamma",
                   "by category, then its sub category (none last), then A to Z: " + up + " | " + down);
        }
        tmp.deleteRecursively();
    }

    std::cout << "\n=== the Cloud ===" << std::endl;
    {
        using Entry = CloudCatalog::Entry;
        using Status = CloudCatalog::Status;
        const auto repo = juce::File (__FILE__).getParentDirectory().getParentDirectory();
        std::vector<Entry> listed;
        juce::String error;
        const bool reads = CloudCatalog::parse (repo.getChildFile ("third_party/fm1-emu/web/firmwares.json").loadFileAsString(), listed, error);
        check (reads && listed.size() >= 50, "fm1-emu's list reads: " + juce::String ((int) listed.size()) + " firmwares " + error);
        auto byId = [&listed] (const juce::String& id) -> const Entry*
        {
            for (const auto& e : listed)
                if (e.id == id)
                    return &e;
            return nullptr;
        };
        const auto* felucca = byId ("felucca");
        const auto* stock = byId ("mvave");
        const auto* april = byId ("april");
        check (felucca != nullptr && felucca->package.startsWith ("https://") && felucca->author.isNotEmpty()
                   && felucca->hubPage() == "https://thefirmwarehub.com/#fw-felucca",
               "an entry has its package, its author and its card on the Hub");
        check (stock != nullptr && stock->bringYourOwn && stock->package.endsWith ("FM-1.fwsc"),
               "the stock firmware is M-VAVE's own download");
        check (april != nullptr && april->package.isEmpty() && april->embedded == "DEFAULT_FIRMWARE" && april->downloadable(),
               "April OS's package is the one its installer page keeps");
        std::vector<Entry> none;
        check (! CloudCatalog::parse ("<html>404</html>", none, error) && none.empty() && error.isNotEmpty(),
               "a page that is not the list is refused (" + error + ")");

        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("fm1c-cloud-test");
        tmp.deleteRecursively();
        const auto folder = tmp.getChildFile ("Firmwares"), served = tmp.getChildFile ("served");
        folder.createDirectory();
        served.createDirectory();
        const auto real = findFirmware (argc, argv);
        const bool haveFw = real.existsAsFile();
        if (haveFw)
            real.copyFileTo (served.getChildFile ("shrub.fwsc"));
        served.getChildFile ("page.fwsc").replaceWithText ("<html><body>Not Found</body></html>");
        auto urlOf = [] (const juce::File& f) { return juce::URL (f).toString (false); };
        const auto listFile = tmp.getChildFile ("list.json");
        auto writeList = [&] (const juce::String& shrubVersion)
        {
            listFile.replaceWithText (
                R"({ "firmwares": [
                    { "id": "shrub", "name": "SHRUB", "version": ")" + shrubVersion + R"(", "author": "xfer",
                      "repo": "https://github.com/xfer/FM1ShrubOS", "package": ")" + urlOf (served.getChildFile ("shrub.fwsc")) + R"(" },
                    { "id": "broken", "name": "Broken", "version": "1", "package": ")" + urlOf (served.getChildFile ("page.fwsc")) + R"(" },
                    { "id": "installer", "name": "Installer Only", "version": "1", "installer": "https://example.com/install" },
                    { "name": "no id, skipped" }
                ] })");
        };
        writeList ("0.4");

        FirmwareLibrary lib (tmp.getChildFile ("library.json"));
        lib.setRoot (folder);
        lib.waitForScan (30000);
        const auto cache = tmp.getChildFile ("cloud.json");
        CloudCatalog cloud (lib, cache, juce::URL (listFile), {});
        check (cloud.entries().empty(), "a Cloud with no list kept starts empty");
        cloud.refresh();
        check (cloud.isFetching() && cloud.waitForIdle (30000), "it fetches the list in the background");
        check (cloud.entries().size() == 3 && cloud.getFetchError().isEmpty() && cache.existsAsFile(),
               "and keeps it (" + juce::String ((int) cloud.entries().size()) + " entries) " + cloud.getFetchError());
        const auto* shrub = cloud.find ("shrub@0.4");
        const auto* broken = cloud.find ("broken@1");
        const auto* installer = cloud.find ("installer@1");
        check (shrub != nullptr && broken != nullptr && installer != nullptr && cloud.statusOf (*shrub) == Status::get
                   && cloud.statusOf (*installer) == Status::installerOnly,
               "one not in the library is to GET, one with no package installs from its page");

        auto itemFor = [&lib] (const juce::File& f) -> std::optional<FirmwareLibrary::Item>
        {
            for (const auto& it : lib.items())
                if (it.file == f)
                    return it;
            return std::nullopt;
        };
        if (haveFw && shrub != nullptr)
        {
            juce::File got;
            cloud.download (*shrub, [&got] (const juce::File& f, const juce::String&) { got = f; });
            check (cloud.statusOf (*shrub) == Status::downloading && cloud.waitForIdle (30000), "a download runs in the background");
            check (got == folder.getChildFile ("Cloud/SHRUB 0.4.fwsc") && got.existsAsFile(),
                   "and goes into Cloud/ in the library's folder: " + got.getFullPathName());
            const auto it = itemFor (got);
            check (it.has_value() && it->cloudId == "shrub@0.4" && it->url == "https://github.com/xfer/FM1ShrubOS"
                       && it->isPackage && cloud.statusOf (*shrub) == Status::inLibrary && cloud.fileOf (*shrub) == got,
                   "it joins the library at once, In Library, its project for its source URL");
            lib.rescan();
            lib.waitForScan (30000);
            lib.dispatchPendingMessages();
            const auto again = itemFor (got);
            check (again.has_value() && again->cloudId == "shrub@0.4" && cloud.statusOf (*shrub) == Status::inLibrary,
                   "and a scan keeps it as the Cloud's");
        }
        if (broken != nullptr)
        {
            juce::String why;
            cloud.download (*broken, [&why] (const juce::File&, const juce::String& e) { why = e; });
            cloud.waitForIdle (30000);
            check (why.isNotEmpty() && cloud.statusOf (*broken) == Status::failed && ! folder.getChildFile ("Cloud/Broken 1.fwsc").exists(),
                   "a page that is no package is not written anywhere (" + why + ")");
        }

        writeList ("0.5");
        cloud.refresh();
        cloud.waitForIdle (30000);
        if (haveFw)
            check (cloud.find ("shrub@0.5") != nullptr && cloud.statusOf (*cloud.find ("shrub@0.5")) == Status::update,
                   "a newer version of one in the library is an UPDATE");

        {
            CloudCatalog offline (lib, cache, juce::URL (tmp.getChildFile ("not-there.json")), {});
            check (offline.entries().size() == 3, "the list kept shows with no network");
            offline.refresh();
            offline.waitForIdle (30000);
            check (offline.getFetchError().isNotEmpty() && offline.entries().size() == 3,
                   "and a fetch that fails keeps it (" + offline.getFetchError() + ")");
        }
        {
            CloudCatalog fromWeb (lib, cache, juce::URL ("https://example.invalid/firmwares.json"), {});
            juce::String why;
            if (const auto* e = fromWeb.find ("shrub@0.5"))
                fromWeb.download (*e, [&why] (const juce::File&, const juce::String& err) { why = err; });
            fromWeb.waitForIdle (30000);
            check (why.contains ("not on the web"), "a list from the web may not point at files on this computer");
        }

        check (CloudCatalog::resolve (juce::URL ("https://thefirmwarehub.com/catalog.json"), "fw/a-1.fwsc") == "https://thefirmwarehub.com/fw/a-1.fwsc"
                   && CloudCatalog::resolve (juce::URL ("https://x.io/app/install/"), "./b.fwsc") == "https://x.io/app/install/b.fwsc"
                   && CloudCatalog::resolve (juce::URL ("https://x.io/app/index.html"), "/fw/c.fwsc") == "https://x.io/fw/c.fwsc"
                   && CloudCatalog::resolve (juce::URL ("https://x.io/app/"), "//cdn.io/d.fwsc") == "https://cdn.io/d.fwsc",
               "a page's paths resolve as a browser's do");
        const auto named = CloudCatalog::packagesNamedIn (R"(<a href="firmware/os-1.2.fwsc">get</a> fetch('../FM-1.fwsc'); x="os-1.2.fwsc" y.fwscz)");
        check (named.size() == 2 && named[0] == "firmware/os-1.2.fwsc" && named[1] == "os-1.2.fwsc",
               "an installer page's packages are found, M-VAVE's own FM-1.fwsc not among them: " + named.joinIntoString (", "));
        check (CloudCatalog::embeddedIn ("var A = 1; const DEFAULT_FIRMWARE = 'QUJD+/==';", "DEFAULT_FIRMWARE") == "QUJD+/=="
                   && CloudCatalog::embeddedIn ("DEFAULT_FIRMWARE(x)", "DEFAULT_FIRMWARE").isEmpty(),
               "and a package a page keeps inside it");

        // the Hub: fm1-emu's list names a package the Hub has replaced since
        if (haveFw)
        {
            const auto hubDir = tmp.getChildFile ("hub");
            hubDir.getChildFile ("fw").createDirectory();
            real.copyFileTo (hubDir.getChildFile ("fw/shrub-0.6.fwsc"));
            real.copyFileTo (hubDir.getChildFile ("fw/shrub-0.7.fwsc"));
            real.copyFileTo (hubDir.getChildFile ("fw/bad.fwsc"));
            const auto digest = FirmwareLibrary::probe (real).digest;
            const auto hubFile = hubDir.getChildFile ("catalog.json");
            auto writeHub = [&] (const juce::String& shrubPkg, const juce::String& shrubVersion)
            {
                hubFile.replaceWithText (R"({ "firmwares": [
                    { "id": "shrubhub", "version": ")" + shrubVersion + R"(", "pkg": ")" + shrubPkg + R"(", "sha256": ")" + digest + juce::String::repeatedString ("0", 32) + R"(" },
                    { "id": "badhub", "version": "2", "pkg": "fw/bad.fwsc", "sha256": ")" + juce::String::repeatedString ("ab", 32) + R"(" },
                    { "id": "nopkg", "name": "No Package Hub", "author": "someone", "what": "Drums, only on the Hub", "kind": ["drums", "synth"],
                      "repo": "o/n", "version": "1", "pkg": null } ] })");
            };
            writeHub ("fw/shrub-0.6.fwsc", "0.6");
            const auto installerPage = served.getChildFile ("install/index.html");
            installerPage.getParentDirectory().getChildFile ("firmware").createDirectory();
            real.copyFileTo (served.getChildFile ("install/firmware/os-3.fwsc"));
            installerPage.replaceWithText (R"(<html><script src="app.js"></script><a href="firmware/os-3.fwsc">Install</a></html>)");
            juce::MemoryBlock fwBytes;
            real.loadFileAsData (fwBytes);
            const auto keeps = served.getChildFile ("keeps.html");
            keeps.replaceWithText ("<script>const DEFAULT_FIRMWARE = \"" + juce::Base64::toBase64 (fwBytes.getData(), fwBytes.getSize()) + "\";</script>");
            const auto hubList = tmp.getChildFile ("hub-list.json");
            hubList.replaceWithText (
                R"({ "firmwares": [
                    { "id": "stale", "name": "Stale", "version": "0.5", "hub": "shrubhub", "package": ")" + urlOf (hubDir.getChildFile ("fw/shrub-0.5.fwsc")) + R"(" },
                    { "id": "bad", "name": "Bad", "version": "1", "hub": "badhub", "package": ")" + urlOf (hubDir.getChildFile ("fw/gone.fwsc")) + R"(" },
                    { "id": "linked", "name": "Linked", "version": "3", "installer": ")" + urlOf (installerPage) + R"(", "package": ")" + urlOf (served.getChildFile ("gone.fwsc")) + R"(" },
                    { "id": "kept", "name": "Kept", "version": "1", "installer": ")" + urlOf (keeps) + R"(", "embedded": "DEFAULT_FIRMWARE" } ] })");
            FirmwareLibrary hubLib (tmp.getChildFile ("hub-library.json"));
            tmp.getChildFile ("HubFirmwares").createDirectory();
            hubLib.setRoot (tmp.getChildFile ("HubFirmwares"));
            hubLib.waitForScan (30000);
            CloudCatalog withHub (hubLib, tmp.getChildFile ("hub-cloud.json"), juce::URL (hubList), juce::URL (hubFile));
            withHub.refresh();
            withHub.waitForIdle (30000);
            const auto* stale = withHub.find ("stale@0.6");
            check (stale != nullptr && stale->package.endsWith ("fw/shrub-0.6.fwsc") && withHub.getHubError().isEmpty(),
                   "an entry the Hub lists takes the Hub's version and package now");
            juce::File got;
            juce::String why;
            auto get = [&] (const juce::String& key)
            {
                got = juce::File();
                why = {};
                if (const auto* e = withHub.find (key))
                    withHub.download (*e, [&] (const juce::File& f, const juce::String& err) { got = f; why = err; });
                withHub.waitForIdle (30000);
            };
            get ("stale@0.6");
            check (got.existsAsFile() && got.getFileName() == "Stale 0.6.fwsc" && withHub.statusOf (*withHub.find ("stale@0.6")) == CloudCatalog::Status::inLibrary,
                   "and downloads it, its checksum the Hub's " + why);

            // replaced on the Hub after the catalog was read: the download asks the Hub again
            writeHub ("fw/shrub-0.7.fwsc", "0.7");
            hubDir.getChildFile ("fw/shrub-0.6.fwsc").deleteFile();
            hubLib.trash (got);
            withHub.dispatchPendingMessages();
            get ("stale@0.6");
            check (got.getFileName() == "Stale 0.7.fwsc" && withHub.find ("stale@0.7") != nullptr
                       && withHub.statusOf (*withHub.find ("stale@0.7")) == CloudCatalog::Status::inLibrary,
                   "a package gone from the Hub is the Hub's newer one: " + got.getFileName() + " " + why);
            get ("bad@2");
            check (! got.exists() && why.contains ("checksum"), "one that is not what the Hub's checksum says is refused (" + why + ")");
            get ("linked@3");
            check (got.existsAsFile(), "a package gone is the one its installer page links " + why);
            get ("kept@1");
            check (got.existsAsFile() && FirmwareLibrary::probe (got).isPackage, "and one its installer page keeps inside it " + why);

            // what the lists say of what is in the library
            using How = FirmwareLibrary::Listing::How;
            const auto hubRoot = hubLib.getRoot();
            real.copyFileTo (hubRoot.getChildFile ("FM-1 my copy.fwsc"));
            hubRoot.getChildFile ("Linked_v2.fwsc").replaceWithText ("not one");
            hubRoot.getChildFile ("no-package-hub-1.2.fwsc").replaceWithText ("nor this");
            hubRoot.getChildFile ("Unheard Of 1.0.fwsc").replaceWithText ("nor this");
            hubLib.rescan();
            hubLib.waitForScan (30000);
            hubLib.dispatchPendingMessages();
            auto listed = [&hubLib] (const juce::String& fileName)
            {
                for (const auto& it : hubLib.items())
                    if (it.file.getFileName() == fileName)
                        return it;
                return FirmwareLibrary::Item();
            };
            const auto copy = listed ("FM-1 my copy.fwsc");
            check (copy.listing.how == How::checksum && copy.name() == "Stale" && copy.version() == "0.7" && copy.listing.ref == "list:stale",
                   "a package the Hub has byte for byte is the Hub's, whatever its file is named: " + copy.name() + " " + copy.version());
            const auto fetched = listed ("Kept 1.fwsc");
            check (fetched.listing.how == How::download && fetched.name() == "Kept" && fetched.version() == "1",
                   "one downloaded is the entry it was downloaded as");
            const auto byName = listed ("Linked_v2.fwsc");
            check (byName.listing.how == How::name && byName.name() == "Linked" && byName.version() == "2",
                   "one named for a firmware is that firmware, its version its file's: " + byName.name() + " " + byName.version());
            const auto hubOnly = listed ("no-package-hub-1.2.fwsc");
            check (hubOnly.name() == "No Package Hub" && hubOnly.listing.author == "someone" && hubOnly.listing.about == "Drums, only on the Hub"
                       && hubOnly.shownCategory() == FirmwareLibrary::Category::groovebox && hubOnly.category == FirmwareLibrary::Category::none
                       && hubOnly.url == "https://github.com/o/n" && hubOnly.listing.ref == "hub:nopkg",
                   "the Hub describes its own too: its name, author, what it is, its kind as a category, its project");
            const auto unheard = listed ("Unheard Of 1.0.fwsc");
            check (unheard.listing.how == How::none && unheard.name() == "Unheard Of" && unheard.version() == "1.0",
                   "one no list knows is named for its file, its version apart");
            hubLib.flush();
            {
                FirmwareLibrary reopened (tmp.getChildFile ("hub-library.json"));
                const auto kept = [&reopened]
                {
                    for (const auto& it : reopened.items())
                        if (it.file.getFileName() == "FM-1 my copy.fwsc")
                            return it;
                    return FirmwareLibrary::Item();
                }();
                check (kept.listing == copy.listing, "and the library keeps what they say");
            }

            CloudCatalog again (hubLib, tmp.getChildFile ("hub-cloud.json"), juce::URL (hubList), juce::URL (hubFile));
            check (again.find ("stale@0.7") != nullptr, "the Hub's packages are kept with the list");
            check (again.describe (hubOnly).name == "No Package Hub" && again.describe (copy).how == How::checksum,
                   "and what the Hub says of them");
            withHub.waitForIdle (30000);
        }

        LibraryView::Engines engines;
        engines.count = [] { return 2; };
        engines.selected = [] { return 0; };
        engines.label = [] (int i) { return "Engine " + juce::String (i + 1); };
        engines.load = [] (int, const juce::File&) {};
        {
            CloudView view (cloud, lib, engines);
            view.setSize (1000, 400);
            check (view.numRows() == 3, "the Cloud's table lists them");
            view.setSearch ("XFER");
            check (view.numRows() == 1 && view.entryAt (0).id == "shrub", "a search finds a firmware by its author");
            view.setSearch ({});
            view.sortBy (CloudView::status, true);
            check (! haveFw || view.entryAt (0).id == "shrub", "sorted by status, an UPDATE first");
        }
        {
            LibraryView view (lib, engines, &cloud);
            view.setSize (1000, 400);
            view.showCloud (true);
            check (view.isCloudShown() && lib.isCloudShown() && view.cloudView() != nullptr, "LOCAL | CLOUD shows the Cloud");
            view.showCloud (false);
            check (! view.isCloudShown() && ! lib.isCloudShown(), "and back");
            LibraryView none (lib, engines);
            none.showCloud (true);
            check (! none.isCloudShown(), "a library with no Cloud has none to show");
        }
        cloud.waitForIdle (30000);

        if (juce::SystemStats::getEnvironmentVariable ("FM1C_TEST_NETWORK", {}).isNotEmpty())
        {
            FirmwareLibrary netLib (tmp.getChildFile ("net-library.json"));
            CloudCatalog net (netLib, tmp.getChildFile ("net-cloud.json"));
            net.refresh();
            net.waitForIdle (60000);
            check (net.entries().size() >= 50 && net.getHubError().isEmpty(),
                   "fm1-emu's list from GitHub: " + juce::String ((int) net.entries().size()) + " firmwares " + net.getFetchError()
                       + net.getHubError());
            // every package the list (with the Hub's) names is there: a HEAD-sized look at each
            tmp.getChildFile ("NetFirmwares").createDirectory();
            netLib.setRoot (tmp.getChildFile ("NetFirmwares"));
            netLib.waitForScan (30000);
            juce::StringArray failed;
            int got = 0;
            for (const auto& e : net.entries())
            {
                if (! e.downloadable())
                    continue;
                net.download (e, [&failed, &got, title = e.title()] (const juce::File& f, const juce::String& why)
                {
                    if (f.existsAsFile())
                        ++got;
                    else
                        failed.add (title + ": " + why);
                });
            }
            net.waitForIdle (600000);
            check (failed.isEmpty(), "every firmware downloads: " + juce::String (got) + " did"
                                         + (failed.isEmpty() ? juce::String() : ", not\n      " + failed.joinIntoString ("\n      ")));
        }
        tmp.deleteRecursively();
    }

    std::cout << "\n=== screens ===" << std::endl;
    {
        const juce::Time when (2026, 9, 31, 18, 5);
        check (LibraryView::formatDay (when) == "2026-10-31" && LibraryView::formatDate (when).contains ("6:05 PM"),
               "the table's dates are days (" + LibraryView::formatDay (when) + "), the tooltips' keep the time");
        const auto real = findFirmware (argc, argv);
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("fm1c-screens-test");
        tmp.deleteRecursively();
        tmp.createDirectory();
        if (real.existsAsFile())
        {
            juce::MemoryBlock pkg;
            real.loadFileAsData (pkg);
            juce::Image early, late;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const auto why = ScreenCache::captureNow (pkg, early, late);
            const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
            auto lit = [] (const juce::Image& img)
            {
                int n = 0;
                for (int y = 0; y < img.getHeight(); y += 4)
                    for (int x = 0; x < img.getWidth(); x += 4)
                        n += img.getPixelAt (x, y).getBrightness() > 0.1f ? 1 : 0;
                return n;
            };
            check (why.isEmpty() && early.getWidth() == FM1E_LCD_W && late.getHeight() == FM1E_LCD_H && lit (late) > 20,
                   "a package's screens are captured with no engine, in " + juce::String (ms, 0) + " ms " + why);

            const auto digest = FirmwareLibrary::probe (real).digest;
            const auto junk = tmp.getChildFile ("junk.fwsc");
            junk.replaceWithText ("not a package");
            {
                ScreenCache cache (tmp.getChildFile ("Screens"));
                check (! cache.shotsOf (digest).has(), "none are kept to begin with");
                cache.capture (digest, real);
                check (cache.shotsOf (digest).capturing, "they are captured in the background");
                check (cache.waitForIdle (60000) && cache.shotsOf (digest).has() && cache.shotsOf (digest).late.isValid(),
                       "and come: " + cache.shotsOf (digest).error);
                cache.capture ("junk", junk);
                cache.waitForIdle (60000);
                check (! cache.shotsOf ("junk").has() && cache.shotsOf ("junk").error.isNotEmpty()
                           && tmp.getChildFile ("Screens/junk.failed").existsAsFile(),
                       "one that is no package has none, and why (" + cache.shotsOf ("junk").error + ")");

                const auto cardFile = tmp.getChildFile ("card.png");
                {
                    juce::FileOutputStream out (cardFile);
                    juce::PNGImageFormat().writeImageToStream (late, out);
                }
                const auto cardUrl = juce::URL (cardFile).toString (false);
                check (! cache.cardOf (cardUrl).isValid(), "a card is fetched in the background");
                cache.waitForIdle (30000);
                check (cache.cardOf (cardUrl).isValid() && tmp.getChildFile ("Screens/Cards/card.png").existsAsFile(),
                       "and kept");
            }
            {
                ScreenCache again (tmp.getChildFile ("Screens"));
                check (again.shotsOf (digest).has() && again.shotsOf ("junk").error.isNotEmpty(),
                       "what was captured, and what could not be, is kept for next time");
                again.capture ("fetched", [&pkg] (juce::MemoryBlock& out) { out = pkg; return juce::String(); });
                again.waitForIdle (60000);
                check (again.shotsOf ("fetched").has(), "a package fetched (one of the Cloud's) is captured too");
            }

            // the panel, beside the table
            FirmwareLibrary lib (tmp.getChildFile ("library.json"));
            const auto folder = tmp.getChildFile ("Firmwares");
            folder.createDirectory();
            real.copyFileTo (folder.getChildFile ("SHRUB.fwsc"));
            lib.setRoot (folder);
            lib.waitForScan (30000);
            ScreenCache cache (tmp.getChildFile ("Screens"));
            LibraryView::Engines engines;
            engines.count = [] { return 1; };
            engines.selected = [] { return 0; };
            engines.label = [] (int) { return juce::String ("Engine 1"); };
            engines.load = [] (int, const juce::File&) {};
            LibraryView view (lib, engines, nullptr, &cache);
            view.setSize (1200, 600);
            check (view.isInfoShown() && view.infoPanel().getWidth() == InfoPanel::kWidth, "the info panel shows at the right");
            const auto info = view.infoOf (view.itemAt (0));
            check (info.title == "SHRUB" && info.version == "0.4" && info.shotsKey == digest && info.package.existsAsFile()
                       && std::any_of (info.facts.begin(), info.facts.end(), [] (const auto& f) { return f.first == "Added" && f.second.length() == 10; }),
                   "with what the firmware is, its dates days, its screens by its digest");
            view.setSize (700, 600);
            check (! view.isInfoShown(), "a window too narrow for it has none");
            view.setSize (1200, 600);
            view.showInfo (false);
            check (! view.isInfoShown() && ! lib.isInfoShown(), "INFO hides it, and the library keeps that");
            view.showInfo (true);
        }
        tmp.deleteRecursively();
    }

    std::cout << "\n=== firmware preferences ===" << std::endl;
    {
        const juce::String name ("FM-1Ception smoke test");
        const int rev = prefs::revision().load();
        prefs::setSoftKnobRow (name, 132);
        check (prefs::softKnobRow (name) == 132 && prefs::fileFor (name).existsAsFile(), "a soft knob row is kept per firmware");
        check (prefs::revision().load() != rev, "and the views hear of it");
        prefs::setSoftKnobRow (name, -1);
        check (prefs::softKnobRow (name) == -1 && ! prefs::fileFor (name).existsAsFile(), "removing it leaves no file");
        check (prefs::softKnobRow ("some firmware never seen") == -1, "a firmware with none set has none");
    }

    const auto fw = findFirmware (argc, argv);
    std::cout << "\n=== firmware: " << fw.getFullPathName() << " ===" << std::endl;
    if (! fw.existsAsFile())
    {
        std::cout << "  (no firmware: skipped)" << std::endl;
    }
    else
    {
        std::cout << "  default clock " << fm1e_default_mhz() << " MHz, JIT "
                  << (fm1e_jit_available() ? "on" : "off") << std::endl;

        juce::MemoryBlock state;
        juce::String name;
        bool keepsUp = false;
        {
            FM1CeptionProcessor p;
            p.prepareToPlay (kSr, kBlock);
            const auto r = p.loadFileNow (0, fw);
            check (r.wasOk(), "loads (" + (r.wasOk() ? juce::String ("ok") : r.getErrorMessage()) + ")");
            name = p.engine (0)->firmwareName();
            check (name.isNotEmpty(), "named " + name);

            const auto s = render (p, 5.0, 3.0, 3.0);
            const auto st = statusOf (p, 0);
            check (st.booted && ! st.halted, "boots and keeps running");
            check (s.finite, "no NaN or infinity");
            check (s.peak > 1.0e-3f, "a note sounds (peak " + juce::String (s.peak, 4) + ")");
            std::cout << "  could run " << st.speed << "x real time, " << st.late << " frames late" << std::endl;
            keepsUp = st.speed >= 1.2f && st.late == 0;
            if (! keepsUp)
                std::cout << "  (this firmware is slower than real time on its own: real-time checks skipped)" << std::endl;

            p.getStateInformation (state);

            p.setMuted (0, true);
            check (render (p, 1.5, 0.1, 0.2).peak == 0.0f, "muted, it is silent");
        }
        {
            FM1CeptionProcessor q;
            q.prepareToPlay (kSr, kBlock);
            q.setStateInformation (state.getData(), (int) state.getSize());
            auto h = q.engine (0);
            check (h != nullptr && h->hasFirmware() && h->firmwareName() == name, "the firmware comes back with the project");
        }
        {
            // edits that live only in the machine's RAM: another preset, a knob turned
            FM1CeptionProcessor p;
            p.prepareToPlay (kSr, kBlock);
            p.loadFileNow (0, fw);
            render (p, 6.0);
            auto* e = p.engine (0)->get();
            std::vector<juce::uint16> booted (FM1E_LCD_W * FM1E_LCD_H);
            fm1e_view (e, booted.data(), nullptr, nullptr);
            fm1e_enc (e, 6, 2);
            fm1e_enc (e, 2, -9);
            render (p, 1.0);
            std::vector<juce::uint16> edited (booted.size()), back (booted.size()), plain (booted.size());
            fm1e_view (e, edited.data(), nullptr, nullptr);
            check (edited != booted && p.engine (0)->flash().getSize() == 0, "a preset and a knob changed, in RAM only");

            juce::MemoryBlock project;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            p.getStateInformation (project);
            std::cout << "  the project: " << project.getSize() / 1024 << " KB, saved in "
                      << juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 1) << " ms" << std::endl;

            FM1CeptionProcessor q;
            q.prepareToPlay (kSr, kBlock);
            q.setStateInformation (project.getData(), (int) project.getSize());
            int result = 1;
            char why[256] {};
            for (int t = 0; t < 100 && result == 1; ++t)
            {
                juce::Thread::sleep (20);
                result = fm1e_restore_result (q.engine (0)->get(), why, sizeof (why));
            }
            check (result == 0, "the machine comes back with the project" + (result < 0 ? ": " + juce::String::fromUTF8 (why) : juce::String()));
            // the two machines run on unsynchronised, so let the touched knob's highlight time out on
            // both, and leave out what moves by itself on the saved one (SHRUB's wave scopes, its
            // modulation dots), a few pixels around
            render (p, 3.0);
            render (q, 3.0);
            std::vector<bool> moving (edited.size());
            {
                std::vector<juce::uint16> first (edited.size()), now (edited.size());
                fm1e_view (e, first.data(), nullptr, nullptr);
                for (int t = 0; t < 20; ++t)
                {
                    render (p, 0.1);
                    fm1e_view (e, now.data(), nullptr, nullptr);
                    for (int y = 0; y < FM1E_LCD_H; ++y)
                        for (int x = 0; x < FM1E_LCD_W; ++x)
                            if (now[(size_t) (y * FM1E_LCD_W + x)] != first[(size_t) (y * FM1E_LCD_W + x)])
                                for (int dy = -3; dy <= 3; ++dy)
                                    for (int dx = -3; dx <= 3; ++dx)
                                        if (juce::isPositiveAndBelow (x + dx, FM1E_LCD_W) && juce::isPositiveAndBelow (y + dy, FM1E_LCD_H))
                                            moving[(size_t) ((y + dy) * FM1E_LCD_W + x + dx)] = true;
                }
            }
            auto still = [&moving] (const Fm1e* m, std::vector<juce::uint16>& px)
            {
                fm1e_view (m, px.data(), nullptr, nullptr);
                for (size_t i = 0; i < px.size(); ++i)
                    if (moving[i])
                        px[i] = 0;
            };
            still (e, edited);
            still (q.engine (0)->get(), back);
            check (back == edited, "where it was: the preset and the knob as edited, on the screen they were on");

            FM1CeptionProcessor r;
            r.prepareToPlay (kSr, kBlock);
            r.loadFileNow (0, fw);
            render (r, 10.0);
            still (r.engine (0)->get(), plain);
            check (plain != edited, "(which a boot from the flash alone is not)");

            // a preset is the same engine on its own, in a file
            const juce::TemporaryFile tmp (FM1CeptionProcessor::kPresetSuffix);
            const auto saved = p.savePreset (0, tmp.getFile());
            check (saved.wasOk(), "saved as a preset " + saved.getErrorMessage());
            FM1CeptionProcessor u;
            u.prepareToPlay (kSr, kBlock);
            const auto loaded = u.loadPreset (0, tmp.getFile());
            int presetResult = 1;
            for (int t = 0; t < 100 && presetResult == 1; ++t)
            {
                juce::Thread::sleep (20);
                presetResult = fm1e_restore_result (u.engine (0)->get(), why, sizeof (why));
            }
            check (loaded.wasOk() && presetResult == 0, "the preset brings the machine back into another engine");
            render (u, 3.0);
            still (e, edited);
            std::vector<juce::uint16> fromPreset (edited.size());
            still (u.engine (0)->get(), fromPreset);
            check (fromPreset == edited, "where it was, as with the project");
            const juce::TemporaryFile junk (FM1CeptionProcessor::kPresetSuffix);
            junk.getFile().replaceWithText ("not a preset");
            check (u.loadPreset (0, junk.getFile()).failed() && u.engine (0)->hasFirmware(),
                   "a file that is not a preset is refused, and the engine plays on");
        }
        {
            FM1CeptionProcessor p;
            p.prepareToPlay (kSr, kBlock);
            p.setNumEngines (4);
            bool ok = true;
            for (int i = 0; i < 4; ++i)
                ok = ok && p.loadFileNow (i, fw).wasOk();
            check (ok, "four engines load");

            const auto s = render (p, 5.0, 3.0, 3.0);
            check (s.finite && s.peak > 1.0e-3f, "four engines sound together (peak " + juce::String (s.peak, 4) + ")");
            juce::uint64 late = 0;
            for (int i = 0; i < 4; ++i)
            {
                const auto st = statusOf (p, i);
                late += st.late;
                std::cout << "  engine " << (i + 1) << ": " << st.speed << "x real time, " << st.late << " late" << std::endl;
            }
            if (keepsUp)
                check (late == 0, "none of them falls behind");
        }
        {
            FM1CeptionProcessor p;
            p.prepareToPlay (kSr, kBlock);
            p.setNumEngines (3);
            p.loadFileNow (0, fw);
            p.setClock (0, 160);
            p.masterParam (0).setValueNotifyingHost (p.masterParam (0).convertTo0to1 (0.5f));
            render (p, 1.0);
            p.duplicateToAll (0);
            for (int t = 0; t < 100 && ! (p.engine (2)->hasFirmware() && p.engine (1)->hasFirmware()); ++t)
                juce::Thread::sleep (100);
            juce::Thread::sleep (500);
            bool same = true;
            for (int i = 1; i < 3; ++i)
                same = same && p.engine (i)->firmwareName() == name && p.getClock (i) == 160
                       && std::abs (p.masterParam (i).convertFrom0to1 (p.masterParam (i).getValue()) - 0.5f) < 1.0e-4f;
            check (same, "duplicated to all: the firmware, clock and MASTER");
            render (p, 3.0);
            check (statusOf (p, 1).booted && ! statusOf (p, 2).halted, "and the copies run");
        }
        {
            FM1CeptionProcessor p;
            p.enableAllBuses();
            p.prepareToPlay (kSr, kBlock);
            p.setMidiMode (FM1CeptionProcessor::MidiMode::custom);
            p.setNumEngines (2);
            p.loadFileNow (0, fw);
            p.loadFileNow (1, fw);
            render (p, 3.0);
            check (p.getTotalNumOutputChannels() == 16, "eight stereo output pairs");

            auto pairPeak = [] (const std::vector<float>& pk, int pair)
            {
                return juce::jmax (pk[(size_t) pair * 2], pk[(size_t) pair * 2 + 1]);
            };
            auto pk = renderChannels (p, 2.5, 0.0);
            check (pairPeak (pk, 0) > 1.0e-3f && pairPeak (pk, 1) == 0.0f, "Stereo Mix: both engines in the main pair");

            p.setMultiOut (true);
            render (p, 2.0);
            pk = renderChannels (p, 2.5, 0.0);
            check (pairPeak (pk, 0) > 1.0e-3f && pairPeak (pk, 1) > 1.0e-3f && pairPeak (pk, 2) == 0.0f,
                   "Multi-Out: engine 2 in pair 2 (" + juce::String (pairPeak (pk, 1), 4) + ")");

            p.setMuted (0, true);
            render (p, 2.0);
            pk = renderChannels (p, 2.5, 0.1);
            check (pairPeak (pk, 0) == 0.0f && pairPeak (pk, 1) > 1.0e-3f, "muting engine 1 leaves engine 2");
            p.setMuted (0, false);
            p.setSoloed (0, true);
            render (p, 2.0);
            pk = renderChannels (p, 2.5, 0.1);
            check (pairPeak (pk, 0) > 1.0e-3f && pairPeak (pk, 1) == 0.0f, "soloing engine 1 silences engine 2");
        }
        {
            using Mode = FM1CeptionProcessor::MidiMode;
            FM1CeptionProcessor p;
            p.prepareToPlay (kSr, kBlock);
            p.setNumEngines (2);
            p.loadFileNow (0, fw);
            p.loadFileNow (1, fw);
            const auto idle = play (p, 0, 3.0);
            if (idle[0] > 1.0e-4f || idle[1] > 1.0e-4f)
            {
                std::cout << "  (this firmware makes sound with no note: routing checks skipped)" << std::endl;
            }
            else
            {
                auto heard = [] (const std::array<float, 8>& pk, int slot)
                {
                    return juce::String ("engine ") + juce::String (slot + 1) + " " + juce::String (pk[(size_t) slot], 4);
                };
                p.setMidiMode (Mode::selected);
                p.setSelected (1);
                auto pk = play (p, 7, 2.5);
                check (pk[1] > 1.0e-3f && pk[0] < 1.0e-4f,
                       "Selected: Ch 7 plays only the selected engine (" + heard (pk, 0) + ", " + heard (pk, 1) + ")");

                p.setMidiMode (Mode::multiChannel);
                play (p, 0, 2.0);
                pk = play (p, 1, 2.5);
                check (pk[0] > 1.0e-3f && pk[1] < 1.0e-4f, "MultiChannel: Ch 1 plays engine 1 (" + heard (pk, 1) + ")");
                play (p, 0, 2.0);
                pk = play (p, 2, 2.5);
                check (pk[1] > 1.0e-3f && pk[0] < 1.0e-4f, "MultiChannel: Ch 2 plays engine 2 (" + heard (pk, 0) + ")");
                play (p, 0, 2.0);
                pk = play (p, 3, 2.5);
                check (pk[0] < 1.0e-4f && pk[1] < 1.0e-4f, "MultiChannel: Ch 3 plays neither");
            }
        }
    }

    std::cout << std::endl
              << (failures == 0 ? "ALL CHECKS PASSED" : juce::String (failures) + " FAILURES").toStdString()
              << std::endl;
    return failures == 0 ? 0 : 1;
}
