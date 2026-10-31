#include "InfoPanel.h"
#include "FirmwareLibrary.h"
#include "LibraryWindow.h"
#include "Theme.h"

namespace fm1c
{
namespace
{
constexpr int kPad = 14, kThumb = 40, kThumbGap = 8;
const char* const kPictureNames[] { "0.3 s", "5 s", "Hub card" };

/** Text wrapped to `width`. */
juce::TextLayout wrapped (const juce::String& text, const juce::Font& font, juce::Colour colour, float width)
{
    juce::AttributedString s;
    s.append (text, font, colour);
    s.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout layout;
    layout.createLayout (s, width);
    return layout;
}

/** Its lines that fit in `area`, from the top; returns the height they take. */
float drawWrapped (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Colour colour,
                   juce::Rectangle<float> area)
{
    if (text.isEmpty() || area.getHeight() <= 0.0f)
        return 0.0f;
    auto layout = wrapped (text, font, colour, area.getWidth());
    float h = 0.0f;
    // whole lines that fit
    for (int i = 0; i < layout.getNumLines(); ++i)
    {
        const auto& line = layout.getLine (i);
        const float bottom = line.getLineBoundsY().getEnd();
        if (bottom > area.getHeight())
            break;
        h = bottom;
    }
    g.saveState();
    g.reduceClipRegion (area.withHeight (h).getSmallestIntegerContainer());
    layout.draw (g, area.withHeight (layout.getHeight()));
    g.restoreState();
    return h;
}

void drawStars (juce::Graphics& g, juce::Rectangle<float> r, int stars)
{
    const float size = r.getHeight(), gap = 2.0f;
    for (int i = 0; i < FirmwareLibrary::kMaxRating; ++i)
    {
        juce::Path p;
        const auto box = juce::Rectangle<float> (r.getX() + (float) i * (size + gap), r.getY(), size, size);
        p.addStar (box.getCentre(), 5, size * 0.22f, size * 0.5f);
        g.setColour (i < stars ? theme::amber : theme::dimSilk.withAlpha (0.35f));
        g.fillPath (p);
    }
}
} // namespace

InfoPanel::InfoPanel (ScreenCache* c) : cache (c)
{
    action.onClick = [this] { captureNow (shots.error.isNotEmpty()); };
    addChildComponent (action);
    if (cache != nullptr)
        cache->addChangeListener (this);
}

InfoPanel::~InfoPanel()
{
    if (cache != nullptr)
        cache->removeChangeListener (this);
}

void InfoPanel::show (Info next)
{
    const bool same = next.key == info.key;
    info = std::move (next);
    if (! same)
    {
        picked = 1;
        hovered = -1;
        stopTimer();
        if (info.package.existsAsFile() && info.shotsKey.isNotEmpty())
            startTimer (kDwellMs);
    }
    refreshPictures();
}

void InfoPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshPictures();
}

void InfoPanel::timerCallback()
{
    stopTimer();
    if (! shots.has() && shots.error.isEmpty() && ! shots.capturing)
        captureNow (false);
}

void InfoPanel::captureNow (bool again)
{
    if (cache == nullptr || info.shotsKey.isEmpty())
        return;
    if (info.package.existsAsFile())
        cache->capture (info.shotsKey, info.package, again);
    else if (info.fetch)
        cache->capture (info.shotsKey, info.fetch, again);
    refreshPictures();
}

void InfoPanel::refreshPictures()
{
    shots = cache != nullptr ? cache->shotsOf (info.shotsKey) : ScreenCache::Shots();
    card = cache != nullptr ? cache->cardOf (info.card) : juce::Image();
    const bool canCapture = info.shotsKey.isNotEmpty() && (info.package.existsAsFile() || info.fetch != nullptr);
    if (shots.error.isNotEmpty() && canCapture)
        action.setButtonText ("TRY AGAIN");
    else if (! info.package.existsAsFile() && info.fetch != nullptr)
        action.setButtonText ("PREVIEW SCREENS");
    action.setTooltip (info.package.existsAsFile() ? "Boot it here, with no engine, and keep its screens"
                                                   : "Fetch its package (not into the library) and boot it here, with no engine, for its screens");
    action.setVisible (canCapture && ! shots.has() && ! shots.capturing
                       && (shots.error.isNotEmpty() || ! info.package.existsAsFile()));
    layout();
    repaint();
}

juce::Image InfoPanel::picture (int which) const
{
    switch (which)
    {
        case 0: return shots.early;
        case 1: return shots.late;
        case 2: return card;
        default: return {};
    }
}

