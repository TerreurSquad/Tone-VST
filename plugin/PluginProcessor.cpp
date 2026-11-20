#include "PluginProcessor.h"

#include "OnsetDetector.h"
#include "Presets.h"
#include "gui/PluginEditor.h"

#include <bit>

using namespace juce;

namespace
{
constexpr const char* kStateMagic = "XTN1";
constexpr int kStateMagicSize = 4;

ValueTree readSavedState (const void* data, size_t size)
{
    if (data == nullptr || size < (size_t) kStateMagicSize || std::memcmp (data, kStateMagic, kStateMagicSize) != 0)
        return {};
    return ValueTree::readFromData (static_cast<const char*> (data) + kStateMagicSize, size - kStateMagicSize);
}

void writeSavedState (MemoryBlock& dest, const ValueTree& state)
{
    MemoryOutputStream out (dest, false);
    out.write (kStateMagic, kStateMagicSize);
    state.writeToStream (out);
}
constexpr int kStateVersion = 4;

static_assert (std::atomic<uint64_t>::is_always_lock_free);

uint64_t encodeStep (const xt::SequenceStep& step)
{
    const auto slice = (uint64_t) (std::clamp (step.slice, -1, 127) + 1);
    const auto pitch = (uint64_t) (std::clamp (step.pitch, -24, 24) + 24);
    const float velocity = std::isfinite (step.velocity) ? std::clamp (step.velocity, 0.0f, 1.0f) : 1.0f;
    return std::bit_cast<uint32_t> (velocity) | (slice << 32) | (pitch << 40)
         | ((uint64_t) step.on << 46) | ((uint64_t) step.tie << 47);
}

xt::SequenceStep decodeStep (uint64_t word)
{
    return { (word & (uint64_t { 1 } << 46)) != 0, (word & (uint64_t { 1 } << 47)) != 0,
             (int) ((word >> 32) & 0xffu) - 1, (int) ((word >> 40) & 0x3fu) - 24,
             std::bit_cast<float> ((uint32_t) word) };
}

void copyLegacyPlaybackToB (ValueTree& params)
{
    constexpr const char* ids[][2] {
        { "scan", "ab_scan" }, { "length", "ab_length" }, { "reverse", "ab_reverse" },
        { "interp", "ab_interp" }, { "mono", "ab_mono" }, { "s_pitch", "ab_sample_pitch" },
        { "s_fine", "ab_fine" }, { "penv_amt", "ab_penv_amt" }, { "penv_decay", "ab_penv_decay" }
    };
    for (const auto& pair : ids)
    {
        const auto source = params.getChildWithProperty ("id", pair[0]);
        if (! source.isValid() || params.getChildWithProperty ("id", pair[1]).isValid()) continue;
        auto copy = source.createCopy();
        copy.setProperty ("id", pair[1], nullptr);
        params.appendChild (copy, nullptr);
    }
}

void addMissingTriggerEnvelopeParams (ValueTree& params)
{
    for (const auto* id : xtp::triggerEnvelopeIds)
    {
        if (params.getChildWithProperty ("id", id).isValid()) continue;
        ValueTree entry ("PARAM");
        entry.setProperty ("id", id, nullptr);
        params.appendChild (entry, nullptr); // APVTS applies the registered default when value is absent.
    }
}
} // namespace

XtoneProcessor::XtoneProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", xtp::createParameterLayout()),
      snapshot (apvts)
{
    for (auto& step : sequenceWords) step.store (encodeStep ({}));
    events.reserve (4096);
    zeroBuffer.setSize (2, kChunk);
    zeroBuffer.clear();
    directBuffer.setSize (2, kChunk);

    auto def = xtp::loader::makeDefault();
    {
        const ScopedLock sl (guiLock);
        guiAsset = def;
    }
    // hand the default to the audio side directly (no audio thread yet)
    def->incReferenceCount();
    audioAsset = def.get();
    engine.setSource (audioAsset->view());

    startTimerHz (20);
}

XtoneProcessor::~XtoneProcessor()
{
    stopTimer();
    lifetimeToken.reset(); // pending callbacks holding a weak_ptr now see expiry
    loaderPool.removeAllJobs (true, 10000);

    if (auto* p = pending.exchange (nullptr)) p->decReferenceCount();
    timerCallback(); // drain retired assets
    if (audioAsset != nullptr) audioAsset->decReferenceCount();
}

bool XtoneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet().isDisabled()
        && layouts.getMainOutputChannelSet() == AudioChannelSet::stereo();
}

