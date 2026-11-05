#include "ui/MainComponent.h"
#include "ui/Materials.h"
#include "core/Mapping.h"
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

    const char* const kMatrix[6][7] = { { "Pattern", "Tempo", "Swing", "Length", "Beat", "RollType", "ArpScale" },
                                        { "Wave", "AccentSw", "SwingSw", "OutBus", "SynTune", nullptr, nullptr },
                                        { "StepRec", "NoteNo", "GateTime", "MotDest", "MotValue", nullptr, nullptr },
                                        { "Song", "Tempo", "Position", "Pattern", "NoteOfs", "MuteHold", "NextSong" },
                                        { "Metronom", "MstTune", "Protect", "ArpCntrl", nullptr, nullptr, nullptr },
                                        { "MIDI ch", "Clock", "BendRnge", nullptr, nullptr, nullptr, nullptr } };
    const char* const kModeNames[6] = { "PATTERN", "PART EDIT", "STEP EDIT", "SONG", "GLOBAL", "MIDI" };

    const juce::Rectangle<int> kMatrixArea { 350, 250, 352, 104 };
    const juce::Rectangle<int> kMessageArea { 350, 398, 470, 18 };

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
        addAndMakeVisible (b);
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
    loadSettings();
    setSize (1440, kHeight * 1440 / kWidth);
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
            const auto img = safe->createComponentSnapshot (safe->getLocalBounds(), true, 2.0f);
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
    // common section: the transport keys on their cream strip
    key (Key::ResetErase, "RESET", "ERASE", S::Transport, false);
    key (Key::Rec, "REC", {}, S::Transport);
    key (Key::StopCancel, "STOP", "CANCEL", S::Transport, false);
    key (Key::PlayPause, "PLAY/PAUSE", {}, S::Transport);
    key (Key::Tap, "TAP", {}, S::Square, false);
    key (Key::PartMute, "PART MUTE", "REST/TIE", S::Square);
    key (Key::Solo, "SOLO", "TRANSPOSE", S::Square);
    for (auto k : { Key::ResetErase, Key::Rec, Key::StopCancel, Key::PlayPause, Key::Tap, Key::PartMute, Key::Solo })
        keyFor[(size_t) k]->onCream = true;
    key (Key::AudioInThru, "AUDIO IN THRU");
    key (Key::TempoLock, "TEMPO LOCK");
    key (Key::AutoBpm, "AUTO BPM SCAN");
    key (Key::WriteRename, "WRITE", "RENAME");
    key (Key::CursorLeft, "<", {}, S::Pill, false);
    key (Key::CursorRight, ">", {}, S::Pill, false);
    keyFor[(size_t) Key::CursorLeft]->onCream = keyFor[(size_t) Key::CursorRight]->onCream = true;
    const Key modeKeys[6] = { Key::ModePattern, Key::ModePartEdit, Key::ModeStepEdit, Key::ModeSong, Key::ModeGlobal, Key::ModeMidi };
    for (int i = 0; i < 6; ++i)
        key (modeKeys[i], kModeNames[i]);
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
    key (Key::DrumAccent, "ACCENT", {}, S::Square);
    for (int i = 0; i < 5; ++i)
        key ((Key) ((int) Key::Synth1 + i), juce::String (i + 1), {}, S::Square);
    key (Key::SynthAccent, "ACCENT", {}, S::Square);
    key (Key::Keyboard, "KEYBOARD");
    key (Key::SelectLeft, "<", {}, S::Pill, false);
    key (Key::SelectRight, ">", {}, S::Pill, false);
    key (Key::Shift, "SHIFT", {}, S::Round, false);
    key (Key::PatternSet, "PATTERN SET", {}, S::Pill, false);
    const char* shiftFns[16] = { "LAST STEP", "MOVE DATA", "SHIFT NOTE", "COPY PART", "COPY SOUND", "CLEAR MOTION", "CLEAR PART", "CLEAR PATTERN",
                                 "INSERT PATTERN", "DELETE PATTERN", "CLEAR SONG", "MIDI UTILITY", "CARD", "", "", "" };
    for (int i = 0; i < 16; ++i)
        key ((Key) ((int) Key::Step1 + i), juce::String (i + 1), shiftFns[i], isBlackStep (i) ? S::StepSharp : S::Step);
    for (auto k : { Key::Shift, Key::PartMute, Key::Solo, Key::PatternSet })
        keyFor[(size_t) k]->latching = true;

    // knobs
    auto knob = [this] (Knob k, const juce::String& label, bool bipolar = false) {
        auto w = std::make_unique<KnobWidget> (label, bipolar);
        w->onChange = [this, k] (int v) { engine.withMachine ([&] (Machine& m) { m.setKnob (k, v); }); };
        canvas.addAndMakeVisible (*w);
        knobFor[(size_t) k] = w.get();
        knobs.push_back (std::move (w));
    };
    auto selector = [this] (Knob k, const juce::String& label, juce::StringArray choices) {
        auto w = std::make_unique<SelectorWidget> (label, std::move (choices));
        w->onChange = [this, k] (int v) { engine.withMachine ([&] (Machine& m) { m.setKnob (k, v); }); };
        canvas.addAndMakeVisible (*w);
        knobFor[(size_t) k] = w.get();
        knobs.push_back (std::move (w));
    };
    knob (Knob::MasterVolume, "MASTER VOLUME");
    knob (Knob::TubeGain, "TUBE GAIN");
    knob (Knob::AudioInLevel, "AUDIO IN LEVEL");
    juce::StringArray fxNames, oscNames;
    for (int i = 0; i < kNumFxTypes; ++i)
        fxNames.add (mapping::fxTypeName ((FxType) i));
    for (int i = 0; i < kNumOscTypes; ++i)
        oscNames.add (mapping::oscTypeName ((OscType) i));
    selector (Knob::FxType, "TYPE", fxNames);
    knob (Knob::FxEdit1, "FX EDIT 1");
    knob (Knob::FxEdit2, "FX EDIT 2");
    knob (Knob::PitchGlide, "PITCH/GLIDE", true);
    knob (Knob::EgTime, "EG TIME");
    knob (Knob::Pan, "PAN", true);
    knob (Knob::Level, "LEVEL");
    knob (Knob::ModSpeed, "SPEED");
    knob (Knob::ModDepth, "DEPTH", true);
    selector (Knob::OscType, "TYPE", oscNames);
    knob (Knob::OscEdit1, "OSC EDIT 1");
    knob (Knob::OscEdit2, "OSC EDIT 2");
    knob (Knob::Cutoff, "CUTOFF");
    knob (Knob::EgInt, "EG INT", true);
    knob (Knob::Resonance, "RESONANCE");
    knob (Knob::Drive, "DRIVE");

    // lamps
    auto lamp = [this] (Lamp l, int index, const juce::String& label, juce::Colour c = colours::ledRed) {
        LampRef ref { std::make_unique<LampWidget> (label, c), l, index };
        canvas.addAndMakeVisible (*ref.w);
        lamps.push_back (std::move (ref));
    };
    lamp (Lamp::Beat, 0, "BEAT", colours::ledOrange);
    lamps.back().w->onCream = true;
    for (int i = 0; i < 8; ++i)
        lamp (Lamp::SelectGreen, i, {}, colours::ledGreen);
    for (int i = 0; i < 8; ++i)
        lamp (Lamp::SelectRed, i, {}, colours::ledRed);
    lamp (Lamp::ChainOneTwo, 0, "1>2");
    lamp (Lamp::ChainTwoThree, 0, "2>3");
    for (int i = 0; i < 3; ++i)
        lamp (Lamp::EditSelect, i, "FX" + juce::String (i + 1));
    lamp (Lamp::MotionSmooth, 0, "SMOOTH");
    lamp (Lamp::MotionTrigHold, 0, "TRIG HOLD");
    lamp (Lamp::AmpDecay, 0, "DECAY");
    lamp (Lamp::AmpGate, 0, "GATE");
    for (int i = 0; i < 3; ++i)
        lamp (Lamp::FxSelectLamp, i, "FX" + juce::String (i + 1));
    for (int i = 0; i < 5; ++i)
        lamp (Lamp::ModTypeLamp, i, mapping::modTypeName ((ModType) i));
    for (int i = 0; i < 6; ++i)
        lamp (Lamp::ModDestLamp, i, mapping::modDestName ((ModDest) i));
    for (int i = 0; i < 4; ++i)
        lamp (Lamp::FilterTypeLamp, i, mapping::filterName ((FilterType) i));

    dial.onTurn = [this] (int c) { engine.withMachine ([&] (Machine& m) { m.turnDial (c); }); };
    ribbonW.onTouch = [this] (bool on, float p) { engine.withMachine ([&] (Machine& m) { m.ribbon (on, p); }); };
    sliderW.onMove = [this] (float v) { engine.withMachine ([&] (Machine& m) { m.slider (v); }); };
    for (auto* c : std::initializer_list<juce::Component*> { &dial, &lcd, &ribbonW, &sliderW, &valves })
        canvas.addAndMakeVisible (c);
}

