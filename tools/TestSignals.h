// Test/demo signals and a tiny WAV reader/writer (no dependencies).
#pragma once
#include "../Source/ParamSpec.h"
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace sig
{

inline void rd (void* dst, size_t sz, size_t n, FILE* f) { if (std::fread (dst, sz, n, f) != n) { /* short read: caller validates sizes */ } }

// A fake "talking voice": vowel-ish syllables with consonant bursts. Good enough to hear glitches on.
inline std::vector<float> speechLike (double sr, double seconds, uint32_t seed)
{
    glitch::Rng rng; rng.seed (seed);
    const size_t N = (size_t) (sr * seconds);
    std::vector<float> out (N, 0.f);
    // F1, F2 pairs for a few vowels
    const float vowels[5][2] = { { 730, 1090 }, { 270, 2290 }, { 300, 870 }, { 530, 1840 }, { 570, 840 } };
    size_t pos = (size_t) (0.1 * sr);
    double f0 = 150.0;
    while (pos + (size_t) (0.4 * sr) < N)
    {
        const size_t len = (size_t) (rng.range (0.11f, 0.26f) * (float) sr);
        const int v = rng.pick (5);
        const double f0a = f0, f0b = f0 * (double) rng.range (0.85f, 1.2f);
        f0 = std::min (240.0, std::max (100.0, f0b));
        double ph = 0.0;
        for (size_t i = 0; i < len && pos + i < N; ++i)
        {
            const double t = (double) i / (double) len;
            const double f = f0a + (f0b - f0a) * t;
            ph += f / sr; ph -= std::floor (ph);
            double s = 0.0;
            const int nh = (int) (4500.0 / f);
            for (int h = 1; h <= nh; ++h)
            {
                const double hf = f * h;
                const double g1 = std::exp (-std::pow (std::log (hf / vowels[v][0]) * 2.2, 2.0));
                const double g2 = std::exp (-std::pow (std::log (hf / vowels[v][1]) * 2.2, 2.0)) * 0.6;
                s += (g1 + g2 + 0.02) / h * std::sin (6.283185307179586 * ph * h);
            }
            const double att = std::min (1.0, (double) i / (0.015 * sr));
            const double rel = std::min (1.0, (double) (len - i) / (0.04 * sr));
            out[pos + i] += (float) (s * att * rel * 0.33);
        }
        // consonant burst at the start of every syllable
        const size_t burst = (size_t) (0.018 * sr);
        float last = 0.f;
        for (size_t i = 0; i < burst && pos + i < N; ++i)
        {
            const float n = rng.range (-1.f, 1.f);
            const float hp = n - last; last = n;
            out[pos + i] += hp * 0.25f * (1.f - (float) i / (float) burst);
        }
        pos += len + (size_t) (rng.range (0.02f, rng.chance (0.3f) ? 0.5f : 0.1f) * (float) sr);
    }
    float peak = 0.f;
    for (float x : out) peak = std::max (peak, std::fabs (x));
    if (peak > 0.f) for (auto& x : out) x *= 0.8f / peak;
    return out;
}

// ----------------------------------------------------------------- WAV ---
inline bool writeWav16 (const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return false;
    const uint32_t n = (uint32_t) L.size();
    const uint16_t ch = 2, bits = 16;
    const uint32_t dataBytes = n * ch * (bits / 8), byteRate = (uint32_t) sr * ch * (bits / 8);
    const uint16_t blockAlign = ch * (bits / 8), fmtTag = 1;
    const uint32_t fmtSize = 16, riffSize = 36 + dataBytes, rate = (uint32_t) sr;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riffSize, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f);
    std::fwrite (&fmtSize, 4, 1, f); std::fwrite (&fmtTag, 2, 1, f); std::fwrite (&ch, 2, 1, f);
    std::fwrite (&rate, 4, 1, f); std::fwrite (&byteRate, 4, 1, f); std::fwrite (&blockAlign, 2, 1, f);
    std::fwrite (&bits, 2, 1, f); std::fwrite ("data", 1, 4, f); std::fwrite (&dataBytes, 4, 1, f);
    for (uint32_t i = 0; i < n; ++i)
        for (int c = 0; c < 2; ++c)
        {
            const float x = std::max (-1.f, std::min (1.f, c == 0 ? L[i] : R[i]));
            const int16_t s = (int16_t) std::lrintf (x * 32767.f);
            std::fwrite (&s, 2, 1, f);
        }
    std::fclose (f);
    return true;
}

// Reads PCM 16/24/32-bit or 32-bit float WAV. Mono is duplicated to stereo.
inline bool readWav (const std::string& path, std::vector<float>& L, std::vector<float>& R, int& sr)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (! f) return false;
    char id[4]; uint32_t sz;
    if (std::fread (id, 1, 4, f) != 4 || std::memcmp (id, "RIFF", 4) != 0) { std::fclose (f); return false; }
    rd (&sz, 4, 1, f);
    if (std::fread (id, 1, 4, f) != 4 || std::memcmp (id, "WAVE", 4) != 0) { std::fclose (f); return false; }
    uint16_t fmtTag = 1, ch = 1, bits = 16; uint32_t rate = 44100;
    std::vector<unsigned char> data;
    while (std::fread (id, 1, 4, f) == 4 && std::fread (&sz, 4, 1, f) == 1)
    {
        if (std::memcmp (id, "fmt ", 4) == 0)
        {
            uint16_t ba; uint32_t br;
            rd (&fmtTag, 2, 1, f); rd (&ch, 2, 1, f); rd (&rate, 4, 1, f);
            rd (&br, 4, 1, f); rd (&ba, 2, 1, f); rd (&bits, 2, 1, f);
            if (sz > 16) std::fseek (f, (long) (sz - 16), SEEK_CUR);
        }
        else if (std::memcmp (id, "data", 4) == 0)
        {
            data.resize (sz);
            const size_t got = std::fread (data.data(), 1, sz, f);
            data.resize (got);
            break;
        }
        else std::fseek (f, (long) sz + (long) (sz & 1), SEEK_CUR);
    }
    std::fclose (f);
    if (ch < 1 || data.empty()) return false;
    const int bytes = bits / 8;
    const size_t frames = data.size() / (size_t) (bytes * ch);
    L.assign (frames, 0.f); R.assign (frames, 0.f);
    auto sample = [&] (size_t frame, int c) -> float
    {
        const unsigned char* p = &data[(frame * ch + (size_t) c) * (size_t) bytes];
        if (fmtTag == 3 && bits == 32) { float v; std::memcpy (&v, p, 4); return v; }
        if (bits == 16) return (float) (int16_t) (p[0] | (p[1] << 8)) / 32768.f;
        if (bits == 24) { int32_t v = (p[0] << 8) | (p[1] << 16) | ((uint32_t) p[2] << 24); return (float) (v >> 8) / 8388608.f; }
        if (bits == 32) { int32_t v; std::memcpy (&v, p, 4); return (float) ((double) v / 2147483648.0); }
        return 0.f;
    };
    for (size_t i = 0; i < frames; ++i)
    {
        L[i] = sample (i, 0);
        R[i] = ch > 1 ? sample (i, 1) : L[i];
    }
    sr = (int) rate;
    return true;
}

} // namespace sig
