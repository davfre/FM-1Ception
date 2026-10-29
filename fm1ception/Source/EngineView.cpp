#include "EngineView.h"
#include "FirmwarePrefs.h"
#include "Theme.h"

namespace fm1c
{
namespace
{
using R = juce::Rectangle<float>;

const R kStrip  { 0.0f, 0.0f, 480.0f, 20.0f };
const R kScreen { 0.0f, 20.0f, 480.0f, 480.0f };
const R kRows   { 0.0f, 500.0f, 480.0f, 128.0f };
const R kLoad   { 150.0f, 240.0f, 180.0f, 40.0f };
const R kMenu   { 456.0f, 0.0f, 24.0f, 20.0f };
const R kToggle { 452.0f, 596.0f, 24.0f, 26.0f };
const R kPopover { 4.0f, 502.0f, 472.0f, 90.0f };
const R kKeys   { 248.0f, 568.0f, 196.0f, 54.0f };

// the strip's M, S and level: a solid bar with the MASTER caret over it
const R kMute   { 302.0f, 4.0f, 15.0f, 12.0f };
const R kSolo   { 320.0f, 4.0f, 15.0f, 12.0f };
const R kLevel  { 338.0f, 0.0f, 116.0f, 20.0f };
const R kTrack  { 342.0f, 10.0f, 108.0f, 5.0f };

/** The strip level's and the caret's scale: dB over the top 60, the caret at the gain v^2 of the
    MASTER pot's v (SHRUB's curve, and the main volume's). */
constexpr float kLevelRangeDb = 60.0f;
float levelPos (float db) { return juce::jlimit (0.0f, 1.0f, (db + kLevelRangeDb) / kLevelRangeDb); }
float masterPos (float v) { return v > 0.0f ? levelPos (40.0f * std::log10 (v)) : 0.0f; }
float masterAt (float pos) { return pos <= 0.0f ? 0.0f : std::pow (10.0f, (kLevelRangeDb * (juce::jmin (1.0f, pos) - 1.0f)) / 40.0f); }

/** Half the soft knobs' band, in screen rows. */
constexpr int kSoftBand = 22;

struct KnobSpec
{
    float cx, cy, r;
    const char* label;
    int enc;   // the matrix encoder
};

// KNOB1..4 go unlabelled: the firmware's screen names them
constexpr int kNumKnobs = 7, kPresetsKnob = 5;
const KnobSpec kKnobs[kNumKnobs] {
    {  60.0f, 530.0f, 17.0f, "KNOB1", 2 },
    { 180.0f, 530.0f, 17.0f, "KNOB2", 3 },
    { 300.0f, 530.0f, 17.0f, "KNOB3", 4 },
    { 420.0f, 530.0f, 17.0f, "KNOB4", 5 },
    {  28.0f, 586.0f, 16.0f, "SELECT", 0 },
    {  82.0f, 586.0f, 16.0f, "PRESETS", 6 },
    { 136.0f, 586.0f, 16.0f, "ALGO", 1 },
};

struct ButtonSpec
{
    R r;
    const char* label;
    int id;
};

const ButtonSpec kOct[2] {
    { { 164.0f, 576.0f, 36.0f, 20.0f }, "OCT-", 0 },
    { { 204.0f, 576.0f, 36.0f, 20.0f }, "OCT+", 1 },
};

constexpr const char* kPanelLabels[12] { "FX", "SCL", "ENV", "LFO", "EDIT", "GLO",
                                         "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC" };
constexpr int kPlay = 12;

R panelButton (int i)   // i: 0..11, matrix id 2 + i
{
    const float bw = (kPopover.getWidth() - 16.0f - 5.0f * 6.0f) / 6.0f, bh = 34.0f;
    return { kPopover.getX() + 8.0f + (float) (i % 6) * (bw + 6.0f),
             kPopover.getY() + 8.0f + (float) (i / 6) * (bh + 6.0f), bw, bh };
}

// F3..G5: 16 white keys, 11 black over the gaps
constexpr int kWhites[16] { 0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26 };
constexpr int kBlacks[11][2] { { 1, 0 }, { 3, 1 }, { 5, 2 }, { 8, 4 }, { 10, 5 }, { 13, 7 },
                               { 15, 8 }, { 17, 9 }, { 20, 11 }, { 22, 12 }, { 25, 14 } };
constexpr int kFirstKey = 14;

R whiteKey (int i)
{
    const float kw = kKeys.getWidth() / 16.0f;
    return { kKeys.getX() + (float) i * kw, kKeys.getY(), kw, kKeys.getHeight() };
}

R blackKey (int after)
{
    const float kw = kKeys.getWidth() / 16.0f, bw = kw * 0.62f;
    const float x = kKeys.getX() + (float) (after + 1) * kw - bw * 0.5f;
    return { x, kKeys.getY(), bw, kKeys.getHeight() * 0.58f };
}

/** An LED's lit fraction as a brightness: a fully lit LED is on ~1 tick in 11. */
float brightness (juce::uint8 level) { return (float) level / 16.0f; }

juce::uint8 levelOf (float fraction)
{
    return (juce::uint8) juce::roundToInt (juce::jlimit (0.0f, 1.0f, fraction / 0.08f) * 16.0f);
}

/** A knob in the panel's grey, or (rose) in the mini keyboard's colour. */
void paintKnob (juce::Graphics& g, const KnobSpec& k, float angle, float glow, bool rose)
{
    const juce::Point<float> c { k.cx, k.cy };
    g.setColour (juce::Colour (0xff222326));
    g.fillEllipse (R (k.r * 2.0f + 2.0f, k.r * 2.0f + 2.0f).withCentre (c.translated (0.8f, 1.6f)));
    if (glow > 0.0f)
    {
        g.setColour (theme::amber.withAlpha (0.75f * glow));
        g.fillEllipse (R (k.r * 2.0f + 5.0f, k.r * 2.0f + 5.0f).withCentre (c));
    }
    const auto key = theme::keyTop.withMultipliedSaturation (1.6f);
    const auto skirtTop = rose ? key.brighter (0.3f) : juce::Colour (0xff585b62);
    const auto skirtBot = rose ? key.darker (0.5f) : juce::Colour (0xff2c2e32);
    const auto capTop = rose ? key.brighter (0.55f) : juce::Colour (0xff6d7178);
    const auto capBot = rose ? key.darker (0.1f) : juce::Colour (0xff3a3d43);
    g.setGradientFill (juce::ColourGradient (skirtTop, c.x, c.y - k.r, skirtBot, c.x, c.y + k.r, false));
    g.fillEllipse (R (k.r * 2.0f, k.r * 2.0f).withCentre (c));
    g.setGradientFill (juce::ColourGradient (capTop, c.x - k.r * 0.5f, c.y - k.r * 0.5f,
                                             capBot, c.x + k.r * 0.5f, c.y + k.r * 0.5f, false));
    g.fillEllipse (R (k.r * 1.44f, k.r * 1.44f).withCentre (c));
    const auto dir = juce::Point<float> (std::sin (angle), -std::cos (angle));
    g.setColour (juce::Colour (0xffeeeae2));
    g.drawLine ({ c + dir * (k.r * 0.15f), c + dir * (k.r * 0.8f) }, juce::jmax (1.2f, k.r * 0.13f));
}
} // namespace

//==============================================================================
EngineView::EngineView (FM1CeptionProcessor& p, int slot)
    : proc (p), index (slot)
{
    lcdRaw.resize ((size_t) (FM1E_LCD_W * FM1E_LCD_H));
    setOpaque (true);
    setRepaintsOnMouseActivity (false);
}

EngineView::~EngineView()
{
    releaseAll();
}

void EngineView::setEngine (std::shared_ptr<EngineHandle> h)
{
    if (h == handle)
        return;
    releaseAll();
    handle = std::move (h);
    lcdPrev.clear();
    status = {};
    hasFirmware = false;
    repaint();
}

void EngineView::releaseAll()
{
    for (int id = 0; id < (int) down.size(); ++id)
    {
        latched[(size_t) id] = false;
        press (id, false);
    }
    if (grab && grab->hit.kind == Hit::level)
        proc.masterParam (index).endChangeGesture();
    grab.reset();
}

bool EngineView::isBlank() const
{
    return handle == nullptr || ! hasFirmware;
}

bool EngineView::popoverShown() const
{
    return (toggleOpen || shiftHeld) && ! isBlank();
}

void EngineView::setShiftHeld (bool held)
{
    if (held == shiftHeld)
        return;
    shiftHeld = held;
    repaintDesign (kRows);
}

void EngineView::setSelected (bool isSelected)
{
    if (isSelected == selectedMark)
        return;
    selectedMark = isSelected;
    repaintDesign (kStrip);
}

void EngineView::setSoftKnobRow (int row)
{
    prefs::setSoftKnobRow (firmware, row);
    softRow = firmware.isEmpty() ? -1 : juce::jlimit (-1, FM1E_LCD_H - 1, row);
    prefsRevision = prefs::revision().load();
    repaintDesign (kScreen);
}

int EngineView::lcdRowAt (juce::Point<float> d) const
{
    if (! kScreen.contains (d))
        return -1;
    return juce::jlimit (0, FM1E_LCD_H - 1, (int) ((d.y - kScreen.getY()) * (float) FM1E_LCD_H / kScreen.getHeight()));
}

//==============================================================================
juce::AffineTransform EngineView::designToLocal() const
{
    const float s = juce::jmin ((float) getWidth() / kW, (float) getHeight() / kH);
    return juce::AffineTransform::scale (s).translated (((float) getWidth() - kW * s) * 0.5f,
                                                        ((float) getHeight() - kH * s) * 0.5f);
}

juce::Point<float> EngineView::toDesign (juce::Point<float> p) const
{
    return p.transformedBy (designToLocal().inverted());
}

void EngineView::repaintDesign (R r)
{
    repaint (r.transformedBy (designToLocal()).getSmallestIntegerContainer().expanded (1));
}

EngineView::Hit EngineView::hitAt (juce::Point<float> d) const
{
    if (popoverShown())
    {
        for (int i = 0; i < 12; ++i)
            if (panelButton (i).contains (d))
                return { Hit::button, 2 + i };
        if (kPopover.contains (d))
            return { Hit::popoverBack };
    }
    if (kToggle.expanded (2.0f).contains (d) && ! isBlank())
        return { Hit::toggle };
    if (kMenu.contains (d))
        return { Hit::menu };
    if (kMute.expanded (1.0f, 4.0f).contains (d))
        return { Hit::mute };
    if (kSolo.expanded (1.0f, 4.0f).contains (d))
        return { Hit::solo };
    if (kLevel.contains (d))
        return { Hit::level };
    if (kStrip.contains (d))
        return { Hit::strip };
    if (isBlank() && kLoad.contains (d))
        return { Hit::loadButton };
    if (kScreen.contains (d))
    {
        if (! isBlank() && softRow >= 0 && std::abs (lcdRowAt (d) - softRow) <= kSoftBand)
            return { Hit::softKnob, juce::jlimit (0, 3, (int) (d.x * 4.0f / kScreen.getWidth())) };
        return { Hit::screen };
    }

    for (int i = 0; i < kNumKnobs; ++i)
        if (d.getDistanceFrom ({ kKnobs[i].cx, kKnobs[i].cy }) <= kKnobs[i].r + 5.0f)
            return { Hit::knob, i };
    for (const auto& b : kOct)
        if (b.r.contains (d))
            return { Hit::button, b.id };
    for (const auto& b : kBlacks)
        if (blackKey (b[1]).contains (d))
            return { Hit::key, kFirstKey + b[0] };
    for (int i = 0; i < 16; ++i)
        if (whiteKey (i).contains (d))
            return { Hit::key, kFirstKey + kWhites[i] };
    return {};
}

//==============================================================================
void EngineView::press (int id, bool isDown)
{
    if (! juce::isPositiveAndBelow (id, (int) down.size()) || down[(size_t) id] == isDown)
        return;
    down[(size_t) id] = isDown;
    if (handle != nullptr)
        fm1e_key (handle->get(), (juce::uint8) id, isDown);
    repaintDesign (kRows);
}

void EngineView::turn (int knob, int n)
{
    if (n == 0 || handle == nullptr)
        return;
    clicks[(size_t) knob] += n;
    glow[(size_t) knob] = 10;
    fm1e_enc (handle->get(), (juce::uint8) kKnobs[knob].enc, n);
    repaintDesign (kRows);
}

void EngineView::setMaster (float v)
{
    auto& p = proc.masterParam (index);
    p.setValueNotifyingHost (p.convertTo0to1 (juce::jlimit (0.0f, 1.0f, v)));
    repaintDesign (kLevel);
}

void EngineView::setMasterAt (float designX)
{
    setMaster (masterAt ((designX - kTrack.getX()) / kTrack.getWidth()));
}

//==============================================================================
void EngineView::refresh()
{
    bool rows = false;
    for (auto& gl : glow)
        if (gl > 0)
        {
            --gl;
            rows = true;
        }

    meter.push (proc.takeEnginePeaks (index));
    {
        const auto now = juce::Time::getMillisecondCounter();
        const float db = juce::jmax (meter.db[0], meter.db[1]);
        if (db >= holdDb)
        {
            holdDb = db;
            holdAt = now;
        }
        else if (now - holdAt > 1000)
        {
            holdDb = juce::jmax (-120.0f, holdDb - 1.2f);
        }

        const auto& p = proc.masterParam (index);
        auto quarters = [] (float pos) { return (int) std::round (pos * kTrack.getWidth() * 4.0f); };
        const int lvl = quarters (levelPos (db)), hold = quarters (levelPos (holdDb));
        const int caret = quarters (masterPos (p.convertFrom0to1 (p.getValue())));
        const bool m = proc.isMuted (index), s = proc.isSoloed (index), audible = proc.isAudible (index);
        if (m != drawnMute || s != drawnSolo || audible != drawnAudible)
        {
            drawnMute = m;
            drawnSolo = s;
            drawnAudible = audible;
            drawnLevel = -1;
            repaintDesign (kMute.getUnion (kSolo));
        }
        if (lvl != drawnLevel || hold != drawnHold || caret != drawnCaret)
        {
            drawnLevel = lvl;
            drawnHold = hold;
            drawnCaret = caret;
            repaintDesign (kLevel);
        }
    }

    if (handle != nullptr)
    {
        std::array<float, FM1E_LEDS> fractions {};
        Fm1eStatus st {};
        fm1e_view (handle->get(), lcdRaw.data(), fractions.data(), &st);

        char why[256] {};
        if (fm1e_restore_result (handle->get(), why, sizeof (why)) < 0)
            handle->post ("Started afresh: " + juce::String::fromUTF8 (why), 8000);

        for (size_t i = 0; i < fractions.size(); ++i)
        {
            const auto l = levelOf (fractions[i]);
            if (l != ledLevel[i])
            {
                ledLevel[i] = l;
                rows = true;
            }
        }

        const bool fw = handle->hasFirmware();
        bool screen = fw != hasFirmware || st.booted != status.booted || st.halted != status.halted;
        if (fw != hasFirmware)
            rows = true;
        hasFirmware = fw;
        status = st;

        // (a hidden tab's screen is converted when it is shown again)
        if (isVisible() && lcdRaw != lcdPrev)
        {
            const juce::Image::BitmapData px (lcd, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < FM1E_LCD_H; ++y)
            {
                const auto* src = lcdRaw.data() + y * FM1E_LCD_W;
                for (int x = 0; x < FM1E_LCD_W; ++x)
                {
                    const auto p = (juce::uint32) src[x];
                    auto* dst = reinterpret_cast<juce::PixelARGB*> (px.getPixelPointer (x, y));
                    dst->setARGB (255, (juce::uint8) ((p >> 11) * 255 / 31),
                                  (juce::uint8) (((p >> 5) & 63) * 255 / 63),
                                  (juce::uint8) ((p & 31) * 255 / 31));
                }
            }
            lcdPrev = lcdRaw;
            screen = true;
        }
        if (screen)
            repaintDesign (kScreen);
    }

    juce::String left, right;
    if (handle == nullptr)
    {
        left = "OFF";
        firmware.clear();
        softRow = -1;
    }
    else
    {
        const auto name = hasFirmware ? handle->firmwareName() : juce::String();
        if (name != firmware || prefs::revision().load() != prefsRevision)
        {
            firmware = name;
            prefsRevision = prefs::revision().load();
            softRow = prefs::softKnobRow (firmware);
            repaintDesign (kScreen);
        }
        left = name.isEmpty() ? juce::String ("EMPTY") : name.toUpperCase();
        const auto msg = handle->currentMessage();
        if (msg.isNotEmpty())
            right = msg;
        else if (hasFirmware)
        {
            right << proc.getClock (index) << " MHz";
            if (! status.jit)
                right << "  INTERP";
            if (status.halted)
                right << "  HALTED";
            else if (status.late > 0)
                right << "  " << (int) status.late << " LATE";
            else if (status.speed > 0.0f)
                right << "  " << juce::String (status.speed, 1) << "x";
        }
    }
    if (left != stripLeft || right != stripRight)
    {
        stripLeft = left;
        stripRight = right;
        repaintDesign (kStrip);
    }

    if (rows)
        repaintDesign (kRows);
}

//==============================================================================
void EngineView::paint (juce::Graphics& g)
{
    g.fillAll (theme::room);
    g.addTransform (designToLocal());
    paintStrip (g);
    paintScreen (g);
    paintRows (g);
}

void EngineView::paintStrip (juce::Graphics& g)
{
    g.setColour (theme::header);
    g.fillRect (kStrip);

    const auto num = R (3.0f, 4.0f, 15.0f, 12.0f);
    g.setColour (handle != nullptr && hasFirmware ? theme::amber : theme::tray);
    g.fillRoundedRectangle (num, 2.0f);
    g.setColour (handle != nullptr && hasFirmware ? theme::header : theme::dimSilk);
    g.setFont (theme::font (10.0f, true));
    g.drawText (juce::String (index + 1), num, juce::Justification::centred);

    const bool muted = proc.isMuted (index), soloed = proc.isSoloed (index);
    paintStripButton (g, kMute, "M", muted, theme::warn, ! muted && ! proc.isAudible (index));
    paintStripButton (g, kSolo, "S", soloed, theme::meterYellow, false);
    paintStripLevel (g);

    auto body = kStrip.withLeft (24.0f).withRight (kMute.getX() - 6.0f);
    const auto small = theme::font (10.0f);
    const float rightW = juce::jmin (body.getWidth() * 0.6f,
                                     juce::GlyphArrangement::getStringWidth (small, stripRight) + 2.0f);
    g.setFont (small);
    g.setColour (status.halted || status.late > 0 ? theme::warn : theme::dimSilk);
    g.drawText (stripRight, body.removeFromRight (rightW), juce::Justification::centredRight, true);
    g.setFont (theme::font (10.0f, true));
    g.setColour (theme::silk);
    g.drawText (stripLeft, body.withTrimmedRight (6.0f), juce::Justification::centredLeft, true);

    // the engine's menu: three short bars
    g.setColour (hover.y >= 0 && kMenu.contains (hover) ? theme::silk : theme::dimSilk);
    for (int i = 0; i < 3; ++i)
        g.fillRect (kMenu.getCentreX() - 5.0f, kMenu.getCentreY() - 4.0f + (float) i * 3.5f, 10.0f, 1.3f);

    if (selectedMark)
    {
        g.setColour (theme::amber);
        g.fillRect (kStrip.withTop (kStrip.getBottom() - 2.0f));
    }
}

void EngineView::paintStripButton (juce::Graphics& g, R r, const char* label, bool on, juce::Colour onColour, bool outlined)
{
    g.setColour (on ? onColour : theme::tray);
    g.fillRoundedRectangle (r, 2.0f);
    if (outlined)
    {
        // silenced by another engine's solo
        g.setColour (onColour.withAlpha (0.8f));
        g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);
    }
    g.setColour (on ? theme::header : outlined ? onColour : theme::dimSilk);
    g.setFont (theme::font (9.0f, true));
    g.drawText (label, r, juce::Justification::centred);
}

