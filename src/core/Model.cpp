#include "core/Model.h"
#include <algorithm>

namespace valvebox
{
const char* partName (int part)
{
    static const char* names[kNumParts] = { "Drum1",  "Drum2",  "Drum3",  "Drum4",  "Drum5",  "Drum6A",   "Drum6B",  "Drum7A",
                                            "Drum7B", "Synth1", "Synth2", "Synth3", "Synth4", "Synth5", "DrmAccnt", "SynAccnt" };
    return part >= 0 && part < kNumParts ? names[part] : "";
}

bool isSwitchParam (Param p)
{
    return p == Param::AmpEg || p == Param::Roll || p == Param::FxSend || p == Param::FxSelect || p == Param::ModType
           || p == Param::ModDest || p == Param::ModBpmSync || p == Param::FilterType;
}

const char* paramShortName (Param p)
{
    static const char* names[kNumParams] = { "Pitch",  "EGTime", "Pan",    "Level",  "AmpEG",  "Roll",   "FxSend", "FxSel",
                                             "ModSpd", "ModDpt", "ModTyp", "ModDst", "ModBPM", "OscEd1", "OscEd2", "Cutoff",
                                             "EgInt",  "Reso",   "Drive",  "FltTyp", "FxEd1",  "FxEd2" };
    return names[(int) p];
}

namespace
{
    juce::String nameFrom (const std::array<char, 9>& a) { return juce::String (a.data()).trimEnd(); }
    void nameTo (std::array<char, 9>& a, const juce::String& n)
    {
        a.fill (0);
        auto s = n.substring (0, 8);
        for (int i = 0; i < s.length(); ++i)
        {
            const auto c = s[i];
            a[(size_t) i] = (c >= 32 && c < 127) ? (char) c : '?';
        }
    }
} // namespace

juce::String Pattern::getName() const { return nameFrom (name); }
void Pattern::setName (const juce::String& n) { nameTo (name, n); }
juce::String Song::getName() const { return nameFrom (name); }
void Song::setName (const juce::String& n) { nameTo (name, n); }

double Pattern::stepQuarters() const
{
    switch (beat)
    {
        case Beat::Sixteenth: return 0.25;
        case Beat::ThirtySecond: return 0.125;
        case Beat::EighthTriplet: return 1.0 / 3.0;
        case Beat::SixteenthTriplet: return 1.0 / 6.0;
    }
    return 0.25;
}

int Pattern::motionCount() const
{
    int n = 0;
    for (auto& m : motion)
        n += m.used() ? 1 : 0;
    return n;
}

int Pattern::findMotion (int target, Param p) const
{
    for (int i = 0; i < kMaxMotion; ++i)
        if (motion[(size_t) i].target == target && motion[(size_t) i].param == p)
            return i;
    return -1;
}

Sound defaultSound (int part)
{
    Sound s;
    if (isDrum (part))
    {
        // a kit to start from: kick, snare, clap, tom, rim, closed / open hat, ride / crash
        static const uint8_t waves[kNumDrumParts] = { 0, 41, 87, 131, 81, 94, 95, 120, 126 };
        s.wave = waves[part];
        s.egTime = part == 6 ? 90 : 72;
    }
    else if (isSynth (part))
    {
        s.oscType = OscType::Waveform;
        s.wave = 0; // saw
        s.oscEdit1 = 0;
        s.oscEdit2 = 64;
        s.cutoff = 90;
        s.resonance = 30;
        s.egInt = 84;
        s.egTime = 60;
    }
    else
        s.level = 64; // accent amount
    return s;
}

void clearPartSteps (Part& p)
{
    for (auto& st : p.steps)
        st = { false, 60, 3 }; // C4, gate 0.75
}

Pattern initPattern()
{
    Pattern p;
    p.setName ("Init");
    for (int i = 0; i < kNumParts; ++i)
    {
        p.parts[(size_t) i] = Part();
        p.parts[(size_t) i].sound = defaultSound (i);
        clearPartSteps (p.parts[(size_t) i]);
    }
    p.fx[0] = { FxType::BpmDelay, 64, 50, false };
    p.fx[1] = { FxType::Reverb, 70, 60, false };
    p.fx[2] = { FxType::ChorusFlanger, 40, 40, false };
    for (auto& m : p.motion)
        m = Motion();
    return p;
}

int Song::numPositions() const
{
    for (int i = 0; i < kMaxPositions; ++i)
        if (positions[(size_t) i].pattern < 0)
            return i;
    return kMaxPositions;
}

Song initSong()
{
    Song s;
    s.setName ("Song");
    for (auto& p : s.positions)
        p = Position();
    return s;
}

Memory::Memory()
{
    patterns.assign ((size_t) kNumPatterns, initPattern());
    songs.assign ((size_t) kNumSongs, initSong());
    for (int i = 0; i < kNumSongs; ++i)
        songs[(size_t) i].setName ("Song " + juce::String (i + 1).paddedLeft ('0', 2));
    for (int i = 0; i < (int) patternSet.size(); ++i)
        patternSet[(size_t) i] = (int16_t) (i % kNumPatterns);
}

juce::String patternNumberText (int index)
{
    index = juce::jlimit (0, kNumPatterns - 1, index);
    return juce::String::charToString ((juce::juce_wchar) ('A' + index / 64)) + "." + juce::String (index % 64 + 1).paddedLeft ('0', 2);
}

// ---------------------------------------------------------------- JSON

namespace
{
    juce::var obj() { return juce::var (new juce::DynamicObject()); }
    void set (juce::var& o, const juce::Identifier& k, const juce::var& v) { o.getDynamicObject()->setProperty (k, v); }
    int geti (const juce::var& o, const char* k, int def, int lo, int hi)
    {
        if (! o.hasProperty (k))
            return def;
        return juce::jlimit (lo, hi, (int) o[k]);
    }
    bool getb (const juce::var& o, const char* k, bool def) { return o.hasProperty (k) ? (bool) o[k] : def; }

