#pragma once

#include <JuceHeader.h>
#include <map>

namespace fm1c
{
//==============================================================================
// The firmware library (MENU > Firmware Library): every .fwsc under a folder the
// user picks, found by a recursive scan, and what the user says of each (its
// category, rating, source URL, a name of their own) in one JSON file beside
// the per-firmware settings (FirmwarePrefs.h):
//
//   macOS    ~/Library/Application Support/Xfer Records/FM-1Ception/FirmwareLibrary.json
//   Windows  %APPDATA%\Xfer Records\FM-1Ception\FirmwareLibrary.json
//
// A scan only reads a file it has not seen at that size and date; what it
// reads (the name the firmware gives itself, a digest of its bytes) comes from
// fm1e_probe_package. A file moved or renamed within the folder keeps what was
// said of it, found again by its digest; one deleted takes it with it. A folder
// that is not there (a drive not mounted) forgets nothing.
//
// Message thread only (the scan has a thread of its own). Every plug-in
// instance in a process shares one, with its Cloud list: SharedFirmwareLibrary
// (CloudCatalog.h).
//==============================================================================
class FirmwareLibrary : public juce::ChangeBroadcaster,
                        private juce::AsyncUpdater,
                        private juce::Timer
{
public:
    /** mutant.cloud's tags (firmware.json), with Synth and Groovebox first. */
    enum class Category { none = -1, synth, groovebox, drums, sampler, tracker, chords, generative, game, midi, experimental, utility };
    static constexpr int kCategories = 11;
    static constexpr int kMaxRating = 5;
    static juce::String categoryName (Category);
    /** A category's name, or its tag ("midi"); one of before the tags ("Misc") is none. */
    static Category categoryFromName (const juce::String&);
    /** A category's sub categories ("FM", "VA / Retro", ... of Synth), A to Z; most have none. */
    static const juce::StringArray& subcategoriesOf (Category);
    struct Preset
    {
        Category category = Category::none;
        juce::String subcategory;
    };
    /** A category and sub category as they were named before the tags (Ambience, Misc, Novelty and
        Groovebox's Drum Machine and Tracker) as they are now; any other as it is. */
    static Preset upgradeCategory (const juce::String& category, const juce::String& subcategory);
    /** Shown after the name of a firmware others are built on (CloudCatalog::Entry::isBase). */
    static constexpr const char* kBaseMark = " (base)";

    /** What the Cloud's lists (mutant.cloud's, fm1-emu's, the FM-1 Firmware Hub's) say of a firmware,
        kept with it (CloudCatalog::describe): the friendly name, who made it, what it is. */
    struct Listing
    {
        /** How it was found to be the list's: downloaded from it, its bytes a package the lists
            give a checksum for (both: `version` is its), or only its file's name (`version` is not). */
        enum class How { none, download, checksum, name };
        How how = How::none;
        /** "list:<id>" (the lists') or "hub:<id>" (the Hub's alone). */
        juce::String ref, name, version, author, about, repo;
        /** Its primary tag (else the Hub's kind) as a category. */
        Category category = Category::none;
        /** Its tags, the primary first (mutant.cloud's). */
        juce::StringArray tags;
        /** The firmware it is built on ("Felucca"), "" for its own code or none said. */
        juce::String base;
        /** Others are built on it. */
        bool isBase = false;

        bool exact() const { return how == How::download || how == How::checksum; }
        bool operator== (const Listing&) const;
        bool operator!= (const Listing& o) const { return ! operator== (o); }
    };

    struct Item
    {
        juce::File file;
        juce::String identity, parsedName, customName, digest, url;
        juce::int64 size = 0;
        juce::Time modified, added;
        Category category = Category::none;
        /** One of subcategoriesOf (shownCategory()), else "". */
        juce::String subcategory;
        int rating = 0;
        /** fm1e_probe_package read it as an FM-1 package. */
        bool isPackage = true;
        /** The Cloud list's entry it was downloaded as (CloudCatalog::Entry::key), else "". */
        juce::String cloudId;
        Listing listing;
        /** The times it was loaded into an engine (noteLaunch). */
        int launches = 0;

        /** The user's name for it, else the lists', else its own (ownName). */
        juce::String name() const;
        /** name() but the user's. */
        juce::String defaultName() const;
        /** The lists' version when they know its bytes, else its own. */
        juce::String version() const;
        /** The user's category, else the lists'. */
        Category shownCategory() const { return category != Category::none ? category : listing.category; }
        /** Its file's name as a name and version ("Crepuscular Homunculus 0.26.19", "dub-1.4.3"): the
            name it gives itself instead when the file's says no more (FM-1.fwsc, SHRUB.fwsc). Not the
            name it gives itself otherwise: a firmware made from another keeps the other's. */
        std::pair<juce::String, juce::String> ownName() const;
    };

    /** The category and sub category FM-1Ception gives a firmware of this name (CategoryPresets.cpp):
        its lists' name, else its own. A firmware with none of its own gets it when first found, or
        first described as one of those names. */
    static std::optional<Preset> presetOf (const Item&);

    /** "Name 1.2.3", "name-1.2.3", "Name v1.2" as name and version ("" if there is none). */
    static std::pair<juce::String, juce::String> splitVersion (const juce::String&);
    /** Letters and digits, lower case: "FM-1 SID" and "fm1-sid" are one. */
    static juce::String normalise (const juce::String&);

    explicit FirmwareLibrary (juce::File database);
    ~FirmwareLibrary() override;

    static juce::File defaultDatabase();
    /** Somewhere else for defaultDatabase() (and the Cloud's cache beside it), before the first
        SharedFirmwareLibrary: the tests', so they never touch the user's library. */
    static void useDatabase (const juce::File&);
    static constexpr const char* kSuffix = ".fwsc";

    juce::File getRoot() const { return root; }
    /** Sets the folder (and scans it). */
    void setRoot (const juce::File&);
    /** The folder is set but not there. */
    bool isRootMissing() const { return root != juce::File() && ! root.isDirectory(); }

    /** The firmwares under the folder, as the last scan found them. */
    std::vector<Item> items() const;
    int numItems() const;

    /** Looks at the folder again in the background, telling listeners when it is done. */
    void rescan();
    bool isScanning() const { return scanning; }
    /** Waits here for the scan (and any it starts) and takes it in: for a test, with no message loop. */
    bool waitForScan (int timeoutMs);

    void setRating (const juce::File&, int stars);
    /** Its category, with one of its sub categories (anything else is none). */
    void setCategory (const juce::File&, Category, const juce::String& subcategory = {});
    /** One of the sub categories of the category it shows (which becomes its own, if it was the
        lists'), "" for none. */
    void setSubcategory (const juce::File&, const juce::String&);
    void setUrl (const juce::File&, const juce::String&);
    /** Empty: back to the name it gives itself. */
    void setCustomName (const juce::File&, const juce::String&);

    /** Moves the file to the trash and forgets it. */
    bool trash (const juce::File&);

    /** It was loaded into an engine: one more launch for it, else for the firmware in the library
        with its bytes. */
    void noteLaunch (const juce::File&);

    /** Of two firmwares of one name, the newer: 1 if `a` is, -1 if `b` is, 0 if neither is (variants
        of one version: "0.26.0" and "0.26.0-bluetooth"). By version when both have one with a number
        in it (of one kind: a year is not one), a commit's hash after it aside, else by DATE UPDATED;
        the same in both, the one whose file's name sorts first. */
    static int newer (const Item& a, const Item& b);
    struct Older
    {
        juce::File file, keep;
    };
    /** Each firmware with the FIRMWARE NAME of a newer one, with the newest of those to keep: the
        name its lists give its bytes, else its own (not the lists' by its file's name alone). */
    std::vector<Older> olderVersions() const;
    /** Moves `older` to the trash, its launches going to `keep`, and its rating, category and source
        URL where `keep` has none. */
    bool replace (const juce::File& older, const juce::File& keep);

    /** Takes in a file just put under the folder (a Cloud download) now rather than at the next
        scan, as the Cloud entry `cloudId`, with `url` for its source if it has none. */
    void adopt (const juce::File&, const juce::String& cloudId, const juce::String& url);
    /** What the lists say of each firmware (one change message for all of it). A firmware first
        described gets its project for its source URL if it has none. */
    void describe (const std::function<Listing (const Item&)>&);
    /** The folder a library with none set gets: ~/Documents/FM-1Ception/Firmwares. */
    static juce::File defaultRoot();

    /** The library window's place and its columns, kept with the library. */
    juce::String getWindowState() const { return windowState; }
    void setWindowState (const juce::String& s) { if (s != windowState) { windowState = s; markDirty(); } }
    juce::String getColumns() const { return columns; }
    void setColumns (const juce::String& s) { if (s != columns) { columns = s; markDirty(); } }
    juce::String getCloudColumns() const { return cloudColumns; }
    void setCloudColumns (const juce::String& s) { if (s != cloudColumns) { cloudColumns = s; markDirty(); } }
    bool isCloudShown() const { return cloudShown; }
    void setCloudShown (bool b) { if (b != cloudShown) { cloudShown = b; markDirty(); } }
    /** The window's info panel shown (InfoPanel). */
    bool isInfoShown() const { return infoShown; }
    void setInfoShown (bool b) { if (b != infoShown) { infoShown = b; markDirty(); } }

    /** Writes now rather than a moment after the last change. */
    void flush();

    /** What one file is, read: the scan's, and the test's. */
    struct Probe
    {
        juce::String identity, name, digest;
        bool isPackage = false;
    };
    static Probe probe (const juce::File&);

private:
    struct Record : Item
    {
        bool found = false;
        /** Its category was its preset, or the user's: a preset is not given it again. */
        bool seeded = false;
    };
    static bool seed (Record&);
    struct Seen
    {
        juce::int64 size = 0, modified = 0;
        Probe probe;
    };
    struct Found
    {
        juce::File file;
        Seen seen;
    };
    class Scanner;

    void load();
    void save();
    void markDirty();
    Record* find (const juce::File&);
    void edit (const juce::File&, const std::function<bool (Record&)>&);
    void merge (const juce::File& scannedRoot, bool rootThere, std::vector<Found>&&);
    void handleAsyncUpdate() override;
    void timerCallback() override { save(); }

    const juce::File database;
    juce::File root;
    std::vector<Record> records;
    juce::String windowState, columns, cloudColumns;
    juce::Time diskTime;
    bool dirty = false, scanning = false, scanAgain = false, cloudShown = false, infoShown = true;

    std::unique_ptr<Scanner> scanner;
    juce::CriticalSection resultLock;
    struct Result
    {
        juce::File root;
        bool rootThere = false;
        std::vector<Found> found;
    };
    std::optional<Result> result;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FirmwareLibrary)
};
} // namespace fm1c
