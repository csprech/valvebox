#include "Voices.h"
#include "Mapping.h"
#include <algorithm>
#include <cmath>

namespace valvebox
{
namespace
{
    constexpr double kTwoPi = 6.283185307179586, kPi = 3.141592653589793;

    double polyBlep (double t, double dt)
    {
        if (dt <= 0.0)
            return 0.0;
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0;
        }
        if (t > 1.0 - dt)
        {
            t = (t - 1.0) / dt;
            return t * t + t + t + 1.0;
        }
        return 0.0;
    }
    double wrap (double p) { return p - std::floor (p); }

    double saw (double p, double dt) { return 2.0 * p - 1.0 - polyBlep (p, dt); }
    double pulse (double p, double dt, double w)
    {
        if (w <= 0.0005)
            return 0.0; // zero width: no sound (OSC EDIT 1 at 127)
        double v = p < w ? 1.0 : -1.0;
        v += polyBlep (p, dt);
        v -= polyBlep (wrap (p - w + 1.0), dt);
        return v - (2.0 * w - 1.0);
    }
    double tri (double p) { return p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p; }
    double fold (double x)
    {
        // triangle folding: values past +-1 reflect back
        x = (x + 1.0) * 0.25;
        x = wrap (x);
        return x < 0.5 ? 4.0 * x - 1.0 : 3.0 - 4.0 * x;
    }

    /** The four basic shapes the models choose between: 0 saw, 1 square, 2 triangle, 3 sine. */
    double basic (int shape, double p, double dt)
    {
        switch (shape)
        {
            case 0: return saw (p, dt);
            case 1: return pulse (p, dt, 0.5);
            case 2: return tri (p);
            default: return std::sin (kTwoPi * p);
        }
    }

    /** The WAVEFORM model's shapes with OSC EDIT 1 (p.34): saw -> square, pulse width, triangle fold, sine harmonics. */
    double shaped (int shape, double p, double dt, double e1)
    {
        switch (shape)
        {
            case 0: return (saw (p, dt) - e1 * saw (wrap (p + 0.5), dt)) / (1.0 + 0.25 * e1);
            case 1: return pulse (p, dt, 0.5 * (1.0 - e1));
            case 2: return fold (tri (p) * (1.0 + 2.0 * e1)); // 127 = a triangle an octave and a fifth up
            default: return std::sin (kTwoPi * p + e1 * 2.5 * std::sin (kTwoPi * p));
        }
    }

    float noise (uint32_t& s)
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (float) ((s & 0xffffff) / 8388607.5 - 1.0);
    }

    double egCoef (int egTimeRaw, double sr)
    {
        // -40 dB after the EG TIME
        const double t = mapping::egSeconds (egTimeRaw);
        return std::exp (-4.6 / (t * sr));
    }
} // namespace

// ---------------------------------------------------------------- LFO

void Lfo::trigger (ModType t)
{
    if (t != ModType::Triangle) // the triangle runs free; the others start again with each note (p.30)
        phase = 0.0;
    eg = 1.0f;
    if (t == ModType::SampleHold)
        held = noise (rng);
}

float Lfo::tick (const Sound& s, double bpm)
{
    double hz = s.modBpmSync ? (bpm / 60.0) / mapping::syncValue (s.modSpeed).quarters : mapping::modHz (s.modSpeed);
    const double before = phase;
    phase = wrap (phase + hz / sampleRate);
    const bool wrapped = phase < before;
    switch (s.modType)
    {
        case ModType::Saw: return (float) (1.0 - 2.0 * phase); // a falling ramp
        case ModType::Square: return phase < 0.5 ? 1.0f : -1.0f;
        case ModType::Triangle: return (float) tri (phase);
        case ModType::SampleHold:
            if (wrapped)
                held = noise (rng);
            return held;
        case ModType::Eg:
            eg *= (float) std::exp (-4.6 * hz / sampleRate); // the EG decays over one "cycle"
            return eg;
    }
    return 0.0f;
}

// ---------------------------------------------------------------- drum voice

void DrumVoice::prepare (double sr)
{
    sampleRate = sr;
    lfo.prepare (sr);
    active = false;
}

void DrumVoice::trigger (int w, float velocityGain, const Sound& s)
{
    wave = &WaveLibrary::get().drum (w);
    pos = 0.0;
    age = 0.0;
    vel = velocityGain;
    egLevel = 1.0f;
    fade = 1.0f;
    choking = false;
    active = true;
    lfo.trigger (s.modType);
}

