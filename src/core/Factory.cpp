// Valvebox's own factory patterns and demo songs (the original's 192 preset patterns aren't
// copied: these are new, in the same spirit, one or two per style).

#include "core/Model.h"
#include "core/Waves.h"
#include <initializer_list>
#include <string>

namespace valvebox
{
namespace
{
    using DC = DrumCategory;
    int wave (DC c, int n = 0) { return WaveLibrary::firstOf (c) + n; }

    /** Drum steps from a string per bar: x = hit, X = hit + accent, . = rest. Repeats to fill the length. */
    void drums (Pattern& p, int part, const std::string& bar, int waveIndex, int level = 100, int egTime = 72, int pitch = 64)
    {
        auto& pt = p.parts[(size_t) part];
        pt.sound.wave = (uint8_t) waveIndex;
        pt.sound.level = (uint8_t) level;
        pt.sound.egTime = (uint8_t) egTime;
        pt.sound.pitch = (uint8_t) pitch;
        const int n = p.numSteps();
        for (int s = 0; s < n; ++s)
        {
            const char c = bar[(size_t) (s % (int) bar.size())];
            pt.steps[(size_t) s].on = c == 'x' || c == 'X';
            if (c == 'X')
                p.parts[(size_t) kDrumAccent].steps[(size_t) s].on = true;
        }
    }

    /** Synth steps: notes (-1 = rest, -2 = tie: the note before is held one step longer), repeated. */
    void notes (Pattern& p, int part, std::initializer_list<int> seq, int gateQuarters = 3, bool accentEveryOther = false)
    {
        auto& pt = p.parts[(size_t) part];
        const std::vector<int> v (seq);
        const int n = p.numSteps();
        int last = -1;
        for (int s = 0; s < n; ++s)
        {
            const int x = v[(size_t) (s % (int) v.size())];
            auto& st = pt.steps[(size_t) s];
            if (x == -2 && last >= 0)
            {
                pt.steps[(size_t) last].gate = (uint16_t) (pt.steps[(size_t) last].gate + 4);
                st.on = false;
                continue;
            }
            st.on = x >= 0;
            if (x >= 0)
            {
                st.note = (uint8_t) x;
                st.gate = (uint16_t) gateQuarters;
                last = s;
                if (accentEveryOther && s % 4 == 2)
                    p.parts[(size_t) kSynthAccent].steps[(size_t) s].on = true;
            }
        }
    }

    Sound& synth (Pattern& p, int part, OscType t, int waveIndex, int e1, int e2, int cutoff, int reso, int egInt, int egTime,
                  FilterType f = FilterType::LPF, int drive = 0, int level = 100)
    {
        auto& s = p.parts[(size_t) part].sound;
        s.oscType = t;
        s.wave = (uint8_t) waveIndex;
        s.oscEdit1 = (uint8_t) e1;
        s.oscEdit2 = (uint8_t) e2;
        s.cutoff = (uint8_t) cutoff;
        s.resonance = (uint8_t) reso;
        s.egInt = (uint8_t) egInt;
        s.egTime = (uint8_t) egTime;
        s.filterType = f;
        s.drive = (uint8_t) drive;
        s.level = (uint8_t) level;
        return s;
    }

    Pattern base (const char* name, float tempo, int length, int swing = 50)
    {
        Pattern p = initPattern();
        p.setName (name);
        p.tempo = tempo;
        p.length = (uint8_t) length;
        p.swing = (uint8_t) swing;
        p.parts[(size_t) kDrumAccent].sound.level = 70;
        p.parts[(size_t) kSynthAccent].sound.level = 60;
        return p;
    }

    void send (Pattern& p, int part, int fx) { p.parts[(size_t) part].sound.fxSend = true, p.parts[(size_t) part].sound.fxSelect = (uint8_t) fx; }

