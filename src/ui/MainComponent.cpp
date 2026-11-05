#include "MainComponent.h"
#include "Materials.h"
#include "../core/Mapping.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace valvebox::ui
{
namespace
{
    // the computer keyboard: step keys as a piano (white keys on the home row), part keys on the numbers
    const char* const kStepChars = "awsdrftghujikol;";
    const char* const kPartChars = "1234567890zxcvbn";
    constexpr Key kPartOrder[16] = { Key::Drum1, Key::Drum2, Key::Drum3, Key::Drum4, Key::Drum5, Key::Drum6A, Key::Drum6B, Key::Drum7A, Key::Drum7B,
                                     Key::DrumAccent, Key::Synth1, Key::Synth2, Key::Synth3, Key::Synth4, Key::Synth5, Key::SynthAccent };
    struct HoldChar { char c; Key k; };
    constexpr HoldChar kHoldChars[] = { { 'q', Key::PartMute }, { 'e', Key::Solo }, { 'p', Key::PatternSet }, { 'y', Key::Tap } };

    const char* const kModeNames[6] = { "PATTERN", "PART EDIT", "STEP EDIT", "SONG", "GLOBAL", "MIDI" };

    const juce::Rectangle<int> kMatrixArea { 158, 472, 526, 92 };
    const juce::Rectangle<int> kMessageArea { 930, 44, 340, 18 };

    bool isBlackStep (int i)
    {
        // the step keys read as a keyboard from A: A, A#, B, C, C#, D, D#, E, F, F#, G, G#, A, A#, B, C
        static const bool b[16] = { false, true, false, false, true, false, true, false, false, true, false, true, false, true, false, false };
        return b[i];
    }
} // namespace

MainComponent::MainComponent (Engine& e) : engine (e)
{
    setWantsKeyboardFocus (true);
    addAndMakeVisible (canvas);
    canvas.painter = [this] (juce::Graphics& g) { paintPanel (g); };
    build();
    layout();
    for (auto* b : { &audioBtn, &helpBtn })
    {
        b->setLookAndFeel (&buttonLook);
        canvas.addAndMakeVisible (b);
        b->setWantsKeyboardFocus (false);
    }
    audioBtn.onClick = [this] { showAudioSettings(); };
    helpBtn.onClick = [this] { showHelp(); };
    engine.onMessage = [this] (const juce::String& m) {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), m] {
            if (safe != nullptr)
            {
                safe->engineMessage = m;
                safe->engineMessageUntil = juce::Time::getMillisecondCounter() + 3000;
                safe->repaint();
            }
        });
    };
    // clicks anywhere give the keys back to the panel (the children never take keyboard focus)
    std::function<void (juce::Component&)> noFocus = [&noFocus] (juce::Component& c) {
        for (auto* child : c.getChildren())
        {
            child->setWantsKeyboardFocus (false);
            child->setMouseClickGrabsKeyboardFocus (false);
            noFocus (*child);
        }
    };
    noFocus (*this);
    addMouseListener (this, true);
    if (engine.isHosted())
        audioBtn.setVisible (false); // the studio's audio interface does this job (and saves the panel with the session)
    else
        loadSettings();
    {
        // as big as the screen allows, up to the panel's own size
        const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        const auto area = display != nullptr ? display->userBounds.toNearestInt() : juce::Rectangle<int> (0, 0, 1440, 900);
        const int h = std::min (kHeight, area.getHeight() - 60);
        setSize (h * kWidth / kHeight, h);
    }
    if (auto* forced = std::getenv ("VALVEBOX_WINDOW_SIZE"))
    {
        const auto wh = juce::StringArray::fromTokens (forced, "x", {});
        if (wh.size() == 2)
            setSize (wh[0].getIntValue(), wh[1].getIntValue());
    }
    startTimerHz (30);
    if (auto* snap = std::getenv ("VALVEBOX_SNAPSHOT")) // testing aid: render the panel at 2x to a PNG and quit
    {
        const juce::File f (snap);
        const bool play = std::getenv ("VALVEBOX_SNAPSHOT_PLAY") != nullptr;
        if (play)
            engine.withMachine ([] (Machine& m) {
                m.selectPattern (1);
                m.setKnob (Knob::TubeGain, 90);
                m.press (Key::PlayPause);
                m.release (Key::PlayPause);
            });
        juce::Timer::callAfterDelay (play ? 1700 : 900, [safe = juce::Component::SafePointer<MainComponent> (this), f] {
            if (safe == nullptr)
                return;
            const auto img = safe->canvas.createComponentSnapshot (safe->canvas.getLocalBounds(), true, 2.0f); // the panel at 2x, whatever the window
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
            os.flush();
            juce::JUCEApplicationBase::quit();
        });
    }
    if (auto* report = std::getenv ("VALVEBOX_SELFTEST"))
    {
        const juce::File f (report);
        juce::Timer::callAfterDelay (1500, [safe = juce::Component::SafePointer<MainComponent> (this), f] {
            if (safe != nullptr)
                safe->runSelfTest (f);
        });
    }
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine.onMessage = nullptr;
    audioBtn.setLookAndFeel (nullptr);
    helpBtn.setLookAndFeel (nullptr);
}

void MainComponent::prepareToQuit() { saveSettings(); }

void MainComponent::mouseDown (const juce::MouseEvent&)
{
    if (! hasKeyboardFocus (false))
        grabKeyboardFocus();
}

// ---------------------------------------------------------------- building the panel

