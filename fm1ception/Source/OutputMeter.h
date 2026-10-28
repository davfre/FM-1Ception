#pragma once

#include <JuceHeader.h>

namespace fm1c
{
//==============================================================================
// SHRUB's output meter (FM1ShrubOS firmware/ui/view.c and ui.c): two rows of
// segments over the top 60 dB, green to yellow to red near the ceiling, each
// channel's peak held a second, and a caret above them marking the volume on
// the same scale. Here the caret is the plugin's Main Volume: drag along the
// meter, scroll over it, double-click for 0 dB.
//==============================================================================
class OutputMeter : public juce::Component,
                    public juce::SettableTooltipClient
{
public:
    explicit OutputMeter (juce::RangedAudioParameter& mainVolume);

    /** The output's peaks (linear) since the last call. Message thread, ~30 Hz. */
    void push (std::array<float, 2> peaks);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int kSegments = 25;
    static constexpr float kRangeDb = 60.0f;

private:
    struct Geometry
    {
        float k, x0, segW, caretTop, row0, rowH, rowGap;
    };
    Geometry geometry() const;
    float positionOf (float db) const;
    void setFromX (float x);
    void setValue (float v);
    void refreshTooltip();

    juce::RangedAudioParameter& param;
    float meterDb[2] { -120.0f, -120.0f }, holdDb[2] { -120.0f, -120.0f };
    juce::uint32 holdAt[2] {}, lastPush = 0;
    bool dragging = false;
};
} // namespace fm1c
