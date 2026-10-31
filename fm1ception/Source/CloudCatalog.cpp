#include "CloudCatalog.h"
#include "fm1ception_engine.h"
#include <set>

namespace fm1c
{
namespace
{
constexpr size_t kMaxList = 4u << 20, kMaxHub = 16u << 20, kMaxPage = 16u << 20, kMaxPackage = 32u << 20;
constexpr int kTimeoutMs = 20000;

juce::String hostOf (const juce::URL& u)
{
    return u.isLocalFile() ? juce::String ("this computer") : u.getDomain();
}

/** fm1e_probe_package's digest of it (the first half of its SHA-256), "" if it is no package. */
juce::String packageDigest (const juce::MemoryBlock& b)
{
    char identity[64] {}, name[256] {}, digest[64] {};
    if (fm1e_probe_package ((const uint8_t*) b.getData(), b.getSize(), identity, sizeof (identity), name, sizeof (name),
                            digest, sizeof (digest)) != 0)
        return {};
    return juce::String::fromUTF8 (digest);
}

bool isPathChar (juce::juce_wchar c)
{
    return juce::CharacterFunctions::isLetterOrDigit (c) || c == '_' || c == '.' || c == '/' || c == '-';
}

bool isBase64Char (juce::juce_wchar c)
{
    return juce::CharacterFunctions::isLetterOrDigit (c) || c == '+' || c == '/' || c == '=';
}
} // namespace

//==============================================================================
juce::String CloudCatalog::Entry::fileName() const
{
    return juce::File::createLegalFileName (title()).trim() + FirmwareLibrary::kSuffix;
}

juce::String CloudCatalog::Entry::hubPage() const
{
    return hub.isEmpty() ? juce::String() : "https://thefirmwarehub.com/#fw-" + juce::URL::addEscapeChars (hub, true);
}

juce::File CloudCatalog::defaultCache()
{
    return FirmwareLibrary::defaultDatabase().getSiblingFile ("CloudCatalog.json");
}

bool CloudCatalog::parse (const juce::String& json, std::vector<Entry>& out, juce::String& error)
{
    out.clear();
    juce::var v;
    if (const auto r = juce::JSON::parse (json, v); r.failed() || ! v.isObject())
    {
        error = "The list is not JSON" + (r.failed() ? ": " + r.getErrorMessage() : juce::String());
        return false;
    }
    const auto* firmwares = v["firmwares"].getArray();
    if (firmwares == nullptr)
    {
        error = "The list has no firmwares in it";
        return false;
    }
    std::set<juce::String> keys;
    for (const auto& e : *firmwares)
    {
        if (! e.isObject())
            continue;
        auto text = [&e] (const char* name) { return e[name].isString() ? e[name].toString().trim() : juce::String(); };
        Entry x;
        x.id = text ("id");
        x.name = text ("name");
        x.version = text ("version");
        x.author = text ("author");
        x.license = text ("license");
        x.about = text ("about");
        x.repo = text ("repo");
        x.installer = text ("installer");
        x.hub = text ("hub");
        x.embedded = text ("embedded");
        x.bringYourOwn = (bool) e.getProperty ("byo", false);
        x.package = text ("package");
        if (x.package.isEmpty())
            x.package = text ("download");
        if (x.id.isEmpty() || x.name.isEmpty() || ! keys.insert (x.key()).second)
            continue;
        out.push_back (std::move (x));
    }
    if (out.empty())
    {
        error = "The list has no firmwares in it";
        return false;
    }
    error = {};
    return true;
}

bool CloudCatalog::parseHub (const juce::String& json, const juce::URL& at, HubIndex& out, juce::String& error)
{
    out.clear();
    juce::var v;
    const auto r = juce::JSON::parse (json, v);
    const auto* firmwares = v["firmwares"].getArray();
    if (r.failed() || firmwares == nullptr)
    {
        error = "The Hub's catalog is not one";
        return false;
    }
    auto text = [] (const juce::var& x) { return x.isString() || x.isInt() || x.isDouble() ? x.toString().trim() : juce::String(); };
    for (const auto& e : *firmwares)
    {
        const auto id = text (e["id"]);
        if (id.isEmpty())
            continue;
        HubPackage p;
        if (const auto pkg = text (e["pkg"]); pkg.isNotEmpty())
            p.url = resolve (at, pkg);
        p.version = text (e["version"]);
        p.sha256 = text (e["sha256"]).toLowerCase();
        p.name = text (e["name"]);
        p.author = text (e["author"]);
        p.about = text (e["what"]);
        const auto repo = text (e["repo"]);
        p.repo = repo.contains ("://") ? repo : repo.isNotEmpty() ? "https://github.com/" + repo : text (e["site"]);
        if (const auto card = text (e["card"]); card.isNotEmpty())
            p.card = resolve (at, card);
        const auto& kind = e["kind"];
        p.category = categoryOfKind (kind.isArray() && kind.size() > 0 ? kind[0].toString() : text (kind),
                                     (bool) e.getProperty ("ambient", false));
        out[id] = std::move (p);
    }
    error = {};
    return true;
}

void CloudCatalog::applyHub (std::vector<Entry>& entries, const HubIndex& index)
{
    for (auto& e : entries)
    {
        if (e.hub.isEmpty())
            continue;
        const auto it = index.find (e.hub);
        if (it == index.end() || it->second.url.isEmpty())
            continue;
        e.package = it->second.url;
        if (it->second.version.isNotEmpty())
            e.version = it->second.version;
        e.sha256 = it->second.sha256;
    }
}

juce::String CloudCatalog::resolve (const juce::URL& base, const juce::String& path)
{
    if (path.contains ("://"))
        return path;
    const auto b = base.toString (false).upToFirstOccurrenceOf ("#", false, false);
    const auto scheme = b.upToFirstOccurrenceOf ("://", false, false);
    if (path.startsWith ("//"))
        return scheme + ":" + path;
    if (path.startsWith ("/"))
        return scheme + "://" + base.getDomain() + path;
    auto rel = path;
    while (rel.startsWith ("./"))
        rel = rel.substring (2);
    return b.upToLastOccurrenceOf ("/", true, false) + rel;
}

juce::StringArray CloudCatalog::packagesNamedIn (const juce::String& text)
{
    juce::StringArray names;
    const auto t = text.toStdString();
    auto lowerAt = [&t] (size_t i) { return (char) std::tolower ((unsigned char) t[i]); };
    for (size_t at = 0; at + 5 <= t.size(); ++at)
    {
        if (t[at] != '.' || lowerAt (at + 1) != 'f' || lowerAt (at + 2) != 'w' || lowerAt (at + 3) != 's' || lowerAt (at + 4) != 'c')
            continue;
        const size_t end = at + 5;
        if (end < t.size() && (std::isalnum ((unsigned char) t[end]) || t[end] == '_' || t[end] == '-'))
            continue;
        size_t start = at;
        while (start > 0 && isPathChar ((juce::juce_wchar) (unsigned char) t[start - 1]))
            --start;
        const auto name = juce::String (t.substr (start, end - start));
        const auto file = name.fromLastOccurrenceOf ("/", false, false);
        if (file.length() > 5 && ! file.equalsIgnoreCase ("FM-1.fwsc"))
            names.addIfNotAlreadyThere (name);
    }
    return names;
}

juce::String CloudCatalog::embeddedIn (const juce::String& text, const juce::String& variable)
{
    const auto t = text.toStdString(), v = variable.toStdString();
    if (v.empty())
        return {};
    for (auto at = t.find (v); at != std::string::npos; at = t.find (v, at + v.size()))
    {
        auto p = at + v.size();
        auto skipSpace = [&t, &p] { while (p < t.size() && std::isspace ((unsigned char) t[p])) ++p; };
        skipSpace();
        if (p >= t.size() || t[p] != '=')
            continue;
        ++p;
        skipSpace();
        if (p >= t.size() || (t[p] != '"' && t[p] != '\''))
            continue;
        const char quote = t[p++];
        const auto start = p;
        while (p < t.size() && isBase64Char ((juce::juce_wchar) (unsigned char) t[p]))
            ++p;
        if (p < t.size() && t[p] == quote && p > start)
            return juce::String (t.substr (start, p - start));
    }
    return {};
}

juce::StringArray CloudCatalog::scriptsIn (const juce::String& text)
{
    juce::StringArray srcs;
    const auto t = text.toStdString();
    for (auto at = t.find ("src=\""); at != std::string::npos; at = t.find ("src=\"", at + 5))
    {
        const auto end = t.find ('"', at + 5);
        if (end == std::string::npos)
            break;
        const auto src = juce::String (t.substr (at + 5, end - at - 5));
        const auto path = src.upToFirstOccurrenceOf ("?", false, false);
        if (path.endsWithIgnoreCase (".js") || path.endsWithIgnoreCase (".mjs"))
            srcs.addIfNotAlreadyThere (src);
    }
    return srcs;
}

//==============================================================================
CloudCatalog::CloudCatalog (FirmwareLibrary& lib, juce::File c, juce::URL l, juce::URL h)
    : library (lib), cache (std::move (c)), source (std::move (l)), hubSource (std::move (h))
{
    loadCache();
    reindex();
    describeLibrary();
    library.addChangeListener (this);
}

CloudCatalog::~CloudCatalog()
{
    quitting = true;
    pool.removeAllJobs (true, 30000);
    cancelPendingUpdate();
    library.removeChangeListener (this);
}

void CloudCatalog::loadCache()
{
    if (! cache.existsAsFile())
        return;
    juce::String error;
    if (! hubSource.isEmpty() && hubCache().existsAsFile())
        parseHub (hubCache().loadFileAsString(), hubSource, hubIndex, error);
    std::vector<Entry> parsed;
    if (parse (cache.loadFileAsString(), parsed, error))
    {
        applyHub (parsed, hubIndex);
        list = std::move (parsed);
        fetched = cache.getLastModificationTime();
    }
}

const CloudCatalog::Entry* CloudCatalog::find (const juce::String& key) const
{
    for (const auto& e : list)
        if (e.key() == key)
            return &e;
    return nullptr;
}

bool CloudCatalog::fetchable (const juce::String& url) const
{
    if (url.startsWithIgnoreCase ("https://") || url.startsWithIgnoreCase ("http://"))
        return true;
    // a list on disk (a test's) may point at packages on disk; one from the network may not
    return source.isLocalFile() && url.startsWithIgnoreCase ("file://");
}

juce::String CloudCatalog::fetch (const juce::URL& url, juce::MemoryBlock& out, size_t maxBytes, const std::atomic<bool>& quit)
{
    int status = 0;
    auto in = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                         .withConnectionTimeoutMs (kTimeoutMs)
                                         .withNumRedirectsToFollow (8)
                                         .withExtraHeaders ("User-Agent: FM-1Ception")
                                         .withStatusCode (&status));
    if (in == nullptr)
        return "Could not reach " + hostOf (url);
    if (! url.isLocalFile() && (status < 200 || status >= 300))
        return hostOf (url) + " answered " + juce::String (status);

