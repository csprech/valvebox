#pragma once

// Valvebox data model: patterns (16 parts x 128 steps, their sounds, three effects, up to 24 motion
// sequences), songs, the pattern set, and the Global / MIDI settings. Everything a pattern holds is
// fixed-size, so the audio thread can copy and edit patterns without allocating. Song event lists
// are reserved up front for the song being edited.
//
// Values are panel units, as the knobs send them: 0..127, with bipolar knobs centred on 64
// (shown as -63..+63).

#include <juce_core/juce_core.h>
#include <array>
#include <cstdint>
#include <vector>

// (the defaulted == below compares floats exactly: that's the point, a value either changed or it didn't)
#if defined(__clang__)
 #pragma clang diagnostic ignored "-Wfloat-equal"
#endif

namespace valvebox
{
constexpr int kNumDrumParts = 9, kNumSynthParts = 5, kNumParts = 16;
constexpr int kFirstSynth = 9, kDrumAccent = 14, kSynthAccent = 15;
constexpr int kMaxSteps = 128, kMaxLength = 8;
constexpr int kNumPatterns = 256, kNumSongs = 64, kMaxPositions = 256, kMaxMotion = 24, kMaxEvents = 20000;
constexpr int kNumFx = 3, kNumDrumWaves = 207, kNumPcmWaves = 76, kNumOscTypes = 16, kNumFxTypes = 16;
constexpr int kPatternSetGroups = 8, kNumArpScales = 31;

// part indices: drums 1-5, 6A, 6B, 7A, 7B = 0..8, synths 1-5 = 9..13, then the two accent parts
inline bool isDrum (int part) { return part >= 0 && part < kNumDrumParts; }
inline bool isSynth (int part) { return part >= kFirstSynth && part < kFirstSynth + kNumSynthParts; }
inline bool isAccent (int part) { return part == kDrumAccent || part == kSynthAccent; }
/** "Drum1".."Drum7B", "Synth1".."Synth5", "DrmAccnt", "SynAccnt" (8 characters, for the display). */
const char* partName (int part);

enum class Beat : uint8_t { Sixteenth = 0, ThirtySecond, EighthTriplet, SixteenthTriplet };
enum class AmpEg : uint8_t { Decay = 0, Gate };
enum class MotionMode : uint8_t { Off = 0, Smooth, TrigHold };
enum class ModType : uint8_t { Saw = 0, Square, Triangle, SampleHold, Eg };
enum class ModDest : uint8_t { Pitch = 0, Amp, Pan, OscEdit1, OscEdit2, Cutoff };
enum class FilterType : uint8_t { LPF = 0, HPF, BPF, BPFPlus };
enum class OscType : uint8_t
{
    Waveform = 0, Dual, Chord, Unison, RingMod, Sync, CrossMod, Vpm,
    Waveshape, Additive, Comb, Formant, Noise, PcmComb, PcmWaveshape, AudioInComb
};
enum class FxType : uint8_t
{
    Reverb = 0, BpmDelay, ShortDelay, ModDelay, GrainShifter, ChorusFlanger, Phaser, RingMod,
    TalkingMod, PitchShifter, Compressor, Distortion, Decimator, Eq, Lpf, Hpf
};
/** FX CHAIN: none, 1 into 2, 2 into 3, or 1 into 2 into 3. */
enum class Chain : uint8_t { None = 0, OneTwo, TwoThree, All };

/** A part's sound. Drums use pitch, wave (0..206) and the common / modulation settings; synths
    use the lot, with wave's meaning depending on the oscillator type. */
struct Sound
{
    uint8_t pitch = 64;     // drums: -63..+63 = -2..+2 octaves
    uint8_t glide = 0;      // synths
    uint8_t egTime = 64, pan = 64, level = 100;
    AmpEg ampEg = AmpEg::Decay;
    bool roll = false;
    uint8_t fxSelect = 0;   // 0..2
    bool fxSend = false;
    MotionMode motion = MotionMode::Off;
    uint8_t modSpeed = 64, modDepth = 64;
    ModType modType = ModType::Triangle;
    ModDest modDest = ModDest::Pitch;
    bool modBpmSync = false;
    uint8_t wave = 0;
    OscType oscType = OscType::Waveform;
    uint8_t oscEdit1 = 0, oscEdit2 = 64;
    FilterType filterType = FilterType::LPF;
    uint8_t cutoff = 127, egInt = 64, resonance = 0, drive = 0;
    int8_t synthTune = 0;   // cents, -50..+50

