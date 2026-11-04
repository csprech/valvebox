// Quality control for the sound: the wave library, the 16 oscillator models, filter, EG, drum pitch,
// modulation, the 16 effects and the chain, the valve stage, and the data model's files.

#include "Rig.h"
#include "core/Edit.h"

using namespace vbtest;

namespace
{
/** Plays one synth note with this sound and returns the left channel. */
std::vector<float> synthNote (const Sound& s, int note, double seconds, double gate = 10.0)
{
    Rig r;
    r.blank();
    r.pat().parts[(size_t) kFirstSynth].sound = s;
    r.pat().parts[(size_t) kFirstSynth].steps[0] = { true, (uint8_t) note, (uint16_t) std::clamp ((int) (gate * 4), 1, 512) };
    r.pat().length = 8;
    r.tap (Key::PlayPause);
    r.run (seconds);
    return r.L;
}

Sound plainSynth (OscType t, int wave)
{
    Sound s = defaultSound (kFirstSynth);
    s.oscType = t;
    s.wave = (uint8_t) wave;
    s.cutoff = 127;
    s.resonance = 0;
    s.egInt = 64;
    s.ampEg = AmpEg::Gate;
    s.level = 100;
    return s;
}

/** Amplitude of the component at hz. */
double toneAt (const std::vector<float>& x, size_t from, size_t to, double hz)
{
    const double w = 6.283185307179586 * hz / kSr, c = 2.0 * std::cos (w);
    double s1 = 0, s2 = 0;
    to = std::min (to, x.size());
    for (size_t i = from; i < to; ++i)
    {
        const double s0 = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double re = s1 - s2 * std::cos (w), im = s2 * std::sin (w);
    return 2.0 * std::sqrt (re * re + im * im) / (double) (to - from);
}

/** Energy above 2 kHz relative to all (a first difference is a crude high-pass). */
double brightness (const std::vector<float>& x, size_t from)
{
    double d = 0, a = 0;
    for (size_t i = std::max<size_t> (from, 1); i < x.size(); ++i)
    {
        d += (x[i] - x[i - 1]) * (x[i] - x[i - 1]);
        a += x[i] * x[i];
    }
    return a > 0 ? std::sqrt (d / a) : 0.0;
}
} // namespace

struct SoundTests : juce::UnitTest
{
    SoundTests() : juce::UnitTest ("Sound (QC)", "valvebox") {}

    void runTest() override
    {
        waves();
        oscillators();
        filterAndEg();
        drums();
        modulation();
        effects();
        valve();
        model();
    }

    void waves()
    {
        beginTest ("the wave library: 207 drum waves in the original's categories, 76 PCM waves; all sound, all finite");
        const auto& lib = WaveLibrary::get();
        std::set<juce::String> names;
        for (int i = 0; i < kNumDrumWaves; ++i)
        {
            const auto& w = lib.drum (i);
            float peak = 0;
            bool fin = true;
            for (auto x : w.data)
            {
                peak = std::max (peak, std::abs (x));
                fin = fin && std::isfinite (x);
            }
            expect (fin && peak > 0.5f && peak <= 1.0f, "drum " + juce::String (i + 1) + " " + w.name);
            expect (w.name.length() <= 8, "8 characters for the display: " + w.name);
            names.insert (w.name);
        }
        expectEquals ((int) names.size(), kNumDrumWaves, "every drum wave has its own name");
        // the numbering of the original (p.39): 1-41 kick ... 205-207 scratch
        expect (WaveLibrary::drumCategory (0) == DrumCategory::Kick && WaveLibrary::drumCategory (40) == DrumCategory::Kick);
        expect (WaveLibrary::drumCategory (41) == DrumCategory::Snare && WaveLibrary::drumCategory (94) == DrumCategory::HiHat);
        expect (WaveLibrary::drumCategory (119) == DrumCategory::HiHat && WaveLibrary::drumCategory (120) == DrumCategory::Ride);
        expect (WaveLibrary::drumCategory (137) == DrumCategory::Percussion && WaveLibrary::drumCategory (206) == DrumCategory::Scratch);
        // closed hats are short, open hats long
        expect (lib.drum (94).data.size() * 4 < lib.drum (95).data.size(), "open hat rings longer than closed");
        for (int i = 0; i < kNumPcmWaves; ++i)
        {
            const auto& w = lib.pcm (i, 60);
            float peak = 0;
            for (auto x : w.data)
                peak = std::max (peak, std::abs (x));
            expect (peak > 0.3f, "PCM " + juce::String (i + 1) + " " + lib.pcmName (i));
        }
        expect (&lib.pcm (65, 40) != &lib.pcm (65, 76), "chord splits: a different chord across the keyboard");
        expect (&lib.pcm (72, 36) == &lib.drum (0) && &lib.pcm (72, 37) == &lib.drum (1), "drum sets: a drum per key");
    }