    out.reset();
    juce::HeapBlock<char> buffer (65536);
    for (;;)
    {
        if (quit)
            return "Stopped";
        const int n = in->read (buffer.get(), 65536);
        if (n <= 0)
            break;
        out.append (buffer.get(), (size_t) n);
        if (out.getSize() > maxBytes)
            return "What " + hostOf (url) + " sent is far too big";
    }
    if (out.getSize() == 0)
        return hostOf (url) + " sent nothing";
    return {};
}

juce::String CloudCatalog::fetchBytes (const Entry& e, juce::MemoryBlock& out) const
{
    Got got;
    const auto error = fetchPackage (e, got);
    out = std::move (got.bytes);
    return error;
}

juce::String CloudCatalog::digestOf (const Entry& e)
{
    return e.sha256.length() >= 32 ? e.sha256.substring (0, 32).toLowerCase() : juce::String();
}

juce::String CloudCatalog::cardOf (const Entry& e) const
{
    const auto it = hubIndex.find (e.hub);
    return it != hubIndex.end() ? it->second.card : juce::String();
}

juce::String CloudCatalog::cardOf (const FirmwareLibrary::Item& item) const
{
    const auto& ref = item.listing.ref;
    const auto id = ref.fromFirstOccurrenceOf (":", false, false);
    if (ref.startsWith ("list:"))
    {
        const auto e = entryById.find (id);
        return e != entryById.end() ? cardOf (list[e->second]) : juce::String();
    }
    const auto it = ref.startsWith ("hub:") ? hubIndex.find (id) : hubIndex.end();
    return it != hubIndex.end() ? it->second.card : juce::String();
}

