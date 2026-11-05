#include "Rack.h"
#include "Mapping.h"
#include <algorithm>
#include <cmath>

namespace valvebox
{
void Rack::prepare (double sr, int block)
{
    maxBlock = std::max (1, block);
    for (auto& d : drums)
        d.prepare (sr);
    for (auto& s : synths)
        s.prepare (sr);
    for (auto& f : fx)
        f.prepare (sr);
    valve.prepare (sr);
    for (auto* b : { &partL, &partR, &mainL, &mainR, &indL, &indR })
        b->assign ((size_t) maxBlock, 0.0f);
    fxL.assign ((size_t) (maxBlock * kNumFx), 0.0f);
    fxR.assign ((size_t) (maxBlock * kNumFx), 0.0f);
}

void Rack::trigger (int part, float velocityGain, int note, const Sound& s, bool legato)
{
    if (isDrum (part))
    {
        // the exclusive pairs: 6A/6B (5, 6) and 7A/7B (7, 8)
        if (part >= 5)
            drums[(size_t) (part % 2 == 1 ? part + 1 : part - 1)].choke();
        drums[(size_t) part].trigger (s.wave, velocityGain, s);
    }
    else if (isSynth (part))
        synths[(size_t) (part - kFirstSynth)].noteOn (note, velocityGain, legato, s);
}

void Rack::noteOff (int part)
{
    if (isSynth (part))
        synths[(size_t) (part - kFirstSynth)].noteOff();
}

void Rack::allNotesOff()
{
    for (auto& s : synths)
        s.noteOff();
}

void Rack::setBend (int synthPart, double semitones)
{
    if (isSynth (synthPart))
        synths[(size_t) (synthPart - kFirstSynth)].setBend (semitones);
}

bool Rack::isSounding (int part) const
{
    if (isDrum (part))
        return drums[(size_t) part].isActive();
    if (isSynth (part))
        return synths[(size_t) (part - kFirstSynth)].isActive();
    return false;
}

bool Rack::isGateOn (int part) const { return isSynth (part) && synths[(size_t) (part - kFirstSynth)].isGateOn(); }
double Rack::synthPitch (int part) const { return isSynth (part) ? synths[(size_t) (part - kFirstSynth)].getPitch() : 0.0; }

void Rack::render (float* const* out, int numOut, int offset, int n, const RackInputs& in)
{
    // (callers split long blocks: n <= maxBlock)
    n = std::min (n, maxBlock);
    std::fill (mainL.begin(), mainL.begin() + n, 0.0f);
    std::fill (mainR.begin(), mainR.begin() + n, 0.0f);
    std::fill (indL.begin(), indL.begin() + n, 0.0f);
    std::fill (indR.begin(), indR.begin() + n, 0.0f);
    std::fill (fxL.begin(), fxL.end(), 0.0f);
    std::fill (fxR.begin(), fxR.end(), 0.0f);
    const auto& sounds = *in.sounds;
    const auto& pat = *in.pattern;

    for (int p = 0; p < kNumDrumParts + kNumSynthParts; ++p)
    {
        if (! isSounding (p))
            continue;
        std::fill (partL.begin(), partL.begin() + n, 0.0f);
        std::fill (partR.begin(), partR.begin() + n, 0.0f);
        const auto& s = sounds[(size_t) p];
        if (isDrum (p))
            drums[(size_t) p].render (partL.data(), partR.data(), n, s, in.ctx);
        else
            synths[(size_t) (p - kFirstSynth)].render (partL.data(), partR.data(), n, s, in.ctx);
        if (! in.audible[(size_t) p])
            continue; // muted or not soloed: the voice runs on, silently
        const auto& part = pat.parts[(size_t) p];
        float *dl = mainL.data(), *dr = mainR.data();
        if (part.outBus34)
        {
            dl = indL.data(); // individual outs: no effect, no valve, no master volume (p.26)
            dr = indR.data();
        }
        else if (s.fxSend)
        {
            const int f = std::clamp ((int) s.fxSelect, 0, kNumFx - 1);
            dl = fxL.data() + f * maxBlock;
            dr = fxR.data() + f * maxBlock;
        }
        for (int i = 0; i < n; ++i)
        {
            dl[i] += partL[(size_t) i];
            dr[i] += partR[(size_t) i];
        }
    }

    // ---- effects, in their chain
    const auto chain = pat.chain;
    for (int f = 0; f < kNumFx; ++f)
    {
        float* l = fxL.data() + f * maxBlock;
        float* r = fxR.data() + f * maxBlock;
        fx[(size_t) f].process (l, r, n, in.fx[(size_t) f], in.ctx.bpm, in.ctx.stepSeconds);
        const bool intoNext = (f == 0 && (chain == Chain::OneTwo || chain == Chain::All)) || (f == 1 && (chain == Chain::TwoThree || chain == Chain::All));
        float* dl = intoNext ? fxL.data() + (f + 1) * maxBlock : mainL.data();
        float* dr = intoNext ? fxR.data() + (f + 1) * maxBlock : mainR.data();
        for (int i = 0; i < n; ++i)
        {
            dl[i] += l[i];
            dr[i] += r[i];
        }
    }

    // ---- audio in thru, the metronome, then the valves and the master volume
    if (in.audioThru && in.audioIn != nullptr)
        for (int i = 0; i < n; ++i)
        {
            mainL[(size_t) i] += in.audioIn[i];
            mainR[(size_t) i] += in.audioIn[i];
        }
    if (in.click != nullptr)
    {
        float* cl = in.clickToIndividual ? indL.data() : mainL.data();
        float* cr = in.clickToIndividual ? indR.data() : mainR.data();
        for (int i = 0; i < n; ++i)
        {
            cl[i] += in.click[i];
            cr[i] += in.click[i];
        }
    }
    valve.process (mainL.data(), mainR.data(), n, in.tubeGain);
    const float vol = (float) mapping::levelGain (in.masterVolume) * 0.7f;
    float pk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float l = mainL[(size_t) i] * vol, r = mainR[(size_t) i] * vol;
        pk = std::max (pk, std::max (std::abs (l), std::abs (r)));
        if (numOut > 0 && out[0] != nullptr) out[0][offset + i] = l;
        if (numOut > 1 && out[1] != nullptr) out[1][offset + i] = r;
        if (numOut > 2 && out[2] != nullptr) out[2][offset + i] = indL[(size_t) i];
        if (numOut > 3 && out[3] != nullptr) out[3][offset + i] = indR[(size_t) i];
    }
    peak = std::max (peak * 0.97f, pk);
}
} // namespace valvebox
