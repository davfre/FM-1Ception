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
// Switching a slot off joins its thread but keeps the firmware and the flash
// it wrote, so switching it back on (or reopening the project) restores it.
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
    void releaseResources() override { awake.reset(); }
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

    /** Open a .fwsc or .fm1save into a slot, in the background. */
    void loadFile (int slot, const juce::File& file);
    void exportFile (int slot, const juce::File& file);
    void initEngine (int slot);

    /** Load and wait: the smoke test's way in. */
    juce::Result loadFileNow (int slot, const juce::File& file);

    int getClock (int slot) const { return slots[(size_t) slot].clockMhz.load(); }
    void setClock (int slot, int mhz);

    /** 0: every channel; 1..16 only that one. */
    int getReceiveChannel (int slot) const { return slots[(size_t) slot].rxChannel.load(); }
    void setReceiveChannel (int slot, int ch) { slots[(size_t) slot].rxChannel = juce::jlimit (0, 16, ch); }

    /** 0: as received; 1..16 every channel message moved to that channel. */
    int getFirmwareChannel (int slot) const { return slots[(size_t) slot].txChannel.load(); }
    void setFirmwareChannel (int slot, int ch) { slots[(size_t) slot].txChannel = juce::jlimit (0, 16, ch); }

    juce::RangedAudioParameter& masterParam (int slot) { return *slots[(size_t) slot].master; }
    juce::RangedAudioParameter& mainParam() { return *mainVolume; }

    /** The output's peaks since the last call (post main volume), per channel. */
    std::array<float, 2> takePeaks();

    juce::Point<int> getEditorSize() const { return { editorW.load(), editorH.load() }; }
    void setEditorSize (juce::Point<int> s) { editorW = s.x; editorH = s.y; }

    juce::AudioProcessorValueTreeState& getApvts() noexcept { return apvts; }

private:
    struct Slot
    {
        // message side, under slotsLock
        std::shared_ptr<fm1c::EngineHandle> handle;
        juce::String parkedName;
        juce::MemoryBlock parkedPackage, parkedFlash;

        // audio side: swapped under engineLock
        Fm1e* live = nullptr;
        juce::uint64 taken = 0;
        double frac = 0.0;
        float cur[2] {}, next[2] {};

        std::atomic<int> clockMhz { 240 }, rxChannel { 0 }, txChannel { 0 };
        juce::AudioParameterFloat* master = nullptr;
    };

    juce::AudioProcessorValueTreeState::ParameterLayout buildParameterLayout();
    void startEngine (Slot&);
    void stopEngine (Slot&);
    void runFileJob (int slot, const juce::File&, bool exporting);
    void setLeadFromBlockSize (double sampleRate);

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioParameterFloat* mainVolume = nullptr;
    std::array<Slot, kMaxEngines> slots;

    juce::CriticalSection slotsLock;   // message side: engines coming and going, state
    juce::CriticalSection engineLock;  // held briefly to swap what the audio thread sees
    std::atomic<int> numEngines { 1 }, layout { 0 };

    double ratio = FM1E_RATE / 44100.0;
    int lead = 2048;
    int maxBlock = 512;
    juce::SmoothedValue<float> mainGain;
    std::atomic<float> peak[2] { 0.0f, 0.0f };
    std::atomic<int> editorW { 0 }, editorH { 0 };

    // The engines run on threads of their own, not the host's audio thread, so
    // macOS would App Nap them (timers coalesced, threads throttled to a fraction
    // of real time) whenever it decides the host is idle.
    std::unique_ptr<juce::ScopedLowPowerModeDisabler> awake;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FM1CeptionProcessor)
};
