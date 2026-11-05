#include "ui/Widgets.h"
#include "ui/DotFont.h"
#include "ui/Materials.h"

namespace valvebox::ui
{
namespace
{
    bool haveCondensed()
    {
        static const bool have = juce::Font::findAllTypefaceNames().contains ("Avenir Next Condensed");
        return have;
    }
    juce::Font condensed (float h, const char* style, int fallbackFlags)
    {
        if (haveCondensed())
            return juce::Font (juce::FontOptions ("Avenir Next Condensed", style, h));
        return juce::Font (juce::FontOptions (h, fallbackFlags));
    }
} // namespace

juce::Font panelFont (float h, bool bold)
{
    return condensed (h, bold ? "Bold Italic" : "Demi Bold Italic", juce::Font::italic | (bold ? juce::Font::bold : 0));
}

juce::Font keyFont (float h) { return condensed (h, "Heavy Italic", juce::Font::bold | juce::Font::italic); }

void print (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float h, juce::Justification j, bool dim, bool bold)
{
    printInk (g, t, r, h, dim ? colours::printDim : colours::print, j, bold, 2);
}

void printInk (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float h, juce::Colour ink, juce::Justification j, bool bold,
               int maxLines)
{
    g.setFont (panelFont (h, bold));
    g.setColour (ink);
    g.drawFittedText (t, r.toNearestInt(), j, maxLines, 0.75f);
}

namespace
{
    constexpr float kStart = -2.4f, kEnd = 2.4f;

    /** Aluminium knob images, cached by size and angle (128 positions round the scale). */
    const juce::Image& knobImage (int px, float angle, bool knurled = false)
    {
        const int step = (int) std::lround (angle / (kEnd - kStart) * 127.0f);
        return materials::cached ("vbalu:" + juce::String (px) + ":" + juce::String (step) + (knurled ? "k" : ""),
                                  [=] { return materials::metalKnob (px, step * (kEnd - kStart) / 127.0f, knurled); });
    }

    void scaleDots (juce::Graphics& g, juce::Point<float> c, float R, int n, bool centreMark, juce::Colour ink, bool between = false)
    {
        g.setColour (ink.withAlpha (0.9f));
        for (int i = 0; i < n; ++i)
        {
            const float t = between ? (i + 0.5f) / (float) n : (float) i / (float) (n - 1);
            const float a = kStart + (kEnd - kStart) * t;
            const bool big = ! between && (i == 0 || i == n - 1 || (centreMark && i == n / 2));
            const float rr = big ? 1.9f : 1.15f;
            g.fillEllipse (juce::Rectangle<float> (rr * 2, rr * 2).withCentre ({ c.x + std::sin (a) * R, c.y - std::cos (a) * R }));
        }
    }
} // namespace

// ---------------------------------------------------------------- knob

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
    r.removeFromBottom (17.0f);
    const float d = std::min (r.getWidth(), r.getHeight()) * 0.70f;
    return juce::Rectangle<float> (d, d).withCentre (r.getCentre());
}