void MainComponent::build()
{
    using S = KeyWidget::Style;
    auto key = [this] (Key k, const juce::String& label, const juce::String& shiftLabel = {}, S st = S::Pill, bool led = true) {
        auto w = std::make_unique<KeyWidget> (k, label, shiftLabel, st, led);
        w->onPress = [this] (Key kk) { pressKey (kk); };
        w->onRelease = [this] (Key kk) { releaseKey (kk); };
        canvas.addAndMakeVisible (*w);
        keyFor[(size_t) k] = w.get();
        keys.push_back (std::move (w));
    };
    // the transport strip
    key (Key::ResetErase, "RESET", "ERASE", S::Transport, false);
    key (Key::Rec, "REC", {}, S::Transport);
    key (Key::StopCancel, "STOP", "CANCEL", S::Transport, false);
    key (Key::PlayPause, "PLAY/PAUSE", {}, S::Transport);
    key (Key::Tap, "TAP", {}, S::Square, false);
    key (Key::PartMute, "PART MUTE", "REST/TIE", S::Square);
    key (Key::Solo, "SOLO", "TRANSPOSE", S::Square);
    // the display panel
    key (Key::AudioInThru, "AUDIO IN THRU");
    key (Key::TempoLock, "TEMPO LOCK");
    key (Key::AutoBpm, "AUTO BPM SCAN");
    key (Key::WriteRename, "WRITE", "RENAME");
    for (auto k : { Key::ResetErase, Key::Rec, Key::StopCancel, Key::PlayPause, Key::Tap, Key::PartMute, Key::Solo, Key::AudioInThru, Key::TempoLock,
                    Key::AutoBpm, Key::WriteRename })
        keyFor[(size_t) k]->onCream = true;
    key (Key::CursorLeft, "^", {}, S::Pill, false);
    key (Key::CursorRight, "v", {}, S::Pill, false);
    const Key modeKeys[6] = { Key::ModePattern, Key::ModePartEdit, Key::ModeStepEdit, Key::ModeSong, Key::ModeGlobal, Key::ModeMidi };
    for (int i = 0; i < 6; ++i)
        key (modeKeys[i], kModeNames[i], i == 0 ? "COMPARE" : "");
    // edit section
    key (Key::FxChain, "FX CHAIN", {}, S::Pill, false);
    key (Key::FxEditSelect, "EDIT SELECT", {}, S::Pill, false);
    key (Key::FxMotion, "MOTION SEQ");
    key (Key::PartMotion, "MOTION SEQ", {}, S::Pill, false);
    key (Key::AmpEg, "AMP EG", {}, S::Pill, false);
    key (Key::Roll, "ROLL");
    key (Key::FxSelect, "FX SELECT", {}, S::Pill, false);
    key (Key::FxSend, "FX SEND");
    key (Key::ModType, "TYPE", {}, S::Pill, false);
    key (Key::ModDest, "DEST", {}, S::Pill, false);
    key (Key::ModBpmSync, "BPM SYNC");
    key (Key::FilterType, "TYPE", {}, S::Pill, false);
    // parts
    const char* drumNames[9] = { "1", "2", "3", "4", "5", "6A", "6B", "7A", "7B" };
    for (int i = 0; i < 9; ++i)
        key ((Key) ((int) Key::Drum1 + i), drumNames[i], {}, S::Square);
    key (Key::DrumAccent, "ACCENT", {}, S::Pill);
    for (int i = 0; i < 5; ++i)
        key ((Key) ((int) Key::Synth1 + i), juce::String (i + 1), {}, S::Square);
    key (Key::SynthAccent, "ACCENT", {}, S::Pill);
    key (Key::Keyboard, "KEYBOARD", {}, S::DarkPill);
    key (Key::SelectLeft, "<", {}, S::Pill, false);
    key (Key::SelectRight, ">", {}, S::Pill, false);
    key (Key::Shift, "SHIFT", {}, S::Round, false);
    key (Key::PatternSet, "PATTERN SET", {}, S::Pill, false);
    const char* shiftFns[16] = { "LAST STEP", "MOVE DATA", "SHIFT NOTE", "COPY PART", "COPY SOUND", "CLEAR MOTION", "CLEAR PART", "CLEAR PATTERN",
                                 "INSERT PATTERN", "DELETE PATTERN", "CLEAR SONG", "MIDI UTILITY", "CARD", " ", " ", " " };
    for (int i = 0; i < 16; ++i)
        key ((Key) ((int) Key::Step1 + i), juce::String (i + 1), shiftFns[i], isBlackStep (i) ? S::StepSharp : S::Step);
    for (auto k : { Key::Shift, Key::PartMute, Key::Solo, Key::PatternSet })
        keyFor[(size_t) k]->latching = true;

    // knobs
    auto knob = [this] (Knob k, const juce::String& label, bool bipolar = false, bool boxed = false) {
        auto w = std::make_unique<KnobWidget> (label, bipolar);
        w->boxedLabel = boxed;
        w->onChange = [this, k] (int v) { engine.withMachine ([&] (Machine& m) { m.setKnob (k, v); }); };
        canvas.addAndMakeVisible (*w);
        knobFor[(size_t) k] = w.get();
        knobs.push_back (std::move (w));
    };
    auto selector = [this] (Knob k, juce::StringArray choices) {
        auto w = std::make_unique<SelectorWidget> (juce::String(), std::move (choices));
        w->onChange = [this, k] (int v) { engine.withMachine ([&] (Machine& m) { m.setKnob (k, v); }); };
        canvas.addAndMakeVisible (*w);
        knobFor[(size_t) k] = w.get();
        knobs.push_back (std::move (w));
    };
    knob (Knob::MasterVolume, "MASTER VOLUME");
    knob (Knob::TubeGain, "TUBE GAIN");
    knob (Knob::AudioInLevel, "AUDIO IN LEVEL");
    selector (Knob::FxType, { "REVERB", "BPM DELAY", "SHORT DELAY", "MOD DELAY", "GRAIN SHIFT", "CHO/FLG", "PHASER", "RING MOD", "TALK MOD", "PITCH SHIFT",
                              "COMPRESSOR", "DISTORTION", "DECIMATOR", "EQ", "LPF", "HPF" });
    knob (Knob::FxEdit1, "FX EDIT 1");
    knob (Knob::FxEdit2, "FX EDIT 2");
    knob (Knob::PitchGlide, "PITCH / GLIDE", true);
    knob (Knob::EgTime, "EG TIME");
    knob (Knob::Pan, "PAN", true);
    knob (Knob::Level, "LEVEL");
    knob (Knob::ModSpeed, "SPEED");
    knob (Knob::ModDepth, "DEPTH", true);
    selector (Knob::OscType, { "WAVEFORM", "DUAL OSC", "CHORD", "UNISON", "RING MOD", "OSC SYNC", "CROSS MOD", "VPM", "WAVESHAPE", "ADDITIVE", "COMB",
                               "FORMANT", "NOISE", "PCM+COMB", "PCM+WS", "AUDIO IN" });
    knob (Knob::OscEdit1, "OSC EDIT 1", false, true);
    knob (Knob::OscEdit2, "OSC EDIT 2", false, true);
    knob (Knob::Cutoff, "CUTOFF", false, true);
    knob (Knob::EgInt, "EG INT", true, true);
    knob (Knob::Resonance, "RESONANCE", false, true);
    knob (Knob::Drive, "DRIVE", false, true);

    // lamps
    auto lamp = [this] (Lamp l, int index, const juce::String& label, juce::Colour c = colours::ledRed, bool above = false) {
        LampRef ref { std::make_unique<LampWidget> (label, c, above), l, index };
        canvas.addAndMakeVisible (*ref.w);
        lamps.push_back (std::move (ref));
    };
    lamp (Lamp::Beat, 0, "BEAT", colours::ledGreen, true);
    lamps.back().w->onCream = true;
    for (int i = 0; i < 8; ++i)
        lamp (Lamp::SelectGreen, i, {}, colours::ledGreen);
    for (int i = 0; i < 8; ++i)
        lamp (Lamp::SelectRed, i, {}, colours::ledRed);
    lamp (Lamp::ChainOneTwo, 0, {});
    lamp (Lamp::ChainTwoThree, 0, {});
    for (int i = 0; i < 3; ++i)
        lamp (Lamp::EditSelect, i, "FX " + juce::String (i + 1));
    lamp (Lamp::MotionSmooth, 0, "SMOOTH");
    lamp (Lamp::MotionTrigHold, 0, "TRIG HOLD");
    lamp (Lamp::AmpDecay, 0, {});
    lamp (Lamp::AmpGate, 0, {});
    for (int i = 0; i < 3; ++i)
        lamp (Lamp::FxSelectLamp, i, "FX " + juce::String (i + 1));
    for (int i = 0; i < 5; ++i)
        lamp (Lamp::ModTypeLamp, i, {}); // (their wave shapes are printed beside them)
    const char* destNames[6] = { "PITCH", "AMP", "PAN", "OSC EDIT 1", "OSC EDIT 2", "CUTOFF" };
    for (int i = 0; i < 6; ++i)
        lamp (Lamp::ModDestLamp, i, destNames[i]);
    for (int i = 0; i < 4; ++i)
        lamp (Lamp::FilterTypeLamp, i, mapping::filterName ((FilterType) i));

    dial.onTurn = [this] (int c) { engine.withMachine ([&] (Machine& m) { m.turnDial (c); }); };
    ribbonW.onTouch = [this] (bool on, float p) { engine.withMachine ([&] (Machine& m) { m.ribbon (on, p); }); };
    sliderW.onMove = [this] (float v) { engine.withMachine ([&] (Machine& m) { m.slider (v); }); };
    for (auto* c : std::initializer_list<juce::Component*> { &dial, &lcd, &ribbonW, &sliderW, &valves })
        canvas.addAndMakeVisible (c);
}

