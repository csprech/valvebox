// Quality control for the panel: every key, knob, the dial, the ribbon and the slider, worked the way
// the owner's manual describes, checked against what the instrument then does. Page references are
// to the EMX-1 Owner's Manual. The last check fails if any control on the panel went untested.

#include "Rig.h"
#include <map>

using namespace vbtest;

namespace
{
std::set<int> keysTested, knobsTested;
bool dialTested = false, ribbonTested = false, sliderTested = false;
void covers (std::initializer_list<Key> ks)
{
    for (auto k : ks)
        keysTested.insert ((int) k);
}
void coversKnob (std::initializer_list<Knob> ks)
{
    for (auto k : ks)
        knobsTested.insert ((int) k);
}

void gotoPage (Rig& r, Key modeKey, int pageIndex)
{
    r.tap (modeKey);
    for (int i = 0; i < 12 && r.m.getPage() != pageIndex; ++i)
        r.tap (Key::CursorRight);
}

int countTriggersAt (const std::vector<Machine::TriggerRecord>& t, double from, double to)
{
    int n = 0;
    for (auto& x : t)
        n += x.pos >= from && x.pos < to ? 1 : 0;
    return n;
}
} // namespace

struct PanelTests : juce::UnitTest
{
    PanelTests() : juce::UnitTest ("Panel (QC)", "valvebox") {}

    void runTest() override
    {
        modes();
        transport();
        tempo();
        patterns();
        steps();
        timing();
        accents();
        muteSolo();
        keyboardAndTranspose();
        recording();
        stepEditing();
        motion();
        writing();
        shiftFunctions();
        songs();
        patternSet();
        effectsAndSoundKeys();
        knobs();
        audioIn();
        globalMode();
        midi();
        midiOut();
        card();
        arpeggiator();

        beginTest ("every key, knob and controller on the panel was tested");
        for (int k = 0; k < kNumKeys; ++k)
            expect (keysTested.count (k) == 1, juce::String ("untested key: ") + keyName ((Key) k));
        for (int k = 0; k < kNumKnobs; ++k)
            expect (knobsTested.count (k) == 1, juce::String ("untested knob: ") + knobName ((Knob) k));
        expect (dialTested && ribbonTested && sliderTested, "dial / ribbon / slider");
        expectEquals (kNumKeys, 68, "68 keys, like the original");
        expectEquals (kNumKnobs + 1, 20, "20 knobs with the dial, like the original");
    }

    // ------------------------------------------------------------------ modes and the matrix menu (p.10)

    void modes()
    {
        beginTest ("mode keys light; the cursor keys walk the matrix menu");
        covers ({ Key::ModePattern, Key::ModePartEdit, Key::ModeStepEdit, Key::ModeSong, Key::ModeGlobal, Key::ModeMidi, Key::CursorLeft, Key::CursorRight });
        Rig r;
        const std::pair<Key, Mode> modesList[] = { { Key::ModePartEdit, Mode::PartEdit }, { Key::ModeStepEdit, Mode::StepEdit }, { Key::ModeSong, Mode::Song },
                                                   { Key::ModeGlobal, Mode::Global }, { Key::ModeMidi, Mode::Midi }, { Key::ModePattern, Mode::Pattern } };
        for (auto [k, md] : modesList)
        {
            r.tap (k);
            expect (r.m.getMode() == md, keyName (k));
            expect (r.m.getLed (k) == Led::On, juce::String (keyName (k)) + " lit");
        }
        // Pattern mode pages: Pattern, Tempo, Swing, Length, Beat, RollType, ArpScale
        const char* names[] = { "Tempo", "Swing", "Length", "Beat", "RollType", "ArpScale" };
        for (auto* n : names)
        {
            r.tap (Key::CursorRight);
            expectEquals (r.m.getDisplay().param, juce::String (n));
        }
        r.tap (Key::CursorRight);
        expectEquals (r.m.getPage(), 0, "wraps round to the pattern name");
        expectEquals (r.m.getDisplay().value, juce::String ("A.01"));
        expect (r.m.getDisplay().patternInd, "PATTERN indicator");
        r.tap (Key::CursorLeft);
        expectEquals (r.m.getDisplay().param, juce::String ("ArpScale"));
        gotoPage (r, Key::ModePartEdit, 1);
        expectEquals (r.m.getDisplay().param, juce::String ("AccentSw"));
        gotoPage (r, Key::ModeStepEdit, 2);
        expectEquals (r.m.getDisplay().param, juce::String ("GateTime"));
        gotoPage (r, Key::ModeGlobal, 0);
        expectEquals (r.m.getDisplay().param, juce::String ("Metronom"));
        gotoPage (r, Key::ModeMidi, 2);
        expectEquals (r.m.getDisplay().param, juce::String ("BendRnge"));
    }

    // ------------------------------------------------------------------ transport (p.19)

    void transport()
    {
        beginTest ("PLAY / PAUSE, STOP / CANCEL, RESET");
        covers ({ Key::PlayPause, Key::StopCancel, Key::ResetErase });
        Rig r;
        r.tap (Key::PlayPause);
        expect (r.m.isPlaying() && r.m.getLed (Key::PlayPause) == Led::On, "playing, lit");
        r.run (0.5);
        expect (r.m.getPlayingStep() > 0);
        r.tap (Key::PlayPause);
        expect (r.m.isPaused() && r.m.getLed (Key::PlayPause) == Led::Blink, "paused: the key blinks");
        const int at = r.m.getPlayingStep();
        r.run (0.3);
        expectEquals (r.m.getPlayingStep(), at, "paused: no steps go by");
        r.tap (Key::PlayPause);
        expect (r.m.isPlaying(), "resumes");
        r.run (0.3);
        r.tap (Key::ResetErase);
        expectEquals (r.m.getPlayingStep(), 0, "RESET: back to the start");
        r.tap (Key::StopCancel);
        expect (! r.m.isPlaying() && r.m.getLed (Key::PlayPause) == Led::Off, "stopped");
        expect (r.m.getLamp (Lamp::Beat) == Led::Off);
    }

    // ------------------------------------------------------------------ tempo (p.20-21)

    void tempo()
    {
        beginTest ("TEMPO with the dial (SHIFT for tenths), TAP, TEMPO LOCK");
        covers ({ Key::Tap, Key::TempoLock, Key::Shift });
        dialTested = true;
        Rig r;
        r.blank();
        gotoPage (r, Key::ModePattern, 1);
        r.dial (5);
        expectWithinAbsoluteError (r.m.getTempo(), 125.0, 1.0e-6, "dial");
        r.dialWith (Key::Shift, 3);
        expectWithinAbsoluteError (r.m.getTempo(), 125.3, 1.0e-4, "SHIFT + dial: tenths");
        r.dial (-1000);
        expectWithinAbsoluteError (r.m.getTempo(), 20.0, 1.0e-6, "20.0 at the bottom");
        r.dial (1000);
        expectWithinAbsoluteError (r.m.getTempo(), 300.0, 1.0e-6, "300.0 at the top");
        // TAP at quarter notes of 100 BPM (0.6 s)
        for (int i = 0; i < 4; ++i)
        {
            r.tap (Key::Tap);
            r.run (0.6);
        }
        expectWithinAbsoluteError (r.m.getTempo(), 100.0, 1.0, "tapped 100 BPM");
        // TEMPO LOCK keeps the tempo through a pattern change
        r.tap (Key::TempoLock);
        expect (r.m.getLed (Key::TempoLock) == Led::On);
        const double locked = r.m.getTempo();
        gotoPage (r, Key::ModePattern, 0);
        r.dial (1); // House1, 124 BPM
        expectWithinAbsoluteError (r.m.getTempo(), locked, 1.0e-6, "locked");
        r.tap (Key::TempoLock);
        r.dial (1);
        r.dial (-1);
        expectWithinAbsoluteError (r.m.getTempo(), 124.0, 1.0e-6, "unlocked: the pattern's own tempo");
    }

    // ------------------------------------------------------------------ patterns (p.20)

    void patterns()
    {
        beginTest ("selecting patterns: at once when stopped, at the end of the pattern when playing (the name blinks)");
        Rig r;
        gotoPage (r, Key::ModePattern, 0);
        r.dial (1);
        expectEquals (r.m.getPatternIndex(), 1);
        expectEquals (r.m.getDisplay().param, juce::String ("House1"));
        expectEquals (r.m.getDisplay().value, juce::String ("A.02"));
        r.tap (Key::PlayPause);
        r.run (0.4);
        r.dial (1);
        expectEquals (r.m.getPatternIndex(), 1, "still playing the old one");
        expect (r.m.getDisplay().blink, "the waiting pattern blinks");
        r.run (60.0 / 124.0 * 8.0 + 0.1); // two bars at 124
        expectEquals (r.m.getPatternIndex(), 2, "changed at the end");
        expect (! r.m.getDisplay().blink);
        r.dial (-200);
        r.tap (Key::StopCancel);
        expectEquals (r.m.getPatternIndex(), 0, "stopping takes the waiting pattern");
        r.dial (300);
        expectEquals (r.m.getPatternIndex(), 255, "D.64 is the last");
        expectEquals (r.m.getDisplay().value, juce::String ("D.64"));
    }

    // ------------------------------------------------------------------ step keys, select keys, last step (p.48, 53)