    // note numbers
    constexpr int C2 = 36, D2 = 38, E2 = 40, F2 = 41, G2 = 43, A2 = 45, Bb2 = 46, C3 = 48, D3 = 50, Eb3 = 51, F3 = 53, G3 = 55,
                  A3 = 57, Bb3 = 58, C4 = 60, D4 = 62, Eb4 = 63, G4 = 67, A4 = 69, Ab2 = 44, R = -1, T = -2;
    constexpr int S1 = kFirstSynth, S2 = kFirstSynth + 1, S3 = kFirstSynth + 2, S4 = kFirstSynth + 3, S5 = kFirstSynth + 4;

    Pattern electro()
    {
        auto p = base ("Electro1", 128, 2);
        drums (p, 0, "X..x..X...x.....", wave (DC::Kick, 8));
        drums (p, 1, "....X.......X...", wave (DC::Snare, 36), 95);
        drums (p, 5, "x.x.x.x.x.x.x.xx", wave (DC::HiHat, 14), 80, 50);
        drums (p, 2, "..........x.....", wave (DC::Clap, 2), 90);
        drums (p, 4, ".x.....x.x......", wave (DC::SynthPerc, 2), 70);
        synth (p, S1, OscType::Sync, 0, 20, 40, 60, 70, 96, 50, FilterType::LPF, 30);
        notes (p, S1, { C2, R, C2, R, R, C3, R, C2, R, R, Eb3, R, C2, R, G2, R }, 2, true);
        synth (p, S2, OscType::Waveform, 1, 50, 64, 90, 40, 80, 40);
        notes (p, S2, { R, R, G4, R, R, R, Eb4, R, R, R, C4, R, R, R, R, R }, 2);
        send (p, S2, 0);
        p.fx[0] = { FxType::BpmDelay, 70, 50, false };
        return p;
    }

    Pattern house()
    {
        auto p = base ("House1", 124, 2);
        drums (p, 0, "X...X...X...X...", wave (DC::Kick, 1));
        drums (p, 2, "....x.......x...", wave (DC::Clap, 1), 92);
        drums (p, 5, "x.x.x.x.x.x.x.x.", wave (DC::HiHat, 0), 70, 45);
        drums (p, 6, "..X...X...X...X.", wave (DC::HiHat, 1), 80, 70);
        drums (p, 4, "...x.....x......", wave (DC::Percussion, 18), 60);
        synth (p, S1, OscType::Waveform, 3, 30, 64, 50, 30, 80, 40);
        notes (p, S1, { A2, R, R, A2, R, R, A3, R, A2, R, R, A2, R, G2, R, R }, 2);
        synth (p, S5, OscType::PcmComb, 67, 0, 0, 110, 10, 64, 60); // an electric piano chord split, off the beat
        notes (p, S5, { R, R, A3, R, R, R, A3, R, R, R, A3, R, R, R, A3, R }, 2);
        send (p, S5, 1);
        p.fx[1] = { FxType::Reverb, 70, 50, false };
        return p;
    }

    Pattern techno()
    {
        auto p = base ("Techno1", 132, 1);
        drums (p, 0, "X...X...X...X...", wave (DC::Kick, 28));
        drums (p, 1, "....x.......x...", wave (DC::Rim, 0), 70);
        drums (p, 5, "xxxxxxxxxxxxxxxx", wave (DC::HiHat, 8), 65, 40);
        drums (p, 6, "..x...x...x...x.", wave (DC::HiHat, 9), 75, 60);
        drums (p, 7, "x...............", wave (DC::Ride, 0), 60);
        p.parts[5].sound.roll = false;
        synth (p, S1, OscType::Waveform, 0, 10, 64, 40, 90, 90, 30, FilterType::LPF, 60);
        notes (p, S1, { R, R, F2, R, R, R, F2, R, R, R, F2, R, R, F3, F2, R }, 1);
        synth (p, S2, OscType::RingMod, 3, 90, 90, 100, 20, 64, 20);
        notes (p, S2, { R, R, R, C4, R, R, R, R, R, R, R, C4, R, R, R, R }, 1);
        send (p, S2, 0);
        p.fx[0] = { FxType::BpmDelay, 80, 70, false };
        return p;
    }