void XtoneProcessor::prepareToPlay (double sampleRate, int)
{
    hostRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (int i = 0; i < 3; ++i)
    {
        oversamplers[i] = std::make_unique<dsp::Oversampling<float>> (
            2, (size_t) (i + 1), dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        oversamplers[i]->initProcessing ((size_t) kChunk);
    }
    osIndex = -1;
    setOversampling (snapshot.osIndex());
    engine.reset();
}

void XtoneProcessor::setOversampling (int index) noexcept
{
    if (index == osIndex) return;
    osIndex = index;
    const int factor = 1 << index;
    if (index > 0 && oversamplers[index - 1] != nullptr) oversamplers[index - 1]->reset();
    engine.setEngineRate (hostRate * factor);
    const float lat = index > 0 && oversamplers[index - 1] != nullptr ? oversamplers[index - 1]->getLatencyInSamples() : 0.0f;
    setLatencySamples ((int) std::lround (lat));
}

void XtoneProcessor::swapPendingAsset() noexcept
{
    if (pending.load (std::memory_order_acquire) == nullptr) return;
    if (retireFifo.getFreeSpace() < 1) return; // try again next chunk
    auto* next = pending.exchange (nullptr, std::memory_order_acq_rel);
    if (next == nullptr) return;
    const bool keepVoices = audioAsset != nullptr && next->audio == audioAsset->audio
                         && next->bounds == audioAsset->bounds;
    engine.setSource (next->view(), keepVoices);
    if (audioAsset != nullptr)
    {
        const auto scope = retireFifo.write (1);
        if (scope.blockSize1 > 0) retired[scope.startIndex1] = audioAsset;
        else if (scope.blockSize2 > 0) retired[scope.startIndex2] = audioAsset;
    }
    audioAsset = next;
}

void XtoneProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || numSamples <= 0)
    {
        buffer.clear();
        return;
    }

    // host transport
    xt::HostInfo hostInfo;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) { hostInfo.bpm = *bpm; hostInfo.valid = true; }
            if (auto ppq = pos->getPpqPosition()) hostInfo.ppq = *ppq;
            else hostInfo.valid = false; // BPM can still be used by tempo-synced LFOs
            hostInfo.playing = pos->getIsPlaying();
        }
    }

    auto midiIt = midi.cbegin();
    const auto midiEnd = midi.cend();

    for (int pos = 0; pos < numSamples;)
    {
        const int n = std::min (kChunk, numSamples - pos);

        swapPendingAsset();
        setOversampling (snapshot.osIndex());
        const int factor = 1 << osIndex;

        snapshot.read (engineParams);
        if (engineParams.seqOn) snapshotSequence();
        engine.setParams (engineParams);
        auto request = auditionRequest.load (std::memory_order_acquire);
        if (request != 0 && audioAsset != nullptr && (uint32_t) (request >> 32) == audioAsset->publicationId
            && auditionRequest.compare_exchange_strong (request, 0, std::memory_order_acq_rel))
            engine.auditionSlice ((int) ((uint32_t) request >> 1) - 1, (request & 1u) != 0);
        xt::HostInfo hi = hostInfo;
        if (hi.valid) hi.ppq += (double) pos / hostRate * hi.bpm / 60.0;
        engine.setHostInfo (hi);

        // MIDI of this chunk -> engine events (positions in engine samples)
        events.clear();
        for (; midiIt != midiEnd; ++midiIt)
        {
            const auto meta = *midiIt;
            if (meta.samplePosition >= pos + n) break;
            const auto m = meta.getMessage();
            xt::MidiEvent e;
            e.offset = std::max (0, meta.samplePosition - pos) * factor;
            if (m.isNoteOn()) { e.type = xt::MidiEvent::NoteOn; e.note = m.getNoteNumber(); e.value = m.getFloatVelocity(); }
            else if (m.isNoteOff()) { e.type = xt::MidiEvent::NoteOff; e.note = m.getNoteNumber(); }
            else if (m.isPitchWheel()) { e.type = xt::MidiEvent::PitchBend; e.value = (float) (m.getPitchWheelValue() - 8192) / 8192.0f; }
            else if (m.isSustainPedalOn()) { e.type = xt::MidiEvent::Sustain; e.value = 1.0f; }
            else if (m.isSustainPedalOff()) { e.type = xt::MidiEvent::Sustain; e.value = 0.0f; }
            else if (m.isAllNotesOff() || m.isAllSoundOff()) { e.type = xt::MidiEvent::AllNotesOff; }
            else continue;
            if (events.size() < events.capacity()) events.push_back (e);
        }

        float* outL = buffer.getWritePointer (0, pos);
        float* outR = buffer.getWritePointer (1, pos);

        if (osIndex == 0)
        {
            engine.process (outL, outR, n, events.data(), (int) events.size());
        }
        else
        {
            auto& os = *oversamplers[osIndex - 1];
            dsp::AudioBlock<const float> in (zeroBuffer.getArrayOfReadPointers(), 2, 0, (size_t) n);
            auto up = os.processSamplesUp (in);
            engine.process (up.getChannelPointer (0), up.getChannelPointer (1), (int) up.getNumSamples(),
                            events.data(), (int) events.size());
            float* chans[2] = { outL, outR };
            dsp::AudioBlock<float> out (chans, 2, (size_t) n);
            os.processSamplesDown (out);
        }
        pos += n;
    }

    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, numSamples);
    for (int c = 0; c < 2; ++c)
    {
        float* d = buffer.getWritePointer (c);
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            if (std::isfinite (d[i])) peak = std::max (peak, std::abs (d[i]));
            else d[i] = 0.0f;
        }
        auto held = outputPeaks[c].load (std::memory_order_relaxed);
        while (peak > held && ! outputPeaks[c].compare_exchange_weak (held, peak, std::memory_order_relaxed)) {}
        if (peak >= 1.0f) outputClipped.store (true, std::memory_order_relaxed);
    }
}

