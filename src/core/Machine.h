#pragma once

// The whole instrument, headless: the panel's 68 keys, 19 knobs, the dial, the ribbon and slider;
// the six modes and their matrix menus; the SHIFT functions; recording (realtime, step, motion,
// song events); the sequencer clock; MIDI in and out. Its outputs are audio, the display text and
// indicators, and the LEDs. The UI and the tests drive it the same way a player drives the panel.
//
// Threading: the engine calls everything under one lock; process() runs on the audio thread and
// never allocates (song event lists are reserved up front, files are read on the message thread).

#include "core/Edit.h"
#include "core/Rack.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <bitset>
#include <functional>

namespace valvebox
{
enum class Key : uint8_t
{
    // common section
    ResetErase = 0, Rec, StopCancel, PlayPause, Tap, PartMute, Solo, AudioInThru, TempoLock, AutoBpm, WriteRename,
    CursorLeft, CursorRight, ModePattern, ModePartEdit, ModeStepEdit, ModeSong, ModeGlobal, ModeMidi,
    // edit section
    FxChain, FxEditSelect, FxMotion, PartMotion, AmpEg, Roll, FxSelect, FxSend, ModType, ModDest, ModBpmSync, FilterType,
    // part select
    Drum1, Drum2, Drum3, Drum4, Drum5, Drum6A, Drum6B, Drum7A, Drum7B, DrumAccent,
    Synth1, Synth2, Synth3, Synth4, Synth5, SynthAccent,
    Keyboard, SelectLeft, SelectRight,
    // step keys
    Shift, PatternSet, Step1, Step2, Step3, Step4, Step5, Step6, Step7, Step8, Step9, Step10, Step11, Step12, Step13,
    Step14, Step15, Step16,
    Count
};
constexpr int kNumKeys = (int) Key::Count; // 68, as on the original
const char* keyName (Key k);
/** Part keys -> part index (drums 0-8, synths 9-13, accents 14/15), or -1. */
int partOfKey (Key k);
Key keyOfPart (int part);
inline bool isStepKey (Key k) { return k >= Key::Step1 && k <= Key::Step16; }
inline int stepKeyIndex (Key k) { return (int) k - (int) Key::Step1; } // 0..15

enum class Knob : uint8_t
{
    MasterVolume = 0, TubeGain, AudioInLevel, FxType, FxEdit1, FxEdit2, PitchGlide, EgTime, Pan, Level,
    ModSpeed, ModDepth, OscType, OscEdit1, OscEdit2, Cutoff, EgInt, Resonance, Drive,
    Count
};
constexpr int kNumKnobs = (int) Knob::Count; // 19, + the dial = 20
static_assert (kNumKnobs == kNumKnobCcs, "Global::ccAssign has one entry per knob");
const char* knobName (Knob k);

enum class Mode : uint8_t { Pattern = 0, PartEdit, StepEdit, Song, Global, Midi };
enum class Led : uint8_t { Off = 0, On, Blink };

/** The indicator LEDs that aren't keys. */
enum class Lamp : uint8_t
{
    Beat = 0,
    SelectGreen,    // index 0..7: the bar playing
    SelectRed,      // index 0..7: bar being edited / keyboard octave / pattern-set group
    ChainOneTwo, ChainTwoThree,
    EditSelect,     // index 0..2
    MotionSmooth, MotionTrigHold,
    AmpDecay, AmpGate,
    FxSelectLamp,   // index 0..2
    ModTypeLamp,    // index 0..4
    ModDestLamp,    // index 0..5
    FilterTypeLamp, // index 0..3
    Count
};

struct Display
{
    juce::String param;   // the 8-character parameter display
    juce::String value;   // the value display
    bool blink = false;   // the name blinks (a pattern change waiting)
    bool patternInd = false, songInd = false, waveInd = false, knobInd = false, originalInd = false;
    int statusRow = 0;    // part status row shown: 0 PLAY, 1 SWING, 2 ACCENT, 3 OUTPUT L/R
    std::array<bool, kNumParts> status {};
    float inputLevel = 0.0f;
    bool clip = false;
    int cursor = -1;      // rename: the character being edited
};

class Machine
{
public:
    Machine();
    void prepare (double sampleRate, int maxBlock);

