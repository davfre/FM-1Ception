#include "EngineView.h"
#include "Theme.h"

namespace fm1c
{
namespace
{
using R = juce::Rectangle<float>;

const R kStrip  { 0.0f, 0.0f, 480.0f, 16.0f };
const R kScreen { 0.0f, 16.0f, 480.0f, 480.0f };
const R kRows   { 0.0f, 496.0f, 480.0f, 128.0f };
const R kLoad   { 150.0f, 236.0f, 180.0f, 40.0f };
const R kMenu   { 458.0f, 0.0f, 22.0f, 16.0f };
const R kToggle { 452.0f, 592.0f, 24.0f, 26.0f };
const R kPopover { 4.0f, 498.0f, 472.0f, 90.0f };
const R kKeys   { 248.0f, 564.0f, 196.0f, 54.0f };

struct KnobSpec
{
    float cx, cy, r;
    const char* label;
    int enc;   // the matrix encoder; -1: the MASTER pot
};

const KnobSpec kKnobs[8] {
    {  60.0f, 520.0f, 17.0f, "KNOB1", 2 },
    { 180.0f, 520.0f, 17.0f, "KNOB2", 3 },
    { 300.0f, 520.0f, 17.0f, "KNOB3", 4 },
    { 420.0f, 520.0f, 17.0f, "KNOB4", 5 },
    {  24.0f, 579.0f, 12.0f, "MASTER", -1 },
    {  68.0f, 579.0f, 12.0f, "SELECT", 0 },
    { 112.0f, 579.0f, 12.0f, "PRESETS", 6 },
    { 156.0f, 579.0f, 12.0f, "ALGO", 1 },
};
constexpr int kMasterKnob = 4;

struct ButtonSpec
{
    R r;
    const char* label;
    int id;
};

const ButtonSpec kOct[2] {
    { { 182.0f, 566.0f, 26.0f, 28.0f }, "OCT-", 0 },
    { { 212.0f, 566.0f, 26.0f, 28.0f }, "OCT+", 1 },
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

void paintKnob (juce::Graphics& g, const KnobSpec& k, float angle, float glow)
{
    const juce::Point<float> c { k.cx, k.cy };
    g.setColour (juce::Colour (0xff222326));
    g.fillEllipse (R (k.r * 2.0f + 2.0f, k.r * 2.0f + 2.0f).withCentre (c.translated (0.8f, 1.6f)));
    if (glow > 0.0f)
    {
        g.setColour (theme::amber.withAlpha (0.75f * glow));
        g.fillEllipse (R (k.r * 2.0f + 5.0f, k.r * 2.0f + 5.0f).withCentre (c));
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff585b62), c.x, c.y - k.r,
                                             juce::Colour (0xff2c2e32), c.x, c.y + k.r, false));
    g.fillEllipse (R (k.r * 2.0f, k.r * 2.0f).withCentre (c));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6d7178), c.x - k.r * 0.5f, c.y - k.r * 0.5f,
                                             juce::Colour (0xff3a3d43), c.x + k.r * 0.5f, c.y + k.r * 0.5f, false));
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
    if (grab && grab->hit.kind == Hit::knob && grab->hit.id == kMasterKnob)
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

