#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace xt;
using namespace xt::test;
using Catch::Approx;

TEST_CASE ("T11 ramp reaches its endpoint and runs in all repeat modes", "[T11]")
{
    for (int repeat = 0; repeat < 3; ++repeat)
    {
        Engine engine;
        engine.setEngineRate (48000.0);
        auto p = cleanParams();
        p.rateSrc = 1;
        p.hz = 100.0;
        p.rampOn = true;
        p.rampTimeMs = 10.0;
        p.rampLoop = repeat;
        engine.setParams (p);
        engine.reset();

        render (engine, 1, 1, { noteOn (0, 60) });
        CHECK (engine.currentRateHz() == Approx (25.0));
        render (engine, 240, 7, {});
        CHECK (engine.currentRateHz() == Approx (50.0));
        render (engine, 240, 64, {});
        CHECK (engine.currentRateHz() == Approx (repeat == 1 ? 25.0 : 100.0));
        render (engine, 240, 7, {});
        CHECK (engine.currentRateHz() == Approx (repeat == 0 ? 100.0 : 50.0));
    }
}

TEST_CASE ("T11 LFO shapes are bipolar and a rate-ratio pattern stays locked under jitter", "[T11]")
{
    for (int shape = 0; shape < 7; ++shape)
    {
        Lfo oscillator;
        oscillator.reset (0.13);
        for (int sample = 0; sample < 1024; ++sample)
        {
            const auto value = oscillator.value (shape);
            REQUIRE (value >= -1.0);
            REQUIRE (value <= 1.0);
            oscillator.advance (0.0091);
        }
    }

    Engine engine;
    engine.setEngineRate (48000.0);
    std::vector<float> data (2000);
    for (size_t i = 0; i < data.size(); ++i) data[i] = (float) i / 2000.0f;
    OwnedSource source (std::move (data), 48000.0);
    engine.setSource (source.view());
    auto p = cleanParams();
    p.rateSrc = 1;
    p.hz = 270.0;
    p.jitter = 0.8;
    p.length = 0.2;
    p.scan = 0.5;
    p.lfo[0].shape = 3; // falling saw, starts at +1 following each lock
    p.lfo[0].sync = 2;
    p.lfo[0].ratio = 2; // one cycle every three ticks
    p.lfo[0].toScan = 0.5;
    p.lfo[1].hz = 8.0;
    p.lfo[1].toRate = 10.0;
    engine.setDcBlockerEnabled (false);
    engine.setParams (p);
    engine.reset();

    const auto audio = render (engine, 10000, 7, { noteOn (0, 60) });
    int hits = 1;
    CHECK (audio.l.front() == Approx (0.8).margin (0.001));
    for (size_t i = 1; i < audio.l.size(); ++i)
        if (audio.l[i - 1] - audio.l[i] > 0.1f || audio.l[i] - audio.l[i - 1] > 0.1f)
        {
            CHECK (audio.l[i] == Approx (0.8 * (1.0 - (hits % 3) / 3.0)).margin (0.002));
            ++hits;
        }
    CHECK (hits > 30);
}

TEST_CASE ("T8 sequencer advances through notes, ties and rests and wraps", "[T8]")
{
    Engine engine;
    engine.setEngineRate (48000.0);
    OwnedSource source (std::vector<float> (24000, 0.8f), 48000.0, { 12000 });
    engine.setSource (source.view());
    auto p = cleanParams();
    p.rateSrc = 1;
    p.hz = 100.0;
    p.seqOn = true;
    p.seqLen = 3;
    p.abMode = 1;
    p.abGainDb = -24.0;
    p.steps[0] = { true, false, 1, 12, 0.5f };
    p.steps[1].tie = true;
    p.steps[2].on = false;
    engine.setDcBlockerEnabled (false);
    engine.setParams (p);
    engine.reset();

    const auto play = [&] (int frames, std::vector<MidiEvent> events = {})
    {
        return render (engine, frames, 7, std::move (events)).l.back();
    };
    CHECK (play (1, { noteOn (0, 60) }) == Approx (0.4));
    CHECK (engine.telemetry.step.load() == 0);
    CHECK (engine.telemetry.slice.load() == 1);
    REQUIRE (engine.lane (0).newestVoice() != nullptr);
    CHECK (engine.lane (0).newestVoice()->getPosition() == Approx (12002.0));
    CHECK (play (481) == Approx (0.4));
    CHECK (engine.telemetry.step.load() == 1);
    CHECK (play (480) == 0.0f);
    CHECK (engine.telemetry.step.load() == 2);
    CHECK (play (480) == Approx (0.4));
    CHECK (engine.telemetry.step.load() == 0);

    p.seqReset = false;
    engine.setParams (p);
    CHECK (play (481, { noteOn (0, 62) }) == Approx (0.4));
    CHECK (engine.telemetry.step.load() == 1);
    p.seqReset = true;
    engine.setParams (p);
    CHECK (play (540, { noteOn (0, 60) }) == Approx (0.4));
    CHECK (engine.telemetry.step.load() == 0);
}

TEST_CASE ("T11 host LFO modulates gate at each hit and sample pitch continuously", "[T11]")
{
    Engine engine;
    engine.setEngineRate (48000.0);
    OwnedSource source (std::vector<float> (24000, 0.8f), 48000.0);
    engine.setSource (source.view());
    auto p = cleanParams();
    p.rateSrc = 1;
    p.hz = 100.0;
    p.gate = 0.5;
    p.lfo[0].shape = 4; // square holds a constant depth over the first hit
    p.lfo[0].sync = 1;
    p.lfo[0].toGate = -0.25;
    p.lfo[1].shape = 4;
    p.lfo[1].phaseDeg = 180.0;
    p.lfo[1].toPitch = 12.0;
    engine.setParams (p);
    engine.setHostInfo ({ true, 180.0, 0.0, true });
    engine.setDcBlockerEnabled (false);
    engine.reset();

    render (engine, 40, 7, { noteOn (0, 60) });
    REQUIRE (engine.lane (0).newestVoice() != nullptr);
    CHECK (engine.lane (0).newestVoice()->getPosition() == Approx (20.0));

    const auto audio = render (engine, 8500, 64, {});
    CHECK (audio.l[119 - 40] == Approx (0.8));
    CHECK (audio.l[121 - 40] == 0.0f);
    // At 180 BPM the synced quarter-note LFO changes sign after 8,000 samples.
    // The next hit at 8,160 lasts 75% of its period.
    CHECK (audio.l[8460 - 40] == Approx (0.8));
}

TEST_CASE ("T10 random unison phase keeps the initial hits at their slice starts", "[T10]")
{
    Engine engine;
    engine.setEngineRate (48000.0);
    OwnedSource source (std::vector<float> (16, 0.75f), 48000.0);
    engine.setSource (source.view());
    auto p = cleanParams();
    p.uniVoices = 4;
    p.rateSrc = 1;
    p.hz = 10.0;
    engine.setParams (p);
    engine.setDcBlockerEnabled (false);
    engine.reset();

    render (engine, 1, 1, { noteOn (0, 60) });
    REQUIRE (engine.activeVoiceCount() == 4);
    for (int lane = 0; lane < 4; ++lane)
        CHECK (engine.lane (lane).newestVoice()->getPosition() == Approx (1.0));
    CHECK (engine.lane (0).clock.getPhase() != engine.lane (1).clock.getPhase());
}