juce::String CloudCatalog::fetchPackage (const Entry& e, Got& got) const
{
    juce::StringArray why;
    auto tryUrl = [&] (const juce::String& url)
    {
        if (! fetchable (url))
        {
            why.add ("Its package is not on the web: " + url);
            return false;
        }
        juce::MemoryBlock bytes;
        if (const auto error = fetch (juce::URL (url), bytes, kMaxPackage, quitting); error.isNotEmpty())
        {
            why.add (error);
            return false;
        }
        if (packageDigest (bytes).isEmpty())
        {
            why.add ("What " + hostOf (juce::URL (url)) + " sent is not an FM-1 firmware package");
            return false;
        }
        got.bytes = std::move (bytes);
        got.url = url;
        return true;
    };
    auto page = [&] (const juce::String& url, juce::String& text)
    {
        juce::MemoryBlock bytes;
        const auto error = fetchable (url) ? fetch (juce::URL (url), bytes, kMaxPage, quitting) : "Not on the web: " + url;
        if (error.isNotEmpty())
            why.add (error);
        text = bytes.toString();
        return error.isEmpty();
    };

    // April OS: its installer page keeps it, base64
    if (e.embedded.isNotEmpty() && e.installer.isNotEmpty())
    {
        if (juce::String text; page (e.installer, text))
        {
            juce::MemoryOutputStream out;
            const auto b64 = embeddedIn (text, e.embedded);
            if (b64.isNotEmpty() && juce::Base64::convertFromBase64 (out, b64) && packageDigest (out.getMemoryBlock()).isNotEmpty())
            {
                got.bytes = out.getMemoryBlock();
                got.url = e.installer;
                return {};
            }
            why.add (hostOf (juce::URL (e.installer)) + "'s page keeps no package in " + e.embedded);
        }
    }

    if (e.package.isNotEmpty() && tryUrl (e.package))
    {
        got.sha256 = e.sha256;
        return {};
    }

    // gone from the Hub since the catalog was read: a newer version replaced it
    if (e.hub.isNotEmpty() && ! hubSource.isEmpty())
    {
        juce::MemoryBlock bytes;
        HubIndex index;
        juce::String error;
        if (fetch (hubSource, bytes, kMaxHub, quitting).isEmpty() && parseHub (bytes.toString(), hubSource, index, error))
            if (const auto it = index.find (e.hub);
                it != index.end() && it->second.url.isNotEmpty() && it->second.url != e.package && tryUrl (it->second.url))
            {
                got.version = it->second.version;
                got.sha256 = it->second.sha256;
                return {};
            }
    }

    // the .fwsc its installer page links now, in the page or in a script it loads
    if (e.installer.isNotEmpty() && e.embedded.isEmpty())
    {
        if (juce::String text; page (e.installer, text))
        {
            auto names = packagesNamedIn (text);
            juce::URL base (e.installer);
            if (names.isEmpty())
            {
                for (const auto& src : scriptsIn (text))
                {
                    const auto script = resolve (base, src);
                    if (juce::String js; page (script, js))
                        if (names = packagesNamedIn (js); ! names.isEmpty())
                        {
                            base = juce::URL (script);
                            break;
                        }
                }
            }
            for (const auto& n : names)
                if (tryUrl (resolve (base, n)))
                    return {};
            if (names.isEmpty())
                why.add (hostOf (juce::URL (e.installer)) + "'s installer page names no package");
        }
    }

    if (why.isEmpty())
        return "There is no package to download: it installs from its own page";
    why.removeDuplicates (false);
    return why.joinIntoString ("; then ");
}

