#include "FirmwareLibrary.h"
#include "CategoryPresets.h"
#include "FirmwarePrefs.h"
#include "fm1ception_engine.h"

namespace fm1c
{
namespace
{
const char* const kCategoryNames[FirmwareLibrary::kCategories] { "Synth", "Groovebox", "Ambience", "Game", "Misc" };
constexpr int kVersion = 1;
} // namespace

//==============================================================================
class FirmwareLibrary::Scanner : public juce::Thread
{
public:
    Scanner (FirmwareLibrary& o, juce::File r, std::map<juce::String, Seen> k)
        : juce::Thread ("FM-1Ception library scan"), owner (o), root (std::move (r)), known (std::move (k))
    {
    }

    ~Scanner() override { stopThread (10000); }

    void run() override
    {
        Result r;
        r.root = root;
        r.rootThere = root.isDirectory();
        if (r.rootThere)
        {
            for (const auto& entry : juce::RangedDirectoryIterator (root, true, juce::String ("*") + kSuffix,
                                                                     juce::File::findFiles | juce::File::ignoreHiddenFiles,
                                                                     juce::File::FollowSymlinks::noCycles))
            {
                if (threadShouldExit())
                    return;
                const auto& file = entry.getFile();
                if (entry.isHidden() || file.getFileName().startsWithChar ('.'))
                    continue;
                Found f { file, { entry.getFileSize(), entry.getModificationTime().toMilliseconds(), {} } };
                const auto it = known.find (file.getFullPathName());
                if (it != known.end() && it->second.size == f.seen.size && it->second.modified == f.seen.modified)
                    f.seen.probe = it->second.probe;
                else
                    f.seen.probe = probe (file);
                r.found.push_back (std::move (f));
            }
        }
        const juce::ScopedLock sl (owner.resultLock);
        owner.result = std::move (r);
        owner.triggerAsyncUpdate();
    }

private:
    FirmwareLibrary& owner;
    const juce::File root;
    const std::map<juce::String, Seen> known;
};

//==============================================================================
juce::String FirmwareLibrary::categoryName (Category c)
{
    return c == Category::none ? juce::String() : juce::String (kCategoryNames[(int) c]);
}

const juce::StringArray& FirmwareLibrary::subcategoriesOf (Category c)
{
    static const juce::StringArray synth { "FM", "Inst / Sample", "Model / Vocal", "Modern / WT", "Multi Engine", "Organic / Pad", "VA / Retro" };
    static const juce::StringArray groovebox { "Arranger", "Drum Machine", "Four-Part XOX", "Tracker", "Workstation" };
    static const juce::StringArray ambience { "Generative", "Instrument" };
    static const juce::StringArray game { "Classic", "Music Game", "NES", "Original" };
    static const juce::StringArray misc { "Chord", "Educational", "Novelty / Fun", "Sequencer", "Utility / USB" };
    static const juce::StringArray none;
    switch (c)
    {
        case Category::synth:     return synth;
        case Category::groovebox: return groovebox;
        case Category::ambience:  return ambience;
        case Category::game:      return game;
        case Category::misc:      return misc;
        case Category::none:      break;
    }
    return none;
}

std::optional<FirmwareLibrary::Preset> FirmwareLibrary::presetOf (const Item& it)
{
    static const auto byName = []
    {
        std::map<juce::String, Preset> m;
        for (size_t i = 0; i < kNumCategoryPresets; ++i)
        {
            const auto& p = kCategoryPresets[i];
            const auto c = categoryFromName (juce::String::fromUTF8 (p.category));
            const auto sub = juce::String::fromUTF8 (p.subcategory);
            if (c != Category::none)
                m.emplace (normalise (juce::String::fromUTF8 (p.name)),
                           Preset { c, subcategoriesOf (c).contains (sub) ? sub : juce::String() });
        }
        return m;
    }();
    for (const auto& name : { it.listing.name, it.ownName().first })
        if (const auto key = normalise (name); key.isNotEmpty())
            if (const auto f = byName.find (key); f != byName.end())
                return f->second;
    return std::nullopt;
}

bool FirmwareLibrary::seed (Record& r)
{
    if (r.seeded || r.category != Category::none)
        return false;
    const auto p = presetOf (r);
    if (! p.has_value())
        return false;
    r.category = p->category;
    r.subcategory = p->subcategory;
    r.seeded = true;
    return true;
}

FirmwareLibrary::Category FirmwareLibrary::categoryFromName (const juce::String& s)
{
    for (int i = 0; i < kCategories; ++i)
        if (s.equalsIgnoreCase (kCategoryNames[i]))
            return (Category) i;
    return Category::none;
}

bool FirmwareLibrary::Listing::operator== (const Listing& o) const
{
    return how == o.how && ref == o.ref && name == o.name && version == o.version && author == o.author && about == o.about
        && repo == o.repo && category == o.category;
}

juce::String FirmwareLibrary::normalise (const juce::String& s)
{
    juce::String out;
    for (auto p = s.getCharPointer(); ! p.isEmpty(); ++p)
        if (juce::CharacterFunctions::isLetterOrDigit (*p))
            out << juce::String::charToString (juce::CharacterFunctions::toLowerCase (*p));
    return out;
}

std::pair<juce::String, juce::String> FirmwareLibrary::splitVersion (const juce::String& text)
{
    const auto t = text.trim();
    auto bare = [] (juce::String v)
    {
        v = v.trim();
        if ((v.startsWithChar ('v') || v.startsWithChar ('V')) && juce::CharacterFunctions::isDigit (v[1]))
            v = v.substring (1);
        // a commit's hash: its first seven
        if (v.length() >= 12 && v.containsOnly ("0123456789abcdefABCDEF"))
            v = v.substring (0, 7);
        return v;
    };
    auto startsVersion = [] (const juce::String& w)
    {
        return juce::CharacterFunctions::isDigit (w[0])
            || ((w[0] == 'v' || w[0] == 'V') && juce::CharacterFunctions::isDigit (w[1]));
    };

    // "Name 1.2.3", "Name 0.27 standard": the words from the first that starts as a version does
    juce::StringArray words;
    words.addTokens (t, " ", {});
    words.removeEmptyStrings();
    for (int i = 1; i < words.size(); ++i)
    {
        if (! startsVersion (words[i]))
            continue;
        auto version = words.joinIntoString (" ", i);
        if (version.length() > 24)
            version = words[i];
        return { words.joinIntoString (" ", 0, i), bare (version) };
    }

    // "name-1.2.3", "name-v1.4-preview2": a version with a dot in it, to the end
    auto isVersionTail = [&startsVersion] (const juce::String& v)
    {
        if (! startsVersion (v) || v.containsChar (' '))
            return false;
        const auto digits = v.trimCharactersAtStart ("vV").initialSectionContainingOnly ("0123456789");
        const auto after = v.trimCharactersAtStart ("vV").substring (digits.length());
        return after.startsWithChar ('.') && juce::CharacterFunctions::isDigit (after[1]);
    };
    for (int at = t.indexOfChar ('-'); at > 0; at = t.indexOfChar (at + 1, '-'))
        if (isVersionTail (t.substring (at + 1)))
            return { t.substring (0, at).trim(), bare (t.substring (at + 1)) };
    return { t, {} };
}

std::pair<juce::String, juce::String> FirmwareLibrary::Item::ownName() const
{
    const auto fromFile = splitVersion (file.getFileNameWithoutExtension().replaceCharacter ('_', ' '));
    if (parsedName.isNotEmpty())
    {
        const auto fromBanner = splitVersion (parsedName);
        const auto f = normalise (fromFile.first), b = normalise (fromBanner.first);
        const bool generic = f.isEmpty() || f == "fm1" || f == "firmware" || f == "update" || f == "fm1update" || f == "fm1firmware";
        if (generic || b.startsWith (f))
            return { fromBanner.first, fromFile.second.isNotEmpty() ? fromFile.second : fromBanner.second };
    }
    return fromFile;
}

juce::String FirmwareLibrary::Item::defaultName() const
{
    return listing.name.isNotEmpty() ? listing.name : ownName().first;
}

juce::String FirmwareLibrary::Item::name() const
{
    return customName.isNotEmpty() ? customName : defaultName();
}

juce::String FirmwareLibrary::Item::version() const
{
    if (listing.exact() && listing.version.isNotEmpty())
    {
        // a commit's hash: its first seven
        if (listing.version.length() >= 12 && listing.version.containsOnly ("0123456789abcdefABCDEF"))
            return listing.version.substring (0, 7);
        return listing.version;
    }
    return ownName().second;
}

namespace
{
juce::File& databaseOverride()
{
    static juce::File f;
    return f;
}
} // namespace

juce::File FirmwareLibrary::defaultDatabase()
{
    if (databaseOverride() != juce::File())
        return databaseOverride();
    return prefs::folder().getParentDirectory().getChildFile ("FirmwareLibrary.json");
}

void FirmwareLibrary::useDatabase (const juce::File& f)
{
    databaseOverride() = f;
}

FirmwareLibrary::Probe FirmwareLibrary::probe (const juce::File& file)
{
    Probe p;
    juce::MemoryBlock bytes;
    if (! file.loadFileAsData (bytes))
        return p;
    char identity[64] {}, name[256] {}, digest[64] {};
    p.isPackage = fm1e_probe_package ((const uint8_t*) bytes.getData(), bytes.getSize(), identity, sizeof (identity),
                                      name, sizeof (name), digest, sizeof (digest)) == 0;
    p.identity = juce::String::fromUTF8 (identity);
    p.name = p.isPackage ? juce::String::fromUTF8 (name) : juce::String();
    p.digest = juce::String::fromUTF8 (digest);
    return p;
}

//==============================================================================
FirmwareLibrary::FirmwareLibrary (juce::File db) : database (std::move (db))
{
    load();
}

FirmwareLibrary::~FirmwareLibrary()
{
    scanner.reset();
    cancelPendingUpdate();
    flush();
}

void FirmwareLibrary::load()
{
    records.clear();
    root = juce::File();
    diskTime = database.getLastModificationTime();
    const auto v = juce::JSON::parse (database);
    if (! v.isObject())
        return;

    if (const auto r = v["root"].toString(); juce::File::isAbsolutePath (r))
        root = juce::File (r);
    windowState = v["window"].toString();
    columns = v["columns"].toString();
    cloudColumns = v["cloudColumns"].toString();
    cloudShown = v["browse"].toString() == "cloud";
    infoShown = ! v.hasProperty ("info") || (bool) v["info"];
    if (const auto* list = v["items"].getArray())
        for (const auto& e : *list)
        {
            const auto path = e["path"].toString();
            if (! juce::File::isAbsolutePath (path))
                continue;
            Record r;
            r.file = juce::File (path);
            r.size = (juce::int64) e["size"];
            r.modified = juce::Time ((juce::int64) e["modified"]);
            r.added = juce::Time ((juce::int64) e["added"]);
            r.digest = e["digest"].toString();
            r.identity = e["identity"].toString();
            r.parsedName = e["parsed"].toString();
            r.customName = e["name"].toString();
            r.category = categoryFromName (e["category"].toString());
            r.subcategory = e["sub"].toString();
            if (e["category"].toString().equalsIgnoreCase ("Novelty"))
            {
                r.category = Category::misc;
                r.subcategory = "Novelty / Fun";
            }
            r.seeded = e.getProperty ("seeded", false);
            r.rating = juce::jlimit (0, kMaxRating, (int) e["rating"]);
            r.url = e["url"].toString();
            r.isPackage = e.getProperty ("package", true);
            r.cloudId = e["cloud"].toString();
            if (const auto& l = e["listing"]; l.isObject())
            {
                const auto how = l["how"].toString();
                r.listing.how = how == "download" ? Listing::How::download : how == "checksum" ? Listing::How::checksum
                              : how == "name" ? Listing::How::name : Listing::How::none;
                r.listing.ref = l["ref"].toString();
                r.listing.name = l["name"].toString();
                r.listing.version = l["version"].toString();
                r.listing.author = l["author"].toString();
                r.listing.about = l["about"].toString();
                r.listing.repo = l["repo"].toString();
                r.listing.category = categoryFromName (l["category"].toString());
            }
            // shown at once, as last seen: the scan corrects it
            r.found = root != juce::File() && r.file.isAChildOf (root);
            records.push_back (std::move (r));
        }
}

void FirmwareLibrary::save()
{
    stopTimer();
    if (! dirty)
        return;
    dirty = false;

    juce::Array<juce::var> list;
    for (const auto& r : records)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("path", r.file.getFullPathName());
        o->setProperty ("size", r.size);
        o->setProperty ("modified", r.modified.toMilliseconds());
        o->setProperty ("added", r.added.toMilliseconds());
        o->setProperty ("digest", r.digest);
        if (r.identity.isNotEmpty())
            o->setProperty ("identity", r.identity);
        if (r.parsedName.isNotEmpty())
            o->setProperty ("parsed", r.parsedName);
        if (r.customName.isNotEmpty())
            o->setProperty ("name", r.customName);
        if (r.category != Category::none)
            o->setProperty ("category", categoryName (r.category));
        if (r.subcategory.isNotEmpty())
            o->setProperty ("sub", r.subcategory);
        if (r.seeded)
            o->setProperty ("seeded", true);
        if (r.rating > 0)
            o->setProperty ("rating", r.rating);
        if (r.url.isNotEmpty())
            o->setProperty ("url", r.url);
        if (! r.isPackage)
            o->setProperty ("package", false);
        if (r.cloudId.isNotEmpty())
            o->setProperty ("cloud", r.cloudId);
        if (r.listing.how != Listing::How::none)
        {
            auto* l = new juce::DynamicObject();
            const char* const how[] { "", "download", "checksum", "name" };
            l->setProperty ("how", how[(int) r.listing.how]);
            l->setProperty ("ref", r.listing.ref);
            l->setProperty ("name", r.listing.name);
            if (r.listing.version.isNotEmpty())
                l->setProperty ("version", r.listing.version);
            if (r.listing.author.isNotEmpty())
                l->setProperty ("author", r.listing.author);
            if (r.listing.about.isNotEmpty())
                l->setProperty ("about", r.listing.about);
            if (r.listing.repo.isNotEmpty())
                l->setProperty ("repo", r.listing.repo);
            if (r.listing.category != Category::none)
                l->setProperty ("category", categoryName (r.listing.category));
            o->setProperty ("listing", juce::var (l));
        }
        list.add (juce::var (o));
    }
    auto* top = new juce::DynamicObject();
    top->setProperty ("version", kVersion);
    top->setProperty ("root", root.getFullPathName());
    top->setProperty ("window", windowState);
    top->setProperty ("columns", columns);
    if (cloudColumns.isNotEmpty())
        top->setProperty ("cloudColumns", cloudColumns);
    top->setProperty ("browse", cloudShown ? "cloud" : "local");
    top->setProperty ("info", infoShown);
    top->setProperty ("items", list);

