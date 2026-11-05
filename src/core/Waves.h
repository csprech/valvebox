#pragma once

// The sample library, made by Valvebox itself (no recorded samples): 207 drum waves in the original's
// categories and numbering (kick 1-41, snare 42-81, rim, clap, hi-hats 95-120, ride, crash, tom,
// percussion, synth perc, reverse, scratch), and 76 instrument / synth waves for the PCM oscillators
// (keyboards, mallets, winds, brass, voices, strings, guitars, basses, synth waves, hits, chord splits,
// drum sets). Rendered once, at 48 kHz.

#include "Model.h"
#include <vector>

namespace valvebox
{
struct Wave
{
    std::vector<float> data;
    int loopStart = -1;      // sustained waves loop [loopStart, size); -1 = one-shot
    double rootNote = 60.0;  // the note it sounds at played back at its own rate
    juce::String name;
};

enum class DrumCategory { Kick = 0, Snare, Rim, Clap, HiHat, Ride, Crash, Tom, Percussion, SynthPerc, Reverse, Scratch };

class WaveLibrary
{
public:
    static constexpr double kRate = 48000.0;
    /** Rendered on first use (about a second); the engine calls it before audio starts. */
    static const WaveLibrary& get();

    const Wave& drum (int index) const;
    /** PCM wave 0..75 for a note: chord splits choose their chord by key range, drum sets play a drum wave per key. */
    const Wave& pcm (int index, int note) const;
    const juce::String& pcmName (int index) const;
    const juce::String& drumName (int index) const { return drum (index).name; }

    static DrumCategory drumCategory (int index);
    static const char* categoryName (DrumCategory c);
    /** First wave of each category (0-based), for the factory kits. */
    static int firstOf (DrumCategory c);
    static bool isChordSplit (int pcmIndex) { return pcmIndex >= 65 && pcmIndex <= 71; }
    static bool isDrumSet (int pcmIndex) { return pcmIndex >= 72; }

private:
    WaveLibrary();
    std::vector<Wave> drums, pcms;
    std::vector<std::array<Wave, 4>> chordSplits; // PCM 66-72
};
} // namespace valvebox
