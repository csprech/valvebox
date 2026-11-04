#include "ui/Widgets.h"
#include "ui/Materials.h"

namespace valvebox::ui
{
juce::Font panelFont (float h, bool bold)
{
    static const auto names = juce::Font::findAllTypefaceNames();
    for (auto* n : { "Avenir Next Condensed", "Helvetica Neue", "Arial" })
        if (names.contains (n))
            return juce::Font (juce::FontOptions (n, h, bold ? juce::Font::bold : juce::Font::plain));
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font lcdFont (float h)
{
    static const auto names = juce::Font::findAllTypefaceNames();
    for (auto* n : { "Menlo", "Courier New" })
        if (names.contains (n))
            return juce::Font (juce::FontOptions (n, h, juce::Font::bold));
    return juce::Font (juce::FontOptions (h, juce::Font::bold));
}

void print (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float h, juce::Justification j, bool dim, bool bold)
{
    g.setFont (panelFont (h, bold));
    g.setColour (dim ? colours::printDim : colours::print);
    g.drawFittedText (t, r.toNearestInt(), j, 2, 0.8f);
}

// ---------------------------------------------------------------- knob

namespace
{
    constexpr float kStart = -2.4f, kEnd = 2.4f;

    /** Knob images, cached by size and angle (128 positions round the scale). */
    const juce::Image& knobImage (int px, float angle, juce::Colour cap = juce::Colour (0xff1a1a1a))
    {
        const int step = (int) std::lround (angle / (kEnd - kStart) * 127.0f);
        return materials::cached ("vbknob:" + juce::String (px) + ":" + juce::String (step) + ":" + juce::String ((int) cap.getARGB()),
                                  [=] { return materials::knob (px, step * (kEnd - kStart) / 127.0f, cap); });
    }
}

KnobWidget::KnobWidget (const juce::String& l, bool bi) : label (l), bipolar (bi) {}

void KnobWidget::setValue (int v)
{
    v = std::clamp (v, 0, 127);
    if (v != value)
    {
        value = v;
        repaint();
    }
}

juce::Rectangle<float> KnobWidget::knobArea() const
{
    auto r = getLocalBounds().toFloat();
    r.removeFromBottom (16.0f);
    const float d = std::min (r.getWidth(), r.getHeight()) * 0.78f;
    return juce::Rectangle<float> (d, d).withCentre (r.getCentre());
}

void KnobWidget::paint (juce::Graphics& g)
{
    const auto k = knobArea();
    const auto c = k.getCentre();
    const float R = k.getWidth() * 0.5f;
    // scale: dots round the knob, a bigger one at the centre for bipolar knobs
    g.setColour (colours::print.withAlpha (0.8f));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = kStart + (kEnd - kStart) * i / 10.0f;
        const float rr = (bipolar && i == 5) || i == 0 || i == 10 ? 2.0f : 1.2f;
        g.fillEllipse (juce::Rectangle<float> (rr * 2, rr * 2).withCentre ({ c.x + std::sin (a) * R * 1.22f, c.y - std::cos (a) * R * 1.22f }));
    }
    materials::roundShadow (g, c, R * 0.95f, R * 0.5f);
    const float angle = kStart + (kEnd - kStart) * value / 127.0f;
    const float sc = materials::scaleOf (g);
    const int px = (int) std::ceil (k.getWidth() * sc);
    materials::drawAt (g, knobImage (px, angle), k);
    print (g, label, getLocalBounds().toFloat().removeFromBottom (16.0f), 10.5f, juce::Justification::centred, false, true);
}

void KnobWidget::mouseDown (const juce::MouseEvent&) { dragStart = value; }

void KnobWidget::mouseDrag (const juce::MouseEvent& e)
{
    const int v = std::clamp (dragStart - (int) std::lround (e.getDistanceFromDragStartY() * (e.mods.isShiftDown() ? 0.25f : 0.8f)), 0, 127);
    if (v != value)
    {
        setValue (v);
        if (onChange)
            onChange (value);
    }
}

void KnobWidget::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const int step = w.deltaY > 0 ? 1 : w.deltaY < 0 ? -1 : 0;
    if (step == 0)
        return;
    setValue (value + step * (std::abs (w.deltaY) > 0.2f ? 4 : 1));
    if (onChange)
        onChange (value);
}

// ---------------------------------------------------------------- selector

