#include "OutputMeter.h"
#include "Theme.h"

namespace fm1c
{
namespace
{
constexpr float kFloorDb = -96.0f;

/** v -> dB of the gain v^2 */
float dbOfValue (float v) { return v > 0.0f ? 40.0f * std::log10 (v) : -120.0f; }
float valueOfDb (float db) { return std::pow (10.0f, db / 40.0f); }

juce::Colour segmentColour (int seg)
{
    if (seg >= OutputMeter::kSegments - 2)
        return theme::meterRed;   // the top 2: the knee
    if (seg >= OutputMeter::kSegments * 3 / 4)
        return theme::meterYellow;
    return theme::meterGreen;
}
} // namespace

OutputMeter::OutputMeter (juce::RangedAudioParameter& p) : param (p)
{
    setRepaintsOnMouseActivity (false);
    refreshTooltip();
}

void OutputMeter::push (std::array<float, 2> peaks)
{
    const auto now = juce::Time::getMillisecondCounter();
    const float dt = lastPush == 0 ? 0.033f : juce::jmin (0.2f, (float) (now - lastPush) / 1000.0f);
    lastPush = now;
    const float fall = 24.0f * dt;   // dB a second

    for (int c = 0; c < 2; ++c)
    {
        const float db = peaks[(size_t) c] > 0.0f ? 20.0f * std::log10 (peaks[(size_t) c]) : kFloorDb;
        meterDb[c] = juce::jmax (db, meterDb[c] - fall, kFloorDb);
        if (db >= holdDb[c])
        {
            holdDb[c] = db;
            holdAt[c] = now;
        }
        else if (now - holdAt[c] > 1000)
        {
            holdDb[c] = juce::jmax (kFloorDb, holdDb[c] - 1.5f * fall);
        }
    }
    refreshTooltip();
    repaint();
}

OutputMeter::Geometry OutputMeter::geometry() const
{
    // SHRUB's header in its own pixels: 3 px segments (2 lit), 4 px rows 1 px
    // apart, a 4-row caret 2 px above them. Scaled to fit, with room for the
    // caret's overhang at the ends.
    const float w = (float) getWidth(), h = (float) getHeight();
    const float k = juce::jmin ((w - 2.0f) / (kSegments * 3.0f + 6.0f), h / 15.0f);
    Geometry g;
    g.k = k;
    g.segW = 3.0f * k;
    g.x0 = std::round ((w - kSegments * g.segW) * 0.5f);
    const float total = 15.0f * k;
    g.caretTop = std::round ((h - total) * 0.5f);
    g.row0 = g.caretTop + 6.0f * k;
    g.rowH = 4.0f * k;
    g.rowGap = 1.0f * k;
    return g;
}

float OutputMeter::positionOf (float db) const
{
    return juce::jlimit (0.0f, 1.0f, (db + kRangeDb) / kRangeDb);
}

void OutputMeter::paint (juce::Graphics& g)
{
    const auto geo = geometry();

    for (int c = 0; c < 2; ++c)
    {
        const float y = geo.row0 + (float) c * (geo.rowH + geo.rowGap);
        const int lit = (int) std::ceil (positionOf (meterDb[c]) * kSegments - 0.004f);
        const int hold = (int) std::ceil (positionOf (holdDb[c]) * kSegments - 0.004f) - 1;
        for (int s = 0; s < kSegments; ++s)
        {
            const auto col = segmentColour (s);
            const bool on = s < lit || (s == hold && holdDb[c] > -kRangeDb);
            g.setColour (on ? col : col.interpolatedWith (theme::meterOff, 6.0f / 7.0f));
            g.fillRect (geo.x0 + (float) s * geo.segW, y, geo.segW - geo.k, geo.rowH);
        }
    }

    // the caret: a small triangle pointing down at the volume
    const float v = param.convertFrom0to1 (param.getValue());
    const float span = kSegments * geo.segW - geo.k;
    const float cx = geo.x0 + (v > 0.0f ? positionOf (dbOfValue (v)) : 0.0f) * span;
    juce::Path caret;
    caret.addTriangle (cx - 3.5f * geo.k, geo.caretTop, cx + 3.5f * geo.k, geo.caretTop, cx, geo.caretTop + 4.0f * geo.k);
    g.setColour (v > 0.0f ? juce::Colours::white : theme::dimSilk);
    g.fillPath (caret);
}

void OutputMeter::setValue (float v)
{
    param.setValueNotifyingHost (param.convertTo0to1 (juce::jlimit (0.0f, 1.0f, v)));
    refreshTooltip();
    repaint();
}

void OutputMeter::setFromX (float x)
{
    const auto geo = geometry();
    const float pos = (x - geo.x0) / (kSegments * geo.segW - geo.k);
    setValue (pos <= 0.0f ? 0.0f : valueOfDb (-kRangeDb + kRangeDb * juce::jmin (1.0f, pos)));
}

void OutputMeter::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    param.beginChangeGesture();
    setFromX (e.position.x);
}

void OutputMeter::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
        setFromX (e.position.x);
}

void OutputMeter::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
        param.endChangeGesture();
    dragging = false;
}

void OutputMeter::mouseDoubleClick (const juce::MouseEvent&)
{
    param.beginChangeGesture();
    setValue (1.0f);
    param.endChangeGesture();
}

void OutputMeter::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float v = param.convertFrom0to1 (param.getValue());
    const float db = juce::jlimit (-kRangeDb, 0.0f, (v > 0.0f ? dbOfValue (v) : -kRangeDb) + w.deltaY * 12.0f);
    param.beginChangeGesture();
    setValue (valueOfDb (db));
    param.endChangeGesture();
}

void OutputMeter::refreshTooltip()
{
    setTooltip ("Main Volume " + param.getCurrentValueAsText()
                + "   (drag or scroll; double-click for 0 dB)");
}
} // namespace fm1c
