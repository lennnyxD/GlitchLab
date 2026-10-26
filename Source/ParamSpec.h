// GlitchLab - parameter table, presets and randomizer.
// Pure C++ (no JUCE) so it can be unit-tested and reused by the CLI tool.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <vector>

namespace glitch
{

// ---------------------------------------------------------------- RNG ----
struct Rng
{
    uint32_t s = 0x12345678u;
    void seed (uint32_t x) { s = x ? x : 0x9E3779B9u; }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    float range (float a, float b) { return a + (b - a) * uni(); }
    bool chance (float p) { return uni() < p; }
    int pick (int n) { return n <= 1 ? 0 : (int) (uni() * (float) n) % n; }
};

// ---------------------------------------------------------- event types ---
enum EventType
{
    EV_STUTTER = 0,
    EV_PITCHJUMP,
    EV_SWOOP,
    EV_TAPESTOP,
    EV_SPEEDUP,
    EV_REVERSE,
    EV_FREEZE,
    EV_SCATTER,
    EV_DROPOUT,
    EV_SHUFFLE,
    EV_COUNT
};

inline const char* eventName (int t)
{
    static const char* n[EV_COUNT] = { "Stutter", "Pitch Jump", "Pitch Swoop", "Tape Stop", "Speed Up",
                                       "Reverse", "Freeze", "Scatter", "Dropout", "Shuffle" };
    return n[t];
}

// ----------------------------------------------------------- parameters ---
enum ParamIndex
{
    P_MIX = 0, P_GAIN,
    P_TRIGMODE, P_DIV, P_PROB, P_SENS, P_CHAOS,
    P_EVMIN, P_EVMAX, P_SLMIN, P_SLMAX, P_PITCH, P_RAMP,
    P_W_STUTTER, P_W_PITCHJUMP, P_W_SWOOP, P_W_TAPESTOP, P_W_SPEEDUP,
    P_W_REVERSE, P_W_FREEZE, P_W_SCATTER, P_W_DROPOUT, P_W_SHUFFLE,
    P_CRUSH, P_DOWNSAMPLE, P_FOLD, P_RING, P_FILTER, P_FLIP, P_FXCHANCE,
    P_STEP1,
    P_STEP16 = P_STEP1 + 15,
    P_COUNT
};

enum ParamKind { K_FLOAT = 0, K_CHOICE = 1, K_BOOL = 2 };

struct ParamSpec
{
    const char* id;
    const char* name;
    float min, max, def;
    ParamKind kind;
    const char* group;
    const char* choices; // '|' separated, for K_CHOICE
};

inline const char* kTriggerModes = "Grid|Transient|Grid + Transient|Sequencer|Manual only";
inline const char* kDivisions    = "1/4|1/8|1/16|1/32|1/64|1/8 triplet|1/16 triplet";

inline const std::array<ParamSpec, P_COUNT>& paramSpecs()
{
    static const std::array<ParamSpec, P_COUNT> specs = []
    {
        std::array<ParamSpec, P_COUNT> a {};
        auto set = [&] (int i, ParamSpec s) { a[(size_t) i] = s; };
        set (P_MIX,   { "mix",   "Mix",    0.f, 1.f, 1.f, K_FLOAT, "Main", nullptr });
        set (P_GAIN,  { "gain",  "Output", -24.f, 12.f, 0.f, K_FLOAT, "Main", nullptr });

        set (P_TRIGMODE, { "trigmode", "Trigger",     0, 4, 0, K_CHOICE, "Trigger", kTriggerModes });
        set (P_DIV,      { "div",      "Grid",        0, 6, 2, K_CHOICE, "Trigger", kDivisions });
        set (P_PROB,     { "prob",     "Probability", 0.f, 1.f, 0.35f, K_FLOAT, "Trigger", nullptr });
        set (P_SENS,     { "sens",     "Sensitivity", 0.f, 1.f, 0.55f, K_FLOAT, "Trigger", nullptr });
        set (P_CHAOS,    { "chaos",    "Chaos",       0.f, 1.f, 0.5f, K_FLOAT, "Trigger", nullptr });

        set (P_EVMIN, { "evmin", "Event min ms", 20.f, 1500.f, 90.f,  K_FLOAT, "Shape", nullptr });
        set (P_EVMAX, { "evmax", "Event max ms", 20.f, 1500.f, 450.f, K_FLOAT, "Shape", nullptr });
        set (P_SLMIN, { "slmin", "Slice min ms", 2.f, 500.f, 18.f,  K_FLOAT, "Shape", nullptr });
        set (P_SLMAX, { "slmax", "Slice max ms", 2.f, 500.f, 130.f, K_FLOAT, "Shape", nullptr });
        set (P_PITCH, { "pitch", "Pitch range st", 0.f, 36.f, 12.f, K_FLOAT, "Shape", nullptr });
        set (P_RAMP,  { "ramp",  "Stutter ramp", -1.f, 1.f, 0.f, K_FLOAT, "Shape", nullptr });

        const char* wid[EV_COUNT]  = { "w_stutter", "w_pitchjump", "w_swoop", "w_tapestop", "w_speedup",
                                       "w_reverse", "w_freeze", "w_scatter", "w_dropout", "w_shuffle" };
        const char* wnm[EV_COUNT]  = { "Stutter", "Pitch Jump", "Swoop", "Tape Stop", "Speed Up",
                                       "Reverse", "Freeze", "Scatter", "Dropout", "Shuffle" };
        const float wdef[EV_COUNT] = { 1.f, 0.7f, 0.6f, 0.3f, 0.3f, 0.4f, 0.15f, 0.3f, 0.3f, 0.3f };
        for (int i = 0; i < EV_COUNT; ++i)
            set (P_W_STUTTER + i, { wid[i], wnm[i], 0.f, 1.f, wdef[i], K_FLOAT, "Glitches", nullptr });

        set (P_CRUSH,      { "crush",    "Bitcrush",   0.f, 1.f, 0.f, K_FLOAT, "FX", nullptr });
        set (P_DOWNSAMPLE, { "down",     "Downsample", 0.f, 1.f, 0.f, K_FLOAT, "FX", nullptr });
        set (P_FOLD,       { "fold",     "Fold dist",  0.f, 1.f, 0.f, K_FLOAT, "FX", nullptr });
        set (P_RING,       { "ring",     "Ring mod",   0.f, 1.f, 0.f, K_FLOAT, "FX", nullptr });
        set (P_FILTER,     { "filter",   "Filter sweep", 0.f, 1.f, 0.f, K_FLOAT, "FX", nullptr });
        set (P_FLIP,       { "flip",     "Auto-pan",   0.f, 1.f, 0.f, K_FLOAT, "FX", nullptr });
        set (P_FXCHANCE,   { "fxchance", "FX chance",  0.f, 1.f, 0.6f, K_FLOAT, "FX", nullptr });

        static const char* stepIds[16] = { "step1","step2","step3","step4","step5","step6","step7","step8",
                                           "step9","step10","step11","step12","step13","step14","step15","step16" };
        static const char* stepNm[16]  = { "Step 1","Step 2","Step 3","Step 4","Step 5","Step 6","Step 7","Step 8",
                                           "Step 9","Step 10","Step 11","Step 12","Step 13","Step 14","Step 15","Step 16" };
        for (int i = 0; i < 16; ++i)
            set (P_STEP1 + i, { stepIds[i], stepNm[i], 0.f, 1.f, (i % 4 == 0) ? 1.f : 0.f, K_BOOL, "Sequencer", nullptr });
        return a;
    }();
    return specs;
}

using ParamValues = std::array<float, P_COUNT>;

inline ParamValues defaultValues()
{
    ParamValues v {};
    for (int i = 0; i < P_COUNT; ++i) v[(size_t) i] = paramSpecs()[(size_t) i].def;
    return v;
}

// ------------------------------------------------------------ randomizer ---
// amount = 1 -> full randomize, small amounts -> "mutate" (drift from current).
// locked[i] == true keeps parameter i untouched. Never touches Mix / Output.
inline void randomizeParams (ParamValues& v, const std::array<bool, P_COUNT>& locked, Rng& rng, float amount)
{
    const auto& sp = paramSpecs();
    for (int i = 0; i < P_COUNT; ++i)
    {
        if (locked[(size_t) i] || i == P_MIX || i == P_GAIN) continue;
        const auto& s = sp[(size_t) i];
        float& x = v[(size_t) i];

        if (s.kind == K_BOOL)
        {
            if (rng.chance (amount * (amount > 0.5f ? 0.4f : 0.35f)))
                x = (amount > 0.5f) ? (rng.chance (0.4f) ? 1.f : 0.f) : 1.f - x;
        }
        else if (s.kind == K_CHOICE)
        {
            if (i == P_TRIGMODE) continue; // keep how the user triggers things
            if (rng.chance (amount > 0.5f ? 1.0f : amount * 0.5f))
                x = (float) rng.pick ((int) s.max + 1);
        }
        else
        {
            float target = rng.range (s.min, s.max);
            if (i >= P_W_STUTTER && i <= P_W_SHUFFLE) // weights: lots of zeros so presets feel focused
                target = rng.chance (0.45f) ? 0.f : rng.range (0.3f, 1.f);
            if (i >= P_CRUSH && i <= P_FLIP)
                target = rng.chance (0.5f) ? 0.f : rng.range (0.2f, 1.f);
            if (i == P_PROB) target = rng.range (0.2f, 0.9f);
            if (i == P_EVMIN || i == P_SLMIN) target = s.min + (s.max - s.min) * std::pow (rng.uni(), 3.f);
            if (i == P_EVMAX || i == P_SLMAX) target = s.min + (s.max - s.min) * (0.15f + 0.85f * std::pow (rng.uni(), 2.f));
            x = x + (target - x) * amount;
        }
        x = std::min (std::max (x, s.min), s.max);
    }
    // keep min <= max
    if (v[P_EVMIN] > v[P_EVMAX]) std::swap (v[P_EVMIN], v[P_EVMAX]);
    if (v[P_SLMIN] > v[P_SLMAX]) std::swap (v[P_SLMIN], v[P_SLMAX]);
    // make sure at least one glitch type is on
    float wsum = 0.f;
    for (int i = P_W_STUTTER; i <= P_W_SHUFFLE; ++i) wsum += v[(size_t) i];
    if (wsum < 0.05f) v[(size_t) (P_W_STUTTER + rng.pick (EV_COUNT))] = 1.f;
}

// --------------------------------------------------------------- presets ---
struct PresetEntry { int index; float value; };
struct Preset { const char* name; std::vector<PresetEntry> values; };

inline const std::vector<Preset>& presets()
{
    auto W = [] (float st, float pj, float sw, float ts, float su, float rv, float fr, float sc, float dr, float sh)
    {
        return std::vector<PresetEntry> {
            { P_W_STUTTER, st }, { P_W_PITCHJUMP, pj }, { P_W_SWOOP, sw }, { P_W_TAPESTOP, ts }, { P_W_SPEEDUP, su },
            { P_W_REVERSE, rv }, { P_W_FREEZE, fr }, { P_W_SCATTER, sc }, { P_W_DROPOUT, dr }, { P_W_SHUFFLE, sh } };
    };
    auto cat = [] (std::vector<PresetEntry> a, std::vector<PresetEntry> b) { a.insert (a.end(), b.begin(), b.end()); return a; };

    static const std::vector<Preset> list = {
        { "Init (everything on)", {} },

        { "Vocal Stutter - Subtle",
          cat (W (1, 0.1f, 0, 0, 0, 0, 0, 0, 0, 0),
               { { P_TRIGMODE, 1 }, { P_PROB, 0.3f }, { P_CHAOS, 0.15f }, { P_EVMIN, 120 }, { P_EVMAX, 320 },
                 { P_SLMIN, 40 }, { P_SLMAX, 110 }, { P_PITCH, 3 }, { P_RAMP, 0.2f }, { P_SENS, 0.5f } }) },

        { "Word Chopper (hi my name name name)",
          cat (W (1, 0, 0, 0, 0, 0, 0, 0, 0, 0.2f),
               { { P_TRIGMODE, 1 }, { P_PROB, 0.55f }, { P_CHAOS, 0.3f }, { P_EVMIN, 150 }, { P_EVMAX, 450 },
                 { P_SLMIN, 50 }, { P_SLMAX, 140 }, { P_PITCH, 0 }, { P_RAMP, 0.0f }, { P_SENS, 0.6f } }) },

        { "Hyperpop Chaos",
          cat (W (1, 1, 1, 0.5f, 0.6f, 0.6f, 0.2f, 0.6f, 0.4f, 0.6f),
               { { P_TRIGMODE, 2 }, { P_DIV, 2 }, { P_PROB, 0.7f }, { P_CHAOS, 0.95f }, { P_EVMIN, 50 }, { P_EVMAX, 400 },
                 { P_SLMIN, 8 }, { P_SLMAX, 160 }, { P_PITCH, 24 }, { P_CRUSH, 0.4f }, { P_FOLD, 0.25f },
                 { P_FILTER, 0.5f }, { P_FLIP, 0.5f }, { P_FXCHANCE, 0.7f } }) },

        { "Pitch Warp",
          cat (W (0.2f, 1, 1, 0, 0.5f, 0, 0, 0, 0, 0),
               { { P_TRIGMODE, 0 }, { P_DIV, 1 }, { P_PROB, 0.5f }, { P_CHAOS, 0.7f }, { P_EVMIN, 120 }, { P_EVMAX, 600 },
                 { P_PITCH, 24 }, { P_FILTER, 0.2f } }) },

        { "Tape Stop Outro",
          cat (W (0, 0, 0, 1, 0.5f, 0, 0, 0, 0, 0),
               { { P_TRIGMODE, 0 }, { P_DIV, 0 }, { P_PROB, 0.25f }, { P_CHAOS, 0.3f }, { P_EVMIN, 300 }, { P_EVMAX, 900 } }) },

        { "Reverse Riser",
          cat (W (0, 0, 0.6f, 0, 1, 1, 0, 0, 0, 0),
               { { P_TRIGMODE, 0 }, { P_DIV, 0 }, { P_PROB, 0.4f }, { P_CHAOS, 0.4f }, { P_EVMIN, 250 }, { P_EVMAX, 800 },
                 { P_SLMIN, 100 }, { P_SLMAX, 400 }, { P_FILTER, 0.4f } }) },

        { "Bass Wreck",
          cat (W (0.6f, 0.4f, 1, 0.5f, 0.3f, 0, 0, 0, 0.3f, 0),
               { { P_TRIGMODE, 0 }, { P_DIV, 2 }, { P_PROB, 0.5f }, { P_CHAOS, 0.6f }, { P_EVMIN, 60 }, { P_EVMAX, 350 },
                 { P_SLMIN, 30 }, { P_SLMAX, 200 }, { P_PITCH, 12 }, { P_CRUSH, 0.5f }, { P_FOLD, 0.6f }, { P_FXCHANCE, 0.85f } }) },

        { "Granular Shatter",
          cat (W (0, 0, 0, 0, 0, 0.2f, 0.6f, 1, 0.2f, 0.5f),
               { { P_TRIGMODE, 2 }, { P_DIV, 1 }, { P_PROB, 0.5f }, { P_CHAOS, 0.8f }, { P_EVMIN, 150 }, { P_EVMAX, 700 },
                 { P_PITCH, 19 }, { P_FLIP, 0.6f }, { P_FXCHANCE, 0.5f } }) },

        { "Robot Glitch",
          cat (W (1, 0.3f, 0, 0, 0, 0, 0.5f, 0, 0.3f, 0),
               { { P_TRIGMODE, 0 }, { P_DIV, 2 }, { P_PROB, 0.5f }, { P_CHAOS, 0.4f }, { P_EVMIN, 80 }, { P_EVMAX, 300 },
                 { P_SLMIN, 6 }, { P_SLMAX, 60 }, { P_PITCH, 7 }, { P_RING, 0.7f }, { P_CRUSH, 0.4f }, { P_FXCHANCE, 0.9f } }) },

        { "Machine Gun",
          cat (W (1, 0, 0, 0, 0, 0, 0, 0, 0, 0),
               { { P_TRIGMODE, 0 }, { P_DIV, 1 }, { P_PROB, 0.45f }, { P_CHAOS, 0.2f }, { P_EVMIN, 200 }, { P_EVMAX, 500 },
                 { P_SLMIN, 80 }, { P_SLMAX, 160 }, { P_RAMP, 0.9f }, { P_PITCH, 0 } }) },

        { "Sequenced Mayhem (16-step)",
          cat (W (1, 0.6f, 0.6f, 0.2f, 0.3f, 0.5f, 0.1f, 0.4f, 0.5f, 0.6f),
               { { P_TRIGMODE, 3 }, { P_DIV, 2 }, { P_CHAOS, 0.6f }, { P_EVMIN, 60 }, { P_EVMAX, 240 },
                 { P_SLMIN, 10 }, { P_SLMAX, 90 }, { P_PITCH, 14 }, { P_CRUSH, 0.2f }, { P_FXCHANCE, 0.5f },
                 { P_STEP1, 1 }, { P_STEP1 + 1, 0 }, { P_STEP1 + 2, 1 }, { P_STEP1 + 3, 0 },
                 { P_STEP1 + 4, 0 }, { P_STEP1 + 5, 1 }, { P_STEP1 + 6, 0 }, { P_STEP1 + 7, 1 },
                 { P_STEP1 + 8, 1 }, { P_STEP1 + 9, 0 }, { P_STEP1 + 10, 0 }, { P_STEP1 + 11, 1 },
                 { P_STEP1 + 12, 0 }, { P_STEP1 + 13, 1 }, { P_STEP1 + 14, 1 }, { P_STEP1 + 15, 1 } }) },

        { "Lo-Fi Skip (CD skip)",
          cat (W (0.6f, 0, 0, 0, 0, 0, 0, 0, 1, 0.4f),
               { { P_TRIGMODE, 0 }, { P_DIV, 2 }, { P_PROB, 0.4f }, { P_CHAOS, 0.5f }, { P_EVMIN, 100 }, { P_EVMAX, 500 },
                 { P_SLMIN, 20 }, { P_SLMAX, 90 }, { P_CRUSH, 0.3f }, { P_DOWNSAMPLE, 0.3f }, { P_FXCHANCE, 0.7f } }) },
    };
    return list;
}

inline ParamValues presetValues (int presetIndex)
{
    ParamValues v = defaultValues();
    const auto& list = presets();
    if (presetIndex < 0 || presetIndex >= (int) list.size()) return v;
    for (const auto& e : list[(size_t) presetIndex].values)
        v[(size_t) e.index] = e.value;
    return v;
}

} // namespace glitch