void MainComponent::layout()
{
    canvas.setBounds (0, 0, kWidth, kHeight);
    auto place = [] (juce::Component* c, int x, int y, int w, int h) { c->setBounds (x, y, w, h); };
    auto k = [this] (Key key) { return keyFor[(size_t) key]; };
    auto kn = [this] (Knob knob) { return knobFor[(size_t) knob]; };
    auto lampAt = [this] (Lamp l, int index) -> LampWidget* {
        for (auto& r : lamps)
            if (r.lamp == l && r.index == index)
                return r.w.get();
        return nullptr;
    };

    // ---- left: volume, tubes, the audio-in keys
    place (kn (Knob::MasterVolume), 40, 66, 92, 100);
    place (kn (Knob::TubeGain), 137, 66, 92, 100);
    place (kn (Knob::AudioInLevel), 234, 66, 92, 100);
    place (&valves, 52, 176, 262, 170);
    place (k (Key::AudioInThru), 40, 360, 92, 32);
    place (k (Key::TempoLock), 137, 360, 92, 32);
    place (k (Key::AutoBpm), 234, 360, 92, 32);

    // ---- the display panel: LCD, dial, cursor keys; the matrix menu, the modes and WRITE under it
    place (&lcd, 350, 66, 352, 168);
    place (&dial, 716, 60, 104, 104);
    place (lampAt (Lamp::Beat, 0), 724, 168, 60, 14);
    place (k (Key::CursorLeft), 712, 192, 54, 34);
    place (k (Key::CursorRight), 768, 192, 54, 34);
    place (k (Key::WriteRename), 712, 262, 108, 50);
    for (int i = 0; i < 6; ++i)
        place (k ((Key) ((int) Key::ModePattern + i)), 350 + i * 59, 360, 59, 32);

    // ---- EFFECT
    place (kn (Knob::FxType), 836, 76, 150, 164);
    place (kn (Knob::FxEdit1), 990, 74, 56, 84);
    place (kn (Knob::FxEdit2), 1044, 74, 56, 84);
    place (k (Key::FxChain), 990, 170, 54, 32);
    place (lampAt (Lamp::ChainOneTwo, 0), 994, 208, 44, 12);
    place (lampAt (Lamp::ChainTwoThree, 0), 994, 222, 44, 12);
    place (k (Key::FxEditSelect), 1046, 170, 54, 32);
    for (int i = 0; i < 3; ++i)
        place (lampAt (Lamp::EditSelect, i), 1052, 206 + i * 13, 46, 12);

    // ---- MODULATION: knobs, keys, and the type / destination lamps beside them
    place (kn (Knob::ModSpeed), 1110, 74, 62, 84);
    place (kn (Knob::ModDepth), 1172, 74, 62, 84);
    place (k (Key::ModType), 1108, 176, 44, 32);
    place (k (Key::ModDest), 1152, 176, 44, 32);
    place (k (Key::ModBpmSync), 1196, 176, 48, 32);
    for (int i = 0; i < 5; ++i)
        place (lampAt (Lamp::ModTypeLamp, i), 1248, 80 + i * 15, 40, 14);
    for (int i = 0; i < 6; ++i)
        place (lampAt (Lamp::ModDestLamp, i), 1286, 80 + i * 15, 46, 14);

    // ---- SYNTH OSCILLATOR
    place (kn (Knob::OscType), 1342, 76, 150, 164);
    place (kn (Knob::OscEdit1), 1500, 72, 72, 84);
    place (kn (Knob::OscEdit2), 1500, 156, 72, 84);

    // ---- PART COMMON
    place (k (Key::PartMotion), 838, 284, 70, 32);
    place (lampAt (Lamp::MotionSmooth, 0), 840, 322, 70, 13);
    place (lampAt (Lamp::MotionTrigHold, 0), 840, 337, 70, 13);
    place (kn (Knob::PitchGlide), 912, 270, 70, 86);
    place (kn (Knob::EgTime), 984, 270, 70, 86);
    place (kn (Knob::Pan), 1056, 270, 70, 86);
    place (kn (Knob::Level), 1128, 270, 70, 86);
    place (k (Key::AmpEg), 914, 368, 66, 32);
    place (lampAt (Lamp::AmpDecay, 0), 914, 404, 36, 12);
    place (lampAt (Lamp::AmpGate, 0), 950, 404, 34, 12);
    place (k (Key::Roll), 986, 368, 66, 32);
    place (k (Key::FxSelect), 1058, 368, 66, 32);
    for (int i = 0; i < 3; ++i)
        place (lampAt (Lamp::FxSelectLamp, i), 1058 + i * 22, 404, 22, 12);
    place (k (Key::FxSend), 1130, 368, 66, 32);
    place (k (Key::FxMotion), 838, 380, 70, 32); // (EFFECT's MOTION SEQ, under the part's)

    // ---- SYNTH FILTER
    place (kn (Knob::Cutoff), 1218, 270, 68, 86);
    place (kn (Knob::EgInt), 1288, 270, 68, 86);
    place (kn (Knob::Resonance), 1358, 270, 68, 86);
    place (kn (Knob::Drive), 1428, 270, 68, 86);
    place (k (Key::FilterType), 1502, 284, 70, 32);
    for (int i = 0; i < 4; ++i)
        place (lampAt (Lamp::FilterTypeLamp, i), 1506, 326 + i * 16, 70, 14);

    // ---- transport (on its cream strip) and parts
    const Key transport[7] = { Key::ResetErase, Key::Rec, Key::StopCancel, Key::PlayPause, Key::Tap, Key::PartMute, Key::Solo };
    for (int i = 0; i < 7; ++i)
        place (k (transport[i]), 40 + i * 77, 462, 74, 96);
    for (int i = 0; i < 10; ++i)
        place (k (i < 9 ? (Key) ((int) Key::Drum1 + i) : Key::DrumAccent), 600 + i * 56, 480, 54, 76);
    for (int i = 0; i < 6; ++i)
        place (k (i < 5 ? (Key) ((int) Key::Synth1 + i) : Key::SynthAccent), 1180 + i * 56, 480, 54, 76);
    place (k (Key::Keyboard), 1512, 500, 68, 34);

    // ---- SHIFT, PATTERN SET, the select keys and LEDs, the step keys
    place (k (Key::Shift), 44, 596, 72, 72);
    place (k (Key::PatternSet), 124, 612, 96, 36);
    place (k (Key::SelectLeft), 226, 612, 60, 36);
    place (k (Key::SelectRight), 750, 612, 60, 36);
    for (int i = 0; i < 8; ++i)
    {
        place (lampAt (Lamp::SelectGreen, i), 304 + i * 56, 606, 14, 14);
        place (lampAt (Lamp::SelectRed, i), 304 + i * 56, 630, 14, 14);
    }
    for (int i = 0; i < 16; ++i)
        place (k ((Key) ((int) Key::Step1 + i)), 226 + i * 68, 678, 66, 96);

    // ---- the arpeggiator
    place (&ribbonW, 1344, 610, 164, 156);
    place (&sliderW, 1510, 600, 48, 172);
}