// ------------------------------------------------------------------ assets

bool XtoneProcessor::publish (xtp::SampleAsset::Ptr asset, const xtp::SampleAsset* expected)
{
    if (asset == nullptr) return false;
    xtp::SampleAsset* displaced = nullptr;
    {
        const ScopedLock sl (guiLock);
        if (expected != nullptr && guiAsset.get() != expected) return false;
        auditionRequest.store (0, std::memory_order_release);
        asset->publicationId = (uint32_t) ++assetVersion;
        guiAsset = asset;
        asset->incReferenceCount();
        displaced = pending.exchange (asset.get(), std::memory_order_acq_rel);
    }
    if (displaced != nullptr) displaced->decReferenceCount(); // never reached the audio thread
    return true;
}

void XtoneProcessor::auditionSlice (const xtp::SampleAsset& asset, int index, bool useB)
{
    const ScopedLock sl (guiLock);
    if (guiAsset.get() != &asset || ! isPositiveAndBelow (index, asset.slices.numSlices())) return;
    const auto request = ((uint64_t) asset.publicationId << 32) | ((uint32_t) (index + 1) << 1) | (useB ? 1u : 0u);
    auditionRequest.store (request, std::memory_order_release);
}

void XtoneProcessor::timerCallback()
{
    const auto scope = retireFifo.read (retireFifo.getNumReady());
    for (int i = 0; i < scope.blockSize1; ++i) retired[scope.startIndex1 + i]->decReferenceCount();
    for (int i = 0; i < scope.blockSize2; ++i) retired[scope.startIndex2 + i]->decReferenceCount();
}

void XtoneProcessor::loadSampleFile (const File& f)
{
    {
        const ScopedLock sl (guiLock);
        statusMessage = "Loading " + f.getFileName() + String::fromUTF8 ("…");
    }
    ++assetVersion;
    std::weak_ptr<int> token = lifetimeToken;
    loaderPool.addJob ([this, f, token]
    {
        String error;
        auto asset = xtp::loader::fromFile (f, error);
        MessageManager::callAsync ([this, token, asset, error]
        {
            if (token.expired()) return;
            {
                const ScopedLock sl (guiLock);
                statusMessage = asset != nullptr ? String() : "Error: " + error;
                if (asset != nullptr && asset->fileBytes == nullptr)
                    statusMessage = "Sample exceeds 32 MB. Only its file path will be saved.";
            }
            if (asset != nullptr) publish (asset);
            else ++assetVersion;
        });
    });
}

void XtoneProcessor::normalizeSample()
{
    auto a = getCurrentAsset();
    if (a == nullptr || a->normalized) return;
    publish (xtp::loader::normalizedCopy (*a), a.get());
}

bool XtoneProcessor::setSlices (const xtp::SampleAsset& asset, xt::SliceModel slices)
{
    if (! publish (asset.withSlices (std::move (slices)), &asset)) return false;
    updateHostDisplay (AudioProcessorListener::ChangeDetails().withNonParameterStateChanged (true));
    return true;
}

