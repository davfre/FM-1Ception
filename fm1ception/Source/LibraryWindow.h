#pragma once

#include <JuceHeader.h>
#include "CloudView.h"
#include "InfoPanel.h"

namespace fm1c
{
//==============================================================================
// The firmware library (MENU > Firmware Library), in a window of its own: the
// folder with its Set Path... / Change... button, a search over the names, and
// a table of every .fwsc under the folder. Click a column's header to sort by
// it, again to reverse; the table scrolls both ways when the window is small.
//
// Right-click a firmware: Load to Engine, its category, edit its name or its
// source URL, reveal it, delete it (to the trash). Double-click it (or Return)
// to load it into the selected engine, or drag it onto an engine. A star sets
// its rating (the same star again clears it).
//
// LOCAL | CLOUD at the left of the bar: CLOUD shows fm1-emu's list of community
// firmwares instead (CloudView), to download into the folder. INFO shows the
// panel at the right (InfoPanel): the firmware under the mouse, else the one
// selected, with its screens.
//==============================================================================
class LibraryView : public juce::Component,
                    public juce::DragAndDropContainer,
                    private juce::TableListBoxModel,
                    private juce::ChangeListener,
                    private juce::Timer
{
public:
    using Engines = LibraryEngines;

    enum Column { fileName = 1, firmwareName, dateAdded, dateUpdated, category, rating, sourceUrl, version, author, description, subcategory };

    /** cloud: the Cloud list, nullptr for none (no LOCAL | CLOUD); screens: where the info panel's
        come from, nullptr for none. */
    LibraryView (FirmwareLibrary&, Engines, CloudCatalog* cloud = nullptr, ScreenCache* screens = nullptr);
    ~LibraryView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    /** CLOUD rather than LOCAL (fetching the list if this process has not yet). */
    void showCloud (bool);
    bool isCloudShown() const { return cloudTable != nullptr && cloudTable->isVisible(); }
    CloudView* cloudView() { return cloudTable.get(); }

    /** The rows as shown, after the search and the sort. */
    int numRows() const { return (int) rows.size(); }
    const FirmwareLibrary::Item& itemAt (int row) const { return items[(size_t) rows[(size_t) row]]; }
    void setSearch (const juce::String&);
    void sortBy (Column, bool forwards);
    void saveColumns();
    /** What the table shows now, then the folder looked through again. */
    void refreshFromLibrary();

    /** "Oct 31, 2026  6:50 AM": the tooltips'. */
    static juce::String formatDate (juce::Time);
    /** "2026-10-31": the table's. */
    static juce::String formatDay (juce::Time);
    /** The row of `table` under the mouse, -1 if none is. */
    static int rowUnderMouse (const juce::TableListBox&);

    /** The info panel, shown (and wide enough to be). */
    void showInfo (bool);
    bool isInfoShown() const { return info.isVisible(); }
    InfoPanel& infoPanel() { return info; }
    /** What the panel shows of one of the library's, and of one of the Cloud's. */
    InfoPanel::Info infoOf (const FirmwareLibrary::Item&) const;
    InfoPanel::Info infoOf (const CloudCatalog::Entry&) const;
    /** The panel brought up to the row under the mouse (else the one selected): its timer's. */
    void updateInfo();
    /** http(s) only: what a source URL opens, "" for anything else. */
    static juce::String openableUrl (const juce::String&);
    /** `text` cut to `width` in the middle, as Finder does a long path. */
    static juce::String middleEllipsis (const juce::String& text, const juce::Font&, float width);
    /** The path with ~ for the home folder. */
    static juce::String shortPath (const juce::File&);
    /** How the Cloud's lists came to describe it ("" if they don't). */
    static juce::String howListed (const FirmwareLibrary::Item&);
    static juce::String revealText();
    /** A box with an arrow out of its corner: opens in the browser. */
    static juce::Path linkIcon (juce::Rectangle<float>);

private:
    class Stars;
    class Link;
    class NameEditor;

    // TableListBoxModel
    int getNumRows() override { return numRows(); }
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int w, int h, bool selected) override;
    juce::Component* refreshComponentForCell (int row, int column, bool selected, juce::Component* existing) override;
    void cellClicked (int row, int column, const juce::MouseEvent&) override;
    void cellDoubleClicked (int row, int column, const juce::MouseEvent&) override;
    void backgroundClicked (const juce::MouseEvent&) override;
    void sortOrderChanged (int column, bool forwards) override;
    juce::String getCellTooltip (int row, int column) override;
    void deleteKeyPressed (int) override;
    void returnKeyPressed (int) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>&) override;
    bool shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails&, juce::StringArray& files,
                                               bool& canMoveFiles) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override { updateInfo(); }
    void refill();
    void resort();
    int rowOf (const juce::File&) const;
    juce::Array<juce::File> selectedFiles() const;
    /** The clicked row joins the selection unless it is in it already. */
    void selectForMenu (int row);
    void showRowMenu (int row);
    void showCategoryMenu (int row);
    void showSubcategoryMenu (int row);
    /** None, each category, and in a category with sub categories each of those: for `files`. */
    juce::PopupMenu categoryMenu (const juce::Array<juce::File>& files);
    void loadInto (int slot, const juce::File&);
    void confirmTrash (const juce::Array<juce::File>&);
    void editName (int row);
    void editUrl (int row);
    void choosePath();
    void updateStatus();

    FirmwareLibrary& library;
    Engines engines;
    CloudCatalog* const cloud;
    juce::LookAndFeel_V4 lnf;
    juce::TextButton pathButton, rescanButton { "Rescan" };
    juce::TextButton localTab { "LOCAL" }, cloudTab { "CLOUD" }, refreshButton { "Refresh" }, infoButton { "INFO" };
    std::unique_ptr<CloudView> cloudTable;
    juce::TextEditor search;
    juce::TableListBox table { "Firmware Library", this };
    juce::Label note;
    InfoPanel info;
    juce::String infoKey;
    bool infoStale = true;
    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ScopedMessageBox confirmBox;

    std::vector<FirmwareLibrary::Item> items;
    std::vector<int> rows;
    int sortColumn = fileName;
    bool sortForwards = true;
    juce::File editingName;
    juce::String status, emptyNote;

    static constexpr int kBarH = 40, kStatusH = 22, kTabW = 58;
    /** Narrower than this, the window has no room for the info panel. */
    static constexpr int kInfoMinWidth = 760;
    juce::Rectangle<int> labelArea, pathArea;
    bool overSource = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryView)
};

//==============================================================================
/** The library's window. It floats over the host's windows while the host is the application in
    front, as the host's own plug-in windows do, and closing it only hides it. */
class LibraryWindow : public juce::DocumentWindow,
                      private juce::Timer
{
public:
    LibraryWindow (FirmwareLibrary&, CloudCatalog*, LibraryView::Engines, ScreenCache* = nullptr);
    ~LibraryWindow() override;

    void show();
    void closeButtonPressed() override;
    void moved() override;
    void resized() override;

    LibraryView& view() { return *static_cast<LibraryView*> (getContentComponent()); }

private:
    void timerCallback() override;
    void keepState();

    FirmwareLibrary& library;
    bool restored = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryWindow)
};
} // namespace fm1c
