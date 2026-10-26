// GlitchLab - the glitch engine.
// Pure C++17, no JUCE, no allocation in process(). Realtime-safe.
//
// How it works: the engine always records the incoming audio into a ring buffer.
// A trigger (grid / vocal transient / sequencer / manual) starts a short "event".
// An event grabs a slice of the last audio and plays it back in a broken way
// (stutter, pitch jump, tape stop, reverse, ...), optionally through destruction FX.
#pragma once
#include "ParamSpec.h"
#include <atomic>
#include <cstdint>
#include <cmath>
#include <vector>

namespace glitch
{

struct EventInfo
{
    int type = -1;
    int lenSamples = 0;
    int sliceLen = 0;
    double semi0 = 0, semi1 = 0;
    int64_t startSample = 0;
};

class GlitchEngine
{
public:
    // -------------------------------------------------------------- setup ---
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        int ringSize = 1;
        while ((double) ringSize < sr * 4.0) ringSize <<= 1;
        ringMask = ringSize - 1;
        ringL.assign ((size_t) ringSize, 0.f);
        ringR.assign ((size_t) ringSize, 0.f);
        frCap = (int) (sr * 1.5);
        frL.assign ((size_t) frCap + 8, 0.f);
        frR.assign ((size_t) frCap + 8, 0.f);
        reset();
    }

    void reset()
    {
        std::fill (ringL.begin(), ringL.end(), 0.f);
        std::fill (ringR.begin(), ringR.end(), 0.f);
        writeIdx = 0;
        active = false;
        pendDelay = -1;
        envFast = envSlow = 0.f;
        refractory = 0;
        lastStepValid = false;
        manual.store (0);
        sampleCounter = 0;
        eventsStarted = 0;
        float scale = (float) sr;
        aFA = 1.f - std::exp (-1.f / (0.001f * scale));
        aFR = 1.f - std::exp (-1.f / (0.010f * scale));
        aSA = 1.f - std::exp (-1.f / (0.050f * scale));
        aSR = 1.f - std::exp (-1.f / (0.200f * scale));
    }

    void seed (uint32_t s) { rng.seed (s); }
    void setParams (const ParamValues& v) { p = v; }

    // Host transport. When the host is not playing the grid free-runs on the last known tempo.
    void setTransport (double hostBpm, double hostPpq, bool playing)
    {
        if (hostBpm > 20.0 && hostBpm < 400.0) bpm = hostBpm;
        if (playing) ppq = hostPpq;
    }

    // type: 0..EV_COUNT-1 forces that glitch, -1 picks randomly from the enabled ones.
    void trigger (int type) { manual.store (type < 0 ? 1 : 2 + type); }

    // ------------------------------------------------------------ queries ---
    bool isActive() const { return active; }
    int currentEvent() const { return active ? ev.type : -1; }
    const EventInfo& lastEvent() const { return info; }
    int64_t eventCount() const { return eventsStarted; }

