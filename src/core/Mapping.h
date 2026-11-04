#pragma once

// Panel values (0..127, bipolar centred on 64) -> what they do: semitones, seconds, Hz, note values.
// Where the manual gives a table (drum pitch, oscillator pitch, comb and sync pitch, BPM-synced
// times) it's followed exactly; elsewhere the ranges are chosen to sound and feel like the original.

#include "core/Model.h"
#include <algorithm>
#include <cmath>

namespace valvebox::mapping
{
/** Bipolar knob: 0..127 -> -63..+63 (0 and 1 both read -63, like the hardware's display). */
inline int bipolar (int raw) { return std::clamp (raw - 64, -63, 63); }
inline int fromBipolar (int v) { return std::clamp (v + 64, 1, 127); }

/** Drum PITCH -63..+63 -> semitones (manual p.29): +-6 = a semitone, +-39 = an octave, +-63 = two,
    with steps of 3 up to the octave and steps of 2 above it; smooth (cents) between the marks. */
inline double drumPitchSemitones (int v)
{
    const double a = std::abs ((double) v);
    double st;
    if (a <= 6.0)
        st = a / 6.0;
    else if (a <= 36.0)
        st = 1.0 + (a - 6.0) / 3.0;          // 9 -> 2 ... 36 -> 11
    else if (a <= 39.0)
        st = 11.0 + (a - 36.0) / 3.0;        // 39 -> 12
    else if (a <= 41.0)
        st = 12.0 + (a - 39.0) / 2.0;        // 41 -> 13
    else if (a <= 61.0)
        st = 13.0 + (a - 41.0) / 2.0;        // 61 -> 23
    else
        st = 23.0 + (a - 61.0) / 2.0;        // 63 -> 24
    return v < 0 ? -st : st;
}

/** Oscillator 2 / modulator pitch, -63..+63 (p.34): 47 = an octave, 63 = two octaves. */
inline double oscPitchSemitones (int v)
{
    const double a = std::abs ((double) v);
    const double st = a <= 47.0 ? a / 47.0 * 12.0 : 12.0 + (a - 47.0) / 16.0 * 12.0;
    return v < 0 ? -st : st;
}

/** Sync slave / comb pitch 0..127 (p.36-37): 31 = +1 octave, 63 = +2, 95 = +3, 127 = +4. */
inline double upPitchOctaves (int raw) { return std::clamp (raw, 0, 127) / 31.75; }

/** Harmonic ratio 0.25..32 (VPM, additive): 7 octaves across the knob. */
inline double harmonic (int raw) { return 0.25 * std::pow (2.0, std::clamp (raw, 0, 127) / 127.0 * 7.0); }

/** EG TIME 0..127 -> decay time in seconds: about 10 ms to 10 s. */
inline double egSeconds (int raw) { return 0.010 * std::pow (1000.0, std::clamp (raw, 0, 127) / 127.0); }

/** GLIDE 0..127 -> time constant in steps (the tempo sets how long that is): 0 = none, up to 2 steps. */
inline double glideSteps (int raw)
{
    const double g = std::clamp (raw, 0, 127) / 127.0;
    return 2.0 * g * g;
}

/** CUTOFF 0..127 -> 20 Hz .. 20 kHz. */
inline double cutoffHz (int raw) { return 20.0 * std::pow (1000.0, std::clamp (raw, 0, 127) / 127.0); }
/** EG INT -63..+63 -> octaves of cutoff sweep (up to 6). */
inline double egIntOctaves (int raw)
{
    const double v = bipolar (raw) / 63.0;
    return 6.0 * v * std::abs (v);
}
/** RESONANCE 0..127 -> 0..1 (self-oscillation near the top). */
inline double resonance (int raw) { return std::clamp (raw, 0, 127) / 127.0; }

/** Modulation SPEED, free: 0..127 -> 0.05 Hz .. 40 Hz. */
inline double modHz (int raw) { return 0.05 * std::pow (800.0, std::clamp (raw, 0, 127) / 127.0); }

/** Note values for BPM-synced modulation, 8/1 .. 1/64 (p.30): one LFO cycle, in quarter notes. */
struct SyncValue
{
    const char* name;
    double quarters;
};
inline const std::array<SyncValue, 21>& syncValues()
{
    static const std::array<SyncValue, 21> v { { { "8/1", 32.0 },  { "6/1", 24.0 },        { "4/1", 16.0 },  { "3/1", 12.0 },
                                                 { "2/1", 8.0 },   { "3/2", 6.0 },         { "1/1", 4.0 },   { "3/4", 3.0 },
                                                 { "2/3", 8.0 / 3 }, { "1/2", 2.0 },       { "3/8", 1.5 },   { "1/3", 4.0 / 3 },
                                                 { "1/4", 1.0 },   { "3/16", 0.75 },       { "1/6", 2.0 / 3 }, { "1/8", 0.5 },
                                                 { "1/12", 1.0 / 3 }, { "1/16", 0.25 },    { "1/24", 1.0 / 6 }, { "1/32", 0.125 },
                                                 { "1/64", 0.0625 } } };
    return v;
}
/** SPEED knob with BPM SYNC on: slow (8/1) on the left, fast (1/64) on the right. */
inline const SyncValue& syncValue (int raw) { return syncValues()[(size_t) std::clamp (std::clamp (raw, 0, 127) * 21 / 128, 0, 20)]; }

/** Delay times synced to the tempo (BPM sync delay, mod delay): 1/64 .. 1/1 of a whole note. */
inline double delayQuarters (int raw)
{
    static const double q[] = { 1.0 / 16.0, 1.0 / 12.0, 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.375, 0.5, 2.0 / 3.0,
                                0.75, 1.0, 4.0 / 3.0, 1.5, 2.0, 8.0 / 3.0, 3.0, 4.0 };
    const int n = (int) (sizeof (q) / sizeof (q[0]));
    return q[std::clamp (raw * n / 128, 0, n - 1)];
}
inline juce::String delayName (int raw)
{
    static const char* names[] = { "1/64", "1/48", "1/32", "1/24", "1/16", "1/12", "3/32", "1/8", "1/6",
                                   "3/16", "1/4",  "1/3",  "3/8",  "1/2",  "2/3",  "3/4",  "1/1" };
    const int n = (int) (sizeof (names) / sizeof (names[0]));
    return names[std::clamp (raw * n / 128, 0, n - 1)];
}

/** Grain shifter SPEED -> sampling interval in steps (manual's table, p.44). */
inline double grainIntervalSteps (int raw)
{
    raw = std::clamp (raw, 0, 127);
    if (raw <= 1) return 128.0;
    if (raw <= 5) return 32.0;
    if (raw <= 9) return 16.0;
    if (raw <= 13) return 12.0;
    if (raw <= 21) return 8.0;
    if (raw <= 25) return 6.0;
    if (raw <= 33) return 4.0;
    if (raw <= 37) return 3.0;
    if (raw <= 41) return 8.0 / 3.0;
    if (raw <= 49) return 2.0;
    if (raw <= 53) return 4.0 / 3.0;
    if (raw <= 83) return 1.0;
    return 0.9 - (raw - 84) / 43.0 * 0.8; // 9/10 .. 1/10 of a step
}

/** Pitch shifter -2400..+2400 cents. */
inline int pitchShiftCents (int raw) { return (int) std::lround (bipolar (raw) / 63.0 * 2400.0); }

/** Accent: how loud accented / unaccented notes are, from the accent part's LEVEL (p.24).
    At 0 there's no difference; turning it up spreads them apart (to 127 and 30 at the top). */
inline int accentedVelocity (int accentLevel) { return 100 + (int) std::lround (27.0 * std::clamp (accentLevel, 0, 127) / 127.0); }
inline int unaccentedVelocity (int accentLevel) { return 100 - (int) std::lround (70.0 * std::clamp (accentLevel, 0, 127) / 127.0); }
inline double velocityGain (int velocity)
{
    const double v = std::clamp (velocity, 0, 127) / 127.0;
    return v * v;
}

/** Synth part level / drum level 0..127 -> gain. */
inline double levelGain (int raw)
{
    const double v = std::clamp (raw, 0, 127) / 127.0;
    return v * v * 1.4;
}

/** Pan L63..R63 -> equal-power gains. */
inline void panGains (int raw, float& l, float& r)
{
    const double p = (bipolar (raw) + 63) / 126.0; // 0..1
    l = (float) std::cos (p * 1.5707963267948966);
    r = (float) std::sin (p * 1.5707963267948966);
}

inline double noteHz (double note, double tuneCents = 0.0) { return 440.0 * std::pow (2.0, (note - 69.0 + tuneCents / 100.0) / 12.0); }

/** Note names as the display shows them: C4 = 60. */
inline juce::String noteName (int note)
{
    static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    note = std::clamp (note, 0, 127);
    return juce::String (n[note % 12]) + juce::String (note / 12 - 1);
}

/** Step key -> note with KEYBOARD on: 16 keys from A to the C an octave and a third above (p.22);
    octave position 1..8, where 5 puts key 4 on C4. */
inline int keyboardNote (int stepKey, int octavePosition) { return 56 + stepKey + (octavePosition - 5) * 12; }

// ---------------------------------------------------------------- arpeggio scales (p.28)

struct Scale
{
    const char* name;
    int count;
    int steps[12];
};
inline const std::array<Scale, kNumArpScales>& scales()
{
    static const std::array<Scale, kNumArpScales> s { {
        { "Chroma", 12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
        { "Ionian", 7, { 0, 2, 4, 5, 7, 9, 11 } },
        { "Dorian", 7, { 0, 2, 3, 5, 7, 9, 10 } },
        { "Phrygi", 7, { 0, 1, 3, 5, 7, 8, 10 } },
        { "Lydian", 7, { 0, 2, 4, 6, 7, 9, 11 } },
        { "MixLyd", 7, { 0, 2, 4, 5, 7, 9, 10 } },
        { "Aeolia", 7, { 0, 2, 3, 5, 7, 8, 10 } },
        { "Locria", 7, { 0, 1, 3, 5, 6, 8, 10 } },
        { "MBlues", 6, { 0, 3, 4, 7, 9, 10 } },
        { "mBlues", 6, { 0, 3, 5, 6, 7, 10 } },
        { "Dim", 8, { 0, 2, 3, 5, 6, 8, 9, 11 } },
        { "ComDim", 8, { 0, 1, 3, 4, 6, 7, 9, 10 } },
        { "MPenta", 5, { 0, 2, 4, 7, 9 } },
        { "mPenta", 5, { 0, 3, 5, 7, 10 } },
        { "Raga1", 7, { 0, 1, 4, 5, 7, 8, 11 } },
        { "Raga2", 7, { 0, 1, 4, 6, 7, 9, 11 } },
        { "Raga3", 7, { 0, 1, 3, 6, 7, 8, 11 } },
        { "Spansh", 8, { 0, 1, 3, 4, 5, 7, 8, 10 } },
        { "Gypsy", 7, { 0, 2, 3, 6, 7, 8, 11 } },
        { "Arabia", 7, { 0, 2, 4, 5, 6, 8, 10 } },
        { "Egypt", 5, { 0, 2, 5, 7, 10 } },
        { "Hawaii", 5, { 0, 2, 3, 7, 9 } },
        { "Pelog", 5, { 0, 1, 3, 7, 8 } },
        { "Japan", 5, { 0, 1, 5, 7, 8 } },
        { "Ryukyu", 5, { 0, 4, 5, 7, 11 } },
        { "Whole", 6, { 0, 2, 4, 6, 8, 10 } },
        { "M3rd", 3, { 0, 4, 8 } },
        { "m3rd", 4, { 0, 3, 6, 9 } },
        { "4th", 3, { 0, 5, 10 } },
        { "5th", 2, { 0, 7 } },
        { "Octave", 1, { 0 } },
    } };
    return s;
}
/** The note `degrees` scale steps above (or below) `centre` in a scale rooted on centre's pitch class. */
inline int scaleNote (int centre, int scaleIndex, int degrees)
{
    const auto& sc = scales()[(size_t) std::clamp (scaleIndex, 0, kNumArpScales - 1)];
    const int n = sc.count;
    const int oct = degrees >= 0 ? degrees / n : -((-degrees + n - 1) / n);
    const int idx = degrees - oct * n;
    return std::clamp (centre + oct * 12 + sc.steps[idx], 0, 127);
}

// ---------------------------------------------------------------- names for the display

inline const char* oscTypeName (OscType t)
{
    static const char* n[kNumOscTypes] = { "WaveForm", "Dual Osc", "ChordOsc", "UnisonOs", "Ring Mod", "OscSync", "CrossMod", "VPM Osc",
                                           "WaveShap", "Additive", "Comb Osc", "Formant", "NoiseOsc", "PCM+Comb", "PCM+WS",   "AudioIn" };
    return n[(int) t];
}
inline const char* fxTypeName (FxType t)
{
    static const char* n[kNumFxTypes] = { "Reverb",   "BPMDelay", "ShrtDly", "ModDelay", "GrainShf", "Cho/Flg", "Phaser",  "RingMod",
                                          "TalkMod",  "PitchShf", "Comp",    "Distort",  "Decimatr", "EQ",      "LPF",     "HPF" };
    return n[(int) t];
}
inline const char* filterName (FilterType f)
{
    static const char* n[4] = { "LPF", "HPF", "BPF", "BPF+" };
    return n[(int) f];
}
inline const char* modTypeName (ModType m)
{
    static const char* n[5] = { "Saw", "Squ", "Tri", "S&H", "EG" };
    return n[(int) m];
}
inline const char* modDestName (ModDest d)
{
    static const char* n[6] = { "Pitch", "Amp", "Pan", "OscEd1", "OscEd2", "Cutoff" };
    return n[(int) d];
}
inline const char* chordName (int index)
{
    static const char* n[16] = { "Major", "6th", "7th", "M7", "7b5", "minor", "m6", "m7", "mMaj7", "dim", "dim7", "m7b5", "aug", "aug7", "sus4", "sus7" };
    return n[std::clamp (index, 0, 15)];
}
/** Chord tones (semitones above the root); 3-note chords double the root an octave up. */
inline std::array<int, 4> chordTones (int index)
{
    static const std::array<int, 4> c[16] = { { 0, 4, 7, 12 }, { 0, 4, 7, 9 },  { 0, 4, 7, 10 }, { 0, 4, 7, 11 },
                                              { 0, 4, 6, 10 }, { 0, 3, 7, 12 }, { 0, 3, 7, 9 },  { 0, 3, 7, 10 },
                                              { 0, 3, 7, 11 }, { 0, 3, 6, 12 }, { 0, 3, 6, 9 },  { 0, 3, 6, 10 },
                                              { 0, 4, 8, 12 }, { 0, 4, 8, 10 }, { 0, 5, 7, 12 }, { 0, 5, 7, 10 } };
    return c[std::clamp (index, 0, 15)];
}

/** How many WAVE choices each oscillator type has (p.34-38). */
inline int waveCount (OscType t)
{
    switch (t)
    {
        case OscType::Waveform: case OscType::Sync: case OscType::Chord: case OscType::Vpm: case OscType::Additive: return 4;
        case OscType::Dual: case OscType::RingMod: case OscType::CrossMod: return 20;
        case OscType::Unison: return 16;
        case OscType::Waveshape: return 2;
        case OscType::Comb: return 5;
        case OscType::PcmComb: case OscType::PcmWaveshape: return kNumPcmWaves;
        case OscType::Formant: case OscType::Noise: case OscType::AudioInComb: return 1;
    }
    return 1;
}

// ---- the fixed control changes (p.92): effects on the global channel, a synth part's keys on its channel
constexpr int kFxTypeCc[3] = { 12, 13, 24 }, kFxEdit1Cc[3] = { 92, 94, 25 }, kFxEdit2Cc[3] = { 93, 95, 26 }, kFxMotionCc[3] = { 20, 21, 22 };
constexpr int kFxChainCc = 23, kPartMotionCc = 80;
struct KeyCc
{
    int cc;
    Param param;
    int options;
};
constexpr KeyCc kKeyCcs[] = { { 83, Param::FilterType, 4 }, { 86, Param::AmpEg, 2 }, { 85, Param::Roll, 2 }, { 91, Param::FxSend, 2 },
                              { 81, Param::FxSelect, 3 }, { 87, Param::ModType, 5 }, { 88, Param::ModDest, 6 }, { 82, Param::ModBpmSync, 2 } };
/** A CC value -> one of n settings, and back (the middle of its range, so it reads back the same). */
inline int ccToOption (int v, int n) { return std::clamp (v * n / 128, 0, n - 1); }
inline int optionToCc (int i, int n) { return std::clamp ((2 * i + 1) * 64 / n, 0, 127); }
} // namespace valvebox::mapping