    void steps()
    {
        beginTest ("step keys toggle the selected part's triggers; select keys choose the bar; LAST STEP");
        covers ({ Key::Step1, Key::Step2, Key::Step3, Key::Step4, Key::Step5, Key::Step6, Key::Step7, Key::Step8, Key::Step9, Key::Step10,
                  Key::Step11, Key::Step12, Key::Step13, Key::Step14, Key::Step15, Key::Step16, Key::SelectLeft, Key::SelectRight,
                  Key::Drum1, Key::Drum2, Key::Drum3, Key::Drum4, Key::Drum5, Key::Drum6A, Key::Drum6B, Key::Drum7A, Key::Drum7B,
                  Key::Synth1, Key::Synth2, Key::Synth3, Key::Synth4, Key::Synth5, Key::DrumAccent, Key::SynthAccent });
        Rig r;
        r.blank();
        for (int p = 0; p < kNumParts; ++p)
        {
            r.selectPart (p);
            expectEquals (r.m.getSelectedPart(), p, partName (p));
            expect (r.m.getLed (keyOfPart (p)) == Led::On, juce::String (partName (p)) + " key lit");
            const int key = p % 16;
            r.tap ((Key) ((int) Key::Step1 + key));
            expect (r.pat().parts[(size_t) p].steps[(size_t) key].on, juce::String (partName (p)) + " step");
            expect (r.m.getLed ((Key) ((int) Key::Step1 + key)) == Led::On, "the step key lights");
            r.tap ((Key) ((int) Key::Step1 + key));
            expect (! r.pat().parts[(size_t) p].steps[(size_t) key].on, "and toggles off");
        }
        // two bars: the select keys move the red LED and the step keys edit bar 2
        r.selectPart (0);
        r.pat().length = 2;
        r.tap (Key::SelectRight);
        expect (r.m.getLamp (Lamp::SelectRed, 1) == Led::On, "red LED on bar 2");
        r.tap (Key::Step1);
        expect (r.pat().parts[0].steps[16].on, "step 1 of bar 2 = step 17");
        r.tap (Key::SelectLeft);
        expect (r.m.getLamp (Lamp::SelectRed, 0) == Led::On);
        // LAST STEP: SHIFT + step 1, dial, step 1 (p.53)
        r.with (Key::Shift, Key::Step1);
        expectEquals (r.m.getDisplay().param, juce::String ("LastStep"));
        expect (r.m.getLed (Key::Step1) == Led::Blink);
        r.dial (-5);
        r.tap (Key::Step1);
        expectEquals ((int) r.pat().lastStep, 11);
        expectEquals (r.pat().numSteps(), 22, "2 bars of 11");
        r.tap (Key::Step13);
        expect (! r.pat().parts[0].steps[11].on && ! r.pat().parts[0].steps[12].on, "keys past the last step do nothing");
        // STOP cancels a function being set up
        r.with (Key::Shift, Key::Step1);
        r.dial (3);
        r.tap (Key::StopCancel);
        expectEquals ((int) r.pat().lastStep, 11, "cancelled");
        // the 6A/6B pair: both on one step, only B sounds (p.48)
        r.blank();
        r.pat().parts[5].steps[0].on = true;
        r.pat().parts[6].steps[0].on = true;
        r.pat().parts[7].steps[2].on = true;
        r.tap (Key::PlayPause);
        r.run (0.3);
        expectEquals ((int) r.triggers (5).size(), 0, "6A doesn't sound with 6B");
        expectEquals ((int) r.triggers (6).size(), 1, "6B does");
        expectEquals ((int) r.triggers (7).size(), 1, "7A on its own sounds");
    }

    // ------------------------------------------------------------------ swing, roll, beat, length (p.25, 52)

    void timing()
    {
        beginTest ("SWING delays the even steps of parts with SWING SW on; ROLL plays 2-4 hits a step; BEAT and LENGTH");
        covers ({ Key::Roll });
        {
            Rig r;
            r.blank();
            for (int s = 0; s < 16; ++s)
                r.pat().parts[5].steps[(size_t) s].on = r.pat().parts[0].steps[(size_t) s].on = true;
            r.pat().swing = 66;
            r.pat().parts[0].swingSw = false;
            r.tap (Key::PlayPause);
            r.run (0.5);
            const auto hats = r.triggers (5), kicks = r.triggers (0);
            expect (hats.size() >= 3 && kicks.size() >= 3);
            expectWithinAbsoluteError (hats[1].pos, 1.32, 0.02, "the second 16th is late: 66 % swing");
            expectWithinAbsoluteError (hats[2].pos, 2.0, 0.02, "the odd steps stay put");
            expectWithinAbsoluteError (kicks[1].pos, 1.0, 0.02, "SWING SW off: straight");
            // triplets ignore swing
            r.tap (Key::StopCancel);
            r.pat().beat = Beat::EighthTriplet;
            r.pat().lastStep = 12;
            r.m.clearTriggerLog();
            r.tap (Key::PlayPause);
            r.run (0.4);
            expectWithinAbsoluteError (r.triggers (5)[1].pos, 1.0, 0.02, "no swing in 8Tri");
            expectWithinAbsoluteError (r.pat().stepQuarters(), 1.0 / 3.0, 1.0e-9, "an 8th triplet a step");
        }
        {
            Rig r;
            r.blank();
            r.pat().parts[0].steps[0].on = true;
            r.selectPart (0);
            r.tap (Key::Roll);
            expect (r.m.getLed (Key::Roll) == Led::On);
            for (int type : { 2, 3, 4 })
            {
                r.tap (Key::StopCancel);
                r.pat().rollType = (uint8_t) type;
                r.m.clearTriggerLog();
                r.tap (Key::PlayPause);
                r.run (0.1); // within step 1
                expectEquals (countTriggersAt (r.triggers (0), 0.0, 1.0), type, "roll type " + juce::String (type));
            }
            r.tap (Key::Roll);
            expect (! r.pat().parts[0].sound.roll);
        }
        {
            // LENGTH: 2 bars = 32 steps of 16ths; at 120 BPM a bar is 2 s
            Rig r;
            r.blank();
            r.pat().length = 2;
            r.pat().parts[0].steps[16].on = true;
            r.tap (Key::PlayPause);
            r.run (2.1);
            const auto k = r.triggers (0);
            expect (k.size() == 1 && std::abs (k[0].pos - 16.0) < 0.05, "the step in bar 2 plays after a bar");
            r.run (4.0);
            expectEquals ((int) r.triggers (0).size(), 2, "the pattern loops every 2 bars (4 s)");
            // ROLL TYPE page
            gotoPage (r, Key::ModePattern, 5);
            r.dial (1);
            expectEquals ((int) r.pat().rollType, 4);
            gotoPage (r, Key::ModePattern, 4);
            r.dial (1);
            expect (r.pat().beat == Beat::ThirtySecond && r.pat().lastStep == 16, "32 beat, last step 16");
            r.dial (1);
            expect (r.pat().beat == Beat::EighthTriplet && r.pat().lastStep == 12, "triplets: last step 12 (p.52)");
            gotoPage (r, Key::ModePattern, 3);
            r.dial (1);
            expectEquals ((int) r.pat().length, 3, "length 3");
            gotoPage (r, Key::ModePattern, 2);
            r.dial (10);
            expectEquals ((int) r.pat().swing, 60);
        }
    }

    // ------------------------------------------------------------------ accents (p.24)

    void accents()
    {
        beginTest ("ACCENT parts: accented steps louder by the accent LEVEL; ACCENT SW off parts aren't accented");
        Rig r;
        r.blank();
        auto& p = r.pat();
        for (int s = 0; s < 4; ++s)
            p.parts[0].steps[(size_t) s].on = p.parts[1].steps[(size_t) s].on = true;
        p.parts[(size_t) kDrumAccent].steps[0].on = true;
        p.parts[(size_t) kDrumAccent].steps[2].on = true;
        p.parts[(size_t) kDrumAccent].sound.level = 127;
        p.parts[1].accentSw = false;
        r.tap (Key::PlayPause);
        r.run (0.45);
        const auto k = r.triggers (0), s = r.triggers (1);
        expect (k.size() >= 4 && s.size() >= 4);
        expectEquals (k[0].velocity, 127, "accented, full accent level");
        expectEquals (k[1].velocity, 30, "unaccented");
        expectEquals (k[2].velocity, 127);
        expectEquals (s[0].velocity, 30, "ACCENT SW off: never accented");
        // level 0: no difference
        r.tap (Key::StopCancel);
        p.parts[(size_t) kDrumAccent].sound.level = 0;
        r.m.clearTriggerLog();
        r.tap (Key::PlayPause);
        r.run (0.3);
        expectEquals (r.triggers (0)[0].velocity, r.triggers (0)[1].velocity, "accent level 0: no difference");
        // the accent part's step keys show the accent pattern
        r.tap (Key::StopCancel);
        r.selectPart (kDrumAccent);
        expect (r.m.getLed (Key::Step1) == Led::On && r.m.getLed (Key::Step2) == Led::Off);
        // ACCENT SW in Part Edit, with the status row
        r.selectPart (1);
        gotoPage (r, Key::ModePartEdit, 1);
        r.dial (1);
        expect (p.parts[1].accentSw, "AccentSw on");
        expect (r.m.getDisplay().status[1] && r.m.getDisplay().statusRow == 2, "the ACCENT row shows it");
        gotoPage (r, Key::ModePartEdit, 2);
        r.dial (-1);
        expect (! p.parts[1].swingSw, "SwingSw off");
        gotoPage (r, Key::ModePartEdit, 3);
        r.dial (-1);
        expect (p.parts[1].outBus34, "OutBus 3/4");
        expectEquals (r.m.getDisplay().value, juce::String ("3/4"));
    }

    // ------------------------------------------------------------------ mute and solo (p.23)

