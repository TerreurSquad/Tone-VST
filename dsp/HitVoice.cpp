#include "HitVoice.h"

#include <cmath>

namespace xt
{
static constexpr double kPi = 3.14159265358979323846;

void HitVoice::start (const HitStart& h, uint64_t newStamp) noexcept
{
    p = h;
    active = p.winEnd > p.winStart;
    fadingOut = false;
    fadeOutT = fadeOutLen = 0.0;
    fadeOutGain = 1.0;
    gatedAt = -1.0;
    t = p.subOffset;
    stamp = newStamp;
    penv = p.penvAmtSt;
    if (p.penvAmtSt != 0.0 && p.subOffset > 0.0)
        penv *= std::pow (p.penvCoef, p.subOffset);
    const double penvMul = p.penvAmtSt != 0.0 ? std::exp2 (penv / 12.0) : 1.0;
    const double travel = p.subOffset * p.speedRatio * p.startPitchRatio * penvMul;
    pos = p.reverse ? p.winEnd - travel : p.winStart + travel;
}

void HitVoice::release (double n) noexcept
{
    if (! active) return;
    if (n <= 0.0)
    {
        kill();
        return;
    }
    if (fadingOut && n >= fadeOutLen - fadeOutT) return;
    fadeOutGain = fadingOut ? releaseGain() : 1.0;
    fadingOut = true;
    fadeOutT = 0.0;
    fadeOutLen = n;
}

double HitVoice::releaseGain() const noexcept
{
    return fadeOutGain * (0.5 + 0.5 * std::cos (kPi * std::min (1.0, fadeOutT / fadeOutLen)));
}

void HitVoice::gateOff (double declickSamples) noexcept
{
    const double duration = std::max (declickSamples, p.releaseSamples);
    if (duration <= 0.0)
    {
        kill();
        return;
    }
    if (! active || fadingOut) return;
    gatedAt = p.gateSamples >= 0.0 ? std::min (t, p.gateSamples) : t;
    release (duration);
}

double HitVoice::envelopeLevel (double elapsed) const noexcept
{
    if (elapsed < p.attackSamples) return elapsed / p.attackSamples;
    if (const double decayTime = elapsed - p.attackSamples; decayTime < p.decaySamples)
        return 1.0 - (1.0 - p.sustain) * decayTime / p.decaySamples;
    return p.sustain;
}

float HitVoice::read (const float* d, int64_t frames, double x, Interp interp) noexcept
{
    // Outside the sample is silence (zero padding).
    auto at = [d, frames] (int64_t i) noexcept -> float { return (i >= 0 && i < frames) ? d[i] : 0.0f; };

    const double fl = std::floor (x);
    const int64_t i = (int64_t) fl;
    const float f = (float) (x - fl);
    switch (interp)
    {
        case Interp::Raw: return at (i);
        case Interp::Linear:
        {
            const float y0 = at (i), y1 = at (i + 1);
            return y0 + (y1 - y0) * f;
        }
        case Interp::Hermite:
        default:
        {
            const float ym1 = at (i - 1), y0 = at (i), y1 = at (i + 1), y2 = at (i + 2);
            const float c0 = y0;
            const float c1 = 0.5f * (y1 - ym1);
            const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
            const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
            return ((c3 * f + c2) * f + c1) * f + c0;
        }
    }
}

void HitVoice::render (const SampleView& s, double pitchMul, float& l, float& r) noexcept
{
    if (! active) return;

    // Gate end starts the hit's release stage.
    if (! fadingOut && p.gateSamples >= 0.0 && t >= p.gateSamples)
    {
        gateOff (p.fadeSamples);
        if (! active) return;
    }

    double speed = p.speedRatio * pitchMul;
    if (p.penvAmtSt != 0.0)
    {
        speed *= std::exp2 (penv / 12.0);
        penv *= p.penvCoef;
    }

    // window end
    const double remainingFrames = p.reverse ? (pos - p.winStart) : (p.winEnd - pos);
    if (remainingFrames <= 0.0)
    {
        active = false;
        return;
    }

    double env = envelopeLevel (gatedAt >= 0.0 ? gatedAt : t);
    if (p.fadeSamples > 0.0 || p.releaseSamples > 0.0)
    {
        const double remainingOut = speed > 0.0 ? remainingFrames / speed : 1.0e30;
        if (remainingOut < p.releaseSamples)
            env *= remainingOut / p.releaseSamples;
        if (t < p.fadeSamples)
            env *= 0.5 - 0.5 * std::cos (kPi * t / p.fadeSamples);
        if (remainingOut < p.fadeSamples)
            env *= 0.5 - 0.5 * std::cos (kPi * remainingOut / p.fadeSamples);
    }
    if (fadingOut)
    {
        if (fadeOutT >= fadeOutLen)
        {
            active = false;
            return;
        }
        env *= releaseGain();
        fadeOutT += 1.0;
    }

    const float g = (float) env * p.gain;
    if (p.mono || s.numCh < 2)
    {
        const float v = read (s.mono, s.frames, pos, p.interp) * g;
        l += v;
        r += v;
    }
    else
    {
        l += read (s.ch[0], s.frames, pos, p.interp) * g;
        r += read (s.ch[1], s.frames, pos, p.interp) * g;
    }

    pos += p.reverse ? -speed : speed;
    t += 1.0;
}
} // namespace xt