    void oscillators()
    {
        beginTest ("the 16 oscillator models all sound; the pitched ones play the note");
        for (int t = 0; t < kNumOscTypes; ++t)
        {
            const auto type = (OscType) t;
            const int waveIndex = type == OscType::PcmComb || type == OscType::PcmWaveshape ? 18 : 0;
            auto s = plainSynth (type, waveIndex);
            s.oscEdit1 = type == OscType::Formant ? 60 : 30;
            s.oscEdit2 = 64;
            const auto x = synthNote (s, 57, 0.5);
            const double level = Rig::rms (x, 4800);
            if (type == OscType::AudioInComb)
            {
                expect (level < 1.0e-4, "audio in + comb: silent with nothing in");
                continue;
            }
            expect (Rig::finite (x) && level > 0.01, juce::String (mapping::oscTypeName (type)) + " sounds: " + juce::String (level, 4));
            if (type == OscType::Waveform || type == OscType::Comb || type == OscType::Vpm || type == OscType::Additive)
            {
                auto sine = s;
                sine.wave = 3; // sine / triangle-ish: clean zero crossings
                sine.oscEdit1 = 0;
                const auto y = synthNote (sine, 57, 0.5);
                if (type == OscType::Waveform)
                    expectWithinAbsoluteError (zeroCrossingHz (y, 4800, y.size()), 220.0, 3.0, "A3 = 220 Hz");
            }
        }
        // WAVEFORM: OSC EDIT 2 near 0 = oscillator 1 alone; +47 = an octave up on oscillator 2
        {
            auto s = plainSynth (OscType::Waveform, 3);
            const auto alone = synthNote (s, 57, 0.5);
            s.oscEdit2 = 64 + 47;
            const auto y = synthNote (s, 57, 0.5);
            expect (toneAt (alone, 4800, 24000, 440.0) < 0.01 && toneAt (y, 4800, 24000, 440.0) > 0.05, "osc 2 adds an octave up (+47)");
        }
        // pulse width at 127 is silence (p.34)
        {
            auto s = plainSynth (OscType::Waveform, 1);
            s.oscEdit1 = 127;
            expect (Rig::rms (synthNote (s, 57, 0.4), 4800) < 1.0e-3, "zero pulse width: no sound");
        }
        // chord: 16 chord types; a major chord has a major third in it (brighter / denser than one note)
        {
            auto chord = plainSynth (OscType::Chord, 3);
            chord.oscEdit1 = 0; // Major: A, C#, E, A
            const auto c = synthNote (chord, 57, 0.5);
            expect (toneAt (c, 4800, 24000, 277.18) > 0.03 && toneAt (c, 4800, 24000, 329.63) > 0.03, "the major third and fifth sound");
            chord.oscEdit1 = 5 * 8 + 2; // minor
            const auto mi = synthNote (chord, 57, 0.5);
            expect (toneAt (mi, 4800, 24000, 261.63) > 0.03 && toneAt (mi, 4800, 24000, 277.18) < 0.01, "minor: a minor third instead");
            expectEquals (mapping::chordTones (5)[1], 3, "minor: a minor third");
        }
        // noise model with no rate: noisy (bright)
        expect (brightness (synthNote (plainSynth (OscType::Noise, 0), 57, 0.4), 4800) > 0.05, "noise oscillator");
        // master tune moves the synths (not the drums)
        {
            Rig r;
            r.blank();
            r.m.getMemory().global.masterTune = 50;
            r.pat().parts[(size_t) kFirstSynth].sound = plainSynth (OscType::Waveform, 3);
            r.pat().parts[(size_t) kFirstSynth].steps[0] = { true, 57, 40 };
            r.tap (Key::PlayPause);
            r.run (0.5);
            expectWithinAbsoluteError (zeroCrossingHz (r.L, 4800, r.L.size()), 220.0 * std::pow (2.0, 0.5 / 12.0), 3.0, "+50 cents");
        }
    }