// ---------------------------------------------------------------- painting

void MainComponent::paint (juce::Graphics& g) { g.fillAll (juce::Colour (0xff0d0e10)); }

void MainComponent::resized()
{
    // the panel keeps its proportions; the buttons sit in the top-right of the panel
    const float sc = std::min (getWidth() / (float) kWidth, getHeight() / (float) kHeight);
    const float ox = (getWidth() - kWidth * sc) * 0.5f, oy = (getHeight() - kHeight * sc) * 0.5f;
    canvas.setTransform (juce::AffineTransform::scale (sc).translated (ox, oy));
    audioBtn.setBounds ((int) (ox + 1372 * sc), (int) (oy + 18 * sc), (int) (104 * sc), (int) (26 * sc));
    helpBtn.setBounds ((int) (ox + 1482 * sc), (int) (oy + 18 * sc), (int) (70 * sc), (int) (26 * sc));
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

    // the matrix menu: the six modes in columns, the current page lit
    auto m = kMatrixArea.toFloat();
    const float cw = m.getWidth() / 6.0f, rh = (m.getHeight() - 17.0f) / 7.0f;
    for (int c = 0; c < 6; ++c)
    {
        const auto col = juce::Rectangle<float> (m.getX() + c * cw, m.getY(), cw, m.getHeight());
        if (c > 0)
        {
            g.setColour (colours::print.withAlpha (0.45f));
            g.fillRect (col.getX(), col.getY() + 2.0f, 1.0f, col.getHeight() - 4.0f);
        }
        printInk (g, kModeNames[c], { col.getX() + 2, m.getY(), cw - 4, 15 }, 11.0f, colours::print.withAlpha (c == currentMode ? 1.0f : 0.8f),
                  juce::Justification::centred);
        for (int r = 0; r < 7 && kMatrix[c][r] != nullptr; ++r)
        {
            auto cell = juce::Rectangle<float> (col.getX() + 3.0f, m.getY() + 17 + r * rh, cw - 6.0f, rh);
            if (c == currentMode && r == currentPage)
            {
                g.setColour (colours::print);
                g.fillRoundedRectangle (cell.reduced (0.0f, 0.5f), rh * 0.45f);
                printInk (g, kMatrix[c][r], cell, 10.5f, colours::panelDark, juce::Justification::centred);
            }
            else
                printInk (g, kMatrix[c][r], cell, 10.5f, colours::print.withAlpha (0.82f), juce::Justification::centred, false);
        }
    }
    // a line for messages from the engine (MIDI devices, dumps) under the modes
    if (engineMessage.isNotEmpty() && juce::Time::getMillisecondCounter() < engineMessageUntil)
        print (g, engineMessage, kMessageArea.toFloat(), 11.0f, juce::Justification::centredLeft, false, true);
}