void CloudCatalog::post (std::function<void()> f)
{
    {
        const juce::ScopedLock sl (postLock);
        posted.push_back (std::move (f));
    }
    triggerAsyncUpdate();
}

void CloudCatalog::handleAsyncUpdate()
{
    std::vector<std::function<void()>> now;
    {
        const juce::ScopedLock sl (postLock);
        now.swap (posted);
    }
    for (auto& f : now)
        f();
}

bool CloudCatalog::waitForIdle (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (inFlight > 0 && juce::Time::getMillisecondCounter() < end)
    {
        handleUpdateNowIfNeeded();
        if (inFlight > 0)
            juce::Thread::sleep (5);
    }
    return inFlight == 0;
}

//==============================================================================
void CloudCatalog::refresh()
{
    if (fetching)
        return;
    fetching = true;
    fetchedThisRun = true;
    ++inFlight;
    sendChangeMessage();
    pool.addJob ([this, url = source, hubUrl = hubSource]
    {
        juce::MemoryBlock bytes, hubBytes;
        auto error = fetch (url, bytes, kMaxList, quitting);
        juce::String hubFetchError;
        if (! hubUrl.isEmpty())
            hubFetchError = fetch (hubUrl, hubBytes, kMaxHub, quitting);
        post ([this, json = bytes.toString(), error, hubJson = hubBytes.toString(), hubFetchError]
        {
            --inFlight;
            fetching = false;
            fetchError = error;
            hubError = hubFetchError;
            HubIndex index;
            juce::String hubParseError;
            const bool hubRead = ! hubSource.isEmpty() && hubFetchError.isEmpty()
                              && parseHub (hubJson, hubSource, index, hubParseError);
            if (hubRead)
            {
                hubIndex = std::move (index);
                keepHub();
            }
            else if (hubError.isEmpty() && ! hubSource.isEmpty())
            {
                hubError = hubParseError;
            }
            if (error.isEmpty())
                takeList (json, hubRead ? &hubIndex : nullptr);
            sendChangeMessage();
        });
    });
}