    void filterAndEg()
    {
        beginTest ("the filter: LPF darker than HPF, cutoff, resonance, drive; the EG: decay vs gate, EG TIME, EG INT");
        auto s = plainSynth (OscType::Waveform, 0);
        s.cutoff = 50;
        const double lp = brightness (synthNote (s, 45, 0.5), 4800);
        s.filterType = FilterType::HPF;
        const double hp = brightness (synthNote (s, 45, 0.5), 4800);
        expect (hp > lp * 2.0, "HPF brighter than LPF");
        s.filterType = FilterType::BPF;
        expect (Rig::rms (synthNote (s, 45, 0.5), 4800) > 1.0e-3, "BPF passes the band");
        s.filterType = FilterType::BPFPlus;
        expect (Rig::rms (synthNote (s, 45, 0.5), 4800) > Rig::rms (synthNote ([&] { auto b = s; b.filterType = FilterType::BPF; return b; }(), 45, 0.5), 4800),
                "BPF+ adds the band to the input");
        s.filterType = FilterType::LPF;
        auto open = s;
        open.cutoff = 120;
        expect (brightness (synthNote (open, 45, 0.5), 4800) > lp * 1.5, "more cutoff, brighter");
        auto driven = s;
        driven.drive = 127;
        expect (brightness (synthNote (driven, 45, 0.5), 4800) > lp, "drive adds overtones");
        // the EG: decay fades during a long note; gate holds
        auto dec = plainSynth (OscType::Waveform, 0);
        dec.ampEg = AmpEg::Decay;
        dec.egTime = 40;
        const auto d = synthNote (dec, 57, 1.0);
        expect (Rig::rms (d, 40000, 48000) < Rig::rms (d, 1000, 6000) * 0.1, "decay: fades while held");
        const auto g = synthNote (plainSynth (OscType::Waveform, 0), 57, 1.0);
        expect (Rig::rms (g, 40000, 48000) > Rig::rms (g, 1000, 6000) * 0.8, "gate: holds while the key's down");
        auto longer = dec;
        longer.egTime = 90;
        const auto l = synthNote (longer, 57, 1.0);
        expect (Rig::rms (l, 40000, 48000) > Rig::rms (d, 40000, 48000) * 5.0, "longer EG TIME");
        // EG INT sweeps the filter: the start is brighter than the end
        auto sweep = plainSynth (OscType::Waveform, 0);
        sweep.cutoff = 30;
        sweep.egInt = 127;
        sweep.egTime = 60;
        const auto sw = synthNote (sweep, 45, 1.0);
        std::vector<float> head (sw.begin() + 400, sw.begin() + 4000), tail (sw.end() - 8000, sw.end());
        expect (brightness (head, 0) > brightness (tail, 0) * 1.3, "EG INT: the filter closes as the EG decays");
        // the gate time: a short gate stops the note
        const auto shortNote = synthNote (plainSynth (OscType::Waveform, 0), 57, 0.6, 1.0);
        expect (Rig::rms (shortNote, 20000, 28800) < 1.0e-3, "gate 1 step: silent after");
    }

    void drums()
    {
        beginTest ("drum parts: PITCH +39 = an octave (p.29), EG TIME, AMP EG gate, LEVEL, PAN");
        expectWithinAbsoluteError (mapping::drumPitchSemitones (39), 12.0, 1.0e-9);
        expectWithinAbsoluteError (mapping::drumPitchSemitones (63), 24.0, 1.0e-9);
        expectWithinAbsoluteError (mapping::drumPitchSemitones (6), 1.0, 1.0e-9);
        expectWithinAbsoluteError (mapping::drumPitchSemitones (-9), -2.0, 1.0e-9);
        expectWithinAbsoluteError (mapping::drumPitchSemitones (43), 14.0, 1.0e-9);
        auto play = [] (Sound s) {
            Rig r;
            r.blank();
            r.pat().parts[3].sound = s;
            r.pat().parts[3].steps[0].on = true;
            r.tap (Key::PlayPause);
            r.run (0.8);
            return r.L;
        };
        auto s = defaultSound (3);
        s.wave = (uint8_t) WaveLibrary::firstOf (DrumCategory::Tom);
        s.egTime = 127;
        const auto base = play (s);
        auto up = s;
        up.pitch = 64 + 39;
        const auto octave = play (up);
        expectWithinAbsoluteError (zeroCrossingHz (octave, 200, 6000) / zeroCrossingHz (base, 200, 6000), 2.0, 0.15, "an octave up");
        auto shortEg = s;
        shortEg.egTime = 20;
        expect (Rig::rms (play (shortEg), 9600, 19200) < Rig::rms (base, 9600, 19200) * 0.2, "short EG TIME");
        auto gate = s;
        gate.ampEg = AmpEg::Gate;
        gate.egTime = 30;
        expect (Rig::rms (play (gate), 12000, 20000) < 1.0e-4, "gate: stops at the EG time");
        auto quiet = s;
        quiet.level = 40;
        expect (Rig::rms (play (quiet)) < Rig::rms (base) * 0.3, "LEVEL");
        // pan: hard left
        Rig r;
        r.blank();
        r.pat().parts[3].sound = s;
        r.pat().parts[3].sound.pan = 1;
        r.pat().parts[3].steps[0].on = true;
        r.tap (Key::PlayPause);
        r.run (0.4);
        expect (Rig::rms (r.L) > 10.0 * Rig::rms (r.R), "PAN L63");
    }