    if (database.getParentDirectory().createDirectory())
        database.replaceWithText (juce::JSON::toString (juce::var (top)));
    diskTime = database.getLastModificationTime();
}

void FirmwareLibrary::flush()
{
    save();
}

void FirmwareLibrary::markDirty()
{
    dirty = true;
    startTimer (400);
}

//==============================================================================
void FirmwareLibrary::setRoot (const juce::File& r)
{
    if (r == root)
    {
        rescan();
        return;
    }
    root = r;
    for (auto& rec : records)
        rec.found = root != juce::File() && rec.file.isAChildOf (root) && rec.file.existsAsFile();
    markDirty();
    sendChangeMessage();
    rescan();
}

std::vector<FirmwareLibrary::Item> FirmwareLibrary::items() const
{
    std::vector<Item> out;
    for (const auto& r : records)
        if (r.found)
            out.push_back (r);
    return out;
}

int FirmwareLibrary::numItems() const
{
    return (int) std::count_if (records.begin(), records.end(), [] (const Record& r) { return r.found; });
}

void FirmwareLibrary::rescan()
{
    if (root == juce::File())
        return;
    if (scanning)
    {
        scanAgain = true;
        return;
    }
    // another process (a host scanning plug-ins out of process) may have written it since
    if (! dirty && database.getLastModificationTime() != diskTime)
        load();

    std::map<juce::String, Seen> known;
    for (const auto& r : records)
        known[r.file.getFullPathName()] = Seen { r.size, r.modified.toMilliseconds(),
                                                  Probe { r.identity, r.parsedName, r.digest, r.isPackage } };
    scanning = true;
    scanner = std::make_unique<Scanner> (*this, root, std::move (known));
    scanner->startThread (juce::Thread::Priority::low);
    sendChangeMessage();
}

