#include "core/Effects.h"
#include "core/Mapping.h"
#include <algorithm>
#include <cmath>

namespace valvebox
{
namespace
{
    constexpr double kTwoPi = 6.283185307179586, kPi = 3.141592653589793;
    double wrap (double p) { return p - std::floor (p); }
    float lerpf (float a, float b, float t) { return a + (b - a) * t; }
} // namespace

float Effect::Delay::read (double samplesAgo) const
{
    const int size = (int) buf.size();
    samplesAgo = std::clamp (samplesAgo, 1.0, (double) size - 2.0);
    double rp = w - samplesAgo;
    while (rp < 0)
        rp += size;
    const int i0 = (int) rp;
    const float f = (float) (rp - i0);
    return buf[(size_t) i0] * (1.0f - f) + buf[(size_t) ((i0 + 1) % size)] * f;
}

void Effect::Svf::run (double x, double hz, double k, double sr)
{
    const double g = std::tan (kPi * std::clamp (hz, 10.0, sr * 0.45) / sr);
    const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0 * v1 - ic1;
    ic2 = 2.0 * v2 - ic2;
    lp = v2;
    bp = v1;
    hp = x - k * v1 - v2;
}

void Effect::shelf (Biquad& q, double sr, double hz, double db, bool high)
{
    // RBJ shelving filter
    const double A = std::pow (10.0, db / 40.0), w0 = kTwoPi * hz / sr, cw = std::cos (w0), sw = std::sin (w0);
    const double alpha = sw / 2.0 * std::sqrt (2.0), sa = 2.0 * std::sqrt (A) * alpha;
    double b0, b1, b2, a0, a1, a2;
    if (high)
    {
        b0 = A * ((A + 1) + (A - 1) * cw + sa);
        b1 = -2 * A * ((A - 1) + (A + 1) * cw);
        b2 = A * ((A + 1) + (A - 1) * cw - sa);
        a0 = (A + 1) - (A - 1) * cw + sa;
        a1 = 2 * ((A - 1) - (A + 1) * cw);
        a2 = (A + 1) - (A - 1) * cw - sa;
    }
    else
    {
        b0 = A * ((A + 1) - (A - 1) * cw + sa);
        b1 = 2 * A * ((A - 1) - (A + 1) * cw);
        b2 = A * ((A + 1) - (A - 1) * cw - sa);
        a0 = (A + 1) + (A - 1) * cw + sa;
        a1 = -2 * ((A - 1) + (A + 1) * cw);
        a2 = (A + 1) + (A - 1) * cw - sa;
    }
    q.b0 = b0 / a0;
    q.b1 = b1 / a0;
    q.b2 = b2 / a0;
    q.a1 = a1 / a0;
    q.a2 = a2 / a0;
}

void Effect::prepare (double sr)
{
    sampleRate = sr;
    dl.prepare ((int) (sr * 3.2));
    dr.prepare ((int) (sr * 3.2));
    static const int combMs[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 }; // Freeverb tunings at 44.1 kHz
    static const int apMs[4] = { 556, 441, 341, 225 };
    for (int i = 0; i < 8; ++i)
    {
        combL[(size_t) i].prepare ((int) (combMs[i] * sr / 44100.0));
        combR[(size_t) i].prepare ((int) ((combMs[i] + 23) * sr / 44100.0));
    }
    for (int i = 0; i < 4; ++i)
    {
        apL[(size_t) i].prepare ((int) (apMs[i] * sr / 44100.0));
        apR[(size_t) i].prepare ((int) ((apMs[i] + 23) * sr / 44100.0));
    }
    grainL.assign ((size_t) sr, 0.0f);
    grainR.assign ((size_t) sr, 0.0f);
    reset();
}

void Effect::reset()
{
    dl.clear();
    dr.clear();
    for (auto& c : combL) c.clear();
    for (auto& c : combR) c.clear();
    for (auto& a : apL) a.clear();
    for (auto& a : apR) a.clear();
    combStoreL.fill (0);
    combStoreR.fill (0);
    for (auto& s : svf) s.reset();
    ap.fill (0);
    lfo = osc = env = grainTimer = grainPlay = pitchPhase = 0.0;
    grainLen = 0;
    grainCapture = -1;
    holdCount = 0;
    holdL = holdR = fbL = fbR = 0;
    lowL = lowR = highL = highR = Biquad();
}

void Effect::process (float* l, float* r, int n, const FxSettings& fx, double bpm, double stepSeconds)
{
    if (fx.type != current)
    {
        current = fx.type; // a new type starts from silence, with its LFO at the start (p.44)
        reset();
    }
    const double sr = sampleRate;
    const double e1 = fx.edit1 / 127.0, e2 = fx.edit2 / 127.0;
    switch (fx.type)
    {
        case FxType::Reverb:
        {
            const float fb = (float) (0.70 + 0.28 * e1), damp = 0.25f, wet = (float) (e2 * 0.9);
            for (int i = 0; i < n; ++i)
            {
                const float in = (l[i] + r[i]) * 0.5f * 0.25f; // mono mix in
                float outL = 0, outR = 0;
                for (int c = 0; c < 8; ++c)
                {
                    const float yl = combL[(size_t) c].read ((double) combL[(size_t) c].buf.size() - 1);
                    combStoreL[(size_t) c] = yl * (1 - damp) + combStoreL[(size_t) c] * damp;
                    combL[(size_t) c].push (in + combStoreL[(size_t) c] * fb);
                    outL += yl;
                    const float yr = combR[(size_t) c].read ((double) combR[(size_t) c].buf.size() - 1);
                    combStoreR[(size_t) c] = yr * (1 - damp) + combStoreR[(size_t) c] * damp;
                    combR[(size_t) c].push (in + combStoreR[(size_t) c] * fb);
                    outR += yr;
                }
                for (int a = 0; a < 4; ++a)
                {
                    const float bl = apL[(size_t) a].read ((double) apL[(size_t) a].buf.size() - 1);
                    apL[(size_t) a].push (outL + bl * 0.5f);
                    outL = bl - outL;
                    const float br = apR[(size_t) a].read ((double) apR[(size_t) a].buf.size() - 1);
                    apR[(size_t) a].push (outR + br * 0.5f);
                    outR = br - outR;
                }
                l[i] += outL * wet;
                r[i] += outR * wet;
            }
            break;
        }
        case FxType::BpmDelay: case FxType::ShortDelay: case FxType::ModDelay:
        {
            double seconds;
            if (fx.type == FxType::ShortDelay)
                seconds = 0.001 * std::pow (400.0, e1); // 1 ms .. 400 ms, not synced
            else
            {
                seconds = mapping::delayQuarters (fx.edit1) * 60.0 / bpm;
                // too long for the tempo: halve it (and again if need be), like the original (p.43)
                while (seconds > 3.0)
                    seconds *= 0.5;
                while (seconds < 0.002)
                    seconds *= 2.0;
            }
            const float wet = (float) e2, fb = (float) (e2 * 0.82);
            const bool cross = fx.type != FxType::ModDelay; // BPM and short delays cross L <-> R
            for (int i = 0; i < n; ++i)
            {
                double d = seconds * sr;
                if (fx.type == FxType::ModDelay)
                {
                    lfo = wrap (lfo + 0.6 / sr);
                    d += (0.003 + 0.002 * std::sin (kTwoPi * lfo)) * sr;
                }
                const float yl = dl.read (d), yr = dr.read (fx.type == FxType::ModDelay ? d * 1.01 : d);
                const float inL = l[i], inR = r[i];
                if (cross)
                {
                    dl.push (inL + yr * fb);
                    dr.push (inR + yl * fb);
                }
                else
                {
                    const float m = 0.5f * (inL + inR);
                    dl.push (m + yl * fb);
                    dr.push (m + yr * fb);
                }
                l[i] = inL + yl * wet;
                r[i] = inR + yr * wet;
            }
            break;
        }
        case FxType::GrainShifter:
        {
            // every interval (in steps), sample a short grain and keep replaying it
            const double interval = mapping::grainIntervalSteps (fx.edit1) * stepSeconds * sr;
            const double grainSeconds = std::min (0.9, interval / sr * (0.6 - 0.5 * e1));
            const int len = std::max (64, (int) std::min ((double) grainL.size(), grainSeconds * sr));
            const float bal = (float) e2;
            for (int i = 0; i < n; ++i)
            {
                grainTimer += 1.0;
                if (grainTimer >= interval)
                {
                    grainTimer = 0.0;
                    grainCapture = 0; // start sampling
                    grainLen = len;
                }
                if (grainCapture >= 0)
                {
                    grainL[(size_t) grainCapture] = l[i];
                    grainR[(size_t) grainCapture] = r[i];
                    if (++grainCapture >= grainLen)
                    {
                        grainCapture = -1;
                        grainPlay = 0;
                    }
                }
                float gl = 0, gr = 0;
                if (grainCapture < 0 && grainLen > 0)
                {
                    const int p = (int) grainPlay;
                    const float edge = (float) std::min (1.0, std::min ((double) p, (double) (grainLen - p)) / 64.0);
                    gl = grainL[(size_t) p] * edge;
                    gr = grainR[(size_t) p] * edge;
                    grainPlay = std::fmod (grainPlay + 1.0, (double) grainLen);
                }
                l[i] = lerpf (l[i], gl, bal);
                r[i] = lerpf (r[i], gr, bal);
            }
            break;
        }
        case FxType::ChorusFlanger:
        {
            const double rate = fx.edit1 == 0 ? 0.0 : 0.05 * std::pow (200.0, e1);
            const double base = (15.0 - 13.5 * e2) * 0.001 * sr, swing = (4.0 - 3.0 * e2) * 0.001 * sr;
            const float fb = (float) (0.75 * e2 * e2), mix = 0.6f;
            for (int i = 0; i < n; ++i)
            {
                lfo = wrap (lfo + rate / sr);
                const double ml = base + swing * (0.5 + 0.5 * std::sin (kTwoPi * lfo));
                const double mr = base + swing * (0.5 + 0.5 * std::sin (kTwoPi * (lfo + 0.25)));
                const float yl = dl.read (ml), yr = dr.read (mr);
                dl.push (l[i] + yl * fb);
                dr.push (r[i] + yr * fb);
                l[i] = l[i] * (1 - mix * 0.5f) + yl * mix;
                r[i] = r[i] * (1 - mix * 0.5f) + yr * mix;
            }
            break;
        }
        case FxType::Phaser:
        {
            const double rate = fx.edit1 == 0 ? 0.0 : 0.03 * std::pow (300.0, e1);
            const float fb = (float) (0.7 * e2);
            for (int i = 0; i < n; ++i)
            {
                lfo = wrap (lfo + rate / sr);
                for (int ch = 0; ch < 2; ++ch)
                {
                    const double m = 0.5 + 0.5 * std::sin (kTwoPi * (lfo + ch * 0.25));
                    const double hz = 200.0 * std::pow (15.0, m);
                    const double t = std::tan (kPi * hz / sr), a = (t - 1.0) / (t + 1.0);
                    float& io = ch == 0 ? l[i] : r[i];
                    double x = io + (ch == 0 ? fbL : fbR) * fb;
                    for (int s = 0; s < 6; ++s)
                    {
                        double& z = ap[(size_t) (ch * 6 + s)];
                        const double y = a * x + z;
                        z = x - a * y;
                        x = y;
                    }
                    (ch == 0 ? fbL : fbR) = (float) x;
                    io = (float) (0.5 * (io + x) * (1.0 + 0.5 * e2));
                }
            }
            break;
        }
        case FxType::RingMod:
        {
            const double hz = 20.0 * std::pow (300.0, e1);
            for (int i = 0; i < n; ++i)
            {
                osc = wrap (osc + hz / sr);
                const float m = (float) std::sin (kTwoPi * osc);
                l[i] = lerpf (l[i], l[i] * m, (float) e2);
                r[i] = lerpf (r[i], r[i] * m, (float) e2);
            }
            break;
        }
        case FxType::TalkingMod:
        {
            // three formants morphing a - e - i - o - u
            static const double vowels[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
            const double pos = e1 * 4.0;
            const int a = std::min (3, (int) pos);
            const double t = pos - a, shift = std::pow (2.0, mapping::bipolar (fx.edit2) / 63.0);
            double f[3];
            for (int k = 0; k < 3; ++k)
                f[k] = (vowels[a][k] + (vowels[a + 1][k] - vowels[a][k]) * t) * shift;
            for (int i = 0; i < n; ++i)
            {
                const double in = 0.5 * (l[i] + r[i]);
                double out = 0;
                for (int k = 0; k < 3; ++k)
                {
                    svf[(size_t) k].run (in, f[k], 0.12, sr);
                    out += svf[(size_t) k].bp * (k == 0 ? 1.0 : k == 1 ? 0.7 : 0.4);
                }
                l[i] = r[i] = (float) (out * 1.6);
            }
            break;
        }
        case FxType::PitchShifter:
        {
            // two read heads sweeping through a short window, crossfaded
            const double ratio = std::pow (2.0, mapping::pitchShiftCents (fx.edit1) / 1200.0);
            const double window = 0.05 * sr;
            for (int i = 0; i < n; ++i)
            {
                dl.push (l[i]);
                dr.push (r[i]);
                pitchPhase = wrap (pitchPhase + (1.0 - ratio) / window);
                float sl = 0, sr2 = 0;
                for (int h = 0; h < 2; ++h)
                {
                    const double p = wrap (pitchPhase + h * 0.5);
                    const float g = (float) std::sin (kPi * p);
                    sl += dl.read (1.0 + p * window) * g;
                    sr2 += dr.read (1.0 + p * window) * g;
                }
                l[i] = lerpf (l[i], sl, (float) e2);
                r[i] = lerpf (r[i], sr2, (float) e2);
            }
            break;
        }
        case FxType::Compressor:
        {
            const double threshold = std::pow (10.0, (-1.0 - 34.0 * e1) / 20.0);
            const double att = std::exp (-1.0 / ((0.0002 + 0.08 * e2) * sr)), rel = std::exp (-1.0 / (0.15 * sr));
            const double makeup = std::pow (threshold, -0.6);
            for (int i = 0; i < n; ++i)
            {
                const double peak = std::max (std::abs (l[i]), std::abs (r[i]));
                env = peak > env ? peak + (env - peak) * att : peak + (env - peak) * rel;
                const double over = env > threshold ? env / threshold : 1.0;
                const double gain = std::pow (over, 1.0 / 4.0 - 1.0) * makeup; // 4:1
                l[i] = (float) (l[i] * gain);
                r[i] = (float) (r[i] * gain);
            }
            break;
        }
        case FxType::Distortion:
        {
            const double g = 1.0 + 120.0 * e1 * e1, out = 0.1 + 1.1 * e2;
            for (int i = 0; i < n; ++i)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    float& io = ch == 0 ? l[i] : r[i];
                    const double x = io * g;
                    const double y = x > 0 ? std::tanh (x) : std::tanh (x * 0.8) * 1.1; // a little asymmetric
                    svf[(size_t) ch].run (y, 7000.0, 1.0, sr);
                    io = (float) (svf[(size_t) ch].lp * out);
                }
            }
            break;
        }
        case FxType::Decimator:
        {
            const int hold = 1 + (int) (e1 * e1 * 47.0);
            const double bits = 24.0 - 22.0 * e2;
            const double q = std::pow (2.0, bits - 1.0);
            for (int i = 0; i < n; ++i)
            {
                if (++holdCount >= hold)
                {
                    holdCount = 0;
                    holdL = (float) (std::round (l[i] * q) / q);
                    holdR = (float) (std::round (r[i] * q) / q);
                }
                l[i] = holdL;
                r[i] = holdR;
            }
            break;
        }
        case FxType::Eq:
        {
            const double lowDb = mapping::bipolar (fx.edit1) / 63.0 * 15.0, highDb = mapping::bipolar (fx.edit2) / 63.0 * 15.0;
            shelf (lowL, sr, 300.0, lowDb, false);
            shelf (lowR, sr, 300.0, lowDb, false);
            shelf (highL, sr, 4000.0, highDb, true);
            shelf (highR, sr, 4000.0, highDb, true);
            // (coefficients change only with the knobs: recomputing per block is plenty)
            for (int i = 0; i < n; ++i)
            {
                l[i] = highL.run (lowL.run (l[i]));
                r[i] = highR.run (lowR.run (r[i]));
            }
            break;
        }
        case FxType::Lpf: case FxType::Hpf:
        {
            const double hz = 20.0 * std::pow (1000.0, e1), k = 2.0 - 1.9 * e2;
            for (int i = 0; i < n; ++i)
            {
                svf[0].run (l[i], hz, k, sr);
                svf[1].run (r[i], hz, k, sr);
                l[i] = (float) (fx.type == FxType::Lpf ? svf[0].lp : svf[0].hp);
                r[i] = (float) (fx.type == FxType::Lpf ? svf[1].lp : svf[1].hp);
            }
            break;
        }
    }
}

