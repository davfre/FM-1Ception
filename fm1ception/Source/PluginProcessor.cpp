#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::Identifier kStateType { "FM1CEPTION" };
const juce::Identifier kSlotType { "SLOT" };
constexpr juce::uint32 kStateMagic = 0x43314d46; // "FM1C"
constexpr int kStateVersion = 1;

/** The length of a channel message from its status byte (0: not one). */
int channelMessageLength (juce::uint8 status)
{
    switch (status & 0xf0)
    {
        case 0x80: case 0x90: case 0xa0: case 0xb0: case 0xe0: return 3;
        case 0xc0: case 0xd0: return 2;
        default: return 0;
    }
}

/** The main volume's gain is its value squared: 40 log10, as SHRUB's MASTER. */
juce::String mainVolumeText (float v)
{
    if (v <= 0.0f)
        return "-inf dB";
    return juce::String (40.0f * std::log10 (v), 1) + " dB";
}
} // namespace

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout FM1CeptionProcessor::buildParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout params;

    params.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "main", 1 }, "Main Volume",
        juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return mainVolumeText (v); })));

    for (int i = 0; i < kMaxEngines; ++i)
        params.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "master" + juce::String (i + 1), 1 }, "Master " + juce::String (i + 1),
            juce::NormalisableRange<float> (0.0f, 1.0f), kDefaultMaster,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })));

    return params;
}

FM1CeptionProcessor::FM1CeptionProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", buildParameterLayout())
{
    mainVolume = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("main"));
    jassert (mainVolume != nullptr);

    const auto mhz = (int) fm1e_default_mhz();
    for (int i = 0; i < kMaxEngines; ++i)
    {
        auto& s = slots[(size_t) i];
        s.master = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("master" + juce::String (i + 1)));
        jassert (s.master != nullptr);
        s.clockMhz = mhz;
    }

    setNumEngines (1);
}

FM1CeptionProcessor::~FM1CeptionProcessor()
{
    const juce::ScopedLock sl (slotsLock);
    for (auto& s : slots)
        stopEngine (s);
}

//==============================================================================
void FM1CeptionProcessor::startEngine (Slot& s)
{
    if (s.handle != nullptr)
        return;

    auto h = std::make_shared<fm1c::EngineHandle>();
    if (! h->isValid())
        return;

    fm1e_set_clock (h->get(), (juce::uint32) s.clockMhz.load());
    fm1e_set_master (h->get(), s.master->get());
    fm1e_set_lead (h->get(), (juce::uint32) lead);

    if (! s.parkedPackage.isEmpty())
    {
        const auto name = s.parkedName.toStdString();
        const auto* pkg = (const juce::uint8*) s.parkedPackage.getData();
        if (fm1e_boot (h->get(), name.c_str(), pkg, s.parkedPackage.getSize(),
                       (const juce::uint8*) s.parkedFlash.getData(), s.parkedFlash.getSize()) != 0)
        {
            // flash that does not parse is dropped rather than losing the firmware too
            fm1e_boot (h->get(), name.c_str(), pkg, s.parkedPackage.getSize(), nullptr, 0);
            h->post ("The saved flash was unreadable: booted without it", 8000);
        }
    }
    s.parkedName.clear();
    s.parkedPackage.reset();
    s.parkedFlash.reset();

    {
        const juce::ScopedLock el (engineLock);
        s.live = h->get();
        s.taken = 0;
        s.frac = 0.0;
        s.cur[0] = s.cur[1] = s.next[0] = s.next[1] = 0.0f;
        s.route = -1;
        s.held = {};
        s.sustain = {};
    }
    s.handle = std::move (h);
}

void FM1CeptionProcessor::stopEngine (Slot& s)
{
    if (s.handle == nullptr)
        return;

    s.parkedName = s.handle->firmwareName();
    s.parkedPackage = s.handle->package();
    s.parkedFlash = s.handle->flash();

    {
        const juce::ScopedLock el (engineLock);
        s.live = nullptr;
    }
    // joins the engine's thread, unless a file job still holds it
    s.handle.reset();
}

void FM1CeptionProcessor::setNumEngines (int n)
{
    n = juce::jlimit (1, kMaxEngines, n);
    {
        const juce::ScopedLock sl (slotsLock);
        numEngines = n;
        for (int i = 0; i < kMaxEngines; ++i)
        {
            auto& s = slots[(size_t) i];
            if (i < n)
                startEngine (s);
            else
                stopEngine (s);
        }
    }
    ++layout;
}

std::shared_ptr<fm1c::EngineHandle> FM1CeptionProcessor::engine (int slot) const
{
    if (! juce::isPositiveAndBelow (slot, kMaxEngines))
        return {};
    const juce::ScopedLock sl (slotsLock);
    return slots[(size_t) slot].handle;
}

