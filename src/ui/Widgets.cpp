#include "Widgets.h"
#include "DotFont.h"
#include "Materials.h"

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

void printTag (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float h)
{
    g.setColour (colours::print);
    g.fillRoundedRectangle (r, 2.0f);
    printInk (g, t, r, h, colours::panelDark);
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

    /** The printed scale round a knob: an arc with ticks (n of them; `between` puts them between the ends). */
    void knobScale (juce::Graphics& g, juce::Point<float> c, float R, int n, bool centreMark, bool between = false)
    {
        g.setColour (colours::print.withAlpha (0.88f));
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, R, R, 0.0f, kStart, kEnd, true);
        g.strokePath (arc, juce::PathStrokeType (1.1f));
        for (int i = 0; i < n; ++i)
        {
            const float t = between ? (i + 0.5f) / (float) n : (float) i / (float) (n - 1);
            const float a = kStart + (kEnd - kStart) * t;
            const bool major = ! between && (i == 0 || i == n - 1 || (centreMark && i == n / 2));
            const float r1 = R + (major ? 5.0f : 3.2f);
            g.drawLine (c.x + std::sin (a) * R, c.y - std::cos (a) * R, c.x + std::sin (a) * r1, c.y - std::cos (a) * r1, major ? 1.6f : 1.0f);
        }
    }
} // namespace

// ---------------------------------------------------------------- knob

KnobWidget::KnobWidget (const juce::String& l, bool bi) : label (l), bipolar (bi) {}

juce::Rectangle<int> KnobWidget::boundsFor (float cx, float cy, float d, float width)
{
    const float w = width > 0.0f ? width : std::max (d * 1.9f, 96.0f);
    const float h = kLabelHeight + d * 1.5f;
    return juce::Rectangle<float> (cx - w * 0.5f, cy - kLabelHeight - d * 0.75f, w, h).getSmallestIntegerContainer();
}

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
    auto r = getLocalBounds().toFloat().withTrimmedTop (kLabelHeight);
    const float d = diameter > 0.0f ? diameter : std::min (r.getWidth(), r.getHeight()) * 0.66f;
    return juce::Rectangle<float> (d, d).withCentre (r.getCentre());
}

void KnobWidget::paint (juce::Graphics& g)
{
    const auto k = knobArea();
    const auto c = k.getCentre();
    const float R = k.getWidth() * 0.5f;
    knobScale (g, c, R * 1.22f, 11, bipolar);
    const float angle = kStart + (kEnd - kStart) * value / 127.0f;
    const float sc = materials::scaleOf (g);
    materials::drawAt (g, knobImage ((int) std::ceil (k.getWidth() * sc), angle), k);
    // the label, just above the scale
    auto lr = juce::Rectangle<float> (0.0f, k.getY() - R * 0.22f - 6.0f - kLabelHeight + 2.0f, (float) getWidth(), kLabelHeight - 2.0f);
    if (boxedLabel)
    {
        const float w = juce::GlyphArrangement::getStringWidth (panelFont (11.0f, true), label) + 14.0f;
        printTag (g, label, lr.withSizeKeepingCentre (std::min (w, lr.getWidth()), lr.getHeight()), 11.0f);
    }
    else
        print (g, label, lr, 11.0f, juce::Justification::centred, false, true);
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
    const auto r = getLocalBounds().toFloat();
    const float d = diameter > 0.0f ? diameter : std::min (r.getWidth(), r.getHeight()) * 0.45f;
    return juce::Rectangle<float> (d, d).withCentre (r.getCentre());
}

