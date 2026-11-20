#pragma once
#include "SampleView.h"

#include <cstdint>

namespace xt
{
enum class Interp : int { Hermite = 0, Linear, Raw };

/// Everything a voice needs at hit start.
struct HitStart
{
    double winStart { 0.0 };  // source frames, inclusive
    double winEnd { 0.0 };    // source frames, exclusive
    bool reverse { false };
    double subOffset { 0.0 }; // engine samples elapsed since the exact hit time
    double speedRatio { 1.0 };// source frames per engine sample, without global pitch mod / pitch env
    double startPitchRatio { 1.0 }; // instantaneous pitch for sub-sample start
    double gateSamples { -1.0 }; // < 0 = no gate
    double fadeSamples { 0.0 };  // declick length in engine samples (fade-in, gate fade-out, window end)
    double penvAmtSt { 0.0 };    // pitch envelope amount (semitones)
    double penvCoef { 0.0 };     // per-sample decay factor of the pitch envelope
    double attackSamples { 0.0 }, decaySamples { 0.0 }, releaseSamples { 0.0 };
    float sustain { 1.0f };
    float gain { 1.0f };
    Interp interp { Interp::Hermite };
    bool mono { true };
    int sliceIndex { 0 };
    bool usesSliceB { false };
};

/// Plays one hit of a sample window.
class HitVoice
{
public:
    void start (const HitStart& h, uint64_t stamp) noexcept;

    /// Immediately silences the voice.
    void kill() noexcept { active = false; }

    /// Fade out over n engine samples (0 = kill).
    void release (double n) noexcept;
    /// Enter the hit envelope's release stage, using declick for short releases.
    void gateOff (double declickSamples) noexcept;

    bool isActive() const noexcept { return active; }
    bool isReleasing() const noexcept { return active && fadingOut; }
    uint64_t getStamp() const noexcept { return stamp; }
    double getPosition() const noexcept { return pos; }
    int getSliceIndex() const noexcept { return p.sliceIndex; }
    bool usesSliceB() const noexcept { return p.usesSliceB; }

    /// Renders one sample, accumulates into l/r. `pitchMul` = global sample-pitch factor.
    void render (const SampleView& s, double pitchMul, float& l, float& r) noexcept;

private:
    static float read (const float* data, int64_t frames, double pos, Interp interp) noexcept;
    double envelopeLevel (double elapsed) const noexcept;
    double releaseGain() const noexcept;

    HitStart p;
    bool active { false };
    bool fadingOut { false };
    double pos { 0.0 };
    double t { 0.0 };          // engine samples since hit (starts at subOffset)
    double fadeOutT { 0.0 }, fadeOutLen { 0.0 }, fadeOutGain { 1.0 };
    double gatedAt { -1.0 };
    double penv { 0.0 };       // current pitch env value (semitones)
    uint64_t stamp { 0 };
};
} // namespace xt