void XtoneProcessor::detectTransients (xtp::SampleAsset::Ptr asset, double sensitivity,
                                       std::function<void (std::optional<int>)> finished)
{
    std::weak_ptr<int> token = lifetimeToken;
    loaderPool.addJob ([this, asset, sensitivity, token, finished]
    {
        const auto sample = asset->view().sample;
        auto markers = xt::detectOnsets ({ sample.mono, (size_t) sample.frames }, sample.sampleRate, sensitivity);
        MessageManager::callAsync ([this, asset, token, onsets = std::move (markers), finished]() mutable
        {
            if (token.expired()) return;
            const auto count = (int) onsets.size();
            if (count > 0)
            {
                auto slices = asset->slices;
                slices.setMarkers (std::move (onsets));
                finished (setSlices (*asset, std::move (slices)) ? std::optional<int> { count } : std::nullopt);
            }
            else
            {
                finished (getCurrentAsset() == asset ? std::optional<int> { 0 } : std::nullopt);
            }
        });
    });
}

xtp::SampleAsset::Ptr XtoneProcessor::getCurrentAsset() const
{
    const ScopedLock sl (guiLock);
    return guiAsset;
}

String XtoneProcessor::getStatusMessage() const
{
    const ScopedLock sl (guiLock);
    return statusMessage;
}

// ------------------------------------------------------------------ sequencer

void XtoneProcessor::readSequence (xt::Sequence& dest) const noexcept
{
    for (size_t i = 0; i < dest.size(); ++i)
        dest[i] = decodeStep (sequenceWords[i].load());
}

xt::Sequence XtoneProcessor::getSequence() const
{
    const ScopedLock lock (sequenceLock);
    xt::Sequence steps;
    readSequence (steps);
    return steps;
}

void XtoneProcessor::snapshotSequence() noexcept
{
    const auto version = sequenceVersion.load();
    if (version == audioSequenceVersion || version % 2 != 0) return;

    xt::Sequence steps;
    readSequence (steps);
    if (sequenceVersion.load() == version)
    {
        engineParams.steps = steps;
        audioSequenceVersion = version;
    }
}

void XtoneProcessor::setSequenceStep (int index, const xt::SequenceStep& step)
{
    const auto word = encodeStep (step);
    {
        const ScopedLock lock (sequenceLock);
        auto& target = sequenceWords[(size_t) index];
        if (target.load() == word) return;
        ++sequenceVersion;
        target.store (word);
        ++sequenceVersion;
    }
    updateHostDisplay (AudioProcessorListener::ChangeDetails().withNonParameterStateChanged (true));
}

void XtoneProcessor::replaceSequence (const xt::Sequence& steps)
{
    const ScopedLock lock (sequenceLock);
    ++sequenceVersion;
    for (size_t i = 0; i < steps.size(); ++i) sequenceWords[i].store (encodeStep (steps[i]));
    ++sequenceVersion;
}

void XtoneProcessor::setSequence (const xt::Sequence& steps)
{
    replaceSequence (steps);
    updateHostDisplay (AudioProcessorListener::ChangeDetails().withNonParameterStateChanged (true));
}

// ------------------------------------------------------------------ programs

int XtoneProcessor::getNumPrograms() { return (int) xtp::factoryPresets().size(); }

void XtoneProcessor::setCurrentProgram (int index)
{
    const auto& p = xtp::factoryPresets();
    // Hosts re-send the current program after restoring a project; that must not
    // overwrite the restored parameters.
    if (index < 0 || index >= (int) p.size() || index == currentProgram) return;
    currentProgram = index;
    xtp::applyPreset (apvts, p[(size_t) index]);
}

const String XtoneProcessor::getProgramName (int index)
{
    const auto& p = xtp::factoryPresets();
    return index >= 0 && index < (int) p.size() ? String (p[(size_t) index].name) : String();
}

// ------------------------------------------------------------------ state

ValueTree XtoneProcessor::buildState()
{
    ValueTree root ("XTONE_STATE");
    root.setProperty ("stateVersion", kStateVersion, nullptr);
    root.appendChild (apvts.copyState(), nullptr); // "PARAMS"
    if (auto a = getCurrentAsset())
    {
        root.appendChild (a->getSampleTree(), nullptr);
        root.appendChild (a->getSlicesTree(), nullptr);
    }
    ValueTree sequence ("SEQ");
    for (const auto& step : getSequence())
    {
        ValueTree entry ("STEP");
        entry.setProperty ("on", step.on, nullptr);
        entry.setProperty ("tie", step.tie, nullptr);
        entry.setProperty ("slice", step.slice, nullptr);
        entry.setProperty ("pitch", step.pitch, nullptr);
        entry.setProperty ("vel", step.velocity, nullptr);
        sequence.appendChild (entry, nullptr);
    }
    root.appendChild (sequence, nullptr);
    return root;
}