void EngineView::paintStripLevel (juce::Graphics& g)
{
    const auto t = kTrack;
    g.setColour (juce::Colour (0xff1c1d20));
    g.fillRoundedRectangle (t.expanded (1.0f), 1.5f);
    g.setColour (theme::meterOff);
    g.fillRect (t);

    // a solid bar, green into yellow into red along the track
    juce::ColourGradient grad (theme::meterGreen, t.getX(), 0.0f, theme::meterRed, t.getRight(), 0.0f, false);
    grad.addColour (0.62, theme::meterGreen);
    grad.addColour (0.82, theme::meterYellow);
    grad.addColour (0.94, theme::meterYellow.interpolatedWith (theme::meterRed, 0.5f));
    const bool audible = proc.isAudible (index);
    if (! audible)
        grad.multiplyOpacity (0.35f);

    const float db = juce::jmax (meter.db[0], meter.db[1]);
    const float lit = levelPos (db) * t.getWidth();
    if (lit > 0.0f)
    {
        g.setGradientFill (grad);
        g.fillRect (t.withWidth (lit));
    }
    if (holdDb > -kLevelRangeDb)
    {
        const float pos = levelPos (holdDb);
        const float hx = t.getX() + juce::jmax (1.5f, pos * t.getWidth()) - 1.5f;
        g.setColour (grad.getColourAtPosition (pos));
        g.fillRect (hx, t.getY(), 1.5f, t.getHeight());
    }

    // the MASTER pot: a caret pointing down at the track
    const auto& p = proc.masterParam (index);
    const float v = p.convertFrom0to1 (p.getValue());
    const float cx = t.getX() + masterPos (v) * t.getWidth();
    juce::Path caret;
    caret.addTriangle (cx - 3.5f, 3.0f, cx + 3.5f, 3.0f, cx, 8.0f);
    g.setColour (v > 0.0f ? juce::Colours::white : theme::dimSilk);
    g.fillPath (caret);
}

