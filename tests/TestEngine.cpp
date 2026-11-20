#include "DefaultKick.h"
#include "Engine.h"
#include "RateMath.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cmath>

using namespace xt;
using namespace xt::test;

TEST_CASE ("Hit envelopes restart and release from the gate level", "[engine][adsr]")
{
    OwnedSource source (std::vector<float> (64, 1.0f), 48000.0);
    HitStart hit;
    hit.winEnd = 64.0;
    hit.interp = Interp::Raw;
    hit.subOffset = 0.5;
    hit.attackSamples = 4.0;
    hit.decaySamples = 4.0;
    hit.sustain = 0.25f;
    hit.releaseSamples = 4.0;
    hit.gateSamples = 6.0;
    HitVoice voice;
    voice.start (hit, 1);

    std::array<float, 12> out {};
    for (auto& sample : out)
    {
        float right = 0.0f;
        voice.render (source.view().sample, 1.0, sample, right);
    }
    CHECK (out[0] == 0.125f);
    CHECK (out[3] == 0.875f);
    CHECK (out[5] == 0.71875f);
    CHECK (out[6] == 0.625f);
    CHECK (out[8] == 0.3125f);
    CHECK (out[10] == 0.0f);
    CHECK_FALSE (voice.isActive());

    hit.subOffset = 0.0;
    hit.gateSamples = -1.0;
    hit.winEnd = 16.0;
    voice.start (hit, 2);
    for (int t = 0; t < 16; ++t)
    {
        float level = 0.0f, right = 0.0f;
        voice.render (source.view().sample, 1.0, level, right);
        if (t == 0) CHECK (level == 0.0f);
        if (t == 8) CHECK (level == 0.25f);
        if (t == 14) CHECK (level == 0.125f);
    }
}

TEST_CASE ("The triggered slice chooses its envelope across playback modes", "[engine][adsr]")
{
    constexpr double rate = 48000.0;
    OwnedSource sample (std::vector<float> (2000, 1.0f), rate, { 1000 });
    const std::array<std::optional<HitEnvelope>, 2> envelopes { std::nullopt, HitEnvelope { 0.0f, 0.0f, 0.25f, 0.0f } };
    auto source = sample.view();
    source.envelopes = envelopes.data();

    Engine engine;
    engine.setEngineRate (rate);
    engine.setSource (source);
    engine.setDcBlockerEnabled (false);
    auto p = cleanParams();
    p.hitEnvelope.sustain = 0.75f;
    p.rateSrc = 1;
    p.hz = 100.0;
    p.interp = (int) Interp::Raw;
    p.abInterp = (int) Interp::Raw;
    int note = 60;

    SECTION ("Selected and unconfigured slices")
    {
        engine.setParams (p);
        engine.reset();
        CHECK (render (engine, 64, 64, { noteOn (0, note) }).l[32] == 0.75f);
        p.slice = 1;
    }
    SECTION ("MIDI selection")
    {
        p.midiMode = 1;
        note = 61;
    }
    SECTION ("Sequencer") { p.seqOn = true; p.steps[0].slice = 1; }
    SECTION ("Cycling")
    {
        p.abMode = 1;
        engine.setParams (p);
        engine.reset();
        const auto out = render (engine, 550, 64, { noteOn (0, note) });
        CHECK (out.l[32] == 0.75f);
        CHECK (out.l[512] == 0.25f);
        return;
    }
    SECTION ("Audition")
    {
        engine.setParams (p);
        engine.reset();
        engine.auditionSlice (1);
        CHECK (render (engine, 64, 64, {}).l[32] == 0.25f * 0.7f);
        return;
    }

    engine.setParams (p);
    engine.reset();
    CHECK (render (engine, 64, 64, { noteOn (0, note) }).l[32] == 0.25f);
}

