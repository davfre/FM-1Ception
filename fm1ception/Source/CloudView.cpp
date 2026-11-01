#include "CloudView.h"
#include "LibraryWindow.h"
#include "Theme.h"

namespace fm1c
{
namespace
{
using Entry = CloudCatalog::Entry;
using Status = CloudCatalog::Status;

const juce::Colour rowA { 0xff1c1d20 }, rowB { 0xff202124 };

juce::String hostOf (const juce::String& url)
{
    auto s = url.fromFirstOccurrenceOf ("://", false, false);
    s = s.upToFirstOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("?", false, false);
    return s.startsWithIgnoreCase ("www.") ? s.substring (4) : s;
}

/** github.com/owner/name for a GitHub page, else the host. */
juce::String projectText (const juce::String& url)
{
    const auto host = hostOf (url);
    if (! host.equalsIgnoreCase ("github.com"))
        return host;
    juce::StringArray parts;
    parts.addTokens (url.fromFirstOccurrenceOf ("github.com/", false, true), "/", {});
    parts.removeEmptyStrings();
    return parts.size() >= 2 ? host + "/" + parts[0] + "/" + parts[1] : host;
}

/** Its tags as the library names them, the primary first. */
juce::String tagsText (const Entry& e)
{
    juce::StringArray names;
    for (const auto& t : e.tags)
    {
        const auto c = FirmwareLibrary::categoryFromName (t);
        names.add (c != FirmwareLibrary::Category::none ? FirmwareLibrary::categoryName (c) : t);
    }
    return names.joinIntoString (", ");
}

juce::String versionText (const Entry& e)
{
    return e.beta && e.version.isNotEmpty() ? e.version + " beta" : e.version;
}

int rank (Status s)
{
    switch (s)
    {
        case Status::update:        return 0;
        case Status::get:           return 1;
        case Status::failed:        return 2;
        case Status::downloading:   return 3;
        case Status::inLibrary:     return 4;
        case Status::installerOnly: return 5;
    }
    return 6;
}
} // namespace

//==============================================================================
CloudView::CloudView (CloudCatalog& c, FirmwareLibrary& lib, LibraryEngines e)
    : cloud (c), library (lib), engines (std::move (e))
{
    auto& h = table.getHeader();
    const int flags = juce::TableHeaderComponent::defaultFlags;
    h.addColumn ("NAME", name, 200, 90, 2000, flags);
    h.addColumn ("VERSION", version, 110, 60, 400, flags);
    h.addColumn ("AUTHOR", author, 140, 70, 400, flags);
    h.addColumn ("STATUS", status, 112, 100, 300, flags);
    h.addColumn ("TAGS", tags, 150, 70, 600, flags);
    h.addColumn ("ABOUT", about, 380, 90, 4000, flags);
    h.addColumn ("LICENSE", license, 130, 70, 600, flags);
    h.addColumn ("PROJECT", project, 210, 90, 2000, flags);
    h.setSortColumnId (name, true);
    h.setStretchToFitActive (false);
    if (const auto saved = library.getCloudColumns(); saved.isNotEmpty())
        h.restoreFromString (saved);
    sortColumn = h.getSortColumnId() != 0 ? h.getSortColumnId() : (int) name;
    sortForwards = h.isSortedForwards();

    table.setRowHeight (24);
    table.setHeaderHeight (26);
    table.setOutlineThickness (0);
    table.getViewport()->setScrollBarThickness (11);
    addAndMakeVisible (table);

    note.setFont (theme::font (14.0f));
    note.setColour (juce::Label::textColourId, theme::dimSilk);
    note.setJustificationType (juce::Justification::centred);
    note.setInterceptsMouseClicks (false, false);
    addChildComponent (note);

    cloud.addChangeListener (this);
    refill();
}

CloudView::~CloudView()
{
    saveColumns();
    cloud.removeChangeListener (this);
}

void CloudView::saveColumns()
{
    library.setCloudColumns (table.getHeader().toString());
}

void CloudView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refill();
}