    Pattern dnb()
    {
        auto p = base ("DnB1", 174, 2);
        drums (p, 0, "X.........X.....X.x.......X.....", wave (DC::Kick, 37));
        drums (p, 1, "....X.......X.......X..x....X...", wave (DC::Snare, 3), 100);
        drums (p, 5, "x.xxx.x.x.xxx.x.", wave (DC::HiHat, 10), 65, 40);
        drums (p, 7, "x...............x.......x.......", wave (DC::Ride, 4), 55);
        synth (p, S1, OscType::Unison, 2, 90, 64, 40, 20, 70, 90, FilterType::LPF, 40);
        notes (p, S1, { D2, T, T, T, R, R, D2, T, R, R, F2, T, T, T, R, R, A2, T, T, T, R, R, G2, T, R, R, F2, T, R, R, R, R }, 3);
        synth (p, S3, OscType::PcmComb, 18, 0, 0, 80, 10, 64, 120);
        notes (p, S3, { D3, T, T, T, T, T, T, T, R, R, R, R, R, R, R, R }, 3);
        send (p, S3, 1);
        p.fx[1] = { FxType::Reverb, 100, 60, false };
        return p;
    }

    Pattern hipHop()
    {
        auto p = base ("HipHop1", 92, 2, 62);
        drums (p, 0, "X.......x.x.....X.....x...x.....", wave (DC::Kick, 24));
        drums (p, 1, "....X.......X...", wave (DC::Snare, 16), 100);
        drums (p, 5, "x.x.x.x.x.x.x.x.", wave (DC::HiHat, 12), 65, 40);
        drums (p, 4, "..............x.", wave (DC::Rim, 3), 70);
        synth (p, S1, OscType::Waveform, 3, 0, 64, 40, 0, 64, 70);
        notes (p, S1, { D2, R, R, R, R, R, R, D2, F2, R, R, R, A2, R, R, R }, 6);
        synth (p, S4, OscType::PcmComb, 67, 0, 0, 100, 0, 64, 80);
        notes (p, S4, { D3, R, R, R, R, R, R, R, C3, R, R, R, R, R, R, R }, 10);
        send (p, S4, 1);
        p.fx[1] = { FxType::Reverb, 50, 40, false };
        return p;
    }

    Pattern trance()
    {
        auto p = base ("Trance1", 138, 2);
        drums (p, 0, "X...X...X...X...", wave (DC::Kick, 4));
        drums (p, 2, "....x.......x...", wave (DC::Clap, 3), 85);
        drums (p, 6, "..x...x...x...x.", wave (DC::HiHat, 3), 80, 65);
        drums (p, 8, "X...............................", wave (DC::Crash, 0), 70, 110);
        synth (p, S1, OscType::Waveform, 0, 0, 64, 50, 40, 80, 30);
        notes (p, S1, { R, R, A2, R, R, R, A2, R, R, R, A2, R, R, R, A2, R, R, R, F2, R, R, R, F2, R, R, R, G2, R, R, R, G2, R }, 2);
        synth (p, S2, OscType::Unison, 3, 80, 64, 90, 30, 70, 50);
        notes (p, S2, { A4, R, R, E2 + 24, R, R, C4, R, A4, R, R, E2 + 24, R, R, D4, R }, 2);
        send (p, S2, 0);
        p.fx[0] = { FxType::BpmDelay, 90, 60, false };
        return p;
    }

    Pattern acid()
    {
        auto p = base ("Acid1", 130, 1);
        drums (p, 0, "X...X...X...X...", wave (DC::Kick, 12));
        drums (p, 5, "..x...x...x...x.", wave (DC::HiHat, 2), 75, 50);
        drums (p, 2, "....x.......x...", wave (DC::Clap, 0), 85);
        auto& s = synth (p, S1, OscType::Waveform, 0, 0, 64, 35, 110, 100, 40, FilterType::LPF, 50);
        s.glide = 40;
        notes (p, S1, { C2, C3, R, C2, Eb3, R, C2, T, F2, R, C3, G2, R, Bb2, C2, R }, 3, true);
        return p;
    }