void FM1CeptionProcessor::setClock (int slot, int mhz)
{
    if (! juce::isPositiveAndBelow (slot, kMaxEngines))
        return;
    slots[(size_t) slot].clockMhz = mhz;
    if (auto h = engine (slot))
        fm1e_set_clock (h->get(), (juce::uint32) mhz);
}

//==============================================================================
void FM1CeptionProcessor::runFileJob (int slot, const juce::File& file, bool exporting)
{
    auto h = engine (slot);
    if (h == nullptr)
        return;
    if (h->fileJob.exchange (true))
    {
        h->post ("Still busy with the last file");
        return;
    }
    h->post (exporting ? "Exporting live flash..." : "Loading " + file.getFileName() + "...", 60000);

    juce::Thread::launch ([h, path = file.getFullPathName().toStdString(), exporting]
    {
        char msg[512] {};
        const int r = exporting ? fm1e_export_file (h->get(), path.c_str(), msg, sizeof (msg))
                                : fm1e_load_file (h->get(), path.c_str(), msg, sizeof (msg));
        h->post (juce::String::fromUTF8 (msg), r == 0 ? 3500 : 8000);
        h->fileJob = false;
    });
}

void FM1CeptionProcessor::loadFile (int slot, const juce::File& file) { runFileJob (slot, file, false); }
void FM1CeptionProcessor::exportFile (int slot, const juce::File& file) { runFileJob (slot, file, true); }

juce::Result FM1CeptionProcessor::loadFileNow (int slot, const juce::File& file)
{
    auto h = engine (slot);
    if (h == nullptr)
        return juce::Result::fail ("that engine is switched off");
    char msg[512] {};
    const auto path = file.getFullPathName().toStdString();
    if (fm1e_load_file (h->get(), path.c_str(), msg, sizeof (msg)) != 0)
        return juce::Result::fail (juce::String::fromUTF8 (msg));
    return juce::Result::ok();
}

void FM1CeptionProcessor::initEngine (int slot)
{
    if (auto h = engine (slot))
    {
        fm1e_unload (h->get());
        h->post ("Engine initialised");
    }
}

//==============================================================================
void FM1CeptionProcessor::setLeadFromBlockSize (double sampleRate)
{
    ratio = (double) FM1E_RATE / juce::jmax (8000.0, sampleRate);
    // a host block's frames, and room for a DMA half and the emulation's unevenness
    lead = (int) std::ceil (maxBlock * ratio) + 1024;
    setLatencySamples (juce::roundToInt (lead / ratio));
}

void FM1CeptionProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = juce::jmax (1, samplesPerBlock);
    setLeadFromBlockSize (sampleRate);
    if (awake == nullptr)
        awake = std::make_unique<juce::ScopedLowPowerModeDisabler>();

    {
        const juce::ScopedLock sl (slotsLock);
        for (auto& s : slots)
            if (s.handle != nullptr)
                fm1e_set_lead (s.handle->get(), (juce::uint32) lead);
    }

    mainGain.reset (sampleRate, 0.02);
    const float v = mainVolume->get();
    mainGain.setCurrentAndTargetValue (v * v);
}