    void muteSolo()
    {
        beginTest ("PART MUTE + part key mutes; SOLO + part key solos; SOLO pressed alone undoes both");
        covers ({ Key::PartMute, Key::Solo });
        Rig r;
        r.blank();
        r.pat().parts[0].steps[0].on = true;
        r.pat().parts[1].steps[0].on = true;
        r.m.press (Key::PartMute);
        r.tap (Key::Drum1);
        expect (r.m.getLed (Key::Drum1) == Led::Off && r.m.getLed (Key::Drum2) == Led::On, "while held: unmuted keys lit");
        r.m.release (Key::PartMute);
        expect (r.pat().parts[0].muted && r.m.getLed (Key::PartMute) == Led::On, "muted, PART MUTE lit");
        expect (! r.m.getDisplay().status[0] && r.m.getDisplay().status[1], "the PLAY row");
        // a muted part makes no sound
        r.pat().parts[1].steps[0].on = false;
        r.tap (Key::PlayPause);
        r.run (0.3);
        expect (Rig::rms (r.L) < 1.0e-4, "muted kick: silence");
        r.tap (Key::StopCancel);
        // SOLO
        r.pat().parts[1].steps[0].on = true;
        r.m.press (Key::Solo);
        r.tap (Key::Drum2);
        r.m.release (Key::Solo);
        expect (r.m.getLed (Key::Solo) == Led::On && r.m.getLed (Key::Drum2) == Led::On);
        expect (! r.m.getDisplay().status[0] && r.m.getDisplay().status[1] && ! r.m.getDisplay().status[2], "only the soloed part plays");
        r.m.press (Key::Solo);
        r.tap (Key::Drum3);
        r.m.release (Key::Solo);
        expect (r.m.getDisplay().status[2], "holding SOLO adds parts");
        r.tap (Key::Solo); // pressed and let go: everything back
        expect (r.m.getLed (Key::Solo) == Led::Off && ! r.pat().parts[0].muted, "solo and mute both defeated");
        // SHIFT + part key while muting: the whole drum group
        r.m.press (Key::PartMute);
        r.with (Key::Shift, Key::Drum1);
        r.m.release (Key::PartMute);
        bool all = true;
        for (int p = 0; p < kNumDrumParts; ++p)
            all = all && r.pat().parts[(size_t) p].muted;
        expect (all && ! r.pat().parts[(size_t) kFirstSynth].muted, "all drum parts muted, synths not");
    }

    // ------------------------------------------------------------------ keyboard, octave, transpose (p.22, 24)

    void keyboardAndTranspose()
    {
        beginTest ("KEYBOARD: the step keys play the synth part (key 4 = C4), select keys change octave; TRANSPOSE");
        covers ({ Key::Keyboard });
        Rig r;
        r.blank();
        r.selectPart (0);
        r.tap (Key::Keyboard);
        expect (! r.m.isKeyboardOn(), "only with a synth part");
        r.selectPart (kFirstSynth);
        r.tap (Key::Keyboard);
        expect (r.m.isKeyboardOn() && r.m.getLed (Key::Keyboard) == Led::On);
        r.m.clearTriggerLog();
        r.m.press (Key::Step4);
        r.run (0.05);
        expect (r.m.getLed (Key::Step4) == Led::On, "the key of the note lights");
        r.m.release (Key::Step4);
        r.tap (Key::Step1);
        r.tap (Key::Step16);
        const auto t = r.triggers (kFirstSynth);
        expect (t.size() == 3 && t[0].note == 60 && t[1].note == 57 && t[2].note == 72, "key 1 = A3, key 4 = C4, key 16 = C5");
        expect (r.m.getLamp (Lamp::SelectRed, 4) == Led::On, "octave 5 of 8");
        r.tap (Key::SelectRight);
        r.tap (Key::Step4);
        expectEquals (r.triggers (kFirstSynth).back().note, 72, "an octave up");
        r.tap (Key::SelectLeft);
        // a drum part cancels the keyboard, a synth part brings it back
        r.selectPart (0);
        expect (! r.m.isKeyboardOn());
        r.selectPart (kFirstSynth + 1);
        expect (r.m.isKeyboardOn(), "back on for a synth part");
        // TRANSPOSE: hold SOLO + dial, or + a step key (key 4 = no transpose)
        r.dialWith (Key::Solo, 5);
        expectEquals (r.m.getTranspose(), 5);
        expect (r.m.getLed (Key::Solo) == Led::Blink, "SOLO blinks while transposed");
        r.with (Key::Solo, Key::Step7);
        expectEquals (r.m.getTranspose(), 3, "step key 7 = +3");
        r.with (Key::Solo, Key::Step4);
        expectEquals (r.m.getTranspose(), 0);
        expect (r.m.getLed (Key::Solo) == Led::Off);
        // a transposed sequence
        r.tap (Key::Keyboard);
        r.pat().parts[(size_t) kFirstSynth].steps[0] = { true, 60, 2 };
        r.dialWith (Key::Solo, 12);
        r.m.clearTriggerLog();
        r.tap (Key::PlayPause);
        r.run (0.1);
        expectEquals (r.triggers (kFirstSynth).at (0).note, 72, "the phrase plays an octave up");
    }

    // ------------------------------------------------------------------ recording (p.47-48)

    void recording()
    {
        beginTest ("realtime recording: REC then PLAY; drum keys and the keyboard record; REC again ends it; SHIFT + RESET erases");
        covers ({ Key::Rec });
        Rig r;
        r.blank();
        r.selectPart (0);
        r.tap (Key::Rec);
        expect (r.m.getLed (Key::Rec) == Led::On && r.m.getLed (Key::PlayPause) == Led::Blink, "record-ready: REC lit, PLAY blinks");
        r.tap (Key::PlayPause);
        expect (r.m.isRecording());
        r.run (0.5); // 120 BPM: 4 steps
        r.tap (Key::Drum1);
        expect (r.pat().parts[0].steps[4].on, "recorded on step 5");
        // the keyboard: a note with its gate
        r.selectPart (kFirstSynth);
        r.tap (Key::Keyboard);
        r.run (0.5);
        r.m.press (Key::Step4);
        r.run (0.25);
        r.m.release (Key::Step4);
        const auto& st = r.pat().parts[(size_t) kFirstSynth].steps[8];
        expect (st.on && st.note == 60, "the note on step 9");
        expectWithinAbsoluteError ((int) st.gate, 8, 1, "held two steps: gate 2.0");
        r.tap (Key::Rec);
        expect (! r.m.isRecording() && r.m.isPlaying(), "REC again: recording ends, playback goes on");
        // erase: SHIFT + hold RESET/ERASE while the steps go by
        r.tap (Key::Keyboard);
        r.selectPart (0);
        for (int s = 0; s < 16; ++s)
            r.pat().parts[0].steps[(size_t) s].on = true;
        r.m.press (Key::Shift);
        r.m.press (Key::ResetErase);
        r.run (2.1);
        r.m.release (Key::ResetErase);
        r.m.release (Key::Shift);
        int left = 0;
        for (int s = 0; s < 16; ++s)
            left += r.pat().parts[0].steps[(size_t) s].on ? 1 : 0;
        expectEquals (left, 0, "erased as they passed");
    }

    // ------------------------------------------------------------------ step edit (p.49-51)

    void stepEditing()
    {
        beginTest ("STEP EDIT: step recording with REST/TIE; NoteNo and GateTime by step");
        Rig r;
        r.blank();
        r.selectPart (kFirstSynth);
        r.tap (Key::Keyboard);
        gotoPage (r, Key::ModeStepEdit, 0);
        r.tap (Key::Rec);
        expect (r.m.getLed (Key::Rec) == Led::On, "step-record ready");
        r.tap (Key::Step4);       // C4 on step 1
        r.tap (Key::Step6);       // D4 on step 2
        r.tap (Key::PartMute);    // a rest on step 3
        r.m.press (Key::Step8);   // E4 on step 4 ...
        r.tap (Key::PartMute);    // ... tied through step 5
        r.m.release (Key::Step8);
        const auto& st = r.pat().parts[(size_t) kFirstSynth].steps;
        expect (st[0].on && st[0].note == 60 && st[1].note == 62 && ! st[2].on && st[3].note == 64, "notes and the rest");
        expectEquals ((int) st[3].gate, 7, "the tie: 0.75 + 1 step");
        expect (! st[4].on);
        r.tap (Key::StopCancel);
        // NoteNo / GateTime: pick a step, turn the dial
        r.tap (Key::Keyboard);
        gotoPage (r, Key::ModeStepEdit, 1);
        r.tap (Key::Step2);
        expect (r.m.getLed (Key::Step2) == Led::Blink, "the step being edited blinks");
        r.dial (3);
        expectEquals ((int) st[1].note, 65);
        expectEquals (r.m.getDisplay().value, juce::String ("F4"));
        gotoPage (r, Key::ModeStepEdit, 2);
        r.dial (5);
        expectEquals ((int) st[1].gate, 8);
        expectEquals (r.m.getDisplay().value, juce::String ("2.00"));
        expect (st[1].on, "(no trigger toggling in STEP EDIT)");
        // with a gate longer than the step, the next note plays legato (p.51)
        r.tap (Key::ModePattern);
        r.m.clearTriggerLog();
        r.tap (Key::PlayPause);
        r.run (0.4);
        const auto t = r.triggers (kFirstSynth);
        expect (t.size() >= 2 && ! t[0].legato && t[1].legato == false, "step 2 after a 0.75 gate: retriggers");
        expect (t.size() >= 3 && t[2].legato == false, "");
        juce::ignoreUnused (t);
    }

    // ------------------------------------------------------------------ motion sequence (p.58-61)