void EngineView::paintScreen (juce::Graphics& g)
{
    if (! isBlank())
    {
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImage (lcd, kScreen);
        if (status.halted || ! status.booted)
        {
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRect (kScreen);
            g.setColour (status.halted ? theme::warn : theme::silk);
            g.setFont (theme::font (16.0f, true));
            g.drawFittedText (status.halted ? "The firmware stopped:\nan instruction the emulator does not know"
                                            : "Booting...",
                              kScreen.reduced (30.0f).toNearestInt(), juce::Justification::centred, 3);
        }
        paintSoftKnobs (g);
    }
    else
    {
        g.setColour (theme::bezel);
        g.fillRect (kScreen);
        g.setColour (theme::tray);
        g.setFont (theme::font (64.0f, true));
        g.drawText ("FM-1", kScreen.withTrimmedBottom (160.0f), juce::Justification::centred);

        if (handle != nullptr)
        {
            const bool over = kLoad.contains (hover);
            g.setColour (over ? theme::amber.withAlpha (0.2f) : theme::tray);
            g.fillRoundedRectangle (kLoad, 6.0f);
            g.setColour (theme::amber);
            g.drawRoundedRectangle (kLoad, 6.0f, 1.5f);
            g.setFont (theme::font (14.0f, true));
            g.drawText ("LOAD FIRMWARE", kLoad, juce::Justification::centred);
            g.setColour (theme::dimSilk);
            g.setFont (theme::font (11.0f));
            g.drawText ("or drop a .fwsc / .fm1save here", kLoad.translated (0.0f, 48.0f).expanded (60.0f, 0.0f),
                        juce::Justification::centred);
        }
    }
    if (dropHover)
    {
        g.setColour (theme::amber);
        g.drawRect (kScreen.reduced (1.0f), 3.0f);
    }
}