bool FirmwareLibrary::waitForScan (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (scanning && juce::Time::getMillisecondCounter() < end)
    {
        handleUpdateNowIfNeeded();
        if (scanning)
            juce::Thread::sleep (5);
    }
    return ! scanning;
}

void FirmwareLibrary::handleAsyncUpdate()
{
    std::optional<Result> r;
    {
        const juce::ScopedLock sl (resultLock);
        r.swap (result);
    }
    scanner.reset();
    scanning = false;
    if (r.has_value() && r->root == root)
        merge (r->root, r->rootThere, std::move (r->found));
    if (scanAgain || (r.has_value() && r->root != root))
    {
        scanAgain = false;
        rescan();
    }
    sendChangeMessage();
}

void FirmwareLibrary::merge (const juce::File& scannedRoot, bool rootThere, std::vector<Found>&& found)
{
    if (! rootThere)
    {
        for (auto& rec : records)
            rec.found = false;
        return;
    }

    std::map<juce::String, size_t> byPath;
    for (size_t i = 0; i < records.size(); ++i)
        byPath[records[i].file.getFullPathName()] = i;

    std::vector<bool> seen (records.size(), false);
    std::vector<const Found*> fresh;
    for (const auto& f : found)
    {
        if (const auto it = byPath.find (f.file.getFullPathName()); it != byPath.end())
            seen[it->second] = true;
        else
            fresh.push_back (&f);
    }

    // a file the scan did not find at its old path and that is no longer there: moved, renamed
    // or deleted. A new file with its digest is where it went.
    std::multimap<juce::String, size_t> goneByDigest;
    for (size_t i = 0; i < records.size(); ++i)
        if (! seen[i] && records[i].digest.isNotEmpty() && ! records[i].file.existsAsFile())
            goneByDigest.emplace (records[i].digest, i);

    const auto now = juce::Time::getCurrentTime();
    for (const auto* f : fresh)
    {
        if (const auto it = goneByDigest.find (f->seen.probe.digest); it != goneByDigest.end())
        {
            records[it->second].file = f->file;
            seen[it->second] = true;
            goneByDigest.erase (it);
            continue;
        }
        Record rec;
        rec.file = f->file;
        rec.added = now;
        records.push_back (std::move (rec));
        seen.push_back (true);
    }

    std::map<juce::String, const Found*> foundByPath;
    for (const auto& f : found)
        foundByPath[f.file.getFullPathName()] = &f;

    for (size_t i = records.size(); i-- > 0;)
    {
        auto& rec = records[i];
        if (const auto it = foundByPath.find (rec.file.getFullPathName()); it != foundByPath.end())
        {
            const auto& s = it->second->seen;
            rec.size = s.size;
            rec.modified = juce::Time (s.modified);
            rec.identity = s.probe.identity;
            rec.parsedName = s.probe.name;
            rec.digest = s.probe.digest;
            rec.isPackage = s.probe.isPackage;
            rec.found = true;
            seed (rec);
        }
        else if (rec.file.isAChildOf (scannedRoot) && ! rec.file.existsAsFile())
        {
            records.erase (records.begin() + (std::ptrdiff_t) i);
        }
        else
        {
            // under the folder and there, it came (adopt) after the scan began
            rec.found = rec.found && rec.file.isAChildOf (scannedRoot) && rec.file.existsAsFile();
        }
    }
    markDirty();
}

