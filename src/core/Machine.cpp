#include "Machine.h"
#include "Mapping.h"
#include <algorithm>
#include <cmath>

namespace valvebox
{

const char* keyName (Key k)
{
    static const char* n[kNumKeys] = {
        "RESET/ERASE", "REC", "STOP/CANCEL", "PLAY/PAUSE", "TAP", "PART MUTE", "SOLO", "AUDIO IN THRU", "TEMPO LOCK", "AUTO BPM SCAN",
        "WRITE/RENAME", "CURSOR <", "CURSOR >", "PATTERN", "PART EDIT", "STEP EDIT", "SONG", "GLOBAL", "MIDI",
        "FX CHAIN", "EDIT SELECT", "FX MOTION SEQ", "PART MOTION SEQ", "AMP EG", "ROLL", "FX SELECT", "FX SEND", "MOD TYPE", "MOD DEST",
        "BPM SYNC", "FILTER TYPE",
        "DRUM 1", "DRUM 2", "DRUM 3", "DRUM 4", "DRUM 5", "DRUM 6A", "DRUM 6B", "DRUM 7A", "DRUM 7B", "DRUM ACCENT",
        "SYNTH 1", "SYNTH 2", "SYNTH 3", "SYNTH 4", "SYNTH 5", "SYNTH ACCENT", "KEYBOARD", "SELECT <", "SELECT >",
        "SHIFT", "PATTERN SET", "STEP 1", "STEP 2", "STEP 3", "STEP 4", "STEP 5", "STEP 6", "STEP 7", "STEP 8", "STEP 9", "STEP 10",
        "STEP 11", "STEP 12", "STEP 13", "STEP 14", "STEP 15", "STEP 16" };
    return n[(int) k];
}

int partOfKey (Key k)
{
    if (k >= Key::Drum1 && k <= Key::Drum7B)
        return (int) k - (int) Key::Drum1;
    if (k == Key::DrumAccent)
        return kDrumAccent;
    if (k >= Key::Synth1 && k <= Key::Synth5)
        return kFirstSynth + (int) k - (int) Key::Synth1;
    if (k == Key::SynthAccent)
        return kSynthAccent;
    return -1;
}

Key keyOfPart (int p)
{
    if (isDrum (p))
        return (Key) ((int) Key::Drum1 + p);
    if (isSynth (p))
        return (Key) ((int) Key::Synth1 + p - kFirstSynth);
    return p == kDrumAccent ? Key::DrumAccent : Key::SynthAccent;
}

const char* knobName (Knob k)
{
    static const char* n[kNumKnobs] = { "MasterVol", "TubeGain", "AudioIn", "FxType", "FxEdit1", "FxEdit2", "Pitch", "EGTime", "Pan", "Level",
                                        "ModSpeed", "ModDepth", "OscType", "OscEdit1", "OscEdit2", "Cutoff", "EGInt", "Reso", "Drive" };
    return n[(int) k];
}

namespace
{
    // matrix menu pages per mode (p.10, and each mode's chapter)
    const char* const kPatternPages[] = { "Pattern", "Tempo", "Swing", "Length", "Beat", "RollType", "ArpScale" };
    const char* const kPartPages[] = { "Wave", "AccentSw", "SwingSw", "OutBus", "SynTune" };
    const char* const kStepPages[] = { "StepRec", "NoteNo", "GateTime", "MotDest", "MotValue" };
    const char* const kSongPages[] = { "Song", "Tempo", "Position", "Pattern", "NoteOfs", "MuteHold", "NextSong" };
    const char* const kGlobalPages[] = { "Metronom", "MstTune", "Protect", "ArpCntrl" };
    const char* const kMidiPages[] = { "MIDI ch", "Clock", "BendRnge" };
    const char* const kMetronomeNames[] = { "Off", "Rec0", "Rec1", "Rec2", "On", "On>3/4" };
    const char* const kBeatNames[] = { "16", "32", "8Tri", "16Tri" };

    juce::String onOff (bool b) { return b ? "On" : "Off"; }
    juce::String signedText (int v) { return (v > 0 ? "+" : "") + juce::String (v); }
    juce::String stepText (int step, int perBar) { return juce::String (step / perBar + 1) + "." + juce::String (step % perBar + 1).paddedLeft ('0', 2); }
    juce::String gateText (int quarters) { return juce::String (quarters / 4.0, 2); }

    juce::String waveNameFor (const Sound& s, int part)
    {
        if (isDrum (part))
            return WaveLibrary::get().drumName (s.wave);
        if (! isSynth (part))
            return "---";
        static const char* basic[5] = { "Saw", "Squ", "Tri", "Sin", "Ns" };
        static const char* wf[4] = { "Saw", "Pulse", "Tri", "Sin" };
        const int w = s.wave;
        switch (s.oscType)
        {
            case OscType::Waveform: case OscType::Sync: return wf[std::clamp (w, 0, 3)];
            case OscType::Chord: case OscType::Vpm: case OscType::Additive: return basic[std::clamp (w, 0, 3)];
            case OscType::Dual: case OscType::RingMod: case OscType::CrossMod:
                return juce::String (basic[std::clamp (w / 5, 0, 3)]) + "-" + basic[std::clamp (w % 5, 0, 4)];
            case OscType::Unison: return juce::String (3 + w % 4) + basic[std::clamp (w / 4, 0, 3)];
            case OscType::Waveshape: return w == 0 ? "Type1" : "Type2";
            case OscType::Comb: return basic[std::clamp (w, 0, 4)];
            case OscType::PcmComb: case OscType::PcmWaveshape: return WaveLibrary::get().pcmName (w);
            case OscType::Formant: case OscType::Noise: case OscType::AudioInComb: return "---";
        }
        return "---";
    }

    /** OSC EDIT 1 / 2 as the display shows them, by model (p.34-38). */
    juce::String oscEditText (const Sound& s, int which, int raw)
    {
        const int b = mapping::bipolar (raw);
        switch (s.oscType)
        {
            case OscType::Chord:
                if (which == 1)
                    return mapping::chordName (std::clamp (raw * 16 / 128, 0, 15));
                return signedText (std::clamp ((int) std::lround ((raw - 64) / 64.0 * 3.0), -3, 3));
            case OscType::Vpm:
                return which == 2 ? juce::String (mapping::harmonic (raw), 2) : juce::String (raw);
            case OscType::Additive:
                return juce::String (mapping::harmonic (raw), 2);
            case OscType::Sync: case OscType::Comb: case OscType::PcmComb: case OscType::AudioInComb: case OscType::PcmWaveshape:
                return juce::String (raw);
            case OscType::Waveform: case OscType::Dual: case OscType::Unison: case OscType::RingMod: case OscType::CrossMod:
            case OscType::Waveshape: case OscType::Formant: case OscType::Noise:
                return which == 2 ? signedText (b) : juce::String (raw);
        }
        return juce::String (raw);
    }

