#pragma once

namespace xt
{
/// Amplitude settings latched when a hit starts.
struct HitEnvelope
{
    static constexpr float kMaxTimeMs = 2000.0f;
    static constexpr float kTimeStepMs = 0.01f;
    static constexpr float kTimeMidpointMs = 50.0f;

    float attackMs { 0.0f };
    float decayMs { 0.0f };
    float sustain { 1.0f };
    float releaseMs { 0.0f };

    bool operator== (const HitEnvelope&) const = default;
};
} // namespace xt