    // ------------------------------------------------------------ process ---
    // In place, stereo. (For mono hosts pass two separate buffers and use L.)
    void process (float* L, float* R, int n)
    {
        if (ringL.empty() || ringR.empty()) return; // prepare() has not been called yet: pass audio through untouched

        const float outGain = std::pow (10.f, p[P_GAIN] / 20.f);
        const float mixAmt = p[P_MIX];
        const int fadeN = std::max (8, msToSamples (3.f));
        const double beatsPerSample = bpm / (60.0 * sr);
        const double stepBeats = stepLengthBeats ((int) p[P_DIV]);

        for (int i = 0; i < n; ++i)
        {
            const float inL = L[i], inR = R[i];
            ringL[(size_t) (writeIdx & ringMask)] = inL;
            ringR[(size_t) (writeIdx & ringMask)] = inR;
            ++writeIdx;
            ++sampleCounter;

            // ---------------- transient detector ----------------
            const float m = 0.5f * (std::fabs (inL) + std::fabs (inR));
            envFast += (m - envFast) * (m > envFast ? aFA : aFR);
            envSlow += (m - envSlow) * (m > envSlow ? aSA : aSR);
            bool onset = false;
            if (refractory > 0) --refractory;
            else
            {
                const float ratio = 1.25f + (1.f - p[P_SENS]) * 3.0f;
                if (envFast > 0.004f && envFast > envSlow * ratio) { onset = true; refractory = msToSamples (90.f); }
            }

            // ---------------- grid / sequencer clock ----------------
            ppq += beatsPerSample;
            const int64_t stepNow = (int64_t) std::floor (ppq / stepBeats);
            bool stepChanged = false;
            if (! lastStepValid) { lastStep = stepNow; lastStepValid = true; }
            else if (stepNow != lastStep) { stepChanged = true; lastStep = stepNow; }

            // ---------------- triggering ----------------
            const int mode = (int) p[P_TRIGMODE];
            const int man = manual.exchange (0);
            if (man > 0)
            {
                startEvent (chooseType (man >= 2 ? man - 2 : -1), 0);
                pendDelay = -1;
            }
            else if (! active)
            {
                if (pendDelay >= 0)
                {
                    if (--pendDelay <= 0)
                    {
                        const int forced = pendSlice;
                        pendDelay = -1;
                        startEvent (chooseType (-1), forced);
                    }
                }
                else
                {
                    if ((mode == 0 || mode == 2) && stepChanged && rng.chance (p[P_PROB]))
                        startEvent (chooseType (-1), 0);
                    else if ((mode == 1 || mode == 2) && onset && rng.chance (p[P_PROB]))
                    {
                        // Wait one slice length so the slice we repeat contains the start of the word.
                        pendSlice = msToSamples (spread (p[P_SLMIN], p[P_SLMAX], p[P_CHAOS]));
                        pendDelay = std::max (1, pendSlice);
                    }
                    else if (mode == 3 && stepChanged)
                    {
                        const int s = (int) (((stepNow % 16) + 16) % 16);
                        if (p[(size_t) (P_STEP1 + s)] > 0.5f) startEvent (chooseType (-1), 0);
                    }
                }
            }

            // ---------------- render ----------------
            float outL = inL, outR = inR;
            if (active)
            {
                float wl = 0.f, wr = 0.f;
                renderEvent (wl, wr, inL, inR);
                applyFx (wl, wr);
                wl = softClip (wl);
                wr = softClip (wr);

                float g = 1.f;
                const int a = ev.pos, b = ev.len - ev.pos;
                if (a < fadeN) g = std::min (g, (float) a / (float) fadeN);
                if (b < fadeN) g = std::min (g, (float) b / (float) fadeN);
                g = std::max (0.f, g) * mixAmt;
                outL = inL * (1.f - g) + wl * g;
                outR = inR * (1.f - g) + wr * g;

                if (++ev.pos >= ev.len)
                {
                    active = false;
                    // Chaos can chain events back to back for rapid-fire craziness.
                    if (rng.chance (p[P_CHAOS] * 0.4f)) startEvent (chooseType (-1), 0);
                }
            }
            L[i] = outL * outGain;
            R[i] = outR * outGain;
        }
    }

private:
    // ----------------------------------------------------------- helpers ---
    int msToSamples (float ms) const { return (int) (ms * 0.001 * sr); }

    static double stepLengthBeats (int div)
    {
        static const double t[7] = { 1.0, 0.5, 0.25, 0.125, 0.0625, 1.0 / 3.0, 1.0 / 6.0 };
        return t[std::min (std::max (div, 0), 6)];
    }

    float spread (float lo, float hi, float chaos)
    {
        if (hi < lo) std::swap (lo, hi);
        const float mid = 0.5f * (lo + hi);
        return mid + (rng.range (lo, hi) - mid) * chaos;
    }

    static float softClip (float x)
    {
        if (! std::isfinite (x)) return 0.f;
        const float a = std::fabs (x);
        if (a <= 0.9f) return x;
        const float y = 0.9f + 0.1f * std::tanh ((a - 0.9f) * 10.f);
        return x < 0.f ? -y : y;
    }

    static float hermite (float xm1, float x0, float x1, float x2, float t)
    {
        const float c = (x1 - xm1) * 0.5f;
        const float v = x0 - x1;
        const float w = c + v;
        const float a = w + v + (x2 - x0) * 0.5f;
        const float bNeg = w + a;
        return ((a * t - bNeg) * t + c) * t + x0;
    }

