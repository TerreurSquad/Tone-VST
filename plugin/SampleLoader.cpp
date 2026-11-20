#include "SampleLoader.h"

#include "DefaultKick.h"

namespace xtp
{
using namespace juce;

namespace
{
    constexpr int64 kMaxFrames = (int64) 48000 * 60 * 10; // 10 minutes at 48 kHz

    std::shared_ptr<const std::vector<float>> buildPeaks (const AudioData& a)
    {
        auto v = std::make_shared<std::vector<float>>();
        const float* m = a.buffer.getReadPointer (2);
        const int n = a.buffer.getNumSamples();
        v->reserve ((size_t) (n / SampleAsset::kPeakBlock + 1) * 2);
        for (int i = 0; i < n; i += SampleAsset::kPeakBlock)
        {
            const int e = std::min (n, i + SampleAsset::kPeakBlock);
            float lo = m[i], hi = m[i];
            for (int j = i + 1; j < e; ++j)
            {
                lo = std::min (lo, m[j]);
                hi = std::max (hi, m[j]);
            }
            v->push_back (lo);
            v->push_back (hi);
        }
        return v;
    }

    /// Wrap 1 or 2 decoded channels into the 3-channel layout.
    std::shared_ptr<AudioData> makeAudio (const AudioBuffer<float>& src, int numCh, double rate)
    {
        auto a = std::make_shared<AudioData>();
        const int n = src.getNumSamples();
        a->buffer.setSize (3, n);
        a->sampleRate = rate;
        a->numSourceChannels = numCh >= 2 ? 2 : 1;
        a->buffer.copyFrom (0, 0, src, 0, 0, n);
        a->buffer.copyFrom (1, 0, src, numCh >= 2 ? 1 : 0, 0, n);
        a->buffer.copyFrom (2, 0, src, 0, 0, n);
        if (numCh >= 2)
        {
            a->buffer.addFrom (2, 0, src, 1, 0, n);
            a->buffer.applyGain (2, 0, n, 0.5f);
        }
        return a;
    }

    SampleAsset::Ptr finish (std::shared_ptr<const AudioData> audio)
    {
        SampleAsset::Ptr a = new SampleAsset();
        a->audio = std::move (audio);
        a->peaks = buildPeaks (*a->audio);
        a->slices.setLength (a->audio->frames());
        a->bounds = a->slices.bounds();
        return a;
    }

    SampleAsset::Ptr decode (std::unique_ptr<InputStream> in, String& error)
    {
        // Local manager: a static one would be destroyed at DLL unload, which is unsafe on Windows.
        AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<AudioFormatReader> reader (fm.createReaderFor (std::move (in)));
        if (reader == nullptr)
        {
            error = "Unsupported audio format";
            return nullptr;
        }
        if (reader->lengthInSamples <= 0)
        {
            error = "Audio file is empty";
            return nullptr;
        }
        const int64 frames = std::min (reader->lengthInSamples, kMaxFrames);
        const int numCh = (int) std::min<unsigned> (2u, reader->numChannels);
        AudioBuffer<float> tmp (numCh, (int) frames);
        if (! reader->read (&tmp, 0, (int) frames, 0, true, numCh >= 2))
        {
            error = "Could not decode audio";
            return nullptr;
        }
        return finish (makeAudio (tmp, numCh, reader->sampleRate));
    }
} // namespace

xt::SourceView SampleAsset::view() const noexcept
{
    xt::SourceView v;
    if (audio == nullptr) return v;
    v.sample.ch[0] = audio->buffer.getReadPointer (0);
    v.sample.ch[1] = audio->buffer.getReadPointer (1);
    v.sample.mono = audio->buffer.getReadPointer (2);
    v.sample.frames = audio->frames();
    v.sample.numCh = audio->numSourceChannels;
    v.sample.sampleRate = audio->sampleRate;
    v.bounds = bounds.data();
    v.numSlices = (int) bounds.size() - 1;
    v.envelopes = slices.getEnvelopes().data();
    return v;
}

SampleAsset::Ptr SampleAsset::withSlices (xt::SliceModel next) const
{
    SampleAsset::Ptr a = new SampleAsset();
    a->audio = audio;
    a->fileBytes = fileBytes;
    a->peaks = peaks;
    a->name = name;
    a->origPath = origPath;
    a->isDefault = isDefault;
    a->normalized = normalized;
    a->cachedSampleTree = cachedSampleTree; // audio unchanged -> same SAMPLE node
    a->slices = std::move (next);
    a->bounds = a->slices.bounds();
    return a;
}

ValueTree SampleAsset::getSampleTree() const
{
    if (cachedSampleTree.isValid()) return cachedSampleTree;
    ValueTree t ("SAMPLE");
    t.setProperty ("name", name, nullptr);
    t.setProperty ("default", isDefault, nullptr);
    t.setProperty ("origPath", origPath, nullptr);
    t.setProperty ("normalized", normalized, nullptr);
    if (audio != nullptr)
    {
        t.setProperty ("srcRate", audio->sampleRate, nullptr);
        t.setProperty ("frames", (int64) audio->frames(), nullptr);
        t.setProperty ("channels", audio->numSourceChannels, nullptr);
    }
    t.setProperty ("embedded", fileBytes != nullptr, nullptr);
    if (fileBytes != nullptr)
        t.setProperty ("data", var (*fileBytes), nullptr);
    cachedSampleTree = t;
    return t;
}

ValueTree SampleAsset::getSlicesTree() const
{
    ValueTree t ("SLICES");
    const auto& m = slices.getMarkers();
    MemoryBlock mb (m.data(), m.size() * sizeof (int64_t));
    t.setProperty ("markers", var (mb), nullptr);
    for (int i = 0; i < slices.numSlices(); ++i)
    {
        if (const auto& env = slices.getEnvelopes()[(size_t) i]; env.has_value())
        {
            ValueTree entry ("ADSR");
            entry.setProperty ("start", (int64) bounds[(size_t) i], nullptr);
            entry.setProperty ("attack", env->attackMs, nullptr);
            entry.setProperty ("decay", env->decayMs, nullptr);
            entry.setProperty ("sustain", env->sustain, nullptr);
            entry.setProperty ("release", env->releaseMs, nullptr);
            t.appendChild (entry, nullptr);
        }
    }
    return t;
}

namespace loader
{
    SampleAsset::Ptr makeDefault()
    {
        const auto kick = xt::makeDefaultKick (48000.0);
        AudioBuffer<float> tmp (1, (int) kick.size());
        tmp.copyFrom (0, 0, kick.data(), (int) kick.size());
        auto a = finish (makeAudio (tmp, 1, 48000.0));
        a->name = "Default Kick";
        a->isDefault = true;
        return a;
    }