    juce::var soundToJson (const Sound& s)
    {
        auto o = obj();
        set (o, "pitch", s.pitch);
        set (o, "glide", s.glide);
        set (o, "egTime", s.egTime);
        set (o, "pan", s.pan);
        set (o, "level", s.level);
        set (o, "ampEg", (int) s.ampEg);
        set (o, "roll", s.roll);
        set (o, "fxSelect", s.fxSelect);
        set (o, "fxSend", s.fxSend);
        set (o, "motion", (int) s.motion);
        set (o, "modSpeed", s.modSpeed);
        set (o, "modDepth", s.modDepth);
        set (o, "modType", (int) s.modType);
        set (o, "modDest", (int) s.modDest);
        set (o, "modBpmSync", s.modBpmSync);
        set (o, "wave", s.wave);
        set (o, "oscType", (int) s.oscType);
        set (o, "oscEdit1", s.oscEdit1);
        set (o, "oscEdit2", s.oscEdit2);
        set (o, "filterType", (int) s.filterType);
        set (o, "cutoff", s.cutoff);
        set (o, "egInt", s.egInt);
        set (o, "resonance", s.resonance);
        set (o, "drive", s.drive);
        set (o, "synthTune", s.synthTune);
        return o;
    }

    Sound soundFromJson (const juce::var& o, Sound s)
    {
        s.pitch = (uint8_t) geti (o, "pitch", s.pitch, 0, 127);
        s.glide = (uint8_t) geti (o, "glide", s.glide, 0, 127);
        s.egTime = (uint8_t) geti (o, "egTime", s.egTime, 0, 127);
        s.pan = (uint8_t) geti (o, "pan", s.pan, 0, 127);
        s.level = (uint8_t) geti (o, "level", s.level, 0, 127);
        s.ampEg = (AmpEg) geti (o, "ampEg", (int) s.ampEg, 0, 1);
        s.roll = getb (o, "roll", s.roll);
        s.fxSelect = (uint8_t) geti (o, "fxSelect", s.fxSelect, 0, 2);
        s.fxSend = getb (o, "fxSend", s.fxSend);
        s.motion = (MotionMode) geti (o, "motion", (int) s.motion, 0, 2);
        s.modSpeed = (uint8_t) geti (o, "modSpeed", s.modSpeed, 0, 127);
        s.modDepth = (uint8_t) geti (o, "modDepth", s.modDepth, 0, 127);
        s.modType = (ModType) geti (o, "modType", (int) s.modType, 0, 4);
        s.modDest = (ModDest) geti (o, "modDest", (int) s.modDest, 0, 5);
        s.modBpmSync = getb (o, "modBpmSync", s.modBpmSync);
        s.wave = (uint8_t) geti (o, "wave", s.wave, 0, 255);
        s.oscType = (OscType) geti (o, "oscType", (int) s.oscType, 0, kNumOscTypes - 1);
        s.oscEdit1 = (uint8_t) geti (o, "oscEdit1", s.oscEdit1, 0, 127);
        s.oscEdit2 = (uint8_t) geti (o, "oscEdit2", s.oscEdit2, 0, 127);
        s.filterType = (FilterType) geti (o, "filterType", (int) s.filterType, 0, 3);
        s.cutoff = (uint8_t) geti (o, "cutoff", s.cutoff, 0, 127);
        s.egInt = (uint8_t) geti (o, "egInt", s.egInt, 0, 127);
        s.resonance = (uint8_t) geti (o, "resonance", s.resonance, 0, 127);
        s.drive = (uint8_t) geti (o, "drive", s.drive, 0, 127);
        s.synthTune = (int8_t) geti (o, "synthTune", s.synthTune, -50, 50);
        return s;
    }