    void readFrozen (double pos, float& l, float& r) const
    {
        const int hi = std::max (0, ev.sliceLen - 1);
        const int i0 = (int) std::floor (pos);
        const float t = (float) (pos - (double) i0);
        auto at = [&] (const std::vector<float>& b, int k) { return b[(size_t) std::min (std::max (k, 0), hi)]; };
        l = hermite (at (frL, i0 - 1), at (frL, i0), at (frL, i0 + 1), at (frL, i0 + 2), t);
        r = hermite (at (frR, i0 - 1), at (frR, i0), at (frR, i0 + 1), at (frR, i0 + 2), t);
    }

    void readRing (double pos, float& l, float& r) const
    {
        const int64_t newest = writeIdx - 1;
        const int64_t i0 = (int64_t) std::floor (pos);
        const float t = (float) (pos - (double) i0);
        auto at = [&] (const std::vector<float>& b, int64_t k) { return b[(size_t) (std::min (k, newest) & ringMask)]; };
        l = hermite (at (ringL, i0 - 1), at (ringL, i0), at (ringL, i0 + 1), at (ringL, i0 + 2), t);
        r = hermite (at (ringR, i0 - 1), at (ringR, i0), at (ringR, i0 + 1), at (ringR, i0 + 2), t);
    }

    int chooseType (int forced)
    {
        if (forced >= 0 && forced < EV_COUNT) return forced;
        float sum = 0.f;
        for (int i = 0; i < EV_COUNT; ++i) sum += p[(size_t) (P_W_STUTTER + i)];
        if (sum <= 0.0001f) return -1;
        float r = rng.uni() * sum;
        for (int i = 0; i < EV_COUNT; ++i)
        {
            r -= p[(size_t) (P_W_STUTTER + i)];
            if (r <= 0.f) return i;
        }
        return EV_STUTTER;
    }

    // ------------------------------------------------------------- events ---
    struct Voice { bool on = false; double pos = 0, inc = 1; int len = 1, age = 0; float gl = 1.f, gr = 1.f; };

    struct Event
    {
        int type = 0, len = 0, pos = 0;
        int sliceLen = 0;
        double rd = 0, loopLen = 0, shrink = 1.0;
        bool reverse = false;
        double semi0 = 0, semi1 = 0, curve = 1.0;
        double phase = 0;                       // freeze
        int pieces = 4; int perm[8] = { 0, 1, 2, 3, 4, 5, 6, 7 }; double pieceLen = 1.0; // shuffle
        Voice v[6]; int nextGrain = 0, grainHop = 1;                                        // scatter
        bool gateOn = false; int gateCount = 0; float gateGain = 1.f; int segMin = 1, segMax = 2; // dropout
        double ringPos = 0;                     // tape stop (reads the live ring buffer)
        // fx
        bool crush = false; float levels = 0.f;
        int hold = 1, holdCnt = 0; float heldL = 0.f, heldR = 0.f;
        bool fold = false; float drive = 1.f;
        bool ring = false; float ringMix = 0.f; double ringPh = 0, ringInc = 0;
        bool filt = false; int filtType = 0; double fc0 = 1000, fc1 = 1000, q = 1; int filtCount = 0;
        float a1 = 0, a2 = 0, a3 = 0, k = 1, ic1L = 0, ic2L = 0, ic1R = 0, ic2R = 0;
        bool pan = false; float panDepth = 0; double panPh = 0, panInc = 0;
    };