void EngineView::paintSoftKnobs (juce::Graphics& g)
{
    if (softRow < 0)
        return;
    const auto h = hitAt (hover);
    const bool turning = grab && grab->hit.kind == Hit::softKnob;
    const int active = turning ? grab->hit.id : h.kind == Hit::softKnob ? h.id : -1;
    if (active < 0)
        return;

    // the band, split in four, while the pointer is in it
    const float rowH = kScreen.getHeight() / (float) FM1E_LCD_H;
    const float y0 = kScreen.getY() + (float) juce::jmax (0, softRow - kSoftBand) * rowH;
    const float y1 = kScreen.getY() + (float) juce::jmin ((int) FM1E_LCD_H, softRow + kSoftBand + 1) * rowH;
    const float w = kScreen.getWidth() / 4.0f;
    for (int i = 0; i < 4; ++i)
    {
        const R r (kScreen.getX() + (float) i * w, y0, w, y1 - y0);
        g.setColour (theme::amber.withAlpha (i == active ? 0.16f : 0.05f));
        g.fillRect (r.reduced (2.0f));
        g.setColour (theme::amber.withAlpha (i == active ? 0.8f : 0.3f));
        g.drawRect (r.reduced (2.0f), 1.0f);
    }
}

void EngineView::paintButton (juce::Graphics& g, R r, const juce::String& label, int id, float labelSize)
{
    const bool isDown = down[(size_t) id];
    const float led = brightness (ledLevel[(size_t) juce::jmin (id, FM1E_LEDS - 1)]);
    const auto tint = id == kPlay ? theme::green : theme::amber;
    const float off = isDown ? 1.5f : 0.0f;

    g.setColour (juce::Colour (0xff1c1d20));
    g.fillRoundedRectangle (r.translated (0.6f, 2.0f), 4.0f);
    const auto top = theme::btnTop.interpolatedWith (tint, led * 0.85f);
    const auto bot = theme::btnBot.interpolatedWith (tint, led * 0.7f);
    const auto face = r.translated (0.0f, off);
    g.setGradientFill (juce::ColourGradient (top, face.getX(), face.getY(), bot, face.getX(), face.getBottom(), false));
    g.fillRoundedRectangle (face, 4.0f);
    if (latched[(size_t) id])
    {
        g.setColour (tint);
        g.drawRoundedRectangle (face.reduced (0.5f), 4.0f, 1.2f);
    }
    g.setColour (led > 0.5f ? juce::Colour (0xff2a1a03) : theme::btnInk);
    g.setFont (theme::font (labelSize, true));
    g.drawText (label, face, juce::Justification::centred);
}