void KnobWidget::paint (juce::Graphics& g)
{
    const auto k = knobArea();
    const auto c = k.getCentre();
    const float R = k.getWidth() * 0.5f;
    scaleDots (g, c, R * 1.24f, 11, bipolar, colours::print);
    const float angle = kStart + (kEnd - kStart) * value / 127.0f;
    const float sc = materials::scaleOf (g);
    materials::drawAt (g, knobImage ((int) std::ceil (k.getWidth() * sc), angle), k);
    print (g, label, getLocalBounds().toFloat().removeFromBottom (17.0f), 11.5f, juce::Justification::centred, false, true);
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

juce::Rectangle<float> SelectorWidget::knobArea() const
{
    auto r = getLocalBounds().toFloat();
    r.removeFromRight (r.getWidth() * 0.46f);
    r.removeFromBottom (18.0f);
    const float d = std::min (r.getWidth(), r.getHeight()) * 0.72f;
    return juce::Rectangle<float> (d, d).withCentre (r.getCentre());
}

void SelectorWidget::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto list = r.removeFromRight (r.getWidth() * 0.46f);
    const auto k = knobArea();
    const float R = k.getWidth() * 0.5f;
    scaleDots (g, k.getCentre(), R * 1.2f, 16, false, colours::print, true);
    const float angle = kStart + (kEnd - kStart) * (getIndex() + 0.5f) / 16.0f;
    const float sc = materials::scaleOf (g);
    materials::drawAt (g, knobImage ((int) std::ceil (k.getWidth() * sc), angle), k);
    print (g, label, r.removeFromBottom (18.0f), 11.5f, juce::Justification::centred, false, true);
    const float rowH = list.getHeight() / 16.0f;
    for (int i = 0; i < 16 && i < choices.size(); ++i)
    {
        auto row = juce::Rectangle<float> (list.getX(), list.getY() + i * rowH, list.getWidth(), rowH);
        if (i == getIndex())
        {
            g.setColour (colours::print);
            g.fillRoundedRectangle (row.reduced (0, 0.4f), rowH * 0.45f);
            printInk (g, choices[i], row.withTrimmedLeft (5.0f), std::min (11.0f, rowH * 1.05f), colours::panelDark, juce::Justification::centredLeft);
        }
        else
            printInk (g, choices[i], row.withTrimmedLeft (5.0f), std::min (11.0f, rowH * 1.05f), colours::print.withAlpha (0.86f),
                      juce::Justification::centredLeft, false);
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

juce::Rectangle<float> KeyWidget::capArea() const
{
    auto r = getLocalBounds().toFloat();
    const bool step = style == Style::Step || style == Style::StepSharp;
    if (step)
        r.removeFromBottom (30.0f); // the SHIFT function under it
    else if (shiftLabel.isNotEmpty())
        r.removeFromTop (17.0f);
    float w = 0, h = 0;
    switch (style)
    {
        case Style::Pill: w = std::min (r.getWidth() - 4.0f, 98.0f); h = std::min (r.getHeight() - 4.0f, 25.0f); break;
        case Style::Square: w = std::min (r.getWidth() - 6.0f, 54.0f); h = std::min (r.getHeight() - 6.0f, 60.0f); break;
        case Style::Step: case Style::StepSharp: w = std::min (r.getWidth() - 6.0f, 60.0f); h = std::min (r.getHeight() - 4.0f, 58.0f); break;
        case Style::Transport: w = std::min (r.getWidth() - 6.0f, 66.0f); h = std::min (r.getHeight() - 6.0f, 50.0f); break;
        case Style::Round: w = h = std::min ({ r.getWidth(), r.getHeight(), 50.0f }) - 4.0f; break;
    }
    return juce::Rectangle<float> (w, h).withCentre (r.getCentre());
}

float KeyWidget::capCorner() const
{
    const auto c = capArea();
    if (style == Style::Round)
        return c.getWidth() * 0.5f;
    if (style == Style::Pill)
        return std::min (6.0f, c.getHeight() * 0.3f);
    return 7.0f;
}

namespace
{
    juce::Path triangle (juce::Rectangle<float> r, bool pointsRight)
    {
        juce::Path p;
        if (pointsRight)
            p.addTriangle (r.getX(), r.getY(), r.getRight(), r.getCentreY(), r.getX(), r.getBottom());
        else
            p.addTriangle (r.getRight(), r.getY(), r.getX(), r.getCentreY(), r.getRight(), r.getBottom());
        return p;
    }

    /** The transport symbols: |<  (o)  []  >/|| */
    juce::Path transportIcon (Key k, juce::Rectangle<float> cap)
    {
        juce::Path p;
        const auto c = cap.getCentre();
        const float s = std::min (cap.getWidth(), cap.getHeight()) * 0.38f;
        if (k == Key::ResetErase)
        {
            p.addRectangle (c.x - s * 0.62f, c.y - s * 0.5f, s * 0.16f, s);
            p.addPath (triangle ({ c.x - s * 0.40f, c.y - s * 0.5f, s * 0.95f, s }, false));
        }
        else if (k == Key::Rec)
            p.addEllipse (juce::Rectangle<float> (s * 0.95f, s * 0.95f).withCentre (c));
        else if (k == Key::StopCancel)
            p.addRectangle (juce::Rectangle<float> (s * 0.85f, s * 0.85f).withCentre (c));
        else if (k == Key::PlayPause)
        {
            {
                const float x0 = c.x - s * 1.05f;
                p.addPath (triangle ({ x0, c.y - s * 0.5f, s * 0.8f, s }, true));
                juce::Path slash;
                slash.startNewSubPath (x0 + s * 1.12f, c.y + s * 0.55f);
                slash.lineTo (x0 + s * 1.42f, c.y - s * 0.55f);
                juce::PathStrokeType (s * 0.12f).createStrokedPath (slash, slash);
                p.addPath (slash);
                p.addRectangle (x0 + s * 1.6f, c.y - s * 0.5f, s * 0.18f, s);
                p.addRectangle (x0 + s * 1.92f, c.y - s * 0.5f, s * 0.18f, s);
            }
        }
        return p;
    }

    const juce::Image& keyCapImage (int w, int h, float cornerPx, KeyWidget::Style style, float sc)
    {
        const bool dark = style == KeyWidget::Style::Transport;
        return materials::cached ("vbcap:" + juce::String (w) + "x" + juce::String (h) + ":" + juce::String ((int) style), [=] {
            materials::Shading s = dark ? materials::Shading { 0.40f, 0.62f, 0.20f, 12.0f, 0.04f, 0.16f, 0.9f }
                                        : materials::Shading { 0.50f, 0.52f, 0.13f, 10.0f, 0.03f, 0.20f, 0.8f };
            return materials::pillow (w, h, cornerPx, std::min (w, h) * (style == KeyWidget::Style::Round ? 0.32f : 0.22f), 1.1f * sc,
                                      dark ? colours::transportKey : colours::cream, s, dark ? 5u : 3u);
        });
    }
} // namespace

void KeyWidget::paint (juce::Graphics& g)
{
    const bool step = style == Style::Step || style == Style::StepSharp;
    const auto rest = capArea();
    const bool pressed = down || latched;
    const auto cap = rest.translated (0.0f, pressed ? 1.2f : 0.0f);
    const float corner = capCorner();
    const auto outsideInk = onCream ? colours::creamInk : colours::print;

    // the SHIFT function: above the key in a small tag, or under a step key
    if (shiftLabel.isNotEmpty())
    {
        if (step)
            printInk (g, shiftLabel, getLocalBounds().toFloat().removeFromBottom (30.0f).withTrimmedTop (2.0f), 10.0f, outsideInk.withAlpha (0.92f),
                      juce::Justification::centredTop, true, 2);
        else
            printInk (g, shiftLabel, juce::Rectangle<float> (rest.getX() - 8.0f, rest.getY() - 17.0f, rest.getWidth() + 16.0f, 14.0f), 9.5f,
                      outsideInk.withAlpha (0.9f), juce::Justification::centredBottom, true);
    }

    const float sc = materials::scaleOf (g);
    const int w = (int) std::ceil (cap.getWidth() * sc), h = (int) std::ceil (cap.getHeight() * sc);
    materials::drawAt (g, keyCapImage (w, h, corner * sc, style, sc), cap);
    if (pressed)
    {
        g.setColour (juce::Colours::black.withAlpha (0.07f));
        g.fillRoundedRectangle (cap, corner);
    }
    const bool on = hasLed && (led == Led::On || (led == Led::Blink && blinkOn));
    paintLabel (g, cap, on);
    if (on && style != Style::Transport)
    {
        // a red LED under translucent rubber: hot in the middle, a pink wash to the edges
        juce::ColourGradient glow (colours::lit.withAlpha (0.50f), cap.getCentreX(), cap.getCentreY(), colours::lit.withAlpha (0.16f),
                                   cap.getRight(), cap.getBottom(), true);
        g.setGradientFill (glow);
        g.fillRoundedRectangle (cap, corner);
    }
    if (latched)
    {
        g.setColour (colours::ledOrange.withAlpha (0.95f));
        g.drawRoundedRectangle (cap.expanded (2.6f), corner + 2.6f, 1.8f);
    }
}

void KeyWidget::paintLabel (juce::Graphics& g, juce::Rectangle<float> cap, bool on)
{
    if (style == Style::Transport)
    {
        const auto icon = transportIcon (key, cap);
        const auto col = on ? (key == Key::PlayPause ? colours::ledGreen : colours::lit) : colours::cream.withAlpha (0.92f);
        if (on)
        {
            juce::ColourGradient bloom (col.withAlpha (0.45f), cap.getCentreX(), cap.getCentreY(), col.withAlpha (0.0f), cap.getCentreX() + cap.getWidth() * 0.5f,
                                        cap.getCentreY(), true);
            g.setGradientFill (bloom);
            g.fillRoundedRectangle (cap, 7.0f);
        }
        else if (key == Key::Rec)
        {
            g.setColour (juce::Colour (0xffd8352b)); // the REC symbol is printed red
            g.fillPath (icon);
            return;
        }
        g.setColour (col);
        g.fillPath (icon);
        return;
    }
    const auto ink = colours::keyInk;
    if (label == "<" || label == ">")
    {
        g.setColour (ink);
        g.fillPath (triangle (juce::Rectangle<float> (cap.getHeight() * 0.36f, cap.getHeight() * 0.42f).withCentre (cap.getCentre()), label == ">"));
        return;
    }
    if (style == Style::StepSharp)
    {
        // a black plate behind the number, like a keyboard's black key
        auto plate = cap.reduced (cap.getWidth() * 0.2f, cap.getHeight() * 0.2f);
        g.setColour (juce::Colour (0xff17181b));
        g.fillRoundedRectangle (plate, 3.0f);
        g.setColour (colours::cream);
        g.setFont (keyFont (cap.getHeight() * 0.44f));
        g.drawText (label, plate, juce::Justification::centred, false);
        return;
    }
    const bool big = style == Style::Step || (style == Style::Square && label.length() <= 3);
    if (big)
    {
        g.setColour (ink);
        g.setFont (keyFont (cap.getHeight() * 0.46f));
        g.drawText (label, cap.translated (-1.0f, 0.0f), juce::Justification::centred, false);
        return;
    }
    const float h = style == Style::Pill ? std::min (11.0f, cap.getHeight() * 0.5f) : 11.5f;
    printInk (g, label, cap.reduced (4.0f, 2.0f), h, ink, juce::Justification::centred, true, style == Style::Pill ? 1 : 2);
    juce::ignoreUnused (on);
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
    const auto ink = (onCream ? colours::creamInk : colours::print).withAlpha (0.9f);
    juce::Rectangle<float> lr;
    if (above)
    {
        printInk (g, label, r.removeFromTop (r.getHeight() - d - 4.0f), 9.5f, ink, juce::Justification::centredBottom);
        lr = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
    }
    else
    {
        lr = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + d * 0.5f + 2.0f, r.getCentreY() });
        printInk (g, label, r.withTrimmedLeft (d + 6.0f), 9.5f, ink, juce::Justification::centredLeft);
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

juce::Rectangle<float> DialWidget::knobArea() const
{
    auto r = getLocalBounds().toFloat();
    const float d = std::min (r.getWidth(), r.getHeight()) * 0.9f;
    return juce::Rectangle<float> (d, d).withCentre (r.getCentre());
}

void DialWidget::paint (juce::Graphics& g)
{
    const auto k = knobArea();
    const float d = k.getWidth();
    const float sc = materials::scaleOf (g);
    // the knurl repeats every few degrees, so a few cached angles are enough
    const float wrapped = std::fmod (std::fmod (angle, 0.5f) + 0.5f, 0.5f) + kStart;
    materials::drawAt (g, knobImage ((int) std::ceil (d * sc), wrapped, true), k);
    // the finger dimple: dark where the rim shades it from the light, a highlight on the far edge
    const float rr = d * 0.27f;
    auto dimple = juce::Rectangle<float> (d * 0.17f, d * 0.17f).withCentre ({ k.getCentreX() + std::sin (angle) * rr, k.getCentreY() - std::cos (angle) * rr });
    juce::ColourGradient shade (juce::Colours::black.withAlpha (0.42f), dimple.getX(), dimple.getY(), juce::Colours::white.withAlpha (0.28f),
                                dimple.getRight(), dimple.getBottom(), false);
    g.setGradientFill (shade);
    g.fillEllipse (dimple);
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.drawEllipse (dimple, 0.8f);
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

namespace
{
    /** Eight 5 x 7 character cells; every dot is drawn, lit or ghosted, like a real character LCD. */
    void dotLine (juce::Graphics& g, const juce::String& text, float x, float y, float pitch, juce::Colour ink, juce::Colour ghost, bool rightAlign,
                  bool hide, int cursor, bool cursorOn)
    {
        juce::String t = text.substring (0, 8);
        if (rightAlign)
            t = juce::String::repeatedString (" ", 8 - t.length()) + t;
        const float dot = pitch * 0.84f;
        for (int i = 0; i < 8; ++i)
        {
            const char ch = i < t.length() && ! hide ? (char) t[i] : ' ';
            const auto& gl = dotfont::glyph (ch);
            const float cx = x + i * 6.0f * pitch;
            for (int row = 0; row < 8; ++row)
                for (int col = 0; col < 5; ++col)
                {
                    const bool on = row < 7 ? dotfont::lit (gl, col, row) : (cursor == i && cursorOn);
                    if (row == 7 && ! on)
                        continue; // the cursor row only shows when it's there
                    g.setColour (on ? ink : ghost);
                    g.fillRect (cx + col * pitch, y + row * pitch, dot, dot);
                }
        }
    }
} // namespace

void LcdWidget::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    // the bezel
    g.setColour (juce::Colour (0xff121316));
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
    auto glass = r.reduced (6.0f);
    // the amber backlight, brightest in the middle
    juce::ColourGradient back (colours::lcdBack.brighter (0.22f), glass.getCentreX(), glass.getCentreY(), colours::lcdBack.darker (0.18f), glass.getX(),
                               glass.getY(), true);
    g.setGradientFill (back);
    g.fillRoundedRectangle (glass, 3.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.22f), 0, glass.getY(), juce::Colours::transparentBlack, 0, glass.getY() + 7.0f, false));
    g.fillRoundedRectangle (glass, 3.0f);

    auto in = glass.reduced (10.0f, 8.0f);
    const auto ink = colours::lcdInk;
    const auto ghost = colours::lcdInk.withAlpha (0.075f);
    const float pitch = std::min (in.getWidth() * 0.6f / 48.0f, in.getHeight() * 0.46f / 16.0f);

    // the two lines: parameter, value
    const bool hide = shown.blink && ! blinkOn;
    dotLine (g, shown.param, in.getX(), in.getY(), pitch, ink, ghost, false, hide, shown.cursor, blinkOn);
    dotLine (g, shown.value, in.getX(), in.getY() + 8.5f * pitch, pitch, ink, ghost, true, false, -1, false);

    // the indicators and the input meter, beside them
    auto side = juce::Rectangle<float> (in.getX() + 48.0f * pitch + 8.0f, in.getY(), in.getRight() - (in.getX() + 48.0f * pitch + 8.0f), 16.5f * pitch);
    const float rowH = side.getHeight() / 4.0f;
    auto indicator = [&] (const juce::String& t, bool on, juce::Rectangle<float> cell) {
        const auto c = on ? ink : ghost.withMultipliedAlpha (2.0f);
        g.setColour (c);
        g.fillPath (triangle (juce::Rectangle<float> (5.0f, 6.0f).withCentre ({ cell.getX() + 3.0f, cell.getCentreY() }), true));
        printInk (g, t, cell.withTrimmedLeft (9.0f), 10.5f, c, juce::Justification::centredLeft);
    };
    auto row0 = side.removeFromTop (rowH), row1 = side.removeFromTop (rowH), row2 = side.removeFromTop (rowH);
    indicator ("PATTERN", shown.patternInd, row0.removeFromLeft (row0.getWidth() * 0.58f));
    indicator ("SONG", shown.songInd, row0);
    indicator ("WAVE", shown.waveInd, row1.removeFromLeft (row1.getWidth() * 0.58f));
    indicator ("KNOB", shown.knobInd, row1);
    indicator ("ORIGINAL", shown.originalInd, row2);
    auto meter = side.reduced (0.0f, rowH * 0.22f);
    auto clip = meter.removeFromRight (30.0f);
    for (int i = 0; i < 12; ++i)
    {
        auto seg = juce::Rectangle<float> (meter.getX() + i * meter.getWidth() / 12.0f, meter.getY(), meter.getWidth() / 12.0f - 2.0f, meter.getHeight());
        g.setColour (shown.inputLevel * 12.0f > i ? ink : ghost.withMultipliedAlpha (1.5f));
        g.fillRect (seg);
    }
    printInk (g, "CLIP", clip, 10.5f, shown.clip ? ink : ghost.withMultipliedAlpha (2.0f), juce::Justification::centredRight);

    // part status: PLAY, SWING, ACCENT, OUT L/R (the one shown marked), and the part numbers under them
    auto st = in.withTrimmedTop (17.5f * pitch + 4.0f);
    auto numbers = st.removeFromBottom (12.0f);
    auto labels = st.removeFromLeft (50.0f);
    static const char* rows[4] = { "PLAY", "SWING", "ACCENT", "OUT L/R" };
    const float sh = st.getHeight() / 4.0f, cw = st.getWidth() / 16.0f;
    for (int rr = 0; rr < 4; ++rr)
    {
        const bool active = rr == shown.statusRow;
        printInk (g, rows[rr], { labels.getX(), st.getY() + rr * sh, labels.getWidth(), sh }, 10.0f, active ? ink : ink.withAlpha (0.32f),
                  juce::Justification::centredLeft);
        for (int p = 0; p < 16; ++p)
        {
            auto c = juce::Rectangle<float> (st.getX() + p * cw, st.getY() + rr * sh, cw, sh).reduced (cw * 0.22f, sh * 0.24f);
            g.setColour (active && shown.status[(size_t) p] ? ink : ghost);
            g.fillRect (c);
        }
    }
    static const char* parts[16] = { "1", "2", "3", "4", "5", "6A", "6B", "7A", "7B", "A", "1", "2", "3", "4", "5", "A" };
    for (int p = 0; p < 16; ++p)
        printInk (g, parts[p], { st.getX() + p * cw, numbers.getY(), cw, numbers.getHeight() }, 9.0f, ink.withAlpha (0.55f), juce::Justification::centred);
    g.setColour (ink.withAlpha (0.45f));
    g.drawHorizontalLine ((int) numbers.getY(), st.getX() + 2.0f, st.getX() + cw * 10.0f - 3.0f);
    g.drawHorizontalLine ((int) numbers.getY(), st.getX() + cw * 10.0f + 3.0f, st.getRight() - 2.0f);

    // a faint reflection on the glass
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.12f), glass.getX(), glass.getY(), juce::Colours::transparentWhite,
                                             glass.getX() + glass.getWidth() * 0.3f, glass.getY() + glass.getHeight() * 0.6f, false));
    g.fillRoundedRectangle (glass, 3.0f);
}