//==============================================================================
FirmwareLibrary::Record* FirmwareLibrary::find (const juce::File& f)
{
    for (auto& r : records)
        if (r.file == f)
            return &r;
    return nullptr;
}

void FirmwareLibrary::edit (const juce::File& f, const std::function<bool (Record&)>& change)
{
    if (auto* r = find (f); r != nullptr && change (*r))
    {
        markDirty();
        sendChangeMessage();
    }
}

void FirmwareLibrary::setRating (const juce::File& f, int stars)
{
    stars = juce::jlimit (0, kMaxRating, stars);
    edit (f, [stars] (Record& r) { return std::exchange (r.rating, stars) != stars; });
}

void FirmwareLibrary::setCategory (const juce::File& f, Category c, const juce::String& subcategory)
{
    const auto sub = subcategoriesOf (c).contains (subcategory) ? subcategory : juce::String();
    edit (f, [c, sub] (Record& r)
    {
        const bool changed = r.category != c || r.subcategory != sub || ! r.seeded;
        r.category = c;
        r.subcategory = sub;
        r.seeded = true;
        return changed;
    });
}

void FirmwareLibrary::setSubcategory (const juce::File& f, const juce::String& subcategory)
{
    edit (f, [subcategory] (Record& r)
    {
        const auto c = r.shownCategory();
        if (! subcategoriesOf (c).contains (subcategory))
            return std::exchange (r.subcategory, juce::String()).isNotEmpty();
        const bool changed = r.category != c || r.subcategory != subcategory || ! r.seeded;
        r.category = c;
        r.subcategory = subcategory;
        r.seeded = true;
        return changed;
    });
}

