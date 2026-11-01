#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "OutputMeter.h"

namespace fm1c
{
//==============================================================================
// One engine's cell: the FM-1's screen at 2x, and everything else small under
// it, drawn in a 480-wide design space scaled to the cell (628 high as it
// opens; MENU > View's options take sections away or add them):
//
//   strip    engine number, firmware, clock and speed, then M(ute), S(olo)
//            and the engine's level with the MASTER pot as a caret over it
//            (drag or scroll it); right-click: the engine's menu
//   screen   240 x 240 at 2x (LOAD FIRMWARE and OPEN BROWSER buttons while it is blank)
//   rows     (not in Screens Only)
//            KNOB1..4, straight under the screen (unlabelled: the firmware's
//            screen names them); then SELECT PRESETS ALGO and OCT- / OCT+
//            under the screen's left half, a mini keyboard (F3..G5) under
//            its right half, and the panel buttons' toggle in the corner
//   edit     (Edit Buttons Show) the panel buttons, two rows of six
//   keys     (Large Keyboard) F3..G5 across the whole cell, in place of the
//            mini keyboard
//
// Without the edit row the panel buttons (FX..GLO, HOME..REC) live in a
// popover over the rows, from the screen's foot down to the toggle: shown
// while Shift is held over the cell, or latched open by the toggle.
// Right-click a button to latch it down (for the firmware's button + knob
// combinations); click it to let go.
//
// Soft knobs: many firmwares draw KNOB1..4 across the screen. Once a row is
// set for the firmware (right-click the screen; saved in its prefs file), a
// band around that row is split in four and dragging there turns the knobs.
//
// Hardware (MENU > View > Hardware): the strip over the FM-1's whole front
// panel as fm1-emu draws it (fm1-live's panel.rs, 1280 x 820 at half size):
// MASTER (this engine's MASTER, as the strip's caret is), SELECT, PRESETS,
// ALGORITHM and OCT- / OCT+ on the left, the screen in its bezel, KNOB1..4
// and the twelve buttons on the right, the 27 pill keys across the bottom.
// The view options do not apply to it.
//==============================================================================
class EngineView : public juce::Component,
                   public juce::FileDragAndDropTarget,
                   public juce::TooltipClient
{
public:
    /** The design's width, and its height as it opens (the screen and the rows). */
    static constexpr float kW = 480.0f, kH = 628.0f;
    /** The design's size: with these FM1CeptionProcessor::ViewOption bits, or as the front panel. */
    static float widthFor (bool hardware);
    static float heightFor (int viewOptions, bool hardware = false);

    EngineView (FM1CeptionProcessor&, int slot);
    ~EngineView() override;

    int slot() const noexcept { return index; }
    void setEngine (std::shared_ptr<EngineHandle>);
    void setViewOptions (int viewOptions);
    void setHardware (bool asFrontPanel);

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
    /** OPEN BROWSER on a blank engine: the firmware library. */
    std::function<void (int slot)> onBrowseRequest;
    /** A file dropped on it, to load: the processor loads it itself if this is not set. */
    std::function<void (int slot, const juce::File&)> onFileDrop;
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
        enum Kind { none, strip, screen, menu, loadButton, browseButton, knob, softKnob, button, key, toggle, popoverBack,
                    mute, solo, level, pot } kind = none;
        int id = -1;   // knob: 0..6 (KNOB1-4, SELECT, PRESETS, ALGO); softKnob: 0..3; button / key: matrix id
    };

    juce::Point<float> toDesign (juce::Point<float> p) const;
    juce::AffineTransform designToLocal() const;
    void repaintDesign (juce::Rectangle<float> r);
    bool popoverShown() const;

    float designW() const { return widthFor (hardware); }
    juce::Rectangle<float> stripArea() const { return { 0.0f, 0.0f, designW(), 20.0f }; }
    juce::Rectangle<float> muteArea() const;
    juce::Rectangle<float> soloArea() const;
    juce::Rectangle<float> levelArea() const;
    juce::Rectangle<float> trackArea() const;
    juce::Rectangle<float> menuArea() const;
    /** The screen's space (the screen 480 x 480 at 0, 20) into the design: as it is, or into the
        panel's bezel. */
    juce::AffineTransform screenToDesign() const;
    juce::Point<float> toScreen (juce::Point<float> design) const;
    void repaintScreen (juce::Rectangle<float> inScreenSpace);
    /** The front panel's coordinates (1280 x 820) into the design, under the strip. */
    static juce::AffineTransform panelToDesign();
    Hit hitPanel (juce::Point<float> panel) const;
    void paintPanel (juce::Graphics&);
    void paintPanelButton (juce::Graphics&, juce::Rectangle<float>, const char* label, int id);
    void paintPanelKey (juce::Graphics&, juce::Rectangle<float>, int id);
    void repaintPot();

    /** The sections under the screen as the view options have them (empty when not shown). */
    bool shows (FM1CeptionProcessor::ViewOption o) const noexcept { return ! hardware && (options & o) != 0; }
    juce::Rectangle<float> rowsArea() const;
    juce::Rectangle<float> editArea() const;
    juce::Rectangle<float> bigKeysArea() const;
    juce::Rectangle<float> belowScreen() const;
    /** Where the panel buttons are: the edit row's, or else the popover's. */
    juce::Rectangle<float> panelArea() const;
    /** The keyboard: the large one's keys, or else the mini one's. */
    juce::Rectangle<float> keysArea() const;
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
    void paintEditRow (juce::Graphics&);
    void paintKeyboard (juce::Graphics&);
    void paintToggle (juce::Graphics&);
    void paintPopover (juce::Graphics&);
    void paintButton (juce::Graphics&, juce::Rectangle<float>, const juce::String& label, int id, float labelSize);

    FM1CeptionProcessor& proc;
    const int index;
    std::shared_ptr<EngineHandle> handle;
    int options = 0;
    bool hardware = false;

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