// ---------------------------------------------------------------- valve stage

void ValveStage::prepare (double sr)
{
    sampleRate = sr;
    dcL = dcR = lpL = lpR = 0;
}

void ValveStage::process (float* l, float* r, int n, int tubeGain)
{
    // a 12AX7 stage: the grid bias makes the curve asymmetric (even harmonics); more gain = more drive and more level
    const double g = std::clamp (tubeGain, 0, 127) / 127.0;
    const double drive = 0.6 + 7.0 * g * g;
    const double bias = 0.25;
    const double norm = 1.0 / std::tanh (drive * 0.9);
    const double level = (1.0 + 1.0 * g) * norm * 0.9;
    const double dcCoef = std::exp (-kTwoPi * 10.0 / sampleRate), lpCoef = std::exp (-kTwoPi * 14000.0 / sampleRate);
    auto triode = [&] (double x) {
        const double v = drive * x + bias;
        const double y = v > 0 ? std::tanh (v) : std::tanh (v * 0.7) / 0.7 * 0.6;
        return y - (std::tanh (bias)); // the bias sits at zero with no signal
    };
    for (int i = 0; i < n; ++i)
    {
        double yl = triode (l[i]), yr = triode (r[i]);
        dcL = yl + (dcL - yl) * dcCoef;
        dcR = yr + (dcR - yr) * dcCoef;
        yl -= dcL;
        yr -= dcR;
        lpL = yl + (lpL - yl) * lpCoef;
        lpR = yr + (lpR - yr) * lpCoef;
        l[i] = (float) (lpL * level);
        r[i] = (float) (lpR * level);
    }
}
} // namespace valvebox
