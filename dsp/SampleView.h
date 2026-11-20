#pragma once
#include "HitEnvelope.h"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace xt
{
/// Non-owning view of decoded sample audio. Owner guarantees lifetime.
struct SampleView
{
    const float* ch[2] { nullptr, nullptr }; // ch[1] == ch[0] for mono files
    const float* mono { nullptr };           // (L+R)/2 mix (== ch[0] for mono files)
    int64_t frames { 0 };
    int numCh { 0 };
    double sampleRate { 48000.0 };

    bool valid() const noexcept { return frames > 0 && ch[0] != nullptr && mono != nullptr; }
};

/// Sample plus slice table, as seen by the audio thread.
struct SourceView
{
    SampleView sample;
    const int64_t* bounds { nullptr }; // numSlices + 1 entries, ascending, bounds[0] = 0, bounds[n] = frames
    int numSlices { 0 };
    const std::optional<HitEnvelope>* envelopes { nullptr }; // numSlices entries, empties use the trigger defaults

    bool valid() const noexcept { return sample.valid() && bounds != nullptr && numSlices > 0; }
};

inline int wrapSliceIndex (int index, int count) noexcept
{
    count = std::max (1, count);
    const int wrapped = index % count;
    return wrapped < 0 ? wrapped + count : wrapped;
}

struct SliceWindow { double start, end; };

inline SliceWindow sliceWindow (double start, double end, double scan, double length) noexcept
{
    const double span = std::max (1.0, end - start);
    const double size = std::max (1.0, std::clamp (length, 0.01, 1.0) * span);
    const double offset = std::clamp (scan, 0.0, 1.0) * (span - size);
    return { start + offset, start + offset + size };
}
} // namespace xt