// The layout follows the original's panel, measured off a photograph and scaled 2.7x (1 mm on the instrument is
// about 4.2 px here). The part keys and the step keys share one 16-column grid.
namespace
{
    constexpr float kGridX = 366.0f, kGridPitch = 66.0f, kGridKey = 51.0f;
    float column (int i) { return kGridX + kGridPitch * (float) i; }
    // the matrix menu: a column per mode, over its mode key
    const float kMatrixCols[7] = { 162, 243, 332, 419, 508, 597, 680 };
    constexpr float kMatrixTop = 474.0f, kMatrixRow = 12.4f;
    const char* const kMatrixPanel[6][7] = { { "PATTERN", "TEMPO", "SWING", "LENGTH", "BEAT", "ROLL TYPE", "ARP SCALE" },
                                             { "WAVE", "ACCENT SW", "SWING SW", "OUTPUT BUS", "SYNTH TUNE", nullptr, nullptr },
                                             { "STEP REC", "NOTE NO.", "GATE TIME", "MOTION DEST", "MOTION VALUE", nullptr, nullptr },
                                             { "SONG", "TEMPO", "POSITION", "PATTERN", "NOTE OFFSET", "MUTE HOLD", "NEXT SONG" },
                                             { "METRONOME", "MASTER TUNE", "PROTECT", "ARP CONTROL", nullptr, nullptr, nullptr },
                                             { "MIDI CH", "CLOCK", "BEND RANGE", nullptr, nullptr, nullptr, nullptr } };
    const juce::Point<float> kValveCentre { 756.0f, 167.0f };
} // namespace

void MainComponent::layout()
{
    canvas.setBounds (0, 0, kWidth, kHeight);
    auto k = [this] (Key key) { return keyFor[(size_t) key]; };
    auto face = [&] (Key key, float x, float y, float w, float h) { k (key)->setBounds (k (key)->boundsFor ({ x, y, w, h })); };
    auto knobAt = [this] (Knob knob, float cx, float cy, float d, float width = 0.0f) {
        auto* w = knobFor[(size_t) knob];
        w->diameter = d;
        w->setBounds (KnobWidget::boundsFor (cx, cy, d, width));
    };
    auto selectorAt = [this] (Knob knob, float cx, float cy, float d, float w, float h) {
        auto* sw = knobFor[(size_t) knob];
        sw->diameter = d;
        sw->setBounds (juce::Rectangle<float> (w, h).withCentre ({ cx, cy }).getSmallestIntegerContainer());
    };
    auto lampAt = [this] (Lamp l, int index, float x, float y, float labelW) {
        for (auto& r : lamps)
            if (r.lamp == l && r.index == index)
                r.w->setBounds (LampWidget::boundsFor (x, y, labelW));
    };

    // ---- top left: the three knobs
    knobAt (Knob::MasterVolume, 132, 224, 50);
    knobAt (Knob::TubeGain, 243, 224, 50);
    knobAt (Knob::AudioInLevel, 410, 224, 50);
    valves.setBounds (614, 62, 284, 212);

    // ---- the display panel
    lcd.setBounds (134, 306, 264, 124);
    face (Key::AudioInThru, 424, 296, 78, 22);
    face (Key::TempoLock, 424, 337, 78, 22);
    face (Key::AutoBpm, 424, 378, 78, 22);
    face (Key::WriteRename, 424, 432, 78, 22);
    for (auto& r : lamps)
        if (r.lamp == Lamp::Beat)
            r.w->setBounds (508, 318, 40, 30);
    dial.setBounds (548, 342, 106, 106);

    // ---- the cursor keys, the matrix menu and the mode keys under its columns
    face (Key::CursorLeft, 96, 484, 48, 26);
    face (Key::CursorRight, 96, 532, 48, 26);
    for (int i = 0; i < 6; ++i)
    {
        const float cx = (kMatrixCols[i] + kMatrixCols[i + 1]) * 0.5f;
        face ((Key) ((int) Key::ModePattern + i), cx - 36, 584, 72, 22);
    }

    // ---- the transport strip
    face (Key::ResetErase, 90, 638, 62, 54);
    face (Key::Rec, 180, 638, 62, 54);
    face (Key::StopCancel, 270, 638, 62, 54);
    face (Key::PlayPause, 360, 638, 118, 54);
    face (Key::Tap, 522, 636, 70, 58);
    face (Key::PartMute, 620, 636, 44, 58);
    face (Key::Solo, 688, 636, 44, 58);

    // ---- EFFECT
    selectorAt (Knob::FxType, 861, 382, 72, 214, 130);
    knobAt (Knob::FxEdit1, 806, 500, 46);
    knobAt (Knob::FxEdit2, 918, 500, 46);
    for (int i = 0; i < 3; ++i)
        lampAt (Lamp::EditSelect, i, 904, 556 + i * 21.0f, 30);
    lampAt (Lamp::ChainOneTwo, 0, 852, 578, 0);
    lampAt (Lamp::ChainTwoThree, 0, 852, 606, 0);
    face (Key::FxEditSelect, 883, 634, 70, 22);
    face (Key::FxChain, 771, 676, 70, 22);
    face (Key::FxMotion, 883, 676, 70, 22);

    // ---- PART COMMON
    knobAt (Knob::PitchGlide, 1028, 205, 48);
    knobAt (Knob::Pan, 1146, 205, 48);
    knobAt (Knob::EgTime, 1028, 296, 48);
    knobAt (Knob::Level, 1146, 296, 48);
    lampAt (Lamp::AmpDecay, 0, 984, 336, 0);
    lampAt (Lamp::AmpGate, 0, 984, 356, 0);
    for (int i = 0; i < 3; ++i)
        lampAt (Lamp::FxSelectLamp, i, 1082, 280 + i * 21.0f, 26);
    face (Key::AmpEg, 990, 376, 70, 22);
    face (Key::FxSelect, 1076, 376, 70, 22);
    face (Key::Roll, 990, 414, 70, 22);
    face (Key::FxSend, 1076, 414, 70, 22);
    lampAt (Lamp::MotionSmooth, 0, 1172, 362, 60);
    lampAt (Lamp::MotionTrigHold, 0, 1172, 383, 60);
    face (Key::PartMotion, 1166, 414, 72, 22);

    // ---- MODULATION
    knobAt (Knob::ModSpeed, 1262, 205, 48);
    knobAt (Knob::ModDepth, 1380, 205, 48);
    for (int i = 0; i < 5; ++i)
        lampAt (Lamp::ModTypeLamp, i, 1262, 260 + i * 21.0f, 0);
    // the destinations in the original's order: PITCH, OSC EDIT 1, OSC EDIT 2, CUTOFF, AMP, PAN
    const int destRow[6] = { 0, 4, 5, 1, 2, 3 }; // (by ModDest: Pitch, Amp, Pan, OscEdit1, OscEdit2, Cutoff)
    for (int i = 0; i < 6; ++i)
        lampAt (Lamp::ModDestLamp, i, 1352, 254 + destRow[i] * 19.0f, 66);
    face (Key::ModType, 1262, 376, 70, 22);
    face (Key::ModBpmSync, 1262, 414, 70, 22);
    face (Key::ModDest, 1346, 414, 70, 22);

    // ---- SYNTH OSCILLATOR
    selectorAt (Knob::OscType, 1087, 552, 74, 222, 136);
    knobAt (Knob::OscEdit1, 1028, 672, 44);
    knobAt (Knob::OscEdit2, 1146, 672, 44);

    // ---- SYNTH FILTER
    knobAt (Knob::Cutoff, 1262, 538, 46);
    knobAt (Knob::Resonance, 1380, 538, 46);
    knobAt (Knob::EgInt, 1262, 632, 46);
    knobAt (Knob::Drive, 1380, 632, 46);
    face (Key::FilterType, 1226, 678, 72, 22);
    lampAt (Lamp::FilterTypeLamp, 0, 1322, 680, 34);
    lampAt (Lamp::FilterTypeLamp, 1, 1378, 680, 34);
    lampAt (Lamp::FilterTypeLamp, 2, 1322, 700, 34);
    lampAt (Lamp::FilterTypeLamp, 3, 1378, 700, 34);

    // ---- the parts, on the 16-column grid
    for (int i = 0; i < 9; ++i)
        face ((Key) ((int) Key::Drum1 + i), column (i), 746, kGridKey, 80);
    face (Key::DrumAccent, column (9) - 2, 796, kGridKey + 4, 30);
    for (int i = 0; i < 5; ++i)
        face ((Key) ((int) Key::Synth1 + i), column (10 + i), 746, kGridKey, 80);
    face (Key::SynthAccent, column (15) - 2, 796, kGridKey + 4, 30);

    // ---- SHIFT, PATTERN SET, KEYBOARD, the select keys and LEDs
    face (Key::Shift, column (0) + 2, 856, 46, 46);
    face (Key::PatternSet, 443, 866, 70, 24);
    face (Key::Keyboard, 535, 866, 70, 24);
    face (Key::SelectLeft, 638, 864, 52, 28);
    face (Key::SelectRight, 706, 864, 52, 28);
    for (int i = 0; i < 8; ++i)
    {
        lampAt (Lamp::SelectGreen, i, 794 + i * 41.0f, 872, 0);
        lampAt (Lamp::SelectRed, i, 794 + i * 41.0f, 896, 0);
    }

    // ---- the step keys
    for (int i = 0; i < 16; ++i)
        face ((Key) ((int) Key::Step1 + i), column (i), 928, kGridKey, 58);

    // ---- the arpeggiator
    ribbonW.setBounds (108, 756, 90, 252);
    sliderW.setBounds (224, 752, 76, 262);

    // ---- the software buttons, in the top band
    audioBtn.setBounds (1282, 16, 104, 24);
    helpBtn.setBounds (1394, 16, 62, 24);
}

