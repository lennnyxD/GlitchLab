// Standalone tests for the glitch engine (no JUCE needed).
#include "../Source/GlitchEngine.h"
#include "../tools/TestSignals.h"
#include <chrono>
#include <cstdio>

using namespace glitch;

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static const double SR = 48000.0;

struct EventRec { int64_t start; int type, len, sliceLen; double semi0; };

struct Run
{
    std::vector<float> outL, outR;
    std::vector<EventRec> events;
};

static Run render (GlitchEngine& e, const std::vector<float>& in, int block = 64)
{
    Run r;
    r.outL = in; r.outR = in;
    int64_t seen = e.eventCount();
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, in.size() - pos);
        e.process (&r.outL[pos], &r.outR[pos], n);
        if (e.eventCount() != seen)
        {
            seen = e.eventCount();
            const auto& li = e.lastEvent();
            r.events.push_back ({ li.startSample - 1, li.type, li.lenSamples, li.sliceLen, li.semi0 });
        }
    }
    return r;
}

static ParamValues onlyEvent (int type)
{
    ParamValues v = defaultValues();
    for (int i = 0; i < EV_COUNT; ++i) v[(size_t) (P_W_STUTTER + i)] = (i == type) ? 1.f : 0.f;
    v[P_TRIGMODE] = 0; v[P_DIV] = 0; v[P_PROB] = 1.f; v[P_CHAOS] = 0.f;
    v[P_EVMIN] = v[P_EVMAX] = 400.f;
    v[P_CRUSH] = v[P_DOWNSAMPLE] = v[P_FOLD] = v[P_RING] = v[P_FILTER] = v[P_FLIP] = 0.f;
    return v;
}

static std::vector<float> noise (size_t n, uint32_t seed, float amp = 0.5f)
{
    Rng r; r.seed (seed);
    std::vector<float> x (n);
    for (auto& s : x) s = r.range (-amp, amp);
    return x;
}

static double zcFreq (const std::vector<float>& x, size_t a, size_t b)
{
    int c = 0;
    for (size_t i = a + 1; i < b; ++i) if (x[i - 1] < 0.f && x[i] >= 0.f) ++c;
    return (double) c / ((double) (b - a) / SR);
}

static GlitchEngine make (const ParamValues& v, uint32_t seed = 1)
{
    GlitchEngine e; e.prepare (SR); e.seed (seed); e.setParams (v); e.setTransport (120.0, 0.0, true); return e;
}

