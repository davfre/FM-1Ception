#pragma once

#include <JuceHeader.h>

//==============================================================================
// The FM-1's colours, from fm1-emu's panel (crates/fm1-live/src/panel.rs), and
// the meter's from SHRUB's header (FM1ShrubOS firmware/ui/view.c).
//==============================================================================
namespace fm1c::theme
{
inline const juce::Colour room      { 0xff18191b };
inline const juce::Colour header    { 0xff111214 };
inline const juce::Colour bodyTop   { 0xff4c4f54 };
inline const juce::Colour bodyBot   { 0xff3e4146 };
inline const juce::Colour tray      { 0xff2b2c2f };
inline const juce::Colour silk      { 0xffc9c6bd };
inline const juce::Colour dimSilk   { 0xff8c8a85 };
inline const juce::Colour btnTop    { 0xff4b4e54 };
inline const juce::Colour btnBot    { 0xff3b3d42 };
inline const juce::Colour btnInk    { 0xff9c9a94 };
inline const juce::Colour keyTop    { 0xff6a5557 };
inline const juce::Colour keyBot    { 0xff4f3f42 };
inline const juce::Colour amber     { 0xfff2a33a };
inline const juce::Colour green     { 0xff62d36e };
inline const juce::Colour bezel     { 0xff0b0b0c };
inline const juce::Colour warn      { 0xffff8a6a };

inline const juce::Colour meterGreen  { 0xff5cff87 };
inline const juce::Colour meterYellow { 0xfffcde67 };
inline const juce::Colour meterRed    { 0xffff5f3f };
inline const juce::Colour meterOff    { 0xff15181b };

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}
} // namespace fm1c::theme
