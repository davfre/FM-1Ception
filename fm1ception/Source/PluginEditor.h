#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "EngineView.h"
#include "OutputMeter.h"

//==============================================================================
// A minimal header (title, MENU, the main volume meter, ENGINES 1..8) over a
// grid of engine cells. The grid takes whatever shape gives the cells the
// most room: resize the window wide and four engines sit in a row, square
// and they make 2 x 2; eight go 8 across, 4 x 2 or 2 x 4.
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

    void timerCallback() override;
    void syncViews();
    void layoutCells();
    juce::Point<int> naturalSize (int engines) const;
    float cellScale() const;

    void showMainMenu();
    void showEngineMenu (int slot);
    juce::PopupMenu channelMenu (int slot);
    juce::PopupMenu clockMenu (int slot);
    juce::String engineLabel (int slot) const;
    void chooseFile (int slot, bool exporting);

    FM1CeptionProcessor& proc;
    juce::LookAndFeel_V4 lnf;
    juce::TextButton menuButton { "MENU" };
    fm1c::OutputMeter meter;
    EngineCount engineCount;
    juce::OwnedArray<fm1c::EngineView> views;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 700 };
    juce::ComponentBoundsConstrainer constrainer;
    int seenLayout = -1, seenEngines = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FM1CeptionEditor)
};
