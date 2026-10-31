#include "LibraryWindow.h"
#include "Theme.h"

namespace fm1c
{
namespace
{
using Item = FirmwareLibrary::Item;
using Category = FirmwareLibrary::Category;

const juce::Colour rowA { 0xff1c1d20 }, rowB { 0xff202124 };
constexpr float kStar = 13.0f, kStarGap = 1.5f, kStarsX = 6.0f;

juce::Path pencilIcon (juce::Rectangle<float> r)
{
    const auto s = juce::jmin (r.getWidth(), r.getHeight());
    const auto b = juce::Rectangle<float> (s, s).withCentre (r.getCentre());
    auto at = [&b] (float x, float y) { return juce::Point<float> (b.getX() + x * b.getWidth(), b.getY() + y * b.getHeight()); };
    juce::Path p;
    p.startNewSubPath (at (0.08f, 0.92f));
    p.lineTo (at (0.14f, 0.66f));
    p.lineTo (at (0.70f, 0.10f));
    p.lineTo (at (0.90f, 0.30f));
    p.lineTo (at (0.34f, 0.86f));
    p.closeSubPath();
    p.startNewSubPath (at (0.14f, 0.66f));
    p.lineTo (at (0.34f, 0.86f));
    p.startNewSubPath (at (0.58f, 0.22f));
    p.lineTo (at (0.78f, 0.42f));
    return p;
}

juce::Path linkIconPath (juce::Rectangle<float> r)
{
    const auto s = juce::jmin (r.getWidth(), r.getHeight());
    const auto b = juce::Rectangle<float> (s, s).withCentre (r.getCentre());
    auto at = [&b] (float x, float y) { return juce::Point<float> (b.getX() + x * b.getWidth(), b.getY() + y * b.getHeight()); };
    juce::Path p;
    p.startNewSubPath (at (0.45f, 0.18f));
    p.lineTo (at (0.12f, 0.18f));
    p.lineTo (at (0.12f, 0.88f));
    p.lineTo (at (0.82f, 0.88f));
    p.lineTo (at (0.82f, 0.55f));
    p.startNewSubPath (at (0.45f, 0.55f));
    p.lineTo (at (0.92f, 0.08f));
    p.startNewSubPath (at (0.60f, 0.08f));
    p.lineTo (at (0.92f, 0.08f));
    p.lineTo (at (0.92f, 0.40f));
    return p;
}

juce::String hostOf (const juce::String& url)
{
    auto s = url.fromFirstOccurrenceOf ("://", false, false);
    s = s.upToFirstOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("?", false, false);
    return s.startsWithIgnoreCase ("www.") ? s.substring (4) : s;
}

juce::String normalisedUrl (const juce::String& text)
{
    const auto s = text.trim();
    if (s.isEmpty() || s.contains ("://"))
        return s;
    return "https://" + s;
}

juce::String shortPathText (const juce::File& f)
{
    auto path = f.getFullPathName();
   #if JUCE_MAC || JUCE_LINUX
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
    if (path.startsWith (home + "/"))
        path = "~" + path.substring (home.length());
   #endif
    return path;
}

juce::String revealMenuText()
{
   #if JUCE_MAC
    return "Reveal in Finder";
   #elif JUCE_WINDOWS
    return "Show in Explorer";
   #else
    return "Show in File Manager";
   #endif
}
} // namespace

//==============================================================================
/** RATING: ten stars, lit up to the rating; the hovered one shows what a click sets. */
class LibraryView::Stars : public juce::Component
{
public:
    explicit Stars (LibraryView& o) : owner (o) {}