// ---------------------------------------------------------------- painting

void MainComponent::paint (juce::Graphics& g) { g.fillAll (juce::Colour (0xff0d0e10)); }

void MainComponent::resized()
{
    // the panel keeps its proportions, centred in the window
    const float sc = std::min (getWidth() / (float) kWidth, getHeight() / (float) kHeight);
    const float ox = (getWidth() - kWidth * sc) * 0.5f, oy = (getHeight() - kHeight * sc) * 0.5f;
    canvas.setTransform (juce::AffineTransform::scale (sc).translated (ox, oy));
    background = {};
}

void MainComponent::paintPanel (juce::Graphics& g)
{
    const float sc = materials::scaleOf (g);
    if (! background.isValid() || background.getWidth() != (int) std::ceil (kWidth * sc))
    {
        background = juce::Image (juce::Image::ARGB, (int) std::ceil (kWidth * sc), (int) std::ceil (kHeight * sc), true);
        juce::Graphics bg (background);
        bg.addTransform (juce::AffineTransform::scale (sc));
        paintBackground (bg, sc);
    }
    g.drawImageTransformed (background, juce::AffineTransform::scale (1.0f / sc));

    // the matrix menu: a column of pages per mode, the first in bold, the current page lit
    for (int c = 0; c < 6; ++c)
    {
        const float x0 = kMatrixCols[c], x1 = kMatrixCols[c + 1];
        g.setColour (colours::print);
        g.fillRect (x0, kMatrixTop + 1.0f, 2.0f, kMatrixRow * 7.0f - 2.0f);
        for (int r = 0; r < 7 && kMatrixPanel[c][r] != nullptr; ++r)
        {
            auto cell = juce::Rectangle<float> (x0 + 5.0f, kMatrixTop + r * kMatrixRow, x1 - x0 - 8.0f, kMatrixRow);
            if (c == currentMode && r == currentPage)
            {
                const float w = juce::GlyphArrangement::getStringWidth (panelFont (9.5f, true), kMatrixPanel[c][r]) + 8.0f;
                printTag (g, kMatrixPanel[c][r], cell.withWidth (std::min (w, cell.getWidth())).withTrimmedLeft (-3.0f).reduced (0.0f, 0.5f), 9.5f);
            }
            else
                printInk (g, kMatrixPanel[c][r], cell, 9.5f, colours::print.withAlpha (r == 0 ? 1.0f : 0.86f), juce::Justification::centredLeft, r == 0);
        }
    }
    // a line for messages from the engine (MIDI devices, dumps), in the top band
    if (engineMessage.isNotEmpty() && juce::Time::getMillisecondCounter() < engineMessageUntil)
        print (g, engineMessage, kMessageArea.toFloat(), 11.0f, juce::Justification::centredLeft, false, true);
}