void MainComponent::paintBackground (juce::Graphics& bg, float sc)
{
    // the end cheeks and the metallic blue panel
    bg.setColour (juce::Colour (0xff141518));
    bg.fillRect (0, 0, kWidth, kHeight);
    const auto pa = juce::Rectangle<float> (14, 8, kWidth - 28, kHeight - 16);
    {
        const auto& tile = materials::cached ("vbflake", [] { return materials::flakeTile (colours::panel, 252, 11); });
        bg.setFillType (juce::FillType (tile, juce::AffineTransform::scale (1.0f / sc)));
        bg.fillRoundedRectangle (pa, 12.0f);
    }
    {
        // the light: brighter towards the top-left, deeper towards the bottom-right, and a broad sheen across
        juce::Graphics::ScopedSaveState save (bg);
        juce::Path panelShape;
        panelShape.addRoundedRectangle (pa, 12.0f);
        bg.reduceClipRegion (panelShape);
        juce::ColourGradient light (juce::Colours::white.withAlpha (0.16f), pa.getX() + pa.getWidth() * 0.25f, pa.getY(), juce::Colours::transparentWhite,
                                    pa.getX() + pa.getWidth() * 0.25f, pa.getY() + pa.getHeight() * 0.9f, true);
        bg.setGradientFill (light);
        bg.fillRect (pa);
        juce::ColourGradient deep (juce::Colours::transparentBlack, pa.getCentreX(), pa.getCentreY(), juce::Colours::black.withAlpha (0.28f), pa.getRight(),
                                   pa.getBottom(), false);
        bg.setGradientFill (deep);
        bg.fillRect (pa);
        // our own motif: wide rings spreading from the valve window
        const juce::Point<float> vc (183.0f, 254.0f);
        juce::Path band;
        band.addEllipse (juce::Rectangle<float> (520, 520).withCentre (vc));
        band.addEllipse (juce::Rectangle<float> (430, 430).withCentre (vc));
        band.setUsingNonZeroWinding (false);
        bg.setColour (juce::Colours::white.withAlpha (0.045f));
        bg.fillPath (band);
        bg.setColour (juce::Colours::white.withAlpha (0.16f));
        bg.drawEllipse (juce::Rectangle<float> (600, 600).withCentre (vc), 1.2f);
        bg.drawEllipse (juce::Rectangle<float> (430, 430).withCentre (vc), 1.0f);
    }
    bg.setColour (juce::Colours::white.withAlpha (0.22f));
    bg.drawRoundedRectangle (pa.reduced (1.0f), 11.0f, 1.0f);
    bg.setColour (juce::Colours::black.withAlpha (0.55f));
    bg.drawRoundedRectangle (pa, 12.0f, 1.5f);
    for (auto pt : { juce::Point<float> (28, 22), juce::Point<float> (kWidth - 28.0f, 22), juce::Point<float> (28, kHeight - 22.0f),
                     juce::Point<float> (kWidth - 28.0f, kHeight - 22.0f) })
    {
        const int px = (int) std::ceil (10 * sc);
        materials::drawAt (bg, materials::cached ("vbscrew:" + juce::String (px), [=] { return materials::screw (px, 0.4f); }),
                           juce::Rectangle<float> (10, 10).withCentre (pt));
    }

    // the name
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (keyFont (34.0f), "valvebox", 0, 0);
        juce::Path word;
        ga.createPath (word);
        word.applyTransform (juce::AffineTransform::translation (44, 44));
        bg.setColour (juce::Colours::black.withAlpha (0.25f));
        bg.fillPath (word, juce::AffineTransform::translation (0.0f, 1.5f));
        bg.setColour (colours::print);
        bg.fillPath (word);
        auto tag = juce::Rectangle<float> (196, 24, 50, 18);
        bg.fillRoundedRectangle (tag, 3.0f);
        printInk (bg, "VB-16", tag, 13.0f, colours::panelDark);
        printInk (bg, "GROOVE PRODUCTION STATION", { 254, 24, 300, 18 }, 13.0f, colours::print, juce::Justification::centredLeft);
    }

    // the cream sub-panels: the display, and the transport strip
    auto creamPanel = [&] (juce::Rectangle<float> r) {
        bg.setColour (juce::Colours::black.withAlpha (0.35f));
        bg.fillRoundedRectangle (r.translated (0.0f, 2.0f).expanded (1.0f), 11.0f);
        const auto& tile = materials::cached ("vbcream", [] { return materials::paintTile (colours::creamPanel, 240, 0.7f, 17); });
        bg.setFillType (juce::FillType (tile, juce::AffineTransform::scale (1.0f / sc)));
        bg.fillRoundedRectangle (r, 10.0f);
        bg.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.18f), 0, r.getY(), juce::Colours::black.withAlpha (0.06f), 0, r.getBottom(), false));
        bg.fillRoundedRectangle (r, 10.0f);
        bg.setColour (juce::Colours::white.withAlpha (0.6f));
        bg.drawRoundedRectangle (r.reduced (1.0f), 9.0f, 1.0f);
        bg.setColour (juce::Colours::black.withAlpha (0.3f));
        bg.drawRoundedRectangle (r, 10.0f, 1.0f);
    };
    creamPanel ({ 338, 54, 494, 190 });
    creamPanel ({ 30, 452, 548, 120 });

    // silk-screened sections: white rules with a title tab
    auto section = [&] (juce::Rectangle<float> r, const juce::String& title) {
        bg.setColour (colours::print.withAlpha (0.8f));
        bg.drawRoundedRectangle (r, 8.0f, 1.3f);
        if (title.isNotEmpty())
        {
            const float w = juce::GlyphArrangement::getStringWidth (panelFont (12.5f, true), title) + 22.0f;
            auto t = juce::Rectangle<float> (r.getX() + 12, r.getY() - 9, w, 18);
            bg.setColour (colours::print);
            bg.fillRoundedRectangle (t, 4.0f);
            printInk (bg, title, t, 12.5f, colours::panelDark);
        }
    };
    section ({ 830, 58, 274, 190 }, "EFFECT");
    section ({ 1106, 58, 228, 190 }, "MODULATION");
    section ({ 1336, 58, 244, 190 }, "SYNTH OSCILLATOR");
    section ({ 830, 260, 372, 170 }, "PART COMMON");
    section ({ 1206, 260, 374, 170 }, "SYNTH FILTER");
    section ({ 592, 456, 580, 116 }, "DRUM PART");
    section ({ 1176, 456, 334, 116 }, "SYNTH PART");
    section ({ 1334, 588, 232, 190 }, "ARPEGGIATOR");
    bg.setColour (colours::print.withAlpha (0.8f));
    bg.fillRect (juce::Rectangle<float> (390, 658, 926, 1.3f)); // the rule over the step keys
    print (bg, "FX MOTION", { 838, 412, 70, 14 }, 9.5f, juce::Justification::centred, true, true);
    for (int i = 0; i < 8; ++i)
        print (bg, juce::String (i + 1), { 304.0f + i * 56 + 17, 604, 20, 42 }, 11.0f, juce::Justification::centredLeft, true, true);
    print (bg, "STEP KEYS  /  KEYBOARD", { 226, 652, 300, 14 }, 11.5f, juce::Justification::centredLeft, false, true);

    // what stands on the panel casts shadows, and every key sits in a well
    for (auto& w : knobs)
    {
        const auto k = w->knobArea() + w->getPosition().toFloat();
        materials::roundShadow (bg, k.getCentre(), k.getWidth() * 0.48f, k.getWidth() * 0.22f, 0.9f);
    }
    {
        const auto k = dial.knobArea() + dial.getPosition().toFloat();
        materials::roundShadow (bg, k.getCentre(), k.getWidth() * 0.48f, k.getWidth() * 0.2f, 0.8f);
    }
    for (auto& w : keys)
    {
        const auto cap = w->capArea() + w->getPosition().toFloat();
        const float corner = w->capCorner();
        juce::Path p;
        p.addRoundedRectangle (cap, corner);
        juce::DropShadow (juce::Colours::black.withAlpha (w->onCream ? 0.35f : 0.5f), 7, { 1, 3 }).drawForPath (bg, p);
        bg.setColour (juce::Colours::black.withAlpha (w->onCream ? 0.32f : 0.5f));
        bg.fillRoundedRectangle (cap.expanded (2.0f), corner + 2.0f);
        bg.setColour (juce::Colours::white.withAlpha (w->onCream ? 0.5f : 0.16f));
        bg.drawRoundedRectangle (cap.expanded (2.6f).translated (0.0f, 0.6f), corner + 2.6f, 0.8f);
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
        const bool down = juce::KeyPress::isKeyCurrentlyDown (ch);
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