void SelectorWidget::paint (juce::Graphics& g)
{
    const auto k = knobArea();
    const auto c = k.getCentre();
    const float R = k.getWidth() * 0.5f;
    knobScale (g, c, R * 1.13f, 16, false, true);
    const float angle = kStart + (kEnd - kStart) * (getIndex() + 0.5f) / 16.0f;
    const float sc = materials::scaleOf (g);
    materials::drawAt (g, knobImage ((int) std::ceil (k.getWidth() * sc), angle), k);
    // the 16 choices printed round the knob: the first eight stacked up its left side, the rest down its right,
    // evenly spaced and tucked against an ellipse round the knob (as on the original's panel)
    const float fh = 9.5f;
    const auto font = panelFont (fh, true);
    const float top = c.y - R * 1.27f, bottom = c.y + R * 1.08f, rx = R * 1.42f, ry = R * 1.34f;
    for (int i = 0; i < 16 && i < choices.size(); ++i)
    {
        const bool left = i < 8;
        const int slot = left ? i : 15 - i; // 0 at the bottom, 7 at the top
        const float y = bottom - (bottom - top) * slot / 7.0f;
        const float q = juce::jlimit (-1.0f, 1.0f, (y - c.y) / ry);
        const float dx = rx * std::sqrt (1.0f - q * q) + 2.0f;
        const float w = juce::GlyphArrangement::getStringWidth (font, choices[i]) + 6.0f;
        juce::Rectangle<float> box (w, fh + 2.0f);
        box = left ? box.withPosition (c.x - dx - w, y - box.getHeight() * 0.5f) : box.withPosition (c.x + dx, y - box.getHeight() * 0.5f);
        if (i == getIndex())
            printTag (g, choices[i], box, fh);
        else
            printInk (g, choices[i], box, fh, colours::print.withAlpha (0.9f));
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

float KeyWidget::extrusion() const
{
    switch (style)
    {
        case Style::Pill: case Style::DarkPill: return 4.0f;
        case Style::Round: return 5.0f;
        case Style::Transport: return 6.0f;
        case Style::Square: case Style::Step: case Style::StepSharp: return 6.0f;
    }
    return 5.0f;
}

juce::Rectangle<int> KeyWidget::boundsFor (juce::Rectangle<float> face) const
{
    auto b = face.expanded (kMargin);
    b.setBottom (b.getBottom() + extrusion() + (labelBelow() ? kBottomLabel : 0.0f));
    if (! labelBelow() && shiftLabel.isNotEmpty())
        b.setTop (b.getY() - kTopLabel);
    return b.getSmallestIntegerContainer();
}

juce::Rectangle<float> KeyWidget::faceArea() const
{
    auto r = getLocalBounds().toFloat().reduced (kMargin);
    if (labelBelow())
        r.removeFromBottom (kBottomLabel);
    else if (shiftLabel.isNotEmpty())
        r.removeFromTop (kTopLabel);
    r.removeFromBottom (extrusion());
    if (style == Style::Round)
        r = r.withSizeKeepingCentre (std::min (r.getWidth(), r.getHeight()), std::min (r.getWidth(), r.getHeight()));
    return r;
}

float KeyWidget::faceCorner() const
{
    const auto f = faceArea();
    if (style == Style::Round)
        return f.getWidth() * 0.5f;
    if (style == Style::Pill || style == Style::DarkPill)
        return std::min (5.0f, f.getHeight() * 0.3f);
    return 5.0f;
}

namespace
{
    juce::Path triangle (juce::Rectangle<float> r, int pointing) // 0 left, 1 right, 2 up, 3 down
    {
        juce::Path p;
        switch (pointing)
        {
            case 0: p.addTriangle (r.getRight(), r.getY(), r.getX(), r.getCentreY(), r.getRight(), r.getBottom()); break;
            case 1: p.addTriangle (r.getX(), r.getY(), r.getRight(), r.getCentreY(), r.getX(), r.getBottom()); break;
            case 2: p.addTriangle (r.getX(), r.getBottom(), r.getCentreX(), r.getY(), r.getRight(), r.getBottom()); break;
            default: p.addTriangle (r.getX(), r.getY(), r.getCentreX(), r.getBottom(), r.getRight(), r.getY()); break;
        }
        return p;
    }

    /** The transport symbols: |<  (o)  []  >/|| */
    juce::Path transportIcon (Key k, juce::Rectangle<float> r)
    {
        juce::Path p;
        const auto c = r.getCentre();
        const float s = std::min (r.getWidth(), r.getHeight()) * 0.52f;
        if (k == Key::ResetErase)
        {
            p.addRectangle (c.x - s * 0.55f, c.y - s * 0.5f, s * 0.17f, s);
            p.addPath (triangle ({ c.x - s * 0.33f, c.y - s * 0.5f, s * 0.9f, s }, 0));
        }
        else if (k == Key::Rec)
            p.addEllipse (juce::Rectangle<float> (s * 0.92f, s * 0.92f).withCentre (c));
        else if (k == Key::StopCancel)
            p.addRectangle (juce::Rectangle<float> (s * 0.85f, s * 0.85f).withCentre (c));
        else if (k == Key::PlayPause)
        {
            const float x0 = c.x - s * 1.1f;
            p.addPath (triangle ({ x0, c.y - s * 0.5f, s * 0.85f, s }, 1));
            juce::Path slash;
            slash.startNewSubPath (x0 + s * 1.12f, c.y + s * 0.55f);
            slash.lineTo (x0 + s * 1.45f, c.y - s * 0.55f);
            juce::PathStrokeType (s * 0.13f).createStrokedPath (slash, slash);
            p.addPath (slash);
            p.addRectangle (x0 + s * 1.62f, c.y - s * 0.5f, s * 0.2f, s);
            p.addRectangle (x0 + s * 1.95f, c.y - s * 0.5f, s * 0.2f, s);
        }
        return p;
    }

    const juce::Image& faceImage (int w, int h, float cornerPx, juce::Colour albedo, float sc)
    {
        return materials::cached ("vbface:" + juce::String (w) + "x" + juce::String (h) + ":" + juce::String ((int) cornerPx) + ":"
                                      + juce::String ((int) albedo.getARGB()),
                                  [=] { return materials::keyFace (w, h, cornerPx, 1.6f * sc, albedo, 3); });
    }
} // namespace

void KeyWidget::paint (juce::Graphics& g)
{
    const auto rest = faceArea();
    const float depth = extrusion(), corner = faceCorner();
    const bool pressed = down || latched;
    const float sink = pressed ? depth * 0.7f : 0.0f;
    const auto face = rest.translated (0.0f, sink);
    const bool on = hasLed && (led == Led::On || (led == Led::Blink && blinkOn));
    const bool dark = style == Style::DarkPill;
    const auto outsideInk = onCream ? colours::creamInk : colours::print;

    // the SHIFT function: above the key, or under a step key
    if (shiftLabel.isNotEmpty())
    {
        if (labelBelow())
            printInk (g, shiftLabel, juce::Rectangle<float> (rest.getX() - 6.0f, rest.getBottom() + depth + 3.0f, rest.getWidth() + 12.0f, kBottomLabel - 3.0f),
                      9.5f, outsideInk.withAlpha (0.92f), juce::Justification::centredTop, true, 2);
        else
            printInk (g, shiftLabel, juce::Rectangle<float> (rest.getX() - 10.0f, rest.getY() - kTopLabel, rest.getWidth() + 20.0f, kTopLabel - 2.0f), 9.5f,
                      outsideInk.withAlpha (0.92f), juce::Justification::centredBottom, true);
    }

    // a lit key throws a little of its light on the panel round its base
    if (on)
    {
        const auto c = style == Style::Transport && key == Key::PlayPause ? colours::ledGreen : colours::lit;
        juce::ColourGradient bloom (c.withAlpha (0.32f), rest.getCentreX(), rest.getBottom(), c.withAlpha (0.0f), rest.getCentreX(),
                                    rest.getBottom() + kMargin + depth, true);
        g.setGradientFill (bloom);
        g.fillRoundedRectangle (rest.expanded (kMargin * 0.8f).withBottom (rest.getBottom() + depth + kMargin * 0.8f), corner + 4.0f);
    }

    // the walls: the face swept down to where the key meets the panel
    const auto walls = juce::Rectangle<float> (rest.getX(), face.getY(), rest.getWidth(), rest.getBottom() + depth - face.getY());
    juce::Colour sideTop = dark ? juce::Colour (0xff45474c) : colours::keySideTop;
    juce::Colour sideBottom = dark ? juce::Colour (0xff1a1b1e) : colours::keySideBottom;
    if (on)
    {
        sideTop = sideTop.interpolatedWith (colours::lit, 0.35f);
        sideBottom = sideBottom.interpolatedWith (colours::lit, 0.25f);
    }
    g.setGradientFill (juce::ColourGradient (sideTop, 0.0f, face.getBottom() - corner, sideBottom, 0.0f, walls.getBottom(), false));
    g.fillRoundedRectangle (walls, corner);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.drawRoundedRectangle (walls.reduced (0.25f), corner, 0.6f);

    // the top face
    const float sc = materials::scaleOf (g);
    const int w = (int) std::ceil (face.getWidth() * sc), h = (int) std::ceil (face.getHeight() * sc);
    materials::drawAt (g, faceImage (w, h, corner * sc, dark ? colours::darkFace : colours::keyFace, sc), face);
    paintLabel (g, face, on);
    if (on && style != Style::Transport)
    {
        // a red LED under translucent rubber: hottest low in the key, a pink wash over the rest
        juce::ColourGradient glow (colours::lit.withAlpha (0.52f), face.getCentreX(), face.getY() + face.getHeight() * 0.72f, colours::lit.withAlpha (0.14f),
                                   face.getRight(), face.getY(), true);
        g.setGradientFill (glow);
        g.fillRoundedRectangle (face, corner);
    }
    if (latched)
    {
        g.setColour (colours::ledOrange.withAlpha (0.95f));
        g.drawRoundedRectangle (face.expanded (2.5f), corner + 2.5f, 1.6f);
    }
}

void KeyWidget::paintLabel (juce::Graphics& g, juce::Rectangle<float> face, bool on)
{
    const auto ink = colours::keyInk;
    if (style == Style::Transport)
    {
        // a black panel set into the grey key, with the symbol on it
        auto inset = face.reduced (std::min (face.getWidth(), face.getHeight()) * 0.1f);
        g.setColour (juce::Colour (0xff141518));
        g.fillRoundedRectangle (inset, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (inset.reduced (0.5f), 3.0f, 1.0f);
        const auto icon = transportIcon (key, inset);
        juce::Colour col = key == Key::Rec ? juce::Colour (0xffff7a80) : key == Key::PlayPause ? juce::Colour (0xff7fd18a) : juce::Colour (0xffc9cbcf);
        if (on)
        {
            col = key == Key::PlayPause ? colours::ledGreen : colours::lit.brighter (0.3f);
            juce::ColourGradient bloom (col.withAlpha (0.45f), inset.getCentreX(), inset.getCentreY(), col.withAlpha (0.0f), inset.getRight(), inset.getCentreY(), true);
            g.setGradientFill (bloom);
            g.fillRoundedRectangle (inset, 3.0f);
        }
        g.setColour (col);
        g.fillPath (icon);
        return;
    }
    if (label == "<" || label == ">" || label == "^" || label == "v")
    {
        const int dir = label == "<" ? 0 : label == ">" ? 1 : label == "^" ? 2 : 3;
        const float s = face.getHeight() * 0.42f;
        g.setColour (ink);
        g.fillPath (triangle (juce::Rectangle<float> (dir < 2 ? s * 0.85f : s * 1.1f, dir < 2 ? s : s * 0.85f).withCentre (face.getCentre()), dir));
        return;
    }
    if (style == Style::StepSharp)
    {
        // a black plate behind the number, like a keyboard's black key
        auto plate = face.reduced (face.getWidth() * 0.16f, face.getHeight() * 0.2f);
        g.setColour (juce::Colour (0xff17181b));
        g.fillRoundedRectangle (plate, 2.5f);
        g.setColour (colours::keyFace);
        g.setFont (keyFont (face.getHeight() * 0.46f));
        g.drawText (label, plate, juce::Justification::centred, false);
        return;
    }
    const bool big = style == Style::Step || (style == Style::Square && label.length() <= 3);
    if (big)
    {
        g.setColour (ink);
        g.setFont (keyFont (std::min (face.getHeight() * 0.42f, face.getWidth() * 0.62f)));
        g.drawText (label, face.translated (-1.0f, 0.0f), juce::Justification::centred, false);
        return;
    }
    const auto textInk = style == Style::DarkPill ? (on ? colours::lit.brighter (0.4f) : juce::Colour (0xffd0d2d6)) : ink;
    const float h = style == Style::Pill || style == Style::DarkPill ? std::min (10.0f, face.getHeight() * 0.5f) : 10.0f;
    printInk (g, label, face.reduced (3.0f, 1.0f), h, textInk, juce::Justification::centred, true, style == Style::Square ? 2 : 1);
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

juce::Rectangle<int> LampWidget::boundsFor (float x, float y, float labelWidth)
{
    return juce::Rectangle<float> (x - 5.5f, y - 7.0f, 11.0f + (labelWidth > 0 ? labelWidth + 4.0f : 0.0f), 14.0f).getSmallestIntegerContainer();
}

void LampWidget::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float d = 7.0f;
    const auto ink = (onCream ? colours::creamInk : colours::print).withAlpha (0.92f);
    juce::Rectangle<float> lr;
    if (above)
    {
        printInk (g, label, r.removeFromTop (r.getHeight() - d - 4.0f), 9.5f, ink, juce::Justification::centredBottom);
        lr = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
    }
    else
    {
        lr = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + 5.5f, r.getCentreY() });
        if (label.isNotEmpty())
            printInk (g, label, r.withTrimmedLeft (13.0f), 9.5f, ink, juce::Justification::centredLeft);
    }
    const bool on = state == Led::On || (state == Led::Blink && blinkOn);
    if (on)
    {
        juce::ColourGradient glow (colour.withAlpha (0.6f), lr.getCentreX(), lr.getCentreY(), colour.withAlpha (0.0f), lr.getCentreX() + 7.0f, lr.getCentreY(), true);
        g.setGradientFill (glow);
        g.fillEllipse (lr.expanded (7.0f));
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

juce::Rectangle<float> LcdWidget::statusArea() const
{
    auto in = getLocalBounds().toFloat().reduced (6.0f).reduced (9.0f, 7.0f);
    return in.removeFromBottom (in.getHeight() * 0.2f).withTrimmedLeft (40.0f);
}

float LcdWidget::segmentCentre (int part) const
{
    const auto st = statusArea();
    return st.getX() + (part + 0.5f) * st.getWidth() / 16.0f;
}

void LcdWidget::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    // the bezel
    g.setColour (juce::Colour (0xff121316));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    auto glass = r.reduced (6.0f);
    // the amber backlight, brightest in the middle
    juce::ColourGradient back (colours::lcdBack.brighter (0.22f), glass.getCentreX(), glass.getCentreY(), colours::lcdBack.darker (0.18f), glass.getX(),
                               glass.getY(), true);
    g.setGradientFill (back);
    g.fillRoundedRectangle (glass, 3.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.22f), 0, glass.getY(), juce::Colours::transparentBlack, 0, glass.getY() + 7.0f, false));
    g.fillRoundedRectangle (glass, 3.0f);

    auto in = glass.reduced (9.0f, 7.0f);
    auto status = in.removeFromBottom (in.getHeight() * 0.2f);
    in.removeFromBottom (3.0f);
    const auto ink = colours::lcdInk;
    const auto ghost = colours::lcdInk.withAlpha (0.075f);
    const float pitch = std::min (in.getWidth() * 0.68f / 48.0f, in.getHeight() / 16.5f);

    // the two lines: parameter, value
    const bool hide = shown.blink && ! blinkOn;
    dotLine (g, shown.param, in.getX(), in.getY(), pitch, ink, ghost, false, hide, shown.cursor, blinkOn);
    dotLine (g, shown.value, in.getX(), in.getY() + 8.5f * pitch, pitch, ink, ghost, true, false, -1, false);

    // the indicators and the input meter, beside them
    const float x0 = in.getX() + 48.0f * pitch + 6.0f;
    auto side = juce::Rectangle<float> (x0, in.getY(), in.getRight() - x0, 16.5f * pitch);
    const float rowH = side.getHeight() / 6.0f;
    auto indicator = [&] (const juce::String& t, bool on) {
        auto cell = side.removeFromTop (rowH);
        const auto c = on ? ink : ghost.withMultipliedAlpha (2.0f);
        g.setColour (c);
        g.fillPath (triangle (juce::Rectangle<float> (4.0f, 5.0f).withCentre ({ cell.getX() + 2.5f, cell.getCentreY() }), 1));
        printInk (g, t, cell.withTrimmedLeft (7.0f), std::min (9.0f, rowH * 1.1f), c, juce::Justification::centredLeft);
    };
    indicator ("PATTERN", shown.patternInd);
    indicator ("SONG", shown.songInd);
    indicator ("WAVE", shown.waveInd);
    indicator ("KNOB", shown.knobInd);
    indicator ("ORIGINAL", shown.originalInd);
    auto meter = side.reduced (0.0f, rowH * 0.18f);
    auto clip = meter.removeFromRight (22.0f);
    for (int i = 0; i < 10; ++i)
    {
        auto seg = juce::Rectangle<float> (meter.getX() + i * meter.getWidth() / 10.0f, meter.getY(), meter.getWidth() / 10.0f - 1.5f, meter.getHeight());
        g.setColour (shown.inputLevel * 10.0f > i ? ink : ghost.withMultipliedAlpha (1.5f));
        g.fillRect (seg);
    }
    printInk (g, "CLIP", clip, 8.5f, shown.clip ? ink : ghost.withMultipliedAlpha (2.0f), juce::Justification::centredRight);

    // the part status row: what it shows (PLAY, SWING, ACCENT, OUT L/R), and a segment for each part
    static const char* rows[4] = { "PLAY", "SWING", "ACCENT", "OUT L/R" };
    printInk (g, rows[std::clamp (shown.statusRow, 0, 3)], status.removeFromLeft (40.0f), 9.0f, ink, juce::Justification::centredLeft);
    const float cw = status.getWidth() / 16.0f;
    for (int p = 0; p < 16; ++p)
    {
        auto c = juce::Rectangle<float> (status.getX() + p * cw, status.getY(), cw, status.getHeight()).reduced (cw * 0.16f, status.getHeight() * 0.2f);
        g.setColour (shown.status[(size_t) p] ? ink : ghost);
        g.fillRect (c);
    }

    // a faint reflection on the glass
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.12f), glass.getX(), glass.getY(), juce::Colours::transparentWhite,
                                             glass.getX() + glass.getWidth() * 0.3f, glass.getY() + glass.getHeight() * 0.6f, false));
    g.fillRoundedRectangle (glass, 3.0f);
}

