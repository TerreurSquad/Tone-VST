#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "Engine.h"
#include "EditorSkin.h"
#include "Parameters.h"
#include "SampleLoader.h"

#include <functional>
#include <optional>

class XtoneProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int kChunk = 256; // host samples per internal chunk
    static constexpr int kMaxOs = 8;

    XtoneProcessor();
    ~XtoneProcessor() override;

    // ---- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- for the editor (message thread)
    juce::AudioProcessorValueTreeState apvts;
    const xt::Telemetry& telemetry() const noexcept { return engine.telemetry; }
    void auditionSlice (const xtp::SampleAsset& asset, int index, bool useB);
    float takeOutputPeak (int channel) noexcept { return outputPeaks[channel].exchange (0.0f, std::memory_order_relaxed); }
    bool hasOutputClipped() const noexcept { return outputClipped.load (std::memory_order_relaxed); }
    void clearOutputClip() noexcept { outputClipped.store (false, std::memory_order_relaxed); }

    /// Asynchronous load (decodes on a background thread).
    void loadSampleFile (const juce::File& f);
    void normalizeSample();
    /// Replace slice settings if the asset being edited is still current.
    bool setSlices (const xtp::SampleAsset& asset, xt::SliceModel slices);
    /// Analyse a snapshot off the message thread. Completion reports the marker count,
    /// or nullopt if the result was superseded by another edit or sample load.
    void detectTransients (xtp::SampleAsset::Ptr asset, double sensitivity,
                           std::function<void (std::optional<int>)> finished);

    xt::Sequence getSequence() const;
    void setSequenceStep (int index, const xt::SequenceStep& step);
    void setSequence (const xt::Sequence& steps);

    xtp::SampleAsset::Ptr getCurrentAsset() const;
    juce::String getStatusMessage() const;
    /// Incremented whenever the asset shown in the GUI changes.
    int getAssetVersion() const noexcept { return assetVersion.load(); }

    bool saveStateToFile (const juce::File& f);
    bool loadStateFromFile (const juce::File& f);
    xtp::EditorSkin getEditorSkin() const noexcept { return editorSkin.load (std::memory_order_relaxed); }
    void setEditorSkin (xtp::EditorSkin skin) noexcept { editorSkin.store (skin, std::memory_order_relaxed); }

private:
    void timerCallback() override;
    bool publish (xtp::SampleAsset::Ptr asset, const xtp::SampleAsset* expected = nullptr);
    void swapPendingAsset() noexcept;            // audio thread
    void setOversampling (int index) noexcept;   // audio thread / prepare
    juce::ValueTree buildState();
    void restoreState (const juce::ValueTree& t);
    void replaceSequence (const xt::Sequence& steps);
    void readSequence (xt::Sequence& dest) const noexcept;
    void snapshotSequence() noexcept;

    xtp::ParamSnapshot snapshot;
    xt::Engine engine;
    xt::EngineParams engineParams;

    // Writes and saves are serialized off the audio thread. The audio thread
    // takes one bounded snapshot attempt and retains the last complete pattern
    // if a write is in progress.
    mutable juce::CriticalSection sequenceLock;
    std::array<std::atomic<uint64_t>, xt::kMaxSequenceSteps> sequenceWords;
    std::atomic<unsigned> sequenceVersion { 0 };
    unsigned audioSequenceVersion = 0;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversamplers[3]; // 2x, 4x, 8x
    int osIndex { -1 };
    juce::AudioBuffer<float> zeroBuffer, directBuffer;
    double hostRate { 48000.0 };

    std::vector<xt::MidiEvent> events; // preallocated
    std::atomic<uint64_t> auditionRequest { 0 }; // publication in the upper bits, (slice + 1) and B flag below
    std::atomic<float> outputPeaks[2] {};
    std::atomic<bool> outputClipped { false };

    // asset hand-over
    std::atomic<xtp::SampleAsset*> pending { nullptr };
    xtp::SampleAsset* audioAsset { nullptr }; // owned (one reference) by the audio thread
    static constexpr int kRetireSize = 32;
    juce::AbstractFifo retireFifo { kRetireSize };
    xtp::SampleAsset* retired[kRetireSize] {};

    mutable juce::CriticalSection guiLock; // guards guiAsset + statusMessage (never taken on the audio thread)
    xtp::SampleAsset::Ptr guiAsset;
    juce::String statusMessage;
    std::atomic<int> assetVersion { 0 };

    std::shared_ptr<int> lifetimeToken = std::make_shared<int> (0);
    juce::ThreadPool loaderPool { 1 };
    int currentProgram { 0 };
    std::atomic<xtp::EditorSkin> editorSkin { xtp::EditorSkin::hammerDrill };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XtoneProcessor)
};