    void motion()
    {
        beginTest ("MOTION SEQ: record a knob for one pass (REC goes off by itself), play back SMOOTH / TRIG HOLD, edit, clear");
        covers ({ Key::PartMotion, Key::FxMotion });
        Rig r;
        r.blank();
        r.selectPart (0);
        r.pat().parts[0].steps[0].on = true;
        r.tap (Key::PartMotion);
        expect (r.m.getLamp (Lamp::MotionSmooth) == Led::On, "SMOOTH");
        r.tap (Key::PartMotion);
        expect (r.m.getLamp (Lamp::MotionTrigHold) == Led::On, "TRIG HOLD");
        r.tap (Key::PartMotion);
        expect (r.m.getLamp (Lamp::MotionSmooth) == Led::Off && r.m.getLamp (Lamp::MotionTrigHold) == Led::Off, "off");
        r.tap (Key::PartMotion); // smooth
        r.tap (Key::Rec);
        r.tap (Key::PlayPause);
        // turn EG TIME from 20 up to 120 over the bar
        for (int i = 0; i < 16; ++i)
        {
            r.m.setKnob (Knob::EgTime, 20 + i * 100 / 15);
            r.run (0.125);
        }
        r.run (0.3);
        expect (! r.m.isRecording(), "REC went off after one cycle");
        const int slot = r.pat().findMotion (0, Param::EgTime);
        expect (slot >= 0, "a motion sequence for EG TIME");
        if (slot >= 0)
        {
            const auto& v = r.pat().motion[(size_t) slot].values;
            expect (v[0] >= 15 && v[0] <= 30 && v[15] >= 110, "the knob's path, step by step");
        }
        // smooth playback moves the effective value along the bar
        r.m.setKnob (Knob::EgTime, 64);
        r.tap (Key::ResetErase);
        r.run (0.06);
        const int early = r.m.getEffectiveSounds()[0].egTime;
        r.run (1.5);
        const int late = r.m.getEffectiveSounds()[0].egTime;
        expect (early < 40 && late > 90, "smooth: " + juce::String (early) + " -> " + juce::String (late));
        // trig hold: the value at each note's step, held
        r.tap (Key::PartMotion);
        r.tap (Key::ResetErase);
        r.run (0.6);
        expect (r.m.getEffectiveSounds()[0].egTime < 40, "trig hold: the value from step 1's note, held");
        // FreeMot
        r.tap (Key::StopCancel);
        r.with (Key::Shift, Key::PartMotion);
        expectEquals (r.m.getDisplay().value, juce::String ("23"), "23 motion sequences free");
        // edit in STEP EDIT: MotDest shows EGTime, MotValue edits a step, down past 0 is Disabl
        gotoPage (r, Key::ModeStepEdit, 3);
        r.dial (1);
        expectEquals (r.m.getDisplay().value, juce::String ("EGTime"));
        gotoPage (r, Key::ModeStepEdit, 4);
        r.tap (Key::Step3);
        r.dial (-200);
        expectEquals (r.m.getDisplay().value, juce::String ("Disabl"));
        expectEquals ((int) r.pat().motion[(size_t) slot].values[2], -1);
        // CLEAR MOTION: SHIFT + step 6, (part already chosen), step 6
        r.tap (Key::ModePattern);
        r.with (Key::Shift, Key::Step6);
        r.tap (Key::Step6);
        expect (r.pat().findMotion (0, Param::EgTime) < 0, "cleared");
        // FX motion: FX MOTION SEQ lights and records FX EDIT 1
        r.tap (Key::FxMotion);
        expect (r.m.getLed (Key::FxMotion) == Led::On);
        r.tap (Key::Rec);
        r.tap (Key::PlayPause);
        r.m.setKnob (Knob::FxEdit1, 10);
        r.run (2.2);
        expect (r.pat().findMotion (kFxTarget, Param::FxEdit1) >= 0, "effect motion recorded");
        r.tap (Key::StopCancel);
        r.tap (Key::FxMotion);
        expect (r.m.getLed (Key::FxMotion) == Led::Off);
    }

    // ------------------------------------------------------------------ write, rename, compare, protect (p.57, 64, 75)

    void writing()
    {
        beginTest ("WRITE: blocked by Memory Protect; WriteTo + dial + WRITE saves; SHIFT + WRITE renames; COMPARE");
        covers ({ Key::WriteRename });
        Rig r;
        r.tap (Key::WriteRename);
        expectEquals (r.m.getDisplay().param, juce::String ("Protect"), "factory setting: memory protect on");
        gotoPage (r, Key::ModeGlobal, 2);
        r.dial (-1);
        expect (! r.m.getMemory().global.protect);
        r.tap (Key::ModePattern);
        r.tap (Key::Step3); // an edit
        r.tap (Key::WriteRename);
        expectEquals (r.m.getDisplay().param, juce::String ("WriteTo"));
        expect (r.m.getLed (Key::WriteRename) == Led::Blink);
        r.dial (2);
        expectEquals (r.m.getDisplay().value, juce::String ("A.03"));
        r.tap (Key::WriteRename);
        expectEquals (r.m.getPatternIndex(), 2, "the destination is selected");
        expect (r.m.getMemory().patterns[2] == r.m.getEditPattern(), "written");
        expect (r.m.takeMemoryChanged(), "the engine is told to save");
        // rename
        r.with (Key::Shift, Key::WriteRename);
        expectEquals (r.m.getDisplay().param, juce::String ("Rename"));
        r.dial (1); // 'E' -> 'F'
        r.tap (Key::CursorRight);
        r.tap (Key::WriteRename);
        r.tap (Key::WriteRename);
        expect (r.m.getMemory().patterns[2].getName().startsWith ("F"), "renamed: " + r.m.getMemory().patterns[2].getName());
        // compare: SHIFT + PATTERN plays the stored version
        r.tap (Key::Step5);
        const bool edited = r.pat().parts[(size_t) r.m.getSelectedPart()].steps[4].on;
        r.with (Key::Shift, Key::ModePattern);
        expect (r.m.getLed (Key::ModePattern) == Led::Blink, "PATTERN blinks in compare");
        expect (r.pat().parts[(size_t) r.m.getSelectedPart()].steps[4].on != edited, "hearing the stored pattern");
        r.tap (Key::ModePattern);
        expect (r.pat().parts[(size_t) r.m.getSelectedPart()].steps[4].on == edited, "back to the edits");
    }

    // ------------------------------------------------------------------ SHIFT + step key functions (p.54-57)

    void shiftFunctions()
    {
        beginTest ("MOVE / REPEAT / RANDOM, SHIFT NOTE, COPY PART / SOUND, CLEAR MOTION / PART / PATTERN; stopped only");
        Rig r;
        r.blank();
        r.selectPart (0);
        r.pat().parts[0].steps[0].on = true;
        r.pat().parts[0].steps[1].on = true;
        // MOVE +3
        r.with (Key::Shift, Key::Step2);
        expectEquals (r.m.getDisplay().value, juce::String ("Move"));
        r.tap (Key::Step2);
        r.dial (3);
        r.tap (Key::Step2);
        expect (r.pat().parts[0].steps[3].on && r.pat().parts[0].steps[4].on && ! r.pat().parts[0].steps[0].on, "moved 3 steps");
        // REPEAT the first 4 steps
        r.blank();
        r.selectPart (0);
        r.pat().parts[0].steps[0].on = true;
        r.with (Key::Shift, Key::Step2);
        r.dial (1);
        expectEquals (r.m.getDisplay().value, juce::String ("Repeat"));
        r.tap (Key::Step2);
        r.dial (3); // 4 steps
        r.tap (Key::Step2);
        expect (r.pat().parts[0].steps[4].on && r.pat().parts[0].steps[8].on && r.pat().parts[0].steps[12].on, "repeated every 4 steps");
        // RANDOM keeps the number of hits
        r.with (Key::Shift, Key::Step2);
        r.dial (2);
        r.tap (Key::Step2);
        r.tap (Key::Step2);
        int hits = 0;
        for (int s = 0; s < 16; ++s)
            hits += r.pat().parts[0].steps[(size_t) s].on ? 1 : 0;
        expectEquals (hits, 4, "random: the same hits, moved");
        // SHIFT NOTE +5 on the synth parts
        r.pat().parts[(size_t) kFirstSynth].steps[0] = { true, 60, 3 };
        r.with (Key::Shift, Key::Step3);
        r.tap (Key::Step3);
        r.dial (5);
        r.tap (Key::Step3);
        expectEquals ((int) r.pat().parts[(size_t) kFirstSynth].steps[0].note, 65);
        // COPY PART: drum 1 onto drum 2 (destination chosen first)
        r.selectPart (1);
        r.with (Key::Shift, Key::Step4);
        r.tap (Key::Drum1);
        r.tap (Key::Step4);
        expect (r.pat().parts[1].steps == r.pat().parts[0].steps && r.pat().parts[1].sound == r.pat().parts[0].sound, "copied");
        // COPY SOUND: not between a drum and a synth part
        r.selectPart (kFirstSynth + 1);
        r.with (Key::Shift, Key::Step5);
        r.tap (Key::Drum1);
        r.tap (Key::Step5);
        expectEquals (r.m.getDisplay().param, juce::String ("Can't"));
        // CLEAR PART: synth steps back to C4 / 0.75
        r.selectPart (kFirstSynth);
        r.with (Key::Shift, Key::Step7);
        r.tap (Key::Step7);
        const auto& st = r.pat().parts[(size_t) kFirstSynth].steps[0];
        expect (! st.on && st.note == 60 && st.gate == 3, "cleared to C4, gate 0.75");
        // CLEAR PATTERN
        r.pat().tempo = 99;
        r.with (Key::Shift, Key::Step8);
        r.tap (Key::Step8);
        expect (r.pat() == initPattern(), "the pattern is initialized");
        // not while playing
        r.tap (Key::PlayPause);
        r.with (Key::Shift, Key::Step2);
        expectEquals (r.m.getDisplay().param, juce::String ("Stop"));
        r.tap (Key::StopCancel);
    }

    // ------------------------------------------------------------------ song mode (p.65-73)

