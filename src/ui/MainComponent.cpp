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

    const juce::Rectangle<int> kMatrixArea { 350, 244, 352, 112 };

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
    setSize (1440, 900);
    if (auto* forced = std::getenv ("VALVEBOX_WINDOW_SIZE"))
    {
        const auto wh = juce::StringArray::fromTokens (forced, "x", {});
        if (wh.size() == 2)
            setSize (wh[0].getIntValue(), wh[1].getIntValue());
    }
    startTimerHz (30);
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
    auto key = [this] (Key k, const juce::String& label, const juce::String& shiftLabel = {}, KeyWidget::Style st = KeyWidget::Style::Grey,
                       bool led = true) {
        auto w = std::make_unique<KeyWidget> (k, label, shiftLabel, st, led);
        w->onPress = [this] (Key kk) { pressKey (kk); };
        w->onRelease = [this] (Key kk) { releaseKey (kk); };
        canvas.addAndMakeVisible (*w);
        keyFor[(size_t) k] = w.get();
        keys.push_back (std::move (w));
    };
    // common section
    key (Key::ResetErase, "RESET", "ERASE", KeyWidget::Style::Grey, false);
    key (Key::Rec, "REC", {}, KeyWidget::Style::Red);
    key (Key::StopCancel, "STOP", "CANCEL", KeyWidget::Style::Grey, false);
    key (Key::PlayPause, "PLAY/PAUSE", {}, KeyWidget::Style::Grey);
    key (Key::Tap, "TAP", {}, KeyWidget::Style::Grey, false);
    key (Key::PartMute, "PART MUTE", "REST/TIE");
    key (Key::Solo, "SOLO", "TRANSPOSE");
    key (Key::AudioInThru, "AUDIO IN THRU");
    key (Key::TempoLock, "TEMPO LOCK");
    key (Key::AutoBpm, "AUTO BPM SCAN");
    key (Key::WriteRename, "WRITE", "RENAME");
    key (Key::CursorLeft, "<", {}, KeyWidget::Style::Grey, false);
    key (Key::CursorRight, ">", {}, KeyWidget::Style::Grey, false);
    const Key modeKeys[6] = { Key::ModePattern, Key::ModePartEdit, Key::ModeStepEdit, Key::ModeSong, Key::ModeGlobal, Key::ModeMidi };
    for (int i = 0; i < 6; ++i)
        key (modeKeys[i], kModeNames[i]);
    // edit section
    key (Key::FxChain, "FX CHAIN", {}, KeyWidget::Style::Grey, false);
    key (Key::FxEditSelect, "EDIT SELECT", {}, KeyWidget::Style::Grey, false);
    key (Key::FxMotion, "MOTION SEQ");
    key (Key::PartMotion, "MOTION SEQ", {}, KeyWidget::Style::Grey, false);
    key (Key::AmpEg, "AMP EG", {}, KeyWidget::Style::Grey, false);
    key (Key::Roll, "ROLL");
    key (Key::FxSelect, "FX SELECT", {}, KeyWidget::Style::Grey, false);
    key (Key::FxSend, "FX SEND");
    key (Key::ModType, "TYPE", {}, KeyWidget::Style::Grey, false);
    key (Key::ModDest, "DEST", {}, KeyWidget::Style::Grey, false);
    key (Key::ModBpmSync, "BPM SYNC");
    key (Key::FilterType, "TYPE", {}, KeyWidget::Style::Grey, false);
    // parts
    const char* drumNames[9] = { "1", "2", "3", "4", "5", "6A", "6B", "7A", "7B" };
    for (int i = 0; i < 9; ++i)
        key ((Key) ((int) Key::Drum1 + i), drumNames[i], {}, KeyWidget::Style::Blue);
    key (Key::DrumAccent, "ACCENT", {}, KeyWidget::Style::Blue);
    for (int i = 0; i < 5; ++i)
        key ((Key) ((int) Key::Synth1 + i), juce::String (i + 1), {}, KeyWidget::Style::Blue);
    key (Key::SynthAccent, "ACCENT", {}, KeyWidget::Style::Blue);
    key (Key::Keyboard, "KEYBOARD");
    key (Key::SelectLeft, "<", {}, KeyWidget::Style::Grey, false);
    key (Key::SelectRight, ">", {}, KeyWidget::Style::Grey, false);
    key (Key::Shift, "SHIFT");
    key (Key::PatternSet, "PATTERN SET");
    const char* shiftFns[16] = { "LAST STEP", "MOVE DATA", "SHIFT NOTE", "COPY PART", "COPY SOUND", "CLR MOTION", "CLEAR PART", "CLEAR PTN",
                                 "INSERT PTN", "DELETE PTN", "CLEAR SONG", "MIDI UTIL", "CARD", "", "", "" };
    const char* notes[16] = { "A", "A#", "B", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "C" };
    for (int i = 0; i < 16; ++i)
        key ((Key) ((int) Key::Step1 + i), juce::String (i + 1) + " " + notes[i], shiftFns[i], isBlackStep (i) ? KeyWidget::Style::Black : KeyWidget::Style::White);
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
    place (kn (Knob::MasterVolume), 40, 72, 92, 100);
    place (kn (Knob::TubeGain), 137, 72, 92, 100);
    place (kn (Knob::AudioInLevel), 234, 72, 92, 100);
    place (&valves, 52, 186, 262, 150);
    place (k (Key::AudioInThru), 40, 352, 92, 66);
    place (k (Key::TempoLock), 137, 352, 92, 66);
    place (k (Key::AutoBpm), 234, 352, 92, 66);

    // ---- the display, dial, matrix menu and modes
    place (&lcd, 350, 70, 352, 166);
    place (&dial, 716, 66, 100, 100);
    place (lampAt (Lamp::Beat, 0), 716, 168, 60, 14);
    place (k (Key::CursorLeft), 712, 184, 52, 56);
    place (k (Key::CursorRight), 766, 184, 52, 56);
    place (k (Key::WriteRename), 712, 266, 106, 74);
    for (int i = 0; i < 6; ++i)
        place (k ((Key) ((int) Key::ModePattern + i)), 350 + i * 59, 360, 59, 66);

    // ---- EFFECT
    place (kn (Knob::FxType), 836, 82, 150, 160);
    place (kn (Knob::FxEdit1), 990, 76, 54, 82);
    place (kn (Knob::FxEdit2), 1044, 76, 54, 82);
    place (k (Key::FxChain), 990, 166, 54, 56);
    place (lampAt (Lamp::ChainOneTwo, 0), 990, 226, 30, 12);
    place (lampAt (Lamp::ChainTwoThree, 0), 1018, 226, 30, 12);
    place (k (Key::FxEditSelect), 1044, 166, 54, 56);
    for (int i = 0; i < 3; ++i)
        place (lampAt (Lamp::EditSelect, i), 1046 + i * 18, 226, 18, 12);

    // ---- MODULATION: knobs, keys, and the type / destination lamps beside them
    place (kn (Knob::ModSpeed), 1110, 76, 62, 82);
    place (kn (Knob::ModDepth), 1172, 76, 62, 82);
    place (k (Key::ModType), 1110, 166, 42, 56);
    place (k (Key::ModDest), 1154, 166, 42, 56);
    place (k (Key::ModBpmSync), 1198, 166, 42, 56);
    for (int i = 0; i < 5; ++i)
        place (lampAt (Lamp::ModTypeLamp, i), 1244, 78 + i * 14, 40, 13);
    for (int i = 0; i < 6; ++i)
        place (lampAt (Lamp::ModDestLamp, i), 1284, 78 + i * 14, 46, 13);

    // ---- SYNTH OSCILLATOR
    place (kn (Knob::OscType), 1342, 82, 150, 160);
    place (kn (Knob::OscEdit1), 1500, 76, 72, 82);
    place (kn (Knob::OscEdit2), 1500, 160, 72, 82);

    // ---- PART COMMON
    place (k (Key::PartMotion), 836, 280, 70, 60);
    place (lampAt (Lamp::MotionSmooth, 0), 838, 342, 70, 12);
    place (lampAt (Lamp::MotionTrigHold, 0), 838, 356, 70, 12);
    place (kn (Knob::PitchGlide), 912, 270, 70, 86);
    place (kn (Knob::EgTime), 984, 270, 70, 86);
    place (kn (Knob::Pan), 1056, 270, 70, 86);
    place (kn (Knob::Level), 1128, 270, 70, 86);
    place (k (Key::AmpEg), 912, 360, 66, 52);
    place (lampAt (Lamp::AmpDecay, 0), 914, 414, 34, 12);
    place (lampAt (Lamp::AmpGate, 0), 946, 414, 34, 12);
    place (k (Key::Roll), 984, 360, 66, 52);
    place (k (Key::FxSelect), 1056, 360, 66, 52);
    for (int i = 0; i < 3; ++i)
        place (lampAt (Lamp::FxSelectLamp, i), 1056 + i * 22, 414, 22, 12);
    place (k (Key::FxSend), 1128, 360, 66, 52);
    place (k (Key::FxMotion), 836, 382, 70, 56); // (EFFECT's MOTION SEQ, under the part's)

    // ---- SYNTH FILTER
    place (kn (Knob::Cutoff), 1218, 270, 68, 86);
    place (kn (Knob::EgInt), 1288, 270, 68, 86);
    place (kn (Knob::Resonance), 1358, 270, 68, 86);
    place (kn (Knob::Drive), 1428, 270, 68, 86);
    place (k (Key::FilterType), 1500, 280, 70, 60);
    for (int i = 0; i < 4; ++i)
        place (lampAt (Lamp::FilterTypeLamp, i), 1504, 346 + i * 15, 70, 14);

    // ---- transport and parts
    const Key transport[7] = { Key::ResetErase, Key::Rec, Key::StopCancel, Key::PlayPause, Key::Tap, Key::PartMute, Key::Solo };
    for (int i = 0; i < 7; ++i)
        place (k (transport[i]), 40 + i * 76, 470, 74, 92);
    for (int i = 0; i < 10; ++i)
        place (k (i < 9 ? (Key) ((int) Key::Drum1 + i) : Key::DrumAccent), 600 + i * 56, 488, 54, 74);
    for (int i = 0; i < 6; ++i)
        place (k (i < 5 ? (Key) ((int) Key::Synth1 + i) : Key::SynthAccent), 1180 + i * 56, 488, 54, 74);
    place (k (Key::Keyboard), 1514, 470, 62, 92);

    // ---- step keys, select keys and LEDs, SHIFT, PATTERN SET
    place (k (Key::Shift), 40, 760, 74, 92);
    place (k (Key::PatternSet), 120, 760, 84, 92);
    place (k (Key::SelectLeft), 226, 620, 58, 56);
    place (k (Key::SelectRight), 750, 620, 58, 56);
    for (int i = 0; i < 8; ++i)
    {
        place (lampAt (Lamp::SelectGreen, i), 300 + i * 56, 626, 14, 14);
        place (lampAt (Lamp::SelectRed, i), 300 + i * 56, 652, 14, 14);
    }
    for (int i = 0; i < 16; ++i)
        place (k ((Key) ((int) Key::Step1 + i)), 226 + i * 68, 700, 66, 190);

    // ---- the arpeggiator
    place (&ribbonW, 1340, 760, 196, 70);
    place (&sliderW, 1540, 620, 40, 270);
}

