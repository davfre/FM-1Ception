#include "EngineView.h"
#include "FirmwarePrefs.h"
#include "Theme.h"

namespace fm1c
{
namespace
{
using R = juce::Rectangle<float>;

constexpr float kStripH = 20.0f;
// the screen, and what is drawn on it while it is blank, in the screen's own space (the hardware
// layout draws that space at half size in the panel's bezel)
const R kScreen { 0.0f, 20.0f, 480.0f, 480.0f };
const R kLoad   { 150.0f, 240.0f, 180.0f, 40.0f };
const R kBrowse { 150.0f, 290.0f, 180.0f, 32.0f };
const R kRows   { 0.0f, 500.0f, 480.0f, 128.0f };
const R kToggle { 452.0f, 596.0f, 24.0f, 26.0f };
const R kPopover { 4.0f, 502.0f, 472.0f, 90.0f };
const R kKeys   { 248.0f, 568.0f, 196.0f, 54.0f };

// the strip's M, S, level (a solid bar with the MASTER caret over it) and menu, from its right end
constexpr float kTrackW = 108.0f;
R stripMute (float w)  { return { w - 178.0f, 4.0f, 15.0f, 12.0f }; }
R stripSolo (float w)  { return { w - 160.0f, 4.0f, 15.0f, 12.0f }; }
R stripLevel (float w) { return { w - 142.0f, 0.0f, 116.0f, kStripH }; }
R stripTrack (float w) { return { w - 138.0f, 10.0f, kTrackW, 5.0f }; }
R stripMenu (float w)  { return { w - 24.0f, 0.0f, 24.0f, kStripH }; }

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

// the sections View's options add under the rows: the edit buttons' row, the large keyboard
constexpr float kEditH = 94.0f, kBigKeysH = 120.0f;

R panelButton (R area, int i)   // i: 0..11, matrix id 2 + i
{
    const float bw = (area.getWidth() - 16.0f - 5.0f * 6.0f) / 6.0f, bh = 34.0f;
    return { area.getX() + 8.0f + (float) (i % 6) * (bw + 6.0f),
             area.getY() + 8.0f + (float) (i / 6) * (bh + 6.0f), bw, bh };
}

// F3..G5: 16 white keys, 11 black over the gaps
constexpr int kWhites[16] { 0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26 };
constexpr int kBlacks[11][2] { { 1, 0 }, { 3, 1 }, { 5, 2 }, { 8, 4 }, { 10, 5 }, { 13, 7 },
                               { 15, 8 }, { 17, 9 }, { 20, 11 }, { 22, 12 }, { 25, 14 } };
constexpr int kFirstKey = 14;

// The hardware layout: fm1-live's front panel (crates/fm1-live/src/panel.rs),
// 1280 x 820, drawn at half size under the strip
constexpr float kPanelW = 1280.0f, kPanelH = 820.0f, kPanelScale = 0.5f;
const R kPanelLcd { 370.0f, 42.0f, 480.0f, 480.0f };

struct PanelKnob
{
    float cx, cy, r;
    const char* label;
    int knob;   // into kKnobs
};
const PanelKnob kPanelKnobs[7] {
    {  250.0f, 135.0f, 44.0f, "SELECT", 4 },
    {  110.0f, 295.0f, 44.0f, "PRESETS", 5 },
    {  250.0f, 295.0f, 44.0f, "ALGORITHM", 6 },
    {  935.0f, 120.0f, 36.0f, "KNOB1", 0 },
    { 1027.0f, 120.0f, 36.0f, "KNOB2", 1 },
    { 1119.0f, 120.0f, 36.0f, "KNOB3", 2 },
    { 1211.0f, 120.0f, 36.0f, "KNOB4", 3 },
};
const juce::Point<float> kPot { 110.0f, 135.0f };
constexpr float kPotR = 44.0f;

R panelButtonRect (int id)   // matrix id 0..13: OCT-, OCT+, then FX..GLO, HOME..REC
{
    if (id < 2)
        return { 62.0f + (float) id * 124.0f, 398.0f, 112.0f, 44.0f };
    const int i = id - 2;
    return { 906.0f + (float) (i % 6) * 58.0f, 232.0f + (float) (i / 6) * 76.0f, 50.0f, 64.0f };
}

R panelWhite (int i) { return { 61.0f + (float) i * 73.0f, 680.0f, 62.0f, 106.0f }; }
R panelBlack (int after) { return { 61.0f + (float) (after + 1) * 73.0f - 5.0f - 31.0f, 574.0f, 62.0f, 94.0f }; }

R whiteKey (R keys, int i)
{
    const float kw = keys.getWidth() / 16.0f;
    return { keys.getX() + (float) i * kw, keys.getY(), kw, keys.getHeight() };
}

R blackKey (R keys, int after)
{
    const float kw = keys.getWidth() / 16.0f, bw = kw * 0.62f;
    const float x = keys.getX() + (float) (after + 1) * kw - bw * 0.5f;
    return { x, keys.getY(), bw, keys.getHeight() * 0.58f };
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
    : proc (p), index (slot), options (p.getViewOptions())
{
    lcdRaw.resize ((size_t) (FM1E_LCD_W * FM1E_LCD_H));
    setOpaque (true);
    setRepaintsOnMouseActivity (false);
}

float EngineView::widthFor (bool asFrontPanel)
{
    return asFrontPanel ? kPanelW * kPanelScale : kW;
}

float EngineView::heightFor (int viewOptions, bool asFrontPanel)
{
    using P = FM1CeptionProcessor;
    if (asFrontPanel)
        return kStripH + kPanelH * kPanelScale;
    return kScreen.getBottom() + ((viewOptions & P::screensOnly) != 0 ? 0.0f : kRows.getHeight())
         + ((viewOptions & P::editButtons) != 0 ? kEditH : 0.0f)
         + ((viewOptions & P::largeKeyboard) != 0 ? kBigKeysH : 0.0f);
}

void EngineView::setViewOptions (int viewOptions)
{
    if (viewOptions == options)
        return;
    releaseAll();
    options = viewOptions;
    toggleOpen = false;
    repaint();
}

void EngineView::setHardware (bool asFrontPanel)
{
    if (asFrontPanel == hardware)
        return;
    releaseAll();
    hardware = asFrontPanel;
    toggleOpen = false;
    repaint();
}

R EngineView::muteArea() const  { return stripMute (designW()); }
R EngineView::soloArea() const  { return stripSolo (designW()); }
R EngineView::levelArea() const { return stripLevel (designW()); }
R EngineView::trackArea() const { return stripTrack (designW()); }
R EngineView::menuArea() const  { return stripMenu (designW()); }

juce::AffineTransform EngineView::panelToDesign()
{
    return juce::AffineTransform::scale (kPanelScale).translated (0.0f, kStripH);
}

juce::AffineTransform EngineView::screenToDesign() const
{
    if (! hardware)
        return {};
    const auto inBezel = kPanelLcd.transformedBy (panelToDesign());
    return juce::AffineTransform::translation (-kScreen.getX(), -kScreen.getY())
        .scaled (inBezel.getWidth() / kScreen.getWidth())
        .translated (inBezel.getX(), inBezel.getY());
}

juce::Point<float> EngineView::toScreen (juce::Point<float> d) const
{
    return hardware ? d.transformedBy (screenToDesign().inverted()) : d;
}

void EngineView::repaintScreen (R r)
{
    repaintDesign (r.transformedBy (screenToDesign()));
}

void EngineView::repaintPot()
{
    if (hardware)
        repaintDesign (R (kPotR * 2.0f + 12.0f, kPotR * 2.0f + 12.0f).withCentre (kPot).transformedBy (panelToDesign()));
}

R EngineView::rowsArea() const
{
    if (hardware)
        return {};
    return kRows.withHeight (shows (FM1CeptionProcessor::screensOnly) ? 0.0f : kRows.getHeight());
}

R EngineView::editArea() const
{
    return { 0.0f, rowsArea().getBottom(), kW, shows (FM1CeptionProcessor::editButtons) ? kEditH : 0.0f };
}

R EngineView::bigKeysArea() const
{
    return { 0.0f, editArea().getBottom(), kW, shows (FM1CeptionProcessor::largeKeyboard) ? kBigKeysH : 0.0f };
}

R EngineView::belowScreen() const
{
    // the front panel's controls are all round the screen
    if (hardware)
        return { 0.0f, kStripH, designW(), kPanelH * kPanelScale };
    return { 0.0f, kScreen.getBottom(), kW, heightFor (options) - kScreen.getBottom() };
}

R EngineView::panelArea() const
{
    return shows (FM1CeptionProcessor::editButtons) ? editArea().reduced (4.0f, 2.0f) : kPopover;
}

R EngineView::keysArea() const
{
    // the large keys: edge to edge but for a pixel, so every key is there to click
    return shows (FM1CeptionProcessor::largeKeyboard) ? bigKeysArea().reduced (1.0f, 4.0f) : kKeys;
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
    return (toggleOpen || shiftHeld) && ! isBlank() && ! rowsArea().isEmpty()
        && ! shows (FM1CeptionProcessor::editButtons);
}

void EngineView::setShiftHeld (bool held)
{
    if (held == shiftHeld)
        return;
    shiftHeld = held;
    repaintDesign (belowScreen());
}

void EngineView::setSelected (bool isSelected)
{
    if (isSelected == selectedMark)
        return;
    selectedMark = isSelected;
    repaintDesign (stripArea());
}

void EngineView::setSoftKnobRow (int row)
{
    prefs::setSoftKnobRow (firmware, row);
    softRow = firmware.isEmpty() ? -1 : juce::jlimit (-1, FM1E_LCD_H - 1, row);
    prefsRevision = prefs::revision().load();
    repaintScreen (kScreen);
}

int EngineView::lcdRowAt (juce::Point<float> d) const
{
    const auto s = toScreen (d);
    if (! kScreen.contains (s))
        return -1;
    return juce::jlimit (0, FM1E_LCD_H - 1, (int) ((s.y - kScreen.getY()) * (float) FM1E_LCD_H / kScreen.getHeight()));
}

//==============================================================================
juce::AffineTransform EngineView::designToLocal() const
{
    const float w = designW(), h = heightFor (options, hardware);
    const float s = juce::jmin ((float) getWidth() / w, (float) getHeight() / h);
    return juce::AffineTransform::scale (s).translated (((float) getWidth() - w * s) * 0.5f,
                                                        ((float) getHeight() - h * s) * 0.5f);
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
    const bool rows = ! rowsArea().isEmpty(), edit = shows (FM1CeptionProcessor::editButtons);
    if (popoverShown() || (edit && ! isBlank() && editArea().contains (d)))
    {
        for (int i = 0; i < 12; ++i)
            if (panelButton (panelArea(), i).contains (d))
                return { Hit::button, 2 + i };
        if (panelArea().contains (d))
            return { Hit::popoverBack };
    }
    if (rows && ! edit && kToggle.expanded (2.0f).contains (d) && ! isBlank())
        return { Hit::toggle };
    if (menuArea().contains (d))
        return { Hit::menu };
    if (muteArea().expanded (1.0f, 4.0f).contains (d))
        return { Hit::mute };
    if (soloArea().expanded (1.0f, 4.0f).contains (d))
        return { Hit::solo };
    if (levelArea().contains (d))
        return { Hit::level };
    if (stripArea().contains (d))
        return { Hit::strip };
    const auto s = toScreen (d);
    if (isBlank() && kLoad.contains (s))
        return { Hit::loadButton };
    if (isBlank() && kBrowse.contains (s))
        return { Hit::browseButton };
    if (kScreen.contains (s))
    {
        if (! isBlank() && softRow >= 0 && std::abs (lcdRowAt (d) - softRow) <= kSoftBand)
            return { Hit::softKnob, juce::jlimit (0, 3, (int) (s.x * 4.0f / kScreen.getWidth())) };
        return { Hit::screen };
    }
    if (hardware)
        return hitPanel (d.transformedBy (panelToDesign().inverted()));

    if (rows)
    {
        for (int i = 0; i < kNumKnobs; ++i)
            if (d.getDistanceFrom ({ kKnobs[i].cx, kKnobs[i].cy }) <= kKnobs[i].r + 5.0f)
                return { Hit::knob, i };
        for (const auto& b : kOct)
            if (b.r.contains (d))
                return { Hit::button, b.id };
    }
    if (rows || shows (FM1CeptionProcessor::largeKeyboard))
    {
        const auto keys = keysArea();
        for (const auto& b : kBlacks)
            if (blackKey (keys, b[1]).contains (d))
                return { Hit::key, kFirstKey + b[0] };
        for (int i = 0; i < 16; ++i)
            if (whiteKey (keys, i).contains (d))
                return { Hit::key, kFirstKey + kWhites[i] };
    }
    return {};
}

EngineView::Hit EngineView::hitPanel (juce::Point<float> p) const
{
    for (const auto& k : kPanelKnobs)
        if (p.getDistanceFrom ({ k.cx, k.cy }) <= k.r + 6.0f)
            return { Hit::knob, k.knob };
    if (p.getDistanceFrom (kPot) <= kPotR + 6.0f)
        return { Hit::pot };
    for (int id = 0; id < 14; ++id)
        if (panelButtonRect (id).contains (p))
            return { Hit::button, id };
    // the black keys first: they sit over the white row's gaps
    for (const auto& b : kBlacks)
        if (panelBlack (b[1]).contains (p))
            return { Hit::key, kFirstKey + b[0] };
    for (int i = 0; i < 16; ++i)
        if (panelWhite (i).contains (p))
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
    repaintDesign (belowScreen());
}

void EngineView::turn (int knob, int n)
{
    if (n == 0 || handle == nullptr)
        return;
    clicks[(size_t) knob] += n;
    glow[(size_t) knob] = 10;
    fm1e_enc (handle->get(), (juce::uint8) kKnobs[knob].enc, n);
    repaintDesign (belowScreen());
}

void EngineView::setMaster (float v)
{
    auto& p = proc.masterParam (index);
    p.setValueNotifyingHost (p.convertTo0to1 (juce::jlimit (0.0f, 1.0f, v)));
    repaintDesign (levelArea());
    repaintPot();
}

void EngineView::setMasterAt (float designX)
{
    setMaster (masterAt ((designX - trackArea().getX()) / kTrackW));
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
        auto quarters = [] (float pos) { return (int) std::round (pos * kTrackW * 4.0f); };
        const int lvl = quarters (levelPos (db)), hold = quarters (levelPos (holdDb));
        const int caret = quarters (masterPos (p.convertFrom0to1 (p.getValue())));
        const bool m = proc.isMuted (index), s = proc.isSoloed (index), audible = proc.isAudible (index);
        if (m != drawnMute || s != drawnSolo || audible != drawnAudible)
        {
            drawnMute = m;
            drawnSolo = s;
            drawnAudible = audible;
            drawnLevel = -1;
            repaintDesign (muteArea().getUnion (soloArea()));
        }
        if (caret != drawnCaret)
            repaintPot();
        if (lvl != drawnLevel || hold != drawnHold || caret != drawnCaret)
        {
            drawnLevel = lvl;
            drawnHold = hold;
            drawnCaret = caret;
            repaintDesign (levelArea());
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
            repaintScreen (kScreen);
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
            repaintScreen (kScreen);
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
        repaintDesign (stripArea());
    }

    if (rows)
        repaintDesign (belowScreen());
}

//==============================================================================
void EngineView::paint (juce::Graphics& g)
{
    g.fillAll (theme::room);
    g.addTransform (designToLocal());
    paintStrip (g);
    if (hardware)
    {
        paintPanel (g);
        juce::Graphics::ScopedSaveState s (g);
        g.addTransform (screenToDesign());
        paintScreen (g);
        return;
    }
    paintScreen (g);
    if (! rowsArea().isEmpty())
        paintRows (g);
    if (shows (FM1CeptionProcessor::editButtons))
        paintEditRow (g);
    if (shows (FM1CeptionProcessor::largeKeyboard))
        paintKeyboard (g);
    if (popoverShown())
        paintPopover (g);
}

void EngineView::paintStrip (juce::Graphics& g)
{
    g.setColour (theme::header);
    g.fillRect (stripArea());

    const auto num = R (3.0f, 4.0f, 15.0f, 12.0f);
    g.setColour (handle != nullptr && hasFirmware ? theme::amber : theme::tray);
    g.fillRoundedRectangle (num, 2.0f);
    g.setColour (handle != nullptr && hasFirmware ? theme::header : theme::dimSilk);
    g.setFont (theme::font (10.0f, true));
    g.drawText (juce::String (index + 1), num, juce::Justification::centred);

    const bool muted = proc.isMuted (index), soloed = proc.isSoloed (index);
    paintStripButton (g, muteArea(), "M", muted, theme::warn, ! muted && ! proc.isAudible (index));
    paintStripButton (g, soloArea(), "S", soloed, theme::meterYellow, false);
    paintStripLevel (g);

    auto body = stripArea().withLeft (24.0f).withRight (muteArea().getX() - 6.0f);
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
    g.setColour (hover.y >= 0 && menuArea().contains (hover) ? theme::silk : theme::dimSilk);
    for (int i = 0; i < 3; ++i)
        g.fillRect (menuArea().getCentreX() - 5.0f, menuArea().getCentreY() - 4.0f + (float) i * 3.5f, 10.0f, 1.3f);

    if (selectedMark)
    {
        g.setColour (theme::amber);
        g.fillRect (stripArea().withTop (kStripH - 2.0f));
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
    const auto t = trackArea();
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
            const auto at = toScreen (hover);
            const bool over = kLoad.contains (at);
            g.setColour (over ? theme::amber.withAlpha (0.2f) : theme::tray);
            g.fillRoundedRectangle (kLoad, 6.0f);
            g.setColour (theme::amber);
            g.drawRoundedRectangle (kLoad, 6.0f, 1.5f);
            g.setFont (theme::font (14.0f, true));
            g.drawText ("LOAD FIRMWARE", kLoad, juce::Justification::centred);

            const bool overBrowse = kBrowse.contains (at);
            g.setColour (overBrowse ? theme::amber.withAlpha (0.2f) : theme::tray);
            g.fillRoundedRectangle (kBrowse, 6.0f);
            g.setColour (theme::amber.withAlpha (overBrowse ? 1.0f : 0.6f));
            g.drawRoundedRectangle (kBrowse, 6.0f, 1.0f);
            g.setColour (overBrowse ? theme::amber : theme::silk);
            g.setFont (theme::font (12.0f, true));
            g.drawText ("OPEN BROWSER", kBrowse, juce::Justification::centred);

            g.setColour (theme::dimSilk);
            g.setFont (theme::font (11.0f));
            g.drawText ("or drop a .fwsc / .fm1save here", kBrowse.translated (0.0f, 42.0f).expanded (60.0f, 0.0f),
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

    if (! shows (FM1CeptionProcessor::largeKeyboard))
        paintKeyboard (g);
    if (! shows (FM1CeptionProcessor::editButtons))
        paintToggle (g);
}

void EngineView::paintEditRow (juce::Graphics& g)
{
    const auto area = editArea();
    g.setGradientFill (juce::ColourGradient (theme::bodyBot, 0.0f, area.getY(), theme::bodyBot.darker (0.15f), 0.0f, area.getBottom(), false));
    g.fillRect (area);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRect (area.withHeight (1.0f).withTrimmedLeft (8.0f).withTrimmedRight (8.0f));
    for (int i = 0; i < 12; ++i)
        paintButton (g, panelButton (panelArea(), i), kPanelLabels[i], 2 + i, 11.0f);
}

void EngineView::paintKeyboard (juce::Graphics& g)
{
    const auto keys = keysArea();
    g.setColour (juce::Colour (0xff27272a));
    if (shows (FM1CeptionProcessor::largeKeyboard))
        g.fillRect (bigKeysArea());
    else
        g.fillRoundedRectangle (keys.expanded (3.0f), 4.0f);

    auto keyColour = [this] (int id, bool black)
    {
        const float heat = down[(size_t) id] ? 0.6f : brightness (ledLevel[(size_t) id]) * 0.6f;
        const auto base = black ? juce::Colour (0xff2a2425) : theme::keyTop;
        return base.interpolatedWith (theme::amber, heat);
    };

    for (int i = 0; i < 16; ++i)
    {
        const int id = kFirstKey + kWhites[i];
        const auto r = whiteKey (keys, i).reduced (0.5f, 0.0f);
        g.setGradientFill (juce::ColourGradient (keyColour (id, false), r.getX(), r.getY(),
                                                 theme::keyBot, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 1.5f);
    }
    for (const auto& b : kBlacks)
    {
        const int id = kFirstKey + b[0];
        const auto r = blackKey (keys, b[1]);
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
        paintButton (g, panelButton (kPopover, i), kPanelLabels[i], 2 + i, 11.0f);
}

void EngineView::paintPanel (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState saved (g);
    g.addTransform (panelToDesign());

    auto fill = [&g] (R r, float radius, juce::uint32 top, juce::uint32 bot)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (top), 0.0f, r.getY(), juce::Colour (bot), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, radius);
    };
    g.setColour (theme::room);
    g.fillRect (R (kPanelW, kPanelH));
    fill ({ 14.0f, 14.0f, kPanelW - 28.0f, kPanelH - 28.0f }, 30.0f, 0xff5e6166, 0xff4a4d52);
    fill ({ 352.0f, 24.0f, 516.0f, 516.0f }, 22.0f, 0xff0e0e10, 0xff0b0b0c);
    g.setColour (theme::tray);
    g.fillRoundedRectangle ({ 50.0f, 388.0f, 260.0f, 64.0f }, 14.0f);
    g.fillRoundedRectangle ({ 896.0f, 222.0f, 360.0f, 160.0f }, 16.0f);
    fill ({ 34.0f, 562.0f, 1212.0f, 236.0f }, 26.0f, 0xff2f2f33, 0xff27272a);

    // the PLAY / REC pair outlined, as on the panel
    g.setColour (juce::Colour (0xff3c6b43));
    g.fillRoundedRectangle ({ 1134.0f, 302.0f, 112.0f, 72.0f }, 10.0f);
    g.setColour (theme::tray);
    g.fillRoundedRectangle ({ 1136.0f, 304.0f, 108.0f, 68.0f }, 9.0f);
    g.setColour (theme::dimSilk);
    g.setFont (theme::font (11.0f, true));
    g.drawText ("FM-1  FELUCCA", R (440.0f, 544.0f, 400.0f, 14.0f), juce::Justification::centred);

    auto label = [&g] (float cx, float top, const char* text)
    {
        g.setColour (theme::silk);
        g.setFont (theme::font (17.0f, true));
        g.drawText (text, R (cx - 90.0f, top - 2.0f, 180.0f, 18.0f), juce::Justification::centred);
    };
    for (const auto& k : kPanelKnobs)
    {
        label (k.cx, k.cy - k.r - 24.0f, k.label);
        const float angle = juce::degreesToRadians ((float) clicks[(size_t) k.knob] * 18.0f);
        paintKnob (g, { k.cx, k.cy, k.r, k.label, 0 }, angle, juce::jmin (1.0f, (float) glow[(size_t) k.knob] / 10.0f), false);
    }
    // MASTER sweeps 270 degrees, on the strip caret's scale
    const auto& master = proc.masterParam (index);
    const float pos = masterPos (master.convertFrom0to1 (master.getValue()));
    label (kPot.x, kPot.y - kPotR - 24.0f, "MASTER");
    paintKnob (g, { kPot.x, kPot.y, kPotR, "MASTER", 0 }, juce::degreesToRadians (-135.0f + 270.0f * pos), 0.0f, false);

    paintPanelButton (g, panelButtonRect (0), "OCT-", 0);
    paintPanelButton (g, panelButtonRect (1), "OCT+", 1);
    for (int i = 0; i < 12; ++i)
        paintPanelButton (g, panelButtonRect (2 + i), kPanelLabels[i], 2 + i);

    for (int i = 0; i < 16; ++i)
        paintPanelKey (g, panelWhite (i), kFirstKey + kWhites[i]);
    for (const auto& b : kBlacks)
        paintPanelKey (g, panelBlack (b[1]), kFirstKey + b[0]);
}

void EngineView::paintPanelButton (juce::Graphics& g, R r, const char* label, int id)
{
    const bool isDown = down[(size_t) id];
    const float led = brightness (ledLevel[(size_t) juce::jmin (id, FM1E_LEDS - 1)]);
    const auto tint = id == kPlay ? theme::green : theme::amber;
    g.setColour (juce::Colour (0xff1c1d20));
    g.fillRoundedRectangle (r.translated (1.0f, 3.0f), 9.0f);
    const auto face = r.translated (0.0f, isDown ? 2.0f : 0.0f);
    g.setGradientFill (juce::ColourGradient (theme::btnTop.interpolatedWith (tint, led * 0.85f), 0.0f, face.getY(),
                                             theme::btnBot.interpolatedWith (tint, led * 0.7f), 0.0f, face.getBottom(), false));
    g.fillRoundedRectangle (face, 9.0f);
    if (latched[(size_t) id])
    {
        g.setColour (tint);
        g.drawRoundedRectangle (face.reduced (1.0f), 9.0f, 2.0f);
    }
    g.setColour (led > 0.5f ? juce::Colour (0xff2a1a03) : theme::btnInk);
    g.setFont (theme::font (17.0f, true));
    g.drawText (label, face, juce::Justification::centred);
}

void EngineView::paintPanelKey (juce::Graphics& g, R r, int id)
{
    const bool isDown = down[(size_t) id];
    const float led = brightness (ledLevel[(size_t) id]);
    const float radius = r.getWidth() * 0.5f;
    g.setColour (juce::Colour (0xff1a1a1c));
    g.fillRoundedRectangle (r.translated (1.0f, 4.0f), radius);
    const float heat = isDown ? 0.55f : led * 0.6f;
    const auto face = r.translated (0.0f, isDown ? 3.0f : 0.0f);
    g.setGradientFill (juce::ColourGradient (theme::keyTop.interpolatedWith (theme::amber, heat), 0.0f, face.getY(),
                                             theme::keyBot.interpolatedWith (theme::amber, heat * 0.8f), 0.0f, face.getBottom(), false));
    g.fillRoundedRectangle (face, radius);
    g.setColour (juce::Colour (0xff8f7c7e).interpolatedWith (juce::Colour (0xffffe2b0), isDown ? 1.0f : led));
    g.fillRoundedRectangle (face.getCentreX() - 2.5f, face.getY() + 18.0f, 5.0f, face.getHeight() - 36.0f, 2.5f);
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
        case Hit::browseButton:
            if (onBrowseRequest != nullptr)
                onBrowseRequest (index);
            return;
        case Hit::toggle:
            toggleOpen = ! toggleOpen;
            repaintDesign (belowScreen());
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
            repaintDesign (stripArea());
            return;
        case Hit::solo:
            if (e.mods.isCommandDown() || e.mods.isAltDown())
                proc.soloExclusive (index);
            else
                proc.setSoloed (index, ! proc.isSoloed (index));
            repaintDesign (stripArea());
            return;
        case Hit::level:
            proc.masterParam (index).beginChangeGesture();
            setMasterAt (d.x);
            break;
        case Hit::pot:
            proc.masterParam (index).beginChangeGesture();
            break;
        case Hit::softKnob:
            repaintScreen (kScreen);
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
    else if (g.hit.kind == Hit::pot)
    {
        // a full turn in about 200 px, on the strip caret's scale
        auto& p = proc.masterParam (index);
        const float pos = masterPos (p.convertFrom0to1 (p.getValue())) + (g.lastY - y) / 200.0f;
        setMaster (masterAt (juce::jlimit (0.0f, 1.0f, pos)));
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
    if (grab->hit.kind == Hit::level || grab->hit.kind == Hit::pot)
        proc.masterParam (index).endChangeGesture();
    const bool soft = grab->hit.kind == Hit::softKnob;
    grab.reset();
    if (soft)
        repaintScreen (kScreen);
}

void EngineView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto h = hitAt (toDesign (e.position));
    if (h.kind == Hit::level || h.kind == Hit::pot)
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
    if (h.kind == Hit::level || h.kind == Hit::pot)
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
    const auto was = toScreen (hover), now = toScreen (d);
    const bool wasOverMenu = menuArea().contains (hover);
    const auto wasSoft = hitAt (hover);
    hover = d;
    if (kLoad.contains (now) != kLoad.contains (was))
        repaintScreen (kLoad);
    if (kBrowse.contains (now) != kBrowse.contains (was))
        repaintScreen (kBrowse);
    if (menuArea().contains (d) != wasOverMenu)
        repaintDesign (menuArea());
    const auto soft = hitAt (d);
    if ((soft.kind == Hit::softKnob) != (wasSoft.kind == Hit::softKnob) || (soft.kind == Hit::softKnob && soft.id != wasSoft.id))
        repaintScreen (kScreen);
}

void EngineView::mouseExit (const juce::MouseEvent&)
{
    const bool wasSoft = hitAt (hover).kind == Hit::softKnob;
    hover = { -1.0f, -1.0f };
    repaintScreen (kLoad);
    repaintScreen (kBrowse);
    repaintDesign (menuArea());
    if (wasSoft)
        repaintScreen (kScreen);
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
        case Hit::browseButton: return "Pick one from the Firmware Library";
        case Hit::toggle:     return "Panel buttons (or hold Shift over the engine)";
        case Hit::button:     return h.id >= 2 ? "Right-click to latch it down" : juce::String();
        case Hit::knob:       return juce::String (kKnobs[h.id].label) + "  (drag or scroll)";
        case Hit::mute:       return "Mute engine " + juce::String (index + 1);
        case Hit::solo:       return "Solo engine " + juce::String (index + 1) + "  (Cmd- or Alt-click: solo it alone)";
        case Hit::level:
            return "Engine " + juce::String (index + 1) + " level; the caret is its MASTER "
                   + proc.masterParam (index).getCurrentValueAsText() + "  (drag or scroll; double-click to reset)";
        case Hit::pot:
            return "MASTER " + proc.masterParam (index).getCurrentValueAsText() + "  (drag or scroll; double-click to reset)";
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
    repaintScreen (kScreen);
}

void EngineView::fileDragExit (const juce::StringArray&)
{
    dropHover = false;
    repaintScreen (kScreen);
}

void EngineView::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    repaintScreen (kScreen);
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("fwsc;fm1save;json;fm1ception"))
        {
            proc.loadFile (index, juce::File (f));
            return;
        }
}
} // namespace fm1c