    void startEvent (int type, int forcedSlice)
    {
        if (type < 0) return;
        Event e;
        e.type = type;
        const float c = p[P_CHAOS];
        const float prange = p[P_PITCH];
        e.len = std::max (msToSamples (spread (p[P_EVMIN], p[P_EVMAX], c)), msToSamples (20.f));

        auto sliceSamples = [&] (float mulMin, float mulMax)
        {
            if (forcedSlice > 0) return forcedSlice;
            return msToSamples (spread (p[P_SLMIN] * mulMin, p[P_SLMAX] * mulMax, c));
        };
        auto randSemi = [&]
        {
            const float mag = prange < 0.5f ? 0.f : rng.range (1.f, std::max (1.f, prange));
            return (double) (rng.chance (0.5f) ? mag : -mag);
        };
        const int minSlice = msToSamples (2.f) + 4;
        bool needFrozen = true;

        switch (type)
        {
            case EV_STUTTER:
                e.sliceLen = sliceSamples (1.f, 1.f);
                e.loopLen = e.sliceLen;
                e.shrink = 1.0 - 0.3 * (double) p[P_RAMP];
                e.semi0 = e.semi1 = (prange > 0.5f && rng.chance (c * 0.5f)) ? randSemi() * 0.5 : 0.0;
                break;
            case EV_PITCHJUMP:
            {
                const double semi = randSemi();
                const double ratio = std::pow (2.0, semi / 12.0);
                e.semi0 = e.semi1 = semi;
                e.sliceLen = (int) std::min ((double) e.len * ratio, (double) frCap);
                e.sliceLen = std::max (e.sliceLen, msToSamples (10.f));
                e.loopLen = e.sliceLen;
                e.reverse = rng.chance (c * 0.15f);
                break;
            }
            case EV_SWOOP:
                e.sliceLen = sliceSamples (2.f, 4.f);
                e.loopLen = e.sliceLen;
                e.semi0 = rng.chance (0.6f) ? 0.0 : randSemi() * 0.4;
                e.semi1 = randSemi();
                e.curve = rng.range (0.6f, 2.0f);
                e.reverse = rng.chance (c * 0.3f);
                break;
            case EV_TAPESTOP:
                needFrozen = false;
                e.len = std::max (e.len, msToSamples (150.f));
                e.curve = rng.range (0.8f, 2.2f);
                e.ringPos = (double) (writeIdx - 1);
                break;
            case EV_SPEEDUP:
                e.sliceLen = sliceSamples (2.f, 4.f);
                e.loopLen = e.sliceLen;
                e.semi0 = -(double) rng.range (12.f, 30.f);
                e.semi1 = rng.chance (0.5f) ? 0.0 : (double) rng.range (3.f, 12.f);
                e.curve = rng.range (0.7f, 1.5f);
                break;
            case EV_REVERSE:
                e.sliceLen = sliceSamples (2.f, 3.f);
                e.loopLen = e.sliceLen;
                e.reverse = true;
                e.len = std::max (e.len, e.sliceLen);
                e.semi0 = e.semi1 = rng.chance (c * 0.5f) ? randSemi() * 0.3 : 0.0;
                break;
            case EV_FREEZE:
                e.sliceLen = sliceSamples (1.5f, 1.5f);
                e.semi0 = e.semi1 = rng.chance (c * 0.5f) ? randSemi() * 0.3 : 0.0;
                break;
            case EV_SCATTER:
                e.sliceLen = std::min (msToSamples (600.f), frCap);
                e.grainHop = std::max (1, msToSamples (spread (8.f, 45.f, c)));
                e.nextGrain = 1;
                break;
            case EV_DROPOUT:
                needFrozen = false;
                e.segMin = std::max (msToSamples (2.f), msToSamples (p[P_SLMIN] * 0.5f));
                e.segMax = std::max (e.segMin + 1, msToSamples (p[P_SLMAX] * 0.8f));
                e.gateOn = false;
                e.gateCount = e.segMin + (int) (rng.uni() * (float) (e.segMax - e.segMin));
                e.gateGain = 1.f;
                break;
            case EV_SHUFFLE:
            default:
                e.sliceLen = sliceSamples (3.f, 6.f);
                e.pieces = 3 + rng.pick (6);
                for (int k = 0; k < 8; ++k) e.perm[k] = k;
                for (int k = e.pieces - 1; k > 0; --k) std::swap (e.perm[k], e.perm[rng.pick (k + 1)]);
                if (e.perm[0] == 0) std::swap (e.perm[0], e.perm[1]); // never the identity at the start
                e.semi0 = e.semi1 = 0.0;
                break;
        }

        if (needFrozen)
        {
            e.sliceLen = std::min (std::max (e.sliceLen, minSlice), frCap);
            if (e.loopLen > 0.0) e.loopLen = std::min (e.loopLen, (double) e.sliceLen);
            if (type == EV_STUTTER || type == EV_PITCHJUMP || type == EV_SWOOP || type == EV_SPEEDUP || type == EV_REVERSE)
                e.loopLen = e.sliceLen;
            if (type == EV_SHUFFLE) e.pieceLen = (double) e.sliceLen / (double) e.pieces;
            for (int k = 0; k < e.sliceLen; ++k)
            {
                const int64_t idx = writeIdx - e.sliceLen + k;
                frL[(size_t) k] = ringL[(size_t) (idx & ringMask)];
                frR[(size_t) k] = ringR[(size_t) (idx & ringMask)];
            }
            e.rd = e.reverse ? (double) (e.sliceLen - 1) : 0.0;
        }

        // -------- destruction FX for this event --------
        const float fxc = p[P_FXCHANCE];
        auto roll = [&] (float amt) { return amt > 0.001f && rng.chance (fxc); };
        if (roll (p[P_DOWNSAMPLE]))
            e.hold = 1 + (int) (p[P_DOWNSAMPLE] * rng.range (0.4f, 1.f) * 31.f);
        if (roll (p[P_CRUSH]))
        {
            e.crush = true;
            const float bits = std::max (3.f, 16.f - p[P_CRUSH] * rng.range (0.5f, 1.f) * 13.f);
            e.levels = std::pow (2.f, bits - 1.f);
        }
        if (roll (p[P_FOLD]))
        {
            e.fold = true;
            e.drive = 1.f + p[P_FOLD] * rng.range (0.5f, 1.f) * 9.f;
        }
        if (roll (p[P_RING]))
        {
            e.ring = true;
            e.ringMix = p[P_RING];
            e.ringInc = (double) (60.f * std::pow (30.f, rng.uni())) / sr;
        }
        if (roll (p[P_FILTER]))
        {
            e.filt = true;
            e.filtType = rng.pick (3);
            e.fc0 = (double) (150.f * std::pow (50.f, rng.uni()));
            e.fc1 = (double) (150.f * std::pow (50.f, rng.uni()));
            if (std::fabs (std::log (e.fc1 / e.fc0)) < 0.7) e.fc1 = e.fc0 * (rng.chance (0.5f) ? 3.0 : 0.33);
            e.q = (double) (0.7f + p[P_FILTER] * rng.range (0.5f, 7.f));
            e.k = (float) (1.0 / e.q);
        }
        if (roll (p[P_FLIP]))
        {
            e.pan = true;
            e.panDepth = p[P_FLIP];
            e.panInc = (double) rng.range (2.f, 14.f) / sr;
        }

        ev = e;
        active = true;
        ++eventsStarted;
        info.type = type;
        info.lenSamples = e.len;
        info.sliceLen = e.sliceLen;
        info.semi0 = e.semi0;
        info.semi1 = e.semi1;
        info.startSample = sampleCounter;
    }