EngineView::Hit EngineView::hitTest (juce::Point<float> d) const
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
    if (kStrip.contains (d))
        return { Hit::strip };
    if (isBlank() && kLoad.contains (d))
        return { Hit::loadButton };
    if (kScreen.contains (d))
        return { Hit::strip };

    for (int i = 0; i < 8; ++i)
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
    glow[kMasterKnob] = 10;
    repaintDesign (kRows);
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

    if (handle != nullptr)
    {
        std::array<float, FM1E_LEDS> fractions {};
        Fm1eStatus st {};
        fm1e_view (handle->get(), lcdRaw.data(), fractions.data(), &st);

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

        if (lcdRaw != lcdPrev)
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
    }
    else
    {
        const auto name = handle->firmwareName();
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

    const auto num = R (2.0f, 2.0f, 14.0f, 12.0f);
    g.setColour (handle != nullptr && hasFirmware ? theme::amber : theme::tray);
    g.fillRoundedRectangle (num, 2.0f);
    g.setColour (handle != nullptr && hasFirmware ? theme::header : theme::dimSilk);
    g.setFont (theme::font (10.0f, true));
    g.drawText (juce::String (index + 1), num, juce::Justification::centred);

    auto body = kStrip.withTrimmedLeft (22.0f).withTrimmedRight (kMenu.getWidth() + 4.0f);
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

    const auto& master = proc.masterParam (index);
    for (int i = 0; i < 8; ++i)
    {
        const auto& k = kKnobs[i];
        const float angle = k.enc < 0
            ? juce::degreesToRadians (-135.0f + 270.0f * master.convertFrom0to1 (master.getValue()))
            : juce::degreesToRadians ((float) clicks[(size_t) i] * 18.0f);
        paintKnob (g, k, angle, juce::jmin (1.0f, (float) glow[(size_t) i] / 10.0f));
        const float labelSize = i < 4 ? 9.0f : 7.5f;
        g.setColour (theme::silk);
        g.setFont (theme::font (labelSize, true));
        g.drawText (k.label, R (k.cx - 30.0f, k.cy + k.r + 3.0f, 60.0f, labelSize + 2.0f), juce::Justification::centred);
    }

    for (const auto& b : kOct)
        paintButton (g, b.r, b.label, b.id, 7.5f);

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
    const auto h = hitTest (d);

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
            onMenuRequest (index);
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
                onMenuRequest (index);
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
        case Hit::knob:
            if (h.id == kMasterKnob)
                proc.masterParam (index).beginChangeGesture();
            break;
        case Hit::none: case Hit::strip: case Hit::popoverBack:
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
        const auto h = hitTest (toDesign (e.position));
        if (h.kind == Hit::key && h.id != g.held)
        {
            press (g.held, false);
            g.held = h.id;
            press (g.held, true);
        }
    }
    else if (g.hit.kind == Hit::knob)
    {
        if (g.hit.id == kMasterKnob)
        {
            const auto& p = proc.masterParam (index);
            setMaster (p.convertFrom0to1 (p.getValue()) + (g.lastY - y) * 0.005f);
        }
        else
        {
            // 8 px of drag a click
            g.acc += g.lastY - y;
            const int n = (int) (g.acc / 8.0f);
            g.acc -= (float) n * 8.0f;
            turn (g.hit.id, n);
        }
    }
    g.lastY = y;
}

void EngineView::mouseUp (const juce::MouseEvent&)
{
    if (! grab)
        return;
    if (grab->held >= 0 && ! latched[(size_t) grab->held])
        press (grab->held, false);
    if (grab->hit.kind == Hit::knob && grab->hit.id == kMasterKnob)
        proc.masterParam (index).endChangeGesture();
    grab.reset();
}

void EngineView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto h = hitTest (toDesign (e.position));
    if (h.kind == Hit::knob && h.id == kMasterKnob)
    {
        auto& p = proc.masterParam (index);
        p.beginChangeGesture();
        setMaster (FM1CeptionProcessor::kDefaultMaster);
        p.endChangeGesture();
    }
}

void EngineView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto h = hitTest (toDesign (e.position));
    if (h.kind != Hit::knob)
        return;
    const float delta = w.isReversed ? -w.deltaY : w.deltaY;
    if (h.id == kMasterKnob)
    {
        auto& p = proc.masterParam (index);
        p.beginChangeGesture();
        setMaster (p.convertFrom0to1 (p.getValue()) + delta * 0.25f);
        p.endChangeGesture();
        return;
    }
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
    hover = d;
    if (kLoad.contains (d) != wasOverLoad)
        repaintDesign (kLoad);
    if (kMenu.contains (d) != wasOverMenu)
        repaintDesign (kMenu);
}

void EngineView::mouseExit (const juce::MouseEvent&)
{
    hover = { -1.0f, -1.0f };
    repaintDesign (kLoad);
    repaintDesign (kMenu);
}

juce::String EngineView::getTooltip()
{
    const auto h = hitTest (hover);
    switch (h.kind)
    {
        case Hit::menu:       return "Engine " + juce::String (index + 1) + " menu";
        case Hit::strip:      return "Right-click for engine " + juce::String (index + 1) + "'s menu";
        case Hit::loadButton: return "Open a .fwsc firmware package or a shared .fm1save setup";
        case Hit::toggle:     return "Panel buttons (or hold Shift over the engine)";
        case Hit::button:     return h.id >= 2 ? "Right-click to latch it down" : juce::String();
        case Hit::knob:
            return h.id == kMasterKnob ? "MASTER " + proc.masterParam (index).getCurrentValueAsText()
                                         + "  (drag, scroll; double-click to reset)"
                                       : juce::String (kKnobs[h.id].label) + "  (drag or scroll)";
        case Hit::none: case Hit::key: case Hit::popoverBack:
            break;
    }
    return {};
}

//==============================================================================
bool EngineView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("fwsc;fm1save;json"))
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
        if (juce::File (f).hasFileExtension ("fwsc;fm1save;json"))
        {
            proc.loadFile (index, juce::File (f));
            return;
        }
}
} // namespace fm1c