SelectorWidget::SelectorWidget (const juce::String& l, juce::StringArray c) : KnobWidget (l), choices (std::move (c)) {}

void SelectorWidget::paint (juce::Graphics& g)
{
    // the knob on the left, the 16 choices in a column beside it
    auto r = getLocalBounds().toFloat();
    auto list = r.removeFromRight (r.getWidth() * 0.48f);
    const auto k = juce::Rectangle<float> (std::min (r.getWidth(), r.getHeight() - 18.0f) * 0.8f, std::min (r.getWidth(), r.getHeight() - 18.0f) * 0.8f)
                       .withCentre (r.withTrimmedBottom (18.0f).getCentre());
    const auto c = k.getCentre();
    const float R = k.getWidth() * 0.5f;
    materials::roundShadow (g, c, R * 0.95f, R * 0.5f);
    const float angle = kStart + (kEnd - kStart) * (getIndex() + 0.5f) / 16.0f;
    const float sc = materials::scaleOf (g);
    materials::drawAt (g, knobImage ((int) std::ceil (k.getWidth() * sc), angle), k);
    g.setColour (colours::print.withAlpha (0.8f));
    for (int i = 0; i < 16; ++i)
    {
        const float a = kStart + (kEnd - kStart) * (i + 0.5f) / 16.0f;
        g.fillEllipse (juce::Rectangle<float> (2.4f, 2.4f).withCentre ({ c.x + std::sin (a) * R * 1.2f, c.y - std::cos (a) * R * 1.2f }));
    }
    print (g, label, r.removeFromBottom (16.0f), 10.5f, juce::Justification::centred, false, true);
    const float rowH = list.getHeight() / 16.0f;
    for (int i = 0; i < 16 && i < choices.size(); ++i)
    {
        auto row = juce::Rectangle<float> (list.getX(), list.getY() + i * rowH, list.getWidth(), rowH);
        if (i == getIndex())
        {
            g.setColour (colours::print.withAlpha (0.22f));
            g.fillRoundedRectangle (row.reduced (0, 0.5f), 2.0f);
        }
        print (g, choices[i], row.withTrimmedLeft (3.0f), std::min (10.0f, rowH * 0.95f), juce::Justification::centredLeft, i != getIndex(), i == getIndex());
    }
}

// ---------------------------------------------------------------- key

KeyWidget::KeyWidget (Key k, const juce::String& l, const juce::String& sl, Style s, bool withLed) : key (k), label (l), shiftLabel (sl), style (s), hasLed (withLed) {}

void KeyWidget::setLed (Led l)
{
    if (l != led)
    {
        led = l;
        repaint();
    }
}

void KeyWidget::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const bool printAbove = label.isNotEmpty() && style != Style::White && style != Style::Black;
    if (printAbove)
        print (g, label, r.removeFromTop (std::min (26.0f, r.getHeight() * 0.36f)), 9.5f, juce::Justification::centredBottom, false, true);
    if (shiftLabel.isNotEmpty())
        print (g, shiftLabel, r.removeFromBottom (14.0f), 8.0f, juce::Justification::centredTop, true);
    auto cap = r.reduced (2.0f, 3.0f);
    if (style != Style::White && style != Style::Black)
        cap = cap.withSizeKeepingCentre (std::min (cap.getWidth(), 58.0f), std::min (cap.getHeight(), 30.0f));
    // the bezel and the key
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (cap.expanded (2.0f), 5.0f);
    const juce::Colour albedo = style == Style::White ? colours::keyWhite : style == Style::Black ? colours::keyBlack
                              : style == Style::Blue ? colours::keyBlue : style == Style::Red ? juce::Colour (0xffa83a33) : colours::keyGrey;
    const float sc = materials::scaleOf (g);
    const int w = (int) std::ceil (cap.getWidth() * sc), h = (int) std::ceil (cap.getHeight() * sc);
    const auto keyName = "vbkey:" + juce::String (w) + "x" + juce::String (h) + ":" + juce::String ((int) style);
    const auto& img = materials::cached (keyName, [=] {
        materials::Shading s { 0.38f, 0.62f, 0.18f, 18.0f, 0.03f, 0.08f, 1.0f };
        return materials::pillow (w, h, 4.0f * sc, std::min (w, h) * 0.2f, 0.7f * sc, albedo, s, 3);
    });
    materials::drawAt (g, img, cap.translated (0, (down || latched) ? 1.0f : 0.0f));
    if (style == Style::White || style == Style::Black)
    {
        g.setFont (panelFont (10.0f, true));
        g.setColour (style == Style::White ? juce::Colour (0xff30343a) : colours::print);
        g.drawFittedText (label, cap.reduced (2.0f).removeFromBottom (16.0f).toNearestInt(), juce::Justification::centred, 1);
    }
    if (latched)
    {
        g.setColour (colours::ledYellow.withAlpha (0.8f));
        g.drawRoundedRectangle (cap.expanded (1.0f), 5.0f, 1.5f);
    }
    if (hasLed)
    {
        const bool on = led == Led::On || (led == Led::Blink && blinkOn);
        const float d = 8.0f;
        auto lr = juce::Rectangle<float> (d, d).withCentre ({ cap.getCentreX(), style == Style::White || style == Style::Black ? cap.getY() + 9.0f : cap.getCentreY() });
        const juce::Colour c = key == Key::PlayPause || isStepKey (key) ? colours::ledRed : colours::ledOrange;
        if (on)
        {
            juce::ColourGradient glow (c.withAlpha (0.55f), lr.getCentreX(), lr.getCentreY(), c.withAlpha (0.0f), lr.getCentreX() + 12.0f, lr.getCentreY(), true);
            g.setGradientFill (glow);
            g.fillEllipse (lr.expanded (12.0f));
        }
        const int lp = (int) std::ceil (d * sc);
        materials::drawAt (g, materials::cached ("vbled:" + juce::String (lp) + ":" + juce::String ((int) c.getARGB()) + (on ? "1" : "0"),
                                                 [=] { return materials::led (lp, c, on); }),
                           lr);
    }
}