    // Wraps/advances the read head inside the frozen slice and returns the loop-edge fade gain.
    float stepFrozen (double ratio, float& l, float& r)
    {
        const double end = (double) ev.sliceLen;
        const double minLoop = (double) std::max (4, msToSamples (3.f));
        double ls = end - ev.loopLen;
        if (! ev.reverse)
        {
            if (ev.rd >= end)
            {
                const double over = ev.rd - end;
                ev.loopLen = std::min (end, std::max (minLoop, ev.loopLen * ev.shrink));
                ls = end - ev.loopLen;
                ev.rd = ls + std::fmod (over, ev.loopLen);
            }
        }
        else if (ev.rd < ls)
        {
            const double under = ls - ev.rd;
            ev.loopLen = std::min (end, std::max (minLoop, ev.loopLen * ev.shrink));
            ls = end - ev.loopLen;
            ev.rd = std::max (ls, end - std::fmod (under, ev.loopLen) - 1.0);
        }
        readFrozen (ev.rd, l, r);
        const double f = std::max (1.0, std::min (ev.loopLen * 0.2, 0.001 * sr));
        const float gain = (float) std::max (0.0, std::min ({ 1.0, (ev.rd - ls) / f, (end - ev.rd) / f }));
        ev.rd += ev.reverse ? -ratio : ratio;
        return gain;
    }

    static float hann (double ph) { return (float) (0.5 - 0.5 * std::cos (6.283185307179586 * ph)); }

