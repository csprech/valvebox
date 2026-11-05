#pragma once

// Audio and MIDI around the Machine.
//  * The Machine lives under one SpinLock: the audio thread holds it for each block, the UI for
//    each key press or knob move (short).
//  * MIDI from every input (plugged in any time) is applied sample-accurately inside the block.
//  * MIDI the instrument sends (keys, sequence, clock) goes to the chosen MIDI output.
//  * Memory (patterns, songs, global) is saved to disk after a WRITE; card-menu files and MIDI data
//    dumps run here, outside the lock.
//  * The audio input opens only when something needs it (AUDIO IN THRU, AUTO BPM, the AUDIO IN
//    oscillator), since macOS asks for permission.

#include "../core/Machine.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

namespace valvebox
{
class Engine : public juce::AudioIODeviceCallback, public juce::MidiInputCallback, private juce::Timer
{
public:
    /** Standalone, the engine opens the audio device and the MIDI ports itself and keeps the memory in its file.
        Hosted (inside a studio or a plug-in host), it opens nothing: the host calls prepareHosted() and
        processHosted(), and the memory travels with the host's session (it starts from the saved one). */
    explicit Engine (bool hosted = false);
    ~Engine() override;
    bool isHosted() const { return hosted; }

    /** Hosted: the sample rate and the largest block to expect. */
    void prepareHosted (double sampleRate, int maxBlockSize);
    /** Hosted: one block. `in` is the AUDIO IN jack (mono, may be null); out[0..3] get L, R and the individual
        outs 3, 4 (any may be null). `midi` brings what arrives at the MIDI IN jack and returns what the
        instrument sends from its MIDI OUT jack (notes, CCs, clock, dumps). */
    void processHosted (const float* in, float* const* out, int numOut, int numSamples, juce::MidiBuffer& midi);

    template <typename Fn>
    auto withMachine (Fn&& fn)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        return fn (machine);
    }

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    juce::StringArray getActiveMidiInputNames() const;
    juce::Array<juce::MidiDeviceInfo> getMidiOutputs() const { return juce::MidiOutput::getAvailableDevices(); }
    void setMidiOutput (const juce::String& identifier);
    juce::String getMidiOutputIdentifier() const { return midiOutId; }
    std::function<void (const juce::String&)> onMessage; // device connected / disconnected, saved, errors

    float getOutputLevel() const { return outputLevel.load(); }
    bool isInputOpen() const { return inputOpen; }
    juce::File getMemoryFile() const;

private:
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void timerCallback() override;
    void prepare (double sampleRate, int block);
    void render (const float* in, float* const* out, int numOut, int n, const juce::MidiBuffer* hostIn, juce::MidiBuffer* hostOut);
    /** A message from a MIDI input: data dumps are collected here; true when it's for the Machine. */
    bool routeMidi (const juce::MidiMessage& m);
    void midiDevicesChanged();
    void saveMemory();
    void runCard (const Machine::CardRequest& r);
    void sendDump (int what);

    const bool hosted;
    juce::SpinLock lock;
    Machine machine;
    juce::AudioDeviceManager deviceManager;
    juce::MidiMessageCollector collector;
    juce::MidiBuffer midiBlock;
    std::unique_ptr<juce::MidiOutput> midiOut;
    juce::String midiOutId;
    std::atomic<float> outputLevel { 0.0f };
    std::atomic<double> sampleRate { 48000.0 };
    bool inputOpen = false;
    juce::Array<juce::MidiDeviceInfo> knownInputs;
    juce::MidiDeviceListConnection midiDeviceListConnection;
    juce::ThreadPool io { 1 };
    std::vector<float> silence;

    // incoming data dumps (system exclusive, our own format)
    juce::CriticalSection sysexLock;
    juce::MemoryBlock sysexIn;

    // testing aid: VALVEBOX_CAPTURE_WAV=/path.wav records the first 30 s of output, written on exit
    juce::File captureFile;
    juce::AudioBuffer<float> capture;
    std::atomic<int> captured { 0 };
};
} // namespace valvebox
