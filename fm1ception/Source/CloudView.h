#pragma once

#include <JuceHeader.h>
#include "CloudCatalog.h"

namespace fm1c
{
/** What the library's window can do to the plug-in's engines. */
struct LibraryEngines
{
    std::function<int()> count;
    std::function<int()> selected;
    std::function<juce::String (int)> label;
    std::function<void (int, const juce::File&)> load;
};

//==============================================================================
// The library window's CLOUD: the firmwares of fm1-emu's list (CloudCatalog),
// with what each is, who made it, and whether it is in the library yet. GET
// (or UPDATE, for a newer version of one in it) downloads it into the
// library's folder; double-click (or Return) downloads it if need be and loads
// it into the selected engine. Right-click for Load to Engine, its project's
// page, its card on the FM-1 Firmware Hub. The footer says how old the list is.
//==============================================================================
class CloudView : public juce::Component,
                  private juce::TableListBoxModel,
                  private juce::ChangeListener
{
public:
    enum Column { name = 1, version, author, status, about, license, project };

    CloudView (CloudCatalog&, FirmwareLibrary&, LibraryEngines);
    ~CloudView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    int numRows() const { return (int) rows.size(); }
    const CloudCatalog::Entry& entryAt (int row) const { return entries[(size_t) rows[(size_t) row]]; }
    /** Over the name, author, about and id. */
    void setSearch (const juce::String&);
    void sortBy (Column, bool forwards);
    void saveColumns();
    void refill();
    /** The row under the mouse, else the one selected last; -1 for neither. */
    int rowForInfo() const;

    static juce::String statusText (CloudCatalog::Status);

private:
    int getNumRows() override { return numRows(); }
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int w, int h, bool selected) override;
    void cellClicked (int row, int column, const juce::MouseEvent&) override;
    void cellDoubleClicked (int row, int column, const juce::MouseEvent&) override;
    void backgroundClicked (const juce::MouseEvent&) override;
    void returnKeyPressed (int) override;
    void sortOrderChanged (int column, bool forwards) override;
    juce::String getCellTooltip (int row, int column) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void resort();
    void updateStatus();
    void showRowMenu (int row);
    void get (const CloudCatalog::Entry&);
    void loadInto (int slot, const CloudCatalog::Entry&);
    static void open (const juce::String& url);

    CloudCatalog& cloud;
    FirmwareLibrary& library;
    LibraryEngines engines;
    juce::TableListBox table { "Cloud", this };
    juce::Label note;

    std::vector<CloudCatalog::Entry> entries;
    std::vector<int> rows;
    juce::String searchText, statusLine, hintLine;
    int sortColumn = name;
    bool sortForwards = true;

    static constexpr int kStatusH = 22;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CloudView)
};
} // namespace fm1c