    void renderEvent (float& wl, float& wr, float inL, float inR)
    {
        const double t = (double) ev.pos / (double) std::max (1, ev.len - 1);
        const double semiNow = ev.semi0 + (ev.semi1 - ev.semi0) * std::pow (t, ev.curve);
        const double ratio = std::pow (2.0, semiNow / 12.0);

        switch (ev.type)
        {
            case EV_STUTTER:
            case EV_PITCHJUMP:
            case EV_SWOOP:
            case EV_SPEEDUP:
            case EV_REVERSE:
            {
                float l, r;
                const float g = stepFrozen (ratio, l, r);
                wl = l * g; wr = r * g;
                break;
            }
            case EV_TAPESTOP:
            {
                const double rr = std::pow (std::max (0.0, 1.0 - t), ev.curve);
                readRing (ev.ringPos, wl, wr);
                ev.ringPos += rr;
                break;
            }
            case EV_FREEZE:
            {
                const double G = (double) ev.sliceLen;
                const double pa = ev.phase, pb = ev.phase - std::floor (ev.phase + 0.5) + 0.5; // pb = frac(pa + 0.5)
                const double pbb = std::fmod (pa + 0.5, 1.0);
                (void) pb;
                float la, ra, lb, rb;
                readFrozen (pa * G, la, ra);
                readFrozen (pbb * G, lb, rb);
                const float wa = hann (pa), wb = hann (pbb);
                wl = la * wa + lb * wb;
                wr = ra * wa + rb * wb;
                ev.phase += ratio / G;
                ev.phase -= std::floor (ev.phase);
                break;
            }
            case EV_SCATTER:
            {
                if (--ev.nextGrain <= 0)
                {
                    ev.nextGrain = std::max (1, (int) ((double) ev.grainHop * (double) rng.range (0.5f, 1.5f)));
                    for (auto& v : ev.v)
                        if (! v.on)
                        {
                            const float c = p[P_CHAOS];
                            const float prange = p[P_PITCH];
                            double semi = 0.0;
                            if (prange >= 0.5f && ! rng.chance (0.5f))
                                semi = (double) (rng.chance (0.5f) ? 1.f : -1.f) * (double) rng.range (1.f, prange) * (0.3 + 0.7 * (double) c);
                            v.inc = std::pow (2.0, semi / 12.0);
                            v.len = std::max (16, msToSamples (rng.range (10.f, 70.f)));
                            const double room = std::max (1.0, (double) ev.sliceLen - (double) v.len * v.inc - 4.0);
                            v.pos = (double) rng.uni() * room;
                            v.age = 0;
                            const float pan = rng.range (-0.7f, 0.7f) * c;
                            const float ang = (pan + 1.f) * 0.78539816f;
                            v.gl = std::cos (ang) * 1.41421356f;
                            v.gr = std::sin (ang) * 1.41421356f;
                            v.on = true;
                            break;
                        }
                }
                float sl = 0.f, sr_ = 0.f;
                for (auto& v : ev.v)
                {
                    if (! v.on) continue;
                    float l, r;
                    readFrozen (v.pos, l, r);
                    const float w = hann ((double) v.age / (double) v.len) * 0.7f;
                    sl += l * w * v.gl;
                    sr_ += r * w * v.gr;
                    v.pos += v.inc;
                    if (++v.age >= v.len) v.on = false;
                }
                wl = sl; wr = sr_;
                break;
            }
            case EV_DROPOUT:
            {
                if (--ev.gateCount <= 0)
                {
                    ev.gateOn = ! ev.gateOn;
                    ev.gateCount = ev.segMin + (int) (rng.uni() * (float) (ev.segMax - ev.segMin));
                }
                const float target = ev.gateOn ? 1.f : 0.f;
                const float step = 1.f / (float) std::max (8, msToSamples (1.f));
                if (ev.gateGain < target) ev.gateGain = std::min (target, ev.gateGain + step);
                else ev.gateGain = std::max (target, ev.gateGain - step);
                wl = inL * ev.gateGain;
                wr = inR * ev.gateGain;
                break;
            }
            case EV_SHUFFLE:
            default:
            {
                const double total = (double) ev.sliceLen;
                if (ev.rd >= total) ev.rd = std::fmod (ev.rd, total);
                const int piece = std::min (ev.pieces - 1, (int) (ev.rd / ev.pieceLen));
                const double within = ev.rd - (double) piece * ev.pieceLen;
                const double src = (double) ev.perm[piece] * ev.pieceLen + within;
                float l, r;
                readFrozen (src, l, r);
                const double f = std::max (1.0, std::min (ev.pieceLen * 0.15, 0.001 * sr));
                const float g = (float) std::max (0.0, std::min ({ 1.0, within / f, (ev.pieceLen - within) / f }));
                wl = l * g; wr = r * g;
                ev.rd += 1.0;
                break;
            }
        }
    }

