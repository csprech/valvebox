#include "Engine.h"

namespace valvebox
{
namespace
{
    // data dumps: F0 7D (non-commercial ID) 'V' 'B' <7-bit packed gzip of the JSON> F7
    constexpr juce::uint8 kSysexHeader[] = { 0x7D, 'V', 'B' };

    juce::MemoryBlock pack7 (const juce::MemoryBlock& data)
    {
        juce::MemoryBlock out;
        auto* d = static_cast<const juce::uint8*> (data.getData());
        for (size_t i = 0; i < data.getSize(); i += 7)
        {
            juce::uint8 msbs = 0;
            const size_t n = std::min<size_t> (7, data.getSize() - i);
            for (size_t k = 0; k < n; ++k)
                msbs |= (juce::uint8) (((d[i + k] >> 7) & 1) << k);
            out.append (&msbs, 1);
            for (size_t k = 0; k < n; ++k)
            {
                const juce::uint8 b = d[i + k] & 0x7f;
                out.append (&b, 1);
            }
        }
        return out;
    }

    juce::MemoryBlock unpack7 (const juce::uint8* d, size_t size)
    {
        juce::MemoryBlock out;
        for (size_t i = 0; i < size; i += 8)
        {
            const juce::uint8 msbs = d[i];
            for (size_t k = 0; k < 7 && i + 1 + k < size; ++k)
            {
                const juce::uint8 b = (juce::uint8) (d[i + 1 + k] | (((msbs >> k) & 1) << 7));
                out.append (&b, 1);
            }
        }
        return out;
    }
} // namespace

Engine::Engine (bool isHosted) : hosted (isHosted)
{
    WaveLibrary::get(); // render the waves before any sound is asked for
    juce::File memFile = getMemoryFile();
    if (memFile.existsAsFile())
    {
        Memory m;
        if (memoryFromJson (juce::JSON::parse (memFile), m))
            machine.setMemory (m);
    }
    machine.setCardFolder (juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Valvebox Card"));
    machine.takeMemoryChanged(); // (loading isn't a change)
    if (hosted)
    {
        startTimerHz (10); // card jobs and dumps
        return;
    }
    if (auto* path = std::getenv ("VALVEBOX_CAPTURE_WAV"))
        captureFile = juce::File (path);

    deviceManager.initialiseWithDefaultDevices (0, 2);
    deviceManager.addAudioCallback (this);
    deviceManager.addMidiInputDeviceCallback ({}, this);
    knownInputs = juce::MidiInput::getAvailableDevices();
    for (auto& d : knownInputs)
        deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
    midiDeviceListConnection = juce::MidiDeviceListConnection::make ([this] { midiDevicesChanged(); });
    startTimerHz (10);
}

Engine::~Engine()
{
    stopTimer();
    if (hosted)
    {
        io.removeAllJobs (false, 10000);
        return; // (the session keeps a hosted instance's memory)
    }
    midiDeviceListConnection.reset();
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
    io.removeAllJobs (false, 10000);
    if (machine.takeMemoryChanged())
        saveMemory();
    if (captureFile != juce::File() && captured.load() > 0)
    {
        captureFile.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (captureFile.createOutputStream().release());
        const auto opts = juce::AudioFormatWriterOptions().withSampleRate (sampleRate.load()).withNumChannels (2).withBitsPerSample (24);
        if (os != nullptr)
            if (auto writer = wav.createWriterFor (os, opts))
                writer->writeFromAudioSampleBuffer (capture, 0, captured.load());
    }
}

juce::File Engine::getMemoryFile() const
{
    if (auto* dir = std::getenv ("VALVEBOX_MEMORY_DIR")) // testing: keep the real memory out of it
        return juce::File (dir).getChildFile ("memory.vbx");
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Valvebox").getChildFile ("memory.vbx");
}

void Engine::setMidiOutput (const juce::String& identifier)
{
    auto out = identifier.isEmpty() ? nullptr : juce::MidiOutput::openDevice (identifier);
    if (out != nullptr)
        out->startBackgroundThread();
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (midiOut, out);
        midiOutId = identifier;
    }
}

juce::StringArray Engine::getActiveMidiInputNames() const
{
    juce::StringArray names;
    for (auto& d : juce::MidiInput::getAvailableDevices())
        if (deviceManager.isMidiInputDeviceEnabled (d.identifier))
            names.add (d.name);
    return names;
}

void Engine::midiDevicesChanged()
{
    const auto now = juce::MidiInput::getAvailableDevices();
    auto contains = [] (const juce::Array<juce::MidiDeviceInfo>& l, const juce::MidiDeviceInfo& d) {
        for (auto& x : l)
            if (x.identifier == d.identifier)
                return true;
        return false;
    };
    for (auto& d : now)
        if (! contains (knownInputs, d))
        {
            deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
            if (onMessage)
                onMessage ("MIDI IN: " + d.name + " connected");
        }
    bool lost = false;
    for (auto& d : knownInputs)
        if (! contains (now, d))
        {
            lost = true;
            if (onMessage)
                onMessage ("MIDI IN: " + d.name + " disconnected");
        }
    knownInputs = now;
    if (lost)
        withMachine ([] (Machine& m) { m.midiIn (juce::MidiMessage::allNotesOff (1)); }); // notes held on it let go
}

void Engine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    if (routeMidi (m))
        collector.addMessageToQueue (m);
}