    void update (int r, const juce::File& f, int stars)
    {
        row = r;
        file = f;
        if (stars != value)
        {
            value = stars;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const int shown = hot >= 0 ? hot + 1 : value;
        for (int i = 0; i < FirmwareLibrary::kMaxRating; ++i)
        {
            const auto box = star (i);
            juce::Path p;
            p.addStar (box.getCentre(), 5, kStar * 0.22f, kStar * 0.5f);
            if (i < shown)
                g.setColour (hot >= 0 ? theme::amber.withAlpha (i < value ? 1.0f : 0.6f) : theme::amber);
            else
                g.setColour (hot >= 0 && i < value ? theme::amber.withAlpha (0.25f) : theme::tray.brighter (0.35f));
            g.fillPath (p);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { setHot (starAt (e.position.x)); }
    void mouseExit (const juce::MouseEvent&) override { setHot (-1); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            owner.selectForMenu (row);
            owner.showRowMenu (row);
            return;
        }
        const int k = starAt (e.position.x);
        if (k < 0)
        {
            owner.table.selectRowsBasedOnModifierKeys (row, e.mods, false);
            return;
        }
        owner.library.setRating (file, k + 1 == value ? 0 : k + 1);
    }

private:
    juce::Rectangle<float> star (int i) const
    {
        return { kStarsX + (float) i * (kStar + kStarGap), ((float) getHeight() - kStar) * 0.5f, kStar, kStar };
    }

    int starAt (float x) const
    {
        const int i = (int) std::floor ((x - kStarsX + kStarGap * 0.5f) / (kStar + kStarGap));
        return juce::isPositiveAndBelow (i, FirmwareLibrary::kMaxRating) ? i : -1;
    }

    void setHot (int h)
    {
        if (h != hot)
        {
            hot = h;
            repaint();
        }
    }

    LibraryView& owner;
    int row = 0, value = 0, hot = -1;
    juce::File file;
};

//==============================================================================
/** SOURCE URL: a link to it (its site's name) and a pencil to edit it; just the pencil while it
    has none. */
class LibraryView::Link : public juce::Component,
                          public juce::SettableTooltipClient
{
public:
    explicit Link (LibraryView& o) : owner (o)
    {
        editor.setFont (theme::font (12.5f));
        editor.setTextToShowWhenEmpty ("https://...", theme::dimSilk);
        editor.setSelectAllWhenFocused (true);
        editor.onReturnKey = [this] { finish (true); };
        editor.onEscapeKey = [this] { finish (false); };
        editor.onFocusLost = [this] { finish (true); };
        addChildComponent (editor);
    }

    void update (int r, const juce::File& f, const juce::String& u)
    {
        if (f != file)
            finish (false);
        row = r;
        file = f;
        if (u != url)
        {
            url = u;
            repaint();
        }
        setTooltip (url.isEmpty() ? "Add where this firmware comes from" : url);
    }

    void beginEdit()
    {
        editing = true;
        editor.setText (url, false);
        editor.setVisible (true);
        editor.grabKeyboardFocus();
        editor.selectAll();
    }

    void resized() override { editor.setBounds (getLocalBounds().reduced (1)); }

    void paint (juce::Graphics& g) override
    {
        if (editing)
            return;
        const auto pencil = pencilBox();
        g.setColour (hot == 2 ? theme::amber : theme::dimSilk);
        g.strokePath (pencilIcon (pencil.reduced (5.0f)), juce::PathStrokeType (1.2f));
        if (url.isEmpty())
            return;

        auto link = linkBox();
        const bool openable = openableUrl (url).isNotEmpty();
        const auto ink = ! openable ? theme::dimSilk : hot == 1 ? theme::amber : theme::silk;
        g.setColour (ink);
        g.strokePath (linkIcon (link.removeFromLeft (18.0f).reduced (4.0f)), juce::PathStrokeType (1.2f));
        const auto host = hostOf (url);
        g.setFont (theme::font (12.5f));
        g.drawText (host.isEmpty() ? url : host, link.withTrimmedLeft (3.0f), juce::Justification::centredLeft, true);
        if (hot == 1 && openable)
        {
            const float w = juce::jmin (link.getWidth() - 3.0f, juce::GlyphArrangement::getStringWidth (theme::font (12.5f), host));
            g.fillRect (link.getX() + 3.0f, link.getCentreY() + 7.0f, w, 1.0f);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = pencilBox().contains (e.position) ? 2 : url.isNotEmpty() && linkBox().contains (e.position) ? 1 : 0;
        if (h != hot)
        {
            hot = h;
            setMouseCursor (h != 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hot = 0;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            owner.selectForMenu (row);
            owner.showRowMenu (row);
        }
        else if (pencilBox().contains (e.position))
        {
            beginEdit();
        }
        else if (url.isNotEmpty() && linkBox().contains (e.position))
        {
            if (const auto u = openableUrl (url); u.isNotEmpty())
                juce::URL (u).launchInDefaultBrowser();
        }
        else
        {
            owner.table.selectRowsBasedOnModifierKeys (row, e.mods, false);
        }
    }

private:
    juce::Rectangle<float> pencilBox() const
    {
        const auto b = getLocalBounds().toFloat();
        return url.isEmpty() ? b.withWidth (24.0f).translated (2.0f, 0.0f) : b.withTrimmedLeft (b.getWidth() - 24.0f);
    }

    juce::Rectangle<float> linkBox() const
    {
        return getLocalBounds().toFloat().withTrimmedLeft (2.0f).withTrimmedRight (26.0f);
    }

    void finish (bool keep)
    {
        if (! editing)
            return;
        editing = false;
        const auto text = editor.getText();
        editor.setVisible (false);
        repaint();
        if (keep)
            owner.library.setUrl (file, normalisedUrl (text));
    }

    LibraryView& owner;
    juce::TextEditor editor;
    juce::File file;
    juce::String url;
    int row = 0, hot = 0;
    bool editing = false;
};

//==============================================================================
/** FIRMWARE NAME while it is being edited. Return keeps it, Escape does not; an empty name goes
    back to the one the firmware gives itself. */
class LibraryView::NameEditor : public juce::TextEditor
{
public:
    NameEditor (LibraryView& o, const juce::File& f, const juce::String& text) : owner (o), file (f)
    {
        setFont (theme::font (12.5f));
        setText (text, false);
        onReturnKey = [this] { finish (true); };
        onEscapeKey = [this] { finish (false); };
        onFocusLost = [this] { finish (true); };
    }

    const juce::File& getFile() const { return file; }

    void parentHierarchyChanged() override
    {
        if (! focused && getParentComponent() != nullptr)
        {
            focused = true;
            juce::Component::SafePointer<NameEditor> safe (this);
            juce::MessageManager::callAsync ([safe]
            {
                if (safe != nullptr && safe->isShowing())
                {
                    safe->grabKeyboardFocus();
                    safe->selectAll();
                }
            });
        }
    }

private:
    void finish (bool keep)
    {
        if (done)
            return;
        done = true;
        auto& o = owner;
        o.editingName = juce::File();
        if (keep)
            o.library.setCustomName (file, getText());
        juce::Component::SafePointer<LibraryView> safe (&o);
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr)
                safe->table.updateContent();
        });
    }

    LibraryView& owner;
    const juce::File file;
    bool focused = false, done = false;
};

//==============================================================================
LibraryView::LibraryView (FirmwareLibrary& lib, Engines e, CloudCatalog* c, ScreenCache* s)
    : library (lib), engines (std::move (e)), cloud (c), info (s)
{
    lnf.setColourScheme (juce::LookAndFeel_V4::getMidnightColourScheme());
    lnf.setColour (juce::PopupMenu::backgroundColourId, theme::header);
    lnf.setColour (juce::PopupMenu::textColourId, theme::silk);
    lnf.setColour (juce::PopupMenu::headerTextColourId, theme::amber);
    lnf.setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::amber.withAlpha (0.85f));
    lnf.setColour (juce::PopupMenu::highlightedTextColourId, theme::header);
    lnf.setColour (juce::TooltipWindow::backgroundColourId, theme::header);
    lnf.setColour (juce::TooltipWindow::textColourId, theme::silk);
    lnf.setColour (juce::TooltipWindow::outlineColourId, theme::tray);
    lnf.setColour (juce::TextButton::buttonColourId, theme::tray);
    lnf.setColour (juce::TextButton::buttonOnColourId, theme::amber);
    lnf.setColour (juce::TextButton::textColourOffId, theme::silk);
    lnf.setColour (juce::TextButton::textColourOnId, theme::header);
    lnf.setColour (juce::ComboBox::outlineColourId, theme::tray);
    lnf.setColour (juce::TextEditor::backgroundColourId, theme::header);
    lnf.setColour (juce::TextEditor::textColourId, theme::silk);
    lnf.setColour (juce::TextEditor::outlineColourId, theme::tray.brighter (0.2f));
    lnf.setColour (juce::TextEditor::focusedOutlineColourId, theme::amber);
    lnf.setColour (juce::TextEditor::highlightColourId, theme::amber.withAlpha (0.35f));
    lnf.setColour (juce::CaretComponent::caretColourId, theme::amber);
    lnf.setColour (juce::ListBox::backgroundColourId, rowA);
    lnf.setColour (juce::ListBox::outlineColourId, juce::Colours::black);
    lnf.setColour (juce::TableHeaderComponent::backgroundColourId, theme::header);
    lnf.setColour (juce::TableHeaderComponent::textColourId, theme::dimSilk);
    lnf.setColour (juce::TableHeaderComponent::outlineColourId, theme::tray);
    lnf.setColour (juce::TableHeaderComponent::highlightColourId, theme::tray);
    lnf.setColour (juce::ScrollBar::thumbColourId, theme::dimSilk.withAlpha (0.6f));
    lnf.setColour (juce::AlertWindow::backgroundColourId, theme::header);
    lnf.setColour (juce::AlertWindow::textColourId, theme::silk);
    lnf.setColour (juce::AlertWindow::outlineColourId, theme::tray);
    setLookAndFeel (&lnf);

    pathButton.onClick = [this] { choosePath(); };
    addAndMakeVisible (pathButton);
    rescanButton.setTooltip ("Look through the folder again");
    rescanButton.onClick = [this] { library.rescan(); };
    addAndMakeVisible (rescanButton);
    refreshButton.setTooltip ("Fetch fm1-emu's list of firmwares again");
    refreshButton.onClick = [this] { if (cloud != nullptr) cloud->refresh(); };
    addChildComponent (refreshButton);