    Pattern dubTechno()
    {
        auto p = base ("DubTech1", 120, 2);
        drums (p, 0, "X...X...X...X...", wave (DC::Kick, 2), 95);
        drums (p, 6, "..x...x...x...x.", wave (DC::HiHat, 4), 60, 55);
        drums (p, 1, "....x.......x...", wave (DC::Rim, 3), 60);
        synth (p, S2, OscType::Chord, 1, 56, 64, 60, 40, 76, 50); // minor 7th chord
        notes (p, S2, { R, R, R, D3, R, R, R, R, R, R, D3, R, R, R, R, R }, 2);
        send (p, S2, 0);
        p.fx[0] = { FxType::BpmDelay, 75, 90, false };
        p.fx[1] = { FxType::Reverb, 110, 70, false };
        p.chain = Chain::OneTwo;
        return p;
    }

    Pattern breaks()
    {
        auto p = base ("Breaks1", 135, 2);
        drums (p, 0, "X.....x...x.....X.......x.x.....", wave (DC::Kick, 36));
        drums (p, 1, "....X.......X..x....X.......X...", wave (DC::Snare, 4), 100);
        drums (p, 5, "x.x.x.x.x.x.x.x.", wave (DC::HiHat, 16), 70, 40);
        drums (p, 3, "..............x.", wave (DC::Tom, 1), 70);
        synth (p, S1, OscType::CrossMod, 1, 40, 40, 70, 30, 80, 40);
        notes (p, S1, { E2, R, R, E2, R, R, G2, R, R, R, E2, R, R, A2, R, R }, 2);
        return p;
    }

    Pattern garage()
    {
        auto p = base ("Garage1", 132, 2, 60);
        drums (p, 0, "X.........X.....X.......X.......", wave (DC::Kick, 3));
        drums (p, 1, "....X.......X...", wave (DC::Snare, 21), 95);
        drums (p, 5, "x.xx..x.x.xx..x.", wave (DC::HiHat, 22), 70, 40);
        drums (p, 4, ".......x.....x..", wave (DC::Percussion, 33), 70);
        synth (p, S1, OscType::Waveform, 3, 20, 64, 45, 30, 70, 50);
        notes (p, S1, { F2, R, R, R, R, R, F2, R, R, R, Ab2, R, R, R, R, R }, 2);
        synth (p, S3, OscType::Formant, 0, 60, 64, 120, 0, 64, 40);
        notes (p, S3, { R, R, R, F3, R, R, R, R, R, R, R, Eb3, R, R, R, R }, 2);
        return p;
    }

    Pattern minimal()
    {
        auto p = base ("Minimal1", 126, 1);
        drums (p, 0, "X...X...X...X...", wave (DC::Kick, 16), 95);
        drums (p, 4, "..x.......x..x..", wave (DC::Percussion, 34), 70);
        drums (p, 1, "....x.......x...", wave (DC::Rim, 4), 60);
        drums (p, 5, "..x...x...x...x.", wave (DC::HiHat, 18), 60, 40);
        synth (p, S1, OscType::Vpm, 3, 60, 40, 80, 10, 80, 30);
        notes (p, S1, { R, R, R, G2, R, R, R, R, R, G2, R, R, R, R, C3, R }, 1);
        return p;
    }

    Pattern idm()
    {
        auto p = base ("IDM1", 110, 1);
        p.beat = Beat::EighthTriplet;
        p.lastStep = 12;
        drums (p, 0, "X..x..X.....", wave (DC::Kick, 40));
        drums (p, 1, "...x.....x..", wave (DC::SynthPerc, 7), 80);
        drums (p, 5, "xxxxx.xxxx.x", wave (DC::HiHat, 24), 60, 30);
        synth (p, S1, OscType::Additive, 2, 70, 90, 100, 30, 64, 40);
        notes (p, S1, { C3, R, G3, Eb3, R, R, Bb3, R, F3, R, R, D3 }, 2);
        send (p, S1, 2);
        p.fx[2] = { FxType::GrainShifter, 40, 50, false };
        return p;
    }