// ---------------------------------------------------------------- ribbon and slider

juce::Rectangle<float> RibbonWidget::padArea() const { return getLocalBounds().toFloat().withTrimmedBottom (18.0f).reduced (3.0f); }

void RibbonWidget::touch (const juce::MouseEvent& e, bool on)
{
    const auto pad = padArea();
    pos = std::clamp ((e.position.x - pad.getX()) / std::max (1.0f, pad.getWidth()), 0.0f, 1.0f);
    touching = on;
    repaint();
    if (onTouch)
        onTouch (on, pos);
}

void RibbonWidget::paint (juce::Graphics& g)
{
    const auto pad = padArea();
    print (g, "RIBBON", getLocalBounds().toFloat().removeFromBottom (16.0f), 11.0f, juce::Justification::centred, false, true);
    // a pale, matt touch strip, set slightly into the panel
    juce::ColourGradient body (colours::cream.darker (0.04f), 0, pad.getY(), colours::cream.darker (0.12f), 0, pad.getBottom(), false);
    g.setGradientFill (body);
    g.fillRoundedRectangle (pad, 5.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.20f), 0, pad.getY(), juce::Colours::transparentBlack, 0, pad.getY() + 8.0f, false));
    g.fillRoundedRectangle (pad, 5.0f);
    g.setColour (colours::creamInk.withAlpha (0.12f));
    for (int i = 1; i < 8; ++i)
        g.drawVerticalLine ((int) (pad.getX() + pad.getWidth() * i / 8.0f), pad.getBottom() - 10.0f, pad.getBottom() - 4.0f);
    if (touching)
    {
        const float x = pad.getX() + pos * pad.getWidth();
        juce::ColourGradient glow (colours::ledOrange.withAlpha (0.75f), x, pad.getCentreY(), colours::ledOrange.withAlpha (0.0f), x + 34.0f, pad.getCentreY(), true);
        g.setGradientFill (glow);
        g.fillRoundedRectangle (pad, 5.0f);
    }
}