    localTab.setTooltip ("The firmwares in your folder");
    cloudTab.setTooltip ("Community firmwares from fm1-emu's list (github.com/joakimeriksson/fm1-emu), to download into your folder");
    localTab.setConnectedEdges (juce::Button::ConnectedOnRight);
    cloudTab.setConnectedEdges (juce::Button::ConnectedOnLeft);
    localTab.onClick = [this] { showCloud (false); };
    cloudTab.onClick = [this] { showCloud (true); };
    addAndMakeVisible (localTab);
    addAndMakeVisible (cloudTab);
    infoButton.setTooltip ("The panel at the right: the firmware under the mouse, else the one selected, with its screens");
    infoButton.setClickingTogglesState (true);
    infoButton.setToggleState (library.isInfoShown(), juce::dontSendNotification);
    infoButton.onClick = [this] { showInfo (infoButton.getToggleState()); };
    addAndMakeVisible (infoButton);
    addChildComponent (info);
    localTab.setVisible (cloud != nullptr);
    cloudTab.setVisible (cloud != nullptr);

    search.setTextToShowWhenEmpty ("Search names", theme::dimSilk);
    search.setFont (theme::font (13.0f));
    search.setIndents (8, 0);
    search.setJustification (juce::Justification::centredLeft);
    search.onTextChange = [this]
    {
        refill();
        if (cloudTable != nullptr)
            cloudTable->setSearch (search.getText());
    };
    search.onEscapeKey = [this]
    {
        search.clear();
        search.onTextChange();
    };
    addAndMakeVisible (search);

    auto& h = table.getHeader();
    const int flags = juce::TableHeaderComponent::defaultFlags;
    h.addColumn ("FILE NAME", fileName, 210, 90, 2000, flags);
    h.addColumn ("FIRMWARE NAME", firmwareName, 190, 90, 2000, flags);
    h.addColumn ("VERSION", version, 80, 50, 300, flags);
    h.addColumn ("AUTHOR", author, 130, 60, 600, flags);
    h.addColumn ("DESCRIPTION", description, 260, 90, 3000, flags);
    h.addColumn ("DATE ADDED", dateAdded, 106, 80, 400, flags);
    h.addColumn ("DATE UPDATED", dateUpdated, 116, 80, 400, flags);
    h.addColumn ("CATEGORY", category, 104, 80, 300, flags);
    h.addColumn ("SUB CATEGORY", subcategory, 124, 80, 300, flags);
    const int starsW = (int) std::ceil (kStarsX * 2.0f + (float) FirmwareLibrary::kMaxRating * (kStar + kStarGap));
    h.addColumn ("RATING", rating, starsW, starsW, starsW + 40, flags);
    h.addColumn ("SOURCE URL", sourceUrl, 210, 90, 2000, flags);
    h.setSortColumnId (fileName, true);
    h.setStretchToFitActive (false);
    if (const auto saved = library.getColumns(); saved.isNotEmpty())
    {
        h.restoreFromString (saved);
        // (a column the saved layout does not know goes last: this one goes beside CATEGORY)
        if (! saved.contains ("id=\"" + juce::String ((int) subcategory) + "\""))
            h.moveColumn (subcategory, h.getIndexOfColumnId (category, false) + 1);
    }
    // (a width saved is taken as it was, past the column's limits)
    if (h.getColumnWidth (rating) > starsW + 40)
        h.setColumnWidth (rating, starsW);
    sortColumn = h.getSortColumnId() != 0 ? h.getSortColumnId() : (int) fileName;
    sortForwards = h.isSortedForwards();

    table.setRowHeight (24);
    table.setHeaderHeight (26);
    table.setMultipleSelectionEnabled (true);
    table.setOutlineThickness (0);
    table.getViewport()->setScrollBarThickness (11);
    addAndMakeVisible (table);

    note.setFont (theme::font (14.0f));
    note.setColour (juce::Label::textColourId, theme::dimSilk);
    note.setJustificationType (juce::Justification::centred);
    note.setInterceptsMouseClicks (false, false);
    addChildComponent (note);