juce::String CloudView::statusText (Status s)
{
    switch (s)
    {
        case Status::get:           return "GET";
        case Status::downloading:   return "Downloading...";
        case Status::inLibrary:     return "In Library";
        case Status::update:        return "UPDATE";
        case Status::installerOnly: return "Installer only";
        case Status::failed:        return "Failed - Retry";
    }
    return {};
}

//==============================================================================
void CloudView::refill()
{
    juce::StringArray selected;
    for (int i = 0; i < table.getNumSelectedRows(); ++i)
        if (const int r = table.getSelectedRow (i); juce::isPositiveAndBelow (r, numRows()))
            selected.add (entryAt (r).key());

    entries = cloud.entries();
    rows.clear();
    juce::StringArray words;
    words.addTokens (searchText, " ", "\"");
    words.removeEmptyStrings();
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];
        if (std::all_of (words.begin(), words.end(), [&e] (const juce::String& w)
            {
                return e.name.containsIgnoreCase (w) || e.author.containsIgnoreCase (w) || e.about.containsIgnoreCase (w)
                    || e.id.containsIgnoreCase (w) || tagsText (e).containsIgnoreCase (w) || e.base.containsIgnoreCase (w)
                    || (e.isBase && juce::String ("base").startsWithIgnoreCase (w));
            }))
            rows.push_back (i);
    }
    resort();

    table.updateContent();
    juce::SparseSet<int> sel;
    for (int r = 0; r < numRows(); ++r)
        if (selected.contains (entryAt (r).key()))
            sel.addRange ({ r, r + 1 });
    table.setSelectedRows (sel, juce::dontSendNotification);
    table.repaint();
    updateStatus();
}

void CloudView::resort()
{
    auto text = [] (const Entry& e, int column) -> juce::String
    {
        switch (column)
        {
            case name:    return e.name;
            case version: return e.version;
            case tags:    return tagsText (e);
            case author:  return e.author;
            case about:   return e.about;
            case license: return e.license;
            case project: return projectText (e.repo);
            default:      return {};
        }
    };
    const int col = sortColumn, dir = sortForwards ? 1 : -1;
    std::stable_sort (rows.begin(), rows.end(), [&] (int ia, int ib)
    {
        const auto& a = entries[(size_t) ia];
        const auto& b = entries[(size_t) ib];
        // what has no value comes last whichever way it sorts
        if (col != status && text (a, col).isEmpty() != text (b, col).isEmpty())
            return text (b, col).isEmpty();
        const int c = col == status ? rank (cloud.statusOf (a)) - rank (cloud.statusOf (b))
                                    : text (a, col).compareNatural (text (b, col));
        if (c != 0)
            return c * dir < 0;
        return a.name.compareNatural (b.name) < 0;
    });
}

int CloudView::rowForInfo() const
{
    if (const int r = LibraryView::rowUnderMouse (table); r >= 0)
        return r;
    const int r = table.getLastRowSelected();
    return juce::isPositiveAndBelow (r, numRows()) ? r : -1;
}

void CloudView::setSearch (const juce::String& s)
{
    if (s == searchText)
        return;
    searchText = s;
    refill();
}

void CloudView::sortBy (Column c, bool forwards)
{
    table.getHeader().setSortColumnId (c, forwards);
    sortOrderChanged (c, forwards);
}

void CloudView::sortOrderChanged (int column, bool forwards)
{
    sortColumn = column;
    sortForwards = forwards;
    refill();
}