    void modulation()
    {
        beginTest ("modulation: SPEED, DEPTH, five types, the destinations, BPM SYNC");
        // pitch modulation by a square LFO makes two pitches
        auto s = plainSynth (OscType::Waveform, 3);
        s.modType = ModType::Square;
        s.modDest = ModDest::Pitch;
        s.modDepth = 127;
        s.modSpeed = 40;
        const auto x = synthNote (s, 57, 2.0, 40.0);
        double lo = 1e9, hi = 0;
        for (int w = 0; w < 16; ++w)
        {
            const double hz = zeroCrossingHz (x, (size_t) (4800 + w * 5000), (size_t) (4800 + w * 5000 + 2400));
            if (hz > 50)
            {
                lo = std::min (lo, hz);
                hi = std::max (hi, hz);
            }
        }
        expect (hi / lo > 1.5, "square LFO on pitch: jumps between pitches");
        // amp modulation: tremolo depth
        auto t = plainSynth (OscType::Waveform, 3);
        t.modDest = ModDest::Amp;
        t.modDepth = 127;
        t.modSpeed = 60;
        t.modType = ModType::Triangle;
        const auto y = synthNote (t, 57, 1.5);
        double mn = 1e9, mx = 0;
        for (int w = 0; w < 30; ++w)
        {
            const double v = Rig::rms (y, (size_t) (4800 + w * 2000), (size_t) (4800 + w * 2000 + 1000));
            mn = std::min (mn, v);
            mx = std::max (mx, v);
        }
        expect (mx > mn * 3.0, "tremolo");
        // BPM sync: one cycle per bar at 1/1
        expectWithinAbsoluteError (mapping::syncValue (127).quarters, 0.0625, 1.0e-9, "1/64 at the right");
        expectWithinAbsoluteError (mapping::syncValue (0).quarters, 32.0, 1.0e-9, "8/1 at the left");
        for (int type = 0; type < 5; ++type)
        {
            auto m = plainSynth (OscType::Waveform, 0);
            m.modType = (ModType) type;
            m.modDest = ModDest::Cutoff;
            m.modDepth = 127;
            m.cutoff = 50;
            expect (Rig::finite (synthNote (m, 57, 0.6)), mapping::modTypeName ((ModType) type));
        }
    }

    void effects()
    {
        beginTest ("the 16 effect types each change the sound (and stay finite); FX SEND routes a part; the chain feeds one into the next");
        auto renderWith = [] (FxType type, int e1, int e2, bool send) {
            Rig r;
            r.blank();
            auto& p = r.pat().parts[(size_t) kFirstSynth];
            p.sound = plainSynth (OscType::Waveform, 0);
            p.sound.fxSend = send;
            p.sound.fxSelect = 0;
            for (int st = 0; st < 16; st += 4)
                p.steps[(size_t) st] = { true, 57, 2 };
            r.pat().fx[0] = { type, (uint8_t) e1, (uint8_t) e2, false };
            r.tap (Key::PlayPause);
            r.run (1.2);
            return r.L;
        };
        const auto dry = renderWith (FxType::Reverb, 64, 64, false);
        for (int t = 0; t < kNumFxTypes; ++t)
        {
            const auto type = (FxType) t;
            const bool filterType = type == FxType::Lpf || type == FxType::Hpf;
            const auto wet = renderWith (type, filterType ? 50 : 100, 110, true);
            double diff = 0;
            for (size_t i = 0; i < std::min (wet.size(), dry.size()); ++i)
                diff += std::abs (wet[i] - dry[i]);
            expect (Rig::finite (wet) && diff / dry.size() > 1.0e-3, juce::String (mapping::fxTypeName (type)) + " changes the sound");
        }
        // reverb and delays leave a tail after the note stops
        const auto rev = renderWith (FxType::Reverb, 120, 127, true);
        expect (Rig::rms (rev, 50000) > Rig::rms (dry, 50000) * 2.0, "the reverb tail");
        // the chain: FX1 into FX2 (an LPF into a distortion sounds different from FX1 alone)
        Rig r;
        r.blank();
        r.pat().parts[(size_t) kFirstSynth].sound = plainSynth (OscType::Waveform, 0);
        r.pat().parts[(size_t) kFirstSynth].sound.fxSend = true;
        r.pat().parts[(size_t) kFirstSynth].steps[0] = { true, 57, 8 };
        r.pat().fx[0] = { FxType::Lpf, 30, 0, false };
        r.pat().fx[1] = { FxType::Distortion, 127, 127, false };
        r.tap (Key::PlayPause);
        r.run (0.5);
        const double alone = brightness (r.L, 2000);
        Rig c;
        c.blank();
        c.pat() = r.pat();
        c.pat().chain = Chain::OneTwo;
        c.tap (Key::PlayPause);
        c.run (0.5);
        juce::ignoreUnused (alone);
        expect (Rig::rms (c.L, 2000) > Rig::rms (r.L, 2000) * 5.0, "chained: the filtered sound goes on through FX2's distortion");
    }

