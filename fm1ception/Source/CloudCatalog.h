#pragma once

#include <JuceHeader.h>
#include <map>
#include "FirmwareLibrary.h"
#include "ScreenCache.h"

namespace fm1c
{
//==============================================================================
// The library's Cloud: the community firmwares fm1-emu lists for its web page
// (github.com/joakimeriksson/fm1-emu, web/firmwares.json: the FM-1 Firmware
// Hub's list and fm1-emu's own), fetched in the background and kept beside the
// library's file for when there is no network:
//
//   macOS    ~/Library/Application Support/Xfer Records/FM-1Ception/CloudCatalog.json
//
// The Hub drops a firmware's old package when a new version comes, and
// fm1-emu's list is only remade now and then, so the Hub's own catalog
// (thefirmwarehub.com/catalog.json) is fetched with it: an entry the Hub lists
// gets the Hub's current version, package and checksum. A download that still
// fails tries what fm1-emu's page does (web/fwsc.js fetchPackage): the Hub's
// catalog again, then the .fwsc the entry's installer page links, or the one
// it keeps inside it (April OS).
//
// A download goes into the library's folder, in Cloud/ (a library with no
// folder gets ~/Documents/FM-1Ception/Firmwares), and joins the library at
// once with the entry's project page for its source URL. What comes back has
// to be an FM-1 package (fm1e_probe_package), and the Hub's own if it gave a
// checksum: a server's error page is not written anywhere.
//
// Message thread only (the fetches have threads of their own).
//==============================================================================
class CloudCatalog : public juce::ChangeBroadcaster,
                     private juce::ChangeListener,
                     private juce::AsyncUpdater
{
public:
    static constexpr const char* kListUrl = "https://raw.githubusercontent.com/joakimeriksson/fm1-emu/master/web/firmwares.json";
    static constexpr const char* kSourcePage = "https://github.com/joakimeriksson/fm1-emu";
    static constexpr const char* kHubCatalogUrl = "https://thefirmwarehub.com/catalog.json";
    static constexpr const char* kFolderName = "Cloud";

    struct Entry
    {
        juce::String id, name, version, author, license, about, repo, package, installer, hub;
        /** The variable its installer page keeps the package in, base64 (April OS). */
        juce::String embedded;
        /** The Hub's SHA-256 of `package` (hex), when the Hub gave it. */
        juce::String sha256;
        /** M-VAVE's stock firmware: fm1-emu links its maker's download rather than hosting it. */
        bool bringYourOwn = false;

        /** id@version: what the library's Item::cloudId holds. */
        juce::String key() const { return id + "@" + version; }
        juce::String title() const { return version.isEmpty() ? name : name + " " + version; }
        juce::String fileName() const;
        /** Its card on the FM-1 Firmware Hub, "" if it has none. */
        juce::String hubPage() const;
        bool downloadable() const { return package.isNotEmpty() || (embedded.isNotEmpty() && installer.isNotEmpty()); }
    };

    /** One of the Hub's firmwares, and its current package ("" if it lists none). */
    struct HubPackage
    {
        juce::String url, version, sha256;
        juce::String name, author, about, repo;
        /** Its artwork on the Hub (an image's URL), "" for none. */
        juce::String card;
        FirmwareLibrary::Category category = FirmwareLibrary::Category::none;
    };
    using HubIndex = std::map<juce::String, HubPackage>;

    /** list: where the list is fetched from (a file:// URL is allowed, and then so are packages on disk);
        hub: the Hub's catalog (an empty URL for none). */
    CloudCatalog (FirmwareLibrary&, juce::File cache, juce::URL list = juce::URL (kListUrl),
                  juce::URL hub = juce::URL (kHubCatalogUrl));
    ~CloudCatalog() override;

    static juce::File defaultCache();
    /** Reads fm1-emu's firmwares.json; false (and why) if it is not one. */
    static bool parse (const juce::String& json, std::vector<Entry>&, juce::String& error);
    /** Reads the Hub's catalog.json, its packages' paths taken from `at` (where it was fetched). */
    static bool parseHub (const juce::String& json, const juce::URL& at, HubIndex&, juce::String& error);
    /** Each entry the Hub lists, with the Hub's package and version now. */
    static void applyHub (std::vector<Entry>&, const HubIndex&);
    /** The Hub's kind ("synth", "groove", ...; ambient) as a category. */
    static FirmwareLibrary::Category categoryOfKind (const juce::String& kind, bool ambient);
    /** What the lists say of a firmware in the library: the entry it was downloaded as, else the Hub
        firmware whose package it is byte for byte, else the one its file is named for. */
    FirmwareLibrary::Listing describe (const FirmwareLibrary::Item&) const;
    /** The .fwsc paths a page or script names (M-VAVE's own FM-1.fwsc not among them). */
    static juce::StringArray packagesNamedIn (const juce::String& text);
    /** The scripts a page loads (src="....js"). */
    static juce::StringArray scriptsIn (const juce::String& text);
    /** The base64 text a page keeps in `variable = "..."`, "" if none. */
    static juce::String embeddedIn (const juce::String& text, const juce::String& variable);
    /** `path` (absolute, //host/..., /..., or relative) from the page at `base`. */
    static juce::String resolve (const juce::URL& base, const juce::String& path);

    const std::vector<Entry>& entries() const { return list; }
    const Entry* find (const juce::String& key) const;
    /** When the list shown was fetched (the kept copy's time until a fetch comes back). */
    juce::Time getFetched() const { return fetched; }
    bool isFetching() const { return fetching; }
    /** Why the last fetch failed, "" if it did not. */
    juce::String getFetchError() const { return fetchError; }
    /** Why the Hub's catalog could not be had with it, "" if it was. */
    juce::String getHubError() const { return hubError; }
    /** Fetches the list again in the background. */
    void refresh();
    /** Fetches it if this process has not yet. */
    void refreshOnce();

    enum class Status { get, downloading, inLibrary, update, installerOnly, failed };
    Status statusOf (const Entry&) const;
    /** The last download's failure for it. */
    juce::String failureOf (const Entry&) const;
    /** Its download in the library, if it is there. */
    juce::File fileOf (const Entry&) const;
    /** Where downloads go: Cloud/ in the library's folder (none if that is not there). */
    juce::File downloadFolder() const;

    using Done = std::function<void (const juce::File&, const juce::String& error)>;
    /** Downloads it into the library (setting the library's folder if it has none); `then` gets
        its file, or "" and why not. */
    void download (const Entry&, Done then = {});

    /** Its package, fetched as download() fetches it but kept nowhere (for a look at its screens
        before it is downloaded). Blocks: a background thread's. "" or why none came. */
    juce::String fetchBytes (const Entry&, juce::MemoryBlock&) const;
    /** Its package's digest as the library reads one (FirmwareLibrary::Item::digest), from the
        Hub's checksum: "" if the Hub gives none. */
    static juce::String digestOf (const Entry&);
    /** The Hub's artwork for it (an image's URL), "" if it has none. */
    juce::String cardOf (const Entry&) const;
    juce::String cardOf (const FirmwareLibrary::Item&) const;
    /** What the lists say of it, as the library is told of what it has (FirmwareLibrary::describe). */
    FirmwareLibrary::Listing listingOf (const Entry&) const;

    /** Waits here for the fetches, taking in what they bring: for a test, with no message loop. */
    bool waitForIdle (int timeoutMs);

private:
    struct Got
    {
        juce::MemoryBlock bytes;
        juce::String url, version, sha256;
    };
    static juce::String fetch (const juce::URL&, juce::MemoryBlock&, size_t maxBytes, const std::atomic<bool>& quit);
    /** The package for it by any of the ways there are, "" or why none came. */
    juce::String fetchPackage (const Entry&, Got&) const;
    bool fetchable (const juce::String& url) const;
    juce::File hubCache() const { return cache.getSiblingFile (cache.getFileNameWithoutExtension() + "-hub.json"); }
    void keepHub() const;
    void rebuildNames();
    void describeLibrary();
    FirmwareLibrary::Listing listingOf (const juce::String& hubId, const HubPackage&) const;
    void loadCache();
    void takeList (const juce::String& json, const HubIndex*);
    void finished (const Entry&, const juce::File&, const juce::String& version, const juce::String& url,
                   const juce::String& sha256, const juce::String& error);
    void reindex();
    void post (std::function<void()>);
    void handleAsyncUpdate() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    FirmwareLibrary& library;
    const juce::File cache;
    const juce::URL source, hubSource;
    std::vector<Entry> list;
    HubIndex hubIndex;
    juce::Time fetched;
    juce::String fetchError, hubError;
    bool fetching = false, fetchedThisRun = false;
    int inFlight = 0;

    // for describe: fm1-emu's entries by id and by their Hub id, the Hub's by checksum, both by name
    std::map<juce::String, size_t> entryById, entryByHub;
    std::map<juce::String, juce::String> hubBySha;
    std::map<juce::String, juce::String> nameRefs;
    std::map<juce::String, juce::File> byKey;
    std::map<juce::String, juce::StringArray> versionsById;
    std::map<juce::String, std::vector<Done>> downloading;
    std::map<juce::String, juce::String> failures;

    std::atomic<bool> quitting { false };
    juce::CriticalSection postLock;
    std::vector<std::function<void()>> posted;
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withThreadName ("FM-1Ception Cloud").withNumberOfThreads (3) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CloudCatalog)
};

/** The process's library and its Cloud: hold a juce::SharedResourcePointer<SharedFirmwareLibrary>. */
struct SharedFirmwareLibrary
{
    FirmwareLibrary library { FirmwareLibrary::defaultDatabase() };
    CloudCatalog cloud { library, CloudCatalog::defaultCache() };
    ScreenCache screens { ScreenCache::defaultFolder() };
};
} // namespace fm1c