void EngineView::paintRows (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (theme::bodyTop, 0.0f, kRows.getY(), theme::bodyBot, 0.0f, kRows.getBottom(), false));
    g.fillRect (kRows);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRect (kRows.withHeight (1.0f).translated (0.0f, 60.0f).withTrimmedLeft (8.0f).withTrimmedRight (8.0f));

    for (int i = 0; i < kNumKnobs; ++i)
    {
        const auto& k = kKnobs[i];
        const float angle = juce::degreesToRadians ((float) clicks[(size_t) i] * 18.0f);
        paintKnob (g, k, angle, juce::jmin (1.0f, (float) glow[(size_t) i] / 10.0f), i == kPresetsKnob);
        if (i < 4)
            continue;
        g.setColour (theme::silk);
        g.setFont (theme::font (9.5f, true));
        g.drawText (k.label, R (k.cx - 30.0f, k.cy + k.r + 3.0f, 60.0f, 11.5f), juce::Justification::centred);
    }

    for (const auto& b : kOct)
        paintButton (g, b.r, b.label, b.id, 9.5f);

    paintKeyboard (g);
    paintToggle (g);
    if (popoverShown())
        paintPopover (g);
}

void EngineView::paintKeyboard (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff27272a));
    g.fillRoundedRectangle (kKeys.expanded (3.0f), 4.0f);

    auto keyColour = [this] (int id, bool black)
    {
        const float heat = down[(size_t) id] ? 0.6f : brightness (ledLevel[(size_t) id]) * 0.6f;
        const auto base = black ? juce::Colour (0xff2a2425) : theme::keyTop;
        return base.interpolatedWith (theme::amber, heat);
    };

    for (int i = 0; i < 16; ++i)
    {
        const int id = kFirstKey + kWhites[i];
        const auto r = whiteKey (i).reduced (0.5f, 0.0f);
        g.setGradientFill (juce::ColourGradient (keyColour (id, false), r.getX(), r.getY(),
                                                 theme::keyBot, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 1.5f);
    }
    for (const auto& b : kBlacks)
    {
        const int id = kFirstKey + b[0];
        const auto r = blackKey (b[1]);
        g.setColour (juce::Colour (0xff141415));
        g.fillRoundedRectangle (r.expanded (0.6f, 0.0f).withTrimmedTop (-0.5f), 1.5f);
        g.setColour (keyColour (id, true));
        g.fillRoundedRectangle (r.reduced (0.6f, 0.0f).withTrimmedBottom (1.0f), 1.2f);
    }
}

