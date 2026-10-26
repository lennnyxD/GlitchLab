// glitch_cli - run the GlitchLab engine on WAV files without a DAW.
//
//   glitch_cli list
//   glitch_cli process in.wav out.wav [--preset N] [--seed S] [--bpm B] [--random] [--set name=value ...]
//   glitch_cli demo outfolder
#include "../Source/GlitchEngine.h"
#include "TestSignals.h"
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace glitch;

static int findParam (const std::string& id)
{
    const auto& sp = paramSpecs();
    for (int i = 0; i < P_COUNT; ++i) if (id == sp[(size_t) i].id) return i;
    return -1;
}

static void renderFile (const std::vector<float>& inL, const std::vector<float>& inR, int sr, const ParamValues& v,
                        uint32_t seed, double bpm, const std::string& outPath)
{
    GlitchEngine e;
    e.prepare ((double) sr);
    e.seed (seed);
    e.setParams (v);
    e.setTransport (bpm, 0.0, true);
    std::vector<float> L = inL, R = inR;
    const size_t block = 512;
    for (size_t pos = 0; pos < L.size(); pos += block)
        e.process (&L[pos], &R[pos], (int) std::min (block, L.size() - pos));
    sig::writeWav16 (outPath, L, R, sr);
    std::printf ("wrote %s (%lld glitch events)\n", outPath.c_str(), (long long) e.eventCount());
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("usage: glitch_cli list | process in.wav out.wav [opts] | demo outfolder\n"); return 1; }
    const std::string cmd = argv[1];

    if (cmd == "list")
    {
        std::printf ("Presets:\n");
        for (size_t i = 0; i < presets().size(); ++i) std::printf ("  %2zu  %s\n", i, presets()[i].name);
        std::printf ("\nParameters (use with --set name=value):\n");
        for (const auto& s : paramSpecs())
            std::printf ("  %-12s %-16s %g .. %g (default %g)%s%s\n", s.id, s.name, s.min, s.max, s.def, s.choices ? "  choices: " : "", s.choices ? s.choices : "");
        return 0;
    }

    if (cmd == "demo")
    {
        const std::string dir = argc > 2 ? argv[2] : ".";
        const int sr = 44100;
        const auto voice = sig::speechLike (sr, 9.0, 7);
        sig::writeWav16 (dir + "/00_dry_voice.wav", voice, voice, sr);
        const int picks[] = { 2, 3, 5, 8, 10 };
        for (int idx : picks)
        {
            std::string name = presets()[(size_t) idx].name;
            for (auto& ch : name) if (! std::isalnum ((unsigned char) ch)) ch = '_';
            char buf[256];
            std::snprintf (buf, sizeof buf, "%s/%02d_%s.wav", dir.c_str(), idx, name.c_str());
            renderFile (voice, voice, sr, presetValues (idx), 1234, 140.0, buf);
        }
        return 0;
    }

    if (cmd == "process" && argc >= 4)
    {
        std::vector<float> L, R; int sr = 0;
        if (! sig::readWav (argv[2], L, R, sr)) { std::printf ("could not read %s (need PCM16/24/32 or float WAV)\n", argv[2]); return 2; }
        ParamValues v = defaultValues();
        uint32_t seed = 1; double bpm = 120.0; bool randomize = false;
        for (int i = 4; i < argc; ++i)
        {
            const std::string a = argv[i];
            if (a == "--preset" && i + 1 < argc) v = presetValues (std::atoi (argv[++i]));
            else if (a == "--seed" && i + 1 < argc) seed = (uint32_t) std::strtoul (argv[++i], nullptr, 10);
            else if (a == "--bpm" && i + 1 < argc) bpm = std::atof (argv[++i]);
            else if (a == "--random") randomize = true;
            else if (a == "--set" && i + 1 < argc)
            {
                const std::string kv = argv[++i];
                const auto eq = kv.find ('=');
                const int idx = eq == std::string::npos ? -1 : findParam (kv.substr (0, eq));
                if (idx < 0) { std::printf ("unknown parameter in '%s' (see: glitch_cli list)\n", kv.c_str()); return 3; }
                v[(size_t) idx] = (float) std::atof (kv.substr (eq + 1).c_str());
            }
        }
        if (randomize) { Rng r; r.seed (seed * 7919u + 1); std::array<bool, P_COUNT> none {}; randomizeParams (v, none, r, 1.f); }
        renderFile (L, R, sr, v, seed, bpm, argv[3]);
        return 0;
    }
    std::printf ("unknown command\n");
    return 1;
}
