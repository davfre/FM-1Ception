#pragma once

#include <JuceHeader.h>
#include "EngineHandle.h"

//==============================================================================
// FM-1Ception: up to eight emulated FM-1s, summed.
//
// Each engine is fm1-emu's machine on a thread of its own (engine/), so eight
// engines run on eight cores. A thread keeps a ring of its 44.1 kHz frames a
// fixed lead ahead of what this processor has taken; the host's MIDI is
// stamped with the frame it belongs to, so notes keep their timing and the
// lead is a constant reported as the plugin's latency. processBlock only moves
// MIDI in and frames out, resampling each engine to the host's rate.
//
// Switching a slot off joins its thread but keeps the firmware, the flash it
// wrote and the whole machine as it was, so switching it back on (or
// reopening the project) picks up where it left off: the preset being edited,
// the screen it was on.
//
// The engines sum into the main output, or in multi-out each plays out of an
// output pair of its own (engine 1 the main one).
//==============================================================================
class FM1CeptionProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kMaxEngines = 8;
    static constexpr float kDefaultMaster = 900.0f / 1023.0f;
    static inline const int kClocks[] { 120, 160, 240, 320, 400 };

    FM1CeptionProcessor();
    ~FM1CeptionProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    int getNumEngines() const noexcept { return numEngines.load(); }
    void setNumEngines (int n);

    /** The slot's engine while it is switched on, else null. */
    std::shared_ptr<fm1c::EngineHandle> engine (int slot) const;

    /** Bumped whenever engines come or go, or a state is restored. */
    int layoutVersion() const noexcept { return layout.load(); }

    /** Open a .fwsc, .fm1save or preset into a slot (the first two in the background). */
    void loadFile (int slot, const juce::File& file);
    void exportFile (int slot, const juce::File& file);
    void initEngine (int slot);

    /** Load and wait: the smoke test's way in. */
    juce::Result loadFileNow (int slot, const juce::File& file);

    /** A preset is one engine as it is: its firmware, flash, whole machine and clock, in a
        .fm1ception file of its own (a project's state is every engine's, and its mix). Saving
        blocks for a few ms; loading an engine boots it there. Either posts how it went. */
    static constexpr const char* kPresetSuffix = ".fm1ception";
    static juce::File presetsFolder();
    juce::Result savePreset (int slot, const juce::File& file);
    juce::Result loadPreset (int slot, const juce::File& file);

    /** Every other running engine takes this one's firmware, live flash, clock and MASTER, in the
        background. The engine numbers it will replace (those with a firmware) are listed by
        duplicateTargets. */
    void duplicateToAll (int slot);
    juce::Array<int> duplicateTargets (int slot) const;

    /** Mute and solo: an engine is heard unless muted, and while any engine is soloed only the
        soloed ones are. */
    bool isMuted (int slot) const { return slots[(size_t) slot].mute.load(); }
    bool isSoloed (int slot) const { return slots[(size_t) slot].solo.load(); }
    void setMuted (int slot, bool on) { slots[(size_t) slot].mute = on; }
    void setSoloed (int slot, bool on) { slots[(size_t) slot].solo = on; }
    /** Solo this engine alone (or, if it already is the only one, clear every solo). */
    void soloExclusive (int slot);
    bool isAudible (int slot) const;

    /** Multi-out: engine N plays out of output pair N (1: the main output) rather than all into
        the main one. An engine whose pair the host has not enabled stays in the main output. */
    bool isMultiOut() const noexcept { return multiOut.load(); }
    void setMultiOut (bool on) { multiOut = on; }
    /** Is output pair `bus` (0: main) enabled in the host? */
    bool isOutputEnabled (int bus) const;

    int getClock (int slot) const { return slots[(size_t) slot].clockMhz.load(); }
    void setClock (int slot, int mhz);

    /** 0: every channel; 1..16 only that one. */
    int getReceiveChannel (int slot) const { return slots[(size_t) slot].rxChannel.load(); }
    void setReceiveChannel (int slot, int ch) { slots[(size_t) slot].rxChannel = juce::jlimit (0, 16, ch); }

    /** 0: as received; 1..16 every channel message moved to that channel. */
    int getFirmwareChannel (int slot) const { return slots[(size_t) slot].txChannel.load(); }
    void setFirmwareChannel (int slot, int ch) { slots[(size_t) slot].txChannel = juce::jlimit (0, 16, ch); }

    /** How the host's MIDI reaches the engines.
          selected      every channel to the selected engine, as received
          multiChannel  channel N to engine N, moved to channel 1
          custom        each engine's own receive and firmware channels */
    enum class MidiMode { selected = 0, multiChannel = 1, custom = 2 };
    MidiMode getMidiMode() const noexcept { return (MidiMode) midiMode.load(); }
    void setMidiMode (MidiMode m) { midiMode = juce::jlimit (0, 2, (int) m); }

    /** The engine Selected mode plays, and the one the tabbed layout shows. */
    int getSelected() const noexcept { return juce::jmin (selected.load(), numEngines.load() - 1); }
    void setSelected (int slot) { selected = juce::jlimit (0, kMaxEngines - 1, slot); }

    enum class ViewLayout { tiled = 0, tabbed = 1 };
    ViewLayout getViewLayout() const noexcept { return (ViewLayout) viewLayout.load(); }
    void setViewLayout (ViewLayout l) { viewLayout = juce::jlimit (0, 1, (int) l); ++layout; }

    juce::RangedAudioParameter& masterParam (int slot) { return *slots[(size_t) slot].master; }
    juce::RangedAudioParameter& mainParam() { return *mainVolume; }

    /** The output's peaks since the last call (post main volume), per channel. */
    std::array<float, 2> takePeaks();

    /** One engine's peaks since the last call (before the main volume), per channel. */
    std::array<float, 2> takeEnginePeaks (int slot);

    juce::Point<int> getEditorSize() const { return { editorW.load(), editorH.load() }; }
    void setEditorSize (juce::Point<int> s) { editorW = s.x; editorH = s.y; }

    juce::AudioProcessorValueTreeState& getApvts() noexcept { return apvts; }