juce::Rectangle<float> SliderWidget::slotArea() const
{
    auto r = getLocalBounds().toFloat().withTrimmedBottom (18.0f);
    return r.withSizeKeepingCentre (6.0f, r.getHeight() - 22.0f);
}

void SliderWidget::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    print (g, "SLIDER", r.removeFromBottom (16.0f), 11.0f, juce::Justification::centred, false, true);
    const auto slot = slotArea();
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.fillRoundedRectangle (slot, 3.0f);
    g.setColour (colours::print.withAlpha (0.7f));
    for (int i = 0; i <= 8; ++i)
        g.fillRect (slot.getRight() + 5.0f, slot.getY() + slot.getHeight() * i / 8.0f - 0.5f, i % 4 == 0 ? 8.0f : 5.0f, 1.0f);
    const float y = slot.getBottom() - value * slot.getHeight();
    auto cap = juce::Rectangle<float> (r.getWidth() * 0.78f, 20.0f).withCentre ({ slot.getCentreX(), y });
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (cap.translated (1.5f, 2.5f).expanded (1.0f), 4.0f);
    const float sc = materials::scaleOf (g);
    const int w = (int) std::ceil (cap.getWidth() * sc), h = (int) std::ceil (cap.getHeight() * sc);
    const auto& img = materials::cached ("vbfader2:" + juce::String (w) + "x" + juce::String (h), [=] {
        return materials::pillow (w, h, 3.5f * sc, h * 0.3f, 0.6f * sc, colours::cream, materials::Shading { 0.50f, 0.52f, 0.13f, 10.0f, 0.03f, 0.2f, 0.8f }, 9);
    });
    materials::drawAt (g, img, cap);
    g.setColour (colours::keyInk);
    g.fillRect (cap.withSizeKeepingCentre (cap.getWidth() - 10.0f, 2.0f));
}

