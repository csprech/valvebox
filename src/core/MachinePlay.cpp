// The Machine's audio side: the step clock (swing, roll, accent, gate / legato), pattern changes at
// the end of the pattern, pattern-set chains, song playback and event recording, motion sequences,
// realtime / step recording, the arpeggiator, the metronome and count-in, auto BPM, MIDI in / out.

#include "Machine.h"
#include "Mapping.h"
#include <algorithm>
#include <cmath>

namespace valvebox
{
namespace
{
    constexpr int kChunk = 32; // events land at most this many samples early (0.7 ms at 48 kHz)

    void writeParam (Sound& s, int part, Param p, int v)
    {
        const auto u8 = [] (int x) { return (uint8_t) std::clamp (x, 0, 127); };
        switch (p)
        {
            case Param::PitchGlide: (isDrum (part) ? s.pitch : s.glide) = u8 (v); break;
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
            case Param::ModDest: s.modDest = (ModDest) std::clamp (v, 0, 5); break;
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

    /** Drum arpeggio rates by ribbon position, in quarter notes (1/4 .. 1/64). */
    double drumArpQuarters (float pos)
    {
        static const double q[8] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625 };
        return q[std::clamp ((int) (pos * 8.0f), 0, 7)];
    }
} // namespace

double Machine::stepSamples() const
{
    const double bpm = (mem.global.clock != ClockMode::Internal && extRunning) ? extBpm : getTempo();
    return playingConst().stepQuarters() * 60.0 / std::max (1.0, bpm) * sampleRate;
}

bool Machine::partAudible (int p) const
{
    const auto& pt = playingConst().parts[(size_t) p];
    if (solo.any())
        return solo.test ((size_t) p);
    return ! pt.muted;
}

int Machine::velocityFor (int p, int localStep) const
{
    const auto& pat = playingConst();
    const int acc = isDrum (p) ? kDrumAccent : kSynthAccent;
    const int level = effective[(size_t) acc].level;
    if (! pat.parts[(size_t) p].accentSw)
        return mapping::unaccentedVelocity (level);
    const bool accented = pat.parts[(size_t) acc].steps[(size_t) std::clamp (localStep, 0, kMaxSteps - 1)].on;
    return accented ? mapping::accentedVelocity (level) : mapping::unaccentedVelocity (level);
}

uint32_t Machine::songTickNow() const
{
    // ticks at 96 a quarter, counted in 16ths of the patterns played so far plus this one
    const double sixteenths = songStepBase + pos * playingConst().stepQuarters() / 0.25;
    return (uint32_t) std::max (0.0, sixteenths * (kSongPPQ / 4.0));
}

void Machine::sendNote (int p, int note, int velocity, int sampleOffset, bool legato)
{
    if (! mem.global.filterNotes)
        return;
    const int ch = 1 + mem.global.midiChannel[(size_t) (isSynth (p) ? p - kFirstSynth : 5)];
    if (! isSynth (p))
    {
        if (velocity > 0) // drums: note-on only, like a drum machine's triggers
            midiOutBuffer.addEvent (juce::MidiMessage::noteOn (ch, mem.global.drumNotes[(size_t) p], (juce::uint8) std::clamp (velocity, 1, 127)),
                                    sampleOffset);
        return;
    }
    auto& sent = noteSent[(size_t) p];
    const int n = std::clamp (note, 0, 127);
    if (velocity <= 0)
    {
        if (sent >= 0)
            midiOutBuffer.addEvent (juce::MidiMessage::noteOff (ch, sent), sampleOffset);
        sent = -1;
        return;
    }
    if (legato && sent == n)
        return; // tied into the same note: it just carries on
    const int old = sent;
    if (old >= 0 && ! legato)
        midiOutBuffer.addEvent (juce::MidiMessage::noteOff (ch, old), sampleOffset);
    midiOutBuffer.addEvent (juce::MidiMessage::noteOn (ch, n, (juce::uint8) std::clamp (velocity, 1, 127)), sampleOffset);
    if (old >= 0 && legato)
        midiOutBuffer.addEvent (juce::MidiMessage::noteOff (ch, old), sampleOffset); // overlapping: a legato slide on the receiver
    sent = n;
}

void Machine::releaseSentNotes (bool evenHeld)
{
    for (int p = kFirstSynth; p < kFirstSynth + kNumSynthParts; ++p)
        if (noteSent[(size_t) p] >= 0 && (evenHeld || heldNote[(size_t) p] < 0))
            sendNote (p, 0, 0, sampleInBlock);
}

void Machine::triggerPart (int p, int velocity, int note, bool fromSequence, bool legato)
{
    if (isAccent (p) || p < 0)
        return;
    const auto& s = effective[(size_t) p];
    rack.trigger (p, (float) mapping::velocityGain (velocity), note, s, legato);
    if (logTriggers && triggerLog.size() < triggerLog.capacity())
        triggerLog.push_back ({ p, note, velocity, transport == Transport::Playing ? pos : -1.0, legato, patternIndex });
    sendNote (p, note, velocity, sampleInBlock, legato);
    juce::ignoreUnused (fromSequence);
}

void Machine::noteOffPart (int p)
{
    if (! isSynth (p))
        return;
    rack.noteOff (p);
    sendNote (p, 0, 0, sampleInBlock); // ends the note it sent, whatever the pitch is now
}

// ---------------------------------------------------------------- recording

void Machine::recordLiveTrigger (int p, int note)
{
    auto& pat = playing();
    const int n = pat.numSteps();
    const int k = ((int) std::lround (pos)) % n;
    auto& st = pat.parts[(size_t) p].steps[(size_t) k];
    st.on = true;
    if (isSynth (p))
    {
        st.note = (uint8_t) std::clamp (note - transpose, 0, 127);
        st.gate = 3;
    }
    if (k != (int) pos)
        skipOnce[(size_t) p] = k; // it rounded up to a step still to come: don't play it twice
    recStep[(size_t) p] = k;
    recPressPos[(size_t) p] = pos;
}

void Machine::recordLiveRelease (int p)
{
    auto& pat = playing();
    const int k = recStep[(size_t) p];
    if (k < 0)
        return;
    double heldSteps = pos - recPressPos[(size_t) p];
    if (heldSteps < 0)
        heldSteps += pat.numSteps();
    pat.parts[(size_t) p].steps[(size_t) k].gate = (uint16_t) std::clamp ((int) std::lround (heldSteps * 4.0), 1, 512);
    recStep[(size_t) p] = -1;
}

void Machine::recordEvent (SongEvent::Type t, int p, int param, int value)
{
    if (! (mode == Mode::Song && eventRecord && transport == Transport::Playing))
        return;
    auto& ev = songEdit.events;
    if ((int) ev.size() >= kMaxEvents || ev.size() >= ev.capacity())
    {
        flash ("Event", "Full");
        return;
    }
    const SongEvent e { songTickNow(), t, (uint8_t) p, (uint8_t) param, (int16_t) value };
    auto it = std::upper_bound (ev.begin(), ev.end(), e, [] (const SongEvent& a, const SongEvent& b) { return a.tick < b.tick; });
    ev.insert (it, e); // (capacity reserved: no allocation)
}

void Machine::recordMotionStep (int k)
{
    bool any = false, finished = false;
    for (auto& r : motionRec)
    {
        if (r.slot < 0)
            continue;
        playing().motion[(size_t) r.slot].values[(size_t) k] = (int16_t) r.value;
        if (--r.stepsLeft <= 0)
        {
            r = MotionRec(); // one pass of the pattern recorded
            finished = true;
        }
        else
            any = true;
    }
    if (finished && ! any)
        recording = false; // REC goes dark by itself after one cycle (p.58)
}

// ---------------------------------------------------------------- motion playback

void Machine::applyMotion (double localPos)
{
    const auto& pat = playingConst();
    for (int p = 0; p < kNumParts; ++p)
        effective[(size_t) p] = pat.parts[(size_t) p].sound;
    effectiveFx = pat.fx;
    if (transport != Transport::Playing)
        return;
    const int n = pat.numSteps();
    const int k = std::clamp ((int) localPos, 0, n - 1);
    const double frac = localPos - std::floor (localPos);
    for (int i = 0; i < kMaxMotion; ++i)
    {
        const auto& m = pat.motion[(size_t) i];
        if (! m.used())
            continue;
        bool beingRecorded = false;
        for (auto& r : motionRec)
            beingRecorded = beingRecorded || r.slot == i;
        if (beingRecorded)
            continue; // the knob plays live while it records
        auto smoothValue = [&] () -> int {
            const int v0 = m.values[(size_t) k];
            if (v0 < 0)
                return -1;
            int v1 = m.values[(size_t) ((k + 1) % n)];
            if (v1 < 0)
                v1 = v0;
            return (int) std::lround (v0 + (v1 - v0) * frac);
        };
        if (m.target >= kFxTarget)
        {
            const int f = m.target - kFxTarget;
            if (! pat.fx[(size_t) f].motion)
                continue;
            const int v = smoothValue(); // effect motion is always smooth (p.59)
            if (v >= 0)
                (m.param == Param::FxEdit1 ? effectiveFx[(size_t) f].edit1 : effectiveFx[(size_t) f].edit2) = (uint8_t) v;
            continue;
        }
        const auto& s = pat.parts[(size_t) m.target].sound;
        if (s.motion == MotionMode::Off)
            continue;
        const bool hold = s.motion == MotionMode::TrigHold || isAccent (m.target) || isSwitchParam (m.param);
        const int v = hold ? trigHoldValue[(size_t) i] : smoothValue();
        if (v >= 0)
            writeParam (effective[(size_t) m.target], m.target, m.param, v);
    }
}

// ---------------------------------------------------------------- the step clock

void Machine::playStep (int k, double when)
{
    juce::ignoreUnused (when);
    auto& pat = playing();
    const int n = pat.numSteps();
    if (k < 0 || k >= n)
        return;
    lastStepPlayed = k;
    // motion: one value a step while recording; trig-hold values are picked up by the triggers
    recordMotionStep (k);
}

void Machine::stepEvents (double from, double to)
{
    auto& pat = playing();
    const int n = pat.numSteps();
    if (n <= 0)
        return;
    if (to >= n)
    {
        // the pattern ends inside this chunk: finish it, change pattern if one is waiting, carry on
        stepEvents (from, (double) n - 1.0e-9);
        patternEnded();
        if (transport != Transport::Playing)
            return;
        const double rest = to - n;
        pos = 0.0;
        stepEvents (0.0, rest);
        pos = rest;
        wrapped = true;
        return;
    }
    const double swingOffset = pat.hasTriplets() ? 0.0 : (pat.swing - 50) / 50.0; // even steps late, up to half a step
    // ---- note-offs: synth gates ending
    for (int p = kFirstSynth; p < kFirstSynth + kNumSynthParts; ++p)
        if (gateOn[(size_t) p] && offAt[(size_t) p] >= from && offAt[(size_t) p] < to)
        {
            gateOn[(size_t) p] = false;
            if (heldNote[(size_t) p] < 0)
                noteOffPart (p);
        }
    // ---- steps and their triggers (swing moves the even steps; roll adds hits inside a step)
    const int k0 = std::max (0, (int) std::floor (from) - 1), k1 = std::min (n - 1, (int) std::floor (to));
    for (int k = k0; k <= k1; ++k)
    {
        if (k >= from && k < to)
            playStep (k, k);
        for (int p = 0; p < kFirstSynth + kNumSynthParts; ++p)
        {
            const auto& partData = pat.parts[(size_t) p];
            const auto& st = partData.steps[(size_t) k];
            if (! st.on)
                continue;
            const bool swung = partData.swingSw && (k % 2 == 1);
            const double base = k + (swung ? swingOffset : 0.0);
            const bool roll = effective[(size_t) p].roll;
            const int hits = roll ? pat.rollType : 1;
            for (int h = 0; h < hits; ++h)
            {
                const double t = base + (swung ? 1.0 - swingOffset : 1.0) * h / hits;
                if (! (t >= from && t < to))
                    continue;
                if (h == 0)
                {
                    if (erasing && p == part)
                    {
                        edit::eraseTrigger (pat, p, k); // SHIFT + RESET/ERASE held (p.48)
                        break;
                    }
                    if (skipOnce[(size_t) p] == k)
                    {
                        skipOnce[(size_t) p] = -1; // just recorded and already heard
                        continue;
                    }
                    // trig-hold motion values are taken at the note (p.59)
                    for (int i = 0; i < kMaxMotion; ++i)
                        if (pat.motion[(size_t) i].target == p)
                            trigHoldValue[(size_t) i] = pat.motion[(size_t) i].values[(size_t) k];
                    for (int i = 0; i < kMaxMotion; ++i)
                    {
                        const int acc = isDrum (p) ? kDrumAccent : kSynthAccent;
                        if (pat.motion[(size_t) i].target == acc)
                            trigHoldValue[(size_t) i] = pat.motion[(size_t) i].values[(size_t) k];
                    }
                    applyMotion (pos);
                }
                // 6A / 6B and 7A / 7B: if both trigger together only B sounds (p.48)
                if ((p == 5 && pat.parts[6].steps[(size_t) k].on) || (p == 7 && pat.parts[8].steps[(size_t) k].on))
                    continue;
                const int vel = velocityFor (p, k);
                if (isDrum (p))
                    triggerPart (p, vel, 60, true);
                else
                {
                    int offset = transport == Transport::Playing && songActive ? songEdit.positions[(size_t) std::clamp (songPosition, 0, kMaxPositions - 1)].noteOffset : 0;
                    const int note = std::clamp (st.note + transpose + offset, 0, 127);
                    // a gate still open from the step before: legato, no retrigger (p.51), unless it's a roll hit
                    const bool legato = h == 0 && gateOn[(size_t) p] && offAt[(size_t) p] >= t - 1.0e-6;
                    if (heldNote[(size_t) p] >= 0 && keyboard && part == p)
                        continue; // the player is holding a note on this part: the keyboard wins
                    triggerPart (p, vel, note, true, legato && ! roll);
                    gateOn[(size_t) p] = true;
                    offAt[(size_t) p] = t + (roll ? 1.0 / hits * 0.75 : st.gate / 4.0);
                    if (offAt[(size_t) p] >= n)
                        offAt[(size_t) p] -= n; // (rare: a gate past the end wraps)
                }
            }
        }
    }
}

void Machine::patternEnded()
{
    // song mode: the next position (or the next song)
    if (mode == Mode::Song && songActive)
    {
        songAdvance();
        return;
    }
    // pattern set chain play: the next pattern in the range, looping (p.62)
    if (chainFrom >= 0)
    {
        const int lo = std::min (chainFrom, chainTo), hi = std::max (chainFrom, chainTo);
        chainPos = chainPos < lo || chainPos >= hi ? lo : chainPos + 1;
        loadPatternNow (mem.patternSet[(size_t) (patternSetGroup * 16 + chainPos)]);
        return;
    }
    if (pendingPattern >= 0)
    {
        const auto mutes = edit.parts;
        loadPatternNow (pendingPattern);
        juce::ignoreUnused (mutes);
    }
}

void Machine::loadSongPosition (int p)
{
    const auto& ps = songEdit.positions[(size_t) std::clamp (p, 0, kMaxPositions - 1)];
    if (ps.pattern < 0)
        return;
    std::array<bool, kNumParts> mutes {};
    for (int i = 0; i < kNumParts; ++i)
        mutes[(size_t) i] = songPat.parts[(size_t) i].muted;
    songPat = mem.patterns[(size_t) ps.pattern];
    if (songEdit.muteHold && p > 0)
        for (int i = 0; i < kNumParts; ++i)
            songPat.parts[(size_t) i].muted = mutes[(size_t) i]; // MUTE HOLD (p.71)
    trigHoldValue.fill (-1);
}

void Machine::songAdvance()
{
    const auto& pat = songPat;
    songStepBase += pat.numSteps() * pat.stepQuarters() / 0.25;
    ++songPosition;
    if (songPosition >= songEdit.numPositions())
    {
        if (songEdit.nextSong >= 0 && mem.global.clock == ClockMode::Internal)
        {
            // NEXT SONG: carry straight on (p.71)
            songIndex = songEdit.nextSong;
            songEdit = mem.songs[(size_t) songIndex];
            songPosition = 0;
            songStepBase = 0.0;
            songEventIndex = 0;
            if (songEdit.numPositions() > 0)
            {
                loadSongPosition (0);
                return;
            }
        }
        songPosition = 0;
        stopPlayback(); // the song ends: playback stops
        return;
    }
    loadSongPosition (songPosition);
}

void Machine::playSongEvents (uint32_t fromTick, uint32_t toTick)
{
    auto& ev = songEdit.events;
    for (size_t i = 0; i < ev.size(); ++i)
    {
        const auto& e = ev[i];
        if (e.tick < fromTick)
            continue;
        if (e.tick >= toTick)
            break;
        switch (e.type)
        {
            case SongEvent::DrumNote: triggerPart (e.part, std::clamp ((int) e.value, 1, 127), 60, false); break;
            case SongEvent::KeyNoteOn: case SongEvent::ArpNote: triggerPart (e.part, 100, e.param, false); break;
            case SongEvent::KeyNoteOff: noteOffPart (e.part); break;
            case SongEvent::Knob: case SongEvent::KeyParam:
                if (e.part >= kFxTarget && e.param == 100)
                    songPat.fx[(size_t) (e.part - kFxTarget)].type = (FxType) std::clamp ((int) e.value, 0, kNumFxTypes - 1);
                else if (e.part >= kFxTarget && e.param == 101)
                    songPat.chain = (Chain) std::clamp ((int) e.value, 0, 3);
                else if (e.part >= kFxTarget)
                    (e.param == (int) Param::FxEdit1 ? songPat.fx[(size_t) (e.part - kFxTarget)].edit1 : songPat.fx[(size_t) (e.part - kFxTarget)].edit2)
                        = (uint8_t) std::clamp ((int) e.value, 0, 127);
                else
                    writeParam (songPat.parts[(size_t) e.part].sound, e.part, (Param) e.param, e.value);
                break;
            case SongEvent::Mute: songPat.parts[(size_t) std::clamp ((int) e.part, 0, kNumParts - 1)].muted = e.value != 0; break;
            case SongEvent::Tempo: songEdit.tempo = std::clamp (e.value / 10.0f, 20.0f, 300.0f); break;
        }
    }
}

// ---------------------------------------------------------------- the arpeggiator

void Machine::ribbon (bool touching, float position)
{
    position = std::clamp (position, 0.0f, 1.0f);
    const bool wasDown = ribbonDown;
    ribbonDown = touching;
    ribbonPos = position;
    if (touching && ! wasDown)
    {
        // a new touch starts the arpeggio again, unless SHIFT is held (p.27)
        if (! held[(size_t) Key::Shift])
            arpIndex = 0;
        arpNext = -1.0; // play at once
    }
    if (! touching && wasDown && isSynth (part) && arpPlayingNote >= 0)
    {
        noteOffPart (part);
        arpPlayingNote = -1;
    }
}

void Machine::slider (float position) { sliderPos = std::clamp (position, 0.0f, 1.0f); }

void Machine::runArp (double fromQ, double toQ)
{
    if (! ribbonDown || isAccent (part))
        return;
    const bool reverse = mem.global.arpReverse;
    const float timePos = reverse ? sliderPos : ribbonPos;  // gate time / drum rate
    const float pitchPos = reverse ? ribbonPos : sliderPos; // pitch in the scale
    const double interval = isDrum (part) ? drumArpQuarters (timePos) : 0.25; // synths: 16ths
    if (arpOff >= 0.0 && arpOff >= fromQ && arpOff < toQ && arpPlayingNote >= 0)
    {
        noteOffPart (part);
        arpPlayingNote = -1;
        arpOff = -1.0;
    }
    if (arpNext < 0.0)
        arpNext = transport == Transport::Playing ? std::ceil (fromQ / interval - 1.0e-9) * interval : fromQ; // on the beat grid while playing
    while (arpNext >= fromQ && arpNext < toQ)
    {
        const double at = arpNext;
        arpNext += interval;
        if (isDrum (part))
        {
            triggerPart (part, mapping::accentedVelocity (effective[(size_t) kDrumAccent].level), 60, false);
            if (recording && transport == Transport::Playing)
                recordLiveTrigger (part, 60);
            continue;
        }
        const auto& pat = playingConst();
        const int base = arpCount > 0 ? arpNotes[(size_t) (arpIndex % arpCount)] : (int) pat.arpCentre;
        ++arpIndex;
        const int count = mapping::scales()[(size_t) pat.arpScale].count;
        const int degrees = (int) std::lround ((pitchPos - 0.5f) * 2.0f * count); // the slider covers two octaves
        const int note = std::clamp (mapping::scaleNote (base, pat.arpScale, degrees) + transpose, 0, 127);
        const double gate = 0.1 + 0.9 * timePos;
        const bool legato = gate > 0.95 && arpPlayingNote >= 0;
        triggerPart (part, mapping::accentedVelocity (effective[(size_t) kSynthAccent].level), note, false, legato);
        arpPlayingNote = note;
        arpOff = gate > 0.95 ? -1.0 : at + interval * gate;
        if (recording && transport == Transport::Playing)
            recordLiveTrigger (part, note);
        if (mode == Mode::Song && eventRecord)
            recordEvent (SongEvent::ArpNote, part, note, 100);
    }
}

// ---------------------------------------------------------------- metronome

void Machine::renderClick (float* buf, int n)
{
    std::fill (buf, buf + n, 0.0f);
    bool on = false;
    switch (metronome)
    {
        case Metronome::Off: break;
        case Metronome::Rec0: case Metronome::Rec1: case Metronome::Rec2: on = recording || countIn > 0; break;
        case Metronome::On: case Metronome::OnIndividual: on = transport == Transport::Playing; break;
    }
    if (! on)
        return;
    for (int i = 0; i < n; ++i)
    {
        if (clickEnv > 1.0e-4)
        {
            clickPhase += clickHz / sampleRate;
            buf[i] = (float) (std::sin (6.283185307179586 * clickPhase) * clickEnv * 0.3);
            clickEnv *= std::exp (-1.0 / (0.012 * sampleRate));
        }
    }
}

// ---------------------------------------------------------------- auto BPM

void Machine::detectBpm (const float* in, int n)
{
    // onset strength at 100 Hz: the rise of a smoothed level
    const int decim = std::max (1, (int) (sampleRate / 100.0));
    for (int i = 0; i < n; ++i)
    {
        onsetAcc = std::max (onsetAcc * 0.9995f, std::abs (in[i]));
        if (++onsetDecim >= decim)
        {
            onsetDecim = 0;
            const float rise = std::max (0.0f, onsetAcc - onsetPrev);
            onsetPrev = onsetAcc;
            if (onsetWrite < (int) onsetEnv.size())
                onsetEnv[(size_t) onsetWrite++] = rise;
        }
    }
    if (onsetWrite < 600)
        return; // six seconds of listening
    float energy = 0;
    for (int i = 0; i < onsetWrite; ++i)
        energy += onsetEnv[(size_t) i];
    float best = 0;
    double bestBpm = 0;
    if (energy > 0.05f)
        for (double bpm = 70.0; bpm <= 180.0; bpm += 0.5)
        {
            const double lag = 6000.0 / bpm; // frames a beat
            float sum = 0;
            for (int i = (int) lag + 1; i < onsetWrite; ++i)
            {
                const double j = i - lag;
                const int j0 = (int) j;
                const float fr = (float) (j - j0);
                sum += onsetEnv[(size_t) i] * (onsetEnv[(size_t) j0] * (1 - fr) + onsetEnv[(size_t) j0 + 1] * fr);
            }
            if (sum > best)
            {
                best = sum;
                bestBpm = bpm;
            }
        }
    while (bestBpm > 0 && bestBpm < 80.0)
        bestBpm *= 2.0;
    while (bestBpm > 160.0)
        bestBpm *= 0.5;
    detectedBpm = (float) bestBpm; // 0 = couldn't tell ("---.-")
    bpmScanning = false;
    bpmFound = true; // the key blinks: press it to use the tempo (p.21)
}

// ---------------------------------------------------------------- MIDI in

void Machine::midiIn (const juce::MidiMessage& m)
{
    auto& g = mem.global;
    // ---- clock
    if (m.isMidiClock())
    {
        if (g.clock == ClockMode::Internal)
            return;
        const double now = clockSeconds + sampleInBlock / sampleRate;
        if (extClockLast > 0.0)
        {
            const double dt = now - extClockLast;
            if (dt > 1.0e-4 && dt < 0.5)
                extBpm = extBpm * 0.9 + (60.0 / (dt * 24.0)) * 0.1;
        }
        extClockLast = now;
        extRunning = true;
        ++extClockTicks;
        return;
    }
    if (m.isMidiStart() && g.clock != ClockMode::Internal)
    {
        extClockTicks = 0;
        startPlayback (true);
        return;
    }
    if (m.isMidiContinue() && g.clock != ClockMode::Internal)
    {
        transport = Transport::Playing;
        return;
    }
    if (m.isMidiStop() && g.clock != ClockMode::Internal)
    {
        stopPlayback();
        return;
    }
    if (m.isSongPositionPointer() && g.clock != ClockMode::Internal)
    {
        pos = std::fmod (m.getSongPositionPointerMidiBeat() * 0.25 / playingConst().stepQuarters(), (double) playingConst().numSteps());
        return;
    }
    if (m.getRawDataSize() >= 2 && m.getRawData()[0] == 0xF3 && g.filterProgram) // song select
    {
        songIndex = std::clamp (m.getRawData()[1], (juce::uint8) 0, (juce::uint8) (kNumSongs - 1));
        songEdit = mem.songs[(size_t) songIndex];
        return;
    }
    const int ch = m.getChannel() - 1;
    if (ch < 0)
        return;
    // which parts listen on this channel (two synth parts on one channel both play)
    std::array<int, kNumSynthParts> synthParts {};
    int numSynth = 0;
    for (int i = 0; i < kNumSynthParts; ++i)
        if (g.midiChannel[(size_t) i] == ch)
            synthParts[(size_t) numSynth++] = kFirstSynth + i;
    const int synthPart = numSynth > 0 ? synthParts[0] : -1;
    const bool drumChannel = g.midiChannel[5] == ch;
    const bool global = g.midiChannel[0] == ch;

    if ((m.isNoteOn() || m.isNoteOff()) && g.filterNotes)
    {
        if (drumChannel && m.isNoteOn())
            for (int p = 0; p < kNumDrumParts; ++p)
                if (g.drumNotes[(size_t) p] == m.getNoteNumber())
                {
                    triggerPart (p, m.getVelocity(), 60, false);
                    if (recording && transport == Transport::Playing)
                        recordLiveTrigger (p, 60);
                }
        for (int i = 0; i < numSynth; ++i)
        {
            const int sp = synthParts[(size_t) i];
            if (m.isNoteOn())
            {
                const bool legato = heldNote[(size_t) sp] >= 0;
                heldNote[(size_t) sp] = m.getNoteNumber();
                triggerPart (sp, m.getVelocity(), m.getNoteNumber(), false, legato);
                if (recording && transport == Transport::Playing)
                    recordLiveTrigger (sp, m.getNoteNumber());
            }
            else if (heldNote[(size_t) sp] == m.getNoteNumber())
            {
                if (recording && transport == Transport::Playing)
                    recordLiveRelease (sp);
                heldNote[(size_t) sp] = -1;
                noteOffPart (sp);
            }
        }
        return;
    }
    if (m.isPitchWheel())
    {
        for (int i = 0; i < numSynth; ++i)
            rack.setBend (synthParts[(size_t) i], (m.getPitchWheelValue() - 8192) / 8192.0 * g.bendRange);
        return;
    }
    if (m.isProgramChange() && g.filterProgram && global)
    {
        selectPattern (m.getProgramChangeNumber());
        return;
    }
    if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        rack.allNotesOff();
        releaseSentNotes (true);
        heldNote.fill (-1);
        return;
    }
    if (m.isController() && g.filterControl)
    {
        const int cc = m.getControllerNumber(), v = m.getControllerValue();
        // effects and the chain on the global channel (p.92)
        if (global)
        {
            for (int f = 0; f < kNumFx; ++f)
            {
                auto& fx = playing().fx[(size_t) f];
                if (cc == mapping::kFxTypeCc[f]) { fx.type = (FxType) mapping::ccToOption (v, kNumFxTypes); return; }
                if (cc == mapping::kFxEdit1Cc[f]) { setParam (kFxTarget + f, Param::FxEdit1, v, true); return; }
                if (cc == mapping::kFxEdit2Cc[f]) { setParam (kFxTarget + f, Param::FxEdit2, v, true); return; }
                if (cc == mapping::kFxMotionCc[f]) { fx.motion = v >= 64; return; }
            }
            if (cc == mapping::kFxChainCc)
            {
                playing().chain = (Chain) mapping::ccToOption (v, 4);
                return;
            }
        }
        if (synthPart < 0)
            return;
        // a synth part's knobs (assignable) and keys (p.92)
        for (int k = (int) Knob::PitchGlide; k < kNumKnobs; ++k)
            if (mem.global.ccAssign[(size_t) k] == cc)
            {
                const auto knob = (Knob) k;
                if (knob == Knob::OscType)
                {
                    auto& s = playing().parts[(size_t) synthPart].sound;
                    s.oscType = (OscType) mapping::ccToOption (v, kNumOscTypes);
                    s.wave = (uint8_t) std::min ((int) s.wave, mapping::waveCount (s.oscType) - 1);
                    return;
                }
                static const Param params[] = { Param::PitchGlide, Param::EgTime, Param::Pan, Param::Level, Param::ModSpeed, Param::ModDepth,
                                                Param::Count, Param::OscEdit1, Param::OscEdit2, Param::Cutoff, Param::EgInt, Param::Resonance, Param::Drive };
                const auto p = params[k - (int) Knob::PitchGlide];
                if (p != Param::Count)
                    setParam (synthPart, p, v, true);
                return;
            }
        for (auto& kc : mapping::kKeyCcs)
            if (kc.cc == cc)
            {
                setParam (synthPart, kc.param, mapping::ccToOption (v, kc.options), true);
                return;
            }
        if (cc == mapping::kPartMotionCc)
            playing().parts[(size_t) synthPart].sound.motion = (MotionMode) mapping::ccToOption (v, 3);
    }
}

// ---------------------------------------------------------------- control changes out (p.92)

void Machine::sendCc (int channel, int cc, int value)
{
    if (! mem.global.filterControl || cc < 0 || cc > 127)
        return;
    midiOutBuffer.addEvent (juce::MidiMessage::controllerEvent (1 + channel, cc, std::clamp (value, 0, 127)), sampleInBlock);
}

void Machine::sendKnobCc (Knob k, int v)
{
    const auto& g = mem.global;
    switch (k)
    {
        case Knob::MasterVolume: case Knob::TubeGain: case Knob::AudioInLevel: case Knob::Count: return; // not sent
        case Knob::FxType: return sendCc (g.midiChannel[0], mapping::kFxTypeCc[fxEdit], v);
        case Knob::FxEdit1: return sendCc (g.midiChannel[0], mapping::kFxEdit1Cc[fxEdit], v);
        case Knob::FxEdit2: return sendCc (g.midiChannel[0], mapping::kFxEdit2Cc[fxEdit], v);
        default: break;
    }
    // a synth part's knobs on its channel, with the MIDI UTILITY assignment (the drums used NRPN: not sent)
    if (isSynth (part))
        sendCc (g.midiChannel[(size_t) (part - kFirstSynth)], mem.global.ccAssign[(size_t) k], v);
}

void Machine::sendKeyCc (Key k)
{
    const auto& g = mem.global;
    const auto& pat = playingConst();
    if (k == Key::FxChain)
        return sendCc (g.midiChannel[0], mapping::kFxChainCc, mapping::optionToCc ((int) pat.chain, 4));
    if (k == Key::FxMotion)
        return sendCc (g.midiChannel[0], mapping::kFxMotionCc[fxEdit], pat.fx[(size_t) fxEdit].motion ? 127 : 0);
    if (! isSynth (part))
        return;
    const int ch = g.midiChannel[(size_t) (part - kFirstSynth)];
    const auto& s = pat.parts[(size_t) part].sound;
    if (k == Key::PartMotion)
        return sendCc (ch, mapping::kPartMotionCc, mapping::optionToCc ((int) s.motion, 3));
    auto send = [&] (Param p, int option) {
        for (auto& kc : mapping::kKeyCcs)
            if (kc.param == p)
                sendCc (ch, kc.cc, mapping::optionToCc (option, kc.options));
    };
    switch (k)
    {
        case Key::FilterType: return send (Param::FilterType, (int) s.filterType);
        case Key::AmpEg: return send (Param::AmpEg, s.ampEg == AmpEg::Decay ? 0 : 1);
        case Key::Roll: return send (Param::Roll, s.roll ? 1 : 0);
        case Key::FxSend: return send (Param::FxSend, s.fxSend ? 1 : 0);
        case Key::FxSelect: return send (Param::FxSelect, s.fxSelect);
        case Key::ModType: return send (Param::ModType, (int) s.modType);
        case Key::ModDest: return send (Param::ModDest, (int) s.modDest);
        case Key::ModBpmSync: return send (Param::ModBpmSync, s.modBpmSync ? 1 : 0);
        default: break;
    }
}

// ---------------------------------------------------------------- process

void Machine::process (float* const* out, int numOut, int numSamples, const float* audioIn)
{
    // (midiOutBuffer is cleared by the host once it has sent it: clearing it here lost what the panel sent between blocks)
    // ---- the audio input: level meter, CLIP, tempo detection
    if (audioIn != nullptr)
    {
        float pk = 0;
        for (int i = 0; i < numSamples; ++i)
            pk = std::max (pk, std::abs (audioIn[i]));
        inputPeak = std::max (pk, inputPeak * 0.9f);
        inputClip = pk > 0.98f;
        if (bpmScanning)
            detectBpm (audioIn, numSamples);
    }
    else
        inputPeak *= 0.9f;

    const bool external = mem.global.clock == ClockMode::External || (mem.global.clock == ClockMode::Auto && extRunning);
    // with an external clock, the steps come from the ticks received this block
    const double extSteps = external ? extClockTicks / 24.0 / playingConst().stepQuarters() : 0.0;
    extClockTicks = 0;
    if (clickBuf.size() < (size_t) numSamples)
        return; // (prepare() sizes it)

    for (int offset = 0; offset < numSamples;)
    {
        const int n = std::min (kChunk, numSamples - offset);
        sampleInBlock = offset;
        const double qPerSample = getTempo() / 60.0 / sampleRate;
        double from = pos;
        // ---- the clock
        if (transport == Transport::Playing && countIn > 0)
        {
            // count-in: clicks on the beats, no steps yet
            const double beatSamples = 60.0 / getTempo() * sampleRate;
            const int before = countInLength - countIn;
            countIn = std::max (0, countIn - n);
            const int after = countInLength - countIn;
            if (before == 0 || std::floor (after / beatSamples) != std::floor (before / beatSamples))
            {
                clickEnv = 1.0;
                clickHz = std::fmod (std::floor (before / beatSamples), 4.0) == 0.0 ? 1500.0 : 1000.0;
            }
        }
        else if (transport == Transport::Playing)
        {
            const double dSteps = external ? extSteps * n / numSamples : n / stepSamples();
            const double q0 = from * playingConst().stepQuarters();
            const uint32_t tick0 = songTickNow();
            wrapped = false;
            stepEvents (from, from + dSteps);
            if (transport == Transport::Playing && ! wrapped)
                pos = from + dSteps;
            // the metronome on each quarter
            const double q1 = q0 + dSteps * playingConst().stepQuarters();
            if (std::floor (q1) != std::floor (q0) || from == 0.0)
            {
                clickEnv = 1.0;
                const int beat = (int) std::floor (q1);
                clickHz = beat % 4 == 0 ? 1500.0 : 1000.0;
            }
            // MIDI clock out: 24 a quarter
            if (! external)
            {
                clockTickAcc += dSteps * playingConst().stepQuarters() * 24.0;
                while (clockTickAcc >= 1.0)
                {
                    clockTickAcc -= 1.0;
                    midiOutBuffer.addEvent (juce::MidiMessage::midiClock(), offset);
                }
            }
            // song events
            if (mode == Mode::Song && songActive && ! eventRecord)
                playSongEvents (tick0, songTickNow() > tick0 ? songTickNow() : tick0 + 1);
        }
        // ---- the arpeggiator runs on its own quarter-note clock (on the beat grid when playing)
        {
            const double arpFrom = transport == Transport::Playing ? from * playingConst().stepQuarters() : clockSeconds * getTempo() / 60.0;
            const double arpTo = arpFrom + n * qPerSample;
            if (transport != Transport::Playing && arpNext > arpTo + 8.0)
                arpNext = -1.0;
            runArp (arpFrom, arpTo);
        }
        // ---- roll while a key is held, stopped (p.25)
        if (transport != Transport::Playing)
            for (int p = 0; p < kFirstSynth + kNumSynthParts; ++p)
            {
                const bool down = isDrum (p) ? held[(size_t) keyOfPart (p)] : (heldNote[(size_t) p] >= 0 && part == p);
                if (! down || ! effective[(size_t) p].roll)
                    continue;
                const double interval = playingConst().stepQuarters() / playingConst().rollType * 60.0 / getTempo();
                const double before = std::fmod (clockSeconds, interval), after = std::fmod (clockSeconds + n / sampleRate, interval);
                if (after < before)
                    triggerPart (p, mapping::accentedVelocity (effective[(size_t) (isDrum (p) ? kDrumAccent : kSynthAccent)].level),
                                 isDrum (p) ? 60 : heldNote[(size_t) p], false);
            }

        applyMotion (pos);
        renderClick (clickBuf.data(), n);
        // ---- the sound
        RackInputs in;
        in.sounds = &effective;
        in.pattern = &playingConst();
        for (int p = 0; p < kNumParts; ++p)
            in.audible[(size_t) p] = partAudible (p);
        in.fx = effectiveFx;
        in.ctx.sampleRate = sampleRate;
        in.ctx.bpm = getTempo();
        in.ctx.stepSeconds = stepSamples() / sampleRate;
        in.ctx.masterTuneCents = mem.global.masterTune;
        in.ctx.audioIn = audioIn != nullptr ? audioIn + offset : nullptr;
        in.masterVolume = knobs[(size_t) Knob::MasterVolume];
        in.tubeGain = knobs[(size_t) Knob::TubeGain];
        in.audioThru = audioThru;
        in.audioIn = in.ctx.audioIn;
        in.click = clickBuf.data();
        in.clickToIndividual = metronome == Metronome::OnIndividual;
        // the input level knob scales what the part / thru hear
        std::array<float, kChunk> scaledIn {};
        if (audioIn != nullptr)
        {
            const float g = (float) mapping::levelGain (knobs[(size_t) Knob::AudioInLevel]);
            for (int i = 0; i < n; ++i)
                scaledIn[(size_t) i] = audioIn[offset + i] * g;
            in.ctx.audioIn = in.audioIn = scaledIn.data();
        }
        rack.render (out, numOut, offset, n, in);
        offset += n;
        clockSeconds += n / sampleRate;
    }
    sampleInBlock = 0;
}
} // namespace valvebox