private:
    struct Slot
    {
        // message side, under slotsLock
        std::shared_ptr<fm1c::EngineHandle> handle;
        juce::String parkedName;
        juce::MemoryBlock parkedPackage, parkedFlash, parkedMachine;

        // audio side: swapped under engineLock
        Fm1e* live = nullptr;
        juce::uint64 taken = 0;
        double frac = 0.0;
        float cur[2] {}, next[2] {};
        // what the firmware was last sent (after any channel move), to let go of it when the
        // routing changes: route -1 = hears nothing, else rx * 32 + tx
        int route = -1;
        std::array<std::bitset<128>, 16> held {};
        std::array<bool, 16> sustain {};

        std::atomic<int> clockMhz { 240 }, rxChannel { 0 }, txChannel { 0 };
        std::atomic<bool> mute { false }, solo { false };
        std::atomic<float> peak[2] { 0.0f, 0.0f };
        juce::AudioParameterFloat* master = nullptr;
        // audio side: the mute / solo gain, ramped to avoid clicks
        float gain = 1.0f;
    };

    static BusesProperties outputBuses();
    juce::AudioProcessorValueTreeState::ParameterLayout buildParameterLayout();
    void startEngine (Slot&);
    void stopEngine (Slot&);
    void runFileJob (int slot, const juce::File&, bool exporting);
    void setLeadFromBlockSize (double sampleRate);
    static void sendMidi (Slot&, Fm1e*, juce::uint64 frame, juce::uint8 status, juce::uint8 d1, juce::uint8 d2, juce::uint8 len);
    static void releaseHeld (Slot&, Fm1e*, juce::uint64 frame);

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioParameterFloat* mainVolume = nullptr;
    std::array<Slot, kMaxEngines> slots;

    juce::CriticalSection slotsLock;   // message side: engines coming and going, state
    juce::CriticalSection engineLock;  // held briefly to swap what the audio thread sees
    std::atomic<int> numEngines { 1 }, layout { 0 };
    std::atomic<int> midiMode { (int) MidiMode::selected }, selected { 0 }, viewLayout { (int) ViewLayout::tiled };
    std::atomic<bool> multiOut { false };

    double ratio = (double) FM1E_RATE / 44100.0;
    int lead = 2048;
    int maxBlock = 512;
    float gainStep = 1.0f / 480.0f;
    juce::SmoothedValue<float> mainGain;
    std::vector<float> monoScratch;
    std::atomic<float> peak[2] { 0.0f, 0.0f };
    std::atomic<int> editorW { 0 }, editorH { 0 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FM1CeptionProcessor)
};