    bool operator== (const Sound&) const = default;
};

/** One step of a part. Drum and accent parts use only `on`. Gate is in quarter steps (1..512 = 0.25..128.0). */
struct Step
{
    bool on = false;
    uint8_t note = 60;
    uint16_t gate = 3;
    bool operator== (const Step&) const = default;
};

struct Part
{
    Sound sound;
    std::array<Step, kMaxSteps> steps {};
    bool accentSw = true, swingSw = true, outBus34 = false, muted = false;
    bool operator== (const Part&) const = default;
};

struct FxSettings
{
    FxType type = FxType::Reverb;
    uint8_t edit1 = 64, edit2 = 64;
    bool motion = false;
    bool operator== (const FxSettings&) const = default;
};

/** What a motion sequence records: a part's knob / key, or an effect's EDIT 1 / EDIT 2. */
enum class Param : uint8_t
{
    PitchGlide = 0, EgTime, Pan, Level, AmpEg, Roll, FxSend, FxSelect,
    ModSpeed, ModDepth, ModType, ModDest, ModBpmSync,
    OscEdit1, OscEdit2, Cutoff, EgInt, Resonance, Drive, FilterType,
    FxEdit1, FxEdit2,
    Count
};
constexpr int kNumParams = (int) Param::Count;
/** Switch-type parameters always play back trig-hold. */
bool isSwitchParam (Param p);
const char* paramShortName (Param p); // the display's MotDest names: "Pitch", "EGTime", "Fx1Ed1"...

constexpr int kFxTarget = 16; // motion target 16..18 = FX1..FX3
struct Motion
{
    int8_t target = -1;  // part 0..15, or kFxTarget + fx; -1 = unused
    Param param = Param::Level;
    std::array<int16_t, kMaxSteps> values {}; // per step; -1 = disabled (no update at that step)
    bool used() const { return target >= 0; }
    bool operator== (const Motion&) const = default;
};

struct Pattern
{
    std::array<char, 9> name {};
    float tempo = 120.0f;   // 20..300
    uint8_t swing = 50;     // 50..75 %
    uint8_t length = 1;     // bars, 1..8
    Beat beat = Beat::Sixteenth;
    uint8_t lastStep = 16;  // 1..16
    uint8_t rollType = 3;   // 2..4 hits a step
    uint8_t arpScale = 0;   // 0..30
    uint8_t arpCentre = 60;
    std::array<Part, kNumParts> parts {};
    std::array<FxSettings, kNumFx> fx {};
    Chain chain = Chain::None;
    std::array<Motion, kMaxMotion> motion {};

    juce::String getName() const;
    void setName (const juce::String& n);
    /** Steps in a bar as the step keys show them: 16 (or 12 for triplet beats), cut to LAST STEP. */
    int stepsPerBar() const { return lastStep; }
    int numSteps() const { return (int) length * (int) lastStep; }
    /** One step's length in quarter notes: 1/4, 1/8, 1/3 or 1/6. */
    double stepQuarters() const;
    bool hasTriplets() const { return beat == Beat::EighthTriplet || beat == Beat::SixteenthTriplet; }
    int motionCount() const;
    /** The motion sequence for this target / parameter, or -1. */
    int findMotion (int target, Param p) const;

