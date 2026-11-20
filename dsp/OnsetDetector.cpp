#include "OnsetDetector.h"
#include "SliceModel.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace xt
{
namespace
{
constexpr size_t kHop = 256;
constexpr size_t kMedianRadius = 7;

double sampleAt (std::span<const float> mono, size_t frame)
{
    return std::isfinite (mono[frame]) ? (double) mono[frame] : 0.0;
}

size_t locateAttack (std::span<const float> mono, size_t start)
{
    const size_t end = std::min (start + kHop, mono.size());
    const auto strength = [mono] (size_t frame)
    {
        const double sample = sampleAt (mono, frame);
        const double previous = frame == 0 ? 0.0 : sampleAt (mono, frame - 1);
        return std::abs (sample) + std::abs (sample - previous);
    };

    double peak = 0.0;
    for (size_t frame = start; frame < end; ++frame)
        peak = std::max (peak, strength (frame));

    double baseline = 0.0;
    if (start > 0)
    {
        for (size_t frame = start - kHop; frame < start; ++frame)
            baseline += strength (frame);
        baseline /= kHop;
    }

    // Refine the hop to the leading edge above the preceding signal level.
    constexpr double attackFraction = 0.12;
    const double threshold = baseline + attackFraction * (peak - baseline);
    for (size_t frame = start; frame < end; ++frame)
        if (strength (frame) >= threshold) return frame;
    return start;
}

struct Candidate
{
    int64_t frame;
    double score;
};
} // namespace

std::vector<int64_t> detectOnsets (std::span<const float> mono, double sampleRate, double sensitivity)
{
    if (mono.empty() || ! std::isfinite (sampleRate) || sampleRate <= 0.0) return {};
    sensitivity = std::isfinite (sensitivity) ? std::clamp (sensitivity, 0.0, 1.0) : 0.5;

    std::vector<double> energy;
    energy.reserve ((mono.size() - 1) / kHop + 1);
    double previousSample = 0.0;
    for (size_t start = 0; start < mono.size(); start += kHop)
    {
        const size_t end = std::min (start + kHop, mono.size());
        double sum = 0.0;
        for (size_t frame = start; frame < end; ++frame)
        {
            const double sample = sampleAt (mono, frame);
            const double delta = sample - previousSample;
            sum += sample * sample + 2.0 * delta * delta;
            previousSample = sample;
        }
        energy.push_back (sum / (double) kHop);
    }

    const double peakEnergy = *std::max_element (energy.begin(), energy.end());
    const double noiseFloor = std::max (1.0e-12, peakEnergy * 1.0e-6);
    double previousLog = std::log (noiseFloor);
    // Positive log-energy change, stored in place for the median filter.
    for (auto& value : energy)
    {
        const double currentLog = std::log (std::max (value, noiseFloor));
        value = std::max (0.0, currentLog - previousLog);
        previousLog = currentLog;
    }

    const double medianMultiplier = 1.0 + 2.0 * (1.0 - sensitivity);
    const double minimumRise = 0.15 + 0.85 * (1.0 - sensitivity);
    std::vector<Candidate> candidates;
    for (size_t i = 0; i < energy.size(); ++i)
    {
        const double score = energy[i];
        if (score < minimumRise || (i > 0 && score <= energy[i - 1])
            || (i + 1 < energy.size() && score < energy[i + 1])) continue;

        const size_t first = i > kMedianRadius ? i - kMedianRadius : 0;
        const size_t last = std::min (i + kMedianRadius + 1, energy.size());
        std::array<double, 2 * kMedianRadius + 1> window {};
        std::copy_n (energy.data() + first, last - first, window.begin());
        const auto middle = window.begin() + kMedianRadius;
        std::nth_element (window.begin(), middle, window.end());
        if (score <= *middle * medianMultiplier) continue;

        const auto frame = (int64_t) locateAttack (mono, i * kHop);
        if (frame > 0) candidates.push_back ({ frame, score }); // Frame 0 is already a slice boundary.
    }

    // Keep the strongest attack within each 30 ms neighbourhood and respect the slice limit.
    std::sort (candidates.begin(), candidates.end(), [] (const Candidate& a, const Candidate& b)
    {
        return a.score == b.score ? a.frame < b.frame : a.score > b.score;
    });
    const double minSpacing = sampleRate * 0.03;
    std::vector<int64_t> markers;
    for (const auto& candidate : candidates)
    {
        if (std::any_of (markers.begin(), markers.end(), [&] (int64_t frame)
                        { return (double) std::abs (frame - candidate.frame) < minSpacing; })) continue;
        markers.push_back (candidate.frame);
        if (markers.size() == SliceModel::kMaxMarkers) break;
    }
    std::sort (markers.begin(), markers.end());
    return markers;
}
} // namespace xt