    Pattern ambient()
    {
        auto p = base ("Ambient1", 90, 4);
        drums (p, 0, "X.......X.......", wave (DC::Kick, 6), 70, 90);
        drums (p, 5, "..x...x...x...x.", wave (DC::HiHat, 20), 45, 50);
        synth (p, S2, OscType::PcmComb, 15, 30, 40, 90, 20, 64, 127);
        p.parts[(size_t) S2].sound.ampEg = AmpEg::Gate;
        notes (p, S2, { D3, T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, F3, T, T, T, T, T, T, T, T, T, T, T, T, T, T, T,
                        Bb2, T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, A2, T, T, T, T, T, T, T, T, T, T, T, T, T, T, T }, 3);
        send (p, S2, 1);
        p.fx[1] = { FxType::Reverb, 120, 90, false };
        return p;
    }

    Pattern funk()
    {
        auto p = base ("Funk1", 104, 2, 56);
        drums (p, 0, "X.....X.X.......X.....X...x.....", wave (DC::Kick, 30));
        drums (p, 1, "....X.......X..x", wave (DC::Snare, 12), 100);
        drums (p, 5, "xxxxxxxxxxxxxxxx", wave (DC::HiHat, 10), 55, 35);
        synth (p, S1, OscType::PcmComb, 29, 0, 0, 100, 10, 64, 60); // slap bass
        notes (p, S1, { E2, R, E2 + 12, R, R, E2, R, G2, A2, R, R, E2, R, D2, E2, R }, 2);
        synth (p, S4, OscType::PcmComb, 2, 0, 0, 110, 0, 64, 30);
        notes (p, S4, { R, R, R, R, E2 + 24, R, R, E2 + 24, R, R, R, R, E2 + 24, R, R, R }, 1);
        return p;
    }

    Pattern hardcore()
    {
        auto p = base ("Gabba1", 170, 1);
        drums (p, 0, "X...X...X...X...", wave (DC::Kick, 30), 110, 60);
        drums (p, 6, "..x...x...x...x.", wave (DC::HiHat, 1), 70, 50);
        synth (p, S1, OscType::Waveform, 0, 0, 64, 60, 80, 90, 30, FilterType::LPF, 100);
        notes (p, S1, { R, R, A2, R, R, R, A2, R, R, R, A2, R, R, R, A3, R }, 1);
        send (p, 0, 2);
        p.fx[2] = { FxType::Distortion, 80, 70, false };
        return p;
    }
} // namespace

Memory factoryMemory()
{
    Memory m;
    const Pattern pats[] = { electro(), house(), techno(), dnb(), hipHop(), trance(), acid(), dubTechno(), breaks(),
                             garage(),  minimal(), idm(), ambient(), funk(), hardcore() };
    int i = 0;
    for (auto& p : pats)
        m.patterns[(size_t) i++] = p;
    // a demo song: house builds into techno, then a breakdown and back
    auto& s = m.songs[0];
    s.setName ("Demo1");
    s.tempo = 128;
    const int order[] = { 1, 1, 0, 0, 2, 2, 7, 7, 1, 1 };
    int n = 0;
    for (int o : order)
        s.positions[(size_t) n++] = { (int16_t) o, 0 };
    auto& s2 = m.songs[1];
    s2.setName ("Demo2");
    s2.tempo = 174;
    const int order2[] = { 3, 3, 3, 3, 12, 3, 3 };
    n = 0;
    for (int o : order2)
        s2.positions[(size_t) n++] = { (int16_t) o, o == 12 ? (int8_t) -2 : (int8_t) 0 };
    for (int k = 0; k < 16; ++k)
        m.patternSet[(size_t) k] = (int16_t) std::min (k, kNumPatterns - 1);
    return m;
}
} // namespace valvebox
