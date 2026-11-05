#pragma once

// The panel's parts: aluminium knobs, the two 16-way selectors, extruded rubber keys that glow when lit, indicator
// lamps, the data dial, the amber dot-matrix LCD, the ribbon and slider, and the valve window.
//
// Shadows and key wells are static, so the panel background draws them (from faceArea() / knobArea()); the widgets
// draw only what moves or lights.

#include "../core/Machine.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace valvebox::ui
{
namespace colours
{
    const juce::Colour panel { 0xff1d4c9c };     // metallic royal blue
    const juce::Colour panelDark { 0xff12397c };
    const juce::Colour print { 0xfff3f6fb };     // white silk-screen
    const juce::Colour printDim { 0xffbfd0ea };
    const juce::Colour keyFace { 0xffe2e3e3 };   // the rubber keys: the top...
    const juce::Colour keySideTop { 0xffb8bbc0 }; // ...and the walls, lit from above
    const juce::Colour keySideBottom { 0xff8d9299 };
    const juce::Colour darkFace { 0xff26272b };
    const juce::Colour creamPanel { 0xffe6e3da }; // the display and transport sub-panels
    const juce::Colour creamInk { 0xff24272c };  // print on cream
    const juce::Colour keyInk { 0xff1b1d22 };
    const juce::Colour lit { 0xffff3a28 };       // a key lit from inside
    const juce::Colour ledRed { 0xffff3b2f }, ledOrange { 0xffff9a1f }, ledGreen { 0xff52ff5a }, ledYellow { 0xffffd84a };
    const juce::Colour lcdBack { 0xffffa63c }, lcdInk { 0xff2a1305 };
} // namespace colours

/** The panel lettering: condensed, bold italic. */
juce::Font panelFont (float h, bool bold = false);
/** The big numbers printed on keys. */
juce::Font keyFont (float h);
void print (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float h, juce::Justification j = juce::Justification::centred,
            bool dim = false, bool bold = false);
void printInk (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float h, juce::Colour ink,
               juce::Justification j = juce::Justification::centred, bool bold = true, int maxLines = 1);
/** A white tag with blue lettering (boxed knob labels, section bands). */
void printTag (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float h);

/** A knob 0..127 (bipolar knobs show a centre mark), its label above it. Drag up/down, or scroll. */
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
    /** Where the knob itself is, in this component. */
    virtual juce::Rectangle<float> knobArea() const;
    /** The knob's size; 0 = as big as the component allows. */
    float diameter = 0.0f;
    /** The label in a white tag (as on the synth sections). */
    bool boxedLabel = false;
    static constexpr float kLabelHeight = 17.0f;
    /** Bounds that put a knob of diameter d at (cx, cy) with its label above. */
    static juce::Rectangle<int> boundsFor (float cx, float cy, float d, float width = 0.0f);

protected:
    juce::String label;
    bool bipolar;
    int value = 64, dragStart = 64;
};

/** A 16-way rotary selector with its choices printed round it (FX TYPE, OSC TYPE). */
class SelectorWidget : public KnobWidget
{
public:
    SelectorWidget (const juce::String& label, juce::StringArray choices);
    void paint (juce::Graphics&) override;
    juce::Rectangle<float> knobArea() const override;
    int getIndex() const { return std::clamp (value * 16 / 128, 0, 15); }

private:
    juce::StringArray choices;
};

/** An extruded rubber key: a flat top standing proud of the panel on visible walls; pressing it sinks it. Its
    function is printed on top (an icon on the transport keys), the SHIFT function above it (below the step keys).
    Keys with a light glow red from inside when lit. */
class KeyWidget : public juce::Component
{
public:
    enum class Style
    {
        Pill,      // small function keys
        DarkPill,  // KEYBOARD
        Square,    // part keys, TAP, PART MUTE, SOLO
        Step,      // the step keys...
        StepSharp, // ...the ones on a keyboard's black notes have a black label plate
        Transport, // the black-faced keys with symbols: RESET, REC, STOP, PLAY
        Round      // SHIFT
    };
    KeyWidget (Key k, const juce::String& label, const juce::String& shiftLabel = {}, Style style = Style::Pill, bool hasLed = true);
    const Key key;
    std::function<void (Key)> onPress, onRelease;
    void setLed (Led l);
    void setLatched (bool b) { if (latched != b) { latched = b; repaint(); } }
    bool isLatched() const { return latched; }
    void setBlinkPhase (bool on) { if (blinkOn != on) { blinkOn = on; if (led == Led::Blink) repaint(); } }
    /** Modifier keys (SHIFT, PART MUTE, SOLO, PATTERN SET) latch with a click: a pointer can't hold two keys. */
    bool latching = false;
    /** Sits on a cream sub-panel: the print around it is dark. */
    bool onCream = false;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::String getLabel() const { return label; }
    /** The key's top face at rest, in this component. */
    juce::Rectangle<float> faceArea() const;
    float faceCorner() const;
    /** How far the key stands out of the panel. */
    float extrusion() const;
    Style getStyle() const { return style; }
    /** Component bounds that put this key's top face at `face` (in the parent). */
    juce::Rectangle<int> boundsFor (juce::Rectangle<float> face) const;

private:
    void paintLabel (juce::Graphics&, juce::Rectangle<float> face, bool on);
    bool labelBelow() const { return style == Style::Step || style == Style::StepSharp; }
    static constexpr float kMargin = 7.0f, kTopLabel = 15.0f, kBottomLabel = 30.0f;
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
    bool onCream = false;
    /** Bounds that put the LED's centre at (x, y), with room for its label to the right. */
    static juce::Rectangle<int> boundsFor (float x, float y, float labelWidth);

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
    juce::Rectangle<float> knobArea() const;

private:
    float angle = 0.0f, wheelAcc = 0.0f;
    int lastY = 0;
};

/** The backlit LCD: two lines of 8 dot-matrix characters (parameter, value), the indicators and input meter beside
    them, and the part status row (the part numbers are printed on the panel under it). */
class LcdWidget : public juce::Component
{
public:
    void setDisplay (const Display& d, bool blinkPhase);
    void paint (juce::Graphics&) override;
    /** Where each of the 16 part status segments sits (x, in this component), for the print under the LCD. */
    float segmentCentre (int part) const;

private:
    juce::Rectangle<float> statusArea() const;
    Display shown;
    bool blinkOn = true;
};

/** The ribbon controller: a tall touch strip; where you touch, from the bottom (0) to the top (1). */
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

/** The arpeggiator slider, with its scale. */
class SliderWidget : public juce::Component
{
public:
    std::function<void (float)> onMove;
    void setValue (float v) { value = std::clamp (v, 0.0f, 1.0f); repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override;

private:
    juce::Rectangle<float> slotArea() const;
    float value = 0.5f;
};

/** The software buttons (AUDIO/MIDI, HELP): white silk-screen outlines in the panel lettering. */
class PanelButtonLook : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
};

/** The oval window and the two glowing tubes behind its glass; the glow follows TUBE GAIN and the output. */
class ValveWindow : public juce::Component
{
public:
    void setGlow (float tubeGain, float level);
    void paint (juce::Graphics&) override;

private:
    float gain = 0.0f, lvl = 0.0f;
};
} // namespace valvebox::ui