// ---------------------------------------------------------------- ribbon and slider

void RibbonWidget::touch (const juce::MouseEvent& e, bool on)
{
    const auto pad = getLocalBounds().toFloat().reduced (4.0f);
    pos = std::clamp (1.0f - (e.position.y - pad.getY()) / std::max (1.0f, pad.getHeight()), 0.0f, 1.0f);
    touching = on;
    repaint();
    if (onTouch)
        onTouch (on, pos);
}

void RibbonWidget::paint (juce::Graphics& g)
{
    auto frame = getLocalBounds().toFloat().reduced (1.0f);
    // a grey bezel round a pale, matt touch strip
    g.setGradientFill (juce::ColourGradient (colours::keySideTop, 0, frame.getY(), colours::keySideBottom, 0, frame.getBottom(), false));
    g.fillRoundedRectangle (frame, 4.0f);
    const auto pad = frame.reduced (4.0f);
    g.setGradientFill (juce::ColourGradient (colours::creamPanel.brighter (0.05f), pad.getX(), pad.getY(), colours::creamPanel.darker (0.08f), pad.getRight(),
                                             pad.getBottom(), false));
    g.fillRoundedRectangle (pad, 2.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.22f), pad.getX(), 0, juce::Colours::transparentBlack, pad.getX() + 7.0f, 0, false));
    g.fillRoundedRectangle (pad, 2.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.18f), 0, pad.getY(), juce::Colours::transparentBlack, 0, pad.getY() + 7.0f, false));
    g.fillRoundedRectangle (pad, 2.5f);
    if (touching)
    {
        const float y = pad.getBottom() - pos * pad.getHeight();
        juce::ColourGradient glow (colours::ledOrange.withAlpha (0.75f), pad.getCentreX(), y, colours::ledOrange.withAlpha (0.0f), pad.getCentreX(), y + 30.0f, true);
        g.setGradientFill (glow);
        g.fillRoundedRectangle (pad, 2.5f);
    }
}

