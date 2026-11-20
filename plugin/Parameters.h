#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

#include "Engine.h"

namespace xtp
{
inline constexpr const char* triggerEnvelopeIds[] { "hit_attack", "hit_decay", "hit_sustain", "hit_release" };

/// Parameter layout. IDs are STABLE - never rename or remove after the first release,
/// only append new ones with a higher version hint.
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/// Raw atomics of all parameters, resolved once; fills the engine snapshot per chunk.
class ParamSnapshot
{
public:
    explicit ParamSnapshot (juce::AudioProcessorValueTreeState& state);
    void read (xt::EngineParams& p) const noexcept;
    int osIndex() const noexcept;

private:
    struct Lfo
    {
        std::atomic<float>*shape, *sync, *hz, *div, *ratio, *retrig, *phase, *toRate, *toScan, *toGate, *toPitch;
    };
    std::atomic<float>*rateSrc, *bpm, *division, *divMod, *hz, *hostMult, *hostLock, *rateSmooth;
    std::atomic<float>*midiMode, *keyMode, *keyRoot, *sliceBase, *transpose, *fine, *a4, *glide, *glideLegato,
        *pbRange, *phaseReset, *velSens;
    std::atomic<float>*snapMode, *snapScale, *snapRoot;
    std::atomic<float>*trigMode, *declick, *gate, *voices, *jitter;
    std::atomic<float>*hitAttack, *hitDecay, *hitSustain, *hitRelease;
    std::atomic<float>*slice, *scan, *length, *reverse, *interp, *mono, *sPitch, *sFine, *penvAmt, *penvDecay;
    std::atomic<float>*abMode, *abSlice, *abPitch, *abGain;
    std::atomic<float>*abScan, *abLength, *abReverse, *abInterp, *abMono, *abSamplePitch, *abFine, *abPenvAmt, *abPenvDecay;
    std::atomic<float>*rampOn, *rampStart, *rampEnd, *rampTime, *rampCurve, *rampLoop, *rampLegato;
    Lfo lfo[2];
    std::atomic<float>*uniVoices, *uniDetune, *uniSpread, *uniPhase;
    std::atomic<float>*seqOn, *seqLen, *seqReset;
    std::atomic<float>*envA, *envD, *envS, *envR;
    std::atomic<float>*os, *drive, *clip, *outGain;
};

} // namespace xtp