    void songs()
    {
        beginTest ("SONG: positions, insert / delete / clear, note offset, mute hold, next song, FF/REW, event recording");
        Rig r;
        gotoPage (r, Key::ModeSong, 0);
        expectEquals (r.m.getDisplay().value, juce::String ("S.01"));
        expect (r.m.getDisplay().songInd);
        r.dial (2);
        expectEquals (r.m.getSongIndex(), 2, "an empty song");
        // positions: Pattern page, dial a pattern, select > to the next position
        gotoPage (r, Key::ModeSong, 3);
        expectEquals (r.m.getDisplay().value, juce::String ("End"));
        r.dial (1);
        expectEquals (r.m.getDisplay().value, juce::String ("A.01"));
        r.tap (Key::SelectRight);
        r.dial (3);
        expectEquals (r.m.getEditSong().numPositions(), 2);
        // note offset for position 2
        gotoPage (r, Key::ModeSong, 4);
        r.dial (-3);
        expectEquals ((int) r.m.getEditSong().positions[1].noteOffset, -3);
        // insert at 1 (a copy), delete it again
        r.tap (Key::SelectLeft);
        r.with (Key::Shift, Key::Step9);
        r.tap (Key::Step9);
        expectEquals (r.m.getEditSong().numPositions(), 3, "inserted");
        r.with (Key::Shift, Key::Step10);
        r.tap (Key::Step10);
        expectEquals (r.m.getEditSong().numPositions(), 2, "deleted");
        // play: position 1 (1 bar of Electro1: 2 bars long) then position 2, then the end
        r.m.editSong().positions[0] = { 6, 0 }; // Acid1: 1 bar at 130
        r.m.editSong().positions[1] = { 6, 2 };
        r.m.editSong().tempoLock = true;
        r.m.editSong().tempo = 120;
        r.m.clearTriggerLog();
        r.tap (Key::PlayPause);
        r.run (2.1);
        expectEquals (r.m.getSongPosition(), 1, "the second position after a bar");
        const auto syn = r.triggers (kFirstSynth);
        expect (! syn.empty() && syn.back().note == syn.front().note + 2, "note offset +2 in position 2");
        r.run (2.0);
        expect (! r.m.isPlaying(), "the song ended: playback stops");
        // MUTE HOLD and NEXT SONG pages
        gotoPage (r, Key::ModeSong, 5);
        r.dial (1);
        expect (r.m.getEditSong().muteHold);
        gotoPage (r, Key::ModeSong, 6);
        r.dial (1);
        expectEquals (r.m.getDisplay().value, juce::String ("S.01"));
        r.dial (-1);
        expectEquals (r.m.getDisplay().value, juce::String ("Off"));
        // TEMPO LOCK is kept per song
        r.tap (Key::TempoLock);
        expect (! r.m.getEditSong().tempoLock);
        r.tap (Key::TempoLock);
        // event recording: REC + PLAY, hit drum 1; it plays back next time
        r.m.editSong().events.reserve (kMaxEvents);
        gotoPage (r, Key::ModeSong, 2);
        r.selectPart (3);
        r.tap (Key::Rec);
        r.tap (Key::PlayPause);
        r.run (0.5);
        r.tap (Key::Drum4);
        r.run (0.2);
        r.tap (Key::StopCancel);
        expectEquals ((int) r.m.getEditSong().events.size(), 1, "one drum event");
        r.m.clearTriggerLog();
        r.tap (Key::PlayPause);
        r.run (0.8);
        bool replayed = false;
        for (auto& t : r.triggers (3))
            replayed = replayed || t.pos > 0;
        expect (replayed, "the event plays back");
        r.tap (Key::StopCancel);
        // CLEAR SONG
        r.with (Key::Shift, Key::Step11);
        r.tap (Key::Step11);
        expectEquals (r.m.getEditSong().numPositions(), 0);
    }

    // ------------------------------------------------------------------ pattern set (p.62-63)

    void patternSet()
    {
        beginTest ("PATTERN SET: hold + step key picks a pattern; select keys choose the group; dial assigns; SHIFT holds; chain play");
        covers ({ Key::PatternSet });
        Rig r;
        r.m.press (Key::PatternSet);
        r.tap (Key::Step3);
        r.m.release (Key::PatternSet);
        expectEquals (r.m.getPatternIndex(), 2, "group 1 key 3 = A.03");
        // assign: hold, press the key, turn the dial
        r.m.press (Key::PatternSet);
        r.tap (Key::Step16);
        r.dial (5);
        expectEquals (r.m.getDisplay().value, juce::String ("A.21"));
        r.tap (Key::SelectRight);
        expect (r.m.getLamp (Lamp::SelectRed, 1) == Led::On, "group 2");
        r.tap (Key::SelectLeft);
        r.tap (Key::WriteRename);
        expectEquals (r.m.getDisplay().param, juce::String ("PtSetWr?"));
        r.dial (1);
        r.tap (Key::WriteRename);
        r.m.release (Key::PatternSet);
        expectEquals ((int) r.m.getMemory().patternSet[15], 20);
        // hold mode and chain play: patterns 1-3 loop
        r.with (Key::Shift, Key::PatternSet);
        expect (r.m.getLed (Key::PatternSet) == Led::Blink, "held");
        r.m.press (Key::Step1);
        r.tap (Key::Step3);
        r.m.release (Key::Step1);
        expectEquals (r.m.getPatternIndex(), 0);
        r.m.getMemory().patterns[0].tempo = r.m.getMemory().patterns[1].tempo = r.m.getMemory().patterns[2].tempo = 120;
        r.m.getMemory().patterns[0].length = r.m.getMemory().patterns[1].length = r.m.getMemory().patterns[2].length = 1;
        r.m.selectPattern (0);
        r.tap (Key::PlayPause);
        r.run (2.05);
        expectEquals (r.m.getPatternIndex(), 1);
        r.run (2.0);
        expectEquals (r.m.getPatternIndex(), 2);
        r.run (2.0);
        expectEquals (r.m.getPatternIndex(), 0, "and round again");
        r.tap (Key::PatternSet);
        expect (r.m.getLed (Key::PatternSet) == Led::Off, "hold off");
        r.tap (Key::StopCancel);
    }

    // ------------------------------------------------------------------ effect and sound keys (p.11)

    void effectsAndSoundKeys()
    {
        beginTest ("EFFECT: EDIT SELECT, TYPE, FX CHAIN; part keys: AMP EG, FX SELECT / SEND, MOD TYPE / DEST, BPM SYNC, FILTER TYPE");
        covers ({ Key::FxEditSelect, Key::FxChain, Key::AmpEg, Key::FxSelect, Key::FxSend, Key::ModType, Key::ModDest, Key::ModBpmSync, Key::FilterType });
        coversKnob ({ Knob::FxType, Knob::FxEdit1, Knob::FxEdit2 });
        Rig r;
        r.blank();
        expect (r.m.getLamp (Lamp::EditSelect, 0) == Led::On);
        r.tap (Key::FxEditSelect);
        expect (r.m.getLamp (Lamp::EditSelect, 1) == Led::On, "FX2 selected");
        for (int t = 0; t < kNumFxTypes; ++t)
        {
            r.m.setKnob (Knob::FxType, t * 8 + 4);
            expect (r.pat().fx[1].type == (FxType) t, mapping::fxTypeName ((FxType) t));
        }
        r.m.setKnob (Knob::FxEdit1, 33);
        r.m.setKnob (Knob::FxEdit2, 77);
        expect (r.pat().fx[1].edit1 == 33 && r.pat().fx[1].edit2 == 77);
        r.tap (Key::FxEditSelect);
        r.tap (Key::FxEditSelect);
        expect (r.m.getLamp (Lamp::EditSelect, 0) == Led::On, "back to FX1");
        // chain: none, 1>2, 2>3, 1>2>3
        r.tap (Key::FxChain);
        expect (r.pat().chain == Chain::OneTwo && r.m.getLamp (Lamp::ChainOneTwo) == Led::On && r.m.getLamp (Lamp::ChainTwoThree) == Led::Off);
        r.tap (Key::FxChain);
        expect (r.pat().chain == Chain::TwoThree && r.m.getLamp (Lamp::ChainTwoThree) == Led::On);
        r.tap (Key::FxChain);
        expect (r.pat().chain == Chain::All && r.m.getLamp (Lamp::ChainOneTwo) == Led::On && r.m.getLamp (Lamp::ChainTwoThree) == Led::On);
        r.tap (Key::FxChain);
        expect (r.pat().chain == Chain::None);
        // part keys
        r.selectPart (kFirstSynth);
        auto& s = r.pat().parts[(size_t) kFirstSynth].sound;
        r.tap (Key::AmpEg);
        expect (s.ampEg == AmpEg::Gate && r.m.getLamp (Lamp::AmpGate) == Led::On);
        r.tap (Key::AmpEg);
        expect (s.ampEg == AmpEg::Decay && r.m.getLamp (Lamp::AmpDecay) == Led::On);
        r.tap (Key::FxSelect);
        expect (s.fxSelect == 1 && r.m.getLamp (Lamp::FxSelectLamp, 1) == Led::On);
        r.tap (Key::FxSend);
        expect (s.fxSend && r.m.getLed (Key::FxSend) == Led::On);
        for (int t = 1; t <= 5; ++t)
        {
            r.tap (Key::ModType);
            expect ((int) s.modType == (2 + t) % 5 && r.m.getLamp (Lamp::ModTypeLamp, (int) s.modType) == Led::On, "mod type");
        }
        for (int d = 1; d <= 6; ++d)
            r.tap (Key::ModDest);
        expect (s.modDest == ModDest::Pitch, "six destinations on a synth part");
        r.tap (Key::ModBpmSync);
        expect (s.modBpmSync && r.m.getLed (Key::ModBpmSync) == Led::On);
        for (int f = 1; f <= 4; ++f)
        {
            r.tap (Key::FilterType);
            expect ((int) s.filterType == f % 4 && r.m.getLamp (Lamp::FilterTypeLamp, f % 4) == Led::On);
        }
        // a drum part: three destinations; no filter
        r.selectPart (0);
        r.tap (Key::ModDest);
        r.tap (Key::ModDest);
        r.tap (Key::ModDest);
        expect (r.pat().parts[0].sound.modDest == ModDest::Pitch, "pitch, amp, pan on a drum part");
        r.tap (Key::FilterType);
        expect (r.m.getLamp (Lamp::FilterTypeLamp, 1) == Led::Off, "no filter on drums");
        // FX SEND and the output bus actually route the sound
        r.blank();
        r.pat().parts[0].steps[0].on = true;
        r.pat().parts[0].outBus34 = true;
        r.tap (Key::PlayPause);
        r.run (0.3);
        expect (Rig::rms (r.I3) > 1.0e-3 && Rig::rms (r.L) < 1.0e-5, "OutBus 3/4: the individual outs, not L/R");
    }

    // ------------------------------------------------------------------ knobs (p.11, 29-33)