void CloudCatalog::keepHub() const
{
    // kept small: what applyHub uses
    juce::Array<juce::var> kept;
    for (const auto& [id, p] : hubIndex)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", id);
        o->setProperty ("pkg", p.url);
        o->setProperty ("version", p.version);
        o->setProperty ("sha256", p.sha256);
        o->setProperty ("name", p.name);
        o->setProperty ("author", p.author);
        o->setProperty ("what", p.about);
        o->setProperty ("repo", p.repo);
        if (p.card.isNotEmpty())
            o->setProperty ("card", p.card);
        const char* const kinds[] { "synth", "groove", "ambient", "game", "other" };
        if (p.category != FirmwareLibrary::Category::none)
            o->setProperty ("kind", kinds[(int) p.category]);
        if (p.category == FirmwareLibrary::Category::ambience)
            o->setProperty ("ambient", true);
        kept.add (juce::var (o));
    }
    auto* top = new juce::DynamicObject();
    top->setProperty ("firmwares", kept);
    if (hubCache().getParentDirectory().createDirectory())
        hubCache().replaceWithText (juce::JSON::toString (juce::var (top), true));
}

void CloudCatalog::refreshOnce()
{
    if (! fetchedThisRun)
        refresh();
}

void CloudCatalog::takeList (const juce::String& json, const HubIndex* hub)
{
    std::vector<Entry> parsed;
    juce::String error;
    if (! parse (json, parsed, error))
    {
        fetchError = error;
        return;
    }
    applyHub (parsed, hub != nullptr ? *hub : hubIndex);
    list = std::move (parsed);
    fetched = juce::Time::getCurrentTime();
    if (cache.getParentDirectory().createDirectory())
        cache.replaceWithText (json);
    reindex();
    describeLibrary();
}

//==============================================================================
void CloudCatalog::changeListenerCallback (juce::ChangeBroadcaster*)
{
    reindex();
    describeLibrary();
    sendChangeMessage();
}

FirmwareLibrary::Category CloudCatalog::categoryOfKind (const juce::String& kind, bool ambient)
{
    using C = FirmwareLibrary::Category;
    if (ambient || kind.equalsIgnoreCase ("ambient"))
        return C::ambience;
    if (kind.equalsIgnoreCase ("synth") || kind.equalsIgnoreCase ("chords"))
        return C::synth;
    if (kind.equalsIgnoreCase ("groove") || kind.equalsIgnoreCase ("drums") || kind.equalsIgnoreCase ("tracker"))
        return C::groovebox;
    if (kind.equalsIgnoreCase ("game"))
        return C::game;
    if (kind.isNotEmpty())
        return C::misc;
    return C::none;
}

