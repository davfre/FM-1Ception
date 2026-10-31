#include "ScreenCache.h"
#include "FirmwareLibrary.h"
#include "fm1ception_engine.h"

namespace fm1c
{
namespace
{
constexpr size_t kMaxPackageBytes = 8 * 1024 * 1024, kMaxCardBytes = 8 * 1024 * 1024;
/** Cards are kept at most this big in memory: the panel shows them smaller. */
constexpr int kCardSize = 480;

juce::Image toImage (const juce::uint16* lcd)
{
    juce::Image img (juce::Image::ARGB, FM1E_LCD_W, FM1E_LCD_H, false);
    const juce::Image::BitmapData px (img, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < FM1E_LCD_H; ++y)
        for (int x = 0; x < FM1E_LCD_W; ++x)
        {
            const auto p = (juce::uint32) lcd[y * FM1E_LCD_W + x];
            reinterpret_cast<juce::PixelARGB*> (px.getPixelPointer (x, y))
                ->setARGB (255, (juce::uint8) ((p >> 11) * 255 / 31), (juce::uint8) (((p >> 5) & 63) * 255 / 63),
                           (juce::uint8) ((p & 31) * 255 / 31));
        }
    return img;
}

bool writePng (const juce::Image& img, const juce::File& f)
{
    if (! f.getParentDirectory().createDirectory())
        return false;
    const auto tmp = f.getSiblingFile (f.getFileName() + ".part");
    {
        juce::FileOutputStream out (tmp);
        if (! out.openedOk() || ! juce::PNGImageFormat().writeImageToStream (img, out))
            return false;
    }
    return tmp.moveFileTo (f);
}

juce::Image readImage (const juce::File& f)
{
    return f.existsAsFile() ? juce::ImageFileFormat::loadFrom (f) : juce::Image();
}

juce::Image cardSized (juce::Image img)
{
    if (img.isValid() && (img.getWidth() > kCardSize || img.getHeight() > kCardSize))
    {
        const float s = (float) kCardSize / (float) juce::jmax (img.getWidth(), img.getHeight());
        img = img.rescaled (juce::roundToInt ((float) img.getWidth() * s), juce::roundToInt ((float) img.getHeight() * s),
                            juce::Graphics::highResamplingQuality);
    }
    return img;
}
} // namespace

juce::File ScreenCache::defaultFolder()
{
    return FirmwareLibrary::defaultDatabase().getSiblingFile ("Screens");
}

ScreenCache::ScreenCache (juce::File f)
    : juce::Thread ("FM-1Ception Screens"), folder (std::move (f))
{
    startThread (juce::Thread::Priority::low);
}

ScreenCache::~ScreenCache()
{
    cancelPendingUpdate();
    signalThreadShouldExit();
    work.signal();
    // (a capture under way runs to its end: a second or so)
    stopThread (40000);
}

juce::File ScreenCache::shotFile (const juce::String& key, bool late) const
{
    return folder.getChildFile (juce::File::createLegalFileName (key) + (late ? "-late.png" : "-early.png"));
}

juce::File ScreenCache::failedFile (const juce::String& key) const
{
    return folder.getChildFile (juce::File::createLegalFileName (key) + ".failed");
}

juce::File ScreenCache::cardFile (const juce::String& url) const
{
    const auto name = juce::URL (url).getFileName();
    return folder.getChildFile ("Cards").getChildFile (juce::File::createLegalFileName (name.isNotEmpty() ? name : juce::String (url.hashCode64())));
}

ScreenCache::Shots ScreenCache::shotsOf (const juce::String& key)
{
    if (key.isEmpty())
        return {};
    const juce::ScopedLock sl (lock);
    if (const auto it = shots.find (key); it != shots.end())
        return it->second;
    Shots s;
    s.early = readImage (shotFile (key, false));
    s.late = readImage (shotFile (key, true));
    if (! s.has())
        s.error = failedFile (key).existsAsFile() ? failedFile (key).loadFileAsString().trim() : juce::String();
    if (s.has() || s.error.isNotEmpty())
        shots[key] = s;
    return s;
}

void ScreenCache::capture (const juce::String& key, const juce::File& package, bool again)
{
    want ({ key, package, {} }, again);
}

void ScreenCache::capture (const juce::String& key, Fetch fetch, bool again)
{
    want ({ key, {}, std::move (fetch) }, again);
}

void ScreenCache::want (Job job, bool again)
{
    if (job.key.isEmpty())
        return;
    const auto kept = shotsOf (job.key);
    if (kept.capturing || (! again && (kept.has() || kept.error.isNotEmpty())))
        return;
    {
        const juce::ScopedLock sl (lock);
        // the one asked for last goes first: one not yet begun is let go
        if (wanted.has_value())
            shots.erase (wanted->key);
        auto& s = shots[job.key];
        s = {};
        s.capturing = true;
        wanted = std::move (job);
    }
    sendChangeMessage();
    work.signal();
}

juce::Image ScreenCache::cardOf (const juce::String& url)
{
    if (url.isEmpty())
        return {};
    const juce::ScopedLock sl (lock);
    if (const auto it = cards.find (url); it != cards.end())
        return it->second;
    if (cardsTried.count (url) != 0)
        return {};
    cardsTried.insert (url);
    if (auto img = cardSized (readImage (cardFile (url))); img.isValid())
    {
        cards[url] = img;
        return img;
    }
    if (! url.startsWithIgnoreCase ("https://") && ! url.startsWithIgnoreCase ("http://") && ! url.startsWithIgnoreCase ("file://"))
        return {};
    cardQueue.push_back (url);
    work.signal();
    return {};
}

juce::String ScreenCache::captureNow (const juce::MemoryBlock& package, juce::Image& early, juce::Image& late)
{
    const juce::uint32 at[] { kEarlyMs, kLateMs };
    std::vector<juce::uint16> lcd ((size_t) (FM1E_LCD_W * FM1E_LCD_H * 2));
    char why[512] {};
    const int n = fm1e_screenshots ((const juce::uint8*) package.getData(), package.getSize(), at, 2, lcd.data(), why, sizeof (why));
    if (n <= 0)
        return juce::String::fromUTF8 (why).isNotEmpty() ? juce::String::fromUTF8 (why) : juce::String ("It could not be captured");
    early = toImage (lcd.data());
    if (n > 1)
        late = toImage (lcd.data() + FM1E_LCD_W * FM1E_LCD_H);
    return {};
}

void ScreenCache::run()
{
    while (! threadShouldExit())
    {
        work.wait (500);
        for (;;)
        {
            if (threadShouldExit())
                return;
            std::optional<juce::String> cardUrl;
            std::optional<Job> job;
            {
                const juce::ScopedLock sl (lock);
                if (! cardQueue.empty())
                {
                    cardUrl = cardQueue.front();
                    cardQueue.pop_front();
                }
                else if (wanted.has_value())
                {
                    job = std::move (wanted);
                    wanted.reset();
                }
                if (cardUrl || job)
                    ++busy;
            }
            if (! cardUrl && ! job)
                break;

            if (cardUrl)
            {
                juce::MemoryBlock bytes;
                const auto in = juce::URL (*cardUrl).createInputStream (
                    juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs (15000));
                if (in != nullptr)
                    in->readIntoMemoryBlock (bytes, (ssize_t) kMaxCardBytes);
                auto img = juce::ImageFileFormat::loadFrom (bytes.getData(), bytes.getSize());
                if (const auto f = cardFile (*cardUrl); img.isValid() && f.getParentDirectory().createDirectory())
                    f.replaceWithData (bytes.getData(), bytes.getSize());
                {
                    const juce::ScopedLock sl (lock);
                    if (img.isValid())
                        cards[*cardUrl] = cardSized (img);
                }
            }
            else
            {
                juce::MemoryBlock bytes;
                juce::String error;
                if (job->fetch)
                    error = job->fetch (bytes);
                else if (! job->file.existsAsFile() || job->file.getSize() > (juce::int64) kMaxPackageBytes || ! job->file.loadFileAsData (bytes))
                    error = "Its file could not be read";
                Shots s;
                if (error.isEmpty())
                    error = captureNow (bytes, s.early, s.late);
                s.error = error;
                if (s.has())
                {
                    writePng (s.early, shotFile (job->key, false));
                    if (s.late.isValid())
                        writePng (s.late, shotFile (job->key, true));
                    failedFile (job->key).deleteFile();
                }
                else if (! job->fetch)
                {
                    // (a firmware that cannot be run here is not run again each time it is shown)
                    folder.createDirectory();
                    failedFile (job->key).replaceWithText (error);
                }
                const juce::ScopedLock sl (lock);
                shots[job->key] = s;
            }
            --busy;
            triggerAsyncUpdate();
        }
    }
}

void ScreenCache::handleAsyncUpdate()
{
    sendChangeMessage();
}

bool ScreenCache::waitForIdle (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    for (;;)
    {
        {
            const juce::ScopedLock sl (lock);
            if (! wanted.has_value() && cardQueue.empty() && busy == 0)
                break;
        }
        if (juce::Time::getMillisecondCounter() > end)
            return false;
        juce::Thread::sleep (10);
    }
    handleUpdateNowIfNeeded();
    return true;
}
} // namespace fm1c