    void knobs()
    {
        beginTest ("every sound knob sets its parameter for the selected part, with the knob indicator and original value");
        coversKnob ({ Knob::PitchGlide, Knob::EgTime, Knob::Pan, Knob::Level, Knob::ModSpeed, Knob::ModDepth, Knob::OscType,
                      Knob::OscEdit1, Knob::OscEdit2, Knob::Cutoff, Knob::EgInt, Knob::Resonance, Knob::Drive });
        Rig r;
        r.blank();
        r.selectPart (kFirstSynth);
        const Knob ks[] = { Knob::PitchGlide, Knob::EgTime, Knob::Pan, Knob::Level, Knob::ModSpeed, Knob::ModDepth, Knob::OscEdit1,
                            Knob::OscEdit2, Knob::Cutoff, Knob::EgInt, Knob::Resonance, Knob::Drive };
        int v = 11;
        for (auto k : ks)
        {
            r.m.setKnob (k, v);
            expectEquals (r.m.getKnobValue (k), v, knobName (k));
            const auto d = r.m.getDisplay();
            expect (d.knobInd && d.param == knobName (k), juce::String ("display for ") + knobName (k));
            v += 7;
        }
        const auto& s = r.pat().parts[(size_t) kFirstSynth].sound;
        expect (s.glide == 11 && s.egTime == 18 && s.pan == 25 && s.level == 32 && s.cutoff == 67, "into the part's sound");
        for (int t = 0; t < kNumOscTypes; ++t)
        {
            r.m.setKnob (Knob::OscType, t * 8 + 4);
            expect (s.oscType == (OscType) t, mapping::oscTypeName ((OscType) t));
        }
        // the display goes back after the knob rests
        r.run (1.6);
        expect (! r.m.getDisplay().knobInd, "the knob indicator goes out");
        // original value indicator: lit when the knob matches the stored pattern
        Rig r2;
        r2.selectPart (0);
        const int stored = r2.m.getKnobValue (Knob::EgTime);
        r2.m.setKnob (Knob::EgTime, stored == 10 ? 11 : 10);
        expect (! r2.m.getDisplay().originalInd);
        r2.m.setKnob (Knob::EgTime, stored);
        expect (r2.m.getDisplay().originalInd, "back on the stored value");
        // drums: PITCH (not glide); the synth-only knobs leave a drum alone
        r2.m.setKnob (Knob::PitchGlide, 103);
        expectEquals ((int) r2.pat().parts[0].sound.pitch, 103);
        const auto before = r2.pat().parts[0].sound;
        r2.m.setKnob (Knob::Cutoff, 3);
        r2.m.setKnob (Knob::OscType, 100);
        expect (r2.pat().parts[0].sound == before, "synth controls do nothing on a drum part");
        // accent part: LEVEL is the accent amount
        r2.selectPart (kSynthAccent);
        r2.m.setKnob (Knob::Level, 90);
        expectEquals ((int) r2.pat().parts[(size_t) kSynthAccent].sound.level, 90);
        // WAVE (part edit) for drums and synths
        r2.selectPart (0);
        gotoPage (r2, Key::ModePartEdit, 0);
        r2.dial (1);
        expect (r2.m.getDisplay().waveInd, "WAVE indicator");
        r2.selectPart (kFirstSynth);
        r2.m.setKnob (Knob::OscType, (int) OscType::Dual * 8 + 4);
        r2.dial (19);
        expectEquals ((int) r2.pat().parts[(size_t) kFirstSynth].sound.wave, 19, "20 combinations");
        expectEquals (r2.m.getDisplay().param, juce::String ("Sin-Ns"));
        gotoPage (r2, Key::ModePartEdit, 4);
        r2.dial (12);
        expectEquals ((int) r2.pat().parts[(size_t) kFirstSynth].sound.synthTune, 12, "SYNTH TUNE");
    }

    // ------------------------------------------------------------------ volume, tube, audio in, auto BPM (p.9, 21, 26)

    void audioIn()
    {
        beginTest ("MASTER VOLUME, TUBE GAIN (more drive, more level), AUDIO IN LEVEL / THRU, AUTO BPM SCAN");
        covers ({ Key::AudioInThru, Key::AutoBpm });
        coversKnob ({ Knob::MasterVolume, Knob::TubeGain, Knob::AudioInLevel });
        // a steady sine in
        std::vector<float> sine ((size_t) (kSr * 8));
        for (size_t i = 0; i < sine.size(); ++i)
            sine[i] = 0.3f * (float) std::sin (6.283185307179586 * 220.0 * i / kSr);
        Rig r;
        r.blank();
        r.run (0.2, &sine);
        expect (Rig::rms (r.L) < 1.0e-5, "the input isn't heard without THRU");
        expect (r.m.getDisplay().inputLevel > 0.25f, "the input meter");
        r.tap (Key::AudioInThru);
        expect (r.m.getLed (Key::AudioInThru) == Led::On && r.m.wantsAudioInput());
        r.clearAudio();
        r.run (0.5, &sine);
        const double thru = Rig::rms (r.L, 4800);
        expect (thru > 0.02, "THRU: heard");
        r.m.setKnob (Knob::AudioInLevel, 30);
        r.clearAudio();
        r.run (0.5, &sine);
        expect (Rig::rms (r.L, 4800) < thru * 0.5, "AUDIO IN LEVEL turns it down");
        r.m.setKnob (Knob::AudioInLevel, 80);
        r.m.setKnob (Knob::MasterVolume, 50);
        r.clearAudio();
        r.run (0.5, &sine);
        const double half = Rig::rms (r.L, 4800);
        expect (half < thru * 0.5, "MASTER VOLUME");
        // TUBE GAIN: louder and distorted (energy away from 220 Hz)
        r.m.setKnob (Knob::MasterVolume, 100);
        r.m.setKnob (Knob::TubeGain, 127);
        r.clearAudio();
        r.run (0.5, &sine);
        expect (Rig::rms (r.L, 4800) > thru * 1.2, "the tubes raise the level");
        r.tap (Key::AudioInThru);
        r.m.setKnob (Knob::TubeGain, 0);
        // AUTO BPM: clicks at 128 BPM into the input
        std::vector<float> clicks ((size_t) (kSr * 8), 0.0f);
        const double beat = 60.0 / 128.0;
        for (double t = 0.05; t < 8.0; t += beat)
            for (int i = 0; i < 400; ++i)
                clicks[(size_t) (t * kSr) + (size_t) i] = (float) (0.8 * std::exp (-i / 60.0) * std::sin (i * 0.9));
        Rig b;
        b.tap (Key::AutoBpm);
        expect (b.m.getLed (Key::AutoBpm) == Led::On, "scanning");
        b.run (7.0, &clicks);
        expect (b.m.getLed (Key::AutoBpm) == Led::Blink, "found: the key blinks");
        b.tap (Key::AutoBpm);
        expectWithinAbsoluteError (b.m.getTempo(), 128.0, 1.5, "the detected tempo");
    }

    // ------------------------------------------------------------------ global mode (p.75)

    void globalMode()
    {
        beginTest ("GLOBAL: metronome (count-in for Rec1), master tune (synths, not drums), protect, arp control; GLOBAL WRITE");
        Rig r;
        r.blank();
        gotoPage (r, Key::ModeGlobal, 0);
        r.dial (4);
        expect (r.m.getMetronome() == Metronome::On);
        r.tap (Key::PlayPause);
        r.run (0.6);
        expect (Rig::rms (r.L) > 1.0e-3, "the metronome clicks on an empty pattern");
        r.tap (Key::StopCancel);
        // Rec1: a bar's count-in before recording starts
        r.dial (-2);
        expect (r.m.getMetronome() == Metronome::Rec1);
        r.tap (Key::Rec);
        r.tap (Key::PlayPause);
        r.run (1.0);
        expectEquals (r.m.getPlayingStep(), 0, "counting in");
        r.run (1.2);
        expect (r.m.getPlayingStep() > 0, "then recording");
        r.tap (Key::StopCancel);
        // master tune moves synth parts
        gotoPage (r, Key::ModeGlobal, 1);
        r.dial (50);
        expectEquals ((int) r.m.getMemory().global.masterTune, 50);
        gotoPage (r, Key::ModeGlobal, 3);
        r.dial (1);
        expect (r.m.getMemory().global.arpReverse && r.m.getDisplay().value == "Revers");
        r.dial (-1);
        r.tap (Key::WriteRename);
        expectEquals (r.m.getDisplay().param, juce::String ("GloblWr?"));
        r.dial (1);
        r.tap (Key::WriteRename);
        expect (r.m.takeMemoryChanged(), "written (even with protect on)");
    }

    // ------------------------------------------------------------------ MIDI (p.79-85)

