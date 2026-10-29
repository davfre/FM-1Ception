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
                      FM1CeptionProcessor::ViewLayout layout = FM1CeptionProcessor::ViewLayout::tiled)
    {
        p.setNumEngines (n);
        p.setViewLayout (layout);
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
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

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
        auto fills = [&ed]
        {
            const float cw = (float) ed->getWidth() - 2.0f * FM1CeptionEditor::kPad;
            const float ch = (float) ed->getHeight() - (float) (FM1CeptionEditor::kHeaderH + FM1CeptionEditor::kTabsH + 2 * FM1CeptionEditor::kPad);
            return std::abs (ch - cw * fm1c::EngineView::kH / fm1c::EngineView::kW) <= 1.5f;
        };
        const auto size = [&ed] { return juce::String (ed->getWidth()) + " x " + juce::String (ed->getHeight()); };
        check (fills(), "four engines, tabbed, open as one engine's shape (" + size() + ")");
        ed->getConstrainer()->setBoundsForComponent (ed.get(), { 0, 0, 1000, 400 }, false, false, false, true);
        check (fills(), "dragged wider, it keeps that shape (" + size() + ")");
        ed->getConstrainer()->setBoundsForComponent (ed.get(), { 0, 0, ed->getWidth(), 500 }, false, false, true, false);
        check (fills(), "and dragged shorter (" + size() + ")");
        check (ed->getConstrainer()->getFixedAspectRatio() > 0.0, "a host resizing the window keeps its ratio");
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
