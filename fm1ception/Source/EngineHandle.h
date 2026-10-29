#pragma once

#include <JuceHeader.h>
#include "fm1ception_engine.h"

namespace fm1c
{
//==============================================================================
// One emulated FM-1 (engine/src/lib.rs): its thread starts with the handle and
// is joined when the last owner lets go. The processor owns it while its slot
// is switched on; a file job running in the background holds it too, so
// switching the slot off mid-load cannot pull it out from under the job.
//==============================================================================
class EngineHandle
{
public:
    EngineHandle() : e (fm1e_create()) {}
    ~EngineHandle() { fm1e_destroy (e); }

    bool isValid() const noexcept { return e != nullptr; }
    Fm1e* get() const noexcept { return e; }

    /** A line for the engine's status strip, shown for a few seconds. */
    void post (const juce::String& text, int ms = 3500)
    {
        const juce::ScopedLock sl (lock);
        message = text;
        until = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    }

    juce::String currentMessage() const
    {
        const juce::ScopedLock sl (lock);
        return juce::Time::getMillisecondCounter() < until ? message : juce::String();
    }

    juce::String firmwareName() const
    {
        const auto n = fm1e_name (e, nullptr, 0);
        juce::HeapBlock<juce::uint8> buf (n + 1, true);
        fm1e_name (e, buf.get(), n);
        return juce::String::fromUTF8 ((const char*) buf.get(), (int) n);
    }

    juce::MemoryBlock package() const
    {
        juce::MemoryBlock mb (fm1e_package (e, nullptr, 0));
        fm1e_package (e, (juce::uint8*) mb.getData(), mb.getSize());
        return mb;
    }

    juce::MemoryBlock flash() const
    {
        juce::MemoryBlock mb (fm1e_flash (e, nullptr, 0));
        fm1e_flash (e, (juce::uint8*) mb.getData(), mb.getSize());
        return mb;
    }

    /** The whole machine as it is now, which flash() goes with from then on (empty: no firmware
        runs, or it has stopped). Blocks for a few ms. */
    juce::MemoryBlock saveState() const
    {
        if (fm1e_save_state (e, nullptr, 0) != 0)
            return {};
        juce::MemoryBlock mb (fm1e_state (e, nullptr, 0));
        fm1e_state (e, (juce::uint8*) mb.getData(), mb.getSize());
        return mb;
    }

    bool hasFirmware() const { return fm1e_package (e, nullptr, 0) > 0; }

    std::atomic<bool> fileJob { false };

private:
    Fm1e* const e;
    juce::CriticalSection lock;
    juce::String message;
    juce::uint32 until = 0;

    JUCE_DECLARE_NON_COPYABLE (EngineHandle)
};
} // namespace fm1c