void CloudView::updateStatus()
{
    const int total = (int) entries.size(), shown = numRows();
    const auto when = LibraryView::formatDate (cloud.getFetched());
    const auto error = cloud.getFetchError();
    const juce::String count = juce::String (total) + " firmware" + (total == 1 ? "" : "s");
    if (total == 0)
        statusLine = cloud.isFetching() ? "Fetching the list..." : error.isNotEmpty() ? "Could not fetch the list: " + error : "No list yet";
    else if (cloud.isFetching())
        statusLine = count + "  -  fetching the list again...";
    else if (error.isNotEmpty())
        statusLine = count + " as fetched " + when + "  -  could not fetch it again: " + error;
    else
        statusLine = count + (shown != total ? " (" + juce::String (shown) + " shown)" : juce::String()) + " in mutant.cloud's list, fetched " + when;

    const auto folder = cloud.downloadFolder();
    hintLine = "Downloads go to "
         + (folder != juce::File() ? LibraryView::shortPath (folder) : LibraryView::shortPath (FirmwareLibrary::defaultRoot().getChildFile (CloudCatalog::kFolderName)))
         + "  -  double-click loads into the selected engine";

    juce::String empty;
    if (total == 0)
        empty = cloud.isFetching() ? "Fetching the list of firmwares from mutant.cloud..."
              : error.isNotEmpty() ? "Could not fetch the list of firmwares (" + error + ").  Refresh tries again."
                                   : "Refresh fetches the list of firmwares from mutant.cloud.";
    else if (shown == 0)
        empty = "No firmware has \"" + searchText.trim() + "\" in its name, author, tags or about.";
    note.setText (empty, juce::dontSendNotification);
    note.setVisible (empty.isNotEmpty());
    repaint();
}

//==============================================================================
void CloudView::paint (juce::Graphics& g)
{
    g.fillAll (theme::room);
    const auto foot = getLocalBounds().removeFromBottom (kStatusH);
    g.setColour (theme::header);
    g.fillRect (foot);
    g.setColour (juce::Colours::black);
    g.fillRect (foot.withHeight (1));

    const auto small = theme::font (11.5f);
    const auto line = foot.reduced (12, 0);
    g.setFont (small);
    g.setColour (cloud.getFetchError().isNotEmpty() ? theme::warn : theme::dimSilk);
    g.drawText (statusLine, line, juce::Justification::centredLeft, true);
    if (juce::GlyphArrangement::getStringWidth (small, statusLine) + juce::GlyphArrangement::getStringWidth (small, hintLine) + 40.0f
        < (float) line.getWidth())
    {
        g.setColour (theme::dimSilk.withAlpha (0.7f));
        g.drawText (hintLine, line, juce::Justification::centredRight, false);
    }
}

void CloudView::resized()
{
    auto b = getLocalBounds();
    b.removeFromBottom (kStatusH);
    table.setBounds (b);
    note.setBounds (b.withTrimmedTop (table.getHeaderHeight() + 30).withHeight (60).reduced (24, 0));
}

void CloudView::paintRowBackground (juce::Graphics& g, int row, int w, int h, bool selected)
{
    g.setColour (selected ? theme::amber.withAlpha (0.22f) : (row % 2 == 0 ? rowA : rowB));
    g.fillRect (0, 0, w, h);
    if (selected)
    {
        g.setColour (theme::amber.withAlpha (0.8f));
        g.fillRect (0, 0, 2, h);
    }
}