int InfoPanel::shownPicture() const
{
    if (hovered >= 0 && picture (hovered).isValid())
        return hovered;
    if (picture (picked).isValid())
        return picked;
    for (const int i : { 1, 0, 2 })
        if (picture (i).isValid())
            return i;
    return -1;
}

void InfoPanel::resized()
{
    layout();
}

void InfoPanel::layout()
{
    auto b = getLocalBounds().reduced (kPad);
    const int side = juce::jmin (b.getWidth(), juce::jmax (120, (int) ((float) getHeight() * 0.46f)));
    big = b.removeFromTop (side).withSizeKeepingCentre (side, side);
    action.setBounds (big.withSizeKeepingCentre (juce::jmin (150, side - 20), 26).translated (0, 30));
    b.removeFromTop (8);
    auto row = b.removeFromTop (kThumb + 14);
    int x = row.getX();
    for (int i = 0; i < 3; ++i)
    {
        if (picture (i).isValid())
        {
            thumbs[i] = { x, row.getY(), kThumb, kThumb };
            x += kThumb + kThumbGap + 18;
        }
        else
        {
            thumbs[i] = {};
        }
    }
}

void InfoPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::header);
    g.setColour (juce::Colours::black);
    g.fillRect (0, 0, 1, getHeight());

    if (info.key.isEmpty())
    {
        g.setColour (theme::dimSilk);
        g.setFont (theme::font (13.0f));
        g.drawFittedText (info.placeholder, getLocalBounds().reduced (24), juce::Justification::centred, 4);
        return;
    }

    // the large picture: a screen as the FM-1 shows it, in its bezel
    g.setColour (theme::bezel);
    g.fillRoundedRectangle (big.toFloat(), 6.0f);
    const int which = shownPicture();
    const auto inner = big.reduced (6);
    if (which >= 0)
    {
        const auto img = picture (which);
        const auto fit = juce::RectanglePlacement (juce::RectanglePlacement::centred).appliedTo (img.getBounds().toFloat(), inner.toFloat());
        g.setImageResamplingQuality (which == 2 ? juce::Graphics::highResamplingQuality : juce::Graphics::mediumResamplingQuality);
        g.drawImage (img, fit);
    }
    else
    {
        juce::String text;
        if (info.shotsKey.isEmpty())
            text = "No screens: it is not an FM-1 package";
        else if (shots.capturing)
            text = "Booting it for its screens...";
        else if (shots.error.isNotEmpty())
            text = "Its screens could not be captured:\n" + shots.error;
        else if (info.package.existsAsFile())
            text = "Its screens are captured in a moment";
        else if (info.fetch != nullptr)
            text = "Not downloaded yet";
        else
            text = "No screens yet";
        g.setColour (shots.error.isNotEmpty() ? theme::warn : theme::dimSilk);
        g.setFont (theme::font (12.5f));
        g.drawFittedText (text, inner.reduced (12).withTrimmedBottom (action.isVisible() ? 50 : 0), juce::Justification::centred, 5);
    }
    g.setColour (theme::tray);
    g.drawRoundedRectangle (big.toFloat().reduced (0.5f), 6.0f, 1.0f);

    // the small ones, to pick from
    for (int i = 0; i < 3; ++i)
    {
        if (thumbs[i].isEmpty())
            continue;
        const auto r = thumbs[i];
        g.setColour (theme::bezel);
        g.fillRect (r);
        const auto img = picture (i);
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        g.drawImage (img, juce::RectanglePlacement (juce::RectanglePlacement::centred).appliedTo (img.getBounds().toFloat(), r.toFloat().reduced (2.0f)));
        g.setColour (i == which ? theme::amber : i == hovered ? theme::silk : theme::tray);
        g.drawRect (r, i == which ? 2 : 1);
        g.setColour (i == which ? theme::amber : theme::dimSilk);
        g.setFont (theme::font (10.0f, i == which));
        g.drawText (kPictureNames[i], r.withY (r.getBottom() + 1).withHeight (13).expanded (12, 0), juce::Justification::centred, false);
    }

    // what it is
    auto area = getLocalBounds().reduced (kPad).withTop (big.getBottom() + 8 + kThumb + 14 + 6).toFloat();
    if (std::all_of (std::begin (thumbs), std::end (thumbs), [] (const auto& t) { return t.isEmpty(); }))
        area.setTop ((float) big.getBottom() + 12.0f);
    const float w = area.getWidth();
    auto line = [&area] (float h) { return area.removeFromTop (h); };

    g.setColour (theme::silk);
    g.setFont (theme::font (17.0f, true));
    g.drawText (info.title, line (22.0f), juce::Justification::centredLeft, true);

    juce::String by = info.version.isNotEmpty() ? info.version : juce::String();
    if (info.author.isNotEmpty())
        by << (by.isNotEmpty() ? "   by " : "by ") << info.author;
    if (by.isNotEmpty())
    {
        g.setColour (theme::dimSilk);
        g.setFont (theme::font (12.5f));
        g.drawText (by, line (18.0f), juce::Justification::centredLeft, true);
    }
    if (info.category.isNotEmpty() || info.rating >= 0)
    {
        auto r = line (20.0f);
        if (info.category.isNotEmpty())
        {
            g.setFont (theme::font (10.5f, true));
            const auto text = info.category.toUpperCase();
            const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 14.0f;
            const auto chip = r.removeFromLeft (tw).withSizeKeepingCentre (tw, 16.0f);
            g.setColour (theme::amber.withAlpha (0.14f));
            g.fillRoundedRectangle (chip, 8.0f);
            g.setColour (theme::amber);
            g.drawText (text, chip, juce::Justification::centred, false);
            r.removeFromLeft (10.0f);
        }
        if (info.rating >= 0)
            drawStars (g, r.withSizeKeepingCentre (r.getWidth(), 11.0f), info.rating);
    }
    area.removeFromTop (6.0f);

    // the facts first in what room there is: the description takes what is left over
    const float factH = 17.0f;
    const float factsH = (float) info.facts.size() * factH + (info.facts.empty() ? 0.0f : 8.0f);
    const float noteH = info.note.isNotEmpty() ? juce::jmin (44.0f, wrapped (info.note, theme::font (11.5f), theme::dimSilk, w).getHeight() + 6.0f) : 0.0f;
    const float aboutRoom = juce::jmax (0.0f, area.getHeight() - factsH - noteH);
    if (info.about.isNotEmpty())
    {
        const float h = drawWrapped (g, info.about, theme::font (12.5f), theme::silk, area.withHeight (aboutRoom));
        area.removeFromTop (h + (h > 0.0f ? 8.0f : 0.0f));
    }
    if (info.note.isNotEmpty())
    {
        const float h = drawWrapped (g, info.note, theme::font (11.5f), theme::dimSilk, area.withHeight (noteH - 6.0f));
        area.removeFromTop (h + 6.0f);
    }

    if (! info.facts.empty())
    {
        area.removeFromTop (2.0f);
        g.setColour (theme::tray);
        g.fillRect (area.removeFromTop (1.0f));
        area.removeFromTop (5.0f);
    }
    linkArea = {};
    const auto valueFont = theme::font (12.0f);
    for (size_t i = 0; i < info.facts.size(); ++i)
    {
        if (area.getHeight() < factH)
            break;
        auto r = line (factH);
        const auto& [label, value] = info.facts[i];
        g.setColour (theme::dimSilk);
        g.setFont (theme::font (10.0f, true));
        g.drawText (label.toUpperCase(), r.removeFromLeft (72.0f), juce::Justification::centredLeft, false);
        const bool isLink = i + 1 == info.facts.size() && LibraryView::openableUrl (info.link).isNotEmpty();
        g.setFont (valueFont);
        if (isLink)
        {
            linkArea = r.getSmallestIntegerContainer();
            g.setColour (overLink ? theme::amber : theme::silk);
            g.strokePath (LibraryView::linkIcon (r.removeFromLeft (16.0f).reduced (2.0f).withSizeKeepingCentre (11.0f, 11.0f)),
                          juce::PathStrokeType (1.1f));
            r.removeFromLeft (3.0f);
        }
        else
        {
            g.setColour (theme::silk);
        }
        g.drawText (LibraryView::middleEllipsis (value, valueFont, r.getWidth()), r, juce::Justification::centredLeft, false);
    }
}

void InfoPanel::mouseMove (const juce::MouseEvent& e)
{
    int over = -1;
    for (int i = 0; i < 3; ++i)
        if (thumbs[i].contains (e.getPosition()))
            over = i;
    const bool link = linkArea.contains (e.getPosition());
    if (over != hovered || link != overLink)
    {
        hovered = over;
        overLink = link;
        setMouseCursor (over >= 0 || link ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void InfoPanel::mouseExit (const juce::MouseEvent&)
{
    if (hovered >= 0 || overLink)
    {
        hovered = -1;
        overLink = false;
        repaint();
    }
}

void InfoPanel::mouseUp (const juce::MouseEvent& e)
{
    for (int i = 0; i < 3; ++i)
        if (thumbs[i].contains (e.getPosition()))
        {
            picked = i;
            repaint();
            return;
        }
    if (linkArea.contains (e.getPosition()))
        if (const auto url = LibraryView::openableUrl (info.link); url.isNotEmpty())
            juce::URL (url).launchInDefaultBrowser();
}
} // namespace fm1c