TEST_CASE ("A sequencer rest uses each hit's release", "[engine][adsr]")
{
    constexpr double rate = 48000.0;
    OwnedSource sample (std::vector<float> (2048, 1.0f), rate);
    Engine engine;
    engine.setEngineRate (rate);
    engine.setSource (sample.view());
    engine.setDcBlockerEnabled (false);
    auto p = cleanParams();
    p.rateSrc = 1;
    p.hz = 100.0;
    p.hitEnvelope.releaseMs = 2.0f;
    p.seqOn = true;
    p.seqLen = 2;
    p.steps[1].on = false;
    engine.setParams (p);
    engine.reset();

    const auto out = render (engine, 640, 64, { noteOn (0, 60) });
    CHECK (out.l[470] == 1.0f);
    CHECK (out.l[510] > 0.0f);
    CHECK (out.l[510] < 1.0f);
    CHECK (out.l[600] == 0.0f);
}

TEST_CASE ("Cut replaces gated tails while Overlap lets them ring", "[engine][adsr]")
{
    constexpr double rate = 48000.0;
    OwnedSource sample (std::vector<float> (4096, 1.0f), rate);
    Engine engine;
    engine.setEngineRate (rate);
    engine.setSource (sample.view());
    engine.setDcBlockerEnabled (false);
    auto p = cleanParams();
    p.rateSrc = 1;
    p.hz = 100.0;
    p.gate = 0.5;
    p.hitEnvelope.releaseMs = 25.0f;

    engine.setParams (p);
    engine.reset();
    CHECK (render (engine, 550, 64, { noteOn (0, 60) }).l[512] == 1.0f);
    p.trigMode = 1;
    engine.setParams (p);
    engine.reset();
    CHECK (render (engine, 550, 64, { noteOn (0, 60) }).l[512] > 1.0f);
}

namespace
{
constexpr double kPi = 3.14159265358979323846;

/// THD+N in dB: residual after least-squares fit of a sinusoid (+ DC) at f.
double thdnDb (const std::vector<float>& x, size_t from, size_t to, double f, double fs)
{
    // normal equations for [sin, cos, 1]
    double A[3][3] {}, b[3] {};
    for (size_t i = from; i < to; ++i)
    {
        const double w = 2.0 * kPi * f * (double) i / fs;
        const double v[3] = { std::sin (w), std::cos (w), 1.0 };
        for (int r = 0; r < 3; ++r)
        {
            b[r] += v[r] * x[i];
            for (int c = 0; c < 3; ++c) A[r][c] += v[r] * v[c];
        }
    }
    // solve 3x3 (Gauss)
    for (int k = 0; k < 3; ++k)
        for (int r = k + 1; r < 3; ++r)
        {
            const double m = A[r][k] / A[k][k];
            for (int c = k; c < 3; ++c) A[r][c] -= m * A[k][c];
            b[r] -= m * b[k];
        }
    double s[3];
    for (int k = 2; k >= 0; --k)
    {
        double acc = b[k];
        for (int c = k + 1; c < 3; ++c) acc -= A[k][c] * s[c];
        s[k] = acc / A[k][k];
    }
    double sig = 0.0, res = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        const double w = 2.0 * kPi * f * (double) i / fs;
        const double fit = s[0] * std::sin (w) + s[1] * std::cos (w);
        sig += fit * fit;
        const double e = x[i] - fit - s[2];
        res += e * e;
    }
    return 10.0 * std::log10 (res / sig);
}

double peakAbs (const std::vector<float>& v, size_t from = 0, size_t to = (size_t) -1)
{
    double p = 0.0;
    for (size_t i = from; i < std::min (to, v.size()); ++i) p = std::max (p, (double) std::abs (v[i]));
    return p;
}

bool allFinite (const Stereo& s)
{
    for (size_t i = 0; i < s.l.size(); ++i)
        if (! std::isfinite (s.l[i]) || ! std::isfinite (s.r[i])) return false;
    return true;
}
} // namespace

TEST_CASE ("T4 phase continuity", "[T4]")
{
    const double fs = 48000.0, f0 = 123.4;
    OwnedSource src (makeSine (f0, fs, 2.0), fs);

    auto measure = [&]
    {
        Engine e;
        e.setEngineRate (fs);
        e.setSource (src.view());
        auto p = cleanParams();
        p.rateSrc = 1;
        p.hz = f0;
        p.trigMode = 0;
        p.declickMs = 0.0;
        p.interp = 0;
        e.setParams (p);
        e.setDcBlockerEnabled (false);
        e.reset();
        auto out = render (e, (int) fs, 256, { noteOn (0, 60) });
        return thdnDb (out.l, 4800, out.l.size(), f0, fs);
    };
    const double thdn = measure();
    INFO ("THD+N = " << thdn << " dB");
    CHECK (thdn < -60.0);
}