void DrumVoice::choke()
{
    if (active)
        choking = true;
}

void DrumVoice::render (float* l, float* r, int n, const Sound& s, const VoiceContext& ctx)
{
    meter *= 0.9f;
    if (! active || wave == nullptr || wave->data.empty())
        return;
    const auto& d = wave->data;
    const double baseRatio = std::pow (2.0, mapping::drumPitchSemitones (mapping::bipolar (s.pitch)) / 12.0) * (WaveLibrary::kRate / ctx.sampleRate);
    const double coef = egCoef (s.egTime, ctx.sampleRate);
    const double gateSamples = mapping::egSeconds (s.egTime) * ctx.sampleRate;
    const double depth = mapping::bipolar (s.modDepth) / 63.0;
    const float gain = (float) (vel * mapping::levelGain (s.level));
    float pl, pr;
    mapping::panGains (s.pan, pl, pr);
    const float chokeStep = (float) (1.0 / (0.004 * ctx.sampleRate));
    for (int i = 0; i < n; ++i)
    {
        const float m = lfo.tick (s, ctx.bpm);
        double ratio = baseRatio;
        float a = gain, lg = pl, rg = pr;
        switch (s.modDest)
        {
            case ModDest::Pitch: ratio *= std::pow (2.0, depth * m); break; // +-1 octave at full depth
            case ModDest::Amp: a *= (float) std::max (0.0, 1.0 + depth * (m - (s.modType == ModType::Eg ? 1.0 : 0.0)) * 0.9); break;
            case ModDest::Pan:
                mapping::panGains (std::clamp (s.pan + (int) std::lround (depth * m * 63.0), 0, 127), lg, rg);
                break;
            case ModDest::OscEdit1: case ModDest::OscEdit2: case ModDest::Cutoff:
                break; // (synth-only destinations)
        }
        const int ip = (int) pos;
        if (ip + 1 >= (int) d.size())
        {
            active = false;
            break;
        }
        const float frac = (float) (pos - ip);
        float x = d[(size_t) ip] * (1.0f - frac) + d[(size_t) ip + 1] * frac;
        // AMP EG: decay fades over EG TIME; gate holds, then stops at EG TIME
        if (s.ampEg == AmpEg::Decay)
            egLevel *= (float) coef;
        else if (age > gateSamples)
            egLevel = std::max (0.0f, egLevel - chokeStep);
        if (choking)
            fade = std::max (0.0f, fade - chokeStep);
        x *= egLevel * fade * a;
        l[i] += x * lg;
        r[i] += x * rg;
        meter = std::max (meter, std::abs (x));
        pos += ratio;
        age += 1.0;
        if (egLevel < 1.0e-4f || fade <= 0.0f)
        {
            active = false;
            break;
        }
    }
}

// ---------------------------------------------------------------- synth voice

void SynthVoice::Svf::process (double x, double hz, double k, double sr, double& lp, double& bp, double& hp)
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

void SynthVoice::prepare (double sr)
{
    sampleRate = sr;
    lfo.prepare (sr);
    combLine.assign ((size_t) (sr / 15.0) + 4, 0.0f); // down to 15 Hz
    combWrite = 0;
    active = false;
    filter.reset();
    f1.reset();
    f2.reset();
}

void SynthVoice::noteOn (int n, float velocityGain, bool legato, const Sound& s)
{
    note = n;
    target = n;
    gate = true;
    if (legato && active)
        return; // the oscillator, EG and modulation carry on (p.51); glide takes the pitch over
    pitch = n;
    vel = velocityGain;
    egLevel = 1.0f;
    release = 1.0f;
    active = true;
    pcmPos = 0.0;
    lfo.trigger (s.modType);
}

void SynthVoice::noteOff() { gate = false; }

float SynthVoice::comb (float x, double hz, double fb)
{
    const int size = (int) combLine.size();
    const double delay = std::clamp (sampleRate / std::max (15.0, hz), 2.0, (double) size - 3.0);
    double rp = combWrite - delay;
    while (rp < 0)
        rp += size;
    const int i0 = (int) rp;
    const float frac = (float) (rp - i0);
    const float delayed = combLine[(size_t) i0] * (1.0f - frac) + combLine[(size_t) ((i0 + 1) % size)] * frac;
    const float y = x + (float) fb * delayed;
    combLine[(size_t) combWrite] = std::clamp (y, -4.0f, 4.0f);
    combWrite = (combWrite + 1) % size;
    return y * (float) (1.0 - 0.6 * fb);
}