void EngineView::paintToggle (juce::Graphics& g)
{
    if (isBlank())
        return;
    const bool on = popoverShown();
    g.setColour (on ? theme::amber : theme::tray);
    g.fillRoundedRectangle (kToggle, 4.0f);
    g.setColour (on ? theme::header : theme::silk);
    // a 3 x 2 grid of button caps
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 3; ++c)
            g.fillRoundedRectangle (kToggle.getX() + 5.0f + (float) c * 5.2f, kToggle.getY() + 8.0f + (float) r * 6.0f,
                                    3.6f, 4.0f, 0.8f);

    // a panel LED lit while the popover is closed
    bool lit = false;
    for (int id = 2; id <= 13; ++id)
        lit = lit || ledLevel[(size_t) id] > 8 || latched[(size_t) id];
    if (lit && ! on)
    {
        g.setColour (theme::amber);
        g.fillEllipse (kToggle.getRight() - 6.0f, kToggle.getY() + 2.0f, 4.0f, 4.0f);
    }
}

void EngineView::paintPopover (juce::Graphics& g)
{
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (kPopover.translated (0.0f, 3.0f), 7.0f);
    g.setColour (theme::tray);
    g.fillRoundedRectangle (kPopover, 7.0f);
    g.setColour (theme::bodyTop);
    g.drawRoundedRectangle (kPopover.reduced (0.5f), 7.0f, 1.0f);
    for (int i = 0; i < 12; ++i)
        paintButton (g, panelButton (i), kPanelLabels[i], 2 + i, 11.0f);
}