TEST_CASE ("T5 gate", "[T5]")
{
    const double fs = 48000.0;
    // DC sample makes "silence after the gate" easy to verify
    OwnedSource src (std::vector<float> ((size_t) fs, 0.5f), fs);
    Engine e;
    e.setEngineRate (fs);
    e.setSource (src.view());
    auto p = cleanParams();
    p.rateSrc = 1;
    p.hz = 100.0; // period 480 samples
    p.gate = 0.5;
    p.declickMs = 0.5; // 24 samples fade
    e.setParams (p);
    e.setDcBlockerEnabled (false);
    e.reset();
    auto out = render (e, 4800, 64, { noteOn (0, 60) });
    const double silence = std::pow (10.0, -90.0 / 20.0);
    for (int period = 1; period < 9; ++period)
    {
        const size_t p0 = (size_t) period * 480;
        CHECK (peakAbs (out.l, p0 + 100, p0 + 200) > 0.4);              // first half sounds
        CHECK (peakAbs (out.l, p0 + 240 + 24 + 1, p0 + 480) < silence); // second half after fade is silent
    }
}

TEST_CASE ("T6 voices", "[T6]")
{
    const double fs = 48000.0;
    const auto kick = makeDefaultKick (fs);
    OwnedSource src (kick, fs);
    for (int uni : { 1, 3, 8 })
    {
        for (int voices : { 1, 4, 8, 32 })
        {
            Engine e;
            e.setEngineRate (fs);
            e.setSource (src.view());
            auto p = cleanParams();
            p.rateSrc = 1;
            p.hz = 200.0; // 450 ms sample -> up to 90 overlapping hits
            p.trigMode = 1;
            p.voices = voices;
            p.uniVoices = uni;
            e.setParams (p);
            e.reset();
            int maxTotal = 0;
            bool laneOk = true;
            std::vector<MidiEvent> ev { noteOn (0, 60) };
            float l[64], r[64];
            for (int b = 0; b < 600; ++b)
            {
                e.process (l, r, 64, ev.data(), b == 0 ? 1 : 0);
                maxTotal = std::max (maxTotal, e.activeVoiceCount());
                for (int k = 0; k < uni; ++k)
                    laneOk &= e.lane (k).nonReleasingCount() <= voices && e.lane (k).activeCount() <= voices + 1;
            }
            INFO ("uni " << uni << " voices " << voices << " maxTotal " << maxTotal);
            CHECK (laneOk);
            CHECK (maxTotal <= Engine::kMaxTotalVoices);
            if (uni == 1) CHECK (maxTotal >= voices); // pool is really used
        }
    }
    // Cut: at most 2 voices per lane (with a declick crossfade)
    Engine e;
    e.setEngineRate (fs);
    e.setSource (src.view());
    auto p = cleanParams();
    p.rateSrc = 1;
    p.hz = 800.0;
    p.declickMs = 5.0;
    e.setParams (p);
    e.reset();
    std::vector<MidiEvent> ev { noteOn (0, 60) };
    float l[64], r[64];
    int maxLane = 0;
    for (int b = 0; b < 300; ++b)
    {
        e.process (l, r, 64, ev.data(), b == 0 ? 1 : 0);
        maxLane = std::max (maxLane, e.lane (0).activeCount());
    }
    CHECK (maxLane <= 2);
}

