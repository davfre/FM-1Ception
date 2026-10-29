#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "EngineView.h"
#include "OutputMeter.h"

//==============================================================================
// A minimal header (title, MENU, the main volume meter, ENGINES 1..8) over
// the engines, laid out one of two ways (MENU > Layout):
//
//   Tiled   a grid that takes whatever shape gives the cells the most room:
//           resize the window wide and four engines sit in a row, square and
//           they make 2 x 2; eight go 8 across, 4 x 2 or 2 x 4
//   Tabbed  one engine at a time under browser-like tabs, each with its level
//
// Clicking an engine (or its tab) selects it: Selected MIDI mode plays it.
//==============================================================================
class FM1CeptionEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit FM1CeptionEditor (FM1CeptionProcessor&);
    ~FM1CeptionEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kHeaderH = 28;
    static constexpr int kTabsH = 24;
    static constexpr int kPad = 6;

private:
    /** ENGINES 1 2 3 4 5 6 7 8: click a number to run that many. */
    class EngineCount : public juce::Component,
                        public juce::SettableTooltipClient
    {
    public:
        EngineCount();
        void setCount (int n);
        std::function<void (int)> onChange;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        int preferredWidth() const;

    private:
        juce::Rectangle<float> cell (int i) const;
        int count = 1, hot = -1;
    };

    /** The tabbed layout's tabs: number, firmware and level per engine. */
    class Tabs : public juce::Component
    {
    public:
        explicit Tabs (FM1CeptionEditor& e) : owner (e) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<float> tab (int i, int n) const;
        FM1CeptionEditor& owner;
    };

    /** Tabbed, the window keeps the one engine's shape, so the cell fills it rather than sitting
        between black bands. */
    class Constrainer : public juce::ComponentBoundsConstrainer
    {
    public:
        void checkBounds (juce::Rectangle<int>&, const juce::Rectangle<int>& previous, const juce::Rectangle<int>& limits,
                          bool top, bool left, bool bottom, bool right) override;
        bool keepTabShape = false;
    };

    void timerCallback() override;
    void syncViews();
    void layoutCells();
    void selectEngine (int slot);
    void markSelection();
    bool tabbed() const { return proc.getViewLayout() == FM1CeptionProcessor::ViewLayout::tabbed; }
    juce::Point<int> naturalSize (int engines) const;
    float cellScale() const;
    /** The tabbed window around its cell, and its height at a width with the cell filling it. */
    static juce::Point<int> tabbedChrome();
    static int tabbedHeightFor (int width);
    void updateConstrainer();

    void showMainMenu();
    void showEngineMenu (int slot, int lcdRow);
    void confirmDuplicate (int slot);
    juce::PopupMenu midiMenu();
    juce::String midiModeText() const;
    juce::String routeText (int slot) const;
    juce::PopupMenu channelMenu (int slot);
    juce::PopupMenu clockMenu (int slot);
    juce::String engineLabel (int slot) const;
    void chooseFile (int slot, bool exporting);
    /** The presets folder's tree (subfolders as submenus), each loading into the slot. */
    juce::PopupMenu presetsMenu (int slot, bool withFolderItem);
    void savePresetAs (int slot);

    FM1CeptionProcessor& proc;
    juce::LookAndFeel_V4 lnf;
    juce::TextButton menuButton { "MENU" };
    fm1c::OutputMeter meter;
    EngineCount engineCount;
    Tabs tabs { *this };
    juce::OwnedArray<fm1c::EngineView> views;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ScopedMessageBox confirmBox;
    juce::TooltipWindow tooltips { this, 700 };
    Constrainer constrainer;
    int seenLayout = -1, seenEngines = -1, seenViewLayout = -1, seenSelected = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FM1CeptionEditor)
};