juce::Rectangle<float> SliderWidget::slotArea() const
{
    auto r = getLocalBounds().toFloat();
    return r.withSizeKeepingCentre (7.0f, r.getHeight() - 28.0f);
}

void SliderWidget::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const auto slot = slotArea();
    // the printed scale across the slot
    g.setColour (colours::print.withAlpha (0.75f));
    for (int i = 0; i <= 12; ++i)
    {
        const float y = slot.getY() + slot.getHeight() * i / 12.0f;
        const float half = i % 6 == 0 ? r.getWidth() * 0.46f : r.getWidth() * 0.36f;
        g.fillRect (slot.getCentreX() - half, y - 0.5f, half - 7.0f, 1.0f);
        g.fillRect (slot.getCentreX() + 7.0f, y - 0.5f, half - 7.0f, 1.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.fillRoundedRectangle (slot, 3.5f);
    g.setColour (colours::print.withAlpha (0.7f));
    g.drawRoundedRectangle (slot.expanded (1.5f), 4.5f, 1.0f);
    // the cap: a short extruded grey block
    const float y = slot.getBottom() - value * slot.getHeight();
    auto cap = juce::Rectangle<float> (r.getWidth() * 0.62f, 24.0f).withCentre ({ slot.getCentreX(), y - 3.0f });
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (cap.translated (2.0f, 5.0f).expanded (1.5f), 4.0f);
    g.setGradientFill (juce::ColourGradient (colours::keySideTop, 0, cap.getBottom() - 3.0f, colours::keySideBottom, 0, cap.getBottom() + 6.0f, false));
    g.fillRoundedRectangle (cap.withBottom (cap.getBottom() + 6.0f), 3.5f);
    const float sc = materials::scaleOf (g);
    const int w = (int) std::ceil (cap.getWidth() * sc), h = (int) std::ceil (cap.getHeight() * sc);
    materials::drawAt (g, materials::cached ("vbfader3:" + juce::String (w) + "x" + juce::String (h),
                                             [=] { return materials::keyFace (w, h, 3.5f * sc, 1.6f * sc, colours::keyFace, 9); }),
                       cap);
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
    auto r = getLocalBounds().toFloat().reduced (3.0f);
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
    // a reflection across the window, and the ring's inner edge
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f), win.getX(), win.getY(), juce::Colours::transparentWhite, win.getX(),
                                             win.getCentreY(), false));
    g.fillEllipse (win);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (win, 1.2f);
}
} // namespace valvebox::ui