    SampleAsset::Ptr fromMemory (std::shared_ptr<const MemoryBlock> bytes, const String& name, const String& path, String& error)
    {
        auto a = decode (std::make_unique<MemoryInputStream> (*bytes, false), error);
        if (a == nullptr) return nullptr;
        a->fileBytes = std::move (bytes);
        a->name = name;
        a->origPath = path;
        return a;
    }

    SampleAsset::Ptr fromFile (const File& f, String& error)
    {
        if (! f.existsAsFile())
        {
            error = "File not found: " + f.getFullPathName();
            return nullptr;
        }
        if ((size_t) f.getSize() <= SampleAsset::kMaxEmbedBytes)
        {
            auto bytes = std::make_shared<MemoryBlock>();
            if (! f.loadFileAsData (*bytes))
            {
                error = "Could not read file";
                return nullptr;
            }
            return fromMemory (bytes, f.getFileName(), f.getFullPathName(), error);
        }
        auto a = decode (f.createInputStream(), error);
        if (a != nullptr)
        {
            a->name = f.getFileName();
            a->origPath = f.getFullPathName();
        }
        return a;
    }

    SampleAsset::Ptr fromStateTree (const ValueTree& t, String& error)
    {
        SampleAsset::Ptr a;
        if (! t.isValid() || (bool) t.getProperty ("default", false))
        {
            a = makeDefault();
            return (bool) t.getProperty ("normalized", false) ? normalizedCopy (*a) : a;
        }

        const String name = t.getProperty ("name").toString();
        const String path = t.getProperty ("origPath").toString();
        if (auto* mb = t.getProperty ("data").getBinaryData(); mb != nullptr && mb->getSize() > 0)
            a = fromMemory (std::make_shared<MemoryBlock> (*mb), name, path, error);
        else if (path.isNotEmpty())
        {
            a = fromFile (File (path), error);
            if (a == nullptr) error = "Missing sample: " + path;
        }
        if (a == nullptr) return nullptr;
        if ((bool) t.getProperty ("normalized", false)) a = normalizedCopy (*a);
        return a;
    }

    SampleAsset::Ptr normalizedCopy (const SampleAsset& src)
    {
        auto audio = std::make_shared<AudioData> (*src.audio);
        const int n = audio->buffer.getNumSamples();
        const float peak = std::max (audio->buffer.getMagnitude (0, 0, n), audio->buffer.getMagnitude (1, 0, n));
        if (peak > 1.0e-6f) audio->buffer.applyGain (1.0f / peak);
        auto a = finish (audio);
        a->fileBytes = src.fileBytes;
        a->name = src.name;
        a->origPath = src.origPath;
        a->isDefault = src.isDefault;
        a->normalized = true;
        a->slices = src.slices;
        a->bounds = a->slices.bounds();
        return a;
    }
} // namespace loader
} // namespace xtp
