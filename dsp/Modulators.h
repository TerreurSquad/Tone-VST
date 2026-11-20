#pragma once
#include "Random.h"

#include <algorithm>
#include <cmath>

namespace xt
{
/// One-pole smoother (exponential). timeMs = 0 -> jumps immediately.
class OnePole
{
public:
    void setTime (double timeMs, double fs) noexcept
    {
        const double n = timeMs * 0.001 * fs;
        coef = n <= 0.0 ? 1.0 : 1.0 - std::exp (-1.0 / n);
    }
    void reset (double v) noexcept { value = v; }
    double process (double target) noexcept
    {
        value += (target - value) * coef;
        return value;
    }
    double get() const noexcept { return value; }

private:
    double value { 0.0 };
    double coef { 1.0 };
};

/// Note-triggered offset in octaves. Time is kept in seconds so oversampling
/// can change during a note without changing the ramp's position.
class Ramp
{
public:
    void reset() noexcept { elapsed = 0.0; }

    void configure (double ms, double curve, int mode, double fs) noexcept
    {
        duration = std::max (ms, 1.0) * 0.001;
        exponent = std::exp2 (3.0 * curve);
        repeat = mode;
        sampleTime = 1.0 / fs;
    }

    double process (double start, double end) noexcept
    {
        double progress = elapsed / duration;
        if (repeat == 0)
        {
            progress = std::min (progress, 1.0);
            elapsed = std::min (elapsed + sampleTime, duration);
        }
        else
        {
            const double period = repeat == 1 ? 1.0 : 2.0;
            if (progress >= period - 1.0e-10)
            {
                progress = std::max (0.0, progress - period * std::floor ((progress + 1.0e-10) / period));
                elapsed = progress * duration;
            }
            elapsed += sampleTime;
            if (repeat == 2 && progress > 1.0) progress = 2.0 - progress;
        }
        return std::lerp (start, end, std::pow (progress, exponent));
    }

private:
    double elapsed = 0.0, duration = 1.0, exponent = 1.0, sampleTime = 0.0;
    int repeat = 0;
};

inline constexpr int lfoRatioHits[] { 1, 2, 3, 4, 6, 8, 12, 16, 32, 64 };
inline constexpr double lfoHostBeats[] { 32, 16, 8, 4, 2, 1, 0.5, 0.25, 0.125,
                                          2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.5, 0.75, 0.375 };

class Lfo
{
public:
    void setSeed (uint64_t seed) noexcept { random.setSeed (seed); }
    void reset (double start) noexcept
    {
        phase = start - std::floor (start);
        previous = random.nextBipolar();
        next = random.nextBipolar();
    }

    void advance (double increment) noexcept
    {
        phase += increment;
        while (phase >= 1.0)
        {
            phase -= 1.0;
            nextCycle();
        }
    }

    /// Correct floating-point drift and re-anchor after a group of clock ticks.
    void anchor (double start) noexcept
    {
        const double target = start - std::floor (start);
        if (phase - target > 0.5) nextCycle();
        phase = target;
    }

    double value (int shape) const noexcept
    {
        constexpr double pi = 3.14159265358979323846;
        switch (shape)
        {
            case 1: return 1.0 - 4.0 * std::abs (phase - 0.5);
            case 2: return 2.0 * phase - 1.0;
            case 3: return 1.0 - 2.0 * phase;
            case 4: return phase < 0.5 ? 1.0 : -1.0;
            case 5: return previous;
            case 6: return std::lerp (previous, next, 0.5 - 0.5 * std::cos (pi * phase));
            default: return std::sin (2.0 * pi * phase);
        }
    }

private:
    void nextCycle() noexcept
    {
        previous = next;
        next = random.nextBipolar();
    }

    Random random;
    double phase = 0.0, previous = 0.0, next = 0.0;
};

/// Linear-attack / exponential decay & release ADSR on the lane sum.
class Adsr
{
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    void setSampleRate (double newFs) noexcept { fs = newFs; }

    void setParams (double aMs, double dMs, double sustain01, double rMs) noexcept
    {
        attackInc = aMs <= 0.0 ? 2.0 : 1.0 / (aMs * 0.001 * fs);
        decayCoef = timeToCoef (dMs);
        sustain = sustain01;
        releaseCoef = timeToCoef (rMs);
        releaseInstant = rMs <= 0.0;
    }

    void noteOn() noexcept { stage = Stage::Attack; }
    void noteOff() noexcept
    {
        if (stage == Stage::Idle) return;
        stage = releaseInstant ? Stage::Idle : Stage::Release;
        if (releaseInstant) level = 0.0;
    }
    void reset() noexcept { stage = Stage::Idle; level = 0.0; }

    bool isIdle() const noexcept { return stage == Stage::Idle; }
    bool isReleasing() const noexcept { return stage == Stage::Release; }
    double getLevel() const noexcept { return level; }
    Stage getStage() const noexcept { return stage; }

    double process() noexcept
    {
        switch (stage)
        {
            case Stage::Idle: level = 0.0; break;
            case Stage::Attack:
                level += attackInc;
                if (level >= 1.0) { level = 1.0; stage = Stage::Decay; }
                break;
            case Stage::Decay:
                level = sustain + (level - sustain) * decayCoef;
                if (std::abs (level - sustain) < 1.0e-5) { level = sustain; stage = Stage::Sustain; }
                break;
            case Stage::Sustain: level = sustain; break;
            case Stage::Release:
                level *= releaseCoef;
                if (level < 1.0e-5) { level = 0.0; stage = Stage::Idle; }
                break;
        }
        return level;
    }

private:
    // time to fall to -80 dB (factor 1e-4)
    double timeToCoef (double ms) const noexcept
    {
        const double n = ms * 0.001 * fs;
        return n <= 0.0 ? 0.0 : std::exp (std::log (1.0e-4) / n);
    }

    double fs { 48000.0 };
    Stage stage { Stage::Idle };
    double level { 0.0 };
    double attackInc { 1.0 }, decayCoef { 0.0 }, sustain { 1.0 }, releaseCoef { 0.0 };
    bool releaseInstant { false };
};
} // namespace xt