TEST_CASE ("T10 unison", "[T10]")
{
    for (int n = 1; n <= 8; ++n)
    {
        Engine e;
        auto p = cleanParams();
        p.uniVoices = n;
        p.uniDetune = 15.0;
        p.uniSpread = 0.8;
        e.setParams (p);
        e.reset();
        CHECK (e.activeLaneCount() == n);
        CHECK (std::abs (e.getLaneGain() - 1.0 / std::sqrt ((double) n)) < 1e-12);
        double sum = 0.0, maxAbs = 0.0;
        for (int k = 0; k < n; ++k)
        {
            const double d = e.laneDetuneCents (k);
            const double mirrored = e.laneDetuneCents (n - 1 - k);
            CHECK (std::abs (d + mirrored) < 1e-9);
            CHECK (std::abs (e.lanePan (k) + e.lanePan (n - 1 - k)) < 1e-6);
            sum += d;
            maxAbs = std::max (maxAbs, std::abs (d));
        }
        CHECK (std::abs (sum) < 1e-9);
        CHECK (std::abs (maxAbs - (n == 1 ? 0.0 : 15.0)) < 1e-9);
        if (n == 1) CHECK (e.lanePan (0) == 0.0f);
    }
}

TEST_CASE ("T13 block size invariance", "[T13]")
{
    const double fs = 96000.0;
    const auto kick = makeDefaultKick (48000.0);
    OwnedSource src (kick, 48000.0, { 5000, 9000 });

    auto p = cleanParams();
    p.driveDb = 12.0;
    p.clip = 2;
    p.outGainDb = -3.0;
    p.glideMs = 20.0;
    p.glideLegato = true;
    p.declickMs = 0.7;
    p.gate = 0.7;
    p.jitter = 0.3;
    p.sPitch = 3.0;
    p.penvAmt = 5.0;
    p.envR = 50.0;
    p.uniVoices = 3;
    p.trigMode = 1;
    p.voices = 6;
    p.slice = 1;

    p.rampOn = true;
    p.rampLoop = 2;
    p.rampTimeMs = 250.0;
    p.lfo[0].sync = 2;
    p.lfo[0].toRate = 7.0;
    p.lfo[0].toPitch = -2.0;
    p.lfo[1].shape = 6;
    p.lfo[1].toGate = 0.3;
    p.seqOn = true;
    p.seqLen = 4;
    p.steps[1].tie = true;
    p.steps[2].on = false;
    p.steps[3] = { true, false, 2, -7, 0.8f };

    const std::vector<MidiEvent> events {
        noteOn (100, 60), noteOn (30011, 72), bend (40000, 0.4f), noteOff (50000, 72),
        noteOff (70001, 60), noteOn (90000, 48, 0.5f), bend (100003, -1.0f), noteOff (150000, 48)
    };
    const int total = 200000;

    auto run = [&] (int block)
    {
        Engine e;
        e.setEngineRate (fs);
        e.setSource (src.view());
        e.setParams (p);
        e.reset();
        return render (e, total, block, events);
    };
    const auto ref = run (total); // one giant block (larger than any announced size)
    REQUIRE (peakAbs (ref.l) > 0.1);
    for (int block : { 1, 7, 64, 256, 4096 })
    {
        const auto o = run (block);
        double maxDiff = 0.0;
        for (size_t i = 0; i < ref.l.size(); ++i)
            maxDiff = std::max ({ maxDiff, (double) std::abs (o.l[i] - ref.l[i]), (double) std::abs (o.r[i] - ref.r[i]) });
        INFO ("block " << block);
        CHECK (maxDiff < 1e-6);
    }
}

