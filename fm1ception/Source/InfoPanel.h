#pragma once

#include <JuceHeader.h>
#include "ScreenCache.h"

namespace fm1c
{
//==============================================================================
// The library window's side panel: one firmware (the row under the mouse, else
// the selected one) with its screens, large, and small ones to pick from (a
// moment after it boots, once it has settled, the Hub's card if it has one),
// then what it is, who made it and the rest. A firmware with a package to hand
// has its screens captured once it has been shown a moment (ScreenCache); one of
// the Cloud's not downloaded yet has PREVIEW SCREENS to fetch it for that.
//==============================================================================
class InfoPanel : public juce::Component,
                  private juce::ChangeListener,
                  private juce::Timer
{
public:
    static constexpr int kWidth = 300;

    struct Info
    {
        /** What it shows ("" for nothing: `placeholder`). */
        juce::String key, placeholder;
        juce::String title, version, author, about, category;
        /** How the lists know it, or what the Cloud says of it. */
        juce::String note;
        /** Stars of FirmwareLibrary::kMaxRating; -1 shows none. */
        int rating = -1;
        std::vector<std::pair<juce::String, juce::String>> facts;
        /** Its project's page (the last fact, a link). */
        juce::String link;
        /** ScreenCache's key for its screens ("" for none), and the package to capture them from,
            else what fetches it. */
        juce::String shotsKey;
        juce::File package;
        ScreenCache::Fetch fetch;
        /** The Hub's artwork for it. */
        juce::String card;
    };

    explicit InfoPanel (ScreenCache*);
    ~InfoPanel() override;

    void show (Info);
    const Info& current() const { return info; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** How long a firmware is shown before its screens are captured. */
    static constexpr int kDwellMs = 350;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshPictures();
    void layout();
    juce::Image picture (int which) const;
    int shownPicture() const;
    void captureNow (bool again);

    ScreenCache* const cache;
    Info info;
    ScreenCache::Shots shots;
    juce::Image card;
    /** 0 the early screen, 1 the late, 2 the card. */
    int picked = 1, hovered = -1;
    juce::Rectangle<int> big, linkArea;
    juce::Rectangle<int> thumbs[3];
    bool overLink = false;
    juce::TextButton action;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InfoPanel)
};
} // namespace fm1c
