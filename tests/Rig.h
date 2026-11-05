#pragma once

// A test bench for the headless instrument: press keys, turn knobs, run time forward, look at
// what came out.

#include "../src/core/Machine.h"
#include "../src/core/Mapping.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <set>

namespace vbtest
{
using namespace valvebox;
constexpr double kSr = 48000.0;

struct Rig
{
    Machine m;
    std::vector<float> L, R, I3, I4;
    std::vector<juce::MidiMessage> sent; // MIDI out, time-stamped in samples

    Rig()
    {
        m.prepare (kSr, 512);
        m.enableTriggerLog (true);
    }

    void tap (Key k)
    {
        m.press (k);
        m.release (k);
    }
    /** Hold `mod`, tap k, let go of mod. */
    void with (Key mod, Key k)
    {
        m.press (mod);
        tap (k);
        m.release (mod);
    }
    void dial (int clicks) { m.turnDial (clicks); }
    void dialWith (Key mod, int clicks)
    {
        m.press (mod);
        m.turnDial (clicks);
        m.release (mod);
    }

    /** Runs the instrument for `seconds` (optionally feeding the audio input). */
    void run (double seconds, const std::vector<float>* input = nullptr)
    {
        const int total = (int) std::lround (seconds * kSr);
        std::vector<float> l (512), r (512), a (512), b (512);
        float* outs[4] = { l.data(), r.data(), a.data(), b.data() };
        for (int done = 0; done < total; done += 512)
        {
            const int n = std::min (512, total - done);
            const float* in = nullptr;
            if (input != nullptr && (int) input->size() >= (int) (inputPos + (size_t) n))
                in = input->data() + inputPos;
            inputPos += (size_t) n;
            m.process (outs, 4, n, in);
            for (const auto meta : m.midiOut())
            {
                auto msg = meta.getMessage();
                msg.setTimeStamp ((double) (L.size() + (size_t) meta.samplePosition)); // in samples since clearAudio()
                sent.push_back (msg);
            }
            m.midiOut().clear(); // as the engine does once it has sent it
            L.insert (L.end(), l.begin(), l.begin() + n);
            R.insert (R.end(), r.begin(), r.begin() + n);
            I3.insert (I3.end(), a.begin(), a.begin() + n);
            I4.insert (I4.end(), b.begin(), b.begin() + n);
        }
    }
    void clearAudio()
    {
        sent.clear();
        L.clear();
        R.clear();
        I3.clear();
        I4.clear();
    }

    static double rms (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
    {
        double a = 0;
        size_t n = 0;
        for (size_t i = from; i < std::min (to, x.size()); ++i, ++n)
            a += (double) x[i] * x[i];
        return n > 0 ? std::sqrt (a / (double) n) : 0.0;
    }
    static bool finite (const std::vector<float>& x)
    {
        for (auto v : x)
            if (! std::isfinite (v))
                return false;
        return true;
    }

    Pattern& pat() { return m.editPattern(); }
    /** An empty pattern to work in: the default sounds, no steps, 120 BPM, 1 bar of 16ths. */
    void blank()
    {
        m.editPattern() = initPattern();
        m.clearTriggerLog();
    }
    /** Triggers logged for a part (while playing, pos >= 0). */
    std::vector<Machine::TriggerRecord> triggers (int part) const
    {
        std::vector<Machine::TriggerRecord> t;
        for (auto& r : m.getTriggerLog())
            if (r.part == part)
                t.push_back (r);
        return t;
    }
    void selectPart (int p) { tap (keyOfPart (p)); }
    void unprotect() { m.getMemory().global.protect = false; }

    size_t inputPos = 0;
};

/** Dominant frequency by zero crossings (for steady tones). */
inline double zeroCrossingHz (const std::vector<float>& x, size_t from, size_t to)
{
    int count = 0;
    size_t first = 0, last = 0;
    for (size_t i = std::max<size_t> (from, 1); i < std::min (to, x.size()); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            if (count == 0)
                first = i;
            last = i;
            ++count;
        }
    return count < 2 ? 0.0 : (count - 1) * kSr / (double) (last - first);
}
} // namespace vbtest