void SliderWidget::mouseDrag (const juce::MouseEvent& e)
{
    const auto slot = slotArea();
    setValue ((slot.getBottom() - e.position.y) / slot.getHeight());
    if (onMove)
        onMove (value);
}

// ---------------------------------------------------------------- software buttons

void PanelButtonLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const float corner = r.getHeight() * 0.3f;
    g.setColour (colours::print.withAlpha (down ? 0.32f : highlighted ? 0.18f : 0.07f));
    g.fillRoundedRectangle (r, corner);
    g.setColour (colours::print.withAlpha (0.85f));
    g.drawRoundedRectangle (r, corner, 1.2f);
}

void PanelButtonLook::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    printInk (g, b.getButtonText(), b.getLocalBounds().toFloat().reduced (4.0f, 0.0f), b.getHeight() * 0.58f, colours::print);
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
    auto r = getLocalBounds().toFloat().reduced (3.0f).withTrimmedBottom (16.0f);
    // the chrome ring and the dark window
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (r.translated (1.5f, 3.0f));
    juce::ColourGradient ring (juce::Colour (0xffeef1f5), r.getX(), r.getY(), juce::Colour (0xff5c6470), r.getRight() * 0.7f, r.getBottom(), false);
    ring.addColour (0.55, juce::Colour (0xffa8b0bb));
    g.setGradientFill (ring);
    g.fillEllipse (r);
    auto win = r.reduced (7.0f);
    g.setColour (juce::Colour (0xff060709));
    g.fillEllipse (win);
    // two tubes: glass envelopes, plates, and the heater glow (brighter with TUBE GAIN and signal)
    const float glow = std::min (1.0f, 0.45f + 0.4f * gain + 0.25f * lvl);
    // the warm light the heaters throw round the inside of the window
    juce::ColourGradient room (juce::Colour (0xffff7a1e).withAlpha (0.22f + 0.3f * glow), win.getCentreX(), win.getCentreY() + win.getHeight() * 0.15f,
                               juce::Colour (0x00ff6a00), win.getCentreX() + win.getWidth() * 0.5f, win.getCentreY(), true);
    g.setGradientFill (room);
    g.fillEllipse (win);
    for (int t = 0; t < 2; ++t)
    {
        const float cx = win.getX() + win.getWidth() * (t == 0 ? 0.37f : 0.63f);
        auto tube = juce::Rectangle<float> (win.getWidth() * 0.18f, win.getHeight() * 0.84f).withCentre ({ cx, win.getCentreY() + 3.0f });
        // the glass envelope: a dome on top, faintly smoky
        juce::Path glass;
        glass.addRoundedRectangle (tube.getX(), tube.getY(), tube.getWidth(), tube.getHeight(), tube.getWidth() * 0.5f, tube.getWidth() * 0.5f, true, true, false, false);
        g.setColour (juce::Colour (0x20c8d0dc));
        g.fillPath (glass);
        // the plates: dark grey metal with a sheen
        auto plates = tube.withTrimmedTop (tube.getHeight() * 0.3f).withTrimmedBottom (tube.getHeight() * 0.12f).reduced (tube.getWidth() * 0.14f, 0.0f);
        juce::ColourGradient metal (juce::Colour (0xff4a4e56), plates.getX(), 0, juce::Colour (0xff1d1f23), plates.getRight(), 0, false);
        metal.addColour (0.3, juce::Colour (0xff6c717b));
        g.setGradientFill (metal);
        g.fillRoundedRectangle (plates, 1.5f);
        // the heater glowing through the gap between the plates
        auto gap = plates.withSizeKeepingCentre (plates.getWidth() * 0.22f, plates.getHeight() * 0.78f);
        juce::ColourGradient heat (juce::Colour (0xffffc060).withAlpha (glow), gap.getCentreX(), gap.getCentreY(), juce::Colour (0xffff6a10).withAlpha (glow * 0.6f),
                                   gap.getRight(), gap.getBottom(), true);
        g.setGradientFill (heat);
        g.fillRect (gap);
        juce::ColourGradient halo (juce::Colour (0xffff8a2a).withAlpha (0.55f * glow), gap.getCentreX(), gap.getCentreY(), juce::Colour (0x00ff6a00),
                                   gap.getCentreX() + tube.getWidth() * 1.2f, gap.getCentreY(), true);
        g.setGradientFill (halo);
        g.fillEllipse (gap.expanded (tube.getWidth() * 1.1f, tube.getWidth() * 0.9f));
        // the getter's silvering in the dome, the glass outline and its highlight
        g.setColour (juce::Colour (0x66b9bec6));
        g.fillEllipse (juce::Rectangle<float> (tube.getWidth() * 0.6f, tube.getHeight() * 0.07f).withCentre ({ cx, tube.getY() + tube.getHeight() * 0.16f }));
        g.setColour (juce::Colours::white.withAlpha (0.28f));
        g.strokePath (glass, juce::PathStrokeType (1.2f));
        g.setColour (juce::Colours::white.withAlpha (0.30f));
        g.fillRoundedRectangle (tube.withWidth (tube.getWidth() * 0.12f).translated (tube.getWidth() * 0.16f, tube.getHeight() * 0.12f).withTrimmedBottom (tube.getHeight() * 0.4f), 2.0f);
    }
    // the perforated grille in front of them: metal webbing between round holes
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addEllipse (win);
        g.reduceClipRegion (clip);
        juce::Path web;
        web.setUsingNonZeroWinding (false);
        web.addRectangle (win);
        const float pitch = 6.5f, hole = 2.75f;
        int rowIndex = 0;
        for (float y = win.getY(); y < win.getBottom() + pitch; y += pitch * 0.866f, ++rowIndex)
            for (float x = win.getX() + ((rowIndex % 2) != 0 ? pitch * 0.5f : 0.0f); x < win.getRight() + pitch; x += pitch)
                web.addEllipse (x - hole, y - hole, hole * 2.0f, hole * 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.fillPath (web, juce::AffineTransform::translation (0.0f, 0.7f));
        g.setColour (juce::Colour (0xffa4abb5).withAlpha (0.42f));
        g.fillPath (web);
    }
    // a reflection across the window, and the ring's inner edge
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f), win.getX(), win.getY(), juce::Colours::transparentWhite, win.getX(),
                                             win.getCentreY(), false));
    g.fillEllipse (win);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (win, 1.2f);
    print (g, "VALVE DRIVE", getLocalBounds().toFloat().removeFromBottom (15.0f), 10.5f, juce::Justification::centred, false, true);
}
} // namespace valvebox::ui