void CloudCatalog::rebuildNames()
{
    entryById.clear();
    entryByHub.clear();
    hubBySha.clear();
    nameRefs.clear();
    for (size_t i = 0; i < list.size(); ++i)
    {
        entryById.emplace (list[i].id, i);
        if (list[i].hub.isNotEmpty())
            entryByHub.emplace (list[i].hub, i);
        nameRefs.emplace (FirmwareLibrary::normalise (list[i].name), "list:" + list[i].id);
    }
    for (const auto& [id, p] : hubIndex)
    {
        if (p.sha256.length() >= 32)
            hubBySha.emplace (p.sha256.substring (0, 32), id);
        nameRefs.emplace (FirmwareLibrary::normalise (p.name), "hub:" + id);
    }
    nameRefs.erase (juce::String());
}

FirmwareLibrary::Listing CloudCatalog::listingOf (const Entry& e) const
{
    FirmwareLibrary::Listing l;
    l.ref = "list:" + e.id;
    l.name = e.name;
    l.version = e.version;
    l.author = e.author;
    l.about = e.about;
    l.repo = e.repo;
    if (const auto it = hubIndex.find (e.hub); it != hubIndex.end())
        l.category = it->second.category;
    return l;
}

FirmwareLibrary::Listing CloudCatalog::listingOf (const juce::String& hubId, const HubPackage& p) const
{
    if (const auto it = entryByHub.find (hubId); it != entryByHub.end())
        return listingOf (list[it->second]);
    FirmwareLibrary::Listing l;
    l.ref = "hub:" + hubId;
    l.name = p.name.isNotEmpty() ? p.name : hubId;
    l.version = p.version;
    l.author = p.author;
    l.about = p.about;
    l.repo = p.repo;
    l.category = p.category;
    return l;
}

FirmwareLibrary::Listing CloudCatalog::describe (const FirmwareLibrary::Item& it) const
{
    using How = FirmwareLibrary::Listing::How;
    if (it.cloudId.isNotEmpty())
        if (const auto e = entryById.find (it.cloudId.upToLastOccurrenceOf ("@", false, false)); e != entryById.end())
        {
            auto l = listingOf (list[e->second]);
            l.how = How::download;
            l.version = it.cloudId.fromLastOccurrenceOf ("@", false, false);
            return l;
        }
    if (it.digest.length() >= 32)
        if (const auto h = hubBySha.find (it.digest.toLowerCase().substring (0, 32)); h != hubBySha.end())
        {
            const auto& p = hubIndex.at (h->second);
            auto l = listingOf (h->second, p);
            l.how = How::checksum;
            l.version = p.version;
            return l;
        }

    // its file's name: the longest run of its first words that is a firmware's name
    juce::StringArray words;
    words.addTokens (it.file.getFileNameWithoutExtension(), " _-()[]", {});
    words.removeEmptyStrings();
    for (int n = words.size(); n > 0; --n)
    {
        const auto key = FirmwareLibrary::normalise (words.joinIntoString ("", 0, n));
        if (key.length() < 3)
            break;
        const auto ref = nameRefs.find (key);
        if (ref == nameRefs.end())
            continue;
        FirmwareLibrary::Listing l;
        const auto id = ref->second.fromFirstOccurrenceOf (":", false, false);
        if (ref->second.startsWith ("list:"))
            l = listingOf (list[entryById.at (id)]);
        else
            l = listingOf (id, hubIndex.at (id));
        l.how = How::name;
        l.version = {};
        return l;
    }
    return {};
}

void CloudCatalog::describeLibrary()
{
    // with no list yet (none kept, none fetched), what the library was told before stands
    if (list.empty() && hubIndex.empty())
        return;
    library.describe ([this] (const FirmwareLibrary::Item& it) { return describe (it); });
}

void CloudCatalog::reindex()
{
    rebuildNames();
    byKey.clear();
    versionsById.clear();
    for (const auto& it : library.items())
    {
        if (it.cloudId.isEmpty())
            continue;
        byKey[it.cloudId] = it.file;
        versionsById[it.cloudId.upToLastOccurrenceOf ("@", false, false)].addIfNotAlreadyThere (
            it.cloudId.fromLastOccurrenceOf ("@", false, false));
    }
}