    bool operator== (const Pattern&) const = default;
};

/** A fresh pattern (what CLEAR PATTERN gives): Init, 120 BPM, 1 bar of 16ths, the default sounds. */
Pattern initPattern();
/** The part's default sound (drum parts get a sensible wave: kick, snare, hats...). */
Sound defaultSound (int part);
/** CLEAR PART: no triggers, synth notes back to C4 with gate 0.75, no motion. */
void clearPartSteps (Part& p);

// ---------------------------------------------------------------- songs

struct Position
{
    int16_t pattern = -1; // 0..255, -1 = End
    int8_t noteOffset = 0;
    bool operator== (const Position&) const = default;
};

/** Event recording in Song mode. Ticks count 96 to a quarter note from the start of the song. */
struct SongEvent
{
    enum Type : uint8_t { DrumNote = 0, KeyNoteOn, KeyNoteOff, ArpNote, Knob, KeyParam, Mute, Tempo };
    uint32_t tick = 0;
    Type type = DrumNote;
    uint8_t part = 0;
    uint8_t param = 0;
    int16_t value = 0;
    bool operator== (const SongEvent&) const = default;
};
constexpr int kSongPPQ = 96;

struct Song
{
    std::array<char, 9> name {};
    float tempo = 120.0f;
    bool tempoLock = false, muteHold = false;
    int8_t nextSong = -1; // -1 = Off
    std::array<Position, kMaxPositions> positions {};
    std::vector<SongEvent> events; // sorted by tick

    juce::String getName() const;
    void setName (const juce::String& n);
    int numPositions() const; // positions before the first End
    bool operator== (const Song&) const = default;
};
Song initSong();

// ---------------------------------------------------------------- global / MIDI

enum class Metronome : uint8_t { Off = 0, Rec0, Rec1, Rec2, On, OnIndividual };
enum class ClockMode : uint8_t { Internal = 0, External, Auto };

constexpr int kNumKnobCcs = 19; // the panel's knobs (core/Machine.h: Knob)

struct Global
{
    // (METRONOME isn't stored: it's Off at every power-on)
    int8_t masterTune = 0;   // cents
    bool protect = true;     // memory protect: on from the factory
    bool arpReverse = false; // ARP CONTROL: swap ribbon and slider
    std::array<uint8_t, 6> midiChannel { 0, 1, 2, 3, 4, 9 }; // synths 1-5, drums (0-based)
    ClockMode clock = ClockMode::Internal;
    int8_t bendRange = 2;
    bool filterProgram = true, filterControl = true, filterExclusive = true, filterNotes = true;
    std::array<uint8_t, kNumDrumParts> drumNotes { 36, 38, 40, 41, 43, 42, 46, 49, 51 };
    /** MIDI UTILITY > CC assign, per panel knob in core/Machine.h's Knob order (p.92); -1 = none. The three global knobs
        (MASTER VOLUME, TUBE GAIN, AUDIO IN LEVEL) aren't on MIDI. */
    std::array<int8_t, kNumKnobCcs> ccAssign { -1, -1, -1, 12, 92, 93, 5, 75, 10, 7, 89, 90, 70, 14, 15, 74, 79, 71, 84 };
    bool operator== (const Global&) const = default;
};

/** All the memory: what WRITE and the card (file) menu save. */
struct Memory
{
    std::vector<Pattern> patterns; // kNumPatterns
    std::vector<Song> songs;       // kNumSongs
    std::array<int16_t, kPatternSetGroups * 16> patternSet {};
    Global global;
    Memory();
};

/** Valvebox's factory patterns, demo songs and pattern set (core/Factory.cpp). */
Memory factoryMemory();

/** "A.01".."D.64" */
juce::String patternNumberText (int index);

// ---------------------------------------------------------------- files (the card menu, data dumps)

juce::var patternToJson (const Pattern& p);
bool patternFromJson (const juce::var& v, Pattern& out);
juce::var songToJson (const Song& s);
bool songFromJson (const juce::var& v, Song& out);
juce::var memoryToJson (const Memory& m);
bool memoryFromJson (const juce::var& v, Memory& out);

} // namespace valvebox