//==============================================================================
void EngineView::mouseDown (const juce::MouseEvent& e)
{
    const auto d = toDesign (e.position);
    const auto h = hitAt (d);

    if (onSelect != nullptr)
        onSelect (index);

    if (e.mods.isPopupMenu())
    {
        if (h.kind == Hit::button && handle != nullptr)
        {
            auto& l = latched[(size_t) h.id];
            l = ! l;
            press (h.id, l);
        }
        else if (onMenuRequest != nullptr)
        {
            onMenuRequest (index, isBlank() ? -1 : lcdRowAt (d));
        }
        return;
    }

    Grab g;
    g.hit = h;
    g.lastY = e.position.y;
    switch (h.kind)
    {
        case Hit::menu:
            if (onMenuRequest != nullptr)
                onMenuRequest (index, -1);
            return;
        case Hit::loadButton:
            if (onLoadRequest != nullptr)
                onLoadRequest (index);
            return;
        case Hit::toggle:
            toggleOpen = ! toggleOpen;
            repaintDesign (kRows);
            return;
        case Hit::button:
            if (latched[(size_t) h.id])
            {
                latched[(size_t) h.id] = false;
                press (h.id, false);
                return;
            }
            press (h.id, true);
            g.held = h.id;
            break;
        case Hit::key:
            press (h.id, true);
            g.held = h.id;
            break;
        case Hit::mute:
            proc.setMuted (index, ! proc.isMuted (index));
            repaintDesign (kStrip);
            return;
        case Hit::solo:
            if (e.mods.isCommandDown() || e.mods.isAltDown())
                proc.soloExclusive (index);
            else
                proc.setSoloed (index, ! proc.isSoloed (index));
            repaintDesign (kStrip);
            return;
        case Hit::level:
            proc.masterParam (index).beginChangeGesture();
            setMasterAt (d.x);
            break;
        case Hit::softKnob:
            repaintDesign (kScreen);
            break;
        case Hit::none: case Hit::strip: case Hit::screen: case Hit::popoverBack: case Hit::knob:
            break;
    }
    grab = g;
}

void EngineView::mouseDrag (const juce::MouseEvent& e)
{
    if (! grab)
        return;
    auto& g = *grab;
    const float y = e.position.y;

    if (g.hit.kind == Hit::key && g.held >= 0)
    {
        // a drag across the keys is a glissando
        const auto h = hitAt (toDesign (e.position));
        if (h.kind == Hit::key && h.id != g.held)
        {
            press (g.held, false);
            g.held = h.id;
            press (g.held, true);
        }
    }
    else if (g.hit.kind == Hit::level)
    {
        setMasterAt (toDesign (e.position).x);
    }
    else if (g.hit.kind == Hit::knob || g.hit.kind == Hit::softKnob)
    {
        // 8 px of drag a click (a soft knob turns KNOB1..4, ids 0..3 either way)
        g.acc += g.lastY - y;
        const int n = (int) (g.acc / 8.0f);
        g.acc -= (float) n * 8.0f;
        turn (g.hit.id, n);
    }
    g.lastY = y;
}

