#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "OutputMeter.h"

namespace fm1c
{
//==============================================================================
// One engine's cell: the FM-1's screen at 2x, and everything else small under
// it, drawn in a 480 x 628 design space scaled to the cell.
//
//   strip    engine number, firmware, clock and speed, then M(ute), S(olo)
//            and the engine's level with the MASTER pot as a caret over it
//            (drag or scroll it); right-click: the engine's menu
//   screen   240 x 240 at 2x (a LOAD FIRMWARE button while it is blank)
//   row 1    KNOB1..4, straight under the screen (unlabelled: the firmware's
//            screen names them)
//   row 2    SELECT PRESETS ALGO and OCT- / OCT+ under the screen's left
//            half; a mini keyboard (F3..G5) under its right half, and the
//            panel buttons' toggle in the corner
//
// The panel buttons (FX..GLO, HOME..REC) live in a popover over the rows,
// from the screen's foot down to the toggle: shown while Shift is held over
// the cell, or latched open by the toggle. Right-click one to latch it down
// (for the firmware's button + knob combinations); click it to let go.
//
// Soft knobs: many firmwares draw KNOB1..4 across the screen. Once a row is
// set for the firmware (right-click the screen; saved in its prefs file), a
// band around that row is split in four and dragging there turns the knobs.
//==============================================================================
class EngineView : public juce::Component,
                   public juce::FileDragAndDropTarget,
                   public juce::TooltipClient
{
public:
    static constexpr float kW = 480.0f, kH = 628.0f;

    EngineView (FM1CeptionProcessor&, int slot);
    ~EngineView() override;

    int slot() const noexcept { return index; }
    void setEngine (std::shared_ptr<EngineHandle>);

    /** Pull the screen, LEDs and status; repaint what changed. ~30 Hz. */
    void refresh();

    /** Shift held while the pointer is over this cell. */
    void setShiftHeld (bool held);

    /** Marked as the selected engine (the editor only marks one when there are several). */
    void setSelected (bool isSelected);

    /** The firmware's name ("" while blank) and the engine's level, for the tabs. */
    juce::String firmwareName() const { return firmware; }
    const MiniLevel& level() const noexcept { return meter; }

    /** The soft knobs' screen row (0..239), -1 when none is set. Writes the firmware's prefs. */
    int softKnobRow() const noexcept { return softRow; }
    void setSoftKnobRow (int row);

    std::function<void (int slot)> onLoadRequest;
    /** lcdRow: the screen row right-clicked, -1 off the screen. */
    std::function<void (int slot, int lcdRow)> onMenuRequest;
    std::function<void (int slot)> onSelect;

    void paint (juce::Graphics&) override;
    void resized() override {}
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    juce::String getTooltip() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    /** What a point in the design space is. */
    struct Hit
    {
        enum Kind { none, strip, screen, menu, loadButton, knob, softKnob, button, key, toggle, popoverBack,
                    mute, solo, level } kind = none;
        int id = -1;   // knob: 0..6 (KNOB1-4, SELECT, PRESETS, ALGO); softKnob: 0..3; button / key: matrix id
    };

    juce::Point<float> toDesign (juce::Point<float> p) const;
    juce::AffineTransform designToLocal() const;
    void repaintDesign (juce::Rectangle<float> r);
    bool popoverShown() const;
    Hit hitAt (juce::Point<float> d) const;
    void press (int id, bool down);
    void releaseAll();
    void turn (int knob, int clicks);
    void setMaster (float v);
    void setMasterAt (float designX);
    bool isBlank() const;

    int lcdRowAt (juce::Point<float> d) const;
    void paintStrip (juce::Graphics&);
    void paintStripLevel (juce::Graphics&);
    void paintStripButton (juce::Graphics&, juce::Rectangle<float>, const char* label, bool on, juce::Colour onColour, bool outlined);
    void paintScreen (juce::Graphics&);
    void paintSoftKnobs (juce::Graphics&);
    void paintRows (juce::Graphics&);
    void paintKeyboard (juce::Graphics&);
    void paintToggle (juce::Graphics&);
    void paintPopover (juce::Graphics&);
    void paintButton (juce::Graphics&, juce::Rectangle<float>, const juce::String& label, int id, float labelSize);

    FM1CeptionProcessor& proc;
    const int index;
    std::shared_ptr<EngineHandle> handle;

    juce::Image lcd { juce::Image::ARGB, FM1E_LCD_W, FM1E_LCD_H, true };
    std::vector<juce::uint16> lcdRaw, lcdPrev;
    std::array<juce::uint8, FM1E_LEDS> ledLevel {};
    Fm1eStatus status {};
    bool hasFirmware = false;
    juce::String firmware, stripLeft, stripRight;
    MiniLevel meter;
    // the strip's level: its peak held a second, and what was last drawn (in quarter design px)
    float holdDb = -120.0f;
    juce::uint32 holdAt = 0;
    int drawnLevel = -1, drawnHold = -1, drawnCaret = -1;
    bool drawnMute = false, drawnSolo = false, drawnAudible = true;
    int softRow = -1, prefsRevision = -1;
    bool selectedMark = false;

    std::array<bool, 64> down {};
    std::array<bool, 64> latched {};
    std::array<int, 8> clicks {};
    std::array<int, 8> glow {};
    bool toggleOpen = false, shiftHeld = false, dropHover = false;
    juce::Point<float> hover { -1.0f, -1.0f };

    struct Grab
    {
        Hit hit;
        float lastY = 0.0f, acc = 0.0f;
        int held = -1;
    };
    std::optional<Grab> grab;
    float wheelAcc = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineView)
};
} // namespace fm1c