int main()
{
    std::printf ("GlitchLab engine tests\n");

    // 1. all presets: finite, bounded, and something actually happens
    {
        std::printf ("[1] presets are stable and audible\n");
        const auto speech = sig::speechLike (SR, 12.0, 7);
        for (int pi = 0; pi < (int) presets().size(); ++pi)
            for (uint32_t seed = 1; seed <= 3; ++seed)
            {
                auto v = presetValues (pi);
                auto e = make (v, seed * 101);
                auto run = render (e, speech);
                float peak = 0.f; bool finite = true; double diff = 0.0;
                for (size_t i = 0; i < speech.size(); ++i)
                {
                    peak = std::max ({ peak, std::fabs (run.outL[i]), std::fabs (run.outR[i]) });
                    finite = finite && std::isfinite (run.outL[i]) && std::isfinite (run.outR[i]);
                    diff += std::fabs (run.outL[i] - speech[i]);
                }
                CHECK (finite, "preset %d seed %u: NaN/inf", pi, seed);
                CHECK (peak <= 1.0f, "preset %d seed %u: peak %.3f", pi, seed, peak);
                if (pi > 0 || seed == 1)
                    CHECK (run.events.size() > 0 && diff > 1.0, "preset %d '%s' seed %u: no glitches happened (%zu events)", pi, presets()[(size_t) pi].name, seed, run.events.size());
            }
    }

    // 2. Mix = 0 is a perfect bypass
    {
        std::printf ("[2] mix 0 = bypass\n");
        auto v = presetValues (3); v[P_MIX] = 0.f;
        auto e = make (v);
        const auto in = sig::speechLike (SR, 4.0, 3);
        auto run = render (e, in);
        double maxd = 0.0;
        for (size_t i = 0; i < in.size(); ++i) maxd = std::max (maxd, (double) std::fabs (run.outL[i] - in[i]));
        CHECK (maxd < 1e-6, "bypass differs by %g", maxd);
    }

    // 3. stutter repeats the slice exactly
    {
        std::printf ("[3] stutter is periodic with the slice length\n");
        auto v = onlyEvent (EV_STUTTER);
        v[P_SLMIN] = v[P_SLMAX] = 50.f; v[P_RAMP] = 0.f; v[P_PITCH] = 0.f;
        auto e = make (v);
        auto run = render (e, noise ((size_t) (SR * 3.0), 5));
        CHECK (run.events.size() >= 3, "expected several events, got %zu", run.events.size());
        int verified = 0;
        for (const auto& ev : run.events)
        {
            const int S = ev.sliceLen, fade = 200;
            if (S != (int) (0.05 * SR)) { CHECK (false, "slice length %d", S); continue; }
            const int64_t a = ev.start + fade + 2 * S, b = ev.start + ev.len - fade;
            if (b + 1 > (int64_t) run.outL.size()) continue;
            double maxd = 0.0;
            for (int64_t n = a; n < b; ++n) maxd = std::max (maxd, (double) std::fabs (run.outL[(size_t) n] - run.outL[(size_t) (n - S)]));
            CHECK (maxd < 1e-5, "not periodic, max diff %g", maxd);
            ++verified;
        }
        CHECK (verified >= 2, "verified only %d stutters", verified);
    }

    // 4. pitch jump moves the pitch by the announced number of semitones
    {
        std::printf ("[4] pitch jump hits the right pitch\n");
        auto v = onlyEvent (EV_PITCHJUMP);
        v[P_CHAOS] = 0.5f; v[P_PITCH] = 12.f;
        auto e = make (v, 9);
        std::vector<float> in ((size_t) (SR * 6.0));
        for (size_t i = 0; i < in.size(); ++i) in[i] = 0.5f * (float) std::sin (6.283185307179586 * 500.0 * (double) i / SR);
        auto run = render (e, in);
        int verified = 0;
        for (const auto& ev : run.events)
        {
            const size_t a = (size_t) ev.start + (size_t) (ev.len * 0.2), b = (size_t) ev.start + (size_t) (ev.len * 0.8);
            if (b >= in.size()) continue;
            const double expect = 500.0 * std::pow (2.0, ev.semi0 / 12.0);
            const double got = zcFreq (run.outL, a, b);
            CHECK (std::fabs (got / expect - 1.0) < 0.05, "semi %.1f: expected %.0f Hz got %.0f Hz", ev.semi0, expect, got);
            ++verified;
        }
        CHECK (verified >= 4, "verified only %d pitch jumps", verified);
    }

    // 5. tape stop slows down
    {
        std::printf ("[5] tape stop slows the pitch down\n");
        auto v = onlyEvent (EV_TAPESTOP);
        v[P_EVMIN] = v[P_EVMAX] = 500.f;
        auto e = make (v, 4);
        std::vector<float> in ((size_t) (SR * 4.0));
        for (size_t i = 0; i < in.size(); ++i) in[i] = 0.5f * (float) std::sin (6.283185307179586 * 1000.0 * (double) i / SR);
        auto run = render (e, in);
        CHECK (! run.events.empty(), "no tape stop happened");
        for (const auto& ev : run.events)
        {
            const size_t s = (size_t) ev.start, L = (size_t) ev.len;
            if (s + L >= in.size()) continue;
            const double early = zcFreq (run.outL, s + (size_t) (0.02 * SR), s + (size_t) (0.07 * SR));
            const double late = zcFreq (run.outL, s + (size_t) (L * 0.55), s + (size_t) (L * 0.7));
            CHECK (early > 650.0 && late < 0.65 * early, "early %.0f Hz late %.0f Hz", early, late);
        }
    }

    // 6. reverse really plays backwards
    {
        std::printf ("[6] reverse plays backwards\n");
        auto v = onlyEvent (EV_REVERSE);
        v[P_SLMIN] = v[P_SLMAX] = 100.f; v[P_EVMIN] = v[P_EVMAX] = 300.f;
        auto e = make (v, 2);
        std::vector<float> in ((size_t) (SR * 3.0));
        for (size_t i = 0; i < in.size(); ++i) in[i] = 0.1f + 0.7f * (float) i / (float) in.size();
        auto run = render (e, in);
        CHECK (! run.events.empty(), "no reverse events");
        for (const auto& ev : run.events)
        {
            const size_t a = (size_t) ev.start + 400, b = (size_t) ev.start + (size_t) (0.2 * SR);
            if (b >= in.size()) continue;
            double slope = 0.0;
            for (size_t n = a; n < b; ++n) slope += run.outL[n + 1] - run.outL[n];
            CHECK (slope < 0.0, "reverse slope %g (should be negative)", slope);
        }
    }

    // 7. transient trigger reacts to word starts only
    {
        std::printf ("[7] transient trigger\n");
        auto v = onlyEvent (EV_STUTTER);
        v[P_TRIGMODE] = 1; v[P_SLMIN] = v[P_SLMAX] = 60.f;
        auto e = make (v, 8);
        std::vector<float> in ((size_t) (SR * 3.0), 0.f);
        const auto burst = noise ((size_t) (0.3 * SR), 6, 0.5f);
        const size_t onset = (size_t) SR;
        for (size_t i = 0; i < burst.size(); ++i) in[onset + i] = burst[i];
        auto run = render (e, in);
        CHECK (run.events.size() == 1, "expected exactly 1 event, got %zu", run.events.size());
        if (! run.events.empty())
        {
            const double dt = (double) ((int64_t) run.events[0].start - (int64_t) onset) / SR * 1000.0;
            CHECK (dt >= 55.0 && dt <= 90.0, "event started %.1f ms after word start (want ~60)", dt);
        }
    }

    // 8. sequencer only fires on enabled steps
    {
        std::printf ("[8] sequencer\n");
        auto v = onlyEvent (EV_STUTTER);
        v[P_TRIGMODE] = 3; v[P_DIV] = 2; v[P_EVMIN] = v[P_EVMAX] = 100.f;
        for (int i = 0; i < 16; ++i) v[(size_t) (P_STEP1 + i)] = (i == 0) ? 1.f : 0.f;
        auto e = make (v);
        auto run = render (e, noise ((size_t) (SR * 10.0), 11));
        CHECK (run.events.size() >= 4 && run.events.size() <= 6, "got %zu events (want ~5)", run.events.size());
        for (const auto& ev : run.events)
        {
            const double secs = (double) ev.start / SR;
            CHECK (std::fabs (secs / 2.0 - std::round (secs / 2.0)) < 0.002, "event at %.4f s is off the 16-step bar", secs);
        }
    }

    // 9. determinism + seed variation
    {
        std::printf ("[9] determinism\n");
        const auto in = sig::speechLike (SR, 5.0, 12);
        auto v = presetValues (3);
        auto a = make (v, 77), b = make (v, 77), c = make (v, 78);
        auto ra = render (a, in), rb = render (b, in, 100), rc = render (c, in);
        bool same = true, diff = false;
        for (size_t i = 0; i < in.size(); ++i)
        {
            if (ra.outL[i] != rb.outL[i]) same = false;
            if (ra.outL[i] != rc.outL[i]) diff = true;
        }
        CHECK (same, "same seed gave different audio (block size dependent?)");
        CHECK (diff, "different seeds gave identical audio");
    }

    // 10. randomizer invariants
    {
        std::printf ("[10] randomize / mutate\n");
        Rng rng; rng.seed (99);
        const auto& sp = paramSpecs();
        ParamValues v = defaultValues();
        std::array<bool, P_COUNT> locked {};
        locked[P_PITCH] = true; locked[P_CHAOS] = true;
        v[P_PITCH] = 7.f; v[P_CHAOS] = 0.123f; v[P_MIX] = 0.8f; v[P_GAIN] = -3.f;
        bool ok = true, lockOk = true, order = true, weights = true, mixOk = true;
        for (int it = 0; it < 3000; ++it)
        {
            randomizeParams (v, locked, rng, (it % 3 == 0) ? 1.f : 0.15f);
            for (int i = 0; i < P_COUNT; ++i)
                if (v[(size_t) i] < sp[(size_t) i].min || v[(size_t) i] > sp[(size_t) i].max || ! std::isfinite (v[(size_t) i])) ok = false;
            lockOk = lockOk && v[P_PITCH] == 7.f && v[P_CHAOS] == 0.123f;
            mixOk = mixOk && v[P_MIX] == 0.8f && v[P_GAIN] == -3.f;
            order = order && v[P_EVMIN] <= v[P_EVMAX] && v[P_SLMIN] <= v[P_SLMAX];
            float s = 0.f; for (int k = P_W_STUTTER; k <= P_W_SHUFFLE; ++k) s += v[(size_t) k];
            weights = weights && s > 0.04f;
        }
        CHECK (ok, "value out of range"); CHECK (lockOk, "locked param changed"); CHECK (mixOk, "mix/output changed");
        CHECK (order, "min > max"); CHECK (weights, "all glitch types off");
    }

    // 11. fx chain sanity: every fx on at full blast stays bounded
    {
        std::printf ("[11] max destruction stays bounded\n");
        auto v = defaultValues();
        v[P_PROB] = 1.f; v[P_CHAOS] = 1.f; v[P_FXCHANCE] = 1.f; v[P_DIV] = 4;
        v[P_CRUSH] = v[P_DOWNSAMPLE] = v[P_FOLD] = v[P_RING] = v[P_FILTER] = v[P_FLIP] = 1.f;
        for (int i = 0; i < EV_COUNT; ++i) v[(size_t) (P_W_STUTTER + i)] = 1.f;
        auto e = make (v, 5);
        auto run = render (e, sig::speechLike (SR, 10.0, 21), 256);
        float peak = 0.f; bool finite = true;
        for (size_t i = 0; i < run.outL.size(); ++i)
        {
            peak = std::max ({ peak, std::fabs (run.outL[i]), std::fabs (run.outR[i]) });
            finite = finite && std::isfinite (run.outL[i]) && std::isfinite (run.outR[i]);
        }
        CHECK (finite && peak <= 1.0f, "finite=%d peak=%.3f", (int) finite, peak);
        std::printf ("    %zu events in 10 s, peak %.3f\n", run.events.size(), peak);
    }

    // 12. speed
    {
        std::printf ("[12] speed\n");
        auto v = presetValues (3);
        auto e = make (v, 3);
        const auto in = sig::speechLike (SR, 30.0, 4);
        const auto t0 = std::chrono::steady_clock::now();
        auto run = render (e, in, 512);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const double cpuPercent = 100.0 * secs / 30.0;
        std::printf ("    30 s of audio in %.3f s -> %.2f%% of one core\n", secs, cpuPercent);
        CHECK (cpuPercent < 15.0, "too slow: %.1f%% cpu", cpuPercent);
    }

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