void KeyWidget::mouseDown (const juce::MouseEvent&)
{
    if (latching)
    {
        latched = ! latched;
        repaint();
        if (latched ? (bool) onPress : (bool) onRelease)
            (latched ? onPress : onRelease) (key);
        return;
    }
    down = true;
    repaint();
    if (onPress)
        onPress (key);
}

void KeyWidget::mouseUp (const juce::MouseEvent&)
{
    if (latching)
        return;
    down = false;
    repaint();
    if (onRelease)
        onRelease (key);
}

// ---------------------------------------------------------------- lamp

void LampWidget::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float d = 7.0f;
    juce::Rectangle<float> lr;
    if (above)
    {
        print (g, label, r.removeFromTop (r.getHeight() - d - 4.0f), 8.5f, juce::Justification::centredBottom, true);
        lr = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
    }
    else
    {
        lr = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + d * 0.5f + 2.0f, r.getCentreY() });
        print (g, label, r.withTrimmedLeft (d + 6.0f), 8.5f, juce::Justification::centredLeft, true);
    }
    const bool on = state == Led::On || (state == Led::Blink && blinkOn);
    if (on)
    {
        juce::ColourGradient glow (colour.withAlpha (0.6f), lr.getCentreX(), lr.getCentreY(), colour.withAlpha (0.0f), lr.getCentreX() + 10.0f, lr.getCentreY(), true);
        g.setGradientFill (glow);
        g.fillEllipse (lr.expanded (10.0f));
    }
    const float sc = materials::scaleOf (g);
    const int lp = (int) std::ceil (d * sc);
    const auto c = colour;
    materials::drawAt (g, materials::cached ("vblamp:" + juce::String (lp) + ":" + juce::String ((int) c.getARGB()) + (on ? "1" : "0"),
                                             [lp, c, on] { return materials::led (lp, c, on); }),
                       lr);
}

// ---------------------------------------------------------------- dial

void DialWidget::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (4.0f);
    const float d = std::min (r.getWidth(), r.getHeight());
    auto k = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
    materials::roundShadow (g, k.getCentre(), d * 0.48f, d * 0.2f);
    const float sc = materials::scaleOf (g);
    const float wrapped = std::fmod (angle, (kEnd - kStart)); // (the dial's knurl repeats: a few angles are enough)
    materials::drawAt (g, knobImage ((int) std::ceil (d * sc), wrapped, juce::Colour (0xff2a2c31)), k);
    // the finger dimple
    const float rr = d * 0.30f;
    auto dimple = juce::Rectangle<float> (d * 0.16f, d * 0.16f).withCentre ({ k.getCentreX() + std::sin (angle) * rr, k.getCentreY() - std::cos (angle) * rr });
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (dimple);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawEllipse (dimple.translated (0.5f, 0.8f), 1.0f);
}

