#pragma once
#include "Lane.h"
#include "Modulators.h"
#include "Random.h"
#include "SampleView.h"
#include "StepSequencer.h"

#include <atomic>
#include <cstdint>

namespace xt
{
/// Plain snapshot of all automatable parameters (choice params as indices,
/// percentages as 0..1, times in ms, gains in dB). Mirrors plugin/Parameters.
struct EngineParams
{
    // Rate
    int rateSrc { 0 };          // 0 BPM, 1 Hz, 2 Host
    double bpm { 500.0 };
    int division { 4 };         // 1/16
    int divMod { 0 };
    double hz { 33.33 };
    int hostMult { 0 };         // index: x(2^i)
    bool hostLock { true };
    double rateSmoothMs { 2.0 };
    // MIDI
    int midiMode { 0 };         // 0 Pitch, 1 Slice
    int keyMode { 0 };          // 0 Relative, 1 Absolute
    int keyRoot { 60 };
    int sliceBase { 60 };
    double transpose { 0.0 };
    double fine { 0.0 };        // cents
    double a4 { 440.0 };
    double glideMs { 0.0 };
    bool glideLegato { true };
    double pbRange { 12.0 };
    bool phaseReset { true };
    double velSens { 0.5 };
    // Snap
    int snapMode { 0 };         // 0 off, 1 base, 2 all
    int snapScale { 0 };
    int snapRoot { 0 };
    // Retrigger
    int trigMode { 0 };         // 0 Cut, 1 Overlap
    double declickMs { 0.0 };
    double gate { 1.0 };
    int voices { 8 };
    double jitter { 0.0 };
    HitEnvelope hitEnvelope;
    // Sample
    int slice { 0 };
    double scan { 0.0 };
    double length { 1.0 };
    bool reverse { false };
    int interp { 0 };
    bool mono { true };
    double sPitch { 0.0 };
    double sFine { 0.0 };
    double penvAmt { 0.0 };
    double penvDecayMs { 50.0 };
    // Cycling
    int abMode { 0 };
    int abSlice { 1 };
    double abPitch { 0.0 };     // additional semitone offset for B
    double abGainDb { 0.0 };
    double abScan { 0.0 }, abLength { 1.0 };
    bool abReverse { false };
    int abInterp { 0 };
    bool abMono { true };
    double abSamplePitch { 0.0 }, abFine { 0.0 };
    double abPenvAmt { 0.0 }, abPenvDecayMs { 50.0 };
    // Ramp
    bool rampOn { false };
    double rampStart { -2.0 };
    double rampEnd { 0.0 };
    double rampTimeMs { 1000.0 };
    double rampCurve { 0.0 };
    int rampLoop { 0 };
    bool rampLegato { false };
    // LFOs
    struct Lfo
    {
        int shape { 0 };
        int sync { 0 };
        double hz { 1.0 };
        int div { 5 };
        int ratio { 5 };
        bool retrig { true };
        double phaseDeg { 0.0 };
        double toRate { 0.0 };
        double toScan { 0.0 };
        double toGate { 0.0 };
        double toPitch { 0.0 };
    } lfo[2];
    // Unison
    int uniVoices { 1 };
    double uniDetune { 10.0 };  // cents
    double uniSpread { 0.5 };
    bool uniPhase { true };
    // Sequencer
    bool seqOn { false };
    int seqLen { 16 };
    bool seqReset { true };
    Sequence steps;
    // Envelope
    double envA { 0.0 }, envD { 100.0 }, envS { 1.0 }, envR { 5.0 };
    // Output
    int os { 1 };               // handled by the plugin
    double driveDb { 0.0 };
    int clip { 2 };             // 0 off, 1 soft, 2 hard
    double outGainDb { -3.0 };
};

struct MidiEvent
{
    enum Type : uint8_t { NoteOn, NoteOff, PitchBend, Sustain, AllNotesOff };
    int offset { 0 };     // engine samples relative to the process() call
    Type type { NoteOn };
    int note { 0 };
    float value { 0.0f }; // velocity 0..1 / bend -1..1 / sustain 0|1
};

struct HostInfo
{
    bool valid { false };
    double bpm { 120.0 };
    double ppq { 0.0 };   // at the first sample of the next process() call
    bool playing { false };
};

/// Values for the GUI (written by the audio thread).
struct Telemetry
{
    std::atomic<float> rateHz { 0.0f };
    std::atomic<float> rampBaseHz { 0.0f };
    std::atomic<int> slice { 0 };
    std::atomic<int> numSlices { 1 };
    std::atomic<int> step { -1 };
    std::atomic<float> envelopeLevel { 0.0f };
    std::atomic<Adsr::Stage> envelopeStage { Adsr::Stage::Idle };
    std::atomic<float> playPos { -1.0f }; // fraction of the sample, -1 = none
    std::atomic<float> auditionPos { -1.0f };
    std::atomic<int> auditionSlice { -1 };
    std::atomic<bool> active { false };
    std::atomic<int> voices { 0 };
};

class Engine
{
public:
    static constexpr int kMaxLanes = 8;
    static constexpr int kMaxTotalVoices = 64;

