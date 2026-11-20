#pragma once

#include <array>

namespace xt
{
struct SequenceStep
{
    bool on { true };
    bool tie { false };
    int slice { -1 }; // -1 follows the selected or MIDI slice
    int pitch { 0 };  // sample playback offset in semitones
    float velocity { 1.0f };
};

inline constexpr int kMaxSequenceSteps = 32;
using Sequence = std::array<SequenceStep, kMaxSequenceSteps>;

class StepSequencer
{
public:
    void reset() noexcept { next = 0; current = -1; }

    int tick (int length) noexcept
    {
        current = next % length;
        next = (current + 1) % length;
        return current;
    }

    int currentStep() const noexcept { return current; }

private:
    int next = 0;
    int current = -1;
};
} // namespace xt