void DialWidget::mouseDown (const juce::MouseEvent& e) { lastY = e.y; }

void DialWidget::mouseDrag (const juce::MouseEvent& e)
{
    const int step = e.mods.isShiftDown() ? 14 : 7; // px a detent
    while (lastY - e.y >= step)
    {
        lastY -= step;
        angle += 0.2f;
        if (onTurn)
            onTurn (1);
    }
    while (e.y - lastY >= step)
    {
        lastY += step;
        angle -= 0.2f;
        if (onTurn)
            onTurn (-1);
    }
    repaint();
}

void DialWidget::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    wheelAcc += w.deltaY * (w.isReversed ? -1.0f : 1.0f) * 12.0f;
    while (wheelAcc >= 1.0f)
    {
        wheelAcc -= 1.0f;
        angle += 0.2f;
        if (onTurn)
            onTurn (1);
    }
    while (wheelAcc <= -1.0f)
    {
        wheelAcc += 1.0f;
        angle -= 0.2f;
        if (onTurn)
            onTurn (-1);
    }
    repaint();
}

// ---------------------------------------------------------------- LCD

void LcdWidget::setDisplay (const Display& d, bool blink)
{
    const bool changed = d.param != shown.param || d.value != shown.value || d.blink != shown.blink || d.knobInd != shown.knobInd
                         || d.originalInd != shown.originalInd || d.patternInd != shown.patternInd || d.songInd != shown.songInd
                         || d.waveInd != shown.waveInd || d.status != shown.status || d.statusRow != shown.statusRow || d.cursor != shown.cursor
                         || std::abs (d.inputLevel - shown.inputLevel) > 0.02f || d.clip != shown.clip || (d.blink && blink != blinkOn)
                         || (d.cursor >= 0 && blink != blinkOn);
    shown = d;
    blinkOn = blink;
    if (changed)
        repaint();
}