void FirmwareLibrary::setUrl (const juce::File& f, const juce::String& url)
{
    const auto u = url.trim();
    edit (f, [u] (Record& r) { return std::exchange (r.url, u) != u; });
}

void FirmwareLibrary::setCustomName (const juce::File& f, const juce::String& name)
{
    edit (f, [n = name.trim()] (Record& r)
    {
        const auto custom = n == r.defaultName() || n == r.file.getFileNameWithoutExtension() ? juce::String() : n;
        return std::exchange (r.customName, custom) != custom;
    });
}

juce::File FirmwareLibrary::defaultRoot()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("FM-1Ception/Firmwares");
}

void FirmwareLibrary::adopt (const juce::File& f, const juce::String& cloudId, const juce::String& url)
{
    if (root == juce::File() || ! f.isAChildOf (root) || ! f.existsAsFile())
        return;
    const auto p = probe (f);
    auto* r = find (f);
    if (r == nullptr)
    {
        records.emplace_back();
        r = &records.back();
        r->file = f;
        r->added = juce::Time::getCurrentTime();
    }
    r->size = f.getSize();
    r->modified = f.getLastModificationTime();
    r->identity = p.identity;
    r->parsedName = p.name;
    r->digest = p.digest;
    r->isPackage = p.isPackage;
    r->found = true;
    r->cloudId = cloudId;
    if (r->url.isEmpty())
        r->url = url.trim();
    seed (*r);
    markDirty();
    sendChangeMessage();
}

void FirmwareLibrary::describe (const std::function<Listing (const Item&)>& listingOf)
{
    bool changed = false;
    for (auto& r : records)
    {
        if (auto l = listingOf (r); l != r.listing)
        {
            if (r.listing.how == Listing::How::none && r.url.isEmpty())
                r.url = l.repo;
            r.listing = std::move (l);
            changed = true;
        }
        changed = seed (r) || changed;
    }
    if (changed)
    {
        markDirty();
        sendChangeMessage();
    }
}

bool FirmwareLibrary::trash (const juce::File& f)
{
    if (f.existsAsFile() && ! f.moveToTrash())
        return false;
    const auto before = records.size();
    records.erase (std::remove_if (records.begin(), records.end(), [&f] (const Record& r) { return r.file == f; }),
                   records.end());
    if (records.size() != before)
    {
        markDirty();
        sendChangeMessage();
    }
    return true;
}
} // namespace fm1c