float SynthVoice::oscillate (const Sound& s, double hz, double e1, double e2raw, const VoiceContext& ctx, int i)
{
    const double sr = sampleRate;
    const double dt = hz / sr;
    const int w = s.wave;
    const int e2b = mapping::bipolar ((int) std::lround (e2raw));
    const double e2u = std::clamp (e2raw, 0.0, 127.0);
    auto advance = [this] (int k, double inc) {
        ph[(size_t) k] = wrap (ph[(size_t) k] + inc);
        return ph[(size_t) k];
    };
    switch (s.oscType)
    {
        case OscType::Waveform:
        {
            const int shape = std::clamp (w, 0, 3);
            const double r2 = std::pow (2.0, mapping::oscPitchSemitones (e2b) / 12.0);
            const double p1 = advance (0, dt), p2 = advance (1, dt * r2);
            const double o1 = shaped (shape, p1, dt, e1);
            // with OSC 2 pitch near 0 only oscillator 1 sounds (p.34)
            if (std::abs (e2b) < 2)
                return (float) o1;
            return (float) (0.6 * (o1 + shaped (shape, p2, dt * r2, e1)));
        }
        case OscType::Dual: case OscType::RingMod: case OscType::CrossMod:
        {
            const int s1 = std::clamp (w / 5, 0, 3), s2 = std::clamp (w % 5, 0, 4);
            const double r2 = std::pow (2.0, mapping::oscPitchSemitones (e2b) / 12.0);
            if (s.oscType == OscType::CrossMod)
            {
                // oscillator 2 bends oscillator 1's frequency (linear FM)
                const double p2 = advance (1, dt * r2);
                const double m = s2 == 4 ? noise (rng) : basic (s2, p2, dt * r2);
                const double inc = dt * (1.0 + e1 * 6.0 * m);
                const double p1 = advance (0, inc);
                return (float) basic (s1, p1, std::abs (inc));
            }
            const double p1 = advance (0, dt), p2 = advance (1, dt * r2);
            const double o1 = basic (s1, p1, dt);
            const double o2 = s2 == 4 ? noise (rng) : basic (s2, p2, dt * r2);
            if (s.oscType == OscType::Dual)
                return (float) ((1.0 - e1) * o1 + e1 * o2);
            return (float) ((1.0 - e1) * o1 + e1 * o1 * o2); // ring: dry -> ring modulation
        }
        case OscType::Chord:
        {
            auto tones = mapping::chordTones (std::clamp ((int) (e1 * 16.0), 0, 15));
            // voicing: inversions up (+) or down (-)
            int voicing = std::clamp ((int) std::lround ((e2u - 64.0) / 64.0 * 3.0), -3, 3);
            std::sort (tones.begin(), tones.end());
            for (; voicing > 0; --voicing)
            {
                tones[0] += 12;
                std::sort (tones.begin(), tones.end());
            }
            for (; voicing < 0; ++voicing)
            {
                tones[3] -= 12;
                std::sort (tones.begin(), tones.end());
            }
            double sum = 0;
            for (int k = 0; k < 4; ++k)
            {
                const double d = dt * std::pow (2.0, tones[(size_t) k] / 12.0);
                sum += basic (std::clamp (w, 0, 3), advance (k, d), d);
            }
            return (float) (sum * 0.35);
        }
        case OscType::Unison:
        {
            const int count = 3 + (w % 4), shape = std::clamp (w / 4, 0, 3);
            const double spread = 3.0 + e1 * 45.0; // cents: never perfectly in tune (p.35)
            const double r1 = std::pow (2.0, mapping::oscPitchSemitones (e2b) / 12.0);
            double sum = 0;
            for (int k = 0; k < count; ++k)
            {
                const double c = count > 1 ? (k / (double) (count - 1) - 0.5) * 2.0 * spread : 0.0;
                const double d = dt * std::pow (2.0, c / 1200.0) * (k == 0 ? r1 : 1.0);
                sum += basic (shape, advance (k, d), d);
            }
            return (float) (sum * 1.6 / count);
        }
        case OscType::Sync:
        {
            const int shape = std::clamp (w, 0, 3);
            const double r2 = std::pow (2.0, mapping::upPitchOctaves ((int) e2u));
            const double before = ph[0];
            const double p1 = advance (0, dt);
            double p2 = advance (1, dt * r2);
            if (p1 < before) // the master wrapped: the slave starts its cycle again
            {
                p2 = wrap (p1 * r2);
                ph[1] = p2;
            }
            return (float) shaped (shape, p2, dt * r2, e1);
        }
        case OscType::Vpm:
        {
            const double h = mapping::harmonic ((int) e2u);
            const double p2 = advance (1, dt * h);
            const double p1 = advance (0, dt);
            return (float) basic (std::clamp (w, 0, 3), wrap (p1 + e1 * 0.8 * std::sin (kTwoPi * p2)), dt * 2.0);
        }
        case OscType::Waveshape:
        {
            const double r2 = std::pow (2.0, mapping::oscPitchSemitones (e2b) / 12.0);
            const double x = 0.5 * (tri (advance (0, dt)) + tri (advance (1, dt * r2)));
            if (w == 0)
                return (float) std::sin (kPi * 0.5 * x * (1.0 + 6.0 * e1));
            return (float) std::tanh ((x + 0.3 * e1) * (1.0 + 8.0 * e1)) * 0.9f;
        }
        case OscType::Additive:
        {
            const int shape = std::clamp (w, 0, 3);
            const double h2 = mapping::harmonic ((int) std::lround (e1 * 127.0)), h3 = mapping::harmonic ((int) e2u);
            return (float) (0.5 * basic (shape, advance (0, dt), dt) + 0.3 * basic (shape, advance (1, dt * h2), dt * h2)
                            + 0.2 * basic (shape, advance (2, dt * h3), dt * h3));
        }
        case OscType::Comb:
        {
            const double src = w == 4 ? noise (rng) : basic (std::clamp (w, 0, 3), advance (0, dt), dt);
            return comb ((float) src, hz * std::pow (2.0, mapping::upPitchOctaves ((int) e2u)), e1 * 0.97);
        }
        case OscType::Formant:
        {
            const double src = saw (advance (0, dt), dt);
            const double shift = std::pow (2.0, e2b / 63.0);
            const double fa = 350.0 * shift, fb = (fa + 300.0 + 2200.0 * e1) * 1.0;
            double lp, bp, hp, lp2, bp2, hp2;
            f1.process (src, fa, 0.12, sr, lp, bp, hp);
            f2.process (src, fb, 0.10, sr, lp2, bp2, hp2);
            return (float) (bp * 0.9 + bp2 * 0.6);
        }
        case OscType::Noise:
        {
            // noise, held for longer as RATE goes up, through resonant filters tuned to the note
            const int hold = 1 + (int) (e1 * e1 * 200.0);
            if (++noiseCount >= hold)
            {
                noiseCount = 0;
                noiseHeld = noise (rng);
            }
            double lp, bp, hp, lp2, bp2, hp2;
            f1.process (noiseHeld, hz, 0.08, sr, lp, bp, hp);
            f2.process (noiseHeld, hz, 0.08, sr, lp2, bp2, hp2);
            const double c = (e2b + 63) / 126.0;
            return (float) (((1.0 - c) * lp + c * hp2) * 0.5);
        }
        case OscType::PcmComb: case OscType::PcmWaveshape:
        {
            const auto& wave = WaveLibrary::get().pcm (w, note);
            float x = 0.0f;
            if (! wave.data.empty())
            {
                const double rate = WaveLibrary::isDrumSet (w) ? WaveLibrary::kRate / sr
                                                               : (hz / mapping::noteHz (wave.rootNote)) * (WaveLibrary::kRate / sr);
                const int sz = (int) wave.data.size();
                if (pcmPos >= sz - 1)
                {
                    if (wave.loopStart >= 0)
                        pcmPos = wave.loopStart + std::fmod (pcmPos - wave.loopStart, (double) (sz - 1 - wave.loopStart));
                }
                if (pcmPos < sz - 1)
                {
                    const int ip = (int) pcmPos;
                    const float fr = (float) (pcmPos - ip);
                    x = wave.data[(size_t) ip] * (1 - fr) + wave.data[(size_t) ip + 1] * fr;
                }
                pcmPos += rate;
            }
            if (s.oscType == OscType::PcmComb)
                return comb (x, hz * std::pow (2.0, mapping::upPitchOctaves ((int) e2u)), e1 * 0.97);
            const double bias = (e2u / 127.0 - 0.5) * 0.8, drive = 1.0 + 8.0 * e1;
            return (float) ((std::tanh ((x + bias) * drive) - std::tanh (bias * drive)) / std::max (0.5, std::tanh (drive) * 0.9));
        }
        case OscType::AudioInComb:
        {
            const float x = ctx.audioIn != nullptr ? ctx.audioIn[i] * 2.0f : 0.0f;
            return comb (x, hz * std::pow (2.0, mapping::upPitchOctaves ((int) e2u)), e1 * 0.97);
        }
    }
    return 0.0f;
}

