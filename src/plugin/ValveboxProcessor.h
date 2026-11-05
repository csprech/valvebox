#pragma once

// Valvebox as an audio processor, for hosts (the Tabletop studio, or a plug-in host): the same engine and panel
// as the app, without an audio device of its own. Its jacks become the processor's buses:
//   input  0          AUDIO IN (mono)
//   output 0, 1       OUTPUT L, R (through the valve stage and MASTER VOLUME)
//   output 2, 3       INDIVIDUAL OUT 3, 4
//   MIDI in / out     MIDI IN / OUT jacks
// Each instance keeps its own memory: it starts from the app's saved memory and is saved with the session.

#include "../engine/Engine.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace valvebox
{
class ValveboxProcessor : public juce::AudioProcessor
{
public:
    ValveboxProcessor();
    ~ValveboxProcessor() override;

    /** The panel's natural size (it scales to any size, keeping these proportions). */
    static constexpr int kPanelWidth = 1500, kPanelHeight = 1090;

    Engine& getEngine() { return engine; }

    const juce::String getName() const override { return "Valvebox"; }
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    /** The whole memory (patterns, songs, global), the pattern selected and the three common knobs (JSON). */
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    Engine engine { true };
    juce::AudioBuffer<float> inputCopy;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ValveboxProcessor)
};
} // namespace valvebox