void MainComponent::paintBackground (juce::Graphics& bg, float sc)
{
    const auto ink = colours::print;
    // the end cheeks and the metallic blue panel
    bg.setColour (juce::Colour (0xff141518));
    bg.fillRect (0, 0, kWidth, kHeight);
    const auto pa = juce::Rectangle<float> (8, 8, kWidth - 16, kHeight - 16);
    {
        const auto& tile = materials::cached ("vbflake", [] { return materials::flakeTile (colours::panel, 252, 11); });
        bg.setFillType (juce::FillType (tile, juce::AffineTransform::scale (1.0f / sc)));
        bg.fillRoundedRectangle (pa, 14.0f);
    }
    {
        // the light: brighter towards the top-left, deeper towards the bottom-right
        juce::Graphics::ScopedSaveState save (bg);
        juce::Path panelShape;
        panelShape.addRoundedRectangle (pa, 14.0f);
        bg.reduceClipRegion (panelShape);
        bg.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.15f), pa.getX() + pa.getWidth() * 0.3f, pa.getY(), juce::Colours::transparentWhite,
                                                  pa.getX() + pa.getWidth() * 0.3f, pa.getY() + pa.getHeight() * 0.85f, true));
        bg.fillRect (pa);
        bg.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, pa.getCentreX(), pa.getCentreY(), juce::Colours::black.withAlpha (0.26f), pa.getRight(),
                                                  pa.getBottom(), false));
        bg.fillRect (pa);
    }
    bg.setColour (juce::Colours::white.withAlpha (0.22f));
    bg.drawRoundedRectangle (pa.reduced (1.0f), 13.0f, 1.0f);
    bg.setColour (juce::Colours::black.withAlpha (0.55f));
    bg.drawRoundedRectangle (pa, 14.0f, 1.5f);
    auto screw = [&] (float x, float y) {
        const int px = (int) std::ceil (12 * sc);
        materials::drawAt (bg, materials::cached ("vbscrew12:" + juce::String (px), [=] { return materials::screw (px, 0.4f); }),
                           juce::Rectangle<float> (12, 12).withCentre ({ x, y }));
    };
    for (auto pt : { juce::Point<float> (26, 26), juce::Point<float> (kWidth - 26.0f, 26), juce::Point<float> (26, kHeight - 26.0f),
                     juce::Point<float> (kWidth - 26.0f, kHeight - 26.0f), juce::Point<float> (598, 84), juce::Point<float> (930, 84),
                     juce::Point<float> (26, 556), juce::Point<float> (kWidth - 26.0f, 556), juce::Point<float> (751, 1044) })
        screw (pt.x, pt.y);

    // the connections, printed along the top edge
    auto small = [&] (const juce::String& t, float x, float w) { printInk (bg, t, { x, 22, w, 14 }, 9.0f, ink.withAlpha (0.85f), juce::Justification::centred); };
    small ("PHONES", 96, 60);
    small ("OUTPUT  L/MONO  -  R", 170, 130);
    small ("INDIVIDUAL OUT  3  -  4", 320, 140);
    small ("AUDIO IN    MIC | LINE", 470, 130);
    small ("MIDI   IN  -  OUT  -  THRU", 940, 160);
    small ("CARD", 1120, 50);
    small ("POWER", 1190, 60);

    // the name
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (keyFont (40.0f), "valvebox", 0, 0);
        juce::Path word;
        ga.createPath (word);
        word.applyTransform (juce::AffineTransform::translation (86, 152));
        bg.setColour (juce::Colours::black.withAlpha (0.25f));
        bg.fillPath (word, juce::AffineTransform::translation (0.0f, 1.5f));
        bg.setColour (ink);
        bg.fillPath (word);
        const float wordW = word.getBounds().getWidth();
        auto tag = juce::Rectangle<float> (86 + wordW + 16, 124, 62, 24);
        bg.drawRoundedRectangle (tag, 3.0f, 1.6f);
        printInk (bg, "VB-16", tag, 15.0f, ink);
        printInk (bg, "GROOVE PRODUCTION STATION", { tag.getRight() + 10, 124, 220, 24 }, 12.0f, ink, juce::Justification::centredLeft);
    }
    // over the valve window
    printInk (bg, "VALVE DRIVE", { kValveCentre.x - 80, 40, 160, 18 }, 14.0f, ink);

    // the silk-screen frame: round the whole panel, dipping under the valve window
    {
        const float L = 46, R = 1454, T = 100, B = 1044, rr = 12;
        const float rx = 156, ry = 118, cosA = (kValveCentre.y - T) / ry;
        const float a = std::acos (juce::jlimit (-1.0f, 1.0f, cosA));
        const float xl = kValveCentre.x - rx * std::sin (a), xr = kValveCentre.x + rx * std::sin (a);
        juce::Path f;
        f.startNewSubPath (L, T + rr);
        f.quadraticTo (L, T, L + rr, T);
        f.lineTo (xl, T);
        f.addCentredArc (kValveCentre.x, kValveCentre.y, rx, ry, 0.0f, juce::MathConstants<float>::twoPi - a, a, false);
        f.lineTo (R - rr, T);
        f.quadraticTo (R, T, R, T + rr);
        f.lineTo (R, B - rr);
        f.quadraticTo (R, B, R - rr, B);
        f.lineTo (L + rr, B);
        f.quadraticTo (L, B, L, B - rr);
        f.closeSubPath();
        juce::ignoreUnused (xr);
        bg.setColour (ink.withAlpha (0.9f));
        bg.strokePath (f, juce::PathStrokeType (1.6f));
    }
    auto hline = [&] (float x0, float x1, float y) { bg.fillRect (juce::Rectangle<float> (x0, y - 0.7f, x1 - x0, 1.4f)); };
    auto vline = [&] (float x, float y0, float y1) { bg.fillRect (juce::Rectangle<float> (x - 0.7f, y0, 1.4f, y1 - y0)); };
    bg.setColour (ink.withAlpha (0.9f));
    vline (751, 282, 714);           // EFFECT's left side, from under the valves
    vline (972, 100, 714);           // EFFECT | PART COMMON, SYNTH OSCILLATOR
    vline (1202, 100, 340);          // PART COMMON | MODULATION (down to the MOTION SEQ box)
    vline (1202, 451, 714);          // SYNTH OSCILLATOR | SYNTH FILTER
    hline (972, 1454, 451);          // over the synth sections
    hline (46, 1454, 714);           // over the parts and the arpeggiator
    vline (338, 714, 1044);          // ARPEGGIATOR | the keys
    vline (1018, 714, 842);          // DRUM PART | SYNTH PART
    hline (338, 1454, 842);          // under the parts
    bg.drawRoundedRectangle ({ 1156, 344, 92, 102 }, 6.0f, 1.3f); // the part's MOTION SEQ, in its own box

    // titles: plain, or a white band on the synth sections
    auto title = [&] (const juce::String& t, float cx, float y) { printInk (bg, t, { cx - 90, y, 180, 18 }, 13.5f, ink); };
    title ("PART COMMON", 1087, 108);
    title ("MODULATION", 1328, 108);
    title ("EFFECT", 861, 298);
    title ("DRUM PART", (column (0) + column (9) + kGridKey) * 0.5f, 721);
    title ("ARPEGGIATOR", 192, 724);
    printTag (bg, "SYNTH OSCILLATOR", { 980, 457, 214, 17 }, 12.0f);
    printTag (bg, "SYNTH FILTER", { 1210, 457, 236, 17 }, 12.0f);
    printTag (bg, "SYNTH PART", { 1024, 721, 424, 17 }, 12.0f);

    // the display panel (with its corner cut away) and the transport strip: cream
    auto cream = [&] (const juce::Path& shape) {
        bg.setColour (juce::Colours::black.withAlpha (0.35f));
        bg.fillPath (shape, juce::AffineTransform::translation (0.0f, 2.0f));
        const auto& tile = materials::cached ("vbcream2", [] { return materials::paintTile (colours::creamPanel, 240, 0.6f, 17); });
        bg.setFillType (juce::FillType (tile, juce::AffineTransform::scale (1.0f / sc)));
        bg.fillPath (shape);
        const auto b = shape.getBounds();
        bg.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.2f), 0, b.getY(), juce::Colours::black.withAlpha (0.05f), 0, b.getBottom(), false));
        bg.fillPath (shape);
        bg.setColour (juce::Colours::black.withAlpha (0.3f));
        bg.strokePath (shape, juce::PathStrokeType (1.0f));
    };
    {
        juce::Path d;
        d.startNewSubPath (95 + 10, 278);
        d.lineTo (650, 278);
        d.lineTo (694, 322);
        d.lineTo (694, 457);
        d.quadraticTo (694, 467, 684, 467);
        d.lineTo (105, 467);
        d.quadraticTo (95, 467, 95, 457);
        d.lineTo (95, 288);
        d.quadraticTo (95, 278, 105, 278);
        d.closeSubPath();
        cream (d);
        juce::Path t;
        t.addRoundedRectangle (49.0f, 612.0f, 696.0f, 96.0f, 8.0f);
        cream (t);
    }
    const auto dark = colours::creamInk;
    printInk (bg, "VB-16", { 138, 284, 46, 18 }, 15.0f, dark, juce::Justification::centredLeft);
    printInk (bg, "Groove Production Station", { 184, 286, 200, 16 }, 11.0f, dark, juce::Justification::centredLeft);
    // the part numbers under the LCD's status row, the drums and synths bracketed
    {
        static const char* parts[16] = { "1", "2", "3", "4", "5", "6A", "6B", "7A", "7B", "A", "1", "2", "3", "4", "5", "A" };
        for (int p = 0; p < 16; ++p)
        {
            const float x = lcd.getX() + lcd.segmentCentre (p);
            printInk (bg, parts[p], { x - 9, 432, 18, 13 }, 9.5f, dark);
        }
        const float d0 = lcd.getX() + lcd.segmentCentre (0) - 7, d1 = lcd.getX() + lcd.segmentCentre (9) + 7;
        const float s0 = lcd.getX() + lcd.segmentCentre (10) - 7, s1 = lcd.getX() + lcd.segmentCentre (15) + 7;
        bg.setColour (dark);
        bg.fillRect (juce::Rectangle<float> (d0, 446, d1 - d0, 1.2f));
        printInk (bg, "DRUM PART", { d0, 448, d1 - d0, 13 }, 9.5f, dark);
        bg.fillRoundedRectangle ({ s0, 446, s1 - s0, 15 }, 2.0f);
        printInk (bg, "SYNTH PART", { s0, 446, s1 - s0, 15 }, 9.5f, colours::creamPanel);
    }

    // the effect chain diagram, with its two LEDs at the joins
    {
        bg.setColour (ink);
        for (int i = 0; i < 3; ++i)
        {
            auto box = juce::Rectangle<float> (782, 562 + i * 28.0f, 52, 16);
            bg.drawRoundedRectangle (box, 2.0f, 1.2f);
            printInk (bg, "FX " + juce::String (i + 1), box, 10.0f, ink);
            hline (834, 852, box.getCentreY());
            hline (764, 782, box.getCentreY());
        }
        vline (852, 570, 626);
        vline (764, 570, 626);
        bg.drawEllipse (juce::Rectangle<float> (12, 12).withCentre ({ 764, 598 }), 1.2f);
        hline (760, 768, 598);
        vline (764, 594, 602);
    }
    // the AMP EG lamps' shapes (decay, gate) and the modulation waves beside their lamps
    {
        bg.setColour (ink);
        juce::Path decay, gate;
        decay.startNewSubPath (992, 342);
        decay.lineTo (994, 331);
        decay.quadraticTo (996, 340, 1006, 342);
        gate.startNewSubPath (992, 362);
        gate.lineTo (992, 351);
        gate.lineTo (1004, 351);
        gate.lineTo (1004, 362);
        bg.strokePath (decay, juce::PathStrokeType (1.3f));
        bg.strokePath (gate, juce::PathStrokeType (1.3f));
        for (int i = 0; i < 5; ++i)
        {
            const float x = 1272, y = 260 + i * 21.0f, w = 22, h = 9;
            juce::Path p;
            switch (i)
            {
                case 0: p.startNewSubPath (x, y + h * 0.5f); p.lineTo (x + w * 0.5f, y - h * 0.5f); p.lineTo (x + w * 0.5f, y + h * 0.5f); p.lineTo (x + w, y - h * 0.5f); p.lineTo (x + w, y + h * 0.5f); break;
                case 1: p.startNewSubPath (x, y + h * 0.5f); p.lineTo (x, y - h * 0.5f); p.lineTo (x + w * 0.5f, y - h * 0.5f); p.lineTo (x + w * 0.5f, y + h * 0.5f); p.lineTo (x + w, y + h * 0.5f); p.lineTo (x + w, y - h * 0.5f); break;
                case 2: p.startNewSubPath (x, y + h * 0.5f); p.lineTo (x + w * 0.25f, y - h * 0.5f); p.lineTo (x + w * 0.75f, y + h * 0.5f); p.lineTo (x + w, y - h * 0.5f); break;
                case 3: p.startNewSubPath (x, y); p.lineTo (x + w * 0.25f, y); p.lineTo (x + w * 0.25f, y - h * 0.5f); p.lineTo (x + w * 0.5f, y - h * 0.5f); p.lineTo (x + w * 0.5f, y + h * 0.5f); p.lineTo (x + w * 0.75f, y + h * 0.5f); p.lineTo (x + w * 0.75f, y - h * 0.2f); p.lineTo (x + w, y - h * 0.2f); break;
                default: p.startNewSubPath (x, y + h * 0.5f); p.lineTo (x + w * 0.2f, y - h * 0.5f); p.quadraticTo (x + w * 0.35f, y + h * 0.3f, x + w, y + h * 0.5f); break;
            }
            bg.strokePath (p, juce::PathStrokeType (1.3f));
        }
    }
    // the select keys' song functions, the LED numbers, and the step keys' groups and brackets
    {
        bg.setColour (ink);
        for (float cx : { 664.0f, 732.0f })
            for (int i = 0; i < 2; ++i)
            {
                const float x = cx - 8 + i * 8;
                juce::Path p;
                if (cx < 700)
                    p.addTriangle (x + 7, 848, x, 852, x + 7, 856);
                else
                    p.addTriangle (x, 848, x + 7, 852, x, 856);
                bg.fillPath (p);
            }
        for (int i = 0; i < 8; ++i)
            printInk (bg, juce::String (i + 1), { 794 + i * 41.0f - 8, 848, 16, 14 }, 10.0f, ink);
        for (int g4 = 0; g4 < 4; ++g4)
            hline (column (g4 * 4), column (g4 * 4 + 3) + kGridKey, 916);
        vline (352, 880, 1030);
        hline (352, column (0) - 4, 880);
        auto bracket = [&] (int from, int to, const juce::String& t) {
            const float x0 = column (from) - 6, x1 = column (to) + kGridKey + 6, y = 1030;
            const float tw = juce::GlyphArrangement::getStringWidth (panelFont (9.5f, true), t) + 10.0f, mid = (x0 + x1) * 0.5f;
            hline (x0, mid - tw * 0.5f, y);
            hline (mid + tw * 0.5f, x1, y);
            vline (x0, y - 5, y);
            vline (x1, y - 5, y);
            printInk (bg, t, { mid - tw * 0.5f, y - 7, tw, 14 }, 9.5f, ink);
        };
        bracket (1, 7, "PATTERN");
        bracket (8, 10, "SONG");
        bracket (11, 12, "GLOBAL / MIDI");
    }

    // knobs stand on the panel and cast shadows; every key stands in a well, its shadow falling down-right
    for (auto& w : knobs)
    {
        const auto kk = w->knobArea() + w->getPosition().toFloat();
        materials::roundShadow (bg, kk.getCentre(), kk.getWidth() * 0.48f, kk.getWidth() * 0.24f, 0.9f);
    }
    {
        const auto kk = dial.knobArea() + dial.getPosition().toFloat();
        materials::roundShadow (bg, kk.getCentre(), kk.getWidth() * 0.48f, kk.getWidth() * 0.2f, 0.8f);
    }
    for (auto& w : keys)
    {
        const auto f = w->faceArea() + w->getPosition().toFloat();
        const float corner = w->faceCorner(), depth = w->extrusion();
        const auto base = f.withBottom (f.getBottom() + depth); // the key's footprint, seen from above at a slight tilt
        juce::Path p;
        p.addRoundedRectangle (base.translated (1.5f, 1.0f), corner);
        juce::DropShadow (juce::Colours::black.withAlpha (w->onCream ? 0.40f : 0.55f), (int) (depth + 4), { 2, 3 }).drawForPath (bg, p);
        bg.setColour (juce::Colours::black.withAlpha (w->onCream ? 0.35f : 0.6f)); // the gap round it in the panel
        bg.fillRoundedRectangle (base.expanded (1.6f), corner + 1.6f);
    }
}