void SynthVoice::render (float* l, float* r, int n, const Sound& s, const VoiceContext& ctx)
{
    if (! active)
        return;
    const double coef = egCoef (s.egTime, ctx.sampleRate);
    const double relCoef = std::exp (-4.6 / (0.006 * ctx.sampleRate));
    const double glideTau = s.roll ? 0.0 : mapping::glideSteps (s.glide) * ctx.stepSeconds;
    const double glideCoef = glideTau > 0.0 ? std::exp (-1.0 / (glideTau * ctx.sampleRate)) : 0.0;
    const double depth = mapping::bipolar (s.modDepth) / 63.0;
    const double gain = vel * mapping::levelGain (s.level);
    const double tune = s.synthTune + ctx.masterTuneCents;
    const double baseCut = std::log2 (mapping::cutoffHz (s.cutoff));
    const double egOct = mapping::egIntOctaves (s.egInt);
    const double res = mapping::resonance (s.resonance);
    const double k = 2.0 - 1.98 * res;
    const double drive = 1.0 + s.drive / 127.0 * 9.0;
    for (int i = 0; i < n; ++i)
    {
        pitch = glideCoef > 0.0 ? target + (pitch - target) * glideCoef : target;
        const float m = lfo.tick (s, ctx.bpm);
        const double mod = depth * m;
        double notePitch = pitch + bend, e1 = s.oscEdit1, e2 = s.oscEdit2, cutOct = baseCut, panRaw = s.pan, ampMod = 1.0;
        switch (s.modDest)
        {
            case ModDest::Pitch: notePitch += 12.0 * mod; break;
            case ModDest::Amp: ampMod = std::max (0.0, 1.0 + (s.modType == ModType::Eg ? depth * (m - 1.0) : mod) * 0.9); break;
            case ModDest::Pan: panRaw += mod * 63.0; break;
            case ModDest::OscEdit1: e1 += mod * 64.0; break;
            case ModDest::OscEdit2: e2 += mod * 64.0; break;
            case ModDest::Cutoff: cutOct += 4.0 * mod; break;
        }
        const double hz = mapping::noteHz (notePitch, tune);
        float x = oscillate (s, hz, std::clamp (e1, 0.0, 127.0) / 127.0, std::clamp (e2, 0.0, 127.0), ctx, i);

        // ---- filter: drive into a state-variable filter swept by the EG
        egLevel *= (float) coef;
        const double cutoff = std::exp2 (cutOct + egOct * egLevel);
        const double in = drive > 1.0 ? std::tanh (x * drive) / std::sqrt (drive) * 1.6 : x;
        double lp, bp, hp;
        filter.process (in, cutoff, k, ctx.sampleRate, lp, bp, hp);
        double y = 0;
        switch (s.filterType)
        {
            case FilterType::LPF: y = lp; break;
            case FilterType::HPF: y = hp; break;
            case FilterType::BPF: y = bp * (1.0 + res); break;
            case FilterType::BPFPlus: y = in + bp * (1.0 + 2.0 * res); break; // the band added back to the input
        }
        // DRIVE overdrives the filter itself: overtones the oscillator didn't have (p.32)
        if (drive > 1.0)
            y = std::tanh (y * (1.0 + (drive - 1.0) * 0.6)) / std::tanh (1.0 + (drive - 1.0) * 0.6) * 0.8;
        // ---- amp: decay follows the EG, gate stays up; both let go quickly when the note ends
        float level = s.ampEg == AmpEg::Decay ? egLevel : 1.0f;
        if (! gate)
            release *= (float) relCoef;
        level *= release;
        const float out = (float) (y * gain * ampMod * level);
        float pl, pr;
        mapping::panGains (std::clamp ((int) std::lround (panRaw), 0, 127), pl, pr);
        l[i] += out * pl;
        r[i] += out * pr;
        if (release < 1.0e-4f || (s.ampEg == AmpEg::Decay && egLevel < 1.0e-4f))
        {
            active = false;
            break;
        }
    }
}
} // namespace valvebox