    juce::String fxEditText (const FxSettings& f, int which)
    {
        const int raw = which == 1 ? f.edit1 : f.edit2;
        switch (f.type)
        {
            case FxType::BpmDelay: case FxType::ModDelay:
                return which == 1 ? mapping::delayName (raw) : juce::String (raw);
            case FxType::PitchShifter:
                return which == 1 ? signedText (mapping::pitchShiftCents (raw)) : juce::String (raw);
            case FxType::TalkingMod:
                return which == 2 ? signedText (mapping::bipolar (raw)) : juce::String (raw);
            case FxType::Eq:
                return signedText (mapping::bipolar (raw));
            case FxType::Reverb: case FxType::ShortDelay: case FxType::GrainShifter: case FxType::ChorusFlanger: case FxType::Phaser:
            case FxType::RingMod: case FxType::Compressor: case FxType::Distortion: case FxType::Decimator: case FxType::Lpf: case FxType::Hpf:
                return juce::String (raw);
        }
        return juce::String (raw);
    }
} // namespace

// ---------------------------------------------------------------- set-up

Machine::Machine()
{
    knobs.fill (64);
    knobs[(size_t) Knob::MasterVolume] = 100;
    knobs[(size_t) Knob::TubeGain] = 0;
    knobs[(size_t) Knob::AudioInLevel] = 80;
    heldNote.fill (-1);
    noteSent.fill (-1);
    skipOnce.fill (-1);
    offAt.fill (-1.0);
    lastPlayedStep.fill (-1);
    recStep.fill (-1);
    trigHoldValue.fill (-1);
    loadPreload();
}

void Machine::loadPreload()
{
    mem = factoryMemory();
    loadPatternNow (0);
    songIndex = 0;
    songEdit = mem.songs[0];
    memoryChanged = true;
}

void Machine::setMemory (const Memory& m)
{
    mem = m;
    loadPatternNow (std::clamp (patternIndex, 0, kNumPatterns - 1));
    songEdit = mem.songs[(size_t) songIndex];
}

void Machine::prepare (double sr, int block)
{
    sampleRate = sr;
    maxBlock = std::max (16, block);
    rack.prepare (sr, 64);
    clickBuf.assign ((size_t) maxBlock, 0.0f);
    onsetEnv.assign (800, 0.0f); // 8 s of input envelope at 100 Hz (auto BPM)
    midiOutBuffer.ensureSize (4096);
}

void Machine::loadPatternNow (int index)
{
    patternIndex = std::clamp (index, 0, kNumPatterns - 1);
    edit = mem.patterns[(size_t) patternIndex];
    pendingPattern = -1;
    barShown = std::min (barShown, (int) edit.length - 1);
    applyTempoForPattern();
    trigHoldValue.fill (-1);
}

void Machine::selectPattern (int index)
{
    if (transport == Transport::Stopped)
        loadPatternNow (index);
    else
        pendingPattern = std::clamp (index, 0, kNumPatterns - 1); // waits for the end of the pattern (p.20)
}

void Machine::applyTempoForPattern()
{
    if (! tempoLock)
        lockedTempo = edit.tempo;
}

double Machine::getTempo() const
{
    if (mode == Mode::Song && songActive)
        return songEdit.tempoLock ? songEdit.tempo : songPat.tempo;
    return tempoLock ? lockedTempo : edit.tempo;
}

int Machine::getPlayingStep() const { return transport == Transport::Stopped ? -1 : (int) pos; }

// ---------------------------------------------------------------- display helpers

void Machine::flash (const juce::String& p, const juce::String& v, double seconds)
{
    flashParam = p;
    flashValue = v;
    flashUntil = clockSeconds + seconds;
}

int Machine::numPages() const
{
    switch (mode)
    {
        case Mode::Pattern: return 7;
        case Mode::PartEdit: return 5;
        case Mode::StepEdit: return 5;
        case Mode::Song: return 7;
        case Mode::Global: return 4;
        case Mode::Midi: return 3;
    }
    return 1;
}

juce::String Machine::pageName() const
{
    const int pg = page[(size_t) mode];
    switch (mode)
    {
        case Mode::Pattern: return pg == 0 ? playingConst().getName() : kPatternPages[pg];
        case Mode::PartEdit: return pg == 0 ? waveNameFor (sound(), part) : kPartPages[pg];
        case Mode::StepEdit: return kStepPages[pg];
        case Mode::Song: return pg == 0 ? songEdit.getName() : kSongPages[pg];
        case Mode::Global: return kGlobalPages[pg];
        case Mode::Midi: return kMidiPages[pg];
    }
    return {};
}

juce::String Machine::pageValue() const
{
    const int pg = page[(size_t) mode];
    const auto& p = playingConst();
    switch (mode)
    {
        case Mode::Pattern:
            switch (pg)
            {
                case 0: return patternNumberText (pendingPattern >= 0 ? pendingPattern : patternIndex);
                case 1: return juce::String (getTempo(), 1);
                case 2: return juce::String (p.swing);
                case 3: return juce::String (p.length);
                case 4: return kBeatNames[(int) p.beat];
                case 5: return juce::String (p.rollType);
                default: return mapping::scales()[(size_t) p.arpScale].name;
            }
        case Mode::PartEdit:
        {
            const auto& pt = p.parts[(size_t) part];
            switch (pg)
            {
                case 0: return isDrum (part) ? juce::String (sound().wave + 1).paddedLeft ('0', 3)
                                             : isSynth (part) ? juce::String (sound().wave + 1).paddedLeft ('0', 2) : juce::String ("---");
                case 1: return onOff (pt.accentSw);
                case 2: return onOff (pt.swingSw);
                case 3: return pt.outBus34 ? "3/4" : "L/R";
                default: return isSynth (part) ? signedText (sound().synthTune) : juce::String ("---");
            }
        }
        case Mode::StepEdit:
        {
            const auto& st = p.parts[(size_t) part].steps[(size_t) std::clamp (pg == 0 ? stepRecTarget : noteEditStep, 0, kMaxSteps - 1)];
            switch (pg)
            {
                case 0: return stepText (stepRecTarget, p.lastStep);
                case 1: return isSynth (part) ? mapping::noteName (st.note) : juce::String ("---");
                case 2: return isSynth (part) ? gateText (st.gate) : juce::String ("---");
                case 3:
                {
                    const auto params = [] {
                        juce::StringArray a;
                        for (int i = 0; i < kNumParams - 2; ++i)
                            a.add (paramShortName ((Param) i));
                        return a;
                    }();
                    juce::ignoreUnused (params);
                    return paramShortName ((Param) std::clamp (motionDest, 0, kNumParams - 1));
                }
                default:
                {
                    const int slot = p.findMotion (part, (Param) motionDest);
                    if (slot < 0)
                        return "---";
                    const int v = p.motion[(size_t) slot].values[(size_t) std::clamp (noteEditStep, 0, kMaxSteps - 1)];
                    return v < 0 ? juce::String ("Disabl") : juce::String (v);
                }
            }
        }
        case Mode::Song:
            switch (pg)
            {
                case 0: return "S." + juce::String (songIndex + 1).paddedLeft ('0', 2);
                case 1: return juce::String (songEdit.tempo, 1);
                case 2: return songPosition >= songEdit.numPositions() ? juce::String ("End") : juce::String (songPosition + 1).paddedLeft ('0', 3);
                case 3:
                {
                    const auto& ps = songEdit.positions[(size_t) std::clamp (songPosition, 0, kMaxPositions - 1)];
                    return ps.pattern < 0 ? juce::String ("End") : patternNumberText (ps.pattern);
                }
                case 4: return signedText (songEdit.positions[(size_t) std::clamp (songPosition, 0, kMaxPositions - 1)].noteOffset);
                case 5: return onOff (songEdit.muteHold);
                default: return songEdit.nextSong < 0 ? juce::String ("Off") : "S." + juce::String (songEdit.nextSong + 1).paddedLeft ('0', 2);
            }
        case Mode::Global:
            switch (pg)
            {
                case 0: return kMetronomeNames[(int) metronome];
                case 1: return signedText (mem.global.masterTune);
                case 2: return onOff (mem.global.protect);
                default: return mem.global.arpReverse ? "Revers" : "Normal";
            }
        case Mode::Midi:
            switch (pg)
            {
                case 0: return juce::String (mem.global.midiChannel[(size_t) (isSynth (part) ? part - kFirstSynth : 5)] + 1);
                case 1: { static const char* c[3] = { "Int", "Ext", "Auto" }; return c[(int) mem.global.clock]; }
                default: return signedText (mem.global.bendRange);
            }
    }
    return {};
}

juce::String Machine::knobValueText (Knob k) const
{
    const auto& s = sound();
    const auto& f = playingConst().fx[(size_t) fxEdit];
    switch (k)
    {
        case Knob::MasterVolume: case Knob::TubeGain: case Knob::AudioInLevel: return juce::String (knobs[(size_t) k]);
        case Knob::FxType: return mapping::fxTypeName (f.type);
        case Knob::FxEdit1: return fxEditText (f, 1);
        case Knob::FxEdit2: return fxEditText (f, 2);
        case Knob::PitchGlide: return isDrum (part) ? signedText (mapping::bipolar (s.pitch)) : juce::String (s.glide);
        case Knob::EgTime: return juce::String (s.egTime);
        case Knob::Pan:
        {
            const int b = mapping::bipolar (s.pan);
            return b == 0 ? juce::String ("Center") : (b < 0 ? "L" : "R") + juce::String (std::abs (b));
        }
        case Knob::Level: return juce::String (s.level);
        case Knob::ModSpeed: return s.modBpmSync ? juce::String (mapping::syncValue (s.modSpeed).name) : juce::String (s.modSpeed);
        case Knob::ModDepth: return signedText (mapping::bipolar (s.modDepth));
        case Knob::OscType: return mapping::oscTypeName (s.oscType);
        case Knob::OscEdit1: return oscEditText (s, 1, s.oscEdit1);
        case Knob::OscEdit2: return oscEditText (s, 2, s.oscEdit2);
        case Knob::Cutoff: return juce::String (s.cutoff);
        case Knob::EgInt: return signedText (mapping::bipolar (s.egInt));
        case Knob::Resonance: return juce::String (s.resonance);
        case Knob::Drive: return juce::String (s.drive);
        case Knob::Count: break;
    }
    return {};
}

void Machine::showKnob (Knob k)
{
    shownKnob = (int) k;
    knobUntil = clockSeconds + 1.5; // then the previous display comes back (p.15)
}

Display Machine::getDisplay() const
{
    Display d;
    const auto& p = playingConst();
    d.inputLevel = inputPeak;
    d.clip = inputClip;
    for (int i = 0; i < kNumParts; ++i)
        d.status[(size_t) i] = partAudible (i);
    d.statusRow = 0;
    if (mode == Mode::PartEdit && page[(size_t) mode] >= 1 && page[(size_t) mode] <= 3)
    {
        const int row = page[(size_t) mode];
        d.statusRow = row == 1 ? 2 : row == 2 ? 1 : 3;
        for (int i = 0; i < kNumParts; ++i)
        {
            const auto& pt = p.parts[(size_t) i];
            d.status[(size_t) i] = row == 1 ? pt.accentSw : row == 2 ? pt.swingSw : ! pt.outBus34;
        }
    }

    if (clockSeconds < flashUntil)
    {
        d.param = flashParam;
        d.value = flashValue;
        return d;
    }
    // an operation in progress
    switch (op)
    {
        case Op::Write:
            d.param = "WriteTo";
            d.value = mode == Mode::Song ? "S." + juce::String (opValue + 1).paddedLeft ('0', 2) : patternNumberText (opValue);
            if (held[(size_t) Key::Shift])
                d.value = mode == Mode::Song ? mem.songs[(size_t) opValue].getName().substring (0, 6) : mem.patterns[(size_t) opValue].getName().substring (0, 6);
            return d;
        case Op::Rename: case Op::CardName:
            d.param = op == Op::Rename ? "Rename" : "FileName";
            d.value = opName;
            d.cursor = opCursor;
            return d;
        case Op::LastStep: d.param = "LastStep"; d.value = juce::String (opValue); return d;
        case Op::MoveSelect: { static const char* m[3] = { "Move", "Repeat", "Random" }; d.param = "MoveData"; d.value = m[opValue]; return d; }
        case Op::Move:
            d.param = opValue2 == 0 ? "Move" : opValue2 == 1 ? "Repeat" : "Random";
            d.value = opValue2 == 2 ? juce::String ("---")
                                    : (opValue < 0 ? "-" : opValue2 == 0 ? "+" : "") + stepText (std::abs (opValue), p.lastStep).replace ("1.", "0.", false);
            if (opValue2 != 2)
                d.value = (opValue < 0 ? "-" : (opValue2 == 0 ? "+" : "")) + juce::String (std::abs (opValue) / p.lastStep) + "."
                          + juce::String (std::abs (opValue) % p.lastStep).paddedLeft ('0', 2);
            return d;
        case Op::ShiftSelect: d.param = "ShiftNot"; d.value = opValue == 0 ? "Shift" : "Random"; return d;
        case Op::Shift: d.param = opValue2 == 0 ? "Shift" : "Random"; d.value = opValue2 == 0 ? signedText (opValue) : juce::String ("---"); return d;
        case Op::CopyPart: case Op::CopySound:
            d.param = op == Op::CopyPart ? "CopyPart" : "CopySnd";
            d.value = patternNumberText (opValue);
            if (opSourcePart >= 0)
                d.param = partName (opSourcePart);
            return d;
        case Op::ClearMotion: d.param = "ClrMotn"; d.value = "Sure?"; return d;
        case Op::ClearPart: d.param = "ClrPart"; d.value = "Sure?"; return d;
        case Op::ClearPattern: d.param = "ClrPtrn"; d.value = held[(size_t) Key::Shift] ? p.getName().substring (0, 6) : patternNumberText (patternIndex); return d;
        case Op::InsertPattern: d.param = "InsPtrn"; d.value = juce::String (opValue + 1).paddedLeft ('0', 3); return d;
        case Op::DeletePattern: d.param = "DelPtrn"; d.value = juce::String (opValue + 1).paddedLeft ('0', 3); return d;
        case Op::ClearSong: d.param = "ClrSong"; d.value = "S." + juce::String (songIndex + 1).paddedLeft ('0', 2); return d;
        case Op::MidiUtil: { static const char* m[4] = { "Filter", "DrNote", "CCAsin", "Dump" }; d.param = "MIDIUtil"; d.value = m[opValue]; return d; }
        case Op::MidiFilter:
        {
            const auto& g = mem.global;
            d.param = "PCEN";
            d.value = juce::String (g.filterProgram ? "O" : "X") + (g.filterControl ? "O" : "X") + (g.filterExclusive ? "O" : "X") + (g.filterNotes ? "O" : "X");
            return d;
        }
        case Op::DrumNote:
            d.param = opSourcePart >= 0 ? partName (opSourcePart) : "DrNote";
            d.value = opSourcePart >= 0 ? mapping::noteName (mem.global.drumNotes[(size_t) opSourcePart]) : juce::String ("---");
            return d;
        case Op::CcSelect: d.param = "CCAssign"; d.value = knobName ((Knob) (opValue + (int) Knob::FxType)); return d;
        case Op::CcNumber: d.param = "CCNumber"; d.value = mem.global.ccAssign[(size_t) (opValue + (int) Knob::FxType)] < 0 ? juce::String ("Off") : juce::String (mem.global.ccAssign[(size_t) (opValue + (int) Knob::FxType)]); return d;
        case Op::CcContinue: d.param = "Continue"; d.value = opValue2 ? "Yes" : "No"; return d;
        case Op::Dump: { static const char* m[6] = { "PtBnkA", "PtBnkB", "PtBnkC", "PtBnkD", "AllSng", "Global" }; d.param = "MIDIDump"; d.value = m[opValue]; return d; }
        case Op::CardMenu: { static const char* m[4] = { "Load", "SaveAll", "Delete", "Format" }; d.param = "CardMenu"; d.value = m[opValue]; return d; }
        case Op::CardLoadWhat: d.param = "Load"; d.value = opValue == 0 ? "All" : "1Ptrn"; return d;
        case Op::CardFile:
            d.param = cardFiles.isEmpty() ? juce::String ("NoFile") : cardFiles[opValue].substring (0, 8);
            d.value = opValue2 == 2 ? "DelFile" : "LdFile";
            return d;
        case Op::CardPattern: d.param = "Load"; d.value = patternNumberText (opValue); return d;
        case Op::CardSure: d.param = opValue2 == 9 ? "OverWrt?" : "Sure?"; d.value = opValue ? "Yes" : "No"; return d;
        case Op::PatternSetWrite: d.param = "PtSetWr?"; d.value = opValue ? "Yes" : "No"; return d;
        case Op::GlobalWrite: d.param = "GloblWr?"; d.value = opValue ? "Yes" : "No"; return d;
        case Op::Compare: d.param = "Compare"; d.value = patternNumberText (patternIndex); return d;
        case Op::None: break;
    }
    // while PATTERN SET is held: the pattern on the key last touched
    if (held[(size_t) Key::PatternSet] && lastPatternSetKey >= 0)
    {
        const int idx = mem.patternSet[(size_t) (patternSetGroup * 16 + lastPatternSetKey)];
        d.param = mem.patterns[(size_t) idx].getName();
        d.value = patternNumberText (idx);
        d.patternInd = true;
        return d;
    }
    if (held[(size_t) Key::Solo] && soloUsed && ! solo.any())
    {
        d.param = "Transpos";
        d.value = signedText (transpose);
        return d;
    }
    if (held[(size_t) Key::Shift] && mode == Mode::Pattern && transport == Transport::Stopped)
    {
        d.param = "Len/Beat";
        d.value = juce::String (p.length) + " " + kBeatNames[(int) p.beat];
        return d;
    }
    if (bpmScanning || bpmFound)
    {
        d.param = "AutoBPM";
        d.value = bpmFound ? (detectedBpm > 0 ? juce::String (detectedBpm, 1) : juce::String ("---.-")) : juce::String ("...");
        return d;
    }
    if (shownKnob >= 0 && clockSeconds < knobUntil)
    {
        const auto k = (Knob) shownKnob;
        d.param = knobName (k);
        d.value = knobValueText (k);
        d.knobInd = true;
        // the original value indicator: the knob is where the stored pattern has it
        const auto& st = stored();
        const auto& now = playingConst();
        switch (k)
        {
            case Knob::FxType: d.originalInd = st.fx[(size_t) fxEdit].type == now.fx[(size_t) fxEdit].type; break;
            case Knob::FxEdit1: d.originalInd = st.fx[(size_t) fxEdit].edit1 == now.fx[(size_t) fxEdit].edit1; break;
            case Knob::FxEdit2: d.originalInd = st.fx[(size_t) fxEdit].edit2 == now.fx[(size_t) fxEdit].edit2; break;
            case Knob::MasterVolume: case Knob::TubeGain: case Knob::AudioInLevel: case Knob::Count: break;
            case Knob::PitchGlide: case Knob::EgTime: case Knob::Pan: case Knob::Level: case Knob::ModSpeed: case Knob::ModDepth:
            case Knob::OscType: case Knob::OscEdit1: case Knob::OscEdit2: case Knob::Cutoff: case Knob::EgInt: case Knob::Resonance:
            case Knob::Drive:
                d.originalInd = getKnobValue (k) == [&] {
                    // the same knob read from the stored pattern
                    const auto& s = st.parts[(size_t) part].sound;
                    switch (k)
                    {
                        case Knob::PitchGlide: return (int) (isDrum (part) ? s.pitch : s.glide);
                        case Knob::EgTime: return (int) s.egTime;
                        case Knob::Pan: return (int) s.pan;
                        case Knob::Level: return (int) s.level;
                        case Knob::ModSpeed: return (int) s.modSpeed;
                        case Knob::ModDepth: return (int) s.modDepth;
                        case Knob::OscType: return (int) s.oscType * 8 + 4;
                        case Knob::OscEdit1: return (int) s.oscEdit1;
                        case Knob::OscEdit2: return (int) s.oscEdit2;
                        case Knob::Cutoff: return (int) s.cutoff;
                        case Knob::EgInt: return (int) s.egInt;
                        case Knob::Resonance: return (int) s.resonance;
                        case Knob::Drive: return (int) s.drive;
                        default: return -1;
                    }
                }();
                break;
        }
        return d;
    }
    d.param = pageName();
    d.value = pageValue();
    d.patternInd = mode == Mode::Pattern && page[0] == 0;
    d.songInd = mode == Mode::Song && page[3] == 0;
    d.waveInd = mode == Mode::PartEdit && page[1] == 0;
    d.blink = mode == Mode::Pattern && page[0] == 0 && pendingPattern >= 0;
    if (mode == Mode::StepEdit && page[2] == 4 && std::fmod (clockSeconds, 1.6) < 0.8)
        d.param = paramShortName ((Param) motionDest); // MotValue alternates the name and the value (p.60)
    return d;
}

// ---------------------------------------------------------------- LEDs

Led Machine::getLed (Key k) const
{
    const auto& p = playingConst();
    const auto& s = sound();
    const bool blinkOn = true;
    juce::ignoreUnused (blinkOn);
    switch (k)
    {
        case Key::ModePattern: return op == Op::Compare ? Led::Blink : mode == Mode::Pattern ? Led::On : Led::Off;
        case Key::ModePartEdit: return mode == Mode::PartEdit ? Led::On : Led::Off;
        case Key::ModeStepEdit: return mode == Mode::StepEdit ? Led::On : Led::Off;
        case Key::ModeSong: return mode == Mode::Song ? Led::On : Led::Off;
        case Key::ModeGlobal: return mode == Mode::Global ? Led::On : Led::Off;
        case Key::ModeMidi: return mode == Mode::Midi ? Led::On : Led::Off;
        case Key::PlayPause:
            if (transport == Transport::Paused || (recordReady && transport == Transport::Stopped))
                return Led::Blink;
            return transport == Transport::Playing ? Led::On : Led::Off;
        case Key::Rec: return (recording || recordReady || stepRecord) ? (countIn > 0 ? Led::Blink : Led::On) : Led::Off;
        case Key::PartMute:
        {
            for (auto& pt : p.parts)
                if (pt.muted)
                    return Led::On;
            return Led::Off;
        }
        case Key::Solo: return solo.any() ? Led::On : transpose != 0 ? Led::Blink : Led::Off;
        case Key::AudioInThru: return audioThru ? Led::On : Led::Off;
        case Key::TempoLock: return (mode == Mode::Song ? songEdit.tempoLock : tempoLock) ? Led::On : Led::Off;
        case Key::AutoBpm: return bpmFound ? Led::Blink : bpmScanning ? Led::On : Led::Off;
        case Key::WriteRename: return (op == Op::Write || op == Op::Rename || op == Op::GlobalWrite || op == Op::PatternSetWrite) ? Led::Blink : Led::Off;
        case Key::Keyboard: return keyboard ? Led::On : Led::Off;
        case Key::FxMotion: return p.fx[(size_t) fxEdit].motion ? Led::On : Led::Off;
        case Key::Roll: return s.roll ? Led::On : Led::Off;
        case Key::FxSend: return s.fxSend ? Led::On : Led::Off;
        case Key::ModBpmSync: return s.modBpmSync ? Led::On : Led::Off;
        case Key::Shift: return held[(size_t) Key::Shift] ? Led::On : Led::Off;
        case Key::PatternSet: return patternSetHold ? Led::Blink : held[(size_t) Key::PatternSet] ? Led::On : Led::Off;
        case Key::ResetErase: case Key::StopCancel: case Key::Tap: case Key::CursorLeft: case Key::CursorRight: case Key::FxChain:
        case Key::FxEditSelect: case Key::PartMotion: case Key::AmpEg: case Key::FxSelect: case Key::ModType: case Key::ModDest:
        case Key::FilterType: case Key::SelectLeft: case Key::SelectRight: case Key::Count:
            return Led::Off;
        default: break;
    }

    if (const int pt = partOfKey (k); pt >= 0)
    {
        // selections for an operation blink
        if ((op == Op::MoveSelect || op == Op::Move || op == Op::ClearPart || op == Op::ClearMotion || op == Op::Shift) && opParts.test ((size_t) pt))
            return Led::Blink;
        if ((op == Op::CopyPart || op == Op::CopySound) && (pt == opSourcePart || (opSourcePart < 0 && pt == part)))
            return Led::Blink;
        if (op == Op::DrumNote && pt == opSourcePart)
            return Led::On;
        if (mode == Mode::Midi && page[5] == 0 && isDrum (part))
            return isDrum (pt) ? Led::On : Led::Off; // the drum channel: all drum keys light
        if (held[(size_t) Key::PartMute])
            return p.parts[(size_t) pt].muted ? Led::Off : Led::On; // the mute status (p.23)
        if (solo.any())
            return solo.test ((size_t) pt) ? Led::On : Led::Off;
        return pt == part ? Led::On : Led::Off;
    }

    if (isStepKey (k))
    {
        const int key = stepKeyIndex (k);
        if (held[(size_t) Key::PatternSet] || patternSetHold)
        {
            if (chainFrom >= 0 && key >= std::min (chainFrom, chainTo) && key <= std::max (chainFrom, chainTo))
                return Led::On;
            const int idx = mem.patternSet[(size_t) (patternSetGroup * 16 + key)];
            return idx == patternIndex ? Led::On : Led::Off;
        }
        // the SHIFT function being set up blinks its key
        static const std::array<std::pair<Op, int>, 14> opKeys { { { Op::LastStep, 0 }, { Op::MoveSelect, 1 }, { Op::Move, 1 }, { Op::ShiftSelect, 2 },
            { Op::Shift, 2 }, { Op::CopyPart, 3 }, { Op::CopySound, 4 }, { Op::ClearMotion, 5 }, { Op::ClearPart, 6 }, { Op::ClearPattern, 7 },
            { Op::InsertPattern, 8 }, { Op::DeletePattern, 9 }, { Op::ClearSong, 10 }, { Op::MidiUtil, 11 } } };
        for (auto [o, kk] : opKeys)
            if (op == o)
                return key == kk ? Led::Blink : Led::Off;
        if (op == Op::CardMenu || op == Op::CardFile || op == Op::CardLoadWhat || op == Op::CardPattern || op == Op::CardSure || op == Op::CardName)
            return key == 12 ? Led::Blink : Led::Off;
        if (op == Op::MidiFilter || op == Op::DrumNote || op == Op::CcSelect || op == Op::CcNumber || op == Op::CcContinue || op == Op::Dump)
            return key == 11 ? Led::Blink : Led::Off;
        if (keyboard && isSynth (part))
        {
            // the key of the note playing lights
            const int n = heldNote[(size_t) part] >= 0 ? heldNote[(size_t) part] : (rack.isGateOn (part) ? (int) std::lround (rack.synthPitch (part)) : -1);
            return n >= 0 && n == mapping::keyboardNote (key + 1, octave) ? Led::On : Led::Off;
        }
        if (key >= p.lastStep)
            return Led::Off;
        const int step = stepFor (key);
        if (mode == Mode::StepEdit && page[2] >= 1 && step == noteEditStep)
            return Led::Blink;
        if (mode == Mode::StepEdit && page[2] == 0 && step == stepRecTarget)
            return Led::Blink;
        const bool on = p.parts[(size_t) part].steps[(size_t) step].on;
        if (transport != Transport::Stopped && (int) pos == step)
            return on ? Led::Off : Led::On; // the step playing shows inverted
        return on ? Led::On : Led::Off;
    }
    return Led::Off;
}

Led Machine::getLamp (Lamp l, int index) const
{
    const auto& p = playingConst();
    const auto& s = sound();
    auto on = [] (bool b) { return b ? Led::On : Led::Off; };
    switch (l)
    {
        case Lamp::Beat:
        {
            if (transport != Transport::Playing)
                return Led::Off;
            const double quarters = pos * p.stepQuarters();
            return on (quarters - std::floor (quarters) < 0.2);
        }
        case Lamp::SelectGreen:
            if (held[(size_t) Key::Shift] && transport == Transport::Stopped)
                return on (index < p.length); // SHIFT: the length (p.52)
            return on (transport != Transport::Stopped && (int) pos / std::max (1, (int) p.lastStep) == index);
        case Lamp::SelectRed:
            if (held[(size_t) Key::Shift] && transport == Transport::Stopped)
                return on (index == (int) p.beat); // and the beat
            if (held[(size_t) Key::PatternSet] || patternSetHold)
                return on (index == patternSetGroup);
            if (keyboard && isSynth (part))
                return on (index == octave - 1);
            return on (index == barShown);
        case Lamp::ChainOneTwo: return on (p.chain == Chain::OneTwo || p.chain == Chain::All);
        case Lamp::ChainTwoThree: return on (p.chain == Chain::TwoThree || p.chain == Chain::All);
        case Lamp::EditSelect: return on (index == fxEdit);
        case Lamp::MotionSmooth: return on (s.motion == MotionMode::Smooth);
        case Lamp::MotionTrigHold: return on (s.motion == MotionMode::TrigHold);
        case Lamp::AmpDecay: return on (s.ampEg == AmpEg::Decay);
        case Lamp::AmpGate: return on (s.ampEg == AmpEg::Gate);
        case Lamp::FxSelectLamp: return on (index == s.fxSelect);
        case Lamp::ModTypeLamp: return on (index == (int) s.modType);
        case Lamp::ModDestLamp: return on (index == (int) s.modDest);
        case Lamp::FilterTypeLamp: return on (isSynth (part) && index == (int) s.filterType);
        case Lamp::Count: break;
    }
    return Led::Off;
}

// ---------------------------------------------------------------- knobs

int Machine::getKnobValue (Knob k) const
{
    const auto& s = sound();
    const auto& f = playingConst().fx[(size_t) fxEdit];
    switch (k)
    {
        case Knob::MasterVolume: case Knob::TubeGain: case Knob::AudioInLevel: return knobs[(size_t) k];
        case Knob::FxType: return (int) f.type * 8 + 4;
        case Knob::FxEdit1: return f.edit1;
        case Knob::FxEdit2: return f.edit2;
        case Knob::PitchGlide: return isDrum (part) ? s.pitch : s.glide;
        case Knob::EgTime: return s.egTime;
        case Knob::Pan: return s.pan;
        case Knob::Level: return s.level;
        case Knob::ModSpeed: return s.modSpeed;
        case Knob::ModDepth: return s.modDepth;
        case Knob::OscType: return (int) s.oscType * 8 + 4;
        case Knob::OscEdit1: return s.oscEdit1;
        case Knob::OscEdit2: return s.oscEdit2;
        case Knob::Cutoff: return s.cutoff;
        case Knob::EgInt: return s.egInt;
        case Knob::Resonance: return s.resonance;
        case Knob::Drive: return s.drive;
        case Knob::Count: break;
    }
    return 0;
}

int Machine::getParam (int target, Param p) const
{
    const auto& pat = playingConst();
    if (target >= kFxTarget)
    {
        const auto& f = pat.fx[(size_t) (target - kFxTarget)];
        return p == Param::FxEdit1 ? f.edit1 : f.edit2;
    }
    const auto& s = pat.parts[(size_t) target].sound;
    switch (p)
    {
        case Param::PitchGlide: return isDrum (target) ? s.pitch : s.glide;
        case Param::EgTime: return s.egTime;
        case Param::Pan: return s.pan;
        case Param::Level: return s.level;
        case Param::AmpEg: return (int) s.ampEg;
        case Param::Roll: return s.roll ? 1 : 0;
        case Param::FxSend: return s.fxSend ? 1 : 0;
        case Param::FxSelect: return s.fxSelect;
        case Param::ModSpeed: return s.modSpeed;
        case Param::ModDepth: return s.modDepth;
        case Param::ModType: return (int) s.modType;
        case Param::ModDest: return (int) s.modDest;
        case Param::ModBpmSync: return s.modBpmSync ? 1 : 0;
        case Param::OscEdit1: return s.oscEdit1;
        case Param::OscEdit2: return s.oscEdit2;
        case Param::Cutoff: return s.cutoff;
        case Param::EgInt: return s.egInt;
        case Param::Resonance: return s.resonance;
        case Param::Drive: return s.drive;
        case Param::FilterType: return (int) s.filterType;
        case Param::FxEdit1: case Param::FxEdit2: case Param::Count: break;
    }
    return 0;
}

void Machine::setParam (int target, Param p, int v, bool fromKnob)
{
    auto& pat = playing();
    const auto u8 = [] (int x) { return (uint8_t) std::clamp (x, 0, 127); };
    if (target >= kFxTarget)
    {
        auto& f = pat.fx[(size_t) (target - kFxTarget)];
        (p == Param::FxEdit1 ? f.edit1 : f.edit2) = u8 (v);
    }
    else
    {
        auto& s = pat.parts[(size_t) target].sound;
        switch (p)
        {
            case Param::PitchGlide: (isDrum (target) ? s.pitch : s.glide) = u8 (v); break;
            case Param::EgTime: s.egTime = u8 (v); break;
            case Param::Pan: s.pan = u8 (v); break;
            case Param::Level: s.level = u8 (v); break;
            case Param::AmpEg: s.ampEg = (AmpEg) std::clamp (v, 0, 1); break;
            case Param::Roll: s.roll = v != 0; break;
            case Param::FxSend: s.fxSend = v != 0; break;
            case Param::FxSelect: s.fxSelect = (uint8_t) std::clamp (v, 0, 2); break;
            case Param::ModSpeed: s.modSpeed = u8 (v); break;
            case Param::ModDepth: s.modDepth = u8 (v); break;
            case Param::ModType: s.modType = (ModType) std::clamp (v, 0, 4); break;
            case Param::ModDest: s.modDest = (ModDest) std::clamp (v, 0, isSynth (target) ? 5 : 2); break;
            case Param::ModBpmSync: s.modBpmSync = v != 0; break;
            case Param::OscEdit1: s.oscEdit1 = u8 (v); break;
            case Param::OscEdit2: s.oscEdit2 = u8 (v); break;
            case Param::Cutoff: s.cutoff = u8 (v); break;
            case Param::EgInt: s.egInt = u8 (v); break;
            case Param::Resonance: s.resonance = u8 (v); break;
            case Param::Drive: s.drive = u8 (v); break;
            case Param::FilterType: s.filterType = (FilterType) std::clamp (v, 0, 3); break;
            case Param::FxEdit1: case Param::FxEdit2: case Param::Count: break;
        }
    }
    if (! fromKnob)
        return;
    // ---- recording: a motion sequence (pattern), or a song event
    if (mode == Mode::Song && eventRecord)
    {
        recordEvent (isSwitchParam (p) ? SongEvent::KeyParam : SongEvent::Knob, target, (int) p, v);
        return;
    }
    const bool motionOn = target >= kFxTarget ? pat.fx[(size_t) (target - kFxTarget)].motion
                                              : pat.parts[(size_t) target].sound.motion != MotionMode::Off;
    if (! (recording && transport == Transport::Playing && motionOn))
        return;
    for (auto& r : motionRec)
        if (r.slot >= 0 && r.target == target && r.param == p)
        {
            r.value = v; // already recording this one: keep the latest value for this step
            return;
        }
    // a new motion sequence: replaces one already recorded for this knob (p.58)
    int slot = pat.findMotion (target, p);
    if (slot < 0)
        slot = edit::freeMotionSlot (pat);
    if (slot < 0)
    {
        flash ("FreeMot", "0");
        return;
    }
    for (auto& r : motionRec)
        if (r.slot < 0)
        {
            auto& m = pat.motion[(size_t) slot];
            m.target = (int8_t) target;
            m.param = p;
            m.values.fill (-1);
            r = { slot, target, p, pat.numSteps(), v };
            m.values[(size_t) std::clamp ((int) pos, 0, kMaxSteps - 1)] = (int16_t) v;
            return;
        }
}

void Machine::setKnob (Knob k, int v)
{
    v = std::clamp (v, 0, 127);
    applyKnob (k, v);
    sendKnobCc (k, v);
}

void Machine::applyKnob (Knob k, int v)
{
    showKnob (k);
    auto& pat = playing();
    switch (k)
    {
        case Knob::MasterVolume: case Knob::TubeGain: case Knob::AudioInLevel:
            knobs[(size_t) k] = v;
            return;
        case Knob::FxType:
        {
            auto& f = pat.fx[(size_t) fxEdit];
            const auto t = (FxType) std::clamp (v * kNumFxTypes / 128, 0, kNumFxTypes - 1);
            if (t != f.type)
            {
                f.type = t;
                if (mode == Mode::Song && eventRecord)
                    recordEvent (SongEvent::KeyParam, kFxTarget + fxEdit, 100, (int) t);
            }
            return;
        }
        case Knob::FxEdit1: return setParam (kFxTarget + fxEdit, Param::FxEdit1, v, true);
        case Knob::FxEdit2: return setParam (kFxTarget + fxEdit, Param::FxEdit2, v, true);
        case Knob::OscType:
        {
            if (! isSynth (part))
                return; // synth oscillator controls do nothing on drum parts (p.11)
            auto& s = sound();
            const auto t = (OscType) std::clamp (v * kNumOscTypes / 128, 0, kNumOscTypes - 1);
            if (t != s.oscType)
            {
                s.oscType = t;
                s.wave = (uint8_t) std::min ((int) s.wave, mapping::waveCount (t) - 1);
            }
            return;
        }
        case Knob::PitchGlide: return setParam (part, Param::PitchGlide, v, true);
        case Knob::EgTime: return setParam (part, Param::EgTime, v, true);
        case Knob::Pan: return setParam (part, Param::Pan, v, true);
        case Knob::Level: return setParam (part, Param::Level, v, true);
        case Knob::ModSpeed: return setParam (part, Param::ModSpeed, v, true);
        case Knob::ModDepth: return setParam (part, Param::ModDepth, v, true);
        case Knob::OscEdit1: case Knob::OscEdit2: case Knob::Cutoff: case Knob::EgInt: case Knob::Resonance: case Knob::Drive:
        {
            if (! isSynth (part))
                return;
            static const Param map[6] = { Param::OscEdit1, Param::OscEdit2, Param::Cutoff, Param::EgInt, Param::Resonance, Param::Drive };
            return setParam (part, map[(int) k - (int) Knob::OscEdit1], v, true);
        }
        case Knob::Count: break;
    }
}

void Machine::cycleSoundKey (Key k)
{
    if (isAccent (part) && k != Key::PartMotion)
        return; // accent parts have only LEVEL (and its motion)
    auto& s = sound();
    switch (k)
    {
        case Key::AmpEg: setParam (part, Param::AmpEg, s.ampEg == AmpEg::Decay ? 1 : 0, true); break;
        case Key::Roll: setParam (part, Param::Roll, s.roll ? 0 : 1, true); break;
        case Key::FxSelect: setParam (part, Param::FxSelect, (s.fxSelect + 1) % 3, true); break;
        case Key::FxSend: setParam (part, Param::FxSend, s.fxSend ? 0 : 1, true); break;
        case Key::ModType: setParam (part, Param::ModType, ((int) s.modType + 1) % 5, true); break;
        case Key::ModDest: setParam (part, Param::ModDest, ((int) s.modDest + 1) % (isSynth (part) ? 6 : 3), true); break;
        case Key::ModBpmSync: setParam (part, Param::ModBpmSync, s.modBpmSync ? 0 : 1, true); break;
        case Key::FilterType:
            if (isSynth (part))
                setParam (part, Param::FilterType, ((int) s.filterType + 1) % 4, true);
            break;
        case Key::PartMotion:
            s.motion = (MotionMode) (((int) s.motion + 1) % 3); // off -> SMOOTH -> TRIG HOLD -> off
            break;
        default: break;
    }
    sendKeyCc (k);
}

// ---------------------------------------------------------------- keys

int Machine::barsForKeys() const { return std::max (1, (int) playingConst().length); }
int Machine::stepFor (int key) const { return std::clamp (barShown, 0, barsForKeys() - 1) * playingConst().lastStep + key; }

void Machine::press (Key k)
{
    if (k == Key::Count)
        return;
    held[(size_t) k] = true;
    if (k == Key::Shift)
    {
        shiftUsed = false;
        return;
    }
    flashUntil = 0.0; // a message stays up only until the next thing you do
    const bool shift = held[(size_t) Key::Shift];
    if (shift)
        shiftUsed = true;
    if (k == Key::Solo)
    {
        soloUsed = false;
        return; // acts when another key comes with it, or on release
    }
    if (k == Key::PartMute && stepRecord)
    {
        // step recording: REST/TIE enters a rest, or (with a step key held) a tie (p.49)
        auto& pat = playing();
        bool tie = false;
        for (int i = 0; i < 16; ++i)
            tie = tie || held[(size_t) Key::Step1 + (size_t) i];
        if (tie && stepRecTarget > 0)
        {
            auto& prev = pat.parts[(size_t) part].steps[(size_t) (stepRecTarget - 1)];
            prev.gate = (uint16_t) std::min (512, prev.gate + 4); // the note before carries on through this step
            pat.parts[(size_t) part].steps[(size_t) stepRecTarget].on = false;
        }
        else
            pat.parts[(size_t) part].steps[(size_t) stepRecTarget].on = false;
        if (++stepRecTarget >= pat.numSteps())
        {
            stepRecTarget = 0;
            stepRecord = false;
        }
        return;
    }
    if (k == Key::PartMute || k == Key::PatternSet)
    {
        if (k == Key::PatternSet && shift)
            patternSetHold = ! patternSetHold; // SHIFT + PATTERN SET holds the function (p.62)
        else if (k == Key::PatternSet && patternSetHold)
        {
            patternSetHold = false;
            chainFrom = chainTo = chainPos = -1;
        }
        lastPatternSetKey = -1;
        return;
    }
    if (const int p = partOfKey (k); p >= 0)
        return pressPart (p);
    if (isStepKey (k))
        return shift && op == Op::None ? pressShiftStep (stepKeyIndex (k)) : pressStep (stepKeyIndex (k));

    switch (k)
    {
        case Key::ModePattern:
            if (shift)
            {
                // COMPARE: play the stored pattern; PATTERN again goes back to the edits (p.57)
                if (op == Op::Compare)
                    break;
                if (recording || recordReady)
                    break;
                compareBackup = edit;
                edit = stored();
                op = Op::Compare;
                break;
            }
            if (op == Op::Compare)
            {
                edit = compareBackup;
                op = Op::None;
                break;
            }
            pressMode (Mode::Pattern);
            break;
        case Key::ModePartEdit: pressMode (Mode::PartEdit); break;
        case Key::ModeStepEdit: pressMode (Mode::StepEdit); break;
        case Key::ModeSong: pressMode (Mode::Song); break;
        case Key::ModeGlobal: pressMode (Mode::Global); break;
        case Key::ModeMidi: pressMode (Mode::Midi); break;
        case Key::CursorLeft: case Key::CursorRight:
        {
            const int dir = k == Key::CursorRight ? 1 : -1;
            if (op == Op::Rename || op == Op::CardName)
            {
                opCursor = std::clamp (opCursor + dir, 0, 7);
                break;
            }
            if (op != Op::None && op != Op::Compare)
                break;
            bpmScanning = bpmFound = false; // the cursor keys cancel an auto BPM result (p.21)
            const int n = numPages();
            page[(size_t) mode] = (page[(size_t) mode] + dir + n) % n;
            shownKnob = -1;
            break;
        }
        case Key::StopCancel: pressStop(); break;
        case Key::PlayPause: pressPlay(); break;
        case Key::Rec: pressRec(); break;
        case Key::ResetErase:
            if (shift)
            {
                erasing = true; // SHIFT + hold: erase triggers as they pass (p.48)
                if (mode == Mode::Song && eventRecord)
                {
                    const uint32_t t = songTickNow();
                    auto& ev = songEdit.events;
                    ev.erase (std::remove_if (ev.begin(), ev.end(), [t] (const SongEvent& e) { return e.tick >= t && e.tick < t + kSongPPQ; }), ev.end());
                }
                break;
            }
            // back to the start of the pattern (or the song position)
            pos = 0.0;
            lastStepPlayed = -1;
            if (mode == Mode::Song && songActive)
                loadSongPosition (songPosition);
            if (chainFrom >= 0)
                chainPos = std::min (chainFrom, chainTo);
            break;
        case Key::Tap:
        {
            bpmScanning = bpmFound = false;
            const double now = clockSeconds;
            if (now - lastTapSeconds > 2.0)
                taps.clearQuick();
            taps.add (now);
            lastTapSeconds = now;
            if (taps.size() >= 2)
            {
                const int nTaps = std::min (taps.size(), 5);
                const double span = taps.getLast() - taps[taps.size() - nTaps];
                const double bpm = 60.0 * (nTaps - 1) / std::max (1.0e-3, span);
                const float t = (float) std::clamp (bpm, 20.0, 300.0);
                if (mode == Mode::Song)
                    songEdit.tempo = t;
                else if (tempoLock)
                    lockedTempo = t;
                else
                    edit.tempo = lockedTempo = t;
                if (mode == Mode::Song && eventRecord)
                    recordEvent (SongEvent::Tempo, 0, 0, (int) std::lround (t * 10.0f));
            }
            break;
        }
        case Key::AudioInThru: audioThru = ! audioThru; break;
        case Key::TempoLock:
            bpmScanning = bpmFound = false;
            if (mode == Mode::Song)
                songEdit.tempoLock = ! songEdit.tempoLock;
            else
            {
                tempoLock = ! tempoLock;
                if (tempoLock)
                    lockedTempo = (float) getTempo();
                else
                    lockedTempo = edit.tempo;
            }
            break;
        case Key::AutoBpm:
            if (bpmFound)
            {
                if (detectedBpm > 0)
                {
                    if (mode == Mode::Song)
                        songEdit.tempo = detectedBpm;
                    else
                        edit.tempo = lockedTempo = detectedBpm;
                }
                bpmFound = bpmScanning = false;
            }
            else if (! bpmScanning)
            {
                bpmScanning = true;
                onsetWrite = 0;
                std::fill (onsetEnv.begin(), onsetEnv.end(), 0.0f);
            }
            else
                bpmScanning = false;
            break;
        case Key::WriteRename: pressWrite(); break;
        case Key::FxChain:
            playing().chain = (Chain) (((int) playing().chain + 1) % 4);
            if (mode == Mode::Song && eventRecord)
                recordEvent (SongEvent::KeyParam, kFxTarget, 101, (int) playing().chain);
            sendKeyCc (k);
            break;
        case Key::FxEditSelect: fxEdit = (fxEdit + 1) % kNumFx; break;
        case Key::FxMotion:
            if (op == Op::ClearMotion || op == Op::MoveSelect || op == Op::Move)
            {
                opFx.flip ((size_t) fxEdit);
                break;
            }
            playing().fx[(size_t) fxEdit].motion = ! playing().fx[(size_t) fxEdit].motion;
            sendKeyCc (k);
            break;
        case Key::PartMotion:
            if (shift)
            {
                flash ("FreeMot", juce::String (kMaxMotion - playingConst().motionCount()), 1.5);
                break;
            }
            if (op == Op::ClearMotion)
            {
                opParts.flip ((size_t) part);
                break;
            }
            cycleSoundKey (k);
            break;
        case Key::AmpEg: case Key::Roll: case Key::FxSelect: case Key::FxSend: case Key::ModType: case Key::ModDest: case Key::ModBpmSync:
        case Key::FilterType:
            cycleSoundKey (k);
            break;
        case Key::Keyboard:
            if (isSynth (part))
                keyboard = ! keyboard; // only with a synth part selected (p.22)
            break;
        case Key::SelectLeft: case Key::SelectRight:
        {
            const int dir = k == Key::SelectRight ? 1 : -1;
            if (held[(size_t) Key::PatternSet] || patternSetHold)
                patternSetGroup = std::clamp (patternSetGroup + dir, 0, kPatternSetGroups - 1);
            else if (held[(size_t) Key::Solo])
            {
                transpose = std::clamp (transpose + 12 * dir, -24, 24);
                soloUsed = true;
            }
            else if (mode == Mode::StepEdit && shift)
            {
                if (page[2] == 0)
                    stepRecTarget = std::clamp (stepRecTarget + dir, 0, playingConst().numSteps() - 1);
                else
                    noteEditStep = std::clamp (noteEditStep + dir, 0, playingConst().numSteps() - 1);
            }
            else if (mode == Mode::Song && (! keyboard || shift))
            {
                // fast-forward / rewind through the positions
                songPosition = std::clamp (songPosition + dir, 0, std::max (0, songEdit.numPositions()));
                if (songActive && songPosition < songEdit.numPositions())
                    loadSongPosition (songPosition);
            }
            else if (keyboard && isSynth (part))
                octave = std::clamp (octave + dir, 1, 8);
            else
                barShown = std::clamp (barShown + dir, 0, barsForKeys() - 1);
            break;
        }
        default: break;
    }
}

void Machine::release (Key k)
{
    if (k == Key::Count)
        return;
    held[(size_t) k] = false;
    if (k == Key::Solo && ! soloUsed)
    {
        // press and let go: solo and mute are both defeated (p.23)
        solo.reset();
        for (auto& pt : playing().parts)
            pt.muted = false;
    }
    if (k == Key::ResetErase)
        erasing = false;
    if (k == Key::PatternSet && lastPatternSetKey >= 0 && transport == Transport::Stopped && ! patternSetHold && op != Op::PatternSetWrite)
    {
        // an assignment made with the dial while holding ends here
    }
    if (isStepKey (k))
        releaseStep (stepKeyIndex (k));
    if (const int p = partOfKey (k); p >= 0 && isDrum (p))
    {
        // (a drum rolled while its key was held: the roll stops with the key)
    }
}

void Machine::pressMode (Mode m)
{
    if (op != Op::None && op != Op::Compare)
        cancelOp();
    bpmScanning = bpmFound = false;
    if (m == Mode::Song && mode != Mode::Song)
    {
        songEdit = mem.songs[(size_t) songIndex];
        songPosition = 0;
    }
    if (mode == Mode::Song && m != Mode::Song && transport != Transport::Stopped)
        stopPlayback();
    mode = m;
    shownKnob = -1;
}

void Machine::pressPart (int p)
{
    const bool shift = held[(size_t) Key::Shift];
    // ---- choosing parts for an operation
    switch (op)
    {
        case Op::MoveSelect: case Op::Move: case Op::ClearPart:
            opParts.flip ((size_t) p);
            if (isDrum (p))
                triggerPart (p, mapping::accentedVelocity (playingConst().parts[(size_t) kDrumAccent].sound.level), 60, false);
            return;
        case Op::ClearMotion:
            opParts.flip ((size_t) p);
            return;
        case Op::Shift:
            if (isSynth (p))
                opParts.flip ((size_t) p);
            return;
        case Op::CopyPart: case Op::CopySound:
            opSourcePart = p;
            return;
        case Op::DrumNote:
            if (isDrum (p))
                opSourcePart = p;
            return;
        default: break;
    }
    if (op != Op::None && op != Op::Compare)
        return;

    // ---- mute and solo
    if (held[(size_t) Key::PartMute])
    {
        auto& pt = playing().parts;
        if (shift)
        {
            // SHIFT: the whole group (drums or synths) on / off
            const bool drums = isDrum (p) || p == kDrumAccent;
            bool anyOn = false;
            for (int i = 0; i < kNumParts; ++i)
                if ((isDrum (i) || i == kDrumAccent) == drums)
                    anyOn = anyOn || ! pt[(size_t) i].muted;
            for (int i = 0; i < kNumParts; ++i)
                if ((isDrum (i) || i == kDrumAccent) == drums)
                    pt[(size_t) i].muted = anyOn;
        }
        else
            pt[(size_t) p].muted = ! pt[(size_t) p].muted;
        if (mode == Mode::Song && eventRecord)
            recordEvent (SongEvent::Mute, p, 0, pt[(size_t) p].muted ? 1 : 0);
        return;
    }
    if (held[(size_t) Key::Solo])
    {
        soloUsed = true;
        if (solo.any() || ! solo.any())
            solo.set ((size_t) p); // hold SOLO: add parts (p.23)
        return;
    }
    if (solo.any())
    {
        solo.reset(); // SOLO lit: another part key moves the solo there
        solo.set ((size_t) p);
    }

    // ---- MIDI mode: choose the part whose channel to set
    // ---- select (and sound) the part
    const bool wasDrum = isDrum (part);
    part = p;
    if (isDrum (p) || isAccent (p))
    {
        if (keyboard)
            keyboardBeforeDrum = true;
        keyboard = false; // the keyboard function waits until a synth part is chosen again (p.22)
    }
    else if (isSynth (p))
    {
        if (wasDrum && keyboardBeforeDrum)
            keyboard = true;
        keyboardBeforeDrum = false;
        lastSynthPart = p;
    }
    if (isDrum (p) && ! shift)
    {
        // a drum key plays its part at the accented level (p.23), and records while recording
        triggerPart (p, mapping::accentedVelocity (playingConst().parts[(size_t) kDrumAccent].sound.level), 60, false);
        if (recording && transport == Transport::Playing)
            recordLiveTrigger (p, 60);
        if (mode == Mode::Song && eventRecord)
            recordEvent (SongEvent::DrumNote, p, 0, 127);
    }
}

void Machine::pressStep (int key)
{
    auto& pat = playing();
    // ---- pattern set: the step keys pick patterns
    if (held[(size_t) Key::PatternSet] || patternSetHold)
    {
        lastPatternSetKey = key;
        if (patternSetHold)
        {
            // chain play: hold one step key and press another (p.62)
            for (int other = 0; other < 16; ++other)
                if (other != key && held[(size_t) Key::Step1 + (size_t) other])
                {
                    chainFrom = other;
                    chainTo = key;
                    chainPos = std::min (other, key);
                    selectPattern (mem.patternSet[(size_t) (patternSetGroup * 16 + chainPos)]);
                    return;
                }
            chainFrom = chainTo = chainPos = -1;
        }
        selectPattern (mem.patternSet[(size_t) (patternSetGroup * 16 + key)]);
        return;
    }
    // ---- SOLO / TRANSPOSE + step key: transpose, step key 4 = C (p.24)
    if (held[(size_t) Key::Solo])
    {
        soloUsed = true;
        transpose = std::clamp (key - 3 + (transpose >= 12 ? 12 : transpose <= -12 ? -12 : 0), -24, 24);
        return;
    }
    // ---- the step key of an operation in progress executes it
    if (op != Op::None && op != Op::Compare)
    {
        const int confirm[] = { 0, 1, 1, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
        juce::ignoreUnused (confirm);
        pressShiftStep (key); // (the same key again confirms)
        return;
    }
    // ---- KEYBOARD: play the synth part
    if (keyboard && isSynth (part))
    {
        const int note = std::clamp (mapping::keyboardNote (key + 1, octave), 0, 127);
        if (mode == Mode::StepEdit && page[2] == 1)
        {
            pat.parts[(size_t) part].steps[(size_t) noteEditStep].note = (uint8_t) note; // NoteNo by keyboard (p.51)
            triggerPart (part, 100, note, false);
            return;
        }
        if (stepRecord)
        {
            // step recording: the note goes in and the target moves on (p.49)
            auto& st = pat.parts[(size_t) part].steps[(size_t) stepRecTarget];
            st.on = true;
            st.note = (uint8_t) note;
            st.gate = 3;
            triggerPart (part, 100, note, false);
            heldNote[(size_t) part] = note;
            if (++stepRecTarget >= pat.numSteps())
            {
                stepRecTarget = 0;
                stepRecord = false;
            }
            return;
        }
        // in Pattern mode, the key pressed alone sets the arpeggiator's centre note (p.27)
        int keysDown = 0;
        for (int i = 0; i < 16; ++i)
            keysDown += held[(size_t) Key::Step1 + (size_t) i] ? 1 : 0;
        if (mode == Mode::Pattern && keysDown == 1)
            pat.arpCentre = (uint8_t) note;
        if (arpCount < (int) arpNotes.size())
            arpNotes[(size_t) arpCount++] = note;
        if (ribbonDown)
            return; // the arpeggiator plays the held keys
        const bool legato = heldNote[(size_t) part] >= 0;
        heldNote[(size_t) part] = note;
        triggerPart (part, mapping::accentedVelocity (pat.parts[(size_t) kSynthAccent].sound.level), note, false, legato);
        if (recording && transport == Transport::Playing)
            recordLiveTrigger (part, note);
        if (mode == Mode::Song && eventRecord)
            recordEvent (SongEvent::KeyNoteOn, part, note, 100);
        return;
    }
    // ---- step edit pages pick a step
    if (mode == Mode::StepEdit)
    {
        if (key < pat.lastStep)
        {
            if (page[2] == 0)
                stepRecTarget = stepFor (key);
            else
                noteEditStep = stepFor (key);
        }
        return; // (no trigger editing while STEP EDIT is lit, p.51)
    }
    // ---- toggle a trigger
    if (key >= pat.lastStep)
        return; // past LAST STEP the keys do nothing
    auto& st = pat.parts[(size_t) part].steps[(size_t) stepFor (key)];
    st.on = ! st.on;
}

void Machine::releaseStep (int key)
{
    juce::ignoreUnused (key);
    if (keyboard && isSynth (part))
    {
        // the note of the key let go stops (unless other keys are still down: then the last one plays)
        const int note = std::clamp (mapping::keyboardNote (key + 1, octave), 0, 127);
        for (int i = 0; i < arpCount; ++i)
            if (arpNotes[(size_t) i] == note)
            {
                for (int j = i; j < arpCount - 1; ++j)
                    arpNotes[(size_t) j] = arpNotes[(size_t) j + 1];
                --arpCount;
                break;
            }
        if (heldNote[(size_t) part] == note)
        {
            if (arpCount > 0 && ! ribbonDown)
            {
                heldNote[(size_t) part] = arpNotes[(size_t) arpCount - 1];
                triggerPart (part, 100, heldNote[(size_t) part], false, true);
            }
            else
            {
                if (recording && transport == Transport::Playing)
                    recordLiveRelease (part);
                heldNote[(size_t) part] = -1;
                if (! ribbonDown)
                    noteOffPart (part);
                if (mode == Mode::Song && eventRecord)
                    recordEvent (SongEvent::KeyNoteOff, part, note, 0);
            }
        }
    }
}

void Machine::pressShiftStep (int key)
{
    auto& pat = playing();
    const bool stopped = transport == Transport::Stopped;
    // the same key again: carry on / execute
    switch (op)
    {
        case Op::LastStep:
            if (key == 0)
            {
                pat.lastStep = (uint8_t) opValue;
                op = Op::None;
            }
            return;
        case Op::MoveSelect:
            if (key == 1)
            {
                opValue2 = opValue; // 0 move, 1 repeat, 2 random
                opValue = opValue2 == 1 ? 1 : 0;
                opParts.reset();
                opParts.set ((size_t) part);
                opFx.reset();
                op = Op::Move;
            }
            return;
        case Op::Move:
            if (key == 1)
            {
                if (opValue2 == 0)
                    edit::moveData (pat, opParts, opFx, opValue);
                else if (opValue2 == 1)
                    edit::repeatData (pat, opParts, opFx, std::max (1, opValue));
                else
                    edit::randomData (pat, opParts, (uint32_t) random.nextInt());
                op = Op::None;
                flash ("Complete");
            }
            return;
        case Op::ShiftSelect:
            if (key == 2)
            {
                opValue2 = opValue;
                opValue = 0;
                opParts.reset();
                for (int i = kFirstSynth; i < kFirstSynth + kNumSynthParts; ++i)
                    opParts.set ((size_t) i);
                op = Op::Shift;
            }
            return;
        case Op::Shift:
            if (key == 2)
            {
                if (opValue2 == 0)
                    edit::shiftNotes (pat, opParts, opValue);
                else
                    edit::randomNotes (pat, opParts, (uint32_t) random.nextInt());
                op = Op::None;
                flash ("Complete");
            }
            return;
        case Op::CopyPart: case Op::CopySound:
            if (key == (op == Op::CopyPart ? 3 : 4))
            {
                if (opSourcePart < 0)
                    return;
                const auto& src = opValue == patternIndex ? playingConst() : mem.patterns[(size_t) opValue];
                if (op == Op::CopyPart)
                    edit::copyPart (pat, part, src, opSourcePart);
                else if (! edit::copySound (pat, part, src, opSourcePart))
                {
                    flash ("Can't", "Copy");
                    op = Op::None;
                    return;
                }
                op = Op::None;
                flash ("Complete");
            }
            return;
        case Op::ClearMotion:
            if (key == 5)
            {
                edit::clearMotion (pat, opParts, opFx);
                op = Op::None;
                flash ("Complete");
            }
            return;
        case Op::ClearPart:
            if (key == 6)
            {
                edit::clearParts (pat, opParts);
                op = Op::None;
                flash ("Complete");
            }
            return;
        case Op::ClearPattern:
            if (key == 7)
            {
                pat = initPattern(); // sounds, sequences, motion, tempo, length, beat: all initialized (p.57)
                op = Op::None;
                flash ("Complete");
            }
            return;
        case Op::InsertPattern:
            if (key == 8)
            {
                auto& ps = songEdit.positions;
                const int at = std::clamp (opValue, 0, kMaxPositions - 1);
                const Position copy = ps[(size_t) at].pattern >= 0 ? ps[(size_t) at] : Position { (int16_t) patternIndex, 0 };
                for (int i = kMaxPositions - 1; i > at; --i)
                    ps[(size_t) i] = ps[(size_t) i - 1];
                ps[(size_t) at] = copy;
                op = Op::None;
                flash ("Complete");
            }
            return;
        case Op::DeletePattern:
            if (key == 9)
            {
                auto& ps = songEdit.positions;
                const int at = std::clamp (opValue, 0, kMaxPositions - 1);
                for (int i = at; i < kMaxPositions - 1; ++i)
                    ps[(size_t) i] = ps[(size_t) i + 1];
                ps[(size_t) kMaxPositions - 1] = Position();
                op = Op::None;
                flash ("Complete");
            }
            return;
        case Op::ClearSong:
            if (key == 10)
            {
                const auto name = songEdit.getName();
                songEdit = initSong();
                songEdit.setName (name);
                songPosition = 0;
                op = Op::None;
                flash ("Complete");
            }
            return;
        case Op::MidiUtil:
            if (key == 11)
            {
                const int chosen = opValue;
                op = chosen == 0 ? Op::MidiFilter : chosen == 1 ? Op::DrumNote : chosen == 2 ? Op::CcSelect : Op::Dump;
                opValue = 0;
                opSourcePart = chosen == 1 ? 0 : -1; // DrNote starts on drum 1
            }
            return;
        case Op::MidiFilter: case Op::DrumNote:
            if (key == 11)
                op = Op::None;
            return;
        case Op::CcSelect:
            if (key == 11)
                op = Op::CcNumber;
            return;
        case Op::CcNumber:
            if (key == 11)
            {
                op = Op::CcContinue;
                opValue2 = 0;
            }
            return;
        case Op::CcContinue:
            if (key == 11)
                op = opValue2 ? Op::CcSelect : Op::None;
            return;
        case Op::Dump:
            if (key == 11)
            {
                pendingCard = { CardRequest::DumpTx, {}, 0, opValue };
                op = Op::None;
                flash ("Transmit");
            }
            return;
        case Op::CardMenu:
            if (key == 12)
            {
                const int what = opValue;
                cardFiles = listCardFiles();
                if (what == 0)
                {
                    op = Op::CardLoadWhat;
                    opValue = 0;
                }
                else if (what == 1)
                {
                    op = Op::CardName;
                    opName = "VALVEBOX";
                    opCursor = 0;
                    opValue2 = 1;
                }
                else if (what == 2)
                {
                    op = Op::CardFile;
                    opValue = 0;
                    opValue2 = 2;
                }
                else
                {
                    op = Op::CardSure;
                    opValue = 0;
                    opValue2 = 3;
                }
            }
            return;
        case Op::CardLoadWhat:
            if (key == 12)
            {
                opValue2 = opValue == 0 ? 0 : 1; // 0 load all, 1 one pattern
                op = Op::CardFile;
                opValue = 0;
            }
            return;
        case Op::CardFile:
            if (key == 12)
            {
                if (cardFiles.isEmpty())
                {
                    op = Op::None;
                    flash ("NoFile");
                    return;
                }
                opName = cardFiles[opValue];
                if (opValue2 == 1)
                {
                    op = Op::CardPattern;
                    opValue = 0;
                }
                else
                {
                    op = Op::CardSure;
                    opValue = 0;
                }
            }
            return;
        case Op::CardPattern:
            if (key == 12)
            {
                pendingCard = { CardRequest::LoadPattern, cardFolder.getChildFile (opName + ".vbx"), opValue, 0 };
                op = Op::None;
            }
            return;
        case Op::CardName:
            if (key == 12)
            {
                opName = opName.trimEnd();
                const auto f = cardFolder.getChildFile (opName + ".vbx");
                op = Op::CardSure;
                opValue = 0;
                opValue2 = f.existsAsFile() ? 9 : 1; // OverWrt? when the file's there already
            }
            return;
        case Op::CardSure:
            if (key == 12)
            {
                if (opValue == 1)
                {
                    const auto f = cardFolder.getChildFile (opName.trimEnd() + ".vbx");
                    if (opValue2 == 1 || opValue2 == 9)
                        pendingCard = { CardRequest::SaveAll, f, 0, 0 };
                    else if (opValue2 == 0)
                    {
                        if (mem.global.protect)
                        {
                            op = Op::None;
                            flash ("Protect", "On");
                            return;
                        }
                        pendingCard = { CardRequest::LoadAll, f, 0, 0 };
                    }
                    else if (opValue2 == 2)
                        pendingCard = { CardRequest::Delete, f, 0, 0 };
                    else
                        pendingCard = { CardRequest::Format, cardFolder, 0, 0 };
                }
                op = Op::None;
            }
            return;
        default: break;
    }
    if (op != Op::None && op != Op::Compare)
        return;

    // ---- SHIFT + step key: start a function (the label under the key)
    const bool patternModes = mode == Mode::Pattern || mode == Mode::PartEdit || mode == Mode::StepEdit;
    if (key <= 7 && patternModes)
    {
        if (! stopped)
        {
            flash ("Stop", "Seq");
            return;
        }
        switch (key)
        {
            case 0: op = Op::LastStep; opValue = pat.lastStep; break;
            case 1: op = Op::MoveSelect; opValue = 0; break;
            case 2: op = Op::ShiftSelect; opValue = 0; break;
            case 3: op = Op::CopyPart; opValue = patternIndex; opSourcePart = -1; break;
            case 4: op = Op::CopySound; opValue = patternIndex; opSourcePart = -1; break;
            case 5: op = Op::ClearMotion; opParts.reset(); opParts.set ((size_t) part); opFx.reset(); break;
            case 6: op = Op::ClearPart; opParts.reset(); opParts.set ((size_t) part); break;
            default: op = Op::ClearPattern; break;
        }
        return;
    }
    if (key >= 8 && key <= 10 && mode == Mode::Song)
    {
        if (! stopped)
        {
            flash ("Stop", "Seq");
            return;
        }
        op = key == 8 ? Op::InsertPattern : key == 9 ? Op::DeletePattern : Op::ClearSong;
        opValue = songPosition;
        return;
    }
    if (key == 11 && mode == Mode::Midi)
    {
        op = Op::MidiUtil;
        opValue = 0;
        return;
    }
    if (key == 12 && mode == Mode::Global)
    {
        op = Op::CardMenu;
        opValue = 0;
        return;
    }
}

void Machine::cancelOp()
{
    if (op == Op::Compare)
        edit = compareBackup;
    op = Op::None;
    opParts.reset();
    opFx.reset();
    opSourcePart = -1;
}

void Machine::pressWrite()
{
    const bool shift = held[(size_t) Key::Shift];
    // PATTERN SET held: write the pattern set (p.63)
    if (held[(size_t) Key::PatternSet] && op == Op::None)
    {
        op = Op::PatternSetWrite;
        opValue = 0;
        return;
    }
    switch (op)
    {
        case Op::Write:
        {
            if (mode == Mode::Song)
            {
                mem.songs[(size_t) opValue] = songEdit;
                songIndex = opValue;
            }
            else
            {
                mem.patterns[(size_t) opValue] = edit;
                patternIndex = opValue; // the destination is selected (p.64)
            }
            memoryChanged = true;
            op = Op::None;
            flash ("Complete");
            return;
        }
        case Op::Rename:
        {
            if (mode == Mode::Song)
                songEdit.setName (opName.trimEnd());
            else
                edit.setName (opName.trimEnd());
            op = Op::Write;
            opValue = mode == Mode::Song ? songIndex : patternIndex;
            return;
        }
        case Op::PatternSetWrite:
            if (opValue == 1)
            {
                memoryChanged = true;
                flash ("Complete");
            }
            op = Op::None;
            return;
        case Op::GlobalWrite:
            if (opValue == 1)
            {
                memoryChanged = true; // (Global and MIDI write regardless of Protect, p.76)
                flash ("Complete");
            }
            op = Op::None;
            return;
        case Op::CardName:
            return;
        case Op::None: case Op::Compare: break;
        default: return;
    }
    if (op == Op::Compare)
        return;
    if (mode == Mode::Global || mode == Mode::Midi)
    {
        op = Op::GlobalWrite;
        opValue = 0;
        return;
    }
    if (mem.global.protect)
    {
        flash ("Protect", "On"); // memory protect: on from the factory (p.64)
        return;
    }
    if (shift)
    {
        op = Op::Rename;
        opName = (mode == Mode::Song ? songEdit.getName() : edit.getName()).paddedRight (' ', 8).substring (0, 8);
        opCursor = 0;
        return;
    }
    op = Op::Write;
    opValue = mode == Mode::Song ? songIndex : patternIndex;
}

void Machine::pressStop()
{
    if (op != Op::None && op != Op::Compare)
    {
        cancelOp();
        return;
    }
    stepRecord = false;
    stopPlayback();
}

void Machine::pressPlay()
{
    if (op != Op::None && op != Op::Compare && op != Op::Write)
        return;
    if (transport == Transport::Playing)
    {
        transport = Transport::Paused; // PLAY again pauses (the key blinks)
        rack.allNotesOff();
        releaseSentNotes (true);
        midiOutBuffer.addEvent (juce::MidiMessage::midiStop(), sampleInBlock);
        return;
    }
    if (transport == Transport::Paused)
    {
        transport = Transport::Playing;
        midiOutBuffer.addEvent (juce::MidiMessage::midiContinue(), sampleInBlock);
        return;
    }
    startPlayback (true);
}

void Machine::pressRec()
{
    if (mode == Mode::StepEdit && page[2] == 0)
    {
        // step recording: ready at the target step; PLAY doesn't start playback (p.49)
        if (transport != Transport::Stopped)
            stopPlayback();
        stepRecord = ! stepRecord;
        return;
    }
    if (transport == Transport::Stopped)
    {
        recordReady = ! recordReady; // REC lights, PLAY blinks
        return;
    }
    // during playback REC punches in and out
    recording = ! recording;
    if (mode == Mode::Song)
        eventRecord = recording;
    if (! recording)
        for (auto& r : motionRec)
            r = MotionRec();
}

void Machine::startPlayback (bool fromTop)
{
    patternSetHold = patternSetHold && ! recordReady; // no pattern set while recording (p.62)
    if (mode == Mode::Song)
    {
        if (songEdit.numPositions() == 0)
        {
            flash ("No", "Pattern");
            return;
        }
        songActive = true;
        eventRecord = recordReady;
        if (fromTop && songPosition >= songEdit.numPositions())
            songPosition = 0;
        loadSongPosition (songPosition);
        songEventIndex = 0;
        songStepBase = 0.0;
        for (int i = 0; i < songPosition; ++i)
        {
            const auto& ps = songEdit.positions[(size_t) i];
            const auto& pt = mem.patterns[(size_t) std::max (0, (int) ps.pattern)];
            songStepBase += pt.numSteps() * pt.stepQuarters() / 0.25;
        }
    }
    pos = 0.0;
    lastStepPlayed = -1;
    recording = recordReady;
    recordReady = false;
    // a count-in before recording (METRONOME Rec1 / Rec2)
    countIn = 0;
    if (recording && (metronome == Metronome::Rec1 || metronome == Metronome::Rec2))
        countIn = (int) ((metronome == Metronome::Rec1 ? 4 : 8) * 60.0 / getTempo() * sampleRate);
    countInLength = countIn;
    transport = Transport::Playing;
    clockTickAcc = 0.0;
    midiOutBuffer.addEvent (juce::MidiMessage::midiStart(), sampleInBlock);
}

void Machine::stopPlayback()
{
    if (transport != Transport::Stopped)
        midiOutBuffer.addEvent (juce::MidiMessage::midiStop(), sampleInBlock);
    transport = Transport::Stopped;
    recording = recordReady = false;
    eventRecord = false;
    songActive = false;
    pos = 0.0;
    lastStepPlayed = -1;
    countIn = 0;
    for (auto& r : motionRec)
        r = MotionRec();
    for (int p = 0; p < kNumParts; ++p)
    {
        if (isSynth (p) && heldNote[(size_t) p] < 0)
            rack.noteOff (p);
        gateOn[(size_t) p] = false;
        offAt[(size_t) p] = -1.0;
    }
    releaseSentNotes (false); // the sequence's notes end at MIDI OUT too (keys still held play on)
    if (pendingPattern >= 0)
        loadPatternNow (pendingPattern);
    chainPos = chainFrom >= 0 ? std::min (chainFrom, chainTo) : -1;
}

// ---------------------------------------------------------------- the dial

void Machine::turnDial (int clicks)
{
    if (clicks == 0)
        return;
    flashUntil = 0.0;
    const bool shift = held[(size_t) Key::Shift];
    // SOLO / TRANSPOSE held + dial
    if (held[(size_t) Key::Solo])
    {
        soloUsed = true;
        transpose = std::clamp (transpose + clicks, -24, 24);
        return;
    }
    // PATTERN SET held + dial (stopped): assign a pattern to the last key pressed (p.63)
    if (held[(size_t) Key::PatternSet] && lastPatternSetKey >= 0 && op == Op::None)
    {
        auto& slot = mem.patternSet[(size_t) (patternSetGroup * 16 + lastPatternSetKey)];
        slot = (int16_t) std::clamp (slot + clicks, 0, kNumPatterns - 1);
        return;
    }
    if (op != Op::None && op != Op::Compare)
        return dialOp (clicks * (shift ? 1 : 1));
    dialPage (clicks);
}

void Machine::dialOp (int clicks)
{
    const bool shift = held[(size_t) Key::Shift];
    const auto& pat = playingConst();
    auto cycle = [] (int v, int d, int n) { return ((v + d) % n + n) % n; };
    switch (op)
    {
        case Op::Write: opValue = std::clamp (opValue + clicks, 0, (mode == Mode::Song ? kNumSongs : kNumPatterns) - 1); break;
        case Op::Rename: case Op::CardName:
        {
            juce::String s = opName.paddedRight (' ', 8);
            int c = (int) s[opCursor] + clicks;
            if (c < 32) c = 126;
            if (c > 126) c = 32;
            s = s.substring (0, opCursor) + juce::String::charToString ((juce::juce_wchar) c) + s.substring (opCursor + 1);
            opName = s;
            break;
        }
        case Op::LastStep: opValue = std::clamp (opValue + clicks, 1, 16); break;
        case Op::MoveSelect: opValue = cycle (opValue, clicks, 3); break;
        case Op::Move:
        {
            const int n = pat.numSteps();
            const int unit = shift ? pat.lastStep : 1; // SHIFT: a whole bar at a time
            if (opValue2 == 0)
                opValue = std::clamp (opValue + clicks * unit, -(n - 1), n - 1);
            else if (opValue2 == 1)
                opValue = std::clamp (opValue + clicks * unit, 1, std::max (1, n - 1));
            break;
        }
        case Op::ShiftSelect: opValue = cycle (opValue, clicks, 2); break;
        case Op::Shift: if (opValue2 == 0) opValue = std::clamp (opValue + clicks, -24, 24); break;
        case Op::CopyPart: case Op::CopySound: opValue = std::clamp (opValue + clicks, 0, kNumPatterns - 1); break;
        case Op::InsertPattern: case Op::DeletePattern: opValue = std::clamp (opValue + clicks, 0, std::max (0, songEdit.numPositions() - (op == Op::DeletePattern ? 1 : 0))); break;
        case Op::MidiUtil: opValue = cycle (opValue, clicks, 4); break;
        case Op::MidiFilter:
        {
            auto& g = mem.global;
            int bits = (g.filterProgram ? 8 : 0) | (g.filterControl ? 4 : 0) | (g.filterExclusive ? 2 : 0) | (g.filterNotes ? 1 : 0);
            bits = cycle (bits, clicks, 16);
            g.filterProgram = bits & 8;
            g.filterControl = bits & 4;
            g.filterExclusive = bits & 2;
            g.filterNotes = bits & 1;
            break;
        }
        case Op::DrumNote:
            if (opSourcePart >= 0)
                mem.global.drumNotes[(size_t) opSourcePart] = (uint8_t) std::clamp (mem.global.drumNotes[(size_t) opSourcePart] + clicks, 0, 127);
            break;
        case Op::CcSelect: opValue = cycle (opValue, clicks, kNumKnobs - (int) Knob::FxType); break;
        case Op::CcNumber:
        {
            auto& cc = mem.global.ccAssign[(size_t) (opValue + (int) Knob::FxType)];
            cc = (int8_t) std::clamp (cc + clicks, -1, 127);
            break;
        }
        case Op::CcContinue: opValue2 = clicks > 0 ? 1 : 0; break;
        case Op::Dump: opValue = cycle (opValue, clicks, 6); break;
        case Op::CardMenu: opValue = cycle (opValue, clicks, 4); break;
        case Op::CardLoadWhat: opValue = cycle (opValue, clicks, 2); break;
        case Op::CardFile: if (! cardFiles.isEmpty()) opValue = cycle (opValue, clicks, cardFiles.size()); break;
        case Op::CardPattern: opValue = std::clamp (opValue + clicks, 0, kNumPatterns - 1); break;
        case Op::CardSure: case Op::PatternSetWrite: case Op::GlobalWrite: opValue = clicks > 0 ? 1 : 0; break;
        default: break;
    }
}

void Machine::dialPage (int clicks)
{
    const bool shift = held[(size_t) Key::Shift];
    auto& pat = playing();
    auto& pt = pat.parts[(size_t) part];
    const int pg = page[(size_t) mode];
    shownKnob = -1;
    switch (mode)
    {
        case Mode::Pattern:
            switch (pg)
            {
                case 0:
                    if (op == Op::Compare)
                        break;
                    selectPattern (std::clamp ((pendingPattern >= 0 ? pendingPattern : patternIndex) + clicks, 0, kNumPatterns - 1));
                    break;
                case 1:
                {
                    // SHIFT: tenths of a BPM (p.20)
                    const float t = (float) std::clamp (std::round ((getTempo() + clicks * (shift ? 0.1 : 1.0)) * 10.0) / 10.0, 20.0, 300.0);
                    if (tempoLock)
                        lockedTempo = t;
                    else
                        pat.tempo = lockedTempo = t;
                    break;
                }
                case 2: pat.swing = (uint8_t) std::clamp (pat.swing + clicks, 50, 75); break;
                case 3:
                {
                    const int old = pat.length;
                    pat.length = (uint8_t) std::clamp (pat.length + clicks, 1, kMaxLength);
                    edit::fillOnLengthen (pat, old, pat.length);
                    barShown = std::min (barShown, pat.length - 1);
                    break;
                }
                case 4:
                    pat.beat = (Beat) std::clamp ((int) pat.beat + clicks, 0, 3);
                    pat.lastStep = pat.hasTriplets() ? 12 : 16; // changing the beat resets LAST STEP (p.53)
                    break;
                case 5: pat.rollType = (uint8_t) std::clamp (pat.rollType + clicks, 2, 4); break;
                default: pat.arpScale = (uint8_t) std::clamp (pat.arpScale + clicks, 0, kNumArpScales - 1); break;
            }
            break;
        case Mode::PartEdit:
            switch (pg)
            {
                case 0:
                    if (isDrum (part))
                        pt.sound.wave = (uint8_t) std::clamp (pt.sound.wave + clicks * (shift ? 10 : 1), 0, kNumDrumWaves - 1);
                    else if (isSynth (part))
                        pt.sound.wave = (uint8_t) std::clamp (pt.sound.wave + clicks, 0, mapping::waveCount (pt.sound.oscType) - 1);
                    break;
                case 1: pt.accentSw = clicks > 0; break;
                case 2: pt.swingSw = clicks > 0; break;
                case 3: pt.outBus34 = clicks < 0; break; // L/R lit, 3/4 dark
                default:
                    if (isSynth (part))
                        pt.sound.synthTune = (int8_t) std::clamp (pt.sound.synthTune + clicks, -50, 50);
                    break;
            }
            break;
        case Mode::StepEdit:
            switch (pg)
            {
                case 0: stepRecTarget = std::clamp (stepRecTarget + clicks * (shift ? pat.lastStep : 1), 0, pat.numSteps() - 1); break;
                case 1:
                    if (isSynth (part))
                    {
                        auto& st = pt.steps[(size_t) noteEditStep];
                        st.note = (uint8_t) std::clamp (st.note + clicks, 0, 127);
                    }
                    break;
                case 2:
                    if (isSynth (part))
                    {
                        auto& st = pt.steps[(size_t) noteEditStep];
                        st.gate = (uint16_t) std::clamp (st.gate + clicks * (shift ? 4 : 1), 1, 512);
                    }
                    break;
                case 3:
                {
                    // the parameters this part has motion for; SHIFT shows all of them (p.60)
                    const int count = kNumParams - 2;
                    int d = motionDest;
                    for (int tries = 0; tries < count; ++tries)
                    {
                        d = ((d + (clicks > 0 ? 1 : -1)) % count + count) % count;
                        if (shift || pat.findMotion (part, (Param) d) >= 0)
                            break;
                    }
                    motionDest = d;
                    break;
                }
                default:
                {
                    int slot = pat.findMotion (part, (Param) motionDest);
                    if (slot < 0)
                    {
                        slot = edit::freeMotionSlot (pat);
                        if (slot < 0)
                            break;
                        auto& m = pat.motion[(size_t) slot];
                        m.target = (int8_t) part;
                        m.param = (Param) motionDest;
                        m.values.fill (-1);
                    }
                    auto& v = pat.motion[(size_t) slot].values[(size_t) noteEditStep];
                    const int maxV = isSwitchParam ((Param) motionDest) ? 5 : 127;
                    v = (int16_t) std::clamp (v + clicks, -1, maxV); // -1 = "Disabl"
                    break;
                }
            }
            break;
        case Mode::Song:
            switch (pg)
            {
                case 0:
                    if (transport == Transport::Stopped)
                    {
                        songIndex = std::clamp (songIndex + clicks, 0, kNumSongs - 1);
                        songEdit = mem.songs[(size_t) songIndex];
                        songPosition = 0;
                    }
                    break;
                case 1: songEdit.tempo = (float) std::clamp (std::round ((songEdit.tempo + clicks * (shift ? 0.1 : 1.0)) * 10.0) / 10.0, 20.0, 300.0); break;
                case 2: songPosition = std::clamp (songPosition + clicks, 0, std::min (kMaxPositions - 1, songEdit.numPositions())); break;
                case 3:
                {
                    auto& ps = songEdit.positions[(size_t) std::clamp (songPosition, 0, kMaxPositions - 1)];
                    const int v = std::clamp ((int) ps.pattern + clicks, -1, kNumPatterns - 1);
                    if (ps.pattern < 0 && v >= 0)
                        ps = { (int16_t) v, 0 };
                    else
                        ps.pattern = (int16_t) v;
                    break;
                }
                case 4:
                {
                    auto& ps = songEdit.positions[(size_t) std::clamp (songPosition, 0, kMaxPositions - 1)];
                    ps.noteOffset = (int8_t) std::clamp (ps.noteOffset + clicks, -24, 24);
                    break;
                }
                case 5: songEdit.muteHold = clicks > 0; break;
                default: songEdit.nextSong = (int8_t) std::clamp (songEdit.nextSong + clicks, -1, kNumSongs - 1); break;
            }
            break;
        case Mode::Global:
            switch (pg)
            {
                case 0: metronome = (Metronome) std::clamp ((int) metronome + clicks, 0, 5); break;
                case 1: mem.global.masterTune = (int8_t) std::clamp (mem.global.masterTune + clicks, -50, 50); break;
                case 2: mem.global.protect = clicks > 0; break;
                default: mem.global.arpReverse = clicks > 0; break;
            }
            break;
        case Mode::Midi:
            switch (pg)
            {
                case 0:
                {
                    auto& ch = mem.global.midiChannel[(size_t) (isSynth (part) ? part - kFirstSynth : 5)];
                    ch = (uint8_t) std::clamp (ch + clicks, 0, 15);
                    break;
                }
                case 1: mem.global.clock = (ClockMode) std::clamp ((int) mem.global.clock + clicks, 0, 2); break;
                default: mem.global.bendRange = (int8_t) std::clamp (mem.global.bendRange + clicks, -12, 12); break;
            }
            break;
    }
}

// ---------------------------------------------------------------- the card (files)

juce::StringArray Machine::listCardFiles() const
{
    juce::StringArray names;
    if (cardFolder.isDirectory())
        for (auto& f : cardFolder.findChildFiles (juce::File::findFiles, false, "*.vbx"))
            names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

Machine::CardRequest Machine::takeCardRequest()
{
    auto r = pendingCard;
    pendingCard = CardRequest();
    return r;
}

void Machine::runCardRequest (const CardRequest& r)
{
    switch (r.kind)
    {
        case CardRequest::SaveAll:
        {
            cardFolder.createDirectory();
            const bool ok = r.file.replaceWithText (juce::JSON::toString (memoryToJson (mem), true));
            cardFinished (r, ok, nullptr, nullptr);
            break;
        }
        case CardRequest::LoadAll: case CardRequest::LoadPattern:
        {
            Memory m;
            const bool ok = r.file.existsAsFile() && memoryFromJson (juce::JSON::parse (r.file), m);
            cardFinished (r, ok, ok ? &m : nullptr, ok ? &m.patterns[(size_t) std::clamp (r.pattern, 0, kNumPatterns - 1)] : nullptr);
            break;
        }
        case CardRequest::Delete: cardFinished (r, r.file.deleteFile(), nullptr, nullptr); break;
        case CardRequest::Format:
        {
            bool ok = true;
            for (auto& f : cardFolder.findChildFiles (juce::File::findFiles, false, "*.vbx"))
                ok = f.deleteFile() && ok;
            cardFinished (r, ok, nullptr, nullptr);
            break;
        }
        case CardRequest::DumpTx: case CardRequest::None: break;
    }
}

void Machine::cardFinished (const CardRequest& r, bool ok, const Memory* loaded, const Pattern* loadedPattern)
{
    if (! ok)
    {
        flash ("Card", "Error");
        return;
    }
    if (r.kind == CardRequest::LoadAll && loaded != nullptr)
    {
        mem = *loaded;
        mode = Mode::Pattern; // loading ends in Pattern mode with A01 (p.78)
        loadPatternNow (0);
        memoryChanged = true;
    }
    if (r.kind == CardRequest::LoadPattern && loadedPattern != nullptr)
    {
        edit = *loadedPattern; // into the edit buffer, not written (p.78)
        mode = Mode::Pattern;
    }
    flash ("Complete");
}

bool Machine::wantsAudioInput() const
{
    if (audioThru || bpmScanning)
        return true;
    for (int p = kFirstSynth; p < kFirstSynth + kNumSynthParts; ++p)
        if (playingConst().parts[(size_t) p].sound.oscType == OscType::AudioInComb)
            return true;
    return false;
}
} // namespace valvebox