    // ---- the panel
    void press (Key k);
    void release (Key k);
    bool isHeld (Key k) const { return held[(size_t) k]; }
    /** A knob turned to value 0..127 (the knob's absolute position). */
    void setKnob (Knob k, int value);
    /** What a knob shows for the current part / effect (the UI draws knobs at these). */
    int getKnobValue (Knob k) const;
    void turnDial (int clicks);
    void ribbon (bool touching, float position);
    void slider (float position);

    // ---- what the panel shows
    Display getDisplay() const;
    Led getLed (Key k) const;
    Led getLamp (Lamp l, int index = 0) const;
    Mode getMode() const { return mode; }
    int getPage() const { return page[(size_t) mode]; }
    int getSelectedPart() const { return part; }
    bool isPlaying() const { return transport == Transport::Playing; }
    bool isPaused() const { return transport == Transport::Paused; }
    bool isRecording() const { return recording; }
    double getTempo() const;
    int getPlayingStep() const; // local step, -1 if stopped

    // ---- audio and MIDI (audio thread)
    void process (float* const* out, int numOut, int numSamples, const float* audioIn);
    void midiIn (const juce::MidiMessage& m);
    /** MIDI the instrument sends (notes, knob and key CCs, clock, start / stop). It collects from process() and from
        the panel between blocks; the host sends it after each process() and then clears it. Events from the panel sit
        at offset 0 of the next block. */
    juce::MidiBuffer& midiOut() { return midiOutBuffer; }
    bool wantsAudioInput() const;

    // ---- memory (message thread)
    Memory& getMemory() { return mem; }
    const Pattern& getEditPattern() const { return edit; }
    Pattern& editPattern() { return edit; }
    int getPatternIndex() const { return patternIndex; }
    int getSongIndex() const { return songIndex; }
    void selectPattern (int index);
    /** Where the card menu keeps its files (Save All / Load / Delete / Format). */
    void setCardFolder (const juce::File& f) { cardFolder = f; }
    const juce::File& getCardFolder() const { return cardFolder; }
    /** The user's settings outside patterns: volume, tube gain, input level (and Global if written). */
    int getMasterVolume() const { return knobs[(size_t) Knob::MasterVolume]; }

    /** Restores the factory memory (patterns, songs, global): SOLO + WRITE at power-on (p.89). */
    void loadPreload();

    /** The card menu and MIDI dumps need files / MIDI: the engine runs these outside the audio lock. */
    struct CardRequest
    {
        enum Kind { None, SaveAll, LoadAll, LoadPattern, Delete, Format, DumpTx } kind = None;
        juce::File file;
        int pattern = 0;  // LoadPattern: which pattern of the file
        int dumpWhat = 0; // DumpTx: 0-3 pattern banks A-D, 4 all songs, 5 global
    };
    CardRequest takeCardRequest();
    /** Runs a request here and now (tests, or when there's no engine): file I/O on the caller's thread. */
    void runCardRequest (const CardRequest& r);
    /** After file I/O: put loaded data in place and show the result. */
    void cardFinished (const CardRequest& r, bool ok, const Memory* loaded, const Pattern* loadedPattern);
    juce::StringArray listCardFiles() const;
    /** True once after a WRITE changed the memory (the engine saves it to disk). */
    bool takeMemoryChanged() { const bool c = memoryChanged; memoryChanged = false; return c; }
    void setMemory (const Memory& m);

    /** Testing: every note the instrument plays (part, note, velocity, step position, legato). */
    struct TriggerRecord
    {
        int part, note, velocity;
        double pos;
        bool legato;
        int pattern;
    };
    void enableTriggerLog (bool on) { logTriggers = on; triggerLog.clear(); triggerLog.reserve (4096); }
    const std::vector<TriggerRecord>& getTriggerLog() const { return triggerLog; }
    void clearTriggerLog() { triggerLog.clear(); }
    const Rack& getRack() const { return rack; }
    const std::array<Sound, kNumParts>& getEffectiveSounds() const { return effective; }
    int getTranspose() const { return transpose; }
    int getOctave() const { return octave; }
    bool isKeyboardOn() const { return keyboard; }
    int getSongPosition() const { return songPosition; }
    const Song& getEditSong() const { return songEdit; }
    Song& editSong() { return songEdit; }
    Metronome getMetronome() const { return metronome; }
    double getPosition() const { return pos; }

private:
    enum class Transport { Stopped, Playing, Paused };
    enum class Op
    {
        None, Write, Rename, LastStep, MoveSelect, Move, ShiftSelect, Shift, CopyPart, CopySound, ClearMotion, ClearPart,
        ClearPattern, InsertPattern, DeletePattern, ClearSong, MidiUtil, MidiFilter, DrumNote, CcSelect, CcNumber, CcContinue,
        Dump, CardMenu, CardLoadWhat, CardFile, CardPattern, CardName, CardSure, PatternSetWrite, GlobalWrite, Compare
    };