    library.addChangeListener (this);
    if (cloud != nullptr)
        cloud->addChangeListener (this);
    refill();
    showCloud (cloud != nullptr && library.isCloudShown());
    startTimer (80);
}

LibraryView::~LibraryView()
{
    stopTimer();
    saveColumns();
    library.removeChangeListener (this);
    if (cloud != nullptr)
        cloud->removeChangeListener (this);
    cloudTable.reset();
    setLookAndFeel (nullptr);
}

void LibraryView::saveColumns()
{
    library.setColumns (table.getHeader().toString());
    if (cloudTable != nullptr)
        cloudTable->saveColumns();
}

void LibraryView::refreshFromLibrary()
{
    refill();
    library.rescan();
    if (isCloudShown())
        cloud->refreshOnce();
}

void LibraryView::showCloud (bool on)
{
    on = on && cloud != nullptr;
    if (on && cloudTable == nullptr)
    {
        cloudTable = std::make_unique<CloudView> (*cloud, library, engines);
        cloudTable->setSearch (search.getText());
        addChildComponent (*cloudTable);
    }
    if (cloudTable != nullptr)
        cloudTable->setVisible (on);
    table.setVisible (! on);
    pathButton.setVisible (! on);
    rescanButton.setVisible (! on);
    refreshButton.setVisible (on);
    refreshButton.setEnabled (cloud != nullptr && ! cloud->isFetching());
    localTab.setToggleState (! on, juce::dontSendNotification);
    cloudTab.setToggleState (on, juce::dontSendNotification);
    search.setTextToShowWhenEmpty (on ? "Search names, authors, about" : "Search names", theme::dimSilk);
    if (cloud != nullptr)
        library.setCloudShown (on);
    if (on)
        cloud->refreshOnce();
    infoStale = true;
    resized();
    updateStatus();
    updateInfo();
}

void LibraryView::showInfo (bool on)
{
    library.setInfoShown (on);
    infoButton.setToggleState (on, juce::dontSendNotification);
    infoStale = true;
    resized();
    repaint();
    updateInfo();
}

int LibraryView::rowUnderMouse (const juce::TableListBox& t)
{
    if (! t.isShowing() || ! t.isMouseOver (true))
        return -1;
    const auto p = t.getMouseXYRelative();
    if (p.y < t.getHeaderHeight() || p.x >= t.getViewport()->getMaximumVisibleWidth())
        return -1;
    return t.getRowContainingPosition (p.x, p.y);
}

void LibraryView::updateInfo()
{
    if (! info.isVisible())
        return;
    InfoPanel::Info next;
    if (isCloudShown())
    {
        const int r = cloudTable->rowForInfo();
        if (r >= 0)
            next = infoOf (cloudTable->entryAt (r));
        else
            next.placeholder = "Point at a firmware, or select one, to see it here";
    }
    else
    {
        int r = rowUnderMouse (table);
        if (r < 0)
            r = table.getLastRowSelected();
        if (juce::isPositiveAndBelow (r, numRows()))
            next = infoOf (itemAt (r));
        else
            next.placeholder = numRows() > 0 ? "Point at a firmware, or select one, to see it here" : juce::String();
    }
    if (next.key == infoKey && ! infoStale)
        return;
    infoKey = next.key;
    infoStale = false;
    info.show (std::move (next));
}

InfoPanel::Info LibraryView::infoOf (const Item& it) const
{
    InfoPanel::Info i;
    i.key = "local:" + it.file.getFullPathName();
    i.title = it.name();
    i.version = it.version();
    i.author = it.listing.author;
    i.about = it.listing.about;
    i.category = FirmwareLibrary::categoryName (it.shownCategory())
               + (it.subcategory.isNotEmpty() ? juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + it.subcategory : juce::String());
    i.rating = it.rating;
    i.note = it.isPackage ? howListed (it) : juce::String ("Not an FM-1 update package");
    i.facts.push_back ({ "File", it.file.getFileName() });
    i.facts.push_back ({ "Folder", shortPath (it.file.getParentDirectory()) });
    i.facts.push_back ({ "Added", formatDay (it.added) });
    i.facts.push_back ({ "Updated", formatDay (it.modified) });
    if (it.size > 0)
        i.facts.push_back ({ "Size", juce::File::descriptionOfSizeInBytes (it.size) });
    if (it.identity.isNotEmpty())
        i.facts.push_back ({ "Identity", it.identity });
    if (it.url.isNotEmpty())
    {
        i.facts.push_back ({ "Source", it.url.fromFirstOccurrenceOf ("://", false, false) });
        i.link = it.url;
    }
    if (it.isPackage && it.digest.isNotEmpty())
    {
        i.shotsKey = it.digest;
        i.package = it.file;
    }
    if (cloud != nullptr)
        i.card = cloud->cardOf (it);
    return i;
}

InfoPanel::Info LibraryView::infoOf (const CloudCatalog::Entry& e) const
{
    InfoPanel::Info i;
    i.key = "cloud:" + e.key();
    i.title = e.name;
    i.version = e.version;
    i.author = e.author;
    i.about = e.about;
    const auto st = cloud->statusOf (e);
    i.category = FirmwareLibrary::categoryName (cloud->listingOf (e).category);
    i.note = st == CloudCatalog::Status::failed ? "Its download failed: " + cloud->failureOf (e)
           : st == CloudCatalog::Status::installerOnly ? juce::String ("It installs from its own page, with no package to download")
                                                           : juce::String();
    const auto file = cloud->fileOf (e);
    i.facts.push_back ({ "Status", CloudView::statusText (st) });
    if (file != juce::File())
        i.facts.push_back ({ "In library", file.getFileName() });
    if (e.license.isNotEmpty())
        i.facts.push_back ({ "License", e.license });
    if (e.hub.isNotEmpty())
        i.facts.push_back ({ "Hub", e.hubPage().fromFirstOccurrenceOf ("://", false, false) });
    if (e.repo.isNotEmpty())
    {
        i.facts.push_back ({ "Project", e.repo.fromFirstOccurrenceOf ("://", false, false) });
        i.link = e.repo;
    }

    // its screens by its package's digest: the one in the library has them if it was shown there
    i.shotsKey = CloudCatalog::digestOf (e);
    if (file != juce::File())
    {
        if (i.shotsKey.isEmpty())
            for (const auto& it : items)
                if (it.file == file)
                    i.shotsKey = it.digest;
        i.package = file;
    }
    if (i.shotsKey.isEmpty() && e.downloadable())
        i.shotsKey = "cloud-" + e.key();
    if (file == juce::File() && e.downloadable())
        i.fetch = [c = cloud, e] (juce::MemoryBlock& out) { return c->fetchBytes (e, out); };
    i.card = cloud->cardOf (e);
    return i;
}

void LibraryView::mouseMove (const juce::MouseEvent& e)
{
    const bool over = isCloudShown() && pathArea.contains (e.getPosition());
    if (over != overSource)
    {
        overSource = over;
        setMouseCursor (over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint (pathArea);
    }
}

void LibraryView::mouseDown (const juce::MouseEvent& e)
{
    if (isCloudShown() && pathArea.contains (e.getPosition()))
        juce::URL (CloudCatalog::kSourcePage).launchInDefaultBrowser();
}

//==============================================================================
juce::String LibraryView::formatDate (juce::Time t)
{
    if (t.toMilliseconds() == 0)
        return {};
    const int h = t.getHoursInAmPmFormat();
    return t.getMonthName (true) + " " + juce::String (t.getDayOfMonth()) + ", " + juce::String (t.getYear()) + "  "
         + juce::String (h) + ":" + juce::String (t.getMinutes()).paddedLeft ('0', 2) + (t.isAfternoon() ? " PM" : " AM");
}

juce::String LibraryView::formatDay (juce::Time t)
{
    if (t.toMilliseconds() == 0)
        return {};
    return t.formatted ("%Y-%m-%d");
}

juce::String LibraryView::openableUrl (const juce::String& url)
{
    const auto u = url.trim();
    return u.startsWithIgnoreCase ("https://") || u.startsWithIgnoreCase ("http://") ? u : juce::String();
}

juce::String LibraryView::shortPath (const juce::File& f) { return shortPathText (f); }
juce::String LibraryView::revealText() { return revealMenuText(); }
juce::Path LibraryView::linkIcon (juce::Rectangle<float> r) { return linkIconPath (r); }

juce::String LibraryView::middleEllipsis (const juce::String& text, const juce::Font& font, float width)
{
    auto widthOf = [&font] (const juce::String& s) { return juce::GlyphArrangement::getStringWidth (font, s); };
    if (widthOf (text) <= width)
        return text;
    const juce::String dots = juce::String::charToString ((juce::juce_wchar) 0x2026);
    auto cut = [&text, &dots] (int keep) { return text.substring (0, (keep + 1) / 2) + dots + text.substring (text.length() - keep / 2); };
    int lo = 0, hi = text.length() - 1;
    while (lo < hi)
    {
        const int mid = (lo + hi + 1) / 2;
        if (widthOf (cut (mid)) <= width)
            lo = mid;
        else
            hi = mid - 1;
    }
    return cut (lo);
}

//==============================================================================
void LibraryView::changeListenerCallback (juce::ChangeBroadcaster* from)
{
    infoStale = true;
    if (cloud != nullptr && from == cloud)
    {
        refreshButton.setEnabled (! cloud->isFetching());
        return;
    }
    refill();
}

void LibraryView::refill()
{
    const auto selected = selectedFiles();
    items = library.items();
    rows.clear();

    juce::StringArray words;
    words.addTokens (search.getText(), " ", "\"");
    words.removeEmptyStrings();
    for (int i = 0; i < (int) items.size(); ++i)
    {
        const auto& it = items[(size_t) i];
        const bool matches = std::all_of (words.begin(), words.end(), [&it] (const juce::String& w)
        {
            return it.name().containsIgnoreCase (w) || it.file.getFileName().containsIgnoreCase (w)
                || it.listing.author.containsIgnoreCase (w) || it.listing.about.containsIgnoreCase (w)
                || FirmwareLibrary::categoryName (it.shownCategory()).containsIgnoreCase (w) || it.subcategory.containsIgnoreCase (w);
        });
        if (matches)
            rows.push_back (i);
    }
    resort();

    table.updateContent();
    juce::SparseSet<int> sel;
    for (const auto& f : selected)
        if (const int r = rowOf (f); r >= 0)
            sel.addRange ({ r, r + 1 });
    table.setSelectedRows (sel, juce::dontSendNotification);
    table.repaint();
    updateStatus();
}

void LibraryView::resort()
{
    auto text = [] (const Item& it, int column) -> juce::String
    {
        switch (column)
        {
            case fileName:     return it.file.getFileName();
            case firmwareName: return it.name();
            case version:      return it.version();
            case author:       return it.listing.author;
            case description:  return it.listing.about;
            case category:     return FirmwareLibrary::categoryName (it.shownCategory());
            case subcategory:  return it.subcategory;
            case sourceUrl:    return it.url;
            default:           return {};
        }
    };
    auto unset = [] (const Item& it, int column)
    {
        return (column == category && it.shownCategory() == Category::none) || (column == rating && it.rating == 0)
            || (column == sourceUrl && it.url.isEmpty()) || (column == version && it.version().isEmpty())
            || (column == author && it.listing.author.isEmpty()) || (column == description && it.listing.about.isEmpty())
            || (column == subcategory && it.subcategory.isEmpty());
    };

    const int col = sortColumn;
    const int dir = sortForwards ? 1 : -1;
    std::stable_sort (rows.begin(), rows.end(), [&] (int ia, int ib)
    {
        const auto& a = items[(size_t) ia];
        const auto& b = items[(size_t) ib];
        // what has no value comes last whichever way it sorts
        if (unset (a, col) != unset (b, col))
            return unset (b, col);
        int c = 0;
        switch (col)
        {
            case dateAdded:   c = (int) juce::jlimit<juce::int64> (-1, 1, a.added.toMilliseconds() - b.added.toMilliseconds()); break;
            case dateUpdated: c = (int) juce::jlimit<juce::int64> (-1, 1, a.modified.toMilliseconds() - b.modified.toMilliseconds()); break;
            case rating:      c = a.rating - b.rating; break;
            default:          c = text (a, col).compareNatural (text (b, col)); break;
        }
        if (c != 0)
            return c * dir < 0;
        // within a category its sub categories (none last), then the names A to Z
        if (col == category)
        {
            if (a.subcategory.isEmpty() != b.subcategory.isEmpty())
                return b.subcategory.isEmpty();
            if (const int s = a.subcategory.compareNatural (b.subcategory); s != 0)
                return s < 0;
        }
        if (col == category || col == subcategory)
            if (const int n = a.name().compareNatural (b.name()); n != 0)
                return n < 0;
        return a.file.getFileName().compareNatural (b.file.getFileName()) < 0;
    });
}

void LibraryView::setSearch (const juce::String& s)
{
    search.setText (s, true);
    refill();
}

void LibraryView::sortBy (Column c, bool forwards)
{
    table.getHeader().setSortColumnId (c, forwards);
    sortOrderChanged (c, forwards);
}

void LibraryView::sortOrderChanged (int column, bool forwards)
{
    sortColumn = column;
    sortForwards = forwards;
    refill();
}

int LibraryView::rowOf (const juce::File& f) const
{
    for (int r = 0; r < numRows(); ++r)
        if (itemAt (r).file == f)
            return r;
    return -1;
}

juce::Array<juce::File> LibraryView::selectedFiles() const
{
    juce::Array<juce::File> files;
    for (int i = 0; i < table.getNumSelectedRows(); ++i)
        if (const int r = table.getSelectedRow (i); juce::isPositiveAndBelow (r, numRows()))
            files.add (itemAt (r).file);
    return files;
}

void LibraryView::updateStatus()
{
    const int total = (int) items.size(), shown = numRows();
    const auto root = library.getRoot();
    if (root == juce::File())
        status = "No folder set";
    else if (library.isRootMissing())
        status = "The folder is not there: " + shortPath (root);
    else if (library.isScanning())
        status = juce::String (total) + " firmware" + (total == 1 ? "" : "s") + "  -  scanning the folder...";
    else if (shown != total)
        status = juce::String (shown) + " of " + juce::String (total) + " firmwares";
    else
        status = juce::String (total) + " firmware" + (total == 1 ? "" : "s");

    if (root == juce::File())
        emptyNote = "Set Path... picks the folder your firmwares (.fwsc) are in. It is searched, subfolders and all.";
    else if (library.isRootMissing())
        emptyNote = "The library's folder is not there. Is its drive connected?";
    else if (total == 0)
        emptyNote = library.isScanning() ? "Looking through the folder..." : "No .fwsc files in this folder or under it.";
    else if (shown == 0)
        emptyNote = "No firmware's name has \"" + search.getText().trim() + "\" in it.";
    else
        emptyNote = {};
    note.setText (emptyNote, juce::dontSendNotification);
    note.setVisible (emptyNote.isNotEmpty() && ! isCloudShown());

    pathButton.setButtonText (root == juce::File() ? "Set Path..." : "Change...");
    rescanButton.setEnabled (root != juce::File() && ! library.isScanning());
    repaint();
}

//==============================================================================
void LibraryView::paint (juce::Graphics& g)
{
    g.fillAll (theme::room);
    auto b = getLocalBounds();
    const auto bar = b.removeFromTop (kBarH);
    g.setColour (theme::header);
    g.fillRect (bar);
    if (info.isVisible())
        b.removeFromRight (info.getWidth());
    const auto foot = b.removeFromBottom (kStatusH);
    g.fillRect (foot);
    g.setColour (juce::Colours::black);
    g.fillRect (bar.withTop (bar.getBottom() - 1));
    g.fillRect (foot.withHeight (1));

    g.setColour (theme::dimSilk);
    g.setFont (theme::font (10.0f, true));
    const bool cloudShown = isCloudShown();
    g.drawText (cloudShown ? "LIST" : "FOLDER", labelArea, juce::Justification::centredLeft);

    const auto font = theme::font (13.0f);
    g.setFont (font);
    if (cloudShown)
    {
        const juce::String source ("github.com/joakimeriksson/fm1-emu  -  web/firmwares.json");
        g.setColour (overSource ? theme::amber : theme::silk);
        g.strokePath (linkIcon (pathArea.toFloat().withWidth (18.0f).reduced (3.0f, 0.0f).withSizeKeepingCentre (12.0f, 12.0f)),
                      juce::PathStrokeType (1.2f));
        g.drawText (middleEllipsis (source, font, (float) pathArea.getWidth() - 22.0f), pathArea.withTrimmedLeft (22),
                    juce::Justification::centredLeft, false);
        return;
    }

    const auto root = library.getRoot();
    g.setColour (root == juce::File() ? theme::dimSilk : library.isRootMissing() ? theme::warn : theme::silk);
    const auto text = root == juce::File() ? juce::String ("none set") : shortPath (root);
    g.drawText (middleEllipsis (text, font, (float) pathArea.getWidth()), pathArea, juce::Justification::centredLeft, false);

    const auto small = theme::font (11.5f);
    const juce::String hint ("Double-click loads into the selected engine  -  right-click for more");
    const auto line = foot.reduced (12, 0);
    g.setFont (small);
    g.setColour (library.isRootMissing() ? theme::warn : theme::dimSilk);
    g.drawText (status, line, juce::Justification::centredLeft, true);
    if (juce::GlyphArrangement::getStringWidth (small, status) + juce::GlyphArrangement::getStringWidth (small, hint) + 40.0f
        < (float) line.getWidth())
    {
        g.setColour (theme::dimSilk.withAlpha (0.7f));
        g.drawText (hint, line, juce::Justification::centredRight, false);
    }
}

void LibraryView::resized()
{
    auto b = getLocalBounds();
    auto bar = b.removeFromTop (kBarH).reduced (8, 7);
    const bool infoFits = library.isInfoShown() && getWidth() >= kInfoMinWidth;
    if (infoFits != info.isVisible())
        infoStale = true;
    info.setVisible (infoFits);
    infoButton.setEnabled (getWidth() >= kInfoMinWidth);
    if (infoFits)
        info.setBounds (b.removeFromRight (InfoPanel::kWidth));
    if (cloudTable != nullptr)
        cloudTable->setBounds (b);
    b.removeFromBottom (kStatusH);
    table.setBounds (b);
    note.setBounds (b.withTrimmedTop (table.getHeaderHeight() + 30).withHeight (60).reduced (24, 0));

    if (cloud != nullptr)
    {
        localTab.setBounds (bar.removeFromLeft (kTabW));
        cloudTab.setBounds (bar.removeFromLeft (kTabW));
        bar.removeFromLeft (14);
    }
    else
    {
        bar.removeFromLeft (4);
    }
    labelArea = bar.removeFromLeft (52);
    infoButton.setBounds (bar.removeFromRight (52));
    bar.removeFromRight (8);
    search.setBounds (bar.removeFromRight (juce::jlimit (140, 260, bar.getWidth() / 3)));
    bar.removeFromRight (16);
    if (isCloudShown())
    {
        refreshButton.setBounds (bar.removeFromRight (70));
    }
    else
    {
        rescanButton.setBounds (bar.removeFromRight (70));
        bar.removeFromRight (6);
        pathButton.setBounds (bar.removeFromRight (90));
    }
    bar.removeFromRight (10);
    pathArea = bar;
}

//==============================================================================
void LibraryView::paintRowBackground (juce::Graphics& g, int row, int w, int h, bool selected)
{
    g.setColour (selected ? theme::amber.withAlpha (0.22f) : (row % 2 == 0 ? rowA : rowB));
    g.fillRect (0, 0, w, h);
    if (selected)
    {
        g.setColour (theme::amber.withAlpha (0.8f));
        g.fillRect (0, 0, 2, h);
    }
}

void LibraryView::paintCell (juce::Graphics& g, int row, int column, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, numRows()))
        return;
    const auto& it = itemAt (row);
    const auto r = juce::Rectangle<int> (w, h).reduced (8, 0);
    g.setFont (theme::font (12.5f));