void CloudView::paintCell (juce::Graphics& g, int row, int column, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, numRows()))
        return;
    const auto& e = entryAt (row);
    const auto r = juce::Rectangle<int> (w, h).reduced (8, 0);
    const auto ink = selected ? juce::Colours::white : theme::silk;
    g.setFont (theme::font (12.5f));

    switch (column)
    {
        case name:
        {
            const auto bold = theme::font (12.5f, true);
            g.setFont (bold);
            g.setColour (ink);
            g.drawText (e.name, r, juce::Justification::centredLeft, true);
            if (e.isBase)
            {
                const auto x = (int) std::ceil (juce::GlyphArrangement::getStringWidth (bold, e.name));
                g.setFont (theme::font (12.5f));
                g.setColour (theme::dimSilk);
                g.drawText (FirmwareLibrary::kBaseMark, r.withTrimmedLeft (x), juce::Justification::centredLeft, true);
            }
            break;
        }
        case version:
        case author:
        case license:
        case tags:
            g.setColour (column == author ? ink : theme::dimSilk);
            g.drawText (column == version ? versionText (e) : column == author ? e.author : column == tags ? tagsText (e) : e.license,
                        r, juce::Justification::centredLeft, true);
            break;
        case about:
            g.setColour (theme::dimSilk);
            g.drawText (e.about, r, juce::Justification::centredLeft, true);
            break;
        case status:
        {
            const auto s = cloud.statusOf (e);
            const auto text = statusText (s);
            const auto box = r.toFloat().withSizeKeepingCentre ((float) r.getWidth(), 18.0f);
            g.setFont (theme::font (11.0f, true));
            if (s == Status::get || s == Status::update)
            {
                const auto pill = box.withWidth (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 22.0f);
                g.setColour (s == Status::update ? theme::amber : theme::amber.withAlpha (0.14f));
                g.fillRoundedRectangle (pill, 9.0f);
                g.setColour (theme::amber);
                g.drawRoundedRectangle (pill.reduced (0.5f), 9.0f, 1.0f);
                g.setColour (s == Status::update ? theme::header : theme::amber);
                g.drawText (text, pill, juce::Justification::centred);
            }
            else
            {
                g.setColour (s == Status::inLibrary ? theme::green : s == Status::failed ? theme::warn : theme::dimSilk);
                g.setFont (theme::font (12.0f, s == Status::inLibrary));
                g.drawText (text, box, juce::Justification::centredLeft, true);
            }
            break;
        }
        case project:
        {
            if (LibraryView::openableUrl (e.repo).isEmpty())
                break;
            auto link = r.toFloat();
            g.setColour (theme::silk);
            g.strokePath (LibraryView::linkIcon (link.removeFromLeft (18.0f).reduced (4.0f)), juce::PathStrokeType (1.2f));
            g.drawText (projectText (e.repo), link.withTrimmedLeft (3.0f), juce::Justification::centredLeft, true);
            break;
        }
        default:
            break;
    }
}

juce::String CloudView::getCellTooltip (int row, int column)
{
    if (! juce::isPositiveAndBelow (row, numRows()))
        return {};
    const auto& e = entryAt (row);
    switch (column)
    {
        case name:
        case about:
            return e.title() + (e.author.isNotEmpty() ? "  by " + e.author : juce::String())
                 + (e.isBase ? "\nA base: other firmwares are built on it" : juce::String())
                 + (e.base.isNotEmpty() ? "\nBuilt on " + e.base : juce::String()) + "\n\n" + e.about;
        case license:
            return e.license;
        case tags:
            return tagsText (e);
        case project:
            return e.repo;
        case status:
            switch (cloud.statusOf (e))
            {
                case Status::get:           return "Download it into the library";
                case Status::update:        return "A newer version of one in the library: download it too";
                case Status::downloading:   return "Downloading from " + hostOf (e.package.isNotEmpty() ? e.package : e.installer);
                case Status::inLibrary:     return cloud.fileOf (e).getFullPathName();
                case Status::installerOnly: return "It installs from its own page, with no package to download: click to open it";
                case Status::failed:        return cloud.failureOf (e) + "\nClick to try again";
            }
            return {};
        default:
            return {};
    }
}

//==============================================================================
void CloudView::open (const juce::String& url)
{
    if (const auto u = LibraryView::openableUrl (url); u.isNotEmpty())
        juce::URL (u).launchInDefaultBrowser();
}

void CloudView::cellClicked (int row, int column, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, numRows()))
        return;
    if (e.mods.isPopupMenu())
    {
        if (! table.isRowSelected (row))
            table.selectRow (row);
        showRowMenu (row);
        return;
    }
    const auto entry = entryAt (row);
    if (column == project)
    {
        open (entry.repo);
    }
    else if (column == status)
    {
        const auto s = cloud.statusOf (entry);
        if (s == Status::get || s == Status::update || s == Status::failed)
            get (entry);
        else if (s == Status::installerOnly)
            open (entry.installer.isNotEmpty() ? entry.installer : entry.repo);
    }
}