    Engine();

    /// Sets the engine sample rate (host rate x oversampling). Real-time safe;
    /// active voices are stopped.
    void setEngineRate (double fsE) noexcept;
    double getEngineRate() const noexcept { return fsE; }

    /// Swap the sample source without retaining the old view. Keep voices only
    /// when the source audio and slice boundaries are identical.
    void setSource (const SourceView& src, bool keepVoices = false) noexcept;

    void setParams (const EngineParams& p) noexcept { params = p; paramsDirty = true; }
    const EngineParams& getParams() const noexcept { return params; }
    void setHostInfo (const HostInfo& h) noexcept;
    void auditionSlice (int index, bool useB = false) noexcept;

    /// Renders n samples (overwrites l / r). Events must be sorted by offset.
    void process (float* l, float* r, int n, const MidiEvent* events, int numEvents) noexcept;

    void reset() noexcept;

    Telemetry telemetry;

    // ---- introspection for tests
    int activeVoiceCount() const noexcept;
    int activeLaneCount() const noexcept { return numLanes; }
    const Lane& lane (int i) const noexcept { return lanes[i]; }
    double currentRateHz() const noexcept { return lastRateHz; }
    double laneDetuneCents (int i) const noexcept;
    float lanePan (int i) const noexcept;
    double getLaneGain() const noexcept { return laneGain; }
    /// Test hook: bypass the DC blocker (always on in the plugin).
    void setDcBlockerEnabled (bool on) noexcept { dcEnabled = on; }

private:
    void updateTargets() noexcept;
    void handleEvent (const MidiEvent& e) noexcept;
    void noteOn (int note, float vel) noexcept;
    void noteOff (int note) noexcept;
    void removeNote (int note) noexcept;
    void afterRelease (int releasedNote) noexcept;
    void updateSnapOffset() noexcept;
    void configureLanes() noexcept;
    void fireHit (int laneIndex, double subOffset, double periodSamples, double scanMod, double gateMod,
                  double pitchA, double pitchB) noexcept;
    HitStart makeHit (int slice, bool useB, double scanMod = 0.0) const noexcept;
    void advanceLfos (double samples, double clockIncrement) noexcept;
    void syncLfosToHit() noexcept;
    double modulation (double EngineParams::Lfo::* depth) const noexcept;
    bool nextHitUsesB (int laneIndex) noexcept;
    int currentSlice() const noexcept;
    double noteRef() const noexcept;

    EngineParams params;
    bool paramsDirty { true };
    double fsE { 96000.0 };
    SourceView source;

    // notes
    int stack[128] {};
    int stackSize { 0 };
    bool sustainedFlag[128] {};
    bool sustainPedal { false };
    int curNote { -1 };
    bool hadNote { false };
    float velocity { 1.0f };
    double bendTarget { 0.0 };
    bool resetPending { false };
    bool gliding { false };

    // rate
    double paramStTarget { 0.0 };
    double srcHz { 1.0 };
    double snapOffset { 0.0 };
    OnePole paramSm, bendSm, sPitchSm, bPitchSm, velSm, driveSm, outSm;
    double glideNote { 60.0 };
    double glideCoef { 1.0 };
    double lastRateHz { 0.0 }, lastRampBaseSt { 0.0 };

    Ramp ramp;
    struct LfoState
    {
        Lfo oscillator;
        double increment = 0.0;
        int sync = -1, ratioHits = 1, ticks = 0;
    } lfos[2];
    Adsr adsr;
    bool running { false };

    Lane lanes[kMaxLanes];
    StepSequencer sequencers[kMaxLanes];
    bool sequenceEnabled = false;
    int numLanes { 1 };
    double laneGain { 1.0 };
    int configuredLanes { -1 };
    double configuredDetune { -1.0 }, configuredSpread { -1.0 };
    Random rng { 0xC0FFEEull };
    Random cyclingRandom { 0xB7C13ull };
    unsigned cyclingTicks[kMaxLanes] {};
    int cyclingMode { 0 };
    double cyclingPitchRatio { 1.0 };
    float cyclingGain { 1.0f };
    HitVoice audition;

    // host
    HostInfo host;
    bool lastPlaying { false };
    double expectedPpq { 0.0 };
    bool havePpq { false };

    // output
    double dcR { 0.9997 };
    bool dcEnabled { true };
    double dcX1[2] {}, dcY1[2] {};
};
} // namespace xt
