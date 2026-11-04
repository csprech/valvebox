#pragma once

// The panel's parts: knobs, the two 16-way selectors, rubber keys with LEDs, indicator lamps, the
// endless dial, the LCD, the ribbon and slider, and the oval valve window.

#include "core/Machine.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace valvebox::ui
{
namespace colours
{
    const juce::Colour panel { 0xff2b5d9a };   // metallic blue
    const juce::Colour panelDark { 0xff1d426f };
    const juce::Colour print { 0xffe9eef5 };   // silk-screen
    const juce::Colour printDim { 0xffaab9cc };
    const juce::Colour ledRed { 0xffff3b2f }, ledOrange { 0xffff9a1f }, ledGreen { 0xff52ff5a }, ledYellow { 0xffffd84a };
    const juce::Colour keyGrey { 0xff3a3d42 }, keyWhite { 0xffdfe2e6 }, keyBlack { 0xff1b1c1f }, keyBlue { 0xff6f8fb7 };
    const juce::Colour lcdBack { 0xffb6d36b }, lcdInk { 0xff16200d };
} // namespace colours

juce::Font panelFont (float h, bool bold = false);
juce::Font lcdFont (float h);
void print (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float h, juce::Justification j = juce::Justification::centred,
            bool dim = false, bool bold = false);

/** A knob 0..127 (bipolar knobs show a centre mark). Drag up/down, or scroll. */
class KnobWidget : public juce::Component
{
public:
    KnobWidget (const juce::String& label, bool bipolar = false);
    std::function<void (int)> onChange;
    void setValue (int v);
    int getValue() const { return value; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::String getLabel() const { return label; }

protected:
    juce::Rectangle<float> knobArea() const;
    juce::String label;
    bool bipolar;
    int value = 64, dragStart = 64;
};

/** A 16-way rotary selector with its choices printed beside it (FX TYPE, OSC TYPE). */
class SelectorWidget : public KnobWidget
{
public:
    SelectorWidget (const juce::String& label, juce::StringArray choices);
    void paint (juce::Graphics&) override;
    int getIndex() const { return std::clamp (value * 16 / 128, 0, 15); }

private:
    juce::StringArray choices;
};

/** A rubber key, with an LED (optional) and its function printed above, SHIFT function below. */
class KeyWidget : public juce::Component
{
public:
    enum class Style { Grey, White, Black, Blue, Red };
    KeyWidget (Key k, const juce::String& label, const juce::String& shiftLabel = {}, Style style = Style::Grey, bool hasLed = true);
    const Key key;
    std::function<void (Key)> onPress, onRelease;
    void setLed (Led l);
    void setLatched (bool b) { if (latched != b) { latched = b; repaint(); } }
    bool isLatched() const { return latched; }
    void setBlinkPhase (bool on) { if (blinkOn != on) { blinkOn = on; if (led == Led::Blink) repaint(); } }
    /** Modifier keys (SHIFT, PART MUTE, SOLO, PATTERN SET) latch with a click: a pointer can't hold two keys. */
    bool latching = false;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::String getLabel() const { return label; }

private:
    juce::String label, shiftLabel;
    Style style;
    bool hasLed;
    Led led = Led::Off;
    bool latched = false, down = false, blinkOn = true;
};

/** An indicator LED with a label beside or above it. */
class LampWidget : public juce::Component
{
public:
    LampWidget (const juce::String& text, juce::Colour c = colours::ledRed, bool labelAbove = false)
        : label (text), colour (c), above (labelAbove) { setInterceptsMouseClicks (false, false); }
    void setState (Led l) { if (l != state) { state = l; repaint(); } }
    void setBlinkPhase (bool on) { if (blinkOn != on) { blinkOn = on; if (state == Led::Blink) repaint(); } }
    void paint (juce::Graphics&) override;

private:
    juce::String label;
    juce::Colour colour;
    bool above;
    Led state = Led::Off;
    bool blinkOn = true;
};

/** The endless data dial: drag or scroll; each detent is one click. */
class DialWidget : public juce::Component
{
public:
    std::function<void (int)> onTurn;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    float angle = 0.0f, wheelAcc = 0.0f;
    int lastY = 0;
};

/** The backlit LCD: parameter, value, the indicators, the input meter, the part status rows. */
class LcdWidget : public juce::Component
{
public:
    void setDisplay (const Display& d, bool blinkPhase);
    void paint (juce::Graphics&) override;

private:
    Display shown;
    bool blinkOn = true;
};

/** Ribbon controller (touch + position) and the arpeggiator slider. */
class RibbonWidget : public juce::Component
{
public:
    std::function<void (bool touching, float pos)> onTouch;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { touch (e, true); }
    void mouseDrag (const juce::MouseEvent& e) override { touch (e, true); }
    void mouseUp (const juce::MouseEvent& e) override { touch (e, false); }

private:
    void touch (const juce::MouseEvent& e, bool on);
    float pos = 0.5f;
    bool touching = false;
};

class SliderWidget : public juce::Component
{
public:
    std::function<void (float)> onMove;
    void setValue (float v) { value = std::clamp (v, 0.0f, 1.0f); repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override;

private:
    float value = 0.5f;
};

/** The oval window and its two glowing tubes; the glow follows TUBE GAIN and the output. */
class ValveWindow : public juce::Component
{
public:
    void setGlow (float tubeGain, float level);
    void paint (juce::Graphics&) override;

private:
    float gain = 0.0f, lvl = 0.0f;
};
} // namespace valvebox::ui