    static float foldback (float x)
    {
        return std::asin (std::sin (x * 1.5707963f)) * 0.63661977f;
    }

    void applyFx (float& l, float& r)
    {
        if (ev.hold > 1)
        {
            if (ev.holdCnt == 0) { ev.heldL = l; ev.heldR = r; }
            ev.holdCnt = (ev.holdCnt + 1) % ev.hold;
            l = ev.heldL; r = ev.heldR;
        }
        if (ev.crush)
        {
            l = std::floor (l * ev.levels + 0.5f) / ev.levels;
            r = std::floor (r * ev.levels + 0.5f) / ev.levels;
        }
        if (ev.fold)
        {
            l = foldback (l * ev.drive);
            r = foldback (r * ev.drive);
        }
        if (ev.ring)
        {
            const float c = (float) std::sin (6.283185307179586 * ev.ringPh);
            ev.ringPh += ev.ringInc;
            ev.ringPh -= std::floor (ev.ringPh);
            const float m = ev.ringMix;
            l *= (1.f - m) + m * c;
            r *= (1.f - m) + m * c;
        }
        if (ev.filt)
        {
            if ((ev.filtCount++ & 15) == 0)
            {
                const double t = (double) ev.pos / (double) std::max (1, ev.len - 1);
                double fc = ev.fc0 * std::pow (ev.fc1 / ev.fc0, t);
                fc = std::min (fc, 0.45 * sr);
                const double g = std::tan (3.141592653589793 * fc / sr);
                ev.a1 = (float) (1.0 / (1.0 + g * (g + (double) ev.k)));
                ev.a2 = (float) g * ev.a1;
                ev.a3 = (float) g * ev.a2;
            }
            auto svf = [&] (float x, float& ic1, float& ic2)
            {
                const float v3 = x - ic2;
                const float v1 = ev.a1 * ic1 + ev.a2 * v3;
                const float v2 = ic2 + ev.a2 * ic1 + ev.a3 * v3;
                ic1 = 2.f * v1 - ic1;
                ic2 = 2.f * v2 - ic2;
                if (ev.filtType == 0) return v2;
                if (ev.filtType == 1) return x - ev.k * v1 - v2;
                return v1 * ev.k;
            };
            l = svf (l, ev.ic1L, ev.ic2L);
            r = svf (r, ev.ic1R, ev.ic2R);
        }
        if (ev.pan)
        {
            const float lfo = (float) std::sin (6.283185307179586 * ev.panPh);
            ev.panPh += ev.panInc;
            ev.panPh -= std::floor (ev.panPh);
            const float ang = (lfo * ev.panDepth + 1.f) * 0.78539816f;
            l *= std::cos (ang) * 1.41421356f;
            r *= std::sin (ang) * 1.41421356f;
        }
    }

    // -------------------------------------------------------------- state ---
    double sr = 48000.0;
    ParamValues p = defaultValues();
    Rng rng;

    std::vector<float> ringL, ringR;
    int ringMask = 0;
    int64_t writeIdx = 0;
    std::vector<float> frL, frR;
    int frCap = 0;

    double bpm = 120.0, ppq = 0.0;
    int64_t lastStep = 0;
    bool lastStepValid = false;

    float envFast = 0.f, envSlow = 0.f, aFA = 0.f, aFR = 0.f, aSA = 0.f, aSR = 0.f;
    int refractory = 0;
    int pendDelay = -1, pendSlice = 0;

    // Copyable atomic int (the engine itself stays copyable, which keeps tests simple).
    struct AtomicInt
    {
        std::atomic<int> v { 0 };
        AtomicInt() = default;
        AtomicInt (const AtomicInt& o) { v.store (o.v.load()); }
        AtomicInt& operator= (const AtomicInt& o) { v.store (o.v.load()); return *this; }
        void store (int x) { v.store (x); }
        int exchange (int x) { return v.exchange (x); }
    };
    AtomicInt manual;
    bool active = false;
    Event ev;
    EventInfo info;
    int64_t sampleCounter = 0;
    int64_t eventsStarted = 0;
};

} // namespace glitch