bool Engine::routeMidi (const juce::MidiMessage& m)
{
    if (m.isSysEx())
    {
        // a data dump coming in (ours): assembled here, loaded on the message thread
        const auto* d = m.getSysExData();
        if (m.getSysExDataSize() > 3 && d[0] == kSysexHeader[0] && d[1] == kSysexHeader[1] && d[2] == kSysexHeader[2])
        {
            const juce::ScopedLock sl (sysexLock);
            sysexIn.replaceAll (d + 3, (size_t) m.getSysExDataSize() - 3);
        }
        return false;
    }
    return true;
}

void Engine::audioDeviceAboutToStart (juce::AudioIODevice* device) { prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples()); }

void Engine::prepareHosted (double sr, int maxBlock) { prepare (sr, maxBlock); }

void Engine::processHosted (const float* in, float* const* out, int numOut, int n, juce::MidiBuffer& midi)
{
    juce::MidiBuffer sent;
    render (in, out, numOut, n, &midi, &sent);
    midi.swapWith (sent);
}

void Engine::prepare (double sr, int blockSize)
{
    sampleRate = sr;
    collector.reset (sr);
    const int block = std::max (512, blockSize);
    silence.assign ((size_t) block, 0.0f);
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        machine.prepare (sr, block);
    }
    if (captureFile != juce::File())
    {
        capture.setSize (2, (int) (sr * 30.0));
        capture.clear();
        captured = 0;
    }
}

void Engine::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                               const juce::AudioIODeviceCallbackContext&)
{
    render (numIn > 0 ? in[0] : nullptr, out, numOut, n, nullptr, nullptr);
}

void Engine::render (const float* input, float* const* out, int numOut, int n, const juce::MidiBuffer* hostIn, juce::MidiBuffer* hostOut)
{
    for (int c = 0; c < numOut; ++c)
        if (out[c] != nullptr)
            juce::FloatVectorOperations::clear (out[c], n);
    midiBlock.clear();
    collector.removeNextBlockOfMessages (midiBlock, n);
    if (hostIn != nullptr)
        for (const auto meta : *hostIn) // the MIDI IN jack, where it arrived in the block
            if (routeMidi (meta.getMessage()))
                midiBlock.addEvent (meta.getMessage(), juce::jlimit (0, juce::jmax (0, n - 1), meta.samplePosition));
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        // MIDI lands where it arrived in the block
        int pos = 0;
        std::array<float*, 4> o {};
        auto renderTo = [&] (int end) {
            if (end <= pos)
                return;
            for (int c = 0; c < 4; ++c)
                o[(size_t) c] = c < numOut && out[c] != nullptr ? out[c] + pos : nullptr;
            machine.process (o.data(), std::min (numOut, 4), end - pos, input != nullptr ? input + pos : nullptr);
            if (hostOut != nullptr)
                hostOut->addEvents (machine.midiOut(), 0, -1, pos); // the MIDI OUT jack, at its place in the block
            else if (midiOut != nullptr && ! machine.midiOut().isEmpty())
                midiOut->sendBlockOfMessages (machine.midiOut(), juce::Time::getMillisecondCounterHiRes() + 5.0, sampleRate.load());
            machine.midiOut().clear(); // sent (or nowhere to send it); what the panel adds next goes out with the next block
            pos = end;
        };
        for (const auto meta : midiBlock)
        {
            renderTo (juce::jlimit (0, n, meta.samplePosition));
            machine.midiIn (meta.getMessage());
        }
        renderTo (n);
    }
    float peak = 0;
    for (int c = 0; c < std::min (2, numOut); ++c)
        if (out[c] != nullptr)
        {
            juce::FloatVectorOperations::clip (out[c], out[c], -1.0f, 1.0f, n);
            peak = std::max (peak, juce::FloatVectorOperations::findMaximum (out[c], n));
        }
    // mono output devices hear the left; more than 4 outputs: the rest stay silent
    outputLevel = peak;
    if (capture.getNumSamples() > 0 && numOut >= 2 && out[0] != nullptr && out[1] != nullptr)
    {
        const int m = juce::jmin (n, capture.getNumSamples() - captured.load());
        if (m > 0)
        {
            capture.copyFrom (0, captured.load(), out[0], m);
            capture.copyFrom (1, captured.load(), out[1], m);
            captured += m;
        }
    }
}

