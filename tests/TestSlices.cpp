#include "OnsetDetector.h"
#include "SliceModel.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>

using namespace xt;

TEST_CASE ("SliceModel basics", "[slices]")
{
    SliceModel m;
    m.setLength (1000);
    CHECK (m.numSlices() == 1);
    m.setEqual (4);
    CHECK (m.getMarkers() == std::vector<int64_t> { 250, 500, 750 });
    CHECK (m.bounds() == std::vector<int64_t> { 0, 250, 500, 750, 1000 });
    CHECK (m.add (100));
    CHECK_FALSE (m.add (100));
    CHECK_FALSE (m.add (0));
    CHECK_FALSE (m.add (1000));
    CHECK (m.numSlices() == 5);
    m.move (0, 600); // clamped below the next marker (250)
    CHECK (m.getMarkers()[0] == 249);
    m.remove (0);
    CHECK (m.numSlices() == 4);
    CHECK (m.findMarker (505, 10) == 1);
    CHECK (m.findMarker (600, 10) == -1);
    m.setEqual (200);
    CHECK ((int) m.getMarkers().size() == SliceModel::kMaxMarkers);
}

TEST_CASE ("Slice envelopes follow their starts through marker edits", "[slices][adsr]")
{
    SliceModel model;
    model.setLength (1000);
    model.setMarkers ({ 200, 600 });
    const HitEnvelope first { 1.0f, 2.0f, 0.3f, 4.0f };
    const HitEnvelope second { 5.0f, 6.0f, 0.7f, 8.0f };
    model.setEnvelope (0, first);
    model.setEnvelope (1, second);

    model.move (0, 250);
    REQUIRE (model.add (125));
    CHECK_FALSE (model.getEnvelopes()[1].has_value());
    CHECK (model.getEnvelopes()[2] == second);
    model.remove (0);
    CHECK (model.getEnvelopes()[1] == second);
    model.setMarkers ({ 100, 250, 400 });
    CHECK (model.getEnvelopes()[2] == second);
    model.remove (1);
    CHECK_FALSE (model.getEnvelopes()[2].has_value());
    model.clear();
    CHECK (model.getEnvelopes()[0] == first);
}

TEST_CASE ("T12 onset markers follow click attacks at different source rates", "[slices][onsets]")
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        CAPTURE (rate);
        std::vector<float> mono ((size_t) rate, 0.0f);
        const std::array<int64_t, 4> attacks { 127, (int64_t) (rate * 0.161),
                                              (int64_t) (rate * 0.435), (int64_t) rate - 13 };
        for (size_t i = 0; i < attacks.size(); ++i)
            mono[(size_t) attacks[i]] = (i % 2 == 0 ? -0.25f : 0.8f);

        const auto markers = detectOnsets (mono, rate, 0.5);
        REQUIRE (markers.size() == attacks.size());
        for (size_t i = 0; i < markers.size(); ++i)
            CHECK (std::abs (markers[i] - attacks[i]) <= rate * 0.005);
    }
}

TEST_CASE ("Transient markers respect the implicit boundary, spacing and slice limit", "[slices][onsets]")
{
    constexpr double rate = 48000.0;
    std::vector<float> mono (200 * 2048, 0.0f);
    mono.front() = 1.0f;
    CHECK (detectOnsets (mono, rate, 1.0).empty());

    for (size_t frame = 2048; frame < mono.size(); frame += 2048) mono[frame] = 1.0f;
    mono[2048 + 600] = 0.25f; // Quieter attack within 30 ms of the preceding click.
    const auto markers = detectOnsets (mono, rate, 1.0);
    REQUIRE (markers.size() == SliceModel::kMaxMarkers);
    CHECK (markers.front() == 2048);
    for (size_t i = 1; i < markers.size(); ++i)
        CHECK (markers[i] - markers[i - 1] >= 1440);
}

TEST_CASE ("Transient sensitivity exposes smaller rises and silence stays empty", "[slices][onsets]")
{
    constexpr double rate = 48000.0;
    std::vector<float> mono (8192, 0.0f);
    CHECK (detectOnsets (mono, rate, 1.0).empty());
    std::fill (mono.begin(), mono.end(), 0.2f);
    CHECK (detectOnsets (mono, rate, 1.0).empty());
    std::fill (mono.begin() + 4096, mono.end(), 0.25f);
    CHECK (detectOnsets (mono, rate, 0.0).empty());
    CHECK (detectOnsets (mono, rate, 1.0) == std::vector<int64_t> { 4096 });

    const std::array<float, 4> shortSample { 0.0f, 0.0f, 0.5f, 0.0f };
    CHECK (detectOnsets (shortSample, rate, 0.5) == std::vector<int64_t> { 2 });
}