void EngineView::mouseUp (const juce::MouseEvent&)
{
    if (! grab)
        return;
    if (grab->held >= 0 && ! latched[(size_t) grab->held])
        press (grab->held, false);
    if (grab->hit.kind == Hit::level)
        proc.masterParam (index).endChangeGesture();
    const bool soft = grab->hit.kind == Hit::softKnob;
    grab.reset();
    if (soft)
        repaintDesign (kScreen);
}

void EngineView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto h = hitAt (toDesign (e.position));
    if (h.kind == Hit::level)
    {
        auto& p = proc.masterParam (index);
        p.beginChangeGesture();
        setMaster (FM1CeptionProcessor::kDefaultMaster);
        p.endChangeGesture();
    }
    else if (h.kind == Hit::mute || h.kind == Hit::solo)
    {
        // the second click of a double-click
        mouseDown (e);
    }
}

void EngineView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto h = hitAt (toDesign (e.position));
    const float delta = w.isReversed ? -w.deltaY : w.deltaY;
    if (h.kind == Hit::level)
    {
        // 12 dB a wheel step, on the caret's scale
        auto& p = proc.masterParam (index);
        const float pos = masterPos (p.convertFrom0to1 (p.getValue())) + delta * 12.0f / kLevelRangeDb;
        p.beginChangeGesture();
        setMaster (masterAt (juce::jlimit (0.0f, 1.0f, pos)));
        p.endChangeGesture();
        return;
    }
    if (h.kind != Hit::knob && h.kind != Hit::softKnob)
        return;
    wheelAcc += delta;
    constexpr float kStep = 0.08f;
    const int n = (int) (wheelAcc / kStep);
    wheelAcc -= (float) n * kStep;
    turn (h.id, n);
}

void EngineView::mouseMove (const juce::MouseEvent& e)
{
    const auto d = toDesign (e.position);
    const bool wasOverLoad = kLoad.contains (hover), wasOverMenu = kMenu.contains (hover);
    const auto wasSoft = hitAt (hover);
    hover = d;
    if (kLoad.contains (d) != wasOverLoad)
        repaintDesign (kLoad);
    if (kMenu.contains (d) != wasOverMenu)
        repaintDesign (kMenu);
    const auto soft = hitAt (d);
    if ((soft.kind == Hit::softKnob) != (wasSoft.kind == Hit::softKnob) || (soft.kind == Hit::softKnob && soft.id != wasSoft.id))
        repaintDesign (kScreen);
}

void EngineView::mouseExit (const juce::MouseEvent&)
{
    const bool wasSoft = hitAt (hover).kind == Hit::softKnob;
    hover = { -1.0f, -1.0f };
    repaintDesign (kLoad);
    repaintDesign (kMenu);
    if (wasSoft)
        repaintDesign (kScreen);
}

juce::String EngineView::getTooltip()
{
    const auto h = hitAt (hover);
    switch (h.kind)
    {
        case Hit::menu:       return "Engine " + juce::String (index + 1) + " menu";
        case Hit::strip:      return "Right-click for engine " + juce::String (index + 1) + "'s menu";
        case Hit::screen:     return ! isBlank() && softRow < 0 ? "Right-click to set where this firmware draws its knobs" : juce::String();
        case Hit::softKnob:   return "KNOB" + juce::String (h.id + 1) + " on the screen  (drag or scroll)";
        case Hit::loadButton: return "Open a .fwsc firmware package or a shared .fm1save setup";
        case Hit::toggle:     return "Panel buttons (or hold Shift over the engine)";
        case Hit::button:     return h.id >= 2 ? "Right-click to latch it down" : juce::String();
        case Hit::knob:       return juce::String (kKnobs[h.id].label) + "  (drag or scroll)";
        case Hit::mute:       return "Mute engine " + juce::String (index + 1);
        case Hit::solo:       return "Solo engine " + juce::String (index + 1) + "  (Cmd- or Alt-click: solo it alone)";
        case Hit::level:
            return "Engine " + juce::String (index + 1) + " level; the caret is its MASTER "
                   + proc.masterParam (index).getCurrentValueAsText() + "  (drag or scroll; double-click to reset)";
        case Hit::none: case Hit::key: case Hit::popoverBack:
            break;
    }
    return {};
}

//==============================================================================
bool EngineView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("fwsc;fm1save;json;fm1ception"))
            return handle != nullptr;
    return false;
}

void EngineView::fileDragEnter (const juce::StringArray&, int, int)
{
    dropHover = true;
    repaintDesign (kScreen);
}

void EngineView::fileDragExit (const juce::StringArray&)
{
    dropHover = false;
    repaintDesign (kScreen);
}

void EngineView::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    repaintDesign (kScreen);
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("fwsc;fm1save;json;fm1ception"))
        {
            proc.loadFile (index, juce::File (f));
            return;
        }
}
} // namespace fm1c