// ---------------------------------------------------------------- keys, sync

void MainComponent::pressKey (Key k)
{
    engine.withMachine ([&] (Machine& m) { m.press (k); });
}

void MainComponent::releaseKey (Key k)
{
    engine.withMachine ([&] (Machine& m) { m.release (k); });
    // a latched SHIFT is one-shot: it lets go after the next key
    if (k != Key::Shift && ! shiftFromKeyboard && keyFor[(size_t) Key::Shift]->isLatched())
    {
        keyFor[(size_t) Key::Shift]->setLatched (false);
        engine.withMachine ([&] (Machine& m) { m.release (Key::Shift); });
    }
}

void MainComponent::timerCallback()
{
    Display d;
    std::array<Led, kNumKeys> leds {};
    std::array<int, kNumKnobs> values {};
    std::vector<Led> lampStates (lamps.size());
    int mode = 0, page = 0;
    float tube = 0;
    engine.withMachine ([&] (Machine& m) {
        d = m.getDisplay();
        for (int i = 0; i < kNumKeys; ++i)
            leds[(size_t) i] = m.getLed ((Key) i);
        for (int i = 0; i < kNumKnobs; ++i)
            values[(size_t) i] = m.getKnobValue ((Knob) i);
        for (size_t i = 0; i < lamps.size(); ++i)
            lampStates[i] = m.getLamp (lamps[i].lamp, lamps[i].index);
        mode = (int) m.getMode();
        page = m.getPage();
        tube = m.getKnobValue (Knob::TubeGain) / 127.0f;
    });
    const bool blink = (juce::Time::getMillisecondCounter() / 300) % 2 == 0;
    lcd.setDisplay (d, blink);
    for (auto& w : keys)
    {
        w->setLed (leds[(size_t) w->key]);
        w->setBlinkPhase (blink);
    }
    for (int i = 0; i < kNumKnobs; ++i)
        knobFor[(size_t) i]->setValue (values[(size_t) i]);
    for (size_t i = 0; i < lamps.size(); ++i)
    {
        lamps[i].w->setState (lampStates[i]);
        lamps[i].w->setBlinkPhase (blink);
    }
    if (mode != currentMode || page != currentPage)
    {
        currentMode = mode;
        currentPage = page;
        canvas.repaint (kMatrixArea.expanded (2));
    }
    valves.setGlow (tube, engine.getOutputLevel());
    if (engineMessageUntil != 0 && juce::Time::getMillisecondCounter() > engineMessageUntil)
    {
        engineMessageUntil = 0;
        canvas.repaint (kMessageArea.expanded (2));
    }
}

