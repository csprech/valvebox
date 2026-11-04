#pragma once

// The sound side of the instrument: nine drum voices and five synth voices, each part's bus routed
// to the line outs (through its effect when FX SEND is on) or to the individual outs 3/4, the three
// effects in their chain, then VALVE FORCE and MASTER VOLUME on the line outs.

#include "core/Effects.h"
#include "core/Voices.h"
#include <array>

namespace valvebox
{
struct RackInputs
{
    const std::array<Sound, kNumParts>* sounds = nullptr; // with motion sequences applied
    const Pattern* pattern = nullptr;                     // routing flags, effects, chain
    std::array<bool, kNumParts> audible {};               // after mute / solo
    std::array<FxSettings, kNumFx> fx {};                 // with effect motion applied
    VoiceContext ctx;
    int masterVolume = 100, tubeGain = 0;
    bool audioThru = false;
    const float* audioIn = nullptr;
    const float* click = nullptr;   // metronome
    bool clickToIndividual = false; // METRONOME On->3/4
};

class Rack
{
public:
    void prepare (double sampleRate, int maxBlock);
    /** Drum hit or synth note. Drums 6A/6B and 7A/7B choke each other. */
    void trigger (int part, float velocityGain, int note, const Sound& s, bool legato);
    void noteOff (int part);
    void allNotesOff();
    void setBend (int synthPart, double semitones);
    bool isSounding (int part) const;
    bool isGateOn (int part) const;
    double synthPitch (int part) const;

    /** Renders n samples into out[0..3] (L, R, individual 3, 4) at offset. */
    void render (float* const* out, int numOut, int offset, int n, const RackInputs& in);
    /** Peak level of the line outs over the last block (for a level display). */
    float getPeak() const { return peak; }

private:
    std::array<DrumVoice, kNumDrumParts> drums;
    std::array<SynthVoice, kNumSynthParts> synths;
    std::array<Effect, kNumFx> fx;
    ValveStage valve;
    std::vector<float> partL, partR, fxL, fxR, mainL, mainR, indL, indR;
    int maxBlock = 0;
    float peak = 0.0f;
};
} // namespace valvebox