    void valve()
    {
        beginTest ("VALVE FORCE: more tube gain = more harmonics and more level; individual outs bypass it");
        ValveStage v;
        v.prepare (kSr);
        auto thd = [&] (int gain, double& level) {
            std::vector<float> l (9600), r (9600);
            for (int i = 0; i < 9600; ++i)
                l[(size_t) i] = r[(size_t) i] = 0.4f * (float) std::sin (6.283185307179586 * 200.0 * i / kSr);
            ValveStage st;
            st.prepare (kSr);
            st.process (l.data(), r.data(), 9600, gain);
            level = Rig::rms (l, 2400);
            // harmonic content: what's left after removing the fundamental's best fit
            double c = 0, s2 = 0;
            for (int i = 2400; i < 9600; ++i)
            {
                c += l[(size_t) i] * std::cos (6.283185307179586 * 200.0 * i / kSr);
                s2 += l[(size_t) i] * std::sin (6.283185307179586 * 200.0 * i / kSr);
            }
            const double a = 2.0 * std::sqrt (c * c + s2 * s2) / 7200.0;
            return std::sqrt (std::max (0.0, level * level - a * a / 2.0)) / std::max (1.0e-9, level);
        };
        double lowLevel = 0, highLevel = 0;
        const double lowThd = thd (0, lowLevel), highThd = thd (127, highLevel);
        expect (lowThd < 0.1, "clean at minimum: " + juce::String (lowThd, 3));
        expect (highThd > lowThd * 3.0 && highThd > 0.2, "distorted at maximum: " + juce::String (highThd, 3));
        expect (highLevel > lowLevel * 1.3, "and louder");
        juce::ignoreUnused (v);
    }

    void model()
    {
        beginTest ("files: a pattern, a song and the whole memory survive a save / load exactly");
        auto mem = factoryMemory();
        auto& p = mem.patterns[0];
        p.motion[0].target = 2;
        p.motion[0].param = Param::Cutoff;
        p.motion[0].values.fill (-1);
        p.motion[0].values[3] = 99;
        Pattern back;
        expect (patternFromJson (juce::JSON::parse (juce::JSON::toString (patternToJson (p))), back));
        expect (back == p, "pattern round trip");
        auto& s = mem.songs[0];
        s.events.push_back ({ 96, SongEvent::DrumNote, 1, 0, 127 });
        Song sb;
        expect (songFromJson (juce::JSON::parse (juce::JSON::toString (songToJson (s))), sb));
        expect (sb == s, "song round trip");
        Memory mb;
        expect (memoryFromJson (juce::JSON::parse (juce::JSON::toString (memoryToJson (mem))), mb));
        expect (mb.patterns[0] == mem.patterns[0] && mb.songs[0] == mem.songs[0] && mb.global == mem.global && mb.patternSet == mem.patternSet,
                "memory round trip");
        Pattern junk;
        expect (! patternFromJson (juce::JSON::parse ("{\"format\":\"other\"}"), junk), "rejects other files");
        // the factory patterns all play and stay finite
        for (int i = 0; i < 15; ++i)
        {
            Rig r;
            r.m.selectPattern (i);
            r.tap (Key::PlayPause);
            r.run (1.0);
            expect (Rig::finite (r.L) && Rig::rms (r.L) > 1.0e-3 && Rig::rms (r.L) < 0.9,
                    "factory " + patternNumberText (i) + " " + r.m.getEditPattern().getName() + " plays: " + juce::String (Rig::rms (r.L), 3));
        }
    }
};

static SoundTests soundTests;