// ---------------------------------------------------------------- the computer keyboard

bool MainComponent::keyPressed (const juce::KeyPress& kp)
{
    const int code = kp.getKeyCode();
    auto once = [this] (Key k) {
        pressKey (k);
        releaseKey (k);
        return true;
    };
    if (kp == juce::KeyPress::spaceKey) return once (Key::PlayPause);
    if (kp == juce::KeyPress::escapeKey) return once (Key::StopCancel);
    if (kp == juce::KeyPress::returnKey) return once (Key::Rec);
    if (code == juce::KeyPress::backspaceKey || code == juce::KeyPress::deleteKey) return once (Key::ResetErase);
    if (code == juce::KeyPress::leftKey) return once (Key::CursorLeft);
    if (code == juce::KeyPress::rightKey) return once (Key::CursorRight);
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        engine.withMachine ([&] (Machine& m) { m.turnDial (code == juce::KeyPress::upKey ? 1 : -1); });
        return true;
    }
    if (code == juce::KeyPress::tabKey) return once (Key::Keyboard);
    const int fkeys[7] = { juce::KeyPress::F1Key, juce::KeyPress::F2Key, juce::KeyPress::F3Key, juce::KeyPress::F4Key,
                           juce::KeyPress::F5Key, juce::KeyPress::F6Key, juce::KeyPress::F7Key };
    const Key fns[7] = { Key::ModePattern, Key::ModePartEdit, Key::ModeStepEdit, Key::ModeSong, Key::ModeGlobal, Key::ModeMidi, Key::WriteRename };
    for (int i = 0; i < 7; ++i)
        if (code == fkeys[i])
            return once (fns[i]);
    const auto c = juce::CharacterFunctions::toLowerCase (kp.getTextCharacter());
    if (c == '[') return once (Key::SelectLeft);
    if (c == ']') return once (Key::SelectRight);
    if (c == '?' || c == '/') { showHelp(); return true; }
    // held keys are handled in keyStateChanged
    for (const char* s : { kStepChars, kPartChars })
        for (int i = 0; s[i] != 0; ++i)
            if (c == (juce::juce_wchar) s[i])
                return true;
    for (auto& h : kHoldChars)
        if (c == (juce::juce_wchar) h.c)
            return true;
    return false;
}

bool MainComponent::keyStateChanged (bool)
{
    if (juce::ModifierKeys::currentModifiers.isCommandDown())
        return false;
    bool handled = false;
    int slot = 0;
    auto track = [&] (char ch, Key k) {
        const bool down = keyIsDown (ch);
        auto& was = charHeld[(size_t) slot++];
        if (down == was)
            return;
        was = down;
        handled = true;
        if (down)
            pressKey (k);
        else
            releaseKey (k);
    };
    for (int i = 0; i < 16; ++i)
        track (kStepChars[i], (Key) ((int) Key::Step1 + i));
    for (int i = 0; i < 16; ++i)
        track (kPartChars[i], kPartOrder[i]);
    for (auto& h : kHoldChars)
        track (h.c, h.k);
    return handled;
}

void MainComponent::modifierKeysChanged (const juce::ModifierKeys& mods)
{
    // the computer's shift key is SHIFT
    if (mods.isShiftDown() != shiftFromKeyboard)
    {
        shiftFromKeyboard = mods.isShiftDown();
        if (shiftFromKeyboard)
            pressKey (Key::Shift);
        else
            engine.withMachine ([&] (Machine& m) { m.release (Key::Shift); });
    }
}

void MainComponent::focusLost (FocusChangeType)
{
    // keys held when the window loses focus would never see their key-up: let go of them
    int slot = 0;
    auto letGo = [&] (Key k) {
        if (charHeld[(size_t) slot])
            releaseKey (k);
        charHeld[(size_t) slot++] = false;
    };
    for (int i = 0; i < 16; ++i)
        letGo ((Key) ((int) Key::Step1 + i));
    for (int i = 0; i < 16; ++i)
        letGo (kPartOrder[i]);
    for (auto& h : kHoldChars)
        letGo (h.k);
    if (shiftFromKeyboard)
    {
        shiftFromKeyboard = false;
        engine.withMachine ([&] (Machine& m) { m.release (Key::Shift); });
    }
}

// ---------------------------------------------------------------- dialogs, settings

void MainComponent::showHelp()
{
    const auto inputs = engine.getActiveMidiInputNames();
    juce::AlertWindow::showMessageBoxAsync (
        juce::MessageBoxIconType::NoIcon, "Valvebox - quick guide",
        "PLAY a pattern: PLAY/PAUSE (Space). Turn the DIAL on the PATTERN page to pick patterns A.01-D.64; the matrix menu "
        "shows the page, the cursor keys < > move through it.\n\n"
        "PARTS: drum parts 1-5, 6A/6B, 7A/7B (keys 1-9, 0 = drum accent) and synth parts 1-5 (Z X C V B, N = synth accent). "
        "The step keys (A W S D R F T G H U J I K O L ;) toggle the selected part's steps; with KEYBOARD on (Tab) they play the synth.\n\n"
        "HOLD KEYS: SHIFT (the shift key, or click: one-shot), PART MUTE (Q), SOLO/TRANSPOSE (E), PATTERN SET (P) - click to hold, "
        "click again to let go. SHIFT + a step key runs the function printed under it.\n\n"
        "RECORD: REC (Return) then PLAY, and play part keys or the keyboard. MOTION SEQ on a part (or effect) + REC records a knob for one pass.\n\n"
        "WRITE (F7) saves a pattern or song; Memory Protect is on from the factory (GLOBAL > Protect).\n\n"
        "Arrow up / down turn the dial. Backspace = RESET, Esc = STOP/CANCEL, [ ] = select keys, F1-F6 = the modes.\n\n"
        "MIDI IN: " + (inputs.isEmpty() ? juce::String ("no controllers connected") : inputs.joinIntoString (", ")) + ".");
}

void MainComponent::showAudioSettings()
{
    auto* content = new juce::Component();
    auto* selector = new juce::AudioDeviceSelectorComponent (engine.getDeviceManager(), 0, 2, 2, 4, true, false, true, false);
    selector->setSize (560, 460);
    auto* outLabel = new juce::Label ({}, "MIDI out:");
    auto* outBox = new juce::ComboBox();
    outBox->addItem ("(none)", 1);
    const auto outs = engine.getMidiOutputs();
    for (int i = 0; i < outs.size(); ++i)
    {
        outBox->addItem (outs[i].name, i + 2);
        if (outs[i].identifier == engine.getMidiOutputIdentifier())
            outBox->setSelectedId (i + 2, juce::dontSendNotification);
    }
    if (outBox->getSelectedId() == 0)
        outBox->setSelectedId (1, juce::dontSendNotification);
    outBox->onChange = [this, outBox, outs] {
        const int id = outBox->getSelectedId();
        engine.setMidiOutput (id >= 2 ? outs[id - 2].identifier : juce::String());
        saveSettings();
    };
    content->addAndMakeVisible (selector);
    content->addAndMakeVisible (outLabel);
    content->addAndMakeVisible (outBox);
    selector->setTopLeftPosition (0, 0);
    outLabel->setBounds (16, 468, 80, 24);
    outBox->setBounds (100, 468, 440, 24);
    content->setSize (560, 504);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (content);
    o.dialogTitle = "Audio / MIDI settings";
    o.dialogBackgroundColour = juce::Colour (0xff1d1f24);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void MainComponent::loadSettings()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Valvebox";
    o.filenameSuffix = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    o.folderName = "Valvebox";
    if (std::getenv ("VALVEBOX_SELFTEST") != nullptr || std::getenv ("VALVEBOX_SNAPSHOT") != nullptr)
        return; // the self-test and snapshots leave the user's settings alone
    settings = std::make_unique<juce::PropertiesFile> (o);
    if (auto xml = settings->getXmlValue ("audioDevice"))
        engine.getDeviceManager().initialise (0, 2, xml.get(), true);
    if (settings->containsKey ("midiOut"))
        engine.setMidiOutput (settings->getValue ("midiOut"));
    engine.withMachine ([&] (Machine& m) {
        m.setKnob (Knob::MasterVolume, settings->getIntValue ("masterVolume", 100));
        m.setKnob (Knob::TubeGain, settings->getIntValue ("tubeGain", 0));
        m.setKnob (Knob::AudioInLevel, settings->getIntValue ("audioInLevel", 80));
    });
}