    switch (column)
    {
        case fileName:
            g.setColour (selected ? juce::Colours::white : theme::silk);
            g.drawText (it.file.getFileName(), r, juce::Justification::centredLeft, true);
            break;
        case firmwareName:
            if (it.file == editingName)
                break;
            g.setFont (theme::font (12.5f, true));
            g.setColour (! it.isPackage ? theme::warn : selected ? juce::Colours::white : theme::silk);
            g.drawText (it.name(), r, juce::Justification::centredLeft, true);
            break;
        case version:
            // the lists' version is the one they give its bytes; its own is only what it is named
            g.setColour (it.listing.exact() ? theme::silk : theme::dimSilk);
            g.drawText (it.version().isNotEmpty() ? it.version() : juce::String ("-"), r, juce::Justification::centredLeft, true);
            break;
        case author:
            g.setColour (theme::silk);
            g.drawText (it.listing.author, r, juce::Justification::centredLeft, true);
            break;
        case description:
            g.setColour (theme::dimSilk);
            g.drawText (it.listing.about, r, juce::Justification::centredLeft, true);
            break;
        case dateAdded:
        case dateUpdated:
            g.setColour (theme::dimSilk);
            g.drawText (formatDay (column == dateAdded ? it.added : it.modified), r, juce::Justification::centredLeft, true);
            break;
        case category:
        {
            const auto shown = it.shownCategory();
            const bool none = shown == Category::none;
            g.setColour (none ? theme::dimSilk.withAlpha (0.6f) : it.category == Category::none ? theme::dimSilk : theme::silk);
            g.drawText (none ? juce::String ("-") : FirmwareLibrary::categoryName (shown), r.withTrimmedRight (12),
                        juce::Justification::centredLeft, true);
            juce::Path arrow;
            const auto a = juce::Rectangle<float> ((float) (w - 16), (float) h * 0.5f - 2.0f, 7.0f, 4.0f);
            arrow.addTriangle (a.getTopLeft(), a.getTopRight(), a.getBottomLeft().withX (a.getCentreX()));
            g.setColour (theme::dimSilk.withAlpha (0.7f));
            g.fillPath (arrow);
            break;
        }
        case subcategory:
        {
            const bool has = ! FirmwareLibrary::subcategoriesOf (it.shownCategory()).isEmpty();
            const bool none = it.subcategory.isEmpty();
            g.setColour (none ? theme::dimSilk.withAlpha (0.6f) : theme::silk);
            g.drawText (none ? juce::String ("-") : it.subcategory, r.withTrimmedRight (has ? 12 : 0), juce::Justification::centredLeft, true);
            if (has)
            {
                juce::Path arrow;
                const auto a = juce::Rectangle<float> ((float) (w - 16), (float) h * 0.5f - 2.0f, 7.0f, 4.0f);
                arrow.addTriangle (a.getTopLeft(), a.getTopRight(), a.getBottomLeft().withX (a.getCentreX()));
                g.setColour (theme::dimSilk.withAlpha (0.7f));
                g.fillPath (arrow);
            }
            break;
        }
        default:
            break;
    }
}

