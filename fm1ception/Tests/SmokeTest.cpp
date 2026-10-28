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

    auto shoot = [&] (int n, int w, int h, const juce::String& file)
    {
        p.setNumEngines (n);
        for (int i = 0; i < n; ++i)
            if (auto e = p.engine (i); e != nullptr && ! e->hasFirmware() && (n == 1 || i != 1))
                p.loadFileNow (i, fw);
        juce::Thread::sleep (3500);

        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (w, h);
        int k = 0;
        for (auto* c : ed->getChildren())
            if (auto* v = dynamic_cast<fm1c::EngineView*> (c))
            {
                v->refresh();
                v->setShiftHeld (n > 1 && k++ == 2);
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
    }

    std::cout << "\n=== state round trip ===" << std::endl;
    {
        FM1CeptionProcessor p;
        p.prepareToPlay (kSr, kBlock);
        p.setNumEngines (3);
        p.setClock (1, 160);
        p.setReceiveChannel (1, 3);
        p.setFirmwareChannel (2, 5);
        p.mainParam().setValueNotifyingHost (p.mainParam().convertTo0to1 (0.5f));

        juce::MemoryBlock state;
        p.getStateInformation (state);

        FM1CeptionProcessor q;
        q.prepareToPlay (kSr, kBlock);
        q.setStateInformation (state.getData(), (int) state.getSize());
        check (q.getNumEngines() == 3, "engine count");
        check (q.getClock (1) == 160 && q.getClock (0) == (int) fm1e_default_mhz(), "clocks");
        check (q.getReceiveChannel (1) == 3 && q.getFirmwareChannel (2) == 5, "MIDI channels");
        check (std::abs (q.mainParam().convertFrom0to1 (q.mainParam().getValue()) - 0.5f) < 1.0e-4f, "main volume");

        const char junk[] = "not a state at all";
        q.setStateInformation (junk, (int) sizeof (junk));
        check (q.getNumEngines() == 3, "a foreign state is ignored");
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
        }
        {
            FM1CeptionProcessor q;
            q.prepareToPlay (kSr, kBlock);
            q.setStateInformation (state.getData(), (int) state.getSize());
            auto h = q.engine (0);
            check (h != nullptr && h->hasFirmware() && h->firmwareName() == name, "the firmware comes back with the project");
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
    }

    std::cout << std::endl
              << (failures == 0 ? "ALL CHECKS PASSED" : juce::String (failures) + " FAILURES").toStdString()
              << std::endl;
    return failures == 0 ? 0 : 1;
}