void LcdWidget::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    // bezel, glass, backlight
    g.setColour (juce::Colour (0xff0c0d10));
    g.fillRoundedRectangle (r, 6.0f);
    auto glass = r.reduced (7.0f);
    juce::ColourGradient back (colours::lcdBack.brighter (0.15f), glass.getX(), glass.getY(), colours::lcdBack.darker (0.15f), glass.getX(), glass.getBottom(), false);
    g.setGradientFill (back);
    g.fillRoundedRectangle (glass, 3.0f);
    auto inner = glass.reduced (10.0f, 6.0f);
    const auto ink = colours::lcdInk;
    const auto ghost = colours::lcdInk.withAlpha (0.06f);

    // the 8-character parameter display (dot-matrix), with the ghost of unlit cells
    auto top = inner.removeFromTop (inner.getHeight() * 0.42f);
    auto paramArea = top.removeFromLeft (top.getWidth() * 0.62f);
    const float cell = paramArea.getWidth() / 8.0f;
    g.setFont (lcdFont (std::min (paramArea.getHeight() * 0.7f, cell * 1.45f)));
    for (int i = 0; i < 8; ++i)
    {
        auto c = juce::Rectangle<float> (paramArea.getX() + i * cell, paramArea.getY(), cell, paramArea.getHeight()).reduced (1.5f, 2.0f);
        g.setColour (ghost);
        g.fillRect (c);
        const juce::String ch = i < shown.param.length() ? shown.param.substring (i, i + 1) : " ";
        const bool hide = shown.blink && ! blinkOn;
        g.setColour (ink);
        if (! hide)
            g.drawFittedText (ch, c.toNearestInt(), juce::Justification::centred, 1, 0.3f);
        if (shown.cursor == i && blinkOn)
            g.fillRect (c.removeFromBottom (3.0f));
    }
    // the value display (big)
    auto valueArea = top.reduced (8.0f, 0.0f);
    g.setColour (ghost);
    g.fillRect (valueArea);
    g.setColour (ink);
    g.setFont (lcdFont (valueArea.getHeight() * 0.8f));
    g.drawFittedText (shown.value, valueArea.toNearestInt(), juce::Justification::centredRight, 1, 0.5f);

    // indicators
    auto row = inner.removeFromTop (16.0f);
    g.setFont (panelFont (10.0f, true));
    auto ind = [&] (const juce::String& t, bool on, float w) {
        auto c = row.removeFromLeft (w);
        g.setColour (on ? ink : ghost.withAlpha (0.18f));
        g.drawText (t, c, juce::Justification::centredLeft, false);
    };
    ind ("PATTERN", shown.patternInd, 60);
    ind ("SONG", shown.songInd, 42);
    ind ("WAVE", shown.waveInd, 42);
    ind ("KNOB", shown.knobInd, 40);
    ind ("ORIGINAL", shown.originalInd, 66);
    // the input meter and CLIP
    auto meter = row.reduced (4.0f, 4.0f);
    auto clip = meter.removeFromRight (30.0f);
    for (int i = 0; i < 12; ++i)
    {
        auto seg = juce::Rectangle<float> (meter.getX() + i * meter.getWidth() / 12.0f, meter.getY(), meter.getWidth() / 12.0f - 2.0f, meter.getHeight());
        g.setColour (shown.inputLevel * 12.0f > i ? ink : ghost);
        g.fillRect (seg);
    }
    g.setColour (shown.clip ? ink : ghost.withAlpha (0.18f));
    g.drawText ("CLIP", clip, juce::Justification::centredRight, false);

    // part status: four rows (PLAY, SWING, ACCENT, OUTPUT L/R), the one shown marked
    inner.removeFromTop (4.0f);
    static const char* rows[4] = { "PLAY", "SWING", "ACCENT", "OUT L/R" };
    auto st = inner;
    auto labels = st.removeFromLeft (60.0f);
    const float rowH = st.getHeight() / 4.0f;
    for (int rr = 0; rr < 4; ++rr)
    {
        g.setColour (rr == shown.statusRow ? ink : ink.withAlpha (0.35f));
        g.setFont (panelFont (9.0f, true));
        g.drawText (rows[rr], juce::Rectangle<float> (labels.getX(), st.getY() + rr * rowH, labels.getWidth(), rowH), juce::Justification::centredLeft, false);
        const float cw = st.getWidth() / 16.0f;
        for (int p = 0; p < 16; ++p)
        {
            auto c = juce::Rectangle<float> (st.getX() + p * cw, st.getY() + rr * rowH, cw, rowH).reduced (cw * 0.28f, rowH * 0.25f);
            g.setColour (rr == shown.statusRow && shown.status[(size_t) p] ? ink : ghost);
            g.fillRect (c);
        }
    }
    // a faint reflection on the glass
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.10f), glass.getX(), glass.getY(), juce::Colours::transparentWhite,
                                             glass.getX(), glass.getY() + glass.getHeight() * 0.5f, false));
    g.fillRoundedRectangle (glass, 3.0f);
}

// ---------------------------------------------------------------- ribbon and slider

void RibbonWidget::touch (const juce::MouseEvent& e, bool on)
{
    pos = std::clamp ((float) e.x / (float) std::max (1, getWidth()), 0.0f, 1.0f);
    touching = on;
    repaint();
    if (onTouch)
        onTouch (on, pos);
}

void RibbonWidget::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    print (g, "RIBBON", r.removeFromBottom (14.0f), 9.0f, juce::Justification::centred, true, true);
    auto strip = r.reduced (2.0f, 4.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (strip.expanded (2.0f), 6.0f);
    juce::ColourGradient body (juce::Colour (0xff2a2b2f), 0, strip.getY(), juce::Colour (0xff121315), 0, strip.getBottom(), false);
    g.setGradientFill (body);
    g.fillRoundedRectangle (strip, 5.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    for (int i = 1; i < 8; ++i)
        g.drawVerticalLine ((int) (strip.getX() + strip.getWidth() * i / 8.0f), strip.getY() + 4.0f, strip.getBottom() - 4.0f);
    if (touching)
    {
        const float x = strip.getX() + pos * strip.getWidth();
        juce::ColourGradient glow (colours::ledOrange.withAlpha (0.7f), x, strip.getCentreY(), colours::ledOrange.withAlpha (0.0f), x + 26.0f, strip.getCentreY(), true);
        g.setGradientFill (glow);
        g.fillRoundedRectangle (strip, 5.0f);
    }
}

void SliderWidget::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    print (g, "SLIDER", r.removeFromBottom (14.0f), 9.0f, juce::Justification::centred, true, true);
    auto slot = r.withSizeKeepingCentre (6.0f, r.getHeight() - 16.0f);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillRoundedRectangle (slot, 3.0f);
    const float y = slot.getBottom() - value * slot.getHeight();
    auto cap = juce::Rectangle<float> (r.getWidth() * 0.7f, 16.0f).withCentre ({ slot.getCentreX(), y });
    const float sc = materials::scaleOf (g);
    const int w = (int) std::ceil (cap.getWidth() * sc), h = (int) std::ceil (cap.getHeight() * sc);
    const auto& img = materials::cached ("vbfader:" + juce::String (w) + "x" + juce::String (h), [=] {
        return materials::pillow (w, h, 3.0f * sc, h * 0.3f, 0.4f * sc, juce::Colour (0xff2d2f34), materials::plastic(), 9);
    });
    materials::drawAt (g, img, cap);
    g.setColour (colours::print);
    g.fillRect (cap.withSizeKeepingCentre (cap.getWidth() - 8.0f, 1.5f));
}