    // ---- panel helpers
    void pressPart (int p);
    void pressStep (int key);
    void releaseStep (int key);
    void pressShiftStep (int key);
    void pressMode (Mode m);
    void pressWrite();
    void pressStop();
    void pressPlay();
    void pressRec();
    void cycleSoundKey (Key k);
    void dialPage (int clicks);
    void dialOp (int clicks);
    void cancelOp();
    void flash (const juce::String& param, const juce::String& value = {}, double seconds = 1.2);
    void showKnob (Knob k);
    juce::String knobValueText (Knob k) const;
    juce::String pageName() const;
    juce::String pageValue() const;
    int numPages() const;
    int barsForKeys() const;
    int stepFor (int key) const; // step key -> pattern step in the shown bar
    Sound& sound() { return playing().parts[(size_t) part].sound; }
    const Sound& sound() const { return playingConst().parts[(size_t) part].sound; }
    Pattern& playing() { return mode == Mode::Song && songActive ? songPat : edit; }
    const Pattern& playingConst() const { return mode == Mode::Song && songActive ? songPat : edit; }
    const Pattern& stored() const { return mem.patterns[(size_t) patternIndex]; }
    void setParam (int target, Param p, int value, bool fromKnob);
    int getParam (int target, Param p) const;
    void loadPatternNow (int index);
    void applyTempoForPattern();
    void startPlayback (bool fromTop);
    void stopPlayback();

    // ---- sequencer (MachinePlay.cpp)
    void stepEvents (double from, double to);
    void playStep (int localStep, double when);
    void triggerPart (int p, int velocity, int note, bool fromSequence, bool legato = false);
    void noteOffPart (int p);
    void patternEnded();
    void songAdvance();
    void loadSongPosition (int pos);
    void applyMotion (double localPos);
    void recordMotionStep (int localStep);
    void recordLiveTrigger (int p, int note);
    void recordLiveRelease (int p);
    void recordEvent (SongEvent::Type t, int p, int param, int value);
    void playSongEvents (uint32_t fromTick, uint32_t toTick);
    void runArp (double from, double to);
    void renderClick (float* buf, int n);
    void detectBpm (const float* in, int n);
    int velocityFor (int p, int localStep) const;
    bool partAudible (int p) const;
    double stepSamples() const;
    uint32_t songTickNow() const;
    /** Note on (velocity > 0) or off at MIDI OUT. A synth part is mono: its note-off is always for the note it sent,
        a new note ends the last one (after it, for legato), so nothing hangs on the receiver. */
    void sendNote (int p, int note, int velocity, int sampleOffset, bool legato = false);
    /** Note-offs for synth notes still on at MIDI OUT (all of them, or only the ones nobody's holding). */
    void releaseSentNotes (bool evenHeld);
    /** Control change out (when the C filter allows it), on a 0-based channel. */
    void sendCc (int channel, int cc, int value);
    /** A panel knob or key moved: send its CC, like the original (synth parts on their channel, effects on the global). */
    void sendKnobCc (Knob k, int value);
    void sendKeyCc (Key k);
    void applyKnob (Knob k, int value);

    Memory mem;
    Pattern edit, compareBackup, songPat;
    int patternIndex = 0, pendingPattern = -1, songIndex = 0;
    Song songEdit;
    bool songActive = false;
    Mode mode = Mode::Pattern;
    std::array<int, 6> page {};
    int part = 0, lastSynthPart = kFirstSynth;
    int fxEdit = 0;
    bool keyboard = false, keyboardBeforeDrum = false;
    int octave = 5, barShown = 0, patternSetGroup = 0;
    int transpose = 0;
    std::array<bool, kNumKeys> held {};
    std::array<int, kNumKnobs> knobs {};
    bool shiftUsed = false, soloUsed = false;
    std::bitset<kNumParts> solo;
    bool tempoLock = false, audioThru = false;
    float lockedTempo = 120.0f;
    Metronome metronome = Metronome::Off;