bool FM1CeptionProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (! layouts.getMainInputChannelSet().isDisabled())
        return false;

    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void FM1CeptionProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n <= 0 || buffer.getNumChannels() == 0)
        return;

    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    // a mono output gets both channels into one
    juce::HeapBlock<float> monoR;
    if (outR == nullptr)
    {
        monoR.calloc ((size_t) n);
        outR = monoR.get();
    }

    {
        const juce::ScopedTryLock el (engineLock);
        if (el.isLocked())
        {
            const auto mode = getMidiMode();
            const int sel = getSelected();
            for (int si = 0; si < kMaxEngines; ++si)
            {
                auto& s = slots[(size_t) si];
                auto* e = s.live;
                if (e == nullptr)
                    continue;

                fm1e_set_master (e, s.master->get());

                const auto start = s.taken;
                int rx = 0, tx = 0;
                bool hears = true;
                switch (mode)
                {
                    case MidiMode::selected:     hears = si == sel; tx = juce::jmax (1, s.txChannel.load()); break;
                    case MidiMode::multiChannel: rx = si + 1; tx = 1; break;
                    case MidiMode::custom:       rx = s.rxChannel.load(); tx = s.txChannel.load(); break;
                }
                const int route = hears ? rx * 32 + tx : -1;
                if (route != s.route)
                {
                    releaseHeld (s, e, start + (juce::uint64) lead);
                    s.route = route;
                }

                for (const auto meta : midi)
                {
                    const auto* d = meta.data;
                    if (! hears || meta.numBytes < 1)
                        continue;
                    const int len = channelMessageLength (d[0]);
                    if (len == 0 || meta.numBytes < len)
                        continue;
                    if (rx != 0 && (d[0] & 0x0f) != rx - 1)
                        continue;
                    const auto status = (juce::uint8) (tx != 0 ? ((d[0] & 0xf0) | (tx - 1)) : d[0]);
                    const auto frame = start + (juce::uint64) (meta.samplePosition * ratio) + (juce::uint64) lead;
                    sendMidi (s, e, frame, status, len > 1 ? d[1] : 0, len > 2 ? d[2] : 0, (juce::uint8) len);
                }

                float spk[2] {};
                juce::uint64 starved = 0;
                for (int i = 0; i < n; ++i)
                {
                    s.frac += ratio;
                    while (s.frac >= 1.0)
                    {
                        float f[2];
                        if (! fm1e_pop (e, f))
                        {
                            // the engine is behind: hold, and take the frame next time
                            s.frac = juce::jmin (s.frac, 1.0);
                            ++starved;
                            break;
                        }
                        s.cur[0] = s.next[0];
                        s.cur[1] = s.next[1];
                        s.next[0] = f[0];
                        s.next[1] = f[1];
                        ++s.taken;
                        s.frac -= 1.0;
                    }
                    const auto t = (float) juce::jmin (s.frac, 1.0);
                    const float l = s.cur[0] + (s.next[0] - s.cur[0]) * t;
                    const float r = s.cur[1] + (s.next[1] - s.cur[1]) * t;
                    outL[i] += l;
                    outR[i] += r;
                    spk[0] = juce::jmax (spk[0], std::abs (l));
                    spk[1] = juce::jmax (spk[1], std::abs (r));
                }
                if (starved > 0)
                    fm1e_starved (e, starved);
                for (int c = 0; c < 2; ++c)
                    if (spk[c] > s.peak[c].load (std::memory_order_relaxed))
                        s.peak[c].store (spk[c], std::memory_order_relaxed);
            }
        }
    }

    const float v = mainVolume->get();
    mainGain.setTargetValue (v * v);
    float pk[2] {};
    for (int i = 0; i < n; ++i)
    {
        const float g = mainGain.getNextValue();
        outL[i] *= g;
        outR[i] *= g;
        pk[0] = juce::jmax (pk[0], std::abs (outL[i]));
        pk[1] = juce::jmax (pk[1], std::abs (outR[i]));
    }
    if (monoR != nullptr)
        for (int i = 0; i < n; ++i)
            outL[i] = 0.5f * (outL[i] + outR[i]);

    for (int c = 0; c < 2; ++c)
        if (pk[c] > peak[c].load (std::memory_order_relaxed))
            peak[c].store (pk[c], std::memory_order_relaxed);

    midi.clear(); // an instrument, not a MIDI effect
}

std::array<float, 2> FM1CeptionProcessor::takePeaks()
{
    return { peak[0].exchange (0.0f), peak[1].exchange (0.0f) };
}

std::array<float, 2> FM1CeptionProcessor::takeEnginePeaks (int slot)
{
    auto& s = slots[(size_t) juce::jlimit (0, kMaxEngines - 1, slot)];
    return { s.peak[0].exchange (0.0f), s.peak[1].exchange (0.0f) };
}

void FM1CeptionProcessor::sendMidi (Slot& s, Fm1e* e, juce::uint64 frame, juce::uint8 status,
                                    juce::uint8 d1, juce::uint8 d2, juce::uint8 len)
{
    const int ch = status & 0x0f, type = status & 0xf0;
    if (type == 0x90 && d2 > 0)
        s.held[(size_t) ch].set (d1 & 127);
    else if (type == 0x80 || type == 0x90)
        s.held[(size_t) ch].reset (d1 & 127);
    else if (type == 0xb0 && d1 == 64)
        s.sustain[(size_t) ch] = d2 >= 64;
    else if (type == 0xb0 && (d1 == 120 || d1 == 123))
        s.held[(size_t) ch].reset();
    fm1e_push_midi (e, frame, status, d1, d2, len);
}

void FM1CeptionProcessor::releaseHeld (Slot& s, Fm1e* e, juce::uint64 frame)
{
    for (int ch = 0; ch < 16; ++ch)
    {
        if (s.sustain[(size_t) ch])
            fm1e_push_midi (e, frame, (juce::uint8) (0xb0 | ch), 64, 0, 3);
        auto& h = s.held[(size_t) ch];
        for (int n = 0; n < 128 && h.any(); ++n)
            if (h.test ((size_t) n))
            {
                fm1e_push_midi (e, frame, (juce::uint8) (0x80 | ch), (juce::uint8) n, 0, 3);
                h.reset ((size_t) n);
            }
        s.sustain[(size_t) ch] = false;
    }
}