    void midi()
    {
        beginTest ("MIDI: channels, drum note map, CCs, program change, filter, CC assign, clock; MIDI mode pages");
        Rig r;
        r.blank();
        r.m.midiIn (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100));
        r.m.midiIn (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 90));
        r.m.midiIn (juce::MidiMessage::noteOn (10, 42, (juce::uint8) 90)); // 6A = F#2
        expect (! r.triggers (kFirstSynth).empty() && r.triggers (kFirstSynth)[0].note == 64, "channel 1 plays synth 1");
        expect (! r.triggers (0).empty() && r.triggers (0)[0].velocity == 90, "C2 on channel 10 plays drum 1");
        expect (! r.triggers (5).empty(), "F#2 plays drum 6A");
        r.m.midiIn (juce::MidiMessage::controllerEvent (2, 74, 20));
        expectEquals ((int) r.pat().parts[(size_t) kFirstSynth + 1].sound.cutoff, 20, "CC 74 on channel 2: synth 2 cutoff");
        r.m.midiIn (juce::MidiMessage::controllerEvent (1, 83, 127));
        expect (r.pat().parts[(size_t) kFirstSynth].sound.filterType == FilterType::BPFPlus, "CC 83: filter type");
        r.m.midiIn (juce::MidiMessage::controllerEvent (1, 92, 99));
        expectEquals ((int) r.pat().fx[0].edit1, 99, "CC 92: FX1 EDIT 1");
        r.m.midiIn (juce::MidiMessage::controllerEvent (1, 23, 127));
        expect (r.pat().chain == Chain::All, "CC 23: FX chain");
        r.m.midiIn (juce::MidiMessage::programChange (1, 4));
        expectEquals (r.m.getPatternIndex(), 4, "program change selects a pattern");
        // MIDI mode: channel for synth 1, clock, bend range
        r.selectPart (kFirstSynth);
        gotoPage (r, Key::ModeMidi, 0);
        r.dial (2);
        expectEquals ((int) r.m.getMemory().global.midiChannel[0], 2, "synth 1 on channel 3");
        r.m.clearTriggerLog();
        r.m.midiIn (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100));
        expect (r.triggers (kFirstSynth).empty(), "channel 1 no longer");
        r.m.midiIn (juce::MidiMessage::noteOn (3, 64, (juce::uint8) 100));
        expect (! r.triggers (kFirstSynth).empty(), "channel 3 now");
        gotoPage (r, Key::ModeMidi, 2);
        r.dial (5);
        expectEquals ((int) r.m.getMemory().global.bendRange, 7);
        // MIDI UTILITY: filter (notes off), drum note, CC assign
        r.with (Key::Shift, Key::Step12);
        expectEquals (r.m.getDisplay().param, juce::String ("MIDIUtil"));
        r.tap (Key::Step12);
        expectEquals (r.m.getDisplay().value, juce::String ("OOOO"));
        r.dial (-1);
        expect (! r.m.getMemory().global.filterNotes, "N off");
        r.tap (Key::Step12);
        r.m.clearTriggerLog();
        r.m.midiIn (juce::MidiMessage::noteOn (3, 64, (juce::uint8) 100));
        expect (r.triggers (kFirstSynth).empty(), "notes filtered");
        r.m.getMemory().global.filterNotes = true;
        r.with (Key::Shift, Key::Step12);
        r.dial (1);
        r.tap (Key::Step12); // DrNote
        r.tap (Key::Drum2);
        r.dial (1);
        expectEquals ((int) r.m.getMemory().global.drumNotes[1], 39);
        r.tap (Key::Step12);
        // clock: external
        gotoPage (r, Key::ModeMidi, 1);
        r.dial (1);
        expect (r.m.getMemory().global.clock == ClockMode::External);
        r.m.midiIn (juce::MidiMessage::midiStart());
        expect (r.m.isPlaying(), "MIDI start");
        for (int i = 0; i < 48; ++i)
            r.m.midiIn (juce::MidiMessage::midiClock());
        r.run (0.02);
        expectEquals (r.m.getPlayingStep(), 8, "48 clocks = 2 quarters = 8 sixteenths");
        r.m.midiIn (juce::MidiMessage::midiStop());
        expect (! r.m.isPlaying());
    }

    // ------------------------------------------------------------------ MIDI out (p.84, p.92)

    void midiOut()
    {
        beginTest ("MIDI OUT: the sequence's notes, part keys, knob and key CCs, start / stop / clock; nothing the panel sends is lost");
        Rig r;
        r.blank();
        auto count = [&r] (const std::function<bool (const juce::MidiMessage&)>& f) {
            int n = 0;
            for (auto& msg : r.sent)
                n += f (msg) ? 1 : 0;
            return n;
        };
        auto ccs = [&count] (int ch, int cc, int v) {
            return count ([=] (const juce::MidiMessage& msg) {
                return msg.isController() && msg.getChannel() == ch && msg.getControllerNumber() == cc && msg.getControllerValue() == v;
            });
        };
        auto anyCc = [&count] { return count ([] (const juce::MidiMessage& msg) { return msg.isController(); }); };

        // a part key pressed between blocks goes out with the next block
        r.tap (Key::Drum1);
        r.run (0.01);
        expectEquals (count ([] (const juce::MidiMessage& msg) { return msg.isNoteOn() && msg.getChannel() == 10 && msg.getNoteNumber() == 36; }), 1,
                      "drum 1 key: C2 on channel 10");

        // a synth part's knobs and keys: CCs on its channel (synth 2 = channel 2)
        r.selectPart (kFirstSynth + 1);
        r.clearAudio();
        r.m.setKnob (Knob::Cutoff, 33);
        r.m.setKnob (Knob::Resonance, 101);
        r.tap (Key::FilterType); // LPF -> HPF
        r.tap (Key::AmpEg);      // decay -> gate
        r.tap (Key::ModDest);
        r.run (0.01);
        expectEquals (ccs (2, 74, 33), 1, "CUTOFF: CC 74");
        expectEquals (ccs (2, 71, 101), 1, "RESONANCE: CC 71");
        expectEquals (ccs (2, 83, mapping::optionToCc (1, 4)), 1, "FILTER TYPE HPF: CC 83");
        expectEquals (ccs (2, 86, 96), 1, "AMP EG gate: CC 86");
        expect (count ([] (const juce::MidiMessage& msg) { return msg.isController() && msg.getControllerNumber() == 88; }) == 1, "MOD DEST: CC 88");

        // what goes out comes back in the same: a second instrument fed our MIDI ends up with the same sound
        Rig other;
        other.blank();
        for (auto& msg : r.sent)
            other.m.midiIn (msg);
        const auto& a = r.pat().parts[(size_t) kFirstSynth + 1].sound;
        const auto& b = other.pat().parts[(size_t) kFirstSynth + 1].sound;
        expect (b.cutoff == a.cutoff && b.resonance == a.resonance && b.filterType == a.filterType && b.ampEg == a.ampEg && b.modDest == a.modDest,
                "round trip through MIDI");

        // the effects on the global channel, for the effect EDIT SELECT picks
        r.clearAudio();
        r.m.setKnob (Knob::FxEdit1, 77);
        r.tap (Key::FxEditSelect); // FX2
        r.m.setKnob (Knob::FxEdit2, 50);
        r.tap (Key::FxChain);
        r.tap (Key::FxMotion);
        r.run (0.01);
        expectEquals (ccs (1, 92, 77), 1, "FX1 EDIT 1: CC 92");
        expectEquals (ccs (1, 95, 50), 1, "FX2 EDIT 2: CC 95");
        expectEquals (ccs (1, 23, mapping::optionToCc ((int) r.pat().chain, 4)), 1, "FX CHAIN: CC 23");
        expectEquals (ccs (1, 21, 127), 1, "FX2 MOTION SEQ on: CC 21");

        // drum parts' knobs aren't sent (the original used NRPN); MASTER VOLUME isn't; the C filter stops CCs
        r.selectPart (2);
        r.clearAudio();
        r.m.setKnob (Knob::PitchGlide, 90);
        r.m.setKnob (Knob::MasterVolume, 90);
        r.run (0.01);
        expectEquals (anyCc(), 0, "no CCs for a drum part or MASTER VOLUME");
        r.selectPart (kFirstSynth);
        r.m.getMemory().global.filterControl = false;
        r.clearAudio();
        r.m.setKnob (Knob::Cutoff, 10);
        r.run (0.01);
        expectEquals (anyCc(), 0, "C filtered");
        r.m.getMemory().global.filterControl = true;

        // PLAY and STOP from the panel: start, 24 clocks a quarter, stop; the sequence's notes on their channels
        r.blank();
        r.pat().parts[0].steps[0].on = true;
        auto& st = r.pat().parts[(size_t) kFirstSynth].steps[4];
        st.on = true;
        st.note = 67;
        r.clearAudio();
        r.tap (Key::PlayPause);
        r.run (2.7); // 120 BPM: a bar is 2 s, so step 1 plays twice and step 5 twice
        r.tap (Key::StopCancel);
        r.run (0.01);
        expectEquals (count ([] (const juce::MidiMessage& msg) { return msg.isMidiStart(); }), 1, "MIDI start");
        expectEquals (count ([] (const juce::MidiMessage& msg) { return msg.isMidiStop(); }), 1, "MIDI stop");
        const int clocks = count ([] (const juce::MidiMessage& msg) { return msg.isMidiClock(); });
        const int expected = (int) std::lround (2.7 * r.m.getTempo() / 60.0 * 24.0);
        expect (std::abs (clocks - expected) <= 1, "24 clocks a quarter: " + juce::String (clocks) + " for " + juce::String (expected));
        const int drumOn = count ([] (const juce::MidiMessage& msg) { return msg.isNoteOn() && msg.getChannel() == 10 && msg.getNoteNumber() == 36; });
        expectEquals (drumOn, 2, "drum 1's step, every bar");
        int synthOn = 0, synthOff = 0;
        double firstOn = -1;
        for (auto& msg : r.sent)
            if (msg.getChannel() == 1 && msg.getNoteNumber() == 67)
            {
                if (msg.isNoteOn()) { ++synthOn; if (firstOn < 0) firstOn = msg.getTimeStamp(); }
                else if (msg.isNoteOff()) ++synthOff;
            }
        expect (synthOn == 2 && synthOff == 2, "synth 1's G4 on channel 1, with its note-offs: " + juce::String (synthOn) + " / " + juce::String (synthOff));
        const double stepLen = 60.0 / r.m.getTempo() / 4.0 * kSr;
        expect (std::abs (firstOn - 4 * stepLen) < 64, "on step 5, sample-accurate: " + juce::String (firstOn) + " vs " + juce::String (4 * stepLen));

        // nothing hangs on the receiver: a note on at STOP or PAUSE gets its note-off, for the note that was sent
        // (even after a transpose in between); a tie starts the next note before ending the last (legato)
        auto balanced = [&r] (int ch) {
            std::map<int, int> on;
            for (auto& msg : r.sent)
                if (msg.getChannel() == ch && (msg.isNoteOn() || msg.isNoteOff()))
                    on[msg.getNoteNumber()] += msg.isNoteOn() ? 1 : -1;
            for (auto& [note, c] : on)
                if (c != 0)
                    return false;
            return true;
        };
        r.blank();
        auto& longNote = r.pat().parts[(size_t) kFirstSynth].steps[0];
        longNote.on = true;
        longNote.note = 60;
        longNote.gate = 60; // 15 steps
        r.clearAudio();
        r.tap (Key::PlayPause);
        r.run (0.3);
        r.dialWith (Key::Solo, 5); // transpose while the note is on
        r.run (0.1);
        r.tap (Key::StopCancel);
        r.run (0.01);
        expect (count ([] (const juce::MidiMessage& msg) { return msg.isNoteOn() && msg.getChannel() == 1; }) == 1, "the long note went out");
        expect (balanced (1), "STOP: its note-off, for the note sent");
        r.dialWith (Key::Solo, -5);
        r.clearAudio();
        r.tap (Key::PlayPause);
        r.run (0.3);
        r.tap (Key::PlayPause); // pause
        r.run (0.01);
        expect (balanced (1), "PAUSE ends it too");
        r.tap (Key::StopCancel);
        r.blank();
        auto& tied = r.pat().parts[(size_t) kFirstSynth].steps;
        tied[0].on = true;
        tied[0].note = 60;
        tied[0].gate = 8; // two steps: ties into step 2
        tied[1].on = true;
        tied[1].note = 64;
        tied[1].gate = 3;
        r.clearAudio();
        r.tap (Key::PlayPause);
        r.run (0.5);
        r.tap (Key::StopCancel);
        r.run (0.01);
        int on64 = -1, off60 = -1;
        for (int i = 0; i < (int) r.sent.size(); ++i)
        {
            const auto& msg = r.sent[(size_t) i];
            if (msg.getChannel() != 1)
                continue;
            if (msg.isNoteOn() && msg.getNoteNumber() == 64 && on64 < 0) on64 = i;
            if (msg.isNoteOff() && msg.getNoteNumber() == 60 && off60 < 0) off60 = i;
        }
        expect (on64 >= 0 && off60 > on64, "legato: E4 on before C4 off");
        expect (balanced (1), "and both end");

        // MIDI UTILITY > CCAsin: CUTOFF to CC 30, from the panel; out and in on the new number; kept in the memory (GLOBAL)
        r.blank();
        r.selectPart (kFirstSynth);
        r.tap (Key::ModeMidi); // MIDI UTILITY is a MIDI-mode function
        r.with (Key::Shift, Key::Step12);
        r.dial (2);
        expectEquals (r.m.getDisplay().value, juce::String ("CCAsin"));
        r.tap (Key::Step12);
        r.dial ((int) Knob::Cutoff - (int) Knob::FxType);
        expectEquals (r.m.getDisplay().param, juce::String ("CCAssign"));
        r.tap (Key::Step12);
        expectEquals (r.m.getDisplay().value, juce::String ("74"), "CUTOFF is CC 74 from the factory");
        r.dial (30 - 74);
        expectEquals (r.m.getDisplay().value, juce::String ("30"));
        r.tap (Key::Step12); // Continue? No
        r.tap (Key::Step12);
        expectEquals ((int) r.m.getMemory().global.ccAssign[(size_t) Knob::Cutoff], 30);
        r.clearAudio();
        r.m.setKnob (Knob::Cutoff, 55);
        r.run (0.01);
        expectEquals (ccs (1, 30, 55), 1, "CUTOFF sends CC 30 now");
        expectEquals (ccs (1, 74, 55), 0, "not 74");
        r.m.midiIn (juce::MidiMessage::controllerEvent (1, 30, 12));
        expectEquals ((int) r.pat().parts[(size_t) kFirstSynth].sound.cutoff, 12, "CC 30 in turns CUTOFF");
        Memory back;
        expect (memoryFromJson (juce::JSON::parse (juce::JSON::toString (memoryToJson (r.m.getMemory()))), back), "memory file");
        expectEquals ((int) back.global.ccAssign[(size_t) Knob::Cutoff], 30, "the assignment is saved with the memory");
    }

    // ------------------------------------------------------------------ the card (files) (p.76-78)

    void card()
    {
        beginTest ("CARD menu (files): Save All, Load All (protect), Load one pattern, Delete, Format");
        auto dir = juce::File::createTempFile ("valvebox-card");
        dir.createDirectory();
        Rig r;
        r.m.setCardFolder (dir);
        gotoPage (r, Key::ModeGlobal, 0);
        r.with (Key::Shift, Key::Step13);
        expectEquals (r.m.getDisplay().param, juce::String ("CardMenu"));
        r.dial (1);
        expectEquals (r.m.getDisplay().value, juce::String ("SaveAll"));
        r.tap (Key::Step13);
        expectEquals (r.m.getDisplay().param, juce::String ("FileName"));
        r.tap (Key::Step13);
        r.dial (1);
        r.tap (Key::Step13);
        auto req = r.m.takeCardRequest();
        expect (req.kind == Machine::CardRequest::SaveAll, "a Save All request");
        r.m.runCardRequest (req);
        expect (dir.getChildFile ("VALVEBOX.vbx").existsAsFile(), "the file");
        // load all: protect on refuses
        r.m.getMemory().patterns[5].setName ("Changed");
        r.with (Key::Shift, Key::Step13);
        r.tap (Key::Step13);
        r.tap (Key::Step13);
        r.tap (Key::Step13);
        r.dial (1);
        r.tap (Key::Step13);
        expectEquals (r.m.getDisplay().param, juce::String ("Protect"));
        r.unprotect();
        r.with (Key::Shift, Key::Step13);
        r.tap (Key::Step13);
        r.tap (Key::Step13);
        r.tap (Key::Step13);
        r.dial (1);
        r.tap (Key::Step13);
        r.m.runCardRequest (r.m.takeCardRequest());
        expect (r.m.getMemory().patterns[5].getName() != "Changed", "loaded back");
        expect (r.m.getMode() == Mode::Pattern && r.m.getPatternIndex() == 0, "Pattern mode, A01 (p.78)");
        // load one pattern into the edit buffer
        gotoPage (r, Key::ModeGlobal, 0);
        r.with (Key::Shift, Key::Step13);
        r.tap (Key::Step13);
        r.dial (1); // 1Ptrn
        r.tap (Key::Step13);
        r.tap (Key::Step13);
        r.dial (3);
        r.tap (Key::Step13);
        r.m.runCardRequest (r.m.takeCardRequest());
        expectEquals (r.m.getEditPattern().getName(), r.m.getMemory().patterns[3].getName(), "pattern A.04 in the edit buffer");
        // delete
        gotoPage (r, Key::ModeGlobal, 0);
        r.with (Key::Shift, Key::Step13);
        r.dial (2);
        r.tap (Key::Step13);
        r.tap (Key::Step13);
        r.dial (1);
        r.tap (Key::Step13);
        r.m.runCardRequest (r.m.takeCardRequest());
        expect (! dir.getChildFile ("VALVEBOX.vbx").existsAsFile(), "deleted");
        dir.deleteRecursively();
    }

    // ------------------------------------------------------------------ the arpeggiator (p.27-28)

    void arpeggiator()
    {
        beginTest ("ARPEGGIATOR: the ribbon plays the part (drums at a rate by position, synths in 16ths); the slider moves the pitch in the scale");
        ribbonTested = sliderTested = true;
        Rig r;
        r.blank();
        r.selectPart (0);
        r.m.clearTriggerLog();
        r.m.ribbon (true, 0.40f); // the 1/16 zone
        r.run (1.0);
        r.m.ribbon (false, 0.40f);
        expectWithinAbsoluteError ((int) r.triggers (0).size(), 8, 1, "16ths at 120 for a second");
        r.m.clearTriggerLog();
        r.m.ribbon (true, 0.95f); // 1/64
        r.run (0.5);
        r.m.ribbon (false, 0.95f);
        expect (r.triggers (0).size() >= 14, "faster further right");
        // a synth part: hold C and E, touch the ribbon
        r.selectPart (kFirstSynth);
        r.tap (Key::Keyboard);
        r.m.slider (0.5f);
        r.m.press (Key::Step4);
        r.m.press (Key::Step8);
        r.m.clearTriggerLog();
        r.m.ribbon (true, 0.3f);
        r.run (0.55);
        r.m.ribbon (false, 0.3f);
        r.m.release (Key::Step4);
        r.m.release (Key::Step8);
        auto t = r.triggers (kFirstSynth);
        expect (t.size() >= 4 && t[0].note == 60 && t[1].note == 64 && t[2].note == 60, "C, E, C, E...");
        // the slider: up the scale (chromatic: 12 degrees = an octave at the top)
        r.m.slider (1.0f);
        r.m.clearTriggerLog();
        r.m.ribbon (true, 0.3f);
        r.run (0.1);
        r.m.ribbon (false, 0.3f);
        t = r.triggers (kFirstSynth);
        expect (! t.empty() && t[0].note == r.pat().arpCentre + 12, "slider at the top: an octave up from the centre note");
        // a scale: major pentatonic, top of the slider = 5 degrees = an octave
        gotoPage (r, Key::ModePattern, 6);
        r.dial (12);
        expectEquals (r.m.getDisplay().value, juce::String ("MPenta"));
        r.m.slider (0.6f); // one degree up: D
        r.m.clearTriggerLog();
        r.m.ribbon (true, 0.3f);
        r.run (0.1);
        r.m.ribbon (false, 0.3f);
        t = r.triggers (kFirstSynth);
        expect (! t.empty() && t[0].note == r.pat().arpCentre + 2, "pentatonic: the next degree is a tone up");
        // ARP CONTROL reversed: the slider sets the rate on a drum part
        r.selectPart (0);
        r.m.getMemory().global.arpReverse = true;
        r.m.slider (0.40f);
        r.m.clearTriggerLog();
        r.m.ribbon (true, 0.9f);
        r.run (1.0);
        r.m.ribbon (false, 0.9f);
        expectWithinAbsoluteError ((int) r.triggers (0).size(), 8, 1, "reversed: the slider sets the rate");
    }
};

static PanelTests panelTests;
