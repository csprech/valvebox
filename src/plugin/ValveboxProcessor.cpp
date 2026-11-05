#include "ValveboxProcessor.h"
#include "../ui/MainComponent.h"

namespace valvebox
{
namespace
{
    class Editor : public juce::AudioProcessorEditor
    {
    public:
        explicit Editor (ValveboxProcessor& p) : juce::AudioProcessorEditor (p), panel (p.getEngine())
        {
            addAndMakeVisible (panel);
            setSize (ValveboxProcessor::kPanelWidth, ValveboxProcessor::kPanelHeight);
        }
        void resized() override { panel.setBounds (getLocalBounds()); }

    private:
        ui::MainComponent panel;
    };
} // namespace

ValveboxProcessor::ValveboxProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Audio In", juce::AudioChannelSet::mono(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Individual Out", juce::AudioChannelSet::stereo(), true))
{
}

ValveboxProcessor::~ValveboxProcessor() = default;

bool ValveboxProcessor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    return layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
           && (layout.getMainInputChannelSet().isDisabled() || layout.getMainInputChannelSet() == juce::AudioChannelSet::mono());
}

void ValveboxProcessor::prepareToPlay (double sampleRate, int maxBlock)
{
    inputCopy.setSize (1, juce::jmax (1, maxBlock));
    engine.prepareHosted (sampleRate, maxBlock);
}

void ValveboxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();
    const bool haveInput = getTotalNumInputChannels() > 0 && n <= inputCopy.getNumSamples();
    if (haveInput)
        inputCopy.copyFrom (0, 0, buffer, 0, 0, n); // (the input shares channel 0 with OUTPUT L)
    float* outs[4] {};
    const int numOut = juce::jmin (4, getTotalNumOutputChannels(), buffer.getNumChannels());
    for (int c = 0; c < numOut; ++c)
        outs[c] = buffer.getWritePointer (c);
    engine.processHosted (haveInput ? inputCopy.getReadPointer (0) : nullptr, outs, numOut, n, midi);
}

juce::AudioProcessorEditor* ValveboxProcessor::createEditor() { return new Editor (*this); }

void ValveboxProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", "valvebox-state");
    engine.withMachine ([&] (Machine& m) {
        o->setProperty ("memory", memoryToJson (m.getMemory()));
        o->setProperty ("pattern", m.getPatternIndex());
        o->setProperty ("masterVolume", m.getKnobValue (Knob::MasterVolume));
        o->setProperty ("tubeGain", m.getKnobValue (Knob::TubeGain));
        o->setProperty ("audioInLevel", m.getKnobValue (Knob::AudioInLevel));
    });
    juce::MemoryOutputStream mos (dest, false);
    juce::GZIPCompressorOutputStream gz (mos, 6); // (the memory is large and very repetitive)
    gz.writeString (juce::JSON::toString (juce::var (o), true));
}

void ValveboxProcessor::setStateInformation (const void* data, int size)
{
    juce::MemoryInputStream mis (data, (size_t) size, false);
    juce::GZIPDecompressorInputStream gz (mis);
    const auto json = juce::JSON::parse (gz.readEntireStreamAsString());
    if (json.getProperty ("format", {}).toString() != "valvebox-state")
        return;
    Memory mem;
    const bool haveMemory = memoryFromJson (json["memory"], mem);
    engine.withMachine ([&] (Machine& m) {
        if (haveMemory)
            m.setMemory (mem);
        m.selectPattern ((int) json.getProperty ("pattern", 0));
        m.setKnob (Knob::MasterVolume, (int) json.getProperty ("masterVolume", 100));
        m.setKnob (Knob::TubeGain, (int) json.getProperty ("tubeGain", 0));
        m.setKnob (Knob::AudioInLevel, (int) json.getProperty ("audioInLevel", 80));
        m.takeMemoryChanged(); // (restoring isn't an edit)
    });
}
} // namespace valvebox
