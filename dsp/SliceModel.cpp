#include "SliceModel.h"

#include <algorithm>
#include <cstdlib>

namespace xt
{
void SliceModel::setLength (int64_t f)
{
    frames = f < 0 ? 0 : f;
    const auto end = std::lower_bound (markers.begin(), markers.end(), frames);
    std::fill (envelopes.begin() + (end - markers.begin()) + 1, envelopes.end(), std::nullopt);
    markers.erase (end, markers.end());
}

void SliceModel::setEqual (int n)
{
    std::vector<int64_t> next;
    n = std::clamp (n, 1, kMaxMarkers + 1);
    for (int i = 1; i < n; ++i)
        next.push_back ((int64_t) ((double) frames * i / n));
    setMarkers (std::move (next));
}

bool SliceModel::add (int64_t frame)
{
    if (frame <= 0 || frame >= frames || (int) markers.size() >= kMaxMarkers) return false;
    const auto position = std::lower_bound (markers.begin(), markers.end(), frame);
    if (position != markers.end() && *position == frame) return false;
    const auto slice = envelopes.begin() + (position - markers.begin()) + 1;
    const auto count = numSlices();
    markers.insert (position, frame);
    std::move_backward (slice, envelopes.begin() + count, envelopes.begin() + count + 1);
    slice->reset();
    return true;
}

void SliceModel::move (int index, int64_t frame)
{
    if (index < 0 || index >= (int) markers.size()) return;
    const int64_t lo = index > 0 ? markers[(size_t) index - 1] + 1 : 1;
    const int64_t hi = index + 1 < (int) markers.size() ? markers[(size_t) index + 1] - 1 : frames - 1;
    if (lo > hi) return;
    markers[(size_t) index] = std::clamp (frame, lo, hi);
}

void SliceModel::remove (int index)
{
    if (index >= 0 && index < (int) markers.size())
    {
        const auto slice = envelopes.begin() + index + 1;
        std::move (slice + 1, envelopes.begin() + numSlices(), slice);
        envelopes[(size_t) numSlices() - 1].reset();
        markers.erase (markers.begin() + index);
    }
}

void SliceModel::setMarkers (std::vector<int64_t> m)
{
    std::sort (m.begin(), m.end());
    m.erase (std::unique (m.begin(), m.end()), m.end());
    m.erase (std::remove_if (m.begin(), m.end(), [this] (int64_t frame) { return frame <= 0 || frame >= frames; }), m.end());
    if (m.size() > kMaxMarkers) m.resize (kMaxMarkers);

    // Preserve settings for starts that survive a bulk edit, including the implicit start at frame zero.
    decltype (envelopes) next;
    next.front() = envelopes.front();
    for (size_t i = 0; i < m.size(); ++i)
    {
        const auto match = std::lower_bound (markers.begin(), markers.end(), m[i]);
        if (match != markers.end() && *match == m[i])
            next[i + 1] = envelopes[(size_t) (match - markers.begin()) + 1];
    }
    envelopes = next;
    markers = std::move (m);
}

void SliceModel::setEnvelope (int slice, std::optional<HitEnvelope> envelope)
{
    if (slice >= 0 && slice < numSlices()) envelopes[(size_t) slice] = envelope;
}

std::vector<int64_t> SliceModel::bounds() const
{
    std::vector<int64_t> b;
    b.reserve (markers.size() + 2);
    b.push_back (0);
    b.insert (b.end(), markers.begin(), markers.end());
    b.push_back (frames);
    return b;
}

int SliceModel::findMarker (int64_t frame, int64_t maxDist) const noexcept
{
    int best = -1;
    int64_t bestD = maxDist + 1;
    for (size_t i = 0; i < markers.size(); ++i)
    {
        const int64_t d = std::llabs (markers[i] - frame);
        if (d < bestD) { bestD = d; best = (int) i; }
    }
    return best;
}

} // namespace xt