juce::Component* LibraryView::refreshComponentForCell (int row, int column, bool, juce::Component* existing)
{
    if (! juce::isPositiveAndBelow (row, numRows()))
    {
        delete existing;
        return nullptr;
    }
    const auto& it = itemAt (row);

    if (column == rating)
    {
        auto* stars = dynamic_cast<Stars*> (existing);
        if (stars == nullptr)
        {
            delete existing;
            stars = new Stars (*this);
        }
        stars->update (row, it.file, it.rating);
        return stars;
    }
    if (column == sourceUrl)
    {
        auto* link = dynamic_cast<Link*> (existing);
        if (link == nullptr)
        {
            delete existing;
            link = new Link (*this);
        }
        link->update (row, it.file, it.url);
        return link;
    }
    if (column == firmwareName && it.file == editingName)
    {
        if (auto* ed = dynamic_cast<NameEditor*> (existing); ed != nullptr && ed->getFile() == it.file)
            return ed;
        delete existing;
        return new NameEditor (*this, it.file, it.name());
    }
    delete existing;
    return nullptr;
}

juce::String LibraryView::getCellTooltip (int row, int column)
{
    if (! juce::isPositiveAndBelow (row, numRows()))
        return {};
    const auto& it = itemAt (row);
    switch (column)
    {
        case fileName:
            return it.file.getFullPathName();
        case firmwareName:
            if (! it.isPackage)
                return "Not an FM-1 update package";
        {
            juce::StringArray lines;
            if (it.customName.isNotEmpty())
                lines.add ("Your name for it (the lists': " + it.defaultName() + ")");
            if (const auto how = howListed (it); how.isNotEmpty())
                lines.add (how);
            else if (it.customName.isEmpty())
                lines.add ("No list knows it: this is its file's name");
            if (it.parsedName.isNotEmpty())
                lines.add ("It names itself " + it.parsedName);
            if (it.identity.isNotEmpty())
                lines.add ("Package identity " + it.identity);
            return lines.joinIntoString ("\n");
        }
        case version:
            if (it.listing.exact())
                return "The lists' version of these bytes";
            return it.version().isNotEmpty() ? "The version in its file's name" : juce::String();
        case author:
            return howListed (it);
        case description:
            return it.listing.about.isNotEmpty() ? it.listing.about + "\n\n" + howListed (it) : juce::String();
        case dateAdded:
            return "Found in the library " + formatDate (it.added);
        case dateUpdated:
            return "The file last changed " + formatDate (it.modified);
        case category:
            return it.category == Category::none && it.listing.category != Category::none
                ? "The Hub's kind for it. Click to pick its category" : "Click to pick its category";
        case subcategory:
            if (FirmwareLibrary::subcategoriesOf (it.shownCategory()).isEmpty())
                return it.shownCategory() == Category::none ? "Pick a category first"
                                                            : FirmwareLibrary::categoryName (it.shownCategory()) + " has no sub categories yet";
            return "Click to pick what kind of " + FirmwareLibrary::categoryName (it.shownCategory()).toLowerCase() + " it is";
        default:
            return {};
    }
}

