#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>

#include "SampleView.h"
#include "SliceModel.h"

#include <memory>

class XtoneProcessor;

namespace xtp
{
/// Decoded audio: channel 0 = L, 1 = R (copy of L for mono), 2 = mono mix.
struct AudioData
{
    juce::AudioBuffer<float> buffer;
    double sampleRate { 48000.0 };
    int numSourceChannels { 1 };
    int64_t frames() const noexcept { return buffer.getNumSamples(); }
};

/// Immutable sample + slices as handed to the audio thread. Slice edits create
/// a new asset sharing the audio data (copy-on-write).
class SampleAsset : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<SampleAsset>;

    static constexpr size_t kMaxEmbedBytes = 32u * 1024u * 1024u;
    static constexpr int kPeakBlock = 256; // frames per peak-cache entry

    std::shared_ptr<const AudioData> audio;
    std::shared_ptr<const juce::MemoryBlock> fileBytes; // original file (only if embedded)
    std::shared_ptr<const std::vector<float>> peaks;     // min,max pairs of the mono mix per kPeakBlock
    juce::String name, origPath;
    bool isDefault { false };
    bool normalized { false };

    xt::SliceModel slices;
    std::vector<int64_t> bounds;

    xt::SourceView view() const noexcept;

    /// Same audio, new slice markers or envelopes.
    Ptr withSlices (xt::SliceModel next) const;

    /// SAMPLE + SLICES child trees for the plugin state (built once and cached).
    juce::ValueTree getSampleTree() const;
    juce::ValueTree getSlicesTree() const;

private:
    friend class ::XtoneProcessor;
    uint32_t publicationId = 0;
    mutable juce::ValueTree cachedSampleTree;
};

/// Decoding / building helpers (call on a background or message thread, never on the audio thread).
namespace loader
{
    /// Synthetic default kick.
    SampleAsset::Ptr makeDefault();

    /// Decode from memory (original file bytes). Returns nullptr + error on failure.
    SampleAsset::Ptr fromMemory (std::shared_ptr<const juce::MemoryBlock> bytes, const juce::String& name,
                                 const juce::String& path, juce::String& error);

    /// Load a file; embeds the bytes if <= kMaxEmbedBytes.
    SampleAsset::Ptr fromFile (const juce::File& f, juce::String& error);

    /// Restore from a SAMPLE state node (embedded data or path).
    SampleAsset::Ptr fromStateTree (const juce::ValueTree& sample, juce::String& error);

    /// Copy with audio normalised to 0 dBFS peak (or the original if `on` is false and it has file bytes).
    SampleAsset::Ptr normalizedCopy (const SampleAsset& a);
} // namespace loader
} // namespace xtp