void SliderWidget::mouseDrag (const juce::MouseEvent& e)
{
    const auto slot = getLocalBounds().toFloat().withTrimmedBottom (14.0f).reduced (0.0f, 8.0f);
    setValue ((slot.getBottom() - (float) e.y) / slot.getHeight());
    if (onMove)
        onMove (value);
}

// ---------------------------------------------------------------- the valves

void ValveWindow::setGlow (float tubeGain, float level)
{
    const float g = std::clamp (tubeGain, 0.0f, 1.0f), l = std::clamp (level, 0.0f, 1.0f);
    if (std::abs (g - gain) > 0.01f || std::abs (l - lvl) > 0.03f)
    {
        gain = g;
        lvl = l;
        repaint();
    }
}

void ValveWindow::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (4.0f);
    // the chrome ring and the dark window
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (r.translated (1.5f, 2.5f));
    juce::ColourGradient ring (juce::Colour (0xffd9dee6), r.getX(), r.getY(), juce::Colour (0xff6c7480), r.getX(), r.getBottom(), false);
    g.setGradientFill (ring);
    g.fillEllipse (r);
    auto win = r.reduced (6.0f);
    g.setColour (juce::Colour (0xff07080a));
    g.fillEllipse (win);
    // two tubes: glass envelopes, plates, and the heater glow (brighter with TUBE GAIN and signal)
    const float glow = 0.35f + 0.45f * gain + 0.25f * lvl;
    for (int t = 0; t < 2; ++t)
    {
        const float cx = win.getX() + win.getWidth() * (t == 0 ? 0.34f : 0.66f);
        auto tube = juce::Rectangle<float> (win.getWidth() * 0.2f, win.getHeight() * 0.78f).withCentre ({ cx, win.getCentreY() + 2.0f });
        juce::ColourGradient heat (juce::Colour (0xffff8a2a).withAlpha (glow), cx, tube.getCentreY() + tube.getHeight() * 0.1f, juce::Colour (0x00ff6a00), cx,
                                   tube.getCentreY() + tube.getHeight() * 0.6f, true);
        g.setGradientFill (heat);
        g.fillEllipse (tube.expanded (tube.getWidth() * 0.8f, 6.0f));
        g.setColour (juce::Colour (0xff3b3e44));
        g.fillRoundedRectangle (tube.withTrimmedTop (tube.getHeight() * 0.3f).reduced (tube.getWidth() * 0.18f, tube.getHeight() * 0.1f), 2.0f); // plates
        g.setColour (juce::Colour (0xffff9a3a).withAlpha (glow));
        g.fillRect (tube.withSizeKeepingCentre (2.5f, tube.getHeight() * 0.35f).translated (0, tube.getHeight() * 0.12f)); // the glowing filament
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.drawRoundedRectangle (tube, tube.getWidth() * 0.45f, 1.2f); // the glass
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.fillRoundedRectangle (tube.withWidth (tube.getWidth() * 0.18f).translated (tube.getWidth() * 0.18f, 6.0f).withTrimmedBottom (tube.getHeight() * 0.3f), 2.0f);
    }
    // glass reflection over the window
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.14f), win.getX(), win.getY(), juce::Colours::transparentWhite, win.getX(),
                                             win.getCentreY(), false));
    g.fillEllipse (win);
    print (g, "VALVE DRIVE", r.withTrimmedTop (r.getHeight() - 22.0f), 8.0f, juce::Justification::centred, true);
}
} // namespace valvebox::ui
