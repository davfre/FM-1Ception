#pragma once

#include <JuceHeader.h>

//==============================================================================
// Settings that belong to a firmware rather than a project, one JSON file per
// firmware (by its package name) under the user's application data:
//
//   macOS    ~/Library/Application Support/Xfer Records/FM-1Ception/Firmware/
//   Windows  %APPDATA%\Xfer Records\FM-1Ception\Firmware\
//
//   { "softKnobRow": 132 }   the screen row (0..239) of the firmware's on-screen
//                            knobs: touching the screen there turns KNOB1..4
//==============================================================================
namespace fm1c::prefs
{
inline juce::File folder()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
    return base.getChildFile ("Xfer Records").getChildFile ("FM-1Ception").getChildFile ("Firmware");
}

inline juce::File fileFor (const juce::String& firmware)
{
    return folder().getChildFile (juce::File::createLegalFileName (firmware) + ".json");
}

/** Bumped by every write, so views showing the same firmware pick a change up. */
inline std::atomic<int>& revision()
{
    static std::atomic<int> r { 0 };
    return r;
}

inline juce::var read (const juce::String& firmware)
{
    if (firmware.isEmpty())
        return {};
    const auto f = fileFor (firmware);
    return f.existsAsFile() ? juce::JSON::parse (f) : juce::var();
}

/** -1: none set. */
inline int softKnobRow (const juce::String& firmware)
{
    const auto v = read (firmware).getProperty ("softKnobRow", -1);
    return v.isInt() || v.isDouble() ? juce::jlimit (-1, 239, (int) v) : -1;
}

/** row < 0 removes it. */
inline void setSoftKnobRow (const juce::String& firmware, int row)
{
    if (firmware.isEmpty())
        return;
    auto v = read (firmware);
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
    {
        v = juce::var (new juce::DynamicObject());
        obj = v.getDynamicObject();
    }
    if (row >= 0)
        obj->setProperty ("softKnobRow", juce::jlimit (0, 239, row));
    else
        obj->removeProperty ("softKnobRow");

    const auto f = fileFor (firmware);
    if (obj->getProperties().isEmpty())
        f.deleteFile();
    else if (f.getParentDirectory().createDirectory())
        f.replaceWithText (juce::JSON::toString (v));
    ++revision();
}
} // namespace fm1c::prefs