void CloudView::cellDoubleClicked (int row, int column, const juce::MouseEvent&)
{
    if (column != status && column != project && juce::isPositiveAndBelow (row, numRows()))
        loadInto (engines.selected(), entryAt (row));
}

void CloudView::backgroundClicked (const juce::MouseEvent&)
{
    table.deselectAllRows();
}

void CloudView::returnKeyPressed (int lastRow)
{
    if (juce::isPositiveAndBelow (lastRow, numRows()))
        loadInto (engines.selected(), entryAt (lastRow));
}

void CloudView::get (const Entry& e)
{
    juce::Component::SafePointer<CloudView> safe (this);
    cloud.download (e, [safe, title = e.title()] (const juce::File& f, const juce::String& error)
    {
        if (safe != nullptr && f == juce::File() && error.isNotEmpty())
            juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                                                                                  "Could not download " + title, error, {},
                                                                                  safe.getComponent()),
                                          nullptr);
    });
}

void CloudView::loadInto (int slot, const Entry& e)
{
    if (! juce::isPositiveAndBelow (slot, engines.count()))
        return;
    if (const auto f = cloud.fileOf (e); f != juce::File())
    {
        engines.load (slot, f);
        return;
    }
    if (cloud.statusOf (e) == Status::installerOnly)
    {
        open (e.installer.isNotEmpty() ? e.installer : e.repo);
        return;
    }
    juce::Component::SafePointer<CloudView> safe (this);
    cloud.download (e, [safe, slot, title = e.title()] (const juce::File& f, const juce::String& error)
    {
        if (safe == nullptr)
            return;
        if (f.existsAsFile() && juce::isPositiveAndBelow (slot, safe->engines.count()))
            safe->engines.load (slot, f);
        else if (error.isNotEmpty())
            juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                                                                                  "Could not download " + title, error, {},
                                                                                  safe.getComponent()),
                                          nullptr);
    });
}

void CloudView::showRowMenu (int row)
{
    if (! juce::isPositiveAndBelow (row, numRows()))
        return;
    const auto e = entryAt (row);
    const auto s = cloud.statusOf (e);
    const auto file = cloud.fileOf (e);
    const bool canLoad = file != juce::File() || (e.downloadable() && s != Status::downloading);
    juce::Component::SafePointer<CloudView> safe (this);

    juce::PopupMenu m;
    m.addSectionHeader (e.title() + (e.author.isNotEmpty() ? "  by " + e.author : juce::String()));
    juce::PopupMenu load;
    for (int i = 0; i < engines.count(); ++i)
        load.addItem (engines.label (i), [safe, i, e] { if (safe) safe->loadInto (i, e); });
    m.addSubMenu (file != juce::File() ? "Load to Engine" : "Download and Load to Engine", load, canLoad);
    if (e.downloadable())
        m.addItem (file != juce::File() ? "Download Again" : "Download to Library", s != Status::downloading,
                   false, [safe, e] { if (safe) safe->get (e); });
    if (file != juce::File())
        m.addItem (LibraryView::revealText(), [file] { file.revealToUser(); });
    m.addSeparator();
    if (LibraryView::openableUrl (e.repo).isNotEmpty())
        m.addItem ("Open Project Page", [url = e.repo] { open (url); });
    if (LibraryView::openableUrl (e.homepage).isNotEmpty() && e.homepage != e.repo)
        m.addItem ("Open Home Page", [url = e.homepage] { open (url); });
    for (const auto& [label, url] : e.links)
        if (LibraryView::openableUrl (url).isNotEmpty() && url != e.installer)
            m.addItem ("Open " + label, [url] { open (url); });
    if (e.hub.isNotEmpty())
        m.addItem ("Open on the FM-1 Firmware Hub", [url = e.hubPage()] { open (url); });
    if (LibraryView::openableUrl (e.installer).isNotEmpty())
        m.addItem ("Open Its Installer Page", [url = e.installer] { open (url); });
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}
} // namespace fm1c
