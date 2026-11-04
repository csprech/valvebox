#include "core/Machine.h"
#include <juce_core/juce_core.h>

using namespace valvebox;

struct SmokeTests : juce::UnitTest
{
    SmokeTests() : juce::UnitTest ("Smoke", "valvebox") {}
    void runTest() override
    {
        beginTest ("the factory pattern plays");
        Machine m;
        m.prepare (48000.0, 512);
        m.press (Key::PlayPause);
        m.release (Key::PlayPause);
        std::vector<float> l (512), r (512), a (512), b (512);
        float* outs[4] = { l.data(), r.data(), a.data(), b.data() };
        double sum = 0;
        bool finite = true;
        for (int i = 0; i < 48000 * 2 / 512; ++i)
        {
            m.process (outs, 4, 512, nullptr);
            for (auto x : l) { sum += x * x; finite = finite && std::isfinite (x); }
        }
        expect (finite);
        expect (sum > 1.0, "sound: " + juce::String (sum));
        logMessage ("rms " + juce::String (std::sqrt (sum / (48000 * 2)), 4) + "  step " + juce::String (m.getPlayingStep()));
    }
};
static SmokeTests smokeTests;
