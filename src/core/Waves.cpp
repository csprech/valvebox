#include "Waves.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace valvebox
{
namespace
{
    constexpr double kSr = WaveLibrary::kRate;
    constexpr double kTwoPi = 6.283185307179586;

    struct Rng
    {
        uint32_t s;
        explicit Rng (uint32_t seed) : s (seed * 2654435761u + 1u) {}
        float next() // -1..1
        {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            return (float) ((s & 0xffffff) / 8388607.5 - 1.0);
        }
    };

    struct OnePole
    {
        float z = 0.0f;
        float lp (float x, double hz)
        {
            const float a = (float) std::exp (-kTwoPi * hz / kSr);
            z = x + (z - x) * a;
            return z;
        }
        float hp (float x, double hz) { return x - lp (x, hz); }
    };

    /** State-variable filter (TPT), used for colouring noise and formants. */
    struct Svf
    {
        float ic1 = 0, ic2 = 0;
        float process (float x, double hz, double q, int mode) // 0 lp 1 bp 2 hp
        {
            const double g = std::tan (3.141592653589793 * std::min (hz, kSr * 0.45) / kSr);
            const double k = 1.0 / std::max (0.5, q);
            const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = (float) (2 * v1 - ic1);
            ic2 = (float) (2 * v2 - ic2);
            return (float) (mode == 0 ? v2 : mode == 1 ? v1 : x - k * v1 - v2);
        }
    };

    std::vector<float> render (double seconds, const std::function<float (double t, int i)>& f)
    {
        const int n = std::max (16, (int) (seconds * kSr));
        std::vector<float> d ((size_t) n);
        for (int i = 0; i < n; ++i)
            d[(size_t) i] = f (i / kSr, i);
        return d;
    }

    void normalize (std::vector<float>& d, float peak = 0.9f)
    {
        float m = 1.0e-9f;
        for (auto x : d)
            m = std::max (m, std::abs (x));
        const float g = peak / m;
        for (auto& x : d)
            x *= g;
    }

    /** Fade the last few ms to zero and remove leading silence: clean one-shots. */
    void finish (std::vector<float>& d, double fadeMs = 8.0)
    {
        const int fade = std::min ((int) d.size(), (int) (fadeMs * 0.001 * kSr));
        for (int i = 0; i < fade; ++i)
            d[d.size() - (size_t) fade + (size_t) i] *= 1.0f - (float) i / (float) fade;
        normalize (d);
    }

    void bitcrush (std::vector<float>& d, int bits, double holdHz)
    {
        const float q = (float) (1 << (bits - 1));
        const int hold = std::max (1, (int) (kSr / holdHz));
        float last = 0;
        for (size_t i = 0; i < d.size(); ++i)
        {
            if (i % (size_t) hold == 0)
                last = std::round (d[i] * q) / q;
            d[i] = last;
        }
    }

    float env (double t, double decay) { return (float) std::exp (-t / std::max (1.0e-4, decay)); }
    float attackEnv (double t, double attack) { return t < attack ? (float) (t / attack) : 1.0f; }

    // 808-style metallic source: six square waves at inharmonic frequencies
    float metal (double t, double scale, const double* ratios, int n)
    {
        float s = 0;
        for (int k = 0; k < n; ++k)
            s += std::fmod (t * 205.3 * scale * ratios[k], 1.0) < 0.5 ? 1.0f : -1.0f;
        return s / (float) n;
    }
    const double kMetalRatios[6] = { 1.0, 1.4827, 1.8003, 2.5461, 2.6303, 3.8968 };

    // ------------------------------------------------------------ drums

    Wave kick (int v)
    {
        struct G { const char* word; double f0, sweep, pt, decay, click, drive, attack; int bits; };
        static const G groups[11] = {
            { "Deep", 45, 3.0, 0.020, 0.50, 0.10, 1.0, 0.0005, 0 }, { "Rnd", 55, 2.5, 0.015, 0.35, 0.00, 1.0, 0.0005, 0 },
            { "Pnch", 58, 4.0, 0.008, 0.25, 0.40, 1.6, 0.0003, 0 }, { "Clik", 52, 3.0, 0.010, 0.30, 0.90, 1.2, 0.0002, 0 },
            { "Tght", 62, 3.0, 0.006, 0.15, 0.30, 1.3, 0.0003, 0 }, { "Boom", 42, 2.0, 0.030, 1.20, 0.05, 1.0, 0.0010, 0 },
            { "Soft", 50, 1.8, 0.020, 0.30, 0.00, 1.0, 0.0030, 0 }, { "Dist", 50, 3.5, 0.012, 0.40, 0.30, 4.5, 0.0003, 0 },
            { "LoFi", 52, 3.0, 0.010, 0.35, 0.30, 1.4, 0.0003, 6 }, { "Sub", 38, 1.5, 0.040, 1.60, 0.00, 1.0, 0.0020, 0 },
            { "Big", 48, 4.0, 0.012, 0.90, 0.50, 2.0, 0.0003, 0 } };
        const int gi = std::min (10, v / 4), k = v % 4;
        const auto& g = groups[gi];
        static const double fMul[4] = { 1.0, 0.93, 1.08, 0.86 }, dMul[4] = { 1.0, 1.3, 0.8, 1.6 };
        const double f0 = g.f0 * fMul[k], decay = g.decay * dMul[k];
        Rng rng ((uint32_t) v + 11);
        double ph = 0;
        OnePole clickHp;
        auto d = render (std::min (2.8, decay * 5.0 + 0.05), [&] (double t, int) {
            const double f = f0 * (1.0 + (g.sweep - 1.0) * std::exp (-t / g.pt));
            ph += kTwoPi * f / kSr;
            const float body = (float) std::sin (ph) * env (t, decay) * attackEnv (t, g.attack);
            const float click = clickHp.hp (rng.next(), 1500.0) * env (t, 0.0025) * (float) g.click;
            const float x = body + click;
            return g.drive > 1.0 ? (float) (std::tanh (g.drive * x) / std::tanh (g.drive)) : x;
        });
        if (g.bits > 0)
            bitcrush (d, g.bits, 12000.0);
        finish (d);
        return { std::move (d), -1, 60.0, "BD-" + juce::String (g.word) + (gi == 10 ? "" : juce::String (k + 1)) };
    }

    Wave snare (int v)
    {
        struct G { const char* word; double body, noiseDecay, bodyDecay, noiseMix, hp, drive; int bits; double attack; };
        static const G groups[10] = {
            { "Tght", 220, 0.12, 0.06, 0.60, 2500, 1.0, 0, 0.0005 }, { "Fat", 180, 0.25, 0.10, 0.50, 1200, 1.2, 0, 0.0008 },
            { "Crck", 240, 0.15, 0.05, 0.70, 4000, 2.2, 0, 0.0003 }, { "Snap", 205, 0.08, 0.04, 0.65, 3000, 1.4, 0, 0.0002 },
            { "Room", 190, 0.38, 0.12, 0.60, 1500, 1.0, 0, 0.0008 }, { "Brsh", 200, 0.30, 0.02, 0.95, 2000, 1.0, 0, 0.0080 },
            { "Picl", 330, 0.13, 0.07, 0.55, 3500, 1.3, 0, 0.0003 }, { "Ring", 210, 0.20, 0.25, 0.45, 2000, 1.1, 0, 0.0005 },
            { "LoFi", 200, 0.16, 0.07, 0.60, 2000, 1.6, 5, 0.0004 }, { "Syn", 185, 0.18, 0.08, 0.55, 1800, 1.2, 0, 0.0003 } };
        const int gi = v / 4, k = v % 4;
        const auto& g = groups[gi];
        static const double fMul[4] = { 1.0, 1.1, 0.9, 1.2 }, dMul[4] = { 1.0, 0.8, 1.3, 1.1 };
        const double f = g.body * fMul[k], nd = g.noiseDecay * dMul[k];
        Rng rng ((uint32_t) v + 101);
        OnePole hp, lp;
        double ph1 = 0, ph2 = 0;
        auto d = render (std::min (1.6, nd * 6.0 + 0.05), [&] (double t, int) {
            const double sweep = gi == 9 ? 1.0 + 0.6 * std::exp (-t / 0.01) : 1.0 + 0.15 * std::exp (-t / 0.004);
            ph1 += kTwoPi * f * sweep / kSr;
            ph2 += kTwoPi * f * 1.62 * sweep / kSr;
            float body = (float) (std::sin (ph1) + 0.5 * std::sin (ph2)) * env (t, g.bodyDecay * dMul[k]);
            if (gi == 7) // ringing shell: extra inharmonic partials
                body += 0.4f * (float) (std::sin (ph1 * 2.31) + std::sin (ph1 * 3.07)) * env (t, 0.3);
            float noise = lp.lp (hp.hp (rng.next(), g.hp), 9000.0) * env (t, nd) * attackEnv (t, g.attack);
            const float x = (float) ((1.0 - g.noiseMix) * body + g.noiseMix * noise * 1.6);
            return g.drive > 1.0 ? (float) (std::tanh (g.drive * x) / std::tanh (g.drive)) : x;
        });
        if (g.bits > 0)
            bitcrush (d, g.bits, 11000.0);
        finish (d);
        return { std::move (d), -1, 60.0, "SD-" + juce::String (g.word) + juce::String (k + 1) };
    }

    Wave rim (int v)
    {
        static const double f1[6] = { 1700, 1500, 1900, 1650, 2100, 1400 }, dec[6] = { 0.020, 0.030, 0.015, 0.060, 0.025, 0.045 };
        static const char* names[6] = { "Rim-Dry1", "Rim-Dry2", "Rim-Hi", "Rim-Ambi", "Rim-Tick", "Rim-Wood" };
        Rng rng ((uint32_t) v + 201);
        OnePole hp;
        double a = 0, b = 0, c = 0;
        auto d = render (dec[v] * 8 + 0.02, [&] (double t, int) {
            a += kTwoPi * f1[v] / kSr;
            b += kTwoPi * f1[v] * 1.43 / kSr;
            c += kTwoPi * f1[v] * 2.1 / kSr;
            const float tone = (float) (std::sin (a) + 0.6 * std::sin (b) + 0.3 * std::sin (c)) * env (t, dec[v]);
            const float click = hp.hp (rng.next(), 3000) * env (t, 0.002);
            const float tail = v == 3 ? hp.hp (rng.next(), 2000) * env (t, 0.12) * 0.2f : 0.0f;
            return tone + click + tail;
        });
        finish (d);
        return { std::move (d), -1, 60.0, names[v] };
    }

    Wave clap (int v)
    {
        static const int bursts[7] = { 3, 4, 4, 5, 3, 4, 3 };
        static const double spacing[7] = { 0.009, 0.010, 0.007, 0.011, 0.012, 0.008, 0.010 }, tail[7] = { 0.18, 0.25, 0.12, 0.35, 0.10, 0.20, 0.40 },
                            centre[7] = { 1100, 1000, 1400, 900, 1600, 1200, 1000 };
        static const char* names[7] = { "Clap-1", "Clap-2", "Clap-Tgt", "Clap-Big", "Clap-Hi", "Clap-R&B", "Clap-Rm" };
        Rng rng ((uint32_t) v + 301);
        Svf bp;
        auto d = render (tail[v] * 6 + 0.06, [&] (double t, int) {
            float e = 0;
            for (int b = 0; b < bursts[v]; ++b)
            {
                const double tb = t - b * spacing[v];
                if (tb >= 0)
                    e = std::max (e, b == bursts[v] - 1 ? env (tb, tail[v]) : env (tb, 0.006));
            }
            return bp.process (rng.next(), centre[v], 1.3, 1) * e * 2.0f;
        });
        finish (d);
        return { std::move (d), -1, 60.0, names[v] };
    }

    Wave hihat (int v)
    {
        // 13 families, each a closed (even) and an open (odd) hat
        const int fam = v / 2;
        const bool open = (v % 2) == 1;
        struct F { const char* word; double scale, hp, metalMix, closedDecay, openDecay; int bits; };
        static const F fams[13] = {
            { "Mtl", 1.00, 7000, 0.80, 0.045, 0.50, 0 }, { "Brt", 1.25, 9000, 0.60, 0.035, 0.40, 0 }, { "Drk", 0.80, 5000, 0.70, 0.060, 0.60, 0 },
            { "Nse", 1.00, 8000, 0.00, 0.040, 0.45, 0 }, { "Tgt", 1.10, 8500, 0.60, 0.025, 0.30, 0 }, { "Crs", 1.40, 9500, 0.50, 0.030, 0.35, 0 },
            { "Old", 0.95, 6000, 0.75, 0.050, 0.55, 6 }, { "Syn", 1.60, 7500, 0.90, 0.040, 0.45, 0 }, { "Thn", 1.80, 10000, 0.40, 0.030, 0.30, 0 },
            { "Grt", 1.05, 6500, 0.85, 0.055, 0.65, 0 }, { "Sft", 0.90, 6000, 0.30, 0.050, 0.40, 0 }, { "Trs", 1.20, 7000, 0.95, 0.070, 0.80, 0 },
            { "Shk", 1.00, 9000, 0.10, 0.060, 0.25, 0 } };
        const auto& f = fams[fam];
        Rng rng ((uint32_t) v + 401);
        OnePole hp1, hp2, lp;
        const double decay = open ? f.openDecay : f.closedDecay;
        auto d = render (decay * 6 + 0.03, [&] (double t, int) {
            const float m = metal (t, f.scale, kMetalRatios, 6);
            const float n = rng.next();
            const float src = (float) (f.metalMix * m + (1.0 - f.metalMix) * n);
            const float hpd = hp2.hp (hp1.hp (src, f.hp), f.hp * 0.7);
            const float att = fam == 12 ? attackEnv (t, 0.008) : 1.0f;
            return lp.lp (hpd, 16000) * env (t, decay) * att;
        });
        if (f.bits > 0)
            bitcrush (d, f.bits, 14000.0);
        finish (d);
        return { std::move (d), -1, 60.0, "HH-" + juce::String (f.word) + (open ? "O" : "C") };
    }

    Wave ride (int v)
    {
        static const double scale[6] = { 1.8, 2.0, 1.6, 2.2, 1.9, 1.5 }, decay[6] = { 1.8, 2.2, 1.6, 2.4, 1.5, 2.0 }, bell[6] = { 0.3, 0.2, 0.6, 0.1, 0.5, 0.25 };
        static const char* names[6] = { "Ride-1", "Ride-2", "Ride-Bel", "Ride-Edg", "Ride-Jaz", "Ride-Drk" };
        Rng rng ((uint32_t) v + 501);
        OnePole hp;
        double ph = 0;
        auto d = render (decay[v] * 2.2, [&] (double t, int) {
            const float m = metal (t, scale[v], kMetalRatios, 6);
            ph += kTwoPi * 2400 * scale[v] / 2.0 / kSr;
            const float b = (float) std::sin (ph) * env (t, 0.6) * (float) bell[v];
            const float n = rng.next() * 0.25f * env (t, 0.05);
            return (hp.hp (m * 0.6f + n, 4000) + b) * env (t, decay[v]);
        });
        finish (d, 40);
        return { std::move (d), -1, 60.0, names[v] };
    }

    Wave crash (int v)
    {
        static const double decay[4] = { 2.4, 2.8, 2.0, 0.8 }, bright[4] = { 6000, 5000, 7000, 8000 };
        static const char* names[4] = { "Crash-1", "Crash-2", "Crash-Dk", "Splash" };
        Rng rng ((uint32_t) v + 601);
        OnePole hp, lp;
        auto d = render (decay[v] * 2.0, [&] (double t, int) {
            const float m = metal (t, 2.6 + v * 0.3, kMetalRatios, 6);
            const float n = rng.next();
            const float x = hp.hp (0.5f * m + 0.8f * n, bright[v] * 0.6);
            return lp.lp (x, 14000) * env (t, decay[v]) * attackEnv (t, 0.002);
        });
        finish (d, 60);
        return { std::move (d), -1, 60.0, names[v] };
    }

    Wave tom (int v)
    {
        static const double f[7] = { 220, 165, 125, 90, 260, 110, 180 }, dec[7] = { 0.30, 0.38, 0.45, 0.6, 0.25, 0.5, 0.35 };
        static const bool synth[7] = { false, false, false, false, true, true, false };
        static const char* names[7] = { "Tom-Hi", "Tom-Mid", "Tom-Low", "Tom-Flr", "Tom-SynH", "Tom-SynL", "Tom-Jazz" };
        Rng rng ((uint32_t) v + 701);
        OnePole lp;
        double ph = 0;
        auto d = render (dec[v] * 5, [&] (double t, int) {
            const double fr = f[v] * (1.0 + (synth[v] ? 0.8 : 0.25) * std::exp (-t / (synth[v] ? 0.06 : 0.03)));
            ph += kTwoPi * fr / kSr;
            const float body = (float) (std::sin (ph) + (synth[v] ? 0.0 : 0.25 * std::sin (ph * 1.5))) * env (t, dec[v]);
            const float hit = lp.lp (rng.next(), 3000) * env (t, 0.01) * 0.5f;
            return body + hit;
        });
        finish (d);
        return { std::move (d), -1, 60.0, names[v] };
    }

    enum class PercModel { Membrane, Slap, Mute, Wood, Metal, Bell, Triangle, Jingle, Shaker, Scrape, Whistle, Bend, Taiko, Junk, Sleigh };
    struct Perc { const char* name; PercModel model; double f, decay; };
    const Perc kPercs[54] = {
        { "Bongo-Hi", PercModel::Membrane, 420, 0.15 }, { "Bongo-Sl", PercModel::Slap, 400, 0.08 },  { "Bongo-L1", PercModel::Membrane, 300, 0.20 },
        { "Bongo-L2", PercModel::Membrane, 270, 0.22 }, { "Conga-H1", PercModel::Membrane, 330, 0.25 }, { "Conga-H2", PercModel::Membrane, 350, 0.22 },
        { "Conga-HM", PercModel::Mute, 340, 0.05 },     { "Conga-L1", PercModel::Membrane, 220, 0.30 }, { "Conga-L2", PercModel::Membrane, 200, 0.34 },
        { "Conga-LM", PercModel::Mute, 220, 0.05 },     { "Conga-SH", PercModel::Slap, 360, 0.10 },     { "Conga-SL", PercModel::Slap, 230, 0.12 },
        { "Timb-Hi1", PercModel::Membrane, 520, 0.25 }, { "Timb-Hi2", PercModel::Membrane, 560, 0.20 }, { "Timb-Lo1", PercModel::Membrane, 380, 0.30 },
        { "Timb-Lo2", PercModel::Membrane, 350, 0.32 }, { "Timb-Rim", PercModel::Wood, 2200, 0.05 },    { "Claves", PercModel::Wood, 2500, 0.06 },
        { "Cowbell", PercModel::Bell, 560, 0.25 },      { "BellChCh", PercModel::Bell, 680, 0.18 },     { "BellMamb", PercModel::Bell, 820, 0.15 },
        { "Agogo", PercModel::Bell, 980, 0.35 },        { "Triangle", PercModel::Triangle, 4800, 1.2 }, { "Tambour", PercModel::Jingle, 6000, 0.25 },
        { "Junk-1", PercModel::Junk, 900, 0.20 },       { "Junk-2", PercModel::Junk, 1300, 0.15 },      { "Sleigh", PercModel::Sleigh, 7000, 0.6 },
        { "Shaker-1", PercModel::Shaker, 7000, 0.07 },  { "Shaker-2", PercModel::Shaker, 5500, 0.10 },  { "Cabasa-1", PercModel::Shaker, 8000, 0.05 },
        { "Cabasa-2", PercModel::Shaker, 9000, 0.08 },  { "Cabasa-3", PercModel::Shaker, 6500, 0.12 },  { "Guiro-S", PercModel::Scrape, 3000, 0.12 },
        { "Guiro-L", PercModel::Scrape, 2800, 0.35 },   { "WoodBl-H", PercModel::Wood, 1500, 0.07 },    { "WoodBl-L", PercModel::Wood, 1050, 0.08 },
        { "Whistle", PercModel::Whistle, 2100, 0.45 },  { "Baya-Bnd", PercModel::Bend, 110, 0.6 },      { "Baya-Mt1", PercModel::Mute, 120, 0.07 },
        { "Baya-Mt2", PercModel::Mute, 135, 0.06 },     { "Tabla-Na", PercModel::Membrane, 640, 0.35 }, { "Tabla-Tn", PercModel::Membrane, 700, 0.5 },
        { "Tabla-M1", PercModel::Mute, 650, 0.04 },     { "Tabla-M2", PercModel::Mute, 600, 0.05 },     { "Djembe1a", PercModel::Membrane, 180, 0.30 },
        { "Djembe1b", PercModel::Slap, 450, 0.10 },     { "Djembe1c", PercModel::Membrane, 280, 0.18 }, { "Djembe2a", PercModel::Membrane, 160, 0.34 },
        { "Djembe2b", PercModel::Slap, 420, 0.12 },     { "Djembe2c", PercModel::Membrane, 260, 0.20 }, { "Udu", PercModel::Bend, 180, 0.5 },
        { "Taiko-Op", PercModel::Taiko, 70, 0.9 },      { "Taiko-Rm", PercModel::Wood, 900, 0.06 },     { "Tsuzumi", PercModel::Bend, 330, 0.35 },
    };

    Wave percussion (int v)
    {
        const auto& p = kPercs[v];
        Rng rng ((uint32_t) v + 801);
        OnePole hp, lp, hp2;
        Svf bp, bp2;
        double ph = 0, ph2 = 0;
        const double len = std::min (2.5, p.decay * 5.0 + 0.04);
        auto d = render (len, [&] (double t, int i) -> float {
            switch (p.model)
            {
                case PercModel::Membrane: case PercModel::Slap: case PercModel::Mute: case PercModel::Taiko:
                {
                    const double fr = p.f * (1.0 + 0.12 * std::exp (-t / 0.02));
                    ph += kTwoPi * fr / kSr;
                    ph2 += kTwoPi * fr * 1.59 / kSr;
                    float body = (float) (std::sin (ph) + 0.3 * std::sin (ph2)) * env (t, p.decay);
                    float noise = lp.lp (rng.next(), p.model == PercModel::Slap ? 6000 : 2500) * env (t, p.model == PercModel::Slap ? 0.02 : 0.006);
                    if (p.model == PercModel::Taiko)
                        noise *= 2.0f;
                    return body + noise * (p.model == PercModel::Slap ? 1.2f : 0.4f);
                }
                case PercModel::Wood:
                    ph += kTwoPi * p.f / kSr;
                    ph2 += kTwoPi * p.f * 2.27 / kSr;
                    return (float) (std::sin (ph) + 0.4 * std::sin (ph2)) * env (t, p.decay) + hp.hp (rng.next(), 4000) * env (t, 0.0015);
                case PercModel::Metal: case PercModel::Junk:
                {
                    const double r[6] = { 1.0, 1.71, 2.39, 3.03, 3.97, 4.6 };
                    float s = 0;
                    for (double x : r)
                        s += (float) std::sin (kTwoPi * p.f * x * t);
                    const float n = p.model == PercModel::Junk ? hp.hp (rng.next(), 3000) * env (t, 0.05) : 0.0f;
                    return (s / 6.0f + n) * env (t, p.decay);
                }
                case PercModel::Bell:
                {
                    const float a = std::fmod (t * p.f, 1.0) < 0.5 ? 1.0f : -1.0f;
                    const float b = std::fmod (t * p.f * 1.48, 1.0) < 0.5 ? 1.0f : -1.0f;
                    return bp.process ((a + b) * 0.5f, p.f * 1.3, 3.0, 1) * env (t, p.decay) * 2.0f;
                }
                case PercModel::Triangle:
                    return (float) (std::sin (kTwoPi * p.f * t) + 0.5 * std::sin (kTwoPi * p.f * 2.76 * t) + 0.3 * std::sin (kTwoPi * p.f * 5.4 * t))
                           * env (t, p.decay) / 1.8f;
                case PercModel::Jingle: case PercModel::Sleigh:
                {
                    // bursts of metallic noise: a hit (tambourine) or a stream of little jingles (sleigh bells)
                    float e = env (t, p.decay);
                    if (p.model == PercModel::Sleigh)
                        e = (0.5f + 0.5f * (float) std::sin (kTwoPi * 22.0 * t)) * env (t, p.decay) * attackEnv (t, 0.05);
                    const float m = metal (t, 4.0, kMetalRatios, 6);
                    return bp.process (0.5f * m + rng.next(), p.f, 2.0, 1) * e * 2.0f;
                }
                case PercModel::Shaker:
                    return hp2.hp (hp.hp (rng.next(), p.f * 0.7), p.f * 0.5) * env (t, p.decay) * attackEnv (t, p.decay * 0.5);
                case PercModel::Scrape:
                {
                    // a train of clicks (a stick dragged over ridges)
                    const double rate = 60.0 + 40.0 * t / len;
                    const float click = std::fmod (t * rate, 1.0) < 0.15 ? 1.0f : 0.0f;
                    return bp.process (rng.next() * click, p.f, 2.0, 1) * attackEnv (t, 0.02) * (t < len * 0.8 ? 1.0f : env (t - len * 0.8, 0.02)) * 2.0f;
                }
                case PercModel::Whistle:
                {
                    ph += kTwoPi * p.f * (1.0 + 0.02 * std::sin (kTwoPi * 18.0 * t)) / kSr;
                    return (float) std::sin (ph) * attackEnv (t, 0.02) * (t < p.decay ? 1.0f : env (t - p.decay, 0.05))
                           + bp2.process (rng.next(), p.f, 4.0, 1) * 0.1f;
                }
                case PercModel::Bend:
                {
                    // the pitch rises after the hit (baya / udu / tsuzumi)
                    const double fr = p.f * (1.0 + 0.6 * (1.0 - std::exp (-t / 0.12)));
                    ph += kTwoPi * fr / kSr;
                    return (float) std::sin (ph) * env (t, p.decay) + lp.lp (rng.next(), 2000) * env (t, 0.008) * 0.4f;
                }
            }
            juce::ignoreUnused (i);
            return 0.0f;
        });
        finish (d);
        return { std::move (d), -1, 60.0, p.name };
    }

    Wave synthPerc (int v)
    {
        static const char* names[9] = { "Zap-1", "Zap-2", "Blip", "Laser", "NoisSwp", "SynPerc1", "Ping", "BitPerc", "FMPerc" };
        Rng rng ((uint32_t) v + 901);
        Svf f;
        double ph = 0, ph2 = 0;
        const double len[9] = { 0.25, 0.3, 0.12, 0.5, 0.6, 0.35, 0.8, 0.2, 0.5 };
        auto d = render (len[v], [&] (double t, int) -> float {
            switch (v)
            {
                case 0: ph += kTwoPi * (60 + 3000 * std::exp (-t / 0.03)) / kSr; return (float) std::sin (ph) * env (t, 0.08);
                case 1: ph += kTwoPi * (80 + 2000 * std::exp (-t / 0.05)) / kSr; return (std::sin (ph) > 0 ? 0.6f : -0.6f) * env (t, 0.1);
                case 2: ph += kTwoPi * 1200 / kSr; return (float) std::sin (ph) * env (t, 0.03);
                case 3: ph2 += kTwoPi * 330 / kSr; ph += kTwoPi * (900 + 1200 * std::exp (-t / 0.15)) / kSr;
                        return (float) std::sin (ph + 2.0 * std::sin (ph2)) * env (t, 0.2);
                case 4: return f.process (rng.next(), 300 + 6000 * (t / len[v]), 4.0, 1) * env (t, 0.25) * attackEnv (t, 0.05) * 2.0f;
                case 5: ph += kTwoPi * (120 + 200 * std::exp (-t / 0.04)) / kSr; return (float) std::sin (ph) * env (t, 0.15);
                case 6: ph += kTwoPi * 2500 / kSr; return (float) std::sin (ph) * env (t, 0.3);
                case 7: { const float n = rng.next() * env (t, 0.06); return std::round (n * 4.0f) / 4.0f; }
                default: ph2 += kTwoPi * 523 * 3.5 / kSr; ph += kTwoPi * 523 / kSr;
                         return (float) std::sin (ph + 4.0 * env (t, 0.05) * std::sin (ph2)) * env (t, 0.2);
            }
        });
        finish (d);
        return { std::move (d), -1, 60.0, names[v] };
    }

    Wave scratch (int v)
    {
        // a recorded-sounding source played back and forth by a 'hand'
        Rng rng ((uint32_t) v + 999);
        std::vector<float> src ((size_t) kSr);
        OnePole lp;
        for (size_t i = 0; i < src.size(); ++i)
        {
            const double t = i / kSr;
            src[i] = (float) (0.6 * std::sin (kTwoPi * 220 * t + 3 * std::sin (kTwoPi * 3 * t))) + lp.lp (rng.next(), 2500) * 0.6f;
        }
        static const double rates[3] = { 6.0, 9.0, 4.0 };
        double pos = kSr * 0.3;
        auto d = render (0.45, [&] (double t, int) {
            const double speed = 2.2 * std::sin (kTwoPi * rates[v] * t) * (v == 1 ? std::abs (std::sin (kTwoPi * 1.5 * t)) : 1.0);
            pos = std::clamp (pos + speed, 0.0, (double) src.size() - 2);
            const int i = (int) pos;
            const float frac = (float) (pos - i);
            return (src[(size_t) i] * (1 - frac) + src[(size_t) i + 1] * frac) * (float) std::min (1.0, std::abs (speed));
        });
        finish (d);
        return { std::move (d), -1, 60.0, "Scratch" + juce::String (v + 1) };
    }

    // ------------------------------------------------------------ PCM (instrument and synth) waves

    constexpr double kRootHz = 261.6255653005986; // C4

    /** Harmonic additive tone at the root: amp(n, t) per partial. Partials above 9 kHz are left out,
        so the wave still sounds clean played two octaves up. */
    std::vector<float> additive (double seconds, const std::function<double (int n, double t)>& amp, double inharm = 0.0,
                                 double vibrato = 0.0)
    {
        const int maxN = std::max (1, (int) (9000.0 / kRootHz));
        std::vector<double> ph ((size_t) maxN + 1, 0.0);
        return render (seconds, [&] (double t, int) {
            double s = 0;
            const double vib = 1.0 + vibrato * std::sin (kTwoPi * 5.2 * t) * std::min (1.0, t * 2.0);
            for (int n = 1; n <= maxN; ++n)
            {
                const double a = amp (n, t);
                if (a < 1.0e-5)
                    continue;
                const double f = kRootHz * n * std::sqrt (1.0 + inharm * n * n) * vib;
                if (f > 10000.0)
                    break;
                ph[(size_t) n] += kTwoPi * f / kSr;
                s += a * std::sin (ph[(size_t) n]);
            }
            return (float) s;
        });
    }

    /** Make a sustained wave loop: crossfade the end into a loop of `loopSeconds`. */
    Wave looped (std::vector<float> d, double loopSeconds, const juce::String& name)
    {
        normalize (d);
        const int n = (int) d.size();
        // a whole number of root periods, so a steady tone loops seamlessly
        const double period = kSr / kRootHz;
        const int loopLen = std::max ((int) period, (int) (std::round (loopSeconds * kSr / period) * period));
        const int loopStart = std::max (1, n - loopLen);
        const int x = std::min (loopStart, std::min (2400, loopLen / 2));
        for (int j = 0; j < x; ++j)
        {
            const float a = (float) j / (float) x;
            d[(size_t) (n - x + j)] = d[(size_t) (n - x + j)] * (1.0f - a) + d[(size_t) (loopStart - x + j)] * a;
        }
        return { std::move (d), loopStart, 60.0, name };
    }

    Wave oneShot (std::vector<float> d, const juce::String& name)
    {
        finish (d, 30);
        return { std::move (d), -1, 60.0, name };
    }

    std::vector<float> karplus (double seconds, double damping, double brightness, uint32_t seed, double buzz = 0.0)
    {
        const int period = (int) std::round (kSr / kRootHz);
        std::vector<float> line ((size_t) period);
        Rng rng (seed);
        OnePole shape;
        for (auto& x : line)
            x = shape.lp (rng.next(), 800 + 9000 * brightness);
        int idx = 0;
        float prev = 0;
        return render (seconds, [&] (double, int) {
            const float cur = line[(size_t) idx];
            // Karplus-Strong: the averaging filter darkens the string; brightness keeps more of the raw sample
            float next = (float) (damping * (brightness * cur + (1.0 - brightness) * 0.5 * (cur + prev)));
            if (buzz > 0.0 && next > 0.2f)
                next = 0.2f + (next - 0.2f) * (float) (1.0 - buzz);
            prev = cur;
            line[(size_t) idx] = next;
            idx = (idx + 1) % period;
            return cur;
        });
    }

    std::vector<float> formantVoice (double seconds, const double* formants, const double* gains, int nf, double detune, uint32_t seed,
                                     double breath = 0.0)
    {
        double ph = 0;
        Rng rng (seed);
        std::array<Svf, 5> f {};
        return render (seconds, [&] (double t, int) {
            ph += (1.0 + detune * std::sin (kTwoPi * 0.3 * t)) * kRootHz / kSr;
            ph -= std::floor (ph);
            const float src = (float) (2.0 * ph - 1.0) + rng.next() * (float) breath;
            float s = 0;
            for (int k = 0; k < nf; ++k)
                s += f[(size_t) k].process (src, formants[k], 6.0 + k * 2.0, 1) * (float) gains[k];
            return s;
        });
    }

    std::vector<float> sawStack (double seconds, int voices, double cents, double cutoff, double octaveMix = 0.0, double fifthMix = 0.0,
                                 bool square = false)
    {
        std::vector<double> ph ((size_t) voices + 2, 0.0);
        Svf lp;
        return render (seconds, [&] (double, int) {
            double s = 0;
            for (int k = 0; k < voices; ++k)
            {
                const double det = voices > 1 ? (k / (double) (voices - 1) - 0.5) * 2.0 * cents : 0.0;
                ph[(size_t) k] += kRootHz * std::pow (2.0, det / 1200.0) / kSr;
                ph[(size_t) k] -= std::floor (ph[(size_t) k]);
                s += square ? (ph[(size_t) k] < 0.5 ? 1.0 : -1.0) : 2.0 * ph[(size_t) k] - 1.0;
            }
            s /= voices;
            ph[(size_t) voices] += kRootHz * 0.5 / kSr;
            ph[(size_t) voices] -= std::floor (ph[(size_t) voices]);
            ph[(size_t) voices + 1] += kRootHz * 1.4983 / kSr;
            ph[(size_t) voices + 1] -= std::floor (ph[(size_t) voices + 1]);
            auto shape = [square] (double p) { return square ? (p < 0.5 ? 1.0 : -1.0) : 2.0 * p - 1.0; };
            s += octaveMix * shape (ph[(size_t) voices]) + fifthMix * shape (ph[(size_t) voices + 1]);
            return lp.process ((float) s, cutoff, 0.7, 0);
        });
    }

    std::vector<float> chordOf (const std::vector<float>& tone, const std::vector<int>& semis)
    {
        // resample a root tone to each chord note and sum
        std::vector<float> out (tone.size(), 0.0f);
        for (int st : semis)
        {
            const double r = std::pow (2.0, st / 12.0);
            for (size_t i = 0; i < out.size(); ++i)
            {
                const double p = i * r;
                const size_t a = (size_t) p;
                if (a + 1 >= tone.size())
                    break;
                const float fr = (float) (p - (double) a);
                out[i] += tone[a] * (1 - fr) + tone[a + 1] * fr;
            }
        }
        return out;
    }

    std::vector<float> pianoTone (double sec, uint32_t seed)
    {
        Rng rng (seed);
        OnePole lp;
        auto d = additive (sec, [] (int n, double t) { return std::pow (n, -1.1) * std::exp (-t * (0.6 + 0.35 * n)); }, 0.0004);
        for (size_t i = 0; i < std::min<size_t> (d.size(), 2400); ++i)
            d[i] += lp.lp (rng.next(), 3000) * 0.3f * (float) std::exp (-(double) i / 300.0);
        return d;
    }

    std::vector<float> ePianoTone (double sec)
    {
        double ph = 0, ph2 = 0;
        return render (sec, [&] (double t, int) {
            ph += kTwoPi * kRootHz / kSr;
            ph2 += kTwoPi * kRootHz * 14.0 / kSr;
            const double idx = 0.3 + 1.5 * std::exp (-t / 0.3);
            return (float) ((std::sin (ph + idx * std::sin (ph)) + 0.15 * std::sin (ph2) * std::exp (-t / 0.05)) * std::exp (-t / 1.4));
        });
    }

    std::vector<float> organTone (double sec, bool percussive)
    {
        return additive (sec, [percussive] (int n, double t) {
            static const double bars[9] = { 0.8, 1.0, 0.6, 0.7, 0.0, 0.5, 0.0, 0.4, 0.0 }; // drawbars over harmonics 1..9
            double a = n <= 9 ? bars[n - 1] : 0.0;
            if (percussive && n == 3)
                a += 1.0 * std::exp (-t / 0.25);
            return a * 0.4;
        });
    }

    std::vector<float> brassTone (double sec, double formant, double width, uint32_t seed)
    {
        Rng rng (seed);
        return additive (sec, [formant, width] (int n, double t) {
            const double f = n * kRootHz;
            const double shape = std::exp (-std::pow ((f - formant) / width, 2.0)) + 0.25 / n;
            return shape * std::min (1.0, t / 0.04);
        }, 0.0, 0.004);
    }

    std::vector<float> stringTone (double sec)
    {
        auto d = sawStack (sec, 6, 18.0, 3500.0);
        for (size_t i = 0; i < d.size(); ++i)
            d[i] *= (float) std::min (1.0, i / (kSr * 0.12));
        return d;
    }
} // namespace

// ---------------------------------------------------------------- the library

WaveLibrary::WaveLibrary()
{
    drums.reserve (kNumDrumWaves);
    for (int i = 0; i < 41; ++i)
        drums.push_back (kick (i));
    for (int i = 0; i < 40; ++i)
        drums.push_back (snare (i));
    for (int i = 0; i < 6; ++i)
        drums.push_back (rim (i));
    for (int i = 0; i < 7; ++i)
        drums.push_back (clap (i));
    for (int i = 0; i < 26; ++i)
        drums.push_back (hihat (i));
    for (int i = 0; i < 6; ++i)
        drums.push_back (ride (i));
    for (int i = 0; i < 4; ++i)
        drums.push_back (crash (i));
    for (int i = 0; i < 7; ++i)
        drums.push_back (tom (i));
    for (int i = 0; i < 54; ++i)
        drums.push_back (percussion (i));
    for (int i = 0; i < 9; ++i)
        drums.push_back (synthPerc (i));
    // reverse: a long kick, a roomy snare, a fat snare, a crash, played backwards
    {
        const int sources[4] = { 20, 57, 45, 127 };
        const char* names[4] = { "Rev-BD", "Rev-SD1", "Rev-SD2", "Rev-Crsh" };
        for (int r = 0; r < 4; ++r)
        {
            Wave w = drums[(size_t) sources[r]];
            std::reverse (w.data.begin(), w.data.end());
            finish (w.data, 3);
            w.name = names[r];
            drums.push_back (std::move (w));
        }
    }
    for (int i = 0; i < 3; ++i)
        drums.push_back (scratch (i));
    jassert ((int) drums.size() == kNumDrumWaves);

    // ---- PCM waves
    pcms.reserve (kNumPcmWaves);
    auto add = [this] (Wave w) { pcms.push_back (std::move (w)); };
    add (oneShot (pianoTone (3.0, 1), "Piano"));
    add (oneShot (ePianoTone (2.8), "E.Piano"));
    add (oneShot (additive (1.5, [] (int n, double t) { return (n % 4 == 0 ? 0.2 : 1.0) / std::sqrt ((double) n) * std::exp (-t * (1.5 + 0.2 * n)); }), "Clavi"));
    add (looped (organTone (1.2, false), 0.6, "OrganA"));
    add (looped (organTone (1.6, true), 0.6, "OrganB"));
    add (oneShot (additive (1.8, [] (int n, double t) { return n == 1 ? std::exp (-t / 0.6) : n == 4 ? 0.4 * std::exp (-t / 0.15) : n == 10 ? 0.15 * std::exp (-t / 0.05) : 0.0; }), "Marimba"));
    add (oneShot (additive (3.0, [] (int n, double t) {
                       const double trem = 0.75 + 0.25 * std::sin (kTwoPi * 5.0 * t);
                       return (n == 1 ? 1.0 : n == 4 ? 0.3 : n == 10 ? 0.1 : 0.0) * std::exp (-t / 1.4) * trem; }), "Vibes"));
    {
        // a bell: inharmonic partials, rendered directly
        auto d = render (3.5, [] (double t, int) {
            const double r[5] = { 1.0, 2.76, 5.4, 8.93, 13.34 }, a[5] = { 1.0, 0.6, 0.4, 0.25, 0.15 }, dc[5] = { 2.5, 1.6, 0.9, 0.5, 0.3 };
            double s = 0;
            for (int k = 0; k < 5; ++k)
                if (kRootHz * r[k] < 10000)
                    s += a[k] * std::sin (kTwoPi * kRootHz * r[k] * t) * std::exp (-t / dc[k]);
            return (float) s;
        });
        add (oneShot (std::move (d), "Bell"));
    }
    {
        Rng rng (77);
        Svf bp;
        auto d = additive (1.4, [] (int n, double t) { return (n == 1 ? 1.0 : n == 2 ? 0.2 : n == 3 ? 0.1 : 0.0) * std::min (1.0, t / 0.06); });
        for (auto& x : d)
            x += bp.process (rng.next(), 1800, 2.0, 1) * 0.12f;
        add (looped (std::move (d), 0.5, "Flute"));
    }
    add (looped (brassTone (1.4, 1500, 900, 2), 0.5, "AltoSax"));
    add (looped (brassTone (1.4, 900, 700, 3), 0.5, "TenorSax"));
    add (looped (brassTone (1.4, 1300, 1500, 4), 0.5, "Trumpet"));
    add (looped (brassTone (1.4, 1900, 500, 5), 0.5, "MuteTp"));
    add (looped (sawStack (1.4, 3, 12.0, 2600.0), 0.5, "Brass"));
    {
        const double ah[3] = { 800, 1150, 2900 }, ahG[3] = { 1.0, 0.6, 0.25 };
        add (looped (formantVoice (1.4, ah, ahG, 3, 0.002, 6, 0.05), 0.5, "VoiceAh"));
        std::vector<float> choir;
        for (int v = 0; v < 4; ++v)
        {
            auto one = formantVoice (1.6, ah, ahG, 3, 0.004 + v * 0.002, 10 + (uint32_t) v, 0.08);
            if (choir.empty())
                choir = one;
            else
                for (size_t i = 0; i < choir.size(); ++i)
                    choir[i] += one[i];
        }
        add (looped (std::move (choir), 0.6, "Choir"));
        const double oo[3] = { 350, 700, 2600 }, ooG[3] = { 1.0, 0.4, 0.15 };
        add (looped (formantVoice (1.4, oo, ooG, 3, 0.003, 12, 0.03), 0.5, "VoxWave"));
    }
    {
        const double body[3] = { 500, 1500, 3000 }, bodyG[3] = { 1.0, 0.7, 0.4 };
        add (looped (formantVoice (1.4, body, bodyG, 3, 0.004, 13, 0.02), 0.5, "Violin"));
    }
    add (looped (stringTone (1.6), 0.6, "Strings"));
    add (oneShot (karplus (0.6, 0.985, 0.5, 21), "Pizz"));
    add (oneShot (karplus (2.0, 0.996, 0.35, 22), "NylonGtr"));
    add (oneShot (karplus (2.5, 0.997, 0.75, 23), "SteelGtr"));
    add (oneShot (karplus (0.3, 0.95, 0.6, 24), "MuteGtr"));
    add (oneShot (karplus (0.7, 0.99, 0.9, 25), "FunkGtr"));
    add (oneShot (karplus (3.0, 0.997, 0.8, 26, 0.35), "Sitar"));
    add (oneShot (karplus (2.0, 0.995, 0.25, 27), "AcBass"));
    add (oneShot (karplus (2.0, 0.996, 0.45, 28), "ElBass"));
    add (looped (sawStack (1.2, 2, 6.0, 900.0, 0.5), 0.4, "SynBass"));
    add (oneShot (karplus (1.6, 0.994, 0.8, 29), "PickBass"));
    {
        auto d = karplus (1.4, 0.993, 0.95, 30);
        for (size_t i = 0; i < std::min<size_t> (d.size(), 1200); ++i)
            d[i] *= 1.8f;
        add (oneShot (std::move (d), "SlapBass"));
    }
    {
        double ph = 0;
        add (looped (render (1.2, [&] (double t, int) {
                         ph += kTwoPi * kRootHz / kSr;
                         return (float) std::sin (ph + (0.8 + 1.6 * std::exp (-t / 0.15)) * std::sin (ph)); }), 0.4, "FMBass"));
    }
    add (looped (sawStack (1.2, 1, 0.0, 1400.0), 0.4, "AnaBass"));
    add (looped (sawStack (1.2, 1, 0.0, 6000.0, 0.7), 0.4, "BoostSaw"));
    {
        auto a = sawStack (1.2, 1, 0.0, 8000.0), b = sawStack (1.2, 1, 0.0, 8000.0, 0.0, 0.0, true);
        for (size_t i = 0; i < a.size(); ++i)
            a[i] = 0.5f * (a[i] + b[i]);
        add (looped (std::move (a), 0.4, "SawSqMix"));
    }
    {
        auto d = sawStack (1.2, 1, 0.0, 9000.0);
        OnePole hp;
        for (auto& x : d)
            x = hp.hp (x, 900);
        add (looped (std::move (d), 0.4, "ThinSaw"));
    }
    add (looped (sawStack (1.2, 1, 0.0, 7000.0, 0.9), 0.4, "OctBass1"));
    add (looped (sawStack (1.2, 1, 0.0, 7000.0, 0.9, 0.0, true), 0.4, "OctBass2"));
    add (looped (sawStack (1.2, 1, 0.0, 7000.0, 0.0, 0.8), 0.4, "Saw5th"));
    add (looped (sawStack (1.2, 1, 0.0, 7000.0, 0.0, 0.8, true), 0.4, "Squ5th"));
    static const double sines[5][4] = { { 1, 0, 0, 0 }, { 1, 0.5, 0, 0 }, { 1, 0, 0.4, 0 }, { 1, 0.3, 0.2, 0.15 }, { 1, 0.6, 0.4, 0.3 } };
    for (int s = 0; s < 5; ++s)
        add (looped (additive (1.0, [s] (int n, double) {
                         static const int h[4] = { 1, 2, 3, 5 };
                         for (int k = 0; k < 4; ++k)
                             if (n == h[k])
                                 return sines[s][k];
                         return 0.0; }), 0.4, "SynSin" + juce::String (s + 1)));
    add (looped (additive (1.0, [] (int n, double) { return 1.0 / std::sqrt ((double) n); }), 0.4, "Wire1"));
    add (looped (additive (1.0, [] (int n, double) { return n % 2 == 1 ? 1.0 / n : 0.6 / n; }), 0.4, "Wire2"));
    static const int digi[4][5] = { { 1, 3, 7, 13, 21 }, { 1, 2, 5, 11, 17 }, { 1, 4, 9, 16, 25 }, { 2, 3, 6, 8, 12 } };
    for (int g = 0; g < 4; ++g)
        add (looped (additive (1.0, [g] (int n, double) {
                         for (int k = 0; k < 5; ++k)
                             if (n == digi[g][k])
                                 return 1.0 / (k + 1);
                         return 0.0; }), 0.4, "Digi" + juce::String (g + 1)));
    {
        const double ee[3] = { 300, 2300, 3000 }, eeG[3] = { 1.0, 0.5, 0.3 }, oo[3] = { 400, 800, 2500 }, ooG[3] = { 1.0, 0.5, 0.2 };
        add (looped (formantVoice (1.2, oo, ooG, 3, 0.0, 31), 0.4, "SynVox1"));
        add (looped (formantVoice (1.2, ee, eeG, 3, 0.0, 32), 0.4, "SynVox2"));
    }
    // an endless (Shepard) tone: octave-spaced partials under a bell curve
    add (looped (additive (1.2, [] (int n, double) {
                     if ((n & (n - 1)) != 0)
                         return 0.0; // octaves only
                     const double o = std::log2 ((double) n);
                     return std::exp (-std::pow ((o - 2.0) / 1.2, 2.0)); }), 0.5, "Endless"));
    {
        // motion: a pulse whose width sweeps, and a vowel that slides
        double ph = 0;
        add (looped (render (2.4, [&] (double t, int) {
                         ph += kRootHz / kSr;
                         ph -= std::floor (ph);
                         const double w = 0.5 + 0.4 * std::sin (kTwoPi * 0.4 * t);
                         return (float) (ph < w ? 0.6 : -0.6); }), 1.0, "Motion1"));
        std::array<Svf, 2> fm {};
        double ph2 = 0;
        add (looped (render (2.4, [&] (double t, int) {
                         ph2 += kRootHz / kSr;
                         ph2 -= std::floor (ph2);
                         const float src = (float) (2 * ph2 - 1);
                         const double m = 0.5 + 0.5 * std::sin (kTwoPi * 0.3 * t);
                         return fm[0].process (src, 300 + 600 * m, 6, 1) + fm[1].process (src, 2300 - 1400 * m, 8, 1) * 0.5f; }), 1.0, "Motion2"));
    }
    // hits: chords of a tone, with a fast decay
    {
        auto stab = [] (std::vector<float> d, double decay) {
            for (size_t i = 0; i < d.size(); ++i)
                d[i] *= (float) std::exp (-(double) i / kSr / decay);
            return d;
        };
        const auto brass = brassTone (1.0, 1300, 1500, 40);
        const auto strings = stringTone (1.0);
        const auto saws = sawStack (1.0, 4, 20.0, 5000.0);
        add (oneShot (stab (chordOf (strings, { 0, 7, 12, 16, 19 }), 0.35), "OrchHit"));
        add (oneShot (stab (chordOf (brass, { 0, 4, 7, 12 }), 0.3), "BandHit1"));
        add (oneShot (stab (chordOf (brass, { 0, 5, 9, 12 }), 0.25), "BandHit2"));
        add (oneShot (stab (chordOf (strings, { 0, 3, 7, 10 }), 0.4), "DiscoHit"));
        const std::vector<int> rave[6] = { { 0, 3, 7 }, { 0, 4, 7 }, { 0, 5, 7 }, { 0, 7, 12 }, { 0, 3, 7, 10 }, { 0, 7, 12, 19, 24 } };
        for (int r = 0; r < 6; ++r)
            add (oneShot (stab (chordOf (saws, rave[r]), 0.25), "RaveHit" + juce::String (r + 1)));
    }
    // chord splits (66-72): one chord per key range
    {
        const auto piano = pianoTone (2.5, 50);
        const auto keys = pianoTone (2.5, 51);
        const auto epiano = ePianoTone (2.5);
        const auto organ = organTone (1.2, false);
        const auto strings = stringTone (1.4);
        const auto gtr = karplus (2.0, 0.996, 0.6, 52);
        const std::vector<int> maj { 0, 4, 7, 12 }, mnr { 0, 3, 7, 12 }, maj7 { 0, 4, 7, 11 }, min7 { 0, 3, 7, 10 }, sus47 { 0, 5, 7, 10 },
            sus4 { 0, 5, 7, 12 }, oct { 0, 12 };
        struct Split { const char* name; const std::vector<float>* tone; std::array<const std::vector<int>*, 4> chords; bool loop; };
        const Split splits[7] = { { "CH-Piano", &piano, { &maj, &mnr, &maj7, &min7 }, false },
                                  { "CH-Keys", &keys, { &maj, &mnr, &maj7, &min7 }, false },
                                  { "CH-EPian", &epiano, { &maj, &mnr, &maj7, &min7 }, false },
                                  { "CH-Organ", &organ, { &maj, &mnr, &maj7, &min7 }, true },
                                  { "CH-Strgs", &strings, { &maj, &mnr, &maj7, &min7 }, true },
                                  { "CH-Gtr1", &gtr, { &mnr, &sus47, &min7, &min7 }, false },
                                  { "CH-Gtr2", &gtr, { &mnr, &mnr, &sus4, &oct }, false } };
        for (auto& sp : splits)
        {
            std::array<Wave, 4> four;
            for (int c = 0; c < 4; ++c)
            {
                auto d = chordOf (*sp.tone, *sp.chords[(size_t) c]);
                four[(size_t) c] = sp.loop ? looped (std::move (d), 0.4, sp.name) : oneShot (std::move (d), sp.name);
            }
            chordSplits.push_back (four);
            add (four[0]); // (the wave itself is the first chord; pcm() picks by key)
        }
    }
    for (auto* n : { "DR-Kicks", "DR-Snrs", "DR-CymTm", "DR-Percs" })
        add ({ { 0.0f }, -1, 60.0, n }); // drum sets play the drum waves (see pcm())
    jassert ((int) pcms.size() == kNumPcmWaves);
}

const WaveLibrary& WaveLibrary::get()
{
    static const WaveLibrary lib;
    return lib;
}

const Wave& WaveLibrary::drum (int index) const { return drums[(size_t) std::clamp (index, 0, kNumDrumWaves - 1)]; }

const Wave& WaveLibrary::pcm (int index, int note) const
{
    index = std::clamp (index, 0, kNumPcmWaves - 1);
    if (isChordSplit (index))
    {
        const int range = note < 48 ? 0 : note < 60 ? 1 : note < 72 ? 2 : 3;
        return chordSplits[(size_t) (index - 65)][(size_t) range];
    }
    if (isDrumSet (index))
    {
        // a drum per key from C2 (36) up, within the set's range of waves
        static const int first[4] = { 0, 41, 94, 137 }, count[4] = { 41, 53, 43, 70 };
        const int s = index - 72;
        return drum (first[s] + std::clamp (note - 36, 0, count[s] - 1));
    }
    return pcms[(size_t) index];
}

const juce::String& WaveLibrary::pcmName (int index) const { return pcms[(size_t) std::clamp (index, 0, kNumPcmWaves - 1)].name; }

DrumCategory WaveLibrary::drumCategory (int i)
{
    if (i < 41) return DrumCategory::Kick;
    if (i < 81) return DrumCategory::Snare;
    if (i < 87) return DrumCategory::Rim;
    if (i < 94) return DrumCategory::Clap;
    if (i < 120) return DrumCategory::HiHat;
    if (i < 126) return DrumCategory::Ride;
    if (i < 130) return DrumCategory::Crash;
    if (i < 137) return DrumCategory::Tom;
    if (i < 191) return DrumCategory::Percussion;
    if (i < 200) return DrumCategory::SynthPerc;
    if (i < 204) return DrumCategory::Reverse;
    return DrumCategory::Scratch;
}

int WaveLibrary::firstOf (DrumCategory c)
{
    static const int first[12] = { 0, 41, 81, 87, 94, 120, 126, 130, 137, 191, 200, 204 };
    return first[(int) c];
}

const char* WaveLibrary::categoryName (DrumCategory c)
{
    static const char* n[12] = { "Kick", "Snare", "Rim", "Clap", "Hi Hat", "Ride", "Crash", "Tom", "Percussion", "Synth Perc", "Reverse", "Scratch" };
    return n[(int) c];
}
} // namespace valvebox