    // steps and motion values pack into base64 (4 bytes a step, 2 bytes a motion value)
    juce::String packSteps (const std::array<Step, kMaxSteps>& steps)
    {
        juce::MemoryBlock mb;
        for (auto& st : steps)
        {
            const uint8_t b[4] = { (uint8_t) (st.on ? 1 : 0), st.note, (uint8_t) (st.gate & 0xff), (uint8_t) (st.gate >> 8) };
            mb.append (b, 4);
        }
        return mb.toBase64Encoding();
    }
    bool unpackSteps (const juce::String& s, std::array<Step, kMaxSteps>& steps)
    {
        juce::MemoryBlock mb;
        if (! mb.fromBase64Encoding (s) || mb.getSize() != (size_t) kMaxSteps * 4)
            return false;
        auto* d = static_cast<const uint8_t*> (mb.getData());
        for (int i = 0; i < kMaxSteps; ++i)
            steps[(size_t) i] = { d[i * 4] != 0, (uint8_t) std::min<int> (127, d[i * 4 + 1]),
                                  (uint16_t) juce::jlimit (1, 512, d[i * 4 + 2] | (d[i * 4 + 3] << 8)) };
        return true;
    }
    juce::String packValues (const std::array<int16_t, kMaxSteps>& v)
    {
        juce::MemoryBlock mb;
        for (auto x : v)
        {
            const uint8_t b[2] = { (uint8_t) (x & 0xff), (uint8_t) ((uint16_t) x >> 8) };
            mb.append (b, 2);
        }
        return mb.toBase64Encoding();
    }
    bool unpackValues (const juce::String& s, std::array<int16_t, kMaxSteps>& v)
    {
        juce::MemoryBlock mb;
        if (! mb.fromBase64Encoding (s) || mb.getSize() != (size_t) kMaxSteps * 2)
            return false;
        auto* d = static_cast<const uint8_t*> (mb.getData());
        for (int i = 0; i < kMaxSteps; ++i)
            v[(size_t) i] = (int16_t) (d[i * 2] | (d[i * 2 + 1] << 8));
        return true;
    }
} // namespace

juce::var patternToJson (const Pattern& p)
{
    auto o = obj();
    set (o, "format", "valvebox-pattern");
    set (o, "name", p.getName());
    set (o, "tempo", p.tempo);
    set (o, "swing", p.swing);
    set (o, "length", p.length);
    set (o, "beat", (int) p.beat);
    set (o, "lastStep", p.lastStep);
    set (o, "rollType", p.rollType);
    set (o, "arpScale", p.arpScale);
    set (o, "arpCentre", p.arpCentre);
    set (o, "chain", (int) p.chain);
    juce::Array<juce::var> parts, fx, motion;
    for (auto& part : p.parts)
    {
        auto po = obj();
        set (po, "sound", soundToJson (part.sound));
        set (po, "steps", packSteps (part.steps));
        set (po, "accentSw", part.accentSw);
        set (po, "swingSw", part.swingSw);
        set (po, "outBus34", part.outBus34);
        set (po, "muted", part.muted);
        parts.add (po);
    }
    for (auto& f : p.fx)
    {
        auto fo = obj();
        set (fo, "type", (int) f.type);
        set (fo, "edit1", f.edit1);
        set (fo, "edit2", f.edit2);
        set (fo, "motion", f.motion);
        fx.add (fo);
    }
    for (auto& m : p.motion)
    {
        if (! m.used())
            continue;
        auto mo = obj();
        set (mo, "target", m.target);
        set (mo, "param", (int) m.param);
        set (mo, "values", packValues (m.values));
        motion.add (mo);
    }
    set (o, "parts", parts);
    set (o, "fx", fx);
    set (o, "motion", motion);
    return o;
}

bool patternFromJson (const juce::var& v, Pattern& out)
{
    if (v.getProperty ("format", {}).toString() != "valvebox-pattern")
        return false;
    Pattern p = initPattern();
    p.setName (v.getProperty ("name", "Init").toString());
    p.tempo = juce::jlimit (20.0f, 300.0f, (float) (double) v.getProperty ("tempo", 120.0));
    p.swing = (uint8_t) geti (v, "swing", 50, 50, 75);
    p.length = (uint8_t) geti (v, "length", 1, 1, kMaxLength);
    p.beat = (Beat) geti (v, "beat", 0, 0, 3);
    p.lastStep = (uint8_t) geti (v, "lastStep", 16, 1, 16);
    p.rollType = (uint8_t) geti (v, "rollType", 3, 2, 4);
    p.arpScale = (uint8_t) geti (v, "arpScale", 0, 0, kNumArpScales - 1);
    p.arpCentre = (uint8_t) geti (v, "arpCentre", 60, 0, 127);
    p.chain = (Chain) geti (v, "chain", 0, 0, 3);
    if (auto* parts = v["parts"].getArray())
        for (int i = 0; i < std::min (kNumParts, parts->size()); ++i)
        {
            const auto& po = parts->getReference (i);
            auto& part = p.parts[(size_t) i];
            part.sound = soundFromJson (po["sound"], part.sound);
            unpackSteps (po["steps"].toString(), part.steps);
            part.accentSw = getb (po, "accentSw", true);
            part.swingSw = getb (po, "swingSw", true);
            part.outBus34 = getb (po, "outBus34", false);
            part.muted = getb (po, "muted", false);
        }
    if (auto* fx = v["fx"].getArray())
        for (int i = 0; i < std::min (kNumFx, fx->size()); ++i)
        {
            const auto& fo = fx->getReference (i);
            p.fx[(size_t) i] = { (FxType) geti (fo, "type", 0, 0, kNumFxTypes - 1), (uint8_t) geti (fo, "edit1", 64, 0, 127),
                                 (uint8_t) geti (fo, "edit2", 64, 0, 127), getb (fo, "motion", false) };
        }
    if (auto* motion = v["motion"].getArray())
        for (int i = 0, n = 0; i < motion->size() && n < kMaxMotion; ++i)
        {
            const auto& mo = motion->getReference (i);
            Motion m;
            m.target = (int8_t) geti (mo, "target", -1, -1, kFxTarget + kNumFx - 1);
            m.param = (Param) geti (mo, "param", 0, 0, kNumParams - 1);
            if (m.target >= 0 && unpackValues (mo["values"].toString(), m.values))
                p.motion[(size_t) n++] = m;
        }
    out = p;
    return true;
}

juce::var songToJson (const Song& s)
{
    auto o = obj();
    set (o, "format", "valvebox-song");
    set (o, "name", s.getName());
    set (o, "tempo", s.tempo);
    set (o, "tempoLock", s.tempoLock);
    set (o, "muteHold", s.muteHold);
    set (o, "nextSong", s.nextSong);
    juce::Array<juce::var> pos, ev;
    for (int i = 0; i < s.numPositions(); ++i)
        pos.add (juce::Array<juce::var> { s.positions[(size_t) i].pattern, s.positions[(size_t) i].noteOffset });
    for (auto& e : s.events)
        ev.add (juce::Array<juce::var> { (int) e.tick, (int) e.type, e.part, e.param, e.value });
    set (o, "positions", pos);
    set (o, "events", ev);
    return o;
}

bool songFromJson (const juce::var& v, Song& out)
{
    if (v.getProperty ("format", {}).toString() != "valvebox-song")
        return false;
    Song s = initSong();
    s.setName (v.getProperty ("name", "Song").toString());
    s.tempo = juce::jlimit (20.0f, 300.0f, (float) (double) v.getProperty ("tempo", 120.0));
    s.tempoLock = getb (v, "tempoLock", false);
    s.muteHold = getb (v, "muteHold", false);
    s.nextSong = (int8_t) geti (v, "nextSong", -1, -1, kNumSongs - 1);
    if (auto* pos = v["positions"].getArray())
        for (int i = 0; i < std::min (kMaxPositions, pos->size()); ++i)
            if (auto* pr = pos->getReference (i).getArray(); pr != nullptr && pr->size() >= 2)
                s.positions[(size_t) i] = { (int16_t) juce::jlimit (0, kNumPatterns - 1, (int) (*pr)[0]),
                                            (int8_t) juce::jlimit (-24, 24, (int) (*pr)[1]) };
    if (auto* ev = v["events"].getArray())
        for (int i = 0; i < std::min (kMaxEvents, ev->size()); ++i)
            if (auto* e = ev->getReference (i).getArray(); e != nullptr && e->size() >= 5)
                s.events.push_back ({ (uint32_t) (int) (*e)[0], (SongEvent::Type) juce::jlimit (0, 7, (int) (*e)[1]),
                                      (uint8_t) (int) (*e)[2], (uint8_t) (int) (*e)[3], (int16_t) (int) (*e)[4] });
    std::stable_sort (s.events.begin(), s.events.end(), [] (const SongEvent& a, const SongEvent& b) { return a.tick < b.tick; });
    out = std::move (s);
    return true;
}

juce::var memoryToJson (const Memory& m)
{
    auto o = obj();
    set (o, "format", "valvebox-all");
    set (o, "version", 1);
    juce::Array<juce::var> pats, songs, ps;
    for (auto& p : m.patterns)
        pats.add (patternToJson (p));
    for (auto& s : m.songs)
        songs.add (songToJson (s));
    for (auto x : m.patternSet)
        ps.add (x);
    set (o, "patterns", pats);
    set (o, "songs", songs);
    set (o, "patternSet", ps);
    auto g = obj();
    const auto& gl = m.global;
    set (g, "masterTune", gl.masterTune);
    set (g, "protect", gl.protect);
    set (g, "arpReverse", gl.arpReverse);
    juce::Array<juce::var> ch, dn;
    for (auto c : gl.midiChannel)
        ch.add (c + 1);
    for (auto n : gl.drumNotes)
        dn.add (n);
    set (g, "midiChannels", ch);
    set (g, "drumNotes", dn);
    juce::Array<juce::var> cc;
    for (auto c : gl.ccAssign)
        cc.add (c);
    set (g, "ccAssign", cc);
    set (g, "clock", (int) gl.clock);
    set (g, "bendRange", gl.bendRange);
    set (g, "filter", juce::String (gl.filterProgram ? "P" : "") + (gl.filterControl ? "C" : "") + (gl.filterExclusive ? "E" : "")
                          + (gl.filterNotes ? "N" : ""));
    set (o, "global", g);
    return o;
}

bool memoryFromJson (const juce::var& v, Memory& out)
{
    if (v.getProperty ("format", {}).toString() != "valvebox-all")
        return false;
    Memory m;
    if (auto* pats = v["patterns"].getArray())
        for (int i = 0; i < std::min (kNumPatterns, pats->size()); ++i)
            patternFromJson (pats->getReference (i), m.patterns[(size_t) i]);
    if (auto* songs = v["songs"].getArray())
        for (int i = 0; i < std::min (kNumSongs, songs->size()); ++i)
            songFromJson (songs->getReference (i), m.songs[(size_t) i]);
    if (auto* ps = v["patternSet"].getArray())
        for (int i = 0; i < std::min ((int) m.patternSet.size(), ps->size()); ++i)
            m.patternSet[(size_t) i] = (int16_t) juce::jlimit (0, kNumPatterns - 1, (int) (*ps)[i]);
    const auto g = v["global"];
    auto& gl = m.global;
    gl.masterTune = (int8_t) geti (g, "masterTune", 0, -50, 50);
    gl.protect = getb (g, "protect", true);
    gl.arpReverse = getb (g, "arpReverse", false);
    if (auto* ch = g["midiChannels"].getArray())
        for (int i = 0; i < std::min (6, ch->size()); ++i)
            gl.midiChannel[(size_t) i] = (uint8_t) (juce::jlimit (1, 16, (int) (*ch)[i]) - 1);
    if (auto* dn = g["drumNotes"].getArray())
        for (int i = 0; i < std::min (kNumDrumParts, dn->size()); ++i)
            gl.drumNotes[(size_t) i] = (uint8_t) juce::jlimit (0, 127, (int) (*dn)[i]);
    if (auto* cc = g["ccAssign"].getArray()) // (files from before it was saved keep the defaults)
        for (int i = 0; i < std::min (kNumKnobCcs, cc->size()); ++i)
            gl.ccAssign[(size_t) i] = (int8_t) juce::jlimit (-1, 127, (int) (*cc)[i]);
    gl.clock = (ClockMode) geti (g, "clock", 0, 0, 2);
    gl.bendRange = (int8_t) geti (g, "bendRange", 2, -12, 12);
    if (g.hasProperty ("filter"))
    {
        const auto f = g["filter"].toString();
        gl.filterProgram = f.containsChar ('P');
        gl.filterControl = f.containsChar ('C');
        gl.filterExclusive = f.containsChar ('E');
        gl.filterNotes = f.containsChar ('N');
    }
    out = std::move (m);
    return true;
}
} // namespace valvebox