TEST_CASE ("T14 robustness", "[T14]")
{
    const double fs = 96000.0;
    const auto kick = makeDefaultKick (48000.0);
    OwnedSource src (kick, 48000.0);

    auto p = cleanParams();
    p.bpm = 60000.0;
    p.division = Div1_64;
    p.uniVoices = 8;
    p.trigMode = 1;
    p.voices = 32;
    p.driveDb = 48.0;
    p.clip = 2;
    p.outGainDb = 0.0;
    p.transpose = 48.0;
    p.sPitch = 48.0;
    p.penvAmt = 48.0;
    p.jitter = 1.0;

    p.rampOn = true;
    p.rampStart = 8.0;
    p.rampEnd = -8.0;
    p.rampTimeMs = 1.0;
    p.rampLoop = 2;
    p.lfo[0].sync = 2;
    p.lfo[0].ratio = 0;
    p.lfo[0].toRate = 24.0;
    p.lfo[0].toPitch = 24.0;

    for (int variant = 0; variant < 4; ++variant)
    {
        auto q = p;
        if (variant == 1) { q.sPitch = -48.0; q.transpose = -48.0; q.bpm = 1.0; q.division = Div1_1; }
        if (variant == 2) { q.rateSrc = 1; q.hz = 4000.0; q.reverse = true; q.length = 0.01; }
        if (variant == 3) { q.rateSrc = 1; q.hz = 0.1; q.declickMs = 5.0; q.gate = 0.01; q.trigMode = 0; }
        Engine e;
        e.setEngineRate (fs);
        e.setSource (src.view());
        e.setParams (q);
        e.reset();
        const auto out = render (e, (int) fs, 512,
                                 { noteOn (0, 127), bend (20000, 1.0f), noteOn (40000, 0), bend (60000, -1.0f) });
        INFO ("variant " << variant);
        CHECK (allFinite (out));
        CHECK (peakAbs (out.l) <= 1.5);
        CHECK (peakAbs (out.r) <= 1.5);
    }
}

TEST_CASE ("Engine: pitch mode relative doubles rate per octave", "[engine]")
{
    Engine e;
    e.setEngineRate (48000.0);
    OwnedSource src (makeDefaultKick(), 48000.0);
    e.setSource (src.view());
    auto p = cleanParams();
    p.rateSmoothMs = 0.0;
    e.setParams (p);
    e.reset();
    float l[32], r[32];
    MidiEvent a = noteOn (0, 60);
    e.process (l, r, 32, &a, 1);
    CHECK (std::abs (e.currentRateHz() - 500.0 / 60.0 / 0.25) < 1e-9);
    MidiEvent b = noteOn (0, 72);
    e.process (l, r, 32, &b, 1);
    CHECK (std::abs (e.currentRateHz() - 2.0 * 500.0 / 60.0 / 0.25) < 1e-9);

    // absolute mode: note 69 = a4
    p.keyMode = 1;
    e.setParams (p);
    MidiEvent c = noteOn (0, 69);
    e.process (l, r, 32, &c, 1);
    CHECK (std::abs (e.currentRateHz() - 440.0) < 1e-9);
}

TEST_CASE ("Engine: release makes the engine idle", "[engine]")
{
    Engine e;
    e.setEngineRate (48000.0);
    OwnedSource src (makeDefaultKick(), 48000.0);
    e.setSource (src.view());
    auto p = cleanParams();
    p.envR = 10.0;
    e.setParams (p);
    e.reset();
    auto out = render (e, 48000, 128, { noteOn (0, 60), noteOff (10000, 60) });
    CHECK (peakAbs (out.l, 0, 10000) > 0.1);
    CHECK (peakAbs (out.l, 20000) < 1e-3);
    CHECK (e.activeVoiceCount() == 0);
}

TEST_CASE ("T9 Cycling uses the selected pattern and B pitch and gain", "[T9]")
{
    constexpr double fs = 48000.0;
    constexpr int period = 128;
    std::vector<float> sample (2000, -0.25f);
    for (int i = 1000; i < 2000; ++i) sample[(size_t) i] = 0.5f + (float) (i - 1000) * 0.0005f;
    OwnedSource src (std::move (sample), fs, { 1000 });
    constexpr std::array<std::array<bool, 6>, 4> patterns {{
        { false, false, false, false, false, false }, { false, true, false, true, false, true },
        { false, false, true, false, false, true }, { false, true, true, false, true, true }
    }};
    Engine e;
    e.setEngineRate (fs);
    e.setSource (src.view());
    e.setDcBlockerEnabled (false);
    auto p = cleanParams();
    p.rateSrc = 1;
    p.hz = fs / period;
    p.envR = 0.0;
    p.abPitch = 12.0;
    p.abGainDb = 20.0 * std::log10 (0.5);
    for (size_t mode = 0; mode < patterns.size(); ++mode)
    {
        p.abMode = (int) mode;
        e.setParams (p);
        e.reset();
        const auto out = render (e, period * 8, 79, { noteOn (0, 60), noteOff (period * 6, 60), noteOn (period * 7, 60) });
        for (size_t hit = 0; hit < patterns[mode].size(); ++hit)
        {
            INFO ("Mode " << mode << " hit " << hit);
            const auto at = hit * period + 12;
            CHECK (std::abs (out.l[at] - (patterns[mode][hit] ? 0.256f : -0.25f)) < 0.001f);
        }
        CHECK (out.l[period * 7 + 12] < 0.0f);
    }
    p.abMode = 4;
    e.setParams (p);
    e.reset();
    const auto random = render (e, period * 32, 79, { noteOn (0, 60) });
    CHECK (std::ranges::min (random.l) < -0.1f);
    CHECK (std::ranges::max (random.l) > 0.1f);
}

