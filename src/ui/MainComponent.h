#pragma once

#include "engine/Engine.h"
#include "ui/Widgets.h"

namespace valvebox::ui
{
/** The panel, laid out on a fixed 1500 x 1090 canvas (the original's proportions) that scales with the window. */
class MainComponent : public juce::Component, private juce::Timer
{
public:
    explicit MainComponent (Engine& engine);
    ~MainComponent() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void modifierKeysChanged (const juce::ModifierKeys&) override;
    void focusLost (FocusChangeType) override;
    void mouseDown (const juce::MouseEvent&) override;
    /** Before the window closes: settings. */
    void prepareToQuit();

    static constexpr int kWidth = 1500, kHeight = 1090;

private:
    struct Canvas : public juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        void paint (juce::Graphics& g) override { if (painter) painter (g); }
    };

    void timerCallback() override;
    void build();
    void layout();
    void paintPanel (juce::Graphics&);
    void paintBackground (juce::Graphics&, float scale);
    void pressKey (Key k);
    void releaseKey (Key k);
    void showHelp();
    void showAudioSettings();
    void loadSettings();
    void saveSettings();
    void runSelfTest (const juce::File& report);

    Engine& engine;
    Canvas canvas;
    juce::Image background;
    std::vector<std::unique_ptr<KeyWidget>> keys;
    std::array<KeyWidget*, kNumKeys> keyFor {};
    std::vector<std::unique_ptr<KnobWidget>> knobs;
    std::array<KnobWidget*, kNumKnobs> knobFor {};
    struct LampRef
    {
        std::unique_ptr<LampWidget> w;
        Lamp lamp;
        int index;
    };
    std::vector<LampRef> lamps;
    DialWidget dial;
    LcdWidget lcd;
    RibbonWidget ribbonW;
    SliderWidget sliderW;
    ValveWindow valves;
    PanelButtonLook buttonLook; // (declared before the buttons: it outlives them)
    juce::TextButton audioBtn { "AUDIO/MIDI" }, helpBtn { "HELP" };
    juce::String engineMessage;
    uint32_t engineMessageUntil = 0;
    int currentMode = 0, currentPage = 0;

    // the computer keyboard: keys held, and SHIFT from the modifier
    std::array<bool, 64> charHeld {};
    bool shiftFromKeyboard = false;
    std::unique_ptr<juce::PropertiesFile> settings;
};
} // namespace valvebox::ui