void Engine::timerCallback()
{
    Machine::CardRequest req;
    bool changed = false, wantsInput = false;
    const bool running = hosted || deviceManager.getCurrentAudioDevice() != nullptr;
    withMachine ([&] (Machine& m) {
        req = m.takeCardRequest();
        changed = m.takeMemoryChanged();
        wantsInput = m.wantsAudioInput();
        if (! running)
            m.midiOut().clear(); // no audio callback to send it: don't let the panel's MIDI pile up
    });
    if (changed && ! hosted)
        saveMemory(); // (hosted, the memory is saved with the session)
    if (req.kind == Machine::CardRequest::DumpTx)
        sendDump (req.dumpWhat);
    else if (req.kind != Machine::CardRequest::None)
        runCard (req);

    // open the audio input the first time something needs it (hosted, it's the AUDIO IN jack)
    if (wantsInput && ! inputOpen && ! hosted)
    {
        auto setup = deviceManager.getAudioDeviceSetup();
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        setup.inputChannels.setBit (0);
        deviceManager.setAudioDeviceSetup (setup, true);
        inputOpen = true;
    }

    // a data dump that came in over MIDI
    juce::MemoryBlock dump;
    {
        const juce::ScopedLock sl (sysexLock);
        dump.swapWith (sysexIn);
    }
    if (dump.getSize() > 0)
    {
        const auto packed = unpack7 (static_cast<const juce::uint8*> (dump.getData()), dump.getSize());
        juce::MemoryInputStream mis (packed, false);
        juce::GZIPDecompressorInputStream gz (mis);
        const auto json = juce::JSON::parse (gz.readEntireStreamAsString());
        Memory m;
        bool ok = false;
        withMachine ([&] (Machine& mc) { m = mc.getMemory(); });
        if (memoryFromJson (json, m))
            ok = true;
        else if (json.getProperty ("format", {}).toString() == "valvebox-pattern")
            ok = patternFromJson (json, m.patterns[0]);
        withMachine ([&] (Machine& mc) {
            if (ok && ! mc.getMemory().global.protect)
                mc.setMemory (m);
        });
        if (onMessage)
            onMessage (ok ? "MIDI dump received" : "MIDI dump: not a Valvebox dump");
    }
}

void Engine::saveMemory()
{
    // copy under the lock, write in the background
    auto copy = std::make_shared<Memory>();
    withMachine ([&] (Machine& m) { *copy = m.getMemory(); });
    const auto file = getMemoryFile();
    io.addJob ([copy, file] {
        file.getParentDirectory().createDirectory();
        file.replaceWithText (juce::JSON::toString (memoryToJson (*copy), true));
    });
}

void Engine::runCard (const Machine::CardRequest& r)
{
    // the slow part (files) happens outside the lock; the result goes back in under it
    if (r.kind == Machine::CardRequest::SaveAll)
    {
        auto copy = std::make_shared<Memory>();
        withMachine ([&] (Machine& m) { *copy = m.getMemory(); });
        r.file.getParentDirectory().createDirectory();
        const bool ok = r.file.replaceWithText (juce::JSON::toString (memoryToJson (*copy), true));
        withMachine ([&] (Machine& m) { m.cardFinished (r, ok, nullptr, nullptr); });
        return;
    }
    if (r.kind == Machine::CardRequest::LoadAll || r.kind == Machine::CardRequest::LoadPattern)
    {
        auto m = std::make_shared<Memory>();
        const bool ok = r.file.existsAsFile() && memoryFromJson (juce::JSON::parse (r.file), *m);
        withMachine ([&] (Machine& mc) {
            mc.cardFinished (r, ok, ok ? m.get() : nullptr, ok ? &m->patterns[(size_t) juce::jlimit (0, kNumPatterns - 1, r.pattern)] : nullptr);
        });
        return;
    }
    // delete / format are quick
    withMachine ([&] (Machine& m) { m.runCardRequest (r); });
}

void Engine::sendDump (int what)
{
    if (midiOut == nullptr && ! hosted)
    {
        if (onMessage)
            onMessage ("MIDI dump: choose a MIDI output in Audio/MIDI first");
        return;
    }
    juce::var json;
    withMachine ([&] (Machine& m) {
        const auto& mem = m.getMemory();
        if (what <= 3)
        {
            Memory bank = mem; // a pattern bank: those 64 patterns (and the rest as they are)
            json = memoryToJson (bank);
        }
        else
            json = memoryToJson (mem);
    });
    juce::MemoryOutputStream mos;
    {
        juce::GZIPCompressorOutputStream gz (mos, 9);
        gz.writeString (juce::JSON::toString (json));
    }
    auto packed = pack7 (mos.getMemoryBlock());
    juce::MemoryBlock msg (kSysexHeader, sizeof (kSysexHeader));
    msg.append (packed.getData(), packed.getSize());
    const auto sysex = juce::MidiMessage::createSysExMessage (msg.getData(), (int) msg.getSize());
    if (hosted)
        withMachine ([&] (Machine& m) { m.midiOut().addEvent (sysex, 0); }); // out of the MIDI OUT jack with the next block
    else
        midiOut->sendMessageNow (sysex);
    if (onMessage)
        onMessage ("MIDI dump sent (" + juce::String ((int) (msg.getSize() / 1024)) + " KB)");
}
} // namespace valvebox