TEST_CASE ("Slice audition is a one-shot in the selected playback window", "[engine]")
{
    constexpr double fs = 48000.0;
    std::vector<float> sample (2000, -0.25f);
    std::fill (sample.begin() + 1000, sample.end(), 0.5f);
    OwnedSource src (std::move (sample), fs, { 1000 });
    Engine e;
    e.setEngineRate (fs);
    e.setSource (src.view());
    e.setDcBlockerEnabled (false);
    auto p = cleanParams();
    p.scan = 0.25;
    p.length = 0.5;
    p.reverse = true;
    e.setParams (p);
    e.reset();

    e.auditionSlice (1);
    const auto beginning = render (e, 64, 64, {});
    CHECK (beginning.l.back() > 0.3f);
    CHECK (e.telemetry.auditionSlice.load() == 1);
    CHECK (std::abs (e.telemetry.auditionPos.load() - (1625.0 - 64.0) / 2000.0) < 1.0e-4);
    CHECK_FALSE (e.telemetry.active.load());
    const auto ending = render (e, 520, 67, {});
    CHECK (peakAbs (ending.l, 436) == 0.0);
    CHECK (e.telemetry.auditionSlice.load() == -1);
    CHECK (e.getParams().slice == 0);

    e.auditionSlice (1);
    e.setSource (src.view());
    CHECK (peakAbs (render (e, 64, 64, {}).l) == 0.0);
}

TEST_CASE ("Cycling and audition use the independent B playback window", "[engine]")
{
    constexpr double fs = 48000.0;
    constexpr int period = 128, frames = 4096;
    OwnedSource src (std::vector<float> (frames, -0.25f), fs, { frames / 2 });
    std::vector<float> left (frames, 0.6f), right (frames, -0.4f);
    auto source = src.view();
    source.sample.ch[0] = left.data();
    source.sample.ch[1] = right.data();
    source.sample.numCh = 2;
    Engine e;
    e.setEngineRate (fs);
    e.setSource (source);
    e.setDcBlockerEnabled (false);

    auto p = cleanParams();
    p.rateSrc = 1;
    p.hz = fs / period;
    p.sPitch = -12.0;
    p.abMode = 1;
    p.abScan = 0.25;
    p.abLength = 0.5;
    p.abReverse = true;
    p.abMono = false;
    p.abSamplePitch = 11.5;
    p.abFine = 50.0;
    p.abPenvAmt = 12.0;
    p.abPenvDecayMs = 1000.0;
    e.setParams (p);
    e.reset();

    constexpr double bEnd = 3328.0;
    const auto cycling = render (e, period + 16, 37, { noteOn (0, 60) });
    CHECK (cycling.l[32] == cycling.r[32]);
    CHECK (cycling.l[32] < 0.0f);
    CHECK (cycling.l.back() > 0.5f);
    CHECK (cycling.r.back() < -0.3f);
    CHECK (e.telemetry.slice.load() == 1);
    CHECK (std::abs (e.telemetry.playPos.load() * frames - (bEnd - 16.0 * 4.0)) < 3.0);

    e.reset();
    e.auditionSlice (1, true);
    const auto preview = render (e, 64, 64, {});
    CHECK (preview.l.back() > 0.3f);
    CHECK (preview.r.back() < -0.2f);
    CHECK (std::abs (e.telemetry.auditionPos.load() * frames - (bEnd - 64.0)) < 0.1);
}