//==============================================================================
void FM1CeptionProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state (kStateType);
    state.setProperty ("version", kStateVersion, nullptr);
    state.setProperty ("engines", numEngines.load(), nullptr);
    state.setProperty ("editorW", editorW.load(), nullptr);
    state.setProperty ("editorH", editorH.load(), nullptr);
    state.setProperty ("midiMode", midiMode.load(), nullptr);
    state.setProperty ("selected", selected.load(), nullptr);
    state.setProperty ("viewLayout", viewLayout.load(), nullptr);
    state.appendChild (apvts.copyState(), nullptr);

    {
        const juce::ScopedLock sl (slotsLock);
        for (int i = 0; i < kMaxEngines; ++i)
        {
            const auto& s = slots[(size_t) i];
            juce::ValueTree slot (kSlotType);
            slot.setProperty ("index", i, nullptr);
            slot.setProperty ("clock", s.clockMhz.load(), nullptr);
            slot.setProperty ("rx", s.rxChannel.load(), nullptr);
            slot.setProperty ("tx", s.txChannel.load(), nullptr);

            const auto name = s.handle != nullptr ? s.handle->firmwareName() : s.parkedName;
            const auto pkg = s.handle != nullptr ? s.handle->package() : s.parkedPackage;
            if (! pkg.isEmpty())
            {
                slot.setProperty ("name", name, nullptr);
                slot.setProperty ("package", pkg, nullptr);
                slot.setProperty ("flash", s.handle != nullptr ? s.handle->flash() : s.parkedFlash, nullptr);
            }
            state.appendChild (slot, nullptr);
        }
    }

    juce::MemoryOutputStream out (destData, false);
    out.writeInt ((int) kStateMagic);
    state.writeToStream (out);
}

void FM1CeptionProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream in (data, (size_t) sizeInBytes, false);
    if (sizeInBytes < 8 || (juce::uint32) in.readInt() != kStateMagic)
        return;
    const auto state = juce::ValueTree::readFromStream (in);
    if (! state.hasType (kStateType))
        return;

    if (const auto params = state.getChildWithName (apvts.state.getType()); params.isValid())
        apvts.replaceState (params);

    editorW = (int) state.getProperty ("editorW", 0);
    editorH = (int) state.getProperty ("editorH", 0);
    setMidiMode ((MidiMode) (int) state.getProperty ("midiMode", (int) MidiMode::selected));
    setSelected ((int) state.getProperty ("selected", 0));
    viewLayout = juce::jlimit (0, 1, (int) state.getProperty ("viewLayout", (int) ViewLayout::tiled));

    {
        const juce::ScopedLock sl (slotsLock);
        for (int i = 0; i < kMaxEngines; ++i)
        {
            auto& s = slots[(size_t) i];
            juce::ValueTree slot;
            for (const auto& c : state)
                if (c.hasType (kSlotType) && (int) c.getProperty ("index", -1) == i)
                    slot = c;

            s.clockMhz = (int) slot.getProperty ("clock", (int) fm1e_default_mhz());
            s.rxChannel = juce::jlimit (0, 16, (int) slot.getProperty ("rx", 0));
            s.txChannel = juce::jlimit (0, 16, (int) slot.getProperty ("tx", 0));

            juce::String name;
            juce::MemoryBlock pkg, flash;
            if (const auto* mb = slot.getProperty ("package").getBinaryData())
                pkg = *mb;
            if (const auto* mb = slot.getProperty ("flash").getBinaryData())
                flash = *mb;
            name = slot.getProperty ("name").toString();

            if (s.handle != nullptr)
            {
                auto* e = s.handle->get();
                fm1e_set_clock (e, (juce::uint32) s.clockMhz.load());
                if (pkg.isEmpty())
                    fm1e_unload (e);
                else if (fm1e_boot (e, name.toRawUTF8(), (const juce::uint8*) pkg.getData(), pkg.getSize(),
                                    (const juce::uint8*) flash.getData(), flash.getSize()) != 0)
                    fm1e_boot (e, name.toRawUTF8(), (const juce::uint8*) pkg.getData(), pkg.getSize(), nullptr, 0);
            }
            else
            {
                s.parkedName = name;
                s.parkedPackage = pkg;
                s.parkedFlash = flash;
            }
        }
    }

    setNumEngines ((int) state.getProperty ("engines", 1));
}

//==============================================================================
juce::AudioProcessorEditor* FM1CeptionProcessor::createEditor()
{
    return new FM1CeptionEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FM1CeptionProcessor();
}