void XtoneProcessor::getStateInformation (MemoryBlock& dest)
{
    auto state = buildState();
    const auto skin = getEditorSkin();
    for (const auto& entry : xtp::editorSkins)
        if (entry.skin == skin)
            state.setProperty ("editorSkin", entry.id, nullptr);

    writeSavedState (dest, state);
}

void XtoneProcessor::restoreState (const ValueTree& root)
{
    if (! root.hasType ("XTONE_STATE")) return;

    xt::Sequence steps;
    const auto sequence = root.getChildWithName ("SEQ");
    size_t index = 0;
    for (const auto& entry : sequence)
    {
        if (index == steps.size()) break;
        if (! entry.hasType ("STEP")) continue;
        steps[index++] = { (bool) entry.getProperty ("on", true), (bool) entry.getProperty ("tie", false),
                           (int) entry.getProperty ("slice", -1), (int) entry.getProperty ("pitch", 0),
                           (float) entry.getProperty ("vel", 1.0f) };
    }
    replaceSequence (steps);

    if (auto params = root.getChildWithName (apvts.state.getType()); params.isValid())
    {
        const int version = (int) root.getProperty ("stateVersion", 1);
        if (version < 4)
        {
            params = params.createCopy();
            if (version < 2) copyLegacyPlaybackToB (params);
            addMissingTriggerEnvelopeParams (params);
        }
        apvts.replaceState (params);
    }

    String error;
    auto asset = xtp::loader::fromStateTree (root.getChildWithName ("SAMPLE"), error);
    const bool restoredSample = asset != nullptr;
    if (asset == nullptr) asset = xtp::loader::makeDefault();

    if (auto slices = root.getChildWithName ("SLICES"); slices.isValid())
    {
        auto model = asset->slices;
        if (auto* mb = slices.getProperty ("markers").getBinaryData())
        {
            std::vector<int64_t> m (mb->getSize() / sizeof (int64_t));
            if (! m.empty()) std::memcpy (m.data(), mb->getData(), m.size() * sizeof (int64_t));
            model.setMarkers (std::move (m));
        }
        const auto bounds = model.bounds();
        const auto end = bounds.end() - 1;
        for (const auto& entry : slices)
        {
            if (! restoredSample || ! entry.hasType ("ADSR")) continue;
            const auto start = (int64_t) (int64) entry.getProperty ("start", -1);
            const auto match = std::lower_bound (bounds.begin(), end, start);
            if (match == end || *match != start) continue;
            const auto read = [&entry] (const char* id, float fallback, float max)
            {
                const auto value = (float) entry.getProperty (id, fallback);
                return std::isfinite (value) ? std::clamp (value, 0.0f, max) : fallback;
            };
            constexpr auto maxTime = xt::HitEnvelope::kMaxTimeMs;
            model.setEnvelope ((int) (match - bounds.begin()),
                               xt::HitEnvelope { read ("attack", 0.0f, maxTime), read ("decay", 0.0f, maxTime),
                                                 read ("sustain", 1.0f, 1.0f), read ("release", 0.0f, maxTime) });
        }
        asset = asset->withSlices (std::move (model));
    }
    {
        const ScopedLock sl (guiLock);
        statusMessage = error;
    }
    publish (asset);
}

void XtoneProcessor::setStateInformation (const void* data, int size)
{
    if (size < 0) return;
    const auto state = readSavedState (data, (size_t) size);
    if (! state.hasType ("XTONE_STATE")) return;
    setEditorSkin (xtp::editorSkinFromId (state.getProperty ("editorSkin").toString()));
    restoreState (state);
}

bool XtoneProcessor::saveStateToFile (const File& f)
{
    MemoryBlock mb;
    writeSavedState (mb, buildState());
    return f.replaceWithData (mb.getData(), mb.getSize());
}

bool XtoneProcessor::loadStateFromFile (const File& f)
{
    MemoryBlock mb;
    if (! f.loadFileAsData (mb)) return false;
    const auto state = readSavedState (mb.getData(), mb.getSize());
    if (! state.hasType ("XTONE_STATE")) return false;
    restoreState (state);
    updateHostDisplay (AudioProcessorListener::ChangeDetails().withNonParameterStateChanged (true));
    return true;
}

AudioProcessorEditor* XtoneProcessor::createEditor() { return new XtoneEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new XtoneProcessor(); }