    // operations in progress
    Op op = Op::None;
    int opValue = 0, opValue2 = 0, opKey = -1, opCursor = 0;
    edit::PartMask opParts;
    edit::FxMask opFx;
    int opSourcePart = -1;
    juce::String opName;
    juce::StringArray cardFiles;
    bool patternSetHold = false;
    int chainFrom = -1, chainTo = -1, chainPos = -1, lastPatternSetKey = -1;

    // display
    juce::String flashParam, flashValue;
    double flashUntil = 0.0;
    int shownKnob = -1;
    double knobUntil = 0.0;
    double clockSeconds = 0.0; // advances with process(): drives the display timeouts

    // transport and recording
    Transport transport = Transport::Stopped;
    bool recording = false, recordReady = false, stepRecord = false, eventRecord = false;
    int countIn = 0, countInLength = 0; // samples of count-in left, and in all
    double pos = 0.0;      // in steps, from the start of the current pattern
    bool wrapped = false;  // the pattern ended (and pos moved on) inside this chunk
    double songStepBase = 0.0; // song mode: steps played before the current position
    int songPosition = 0;
    int lastStepPlayed = -1;
    int stepRecTarget = 0;
    int noteEditStep = 0;
    int motionDest = 0;
    struct MotionRec { int slot = -1; int target = -1; Param param = Param::Level; int stepsLeft = 0; int value = 0; };
    std::array<MotionRec, 4> motionRec {};
    std::array<int, kNumParts> heldNote {};       // keyboard / MIDI note held per synth part (-1 none)
    std::array<int, kNumParts> noteSent {};       // the note each synth part has on at MIDI OUT (-1 none)
    std::array<double, kNumParts> recPressPos {}; // realtime recording: where a key went down
    std::array<int, kNumParts> recStep {};
    std::array<int, kNumParts> skipOnce {};       // a step just recorded ahead of the playhead: don't play it twice
    std::array<double, kNumParts> offAt {};       // synth gate ends (steps, may pass the pattern end)
    std::array<bool, kNumParts> gateOn {};
    std::array<int, kNumParts> lastPlayedStep {};
    std::array<int, kMaxMotion> trigHoldValue {};
    std::array<Sound, kNumParts> effective {};
    std::array<FxSettings, kNumFx> effectiveFx {};
    juce::Array<double> taps;
    double lastTapSeconds = -10.0;
    bool erasing = false;
    int songEventIndex = 0;

    // arpeggiator
    bool ribbonDown = false;
    float ribbonPos = 0.5f, sliderPos = 0.5f;
    std::array<int, 16> arpNotes {};
    int arpCount = 0, arpIndex = 0;
    double arpNext = 0.0, arpOff = -1.0;
    int arpPlayingNote = -1;

    // auto BPM
    bool bpmScanning = false, bpmFound = false;
    float detectedBpm = 0.0f;
    std::vector<float> onsetEnv;
    int onsetWrite = 0, onsetDecim = 0;
    float onsetAcc = 0.0f, onsetPrev = 0.0f;
    float inputPeak = 0.0f;
    bool inputClip = false;

    // MIDI
    juce::MidiBuffer midiOutBuffer;
    double clockTickAcc = 0.0;
    int extClockTicks = 0;
    double extClockLast = -1.0, extBpm = 120.0;
    bool extRunning = false;
    int sampleInBlock = 0;

    // audio
    Rack rack;
    double sampleRate = 48000.0;
    int maxBlock = 512;
    std::vector<float> clickBuf;
    double clickPhase = 0.0, clickEnv = 0.0, clickHz = 1000.0;
    juce::File cardFolder;
    juce::Random random;
    CardRequest pendingCard;
    bool memoryChanged = false;
    bool logTriggers = false;
    std::vector<TriggerRecord> triggerLog;
};
} // namespace valvebox