CloudCatalog::Status CloudCatalog::statusOf (const Entry& e) const
{
    if (downloading.count (e.key()) != 0)
        return Status::downloading;
    if (const auto it = byKey.find (e.key()); it != byKey.end() && it->second.existsAsFile())
        return Status::inLibrary;
    if (! e.downloadable())
        return Status::installerOnly;
    if (failures.count (e.key()) != 0)
        return Status::failed;
    if (versionsById.count (e.id) != 0)
        return Status::update;
    return Status::get;
}

juce::String CloudCatalog::failureOf (const Entry& e) const
{
    const auto it = failures.find (e.key());
    return it != failures.end() ? it->second : juce::String();
}

juce::File CloudCatalog::fileOf (const Entry& e) const
{
    const auto it = byKey.find (e.key());
    return it != byKey.end() && it->second.existsAsFile() ? it->second : juce::File();
}

juce::File CloudCatalog::downloadFolder() const
{
    const auto root = library.getRoot();
    if (root == juce::File() || library.isRootMissing())
        return {};
    return root.getChildFile (kFolderName);
}

void CloudCatalog::download (const Entry& e, Done then)
{
    auto fail = [&] (const juce::String& why)
    {
        failures[e.key()] = why;
        sendChangeMessage();
        if (then != nullptr)
            then ({}, why);
    };
    if (! e.downloadable())
        return fail ("There is no package to download: it installs from its own page");
    if (const auto it = downloading.find (e.key()); it != downloading.end())
    {
        it->second.push_back (std::move (then));
        return;
    }

    if (library.getRoot() == juce::File())
    {
        const auto root = FirmwareLibrary::defaultRoot();
        root.createDirectory();
        library.setRoot (root);
    }
    const auto folder = downloadFolder();
    if (folder == juce::File())
        return fail ("The library's folder is not there: " + library.getRoot().getFullPathName());
    if (! folder.createDirectory())
        return fail ("Could not make the folder " + folder.getFullPathName());

    failures.erase (e.key());
    downloading[e.key()].push_back (std::move (then));
    ++inFlight;
    sendChangeMessage();

    pool.addJob ([this, e, folder]
    {
        Got got;
        auto error = fetchPackage (e, got);
        auto as = e;
        if (got.version.isNotEmpty())
            as.version = got.version;
        const auto target = folder.getChildFile (as.fileName());
        if (error.isEmpty() && got.sha256.length() >= 32
            && ! packageDigest (got.bytes).equalsIgnoreCase (got.sha256.substring (0, 32)))
            error = "What " + hostOf (juce::URL (got.url)) + " sent is not the package the Hub lists (its checksum differs)";
        if (error.isEmpty())
        {
            juce::TemporaryFile tmp (target);
            if (! tmp.getFile().replaceWithData (got.bytes.getData(), got.bytes.getSize()) || ! tmp.overwriteTargetFileWithTemporary())
                error = "Could not write " + target.getFullPathName();
        }
        post ([this, e, target, version = got.version, url = got.url, sha = got.sha256, error]
        {
            finished (e, target, version, url, sha, error);
        });
    });
}

void CloudCatalog::finished (const Entry& e, const juce::File& file, const juce::String& version, const juce::String& url,
                             const juce::String& sha256, const juce::String& error)
{
    --inFlight;
    auto waiting = std::move (downloading[e.key()]);
    downloading.erase (e.key());
    auto key = e.key();
    if (error.isEmpty() && version.isNotEmpty() && version != e.version)
    {
        // the Hub's newer one came: the entry is that version now
        for (auto& x : list)
            if (x.key() == e.key())
            {
                x.version = version;
                x.package = url;
                x.sha256 = sha256;
                key = x.key();
            }
        if (e.hub.isNotEmpty() && ! hubSource.isEmpty())
        {
            auto& p = hubIndex[e.hub];
            p.url = url;
            p.version = version;
            p.sha256 = sha256;
            keepHub();
        }
    }
    if (error.isEmpty())
        library.adopt (file, key, e.repo);
    else
        failures[key] = error;
    reindex();
    describeLibrary();
    sendChangeMessage();
    for (auto& then : waiting)
        if (then != nullptr)
            then (error.isEmpty() ? file : juce::File(), error);
}
} // namespace fm1c