juce::String LibraryView::howListed (const FirmwareLibrary::Item& it)
{
    using How = FirmwareLibrary::Listing::How;
    const auto list = it.listing.ref.startsWith ("hub:") ? juce::String ("the FM-1 Firmware Hub") : juce::String ("the Cloud list");
    switch (it.listing.how)
    {
        case How::download: return "Downloaded from " + list + " as " + it.listing.name;
        case How::checksum: return "Byte for byte " + it.listing.name + " " + it.listing.version + " on " + list;
        case How::name:     return "Its file is named for " + it.listing.name + " on " + list;
        case How::none:     break;
    }
    return {};
}

//==============================================================================
void LibraryView::cellClicked (int row, int column, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showRowMenu (row);
    else if (column == category)
        showCategoryMenu (row);
    else if (column == subcategory)
        showSubcategoryMenu (row);
}

void LibraryView::cellDoubleClicked (int row, int column, const juce::MouseEvent&)
{
    if (column != category && column != subcategory && juce::isPositiveAndBelow (row, numRows()))
        loadInto (engines.selected(), itemAt (row).file);
}

void LibraryView::backgroundClicked (const juce::MouseEvent&)
{
    table.deselectAllRows();
}

void LibraryView::returnKeyPressed (int lastRow)
{
    if (juce::isPositiveAndBelow (lastRow, numRows()))
        loadInto (engines.selected(), itemAt (lastRow).file);
}

void LibraryView::deleteKeyPressed (int)
{
    confirmTrash (selectedFiles());
}

juce::var LibraryView::getDragSourceDescription (const juce::SparseSet<int>& selected)
{
    return selected.isEmpty() ? juce::var() : juce::var ("fm1c-library");
}

bool LibraryView::shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails&,
                                                        juce::StringArray& files, bool& canMoveFiles)
{
    for (const auto& f : selectedFiles())
        files.add (f.getFullPathName());
    canMoveFiles = false;
    return ! files.isEmpty();
}

void LibraryView::selectForMenu (int row)
{
    if (! table.isRowSelected (row))
        table.selectRow (row);
}

void LibraryView::loadInto (int slot, const juce::File& file)
{
    if (juce::isPositiveAndBelow (slot, engines.count()))
        engines.load (slot, file);
}

void LibraryView::showRowMenu (int row)
{
    if (! juce::isPositiveAndBelow (row, numRows()))
        return;
    const auto file = itemAt (row).file;
    const auto files = selectedFiles().contains (file) ? selectedFiles() : juce::Array<juce::File> { file };
    juce::Component::SafePointer<LibraryView> safe (this);

    juce::PopupMenu m;
    m.addSectionHeader (itemAt (row).name());
    juce::PopupMenu load;
    for (int i = 0; i < engines.count(); ++i)
        load.addItem (engines.label (i), [safe, i, file] { if (safe) safe->loadInto (i, file); });
    m.addSubMenu ("Load to Engine", load);
    m.addSeparator();

    const auto cats = categoryMenu (files);
    m.addSubMenu (files.size() > 1 ? "Category  (" + juce::String (files.size()) + " firmwares)" : juce::String ("Category"), cats);
    m.addItem ("Edit Firmware Name...", [safe, file] { if (safe) safe->editName (safe->rowOf (file)); });
    m.addItem ("Edit Source URL...", [safe, file] { if (safe) safe->editUrl (safe->rowOf (file)); });
    m.addSeparator();
    m.addItem (revealText(), [file] { file.revealToUser(); });
    m.addItem (files.size() > 1 ? "Delete " + juce::String (files.size()) + " Firmwares..." : juce::String ("Delete..."),
               [safe, files] { if (safe) safe->confirmTrash (files); });

    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

juce::PopupMenu LibraryView::categoryMenu (const juce::Array<juce::File>& files)
{
    juce::Component::SafePointer<LibraryView> safe (this);
    auto all = [this, &files] (Category c, const juce::String* sub)
    {
        for (const auto& f : files)
            if (const int r = rowOf (f); r < 0 || itemAt (r).category != c || (sub != nullptr && itemAt (r).subcategory != *sub))
                return false;
        return true;
    };
    auto set = [safe, files] (Category c, const juce::String& sub)
    {
        return [safe, files, c, sub]
        {
            if (safe)
                for (const auto& f : files)
                    safe->library.setCategory (f, c, sub);
        };
    };

    juce::PopupMenu m;
    m.addItem ("None", true, all (Category::none, nullptr), set (Category::none, {}));
    m.addSeparator();
    for (int i = 0; i < FirmwareLibrary::kCategories; ++i)
    {
        const auto c = (Category) i;
        const auto name = FirmwareLibrary::categoryName (c);
        const auto& subs = FirmwareLibrary::subcategoriesOf (c);
        if (subs.isEmpty())
        {
            m.addItem (name, true, all (c, nullptr), set (c, {}));
            continue;
        }
        juce::PopupMenu sub;
        const juce::String plain;
        sub.addItem (name + " (no sub category)", true, all (c, &plain), set (c, {}));
        sub.addSeparator();
        for (const auto& s : subs)
            sub.addItem (s, true, all (c, &s), set (c, s));
        m.addSubMenu (name, sub, true, juce::Image(), all (c, nullptr));
    }
    return m;
}

void LibraryView::showCategoryMenu (int row)
{
    if (! juce::isPositiveAndBelow (row, numRows()))
        return;
    const auto file = itemAt (row).file;
    const auto files = selectedFiles().contains (file) ? selectedFiles() : juce::Array<juce::File> { file };
    const auto cell = table.getCellPosition (category, row, true);
    categoryMenu (files).showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (table.localAreaToGlobal (cell)));
}

