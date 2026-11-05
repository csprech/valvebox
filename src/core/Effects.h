#pragma once

// The three effect processors (16 types each, two EDIT knobs), and the valve output stage.
// Stereo in place; allocation-free after prepare().

#include "Model.h"
#include <array>
#include <vector>

namespace valvebox
{
class Effect
{
public:
    void prepare (double sampleRate);
    /** Processes l/r in place. stepSeconds is the sequencer's step (the grain shifter counts in steps). */
    void process (float* l, float* r, int n, const FxSettings& fx, double bpm, double stepSeconds);
    void reset();

private:
    struct Delay
    {
        std::vector<float> buf;
        int w = 0;
        void prepare (int size) { buf.assign ((size_t) std::max (4, size), 0.0f); w = 0; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
        void push (float x) { buf[(size_t) w] = x; w = (w + 1) % (int) buf.size(); }
        float read (double samplesAgo) const;
    };
    struct Svf
    {
        double ic1 = 0, ic2 = 0;
        void reset() { ic1 = ic2 = 0; }
        double lp = 0, bp = 0, hp = 0;
        void run (double x, double hz, double k, double sr);
    };
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        float run (float x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
    };
    static void shelf (Biquad& q, double sr, double hz, double db, bool high);

    double sampleRate = 48000.0;
    FxType current = FxType::Reverb;
    Delay dl, dr;
    // reverb
    std::array<Delay, 8> combL, combR;
    std::array<float, 8> combStoreL {}, combStoreR {};
    std::array<Delay, 4> apL, apR;
    // modulation, filters, dynamics
    double lfo = 0.0, osc = 0.0, env = 0.0, grainTimer = 0.0, grainPlay = 0.0, pitchPhase = 0.0;
    int grainLen = 0, grainCapture = -1, holdCount = 0;
    float holdL = 0, holdR = 0, fbL = 0, fbR = 0;
    std::vector<float> grainL, grainR;
    std::array<Svf, 6> svf {};
    std::array<double, 12> ap {};
    Biquad lowL, lowR, highL, highR;
};

/** VALVE FORCE: two triode stages on the main outputs. TUBE GAIN (0..127) drives them harder:
    more distortion and more level. MASTER VOLUME comes after. */
class ValveStage
{
public:
    void prepare (double sampleRate);
    void process (float* l, float* r, int n, int tubeGain);

private:
    double sampleRate = 48000.0;
    double dcL = 0, dcR = 0, lpL = 0, lpR = 0;
};
} // namespace valvebox