// ---------------------------------------------------------------- painting

void MainComponent::paint (juce::Graphics& g) { g.fillAll (juce::Colour (0xff0d0e10)); }

void MainComponent::resized()
{
    // the panel keeps its proportions; the buttons sit in the top-right of the panel
    const float sc = std::min (getWidth() / (float) kWidth, getHeight() / (float) kHeight);
    const float ox = (getWidth() - kWidth * sc) * 0.5f, oy = (getHeight() - kHeight * sc) * 0.5f;
    canvas.setTransform (juce::AffineTransform::scale (sc).translated (ox, oy));
    audioBtn.setBounds ((int) (ox + 1390 * sc), (int) (oy + 18 * sc), (int) (104 * sc), (int) (26 * sc));
    helpBtn.setBounds ((int) (ox + 1500 * sc), (int) (oy + 18 * sc), (int) (70 * sc), (int) (26 * sc));
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
        // the end cheeks and the metallic blue panel
        bg.setColour (juce::Colour (0xff1a1b1e));
        bg.fillRect (0, 0, kWidth, kHeight);
        auto pa = juce::Rectangle<float> (18, 10, kWidth - 36, kHeight - 20);
        const auto& tile = materials::cached ("vbpaint", [=] { return materials::paintTile (colours::panel, 256, 0.9f, 11); });
        bg.setFillType (juce::FillType (tile, juce::AffineTransform::scale (1.0f / sc)));
        bg.fillRoundedRectangle (pa, 10.0f);
        // brushed metal: fine horizontal streaks, and a sheen from the top
        juce::Random rng (5);
        for (int y = (int) pa.getY(); y < (int) pa.getBottom(); y += 2)
        {
            bg.setColour (juce::Colours::white.withAlpha (0.015f + 0.02f * rng.nextFloat()));
            bg.drawHorizontalLine (y, pa.getX() + 4, pa.getRight() - 4);
        }
        bg.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f), 0, pa.getY(), juce::Colours::transparentWhite, 0, pa.getY() + 340, false));
        bg.fillRoundedRectangle (pa, 10.0f);
        bg.setColour (juce::Colours::black.withAlpha (0.45f));
        bg.drawRoundedRectangle (pa, 10.0f, 1.5f);
        for (auto pt : { juce::Point<float> (32, 24), juce::Point<float> (kWidth - 32.0f, 24), juce::Point<float> (32, kHeight - 24.0f),
                         juce::Point<float> (kWidth - 32.0f, kHeight - 24.0f) })
        {
            const int px = (int) std::ceil (10 * sc);
            materials::drawAt (bg, materials::cached ("vbscrew:" + juce::String (px), [=] { return materials::screw (px, 0.4f); }),
                               juce::Rectangle<float> (10, 10).withCentre (pt));
        }
        // the name
        juce::GlyphArrangement ga;
        ga.addLineOfText (panelFont (30.0f, true), "valvebox", 0, 0);
        juce::Path word;
        ga.createPath (word);
        word.applyTransform (juce::AffineTransform::translation (46, 50));
        bg.setColour (colours::print);
        bg.fillPath (word);
        print (bg, "VB-16   GROOVE PRODUCTION STATION", { 190, 26, 420, 26 }, 13.0f, juce::Justification::centredLeft, false, true);
        // section boxes and titles
        auto box = [&] (juce::Rectangle<float> r, const juce::String& title) {
            bg.setColour (juce::Colours::black.withAlpha (0.13f));
            bg.fillRoundedRectangle (r, 6.0f);
            bg.setColour (colours::print.withAlpha (0.55f));
            bg.drawRoundedRectangle (r, 6.0f, 1.0f);
            if (title.isNotEmpty())
            {
                auto t = juce::Rectangle<float> (r.getX() + 10, r.getY() - 8, (float) title.length() * 7.0f + 16, 16);
                bg.setColour (colours::panel);
                bg.fillRect (t);
                print (bg, title, t, 10.5f, juce::Justification::centred, false, true);
            }
        };
        box ({ 830, 64, 272, 182 }, "EFFECT");
        box ({ 1104, 64, 230, 182 }, "MODULATION");
        box ({ 1336, 64, 244, 182 }, "SYNTH OSCILLATOR");
        box ({ 830, 258, 372, 182 }, "PART COMMON");
        box ({ 1206, 258, 374, 182 }, "SYNTH FILTER");
        box ({ 592, 462, 580, 108 }, "DRUM PART");
        box ({ 1172, 462, 340, 108 }, "SYNTH PART");
        box ({ 1330, 600, 250, 290 }, "ARPEGGIATOR");
        print (bg, "FX MOTION", { 836, 438, 70, 12 }, 7.5f, juce::Justification::centred, true);
        // select LED numbers, the step key row's title
        for (int i = 0; i < 8; ++i)
            print (bg, juce::String (i + 1), { 300.0f + i * 56 + 16, 626, 20, 40 }, 10.0f, juce::Justification::centredLeft, true);
        print (bg, "PLAYING", { 760.0f - 0, 600, 60, 14 }, 8.0f, juce::Justification::centredLeft, true);
        print (bg, "STEP KEYS  /  KEYBOARD", { 226, 680, 300, 16 }, 10.0f, juce::Justification::centredLeft, false, true);
        print (bg, "valve drive by twin triodes", { 52, 336, 262, 14 }, 8.5f, juce::Justification::centred, true);
    }
    g.drawImageTransformed (background, juce::AffineTransform::scale (1.0f / sc));

    // the matrix menu: the current page lit
    auto m = kMatrixArea.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillRoundedRectangle (m, 4.0f);
    const float cw = m.getWidth() / 6.0f, rh = (m.getHeight() - 14.0f) / 7.0f;
    for (int c = 0; c < 6; ++c)
    {
        print (g, kModeNames[c], { m.getX() + c * cw, m.getY() + 1, cw, 12 }, 8.0f, juce::Justification::centred, c != currentMode, true);
        for (int r = 0; r < 7 && kMatrix[c][r] != nullptr; ++r)
        {
            auto cell = juce::Rectangle<float> (m.getX() + c * cw, m.getY() + 14 + r * rh, cw, rh);
            if (c == currentMode && r == currentPage)
            {
                g.setColour (colours::print.withAlpha (0.85f));
                g.fillRoundedRectangle (cell.reduced (2.0f, 0.5f), 2.0f);
                g.setColour (colours::panelDark);
                g.setFont (panelFont (9.0f, true));
                g.drawText (kMatrix[c][r], cell, juce::Justification::centred, false);
            }
            else
                print (g, kMatrix[c][r], cell, 9.0f, juce::Justification::centred, true);
        }
    }
    // a line for messages from the engine (MIDI devices, dumps) under the display
    if (engineMessage.isNotEmpty() && juce::Time::getMillisecondCounter() < engineMessageUntil)
        print (g, engineMessage, { 350, 430, 470, 16 }, 10.0f, juce::Justification::centredLeft, false, true);
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
        canvas.repaint (350, 428, 470, 20);
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
    if (std::getenv ("VALVEBOX_SELFTEST") != nullptr)
        return; // the self-test leaves the user's settings alone
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