void LibraryView::showSubcategoryMenu (int row)
{
    if (! juce::isPositiveAndBelow (row, numRows()))
        return;
    const auto& it = itemAt (row);
    const auto file = it.file;
    const auto files = selectedFiles().contains (file) ? selectedFiles() : juce::Array<juce::File> { file };
    const auto shown = it.shownCategory();
    const auto& subs = FirmwareLibrary::subcategoriesOf (shown);
    juce::Component::SafePointer<LibraryView> safe (this);

    juce::PopupMenu m;
    if (subs.isEmpty())
    {
        m.addItem (shown == Category::none ? juce::String ("Pick a category first")
                                           : FirmwareLibrary::categoryName (shown) + " has no sub categories yet", false, false, [] {});
    }
    else
    {
        m.addSectionHeader (FirmwareLibrary::categoryName (shown));
        auto add = [&] (const juce::String& text, const juce::String& sub)
        {
            const bool ticked = std::all_of (files.begin(), files.end(), [this, &sub] (const juce::File& f)
            {
                const int r = rowOf (f);
                return r >= 0 && itemAt (r).subcategory == sub;
            });
            m.addItem (text, true, ticked, [safe, files, sub]
            {
                if (safe)
                    for (const auto& f : files)
                        safe->library.setSubcategory (f, sub);
            });
        };
        add ("None", {});
        m.addSeparator();
        for (const auto& s : subs)
            add (s, s);
    }
    const auto cell = table.getCellPosition (subcategory, row, true);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (table.localAreaToGlobal (cell)));
}

void LibraryView::editName (int row)
{
    if (! juce::isPositiveAndBelow (row, numRows()))
        return;
    editingName = itemAt (row).file;
    table.scrollToEnsureRowIsOnscreen (row);
    table.updateContent();
}

void LibraryView::editUrl (int row)
{
    if (! juce::isPositiveAndBelow (row, numRows()))
        return;
    table.scrollToEnsureRowIsOnscreen (row);
    if (auto* link = dynamic_cast<Link*> (table.getCellComponent (sourceUrl, row)))
        link->beginEdit();
}

void LibraryView::confirmTrash (const juce::Array<juce::File>& files)
{
    if (files.isEmpty())
        return;
    const auto what = files.size() == 1 ? "\"" + files[0].getFileName() + "\"" : juce::String (files.size()) + " firmwares";
    const auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
        juce::MessageBoxIconType::WarningIcon, "Delete " + what + "?",
        juce::String (files.size() == 1 ? "The file moves" : "The files move")
            + " to the Trash, and the library forgets the rating, category and source URL.",
        "Delete", "Cancel", this);
    juce::Component::SafePointer<LibraryView> safe (this);
    confirmBox = juce::AlertWindow::showScopedAsync (options, [safe, files] (int result)
    {
        if (safe == nullptr || result != 1)
            return;
        juce::StringArray failed;
        for (const auto& f : files)
            if (! safe->library.trash (f))
                failed.add (f.getFileName());
        if (! failed.isEmpty())
            juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                                                                                  "Could not delete",
                                                                                  failed.joinIntoString ("\n"), {},
                                                                                  safe.getComponent()),
                                          nullptr);
    });
}

void LibraryView::choosePath()
{
    const auto root = library.getRoot();
    chooser = std::make_unique<juce::FileChooser> (
        "The folder your firmwares are in (searched with its subfolders)",
        root.isDirectory() ? root : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    juce::Component::SafePointer<LibraryView> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe] (const juce::FileChooser& fc)
    {
        const auto dir = fc.getResult();
        if (safe != nullptr && dir.isDirectory())
            safe->library.setRoot (dir);
    });
}

//==============================================================================
LibraryWindow::LibraryWindow (FirmwareLibrary& lib, CloudCatalog* cloud, LibraryView::Engines engines, ScreenCache* screens)
    : juce::DocumentWindow ("Firmware Library", theme::room, juce::DocumentWindow::allButtons, true), library (lib)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new LibraryView (lib, std::move (engines), cloud, screens), false);
    setResizable (true, false);
    setResizeLimits (560, 260, 16000, 16000);
    if (! restoreWindowStateFromString (lib.getWindowState()))
        centreWithSize (1180, 640);
    restored = true;
    startTimer (250);
    timerCallback();
}

LibraryWindow::~LibraryWindow()
{
    stopTimer();
    keepState();
    view().saveColumns();
}

void LibraryWindow::show()
{
    setVisible (true);
    setMinimised (false);
    toFront (true);
    view().refreshFromLibrary();
}

void LibraryWindow::closeButtonPressed()
{
    keepState();
    view().saveColumns();
    library.flush();
    setVisible (false);
}

void LibraryWindow::moved()
{
    juce::DocumentWindow::moved();
    keepState();
}

void LibraryWindow::resized()
{
    juce::DocumentWindow::resized();
    keepState();
}

void LibraryWindow::keepState()
{
    if (restored && isOnDesktop() && ! isMinimised())
        library.setWindowState (getWindowStateAsString());
}

void LibraryWindow::timerCallback()
{
    // float over the host's own windows while it is the application in front, not over others'
    const bool front = juce::Process::isForegroundProcess();
    if (front != isAlwaysOnTop())
        setAlwaysOnTop (front);
}
} // namespace fm1c