void MainComponent::saveSettings()
{
    if (settings == nullptr)
        return;
    if (auto xml = engine.getDeviceManager().createStateXml())
        settings->setValue ("audioDevice", xml.get());
    settings->setValue ("midiOut", engine.getMidiOutputIdentifier());
    engine.withMachine ([&] (Machine& m) {
        settings->setValue ("masterVolume", m.getKnobValue (Knob::MasterVolume));
        settings->setValue ("tubeGain", m.getKnobValue (Knob::TubeGain));
        settings->setValue ("audioInLevel", m.getKnobValue (Knob::AudioInLevel));
    });
    settings->saveIfNeeded();
}

// ---------------------------------------------------------------- self-test

void MainComponent::runSelfTest (const juce::File& reportFile)
{
    auto lines = std::make_shared<juce::StringArray>();
    auto failures = std::make_shared<int> (0);
    auto log = [lines] (const juce::String& s) { lines->add (s); };
    auto fail = [lines, failures] (const juce::String& s) { lines->add ("FAIL  " + s); ++*failures; };

    log ("-- every key on the panel has its widget, and the widget reaches the instrument");
    for (int k = 0; k < kNumKeys; ++k)
    {
        auto* w = keyFor[(size_t) k];
        if (w == nullptr)
        {
            fail (juce::String ("no widget for ") + keyName ((Key) k));
            continue;
        }
        if (w->key != (Key) k)
            fail (juce::String ("widget mix-up for ") + keyName ((Key) k));
        bool heldDuring = false;
        if (w->onPress)
            w->onPress (w->key);
        engine.withMachine ([&] (Machine& m) { heldDuring = m.isHeld ((Key) k); });
        if (w->onRelease)
            w->onRelease (w->key);
        bool heldAfter = true;
        engine.withMachine ([&] (Machine& m) { heldAfter = m.isHeld ((Key) k); });
        if (! heldDuring || heldAfter)
            fail (juce::String (keyName ((Key) k)) + ": press / release didn't reach the instrument");
        // put things back the way they were (modes, toggles): STOP and the pattern page
        engine.withMachine ([&] (Machine& m) {
            m.press (Key::StopCancel);
            m.release (Key::StopCancel);
        });
    }
    log ("ok    " + juce::String (kNumKeys) + " keys");

    log ("-- every knob sets its value");
    engine.withMachine ([&] (Machine& m) {
        m.press (Key::ModePattern);
        m.release (Key::ModePattern);
        m.press (Key::Synth1);
        m.release (Key::Synth1);
    });
    for (int k = 0; k < kNumKnobs; ++k)
    {
        auto* w = knobFor[(size_t) k];
        const bool selector = (Knob) k == Knob::FxType || (Knob) k == Knob::OscType;
        const int v = selector ? 5 * 8 + 4 : 37;
        w->onChange (v);
        int got = -1;
        engine.withMachine ([&] (Machine& m) { got = m.getKnobValue ((Knob) k); });
        if (got != v)
            fail (juce::String (knobName ((Knob) k)) + ": set " + juce::String (v) + ", reads " + juce::String (got));
    }
    log ("ok    " + juce::String (kNumKnobs) + " knobs");

    log ("-- the dial, the ribbon, the slider");
    double tempoBefore = 0, tempoAfter = 0;
    engine.withMachine ([&] (Machine& m) {
        m.selectPattern (0);
        m.press (Key::CursorRight); // the Tempo page
        m.release (Key::CursorRight);
        tempoBefore = m.getTempo();
    });
    dial.onTurn (3);
    engine.withMachine ([&] (Machine& m) { tempoAfter = m.getTempo(); });
    if (std::abs (tempoAfter - tempoBefore - 3.0) > 1.0e-6)
        fail ("the dial didn't turn the tempo");
    engine.withMachine ([&] (Machine& m) {
        m.press (Key::CursorLeft);
        m.release (Key::CursorLeft);
        m.press (Key::Drum1);
        m.release (Key::Drum1);
        m.enableTriggerLog (true);
    });
    ribbonW.onTouch (true, 0.4f);
    sliderW.onMove (0.7f);

    struct Step { int ms; std::function<void()> fn; };
    auto steps = std::make_shared<std::vector<Step>>();
    steps->push_back ({ 600, [=, this] {
        ribbonW.onTouch (false, 0.4f);
        int hits = 0;
        engine.withMachine ([&] (Machine& m) { hits = (int) m.getTriggerLog().size(); });
        if (hits < 3)
            fail ("the ribbon didn't play the drum part (" + juce::String (hits) + " hits)");
        else
            log ("ok    the ribbon played " + juce::String (hits) + " hits");
        log ("-- sound: the factory pattern plays, MASTER VOLUME, the display");
        engine.withMachine ([&] (Machine& m) {
            m.setKnob (Knob::MasterVolume, 100);
            m.press (Key::PlayPause);
            m.release (Key::PlayPause);
        });
    } });
    steps->push_back ({ 800, [=, this] {
        const float l = engine.getOutputLevel();
        if (l < 0.02f)
            fail ("no sound playing the pattern (" + juce::String (l, 4) + ")");
        else
            log ("ok    playing: peak " + juce::String (l, 3));
        knobFor[(size_t) Knob::MasterVolume]->onChange (0);
    } });
    steps->push_back ({ 400, [=, this] {
        const float l = engine.getOutputLevel();
        if (l > 1.0e-3f)
            fail ("MASTER VOLUME at 0 isn't silent");
        else
            log ("ok    MASTER VOLUME 0: silent");
        knobFor[(size_t) Knob::MasterVolume]->onChange (100);
        Display d;
        engine.withMachine ([&] (Machine& m) { d = m.getDisplay(); });
        if (d.param.isEmpty())
            fail ("the display is blank");
        engine.withMachine ([&] (Machine& m) {
            m.press (Key::StopCancel);
            m.release (Key::StopCancel);
        });
        lines->add ("");
        lines->add (*failures == 0 ? juce::String ("ALL PASSED") : juce::String (*failures) + " FAILED");
        reportFile.replaceWithText (lines->joinIntoString ("\n") + "\n");
        juce::JUCEApplicationBase::quit();
    } });
    struct Runner
    {
        static void next (std::shared_ptr<std::vector<Step>> s, size_t i, juce::Component::SafePointer<MainComponent> safe)
        {
            if (safe == nullptr || i >= s->size())
                return;
            juce::Timer::callAfterDelay ((*s)[i].ms, [s, i, safe] {
                if (safe == nullptr)
                    return;
                (*s)[i].fn();
                next (s, i + 1, safe);
            });
        }
    };
    Runner::next (steps, 0, this);
}
} // namespace valvebox::ui
