#pragma once
#include "HitVoice.h"
#include "RetriggerClock.h"

namespace xt
{
/// One unison lane: its own clock and voice pool.
class Lane
{
public:
    static constexpr int kMaxVoices = 33; // 32 overlap voices + 1 slot for a fading, stolen voice

    RetriggerClock clock;
    double detuneRatio { 1.0 };
    float gainL { 1.0f }, gainR { 1.0f };

    void killAll() noexcept
    {
        for (auto& v : voices) v.kill();
    }

    /// Start a new hit. At most `limit` voices are non-releasing afterwards.
    /// Cut also fades any previous release tails over at most `stealFade` samples.
    void trigger (const HitStart& h, int limit, double stealFade, bool cut = false) noexcept;

    void gateOff (double declickSamples) noexcept
    {
        for (auto& v : voices) v.gateOff (declickSamples);
    }

    void render (const SampleView& s, double pitchA, double pitchB, float& l, float& r) noexcept
    {
        float vl = 0.0f, vr = 0.0f;
        for (int i = 0; i < kMaxVoices; ++i)
            if (voices[i].isActive()) voices[i].render (s, voices[i].usesSliceB() ? pitchB : pitchA, vl, vr);
        l += vl * gainL;
        r += vr * gainR;
    }

    int activeCount() const noexcept;
    int nonReleasingCount() const noexcept;
    const HitVoice* newestVoice() const noexcept;

private:
    HitVoice voices[kMaxVoices];
    uint64_t stampCounter { 0 };
};
} // namespace xt
