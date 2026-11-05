#pragma once

// The voices: a drum voice (a wave, its pitch, an EG and modulation) and a monophonic synth voice
// (16 oscillator models, a 4-type filter with drive, one EG for amp and filter, modulation, glide).
// Both render a part into its own stereo bus. Allocation-free after prepare().

#include "Model.h"
#include "Waves.h"
#include <array>
#include <vector>

namespace valvebox
{
/** What every voice needs to know each block. */
struct VoiceContext
{
    double sampleRate = 48000.0;
    double bpm = 120.0;
    double stepSeconds = 0.125;   // one sequencer step
    double masterTuneCents = 0.0;
    const float* audioIn = nullptr; // mono input this block (AUDIO IN + COMB), or nullptr
};

/** The modulation source: saw, square, triangle (free-running), sample & hold, or a decaying EG. */
class Lfo
{
public:
    void prepare (double sr) { sampleRate = sr; }
    void trigger (ModType t);
    /** Advances one sample; returns -1..1 (EG: 1 decaying to 0). */
    float tick (const Sound& s, double bpm);

private:
    double sampleRate = 48000.0, phase = 0.0;
    float held = 0.0f, eg = 0.0f;
    uint32_t rng = 12345;
};

/** One voice per drum part (6A/6B and 7A/7B each share one: they can't sound together). */
class DrumVoice
{
public:
    void prepare (double sr);
    void trigger (int wave, float velocityGain, const Sound& s);
    /** A quick fade: the other half of an exclusive pair started. */
    void choke();
    void render (float* l, float* r, int n, const Sound& s, const VoiceContext& ctx);
    bool isActive() const { return active; }
    float getLevel() const { return meter; }

private:
    double sampleRate = 48000.0, pos = 0.0, age = 0.0;
    const Wave* wave = nullptr;
    float vel = 1.0f, egLevel = 1.0f, fade = 1.0f, meter = 0.0f;
    bool active = false, choking = false;
    Lfo lfo;
};

class SynthVoice
{
public:
    void prepare (double sr);
    /** legato: the previous note was still held (gate overlap): no retrigger, glide to the new pitch. */
    void noteOn (int note, float velocityGain, bool legato, const Sound& s);
    void noteOff();
    void setBend (double semitones) { bend = semitones; }
    void render (float* l, float* r, int n, const Sound& s, const VoiceContext& ctx);
    bool isActive() const { return active; }
    bool isGateOn() const { return gate; }
    int getNote() const { return note; }
    double getPitch() const { return pitch; } // glided, in notes
    float getEnvelope() const { return egLevel; }

private:
    float oscillate (const Sound& s, double hz, double e1, double e2raw, const VoiceContext& ctx, int i);
    float comb (float x, double hz, double fb);
    double sampleRate = 48000.0;
    int note = 60;
    double pitch = 60.0, target = 60.0, bend = 0.0;
    bool active = false, gate = false;
    float vel = 1.0f, egLevel = 0.0f, release = 1.0f;
    std::array<double, 6> ph {};
    double pcmPos = 0.0;
    float noiseHeld = 0.0f;
    int noiseCount = 0;
    uint32_t rng = 777;
    std::vector<float> combLine;
    int combWrite = 0;
    // filters: the synth filter, and the oscillators' own (formant, noise colour)
    struct Svf
    {
        double ic1 = 0, ic2 = 0;
        void reset() { ic1 = ic2 = 0; }
        void process (double x, double hz, double k, double sr, double& lp, double& bp, double& hp);
    };
    Svf filter, f1, f2;
    Lfo lfo;
};
} // namespace valvebox
