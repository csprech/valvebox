// The shared half of the bridge: drawing the parts, and the mouse and keyboard sent into the off-screen panel.

#include "PanelBridge.h"

namespace panelbridge
{
struct Host
{
    std::unique_ptr<juce::ScopedJuceInitialiser_GUI> juce = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
    std::unique_ptr<Adapter> adapter = makeAdapter();
    std::vector<std::pair<juce::String, juce::Component*>> parts = adapter->parts();
    std::string layout;
    // the mouse
    juce::Component::SafePointer<juce::Component> target;
    juce::Point<float> downPos;
    juce::Time downTime;
    bool dragged = false;
    int clicks = 1;
    // the keyboard
    std::set<int> held;
    int modBits = 0;

    Host()
    {
        // (visible, so JUCE's hit-testing finds what's under the pointer; it isn't on the desktop, so nothing shows)
        panel().setVisible (true);
        adapter->setKeyHook ([this] (int c) { return held.count (juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) c)) != 0; });
    }

    ~Host()
    {
        adapter->prepareToQuit();
        adapter.reset();
    }

    juce::Component& panel() { return adapter->panel(); }

    juce::Rectangle<int> boundsOf (juce::Component& c) { return panel().getLocalArea (&c, c.getLocalBounds()); }

    const char* layoutJson()
    {
        juce::Array<juce::var> list;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            const auto b = boundsOf (*parts[i].second);
            auto* o = new juce::DynamicObject();
            o->setProperty ("index", (int) i);
            o->setProperty ("kind", parts[i].first);
            o->setProperty ("type", juce::String (typeid (*parts[i].second).name()));
            o->setProperty ("x", b.getX());
            o->setProperty ("y", b.getY());
            o->setProperty ("w", b.getWidth());
            o->setProperty ("h", b.getHeight());
            list.add (juce::var (o));
        }
        auto* root = new juce::DynamicObject();
        root->setProperty ("width", panel().getWidth());
        root->setProperty ("height", panel().getHeight());
        root->setProperty ("title", adapter->title());
        root->setProperty ("parts", list);
        layout = juce::JSON::toString (juce::var (root), true).toStdString();
        return layout.c_str();
    }

    static void copyOut (const juce::Image& img, uint8_t* bgra, int w, int h)
    {
        // JUCE's pixels are premultiplied; GPUI's images aren't
        const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < h; ++y)
        {
            const auto* src = reinterpret_cast<const juce::PixelARGB*> (bd.getLinePointer (y));
            auto* dst = bgra + (size_t) y * (size_t) w * 4;
            for (int x = 0; x < w; ++x)
            {
                auto px = src[x];
                px.unpremultiply();
                dst[x * 4 + 0] = px.getBlue();
                dst[x * 4 + 1] = px.getGreen();
                dst[x * 4 + 2] = px.getRed();
                dst[x * 4 + 3] = px.getAlpha();
            }
        }
    }

    /** `c` and what's inside it, drawn into exactly w x h pixels at `scale`. */
    static void paintInto (juce::Component& c, float scale, uint8_t* bgra, int w, int h)
    {
        juce::Image img (juce::Image::ARGB, std::max (1, w), std::max (1, h), true, juce::SoftwareImageType());
        {
            juce::Graphics g (img);
            g.addTransform (juce::AffineTransform::scale (scale));
            c.paintEntireComponent (g, true);
        }
        copyOut (img, bgra, w, h);
    }

    int renderBackground (float scale, uint8_t* bgra, int w, int h)
    {
        std::vector<std::pair<juce::Component*, bool>> shown;
        for (auto& [kind, c] : parts)
        {
            shown.push_back ({ c, c->isVisible() });
            c->setVisible (false);
        }
        paintInto (panel(), scale, bgra, w, h);
        for (auto& [c, v] : shown)
            c->setVisible (v);
        return 0;
    }

    int renderPart (int index, float scale, uint8_t* bgra, int w, int h)
    {
        if (index < 0 || index >= (int) parts.size())
            return 1;
        auto& c = *parts[(size_t) index].second;
        if (! c.isVisible())
        {
            std::memset (bgra, 0, (size_t) w * (size_t) h * 4);
            return 0;
        }
        paintInto (c, scale, bgra, w, h);
        return 0;
    }

    uint64_t walk (juce::Component& c, uint64_t h)
    {
        h = mix (h, c.isVisible() ? 1 : 2);
        if (! c.isVisible())
            return h;
        const auto b = c.getBounds();
        h = mix (mix (mix (mix (h, (uint64_t) b.getX()), (uint64_t) b.getY()), (uint64_t) b.getWidth()), (uint64_t) b.getHeight());
        if (auto* l = dynamic_cast<juce::Label*> (&c))
            h = mixString (h, l->getText());
        if (auto* s = dynamic_cast<juce::Slider*> (&c))
            h = mixFloat (mix (h, s->isMouseButtonDown()), (float) s->getValue(), 1.0e-4f);
        if (auto* b2 = dynamic_cast<juce::Button*> (&c))
            h = mixString (mix (mix (mix (h, b2->getToggleState()), b2->isDown()), b2->isEnabled()), b2->getButtonText());
        if (auto* cb = dynamic_cast<juce::ComboBox*> (&c))
            h = mixString (h, cb->getText());
        if (auto* te = dynamic_cast<juce::TextEditor*> (&c))
            h = mixString (h, te->getText());
        h = mix (h, adapter->custom (c));
        if (adapter->live (c))
            h = mix (h, juce::Time::getMillisecondCounter() / 100);
        for (auto* child : c.getChildren())
            h = walk (*child, h);
        return h;
    }

    uint64_t signature (int index)
    {
        if (index < 0 || index >= (int) parts.size())
            return 0;
        return walk (*parts[(size_t) index].second, 1469598103934665603ull);
    }

    // ---------------------------------------------------------------- the mouse

    static juce::ModifierKeys modsFrom (int bits, bool button)
    {
        int f = 0;
        if (bits & 1) f |= juce::ModifierKeys::shiftModifier;
        if (bits & 2) f |= juce::ModifierKeys::commandModifier;
        if (bits & 4) f |= juce::ModifierKeys::altModifier;
        if (bits & 8) f |= juce::ModifierKeys::ctrlModifier;
        if (button) f |= juce::ModifierKeys::leftButtonModifier;
        return juce::ModifierKeys (f);
    }

    juce::MouseEvent eventFor (juce::Component& c, juce::Point<float> p, int bits, bool button)
    {
        const auto local = c.getLocalPoint (&panel(), p);
        const auto down = c.getLocalPoint (&panel(), downPos);
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), local, modsFrom (bits, button), juce::MouseInputSource::defaultPressure,
                                 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(), down, downTime, clicks, dragged);
    }

    /** The button a click lands on (the component itself, or a button it's part of). */
    static juce::Button* buttonOf (juce::Component* c)
    {
        for (; c != nullptr; c = c->getParentComponent())
            if (auto* b = dynamic_cast<juce::Button*> (c))
                return b;
        return nullptr;
    }

    juce::Component* componentAt (juce::Point<float> p)
    {
        auto* c = panel().getComponentAt (p.roundToInt());
        return c != nullptr ? c : &panel();
    }

    void mouse (int kind, float x, float y, int bits, int numClicks)
    {
        const juce::Point<float> p (x, y);
        if (kind == 0)
        {
            target = componentAt (p);
            downPos = p;
            downTime = juce::Time::getCurrentTime();
            dragged = false;
            clicks = std::max (1, numClicks);
            if (auto* t = target.getComponent())
            {
                // (a button only presses on screen, which this panel isn't: press it directly, so its hold and
                // release callbacks still run, and click it on release)
                if (auto* button = buttonOf (t))
                {
                    target = button;
                    if (button->isEnabled())
                        button->setState (juce::Button::buttonDown);
                    return;
                }
                const auto e = eventFor (*t, p, bits, true);
                t->mouseDown (e);
                if (clicks == 2 && target != nullptr)
                    t->mouseDoubleClick (e);
            }
            return;
        }
        auto* t = target.getComponent();
        if (kind == 3 || t == nullptr)
            return;
        if (auto* button = dynamic_cast<juce::Button*> (t))
        {
            const bool over = button->getLocalBounds().contains (button->getLocalPoint (&panel(), p).roundToInt());
            if (kind == 1)
                button->setState (over ? juce::Button::buttonDown : juce::Button::buttonNormal);
            else
            {
                const bool wasDown = button->isDown();
                button->setState (juce::Button::buttonNormal);
                if (wasDown && over)
                    button->triggerClick();
                target = nullptr;
            }
            return;
        }
        if (kind == 1)
        {
            dragged = dragged || p.getDistanceFrom (downPos) > 2.0f;
            t->mouseDrag (eventFor (*t, p, bits, true));
            return;
        }
        t->mouseUp (eventFor (*t, p, bits, false));
        target = nullptr;
    }

    void wheel (float x, float y, float dx, float dy, int bits)
    {
        const juce::Point<float> p (x, y);
        if (auto* c = componentAt (p))
        {
            juce::MouseWheelDetails d;
            d.deltaX = dx;
            d.deltaY = dy;
            d.isReversed = false;
            d.isSmooth = true;
            d.isInertial = false;
            downPos = p;
            c->mouseWheelMove (eventFor (*c, p, bits, false), d);
        }
    }

    // ---------------------------------------------------------------- the keyboard

    static int codeFor (const juce::String& key, juce::juce_wchar ch)
    {
        static const std::pair<const char*, int> named[] = {
            { "space", juce::KeyPress::spaceKey },   { "escape", juce::KeyPress::escapeKey },     { "enter", juce::KeyPress::returnKey },
            { "backspace", juce::KeyPress::backspaceKey }, { "delete", juce::KeyPress::deleteKey }, { "tab", juce::KeyPress::tabKey },
            { "left", juce::KeyPress::leftKey },     { "right", juce::KeyPress::rightKey },       { "up", juce::KeyPress::upKey },
            { "down", juce::KeyPress::downKey },     { "home", juce::KeyPress::homeKey },         { "end", juce::KeyPress::endKey },
            { "pageup", juce::KeyPress::pageUpKey }, { "pagedown", juce::KeyPress::pageDownKey },
            { "f1", juce::KeyPress::F1Key },         { "f2", juce::KeyPress::F2Key },             { "f3", juce::KeyPress::F3Key },
            { "f4", juce::KeyPress::F4Key },         { "f5", juce::KeyPress::F5Key },             { "f6", juce::KeyPress::F6Key },
            { "f7", juce::KeyPress::F7Key },         { "f8", juce::KeyPress::F8Key },             { "f9", juce::KeyPress::F9Key },
            { "f10", juce::KeyPress::F10Key },       { "f11", juce::KeyPress::F11Key },           { "f12", juce::KeyPress::F12Key },
        };
        for (auto& [n, c] : named)
            if (key == n)
                return c;
        return (int) juce::CharacterFunctions::toUpperCase (ch);
    }

    void setModifiers (int bits)
    {
        juce::ModifierKeys::currentModifiers = modsFrom (bits, false);
        if (bits != modBits)
        {
            modBits = bits;
            adapter->keyTarget().modifierKeysChanged (juce::ModifierKeys::currentModifiers);
        }
    }

    int key (const char* keyName, const char* chars, bool down, int bits)
    {
        setModifiers (bits);
        const juce::String name (keyName), typed = juce::String::fromUTF8 (chars);
        const juce::juce_wchar ch = typed.isNotEmpty() ? typed[0] : (name.length() == 1 ? name[0] : 0);
        const int code = codeFor (name, ch);
        // the panel asks for character keys by the character (lower case); the named ones by their code
        const int heldKey = name.length() == 1 ? (int) juce::CharacterFunctions::toLowerCase (name[0]) : code;
        bool handled = false;
        if (down)
        {
            const bool repeat = held.count (heldKey) != 0;
            held.insert (heldKey);
            if (! repeat)
                handled = adapter->keyTarget().keyPressed (juce::KeyPress (code, juce::ModifierKeys::currentModifiers, ch));
        }
        else
            held.erase (heldKey);
        handled = adapter->keyTarget().keyStateChanged (down) || handled;
        return handled ? 1 : 0;
    }
};
} // namespace panelbridge

struct JpPanel
{
    panelbridge::Host host;
};

extern "C" {
JpPanel* jp_create (void) { return new JpPanel(); }
void jp_destroy (JpPanel* p) { delete p; }
const char* jp_layout_json (JpPanel* p) { return p->host.layoutJson(); }
int jp_render_background (JpPanel* p, float scale, uint8_t* bgra, int w, int h) { return p->host.renderBackground (scale, bgra, w, h); }
uint64_t jp_signature (JpPanel* p, int index) { return p->host.signature (index); }
int jp_render_part (JpPanel* p, int index, float scale, uint8_t* bgra, int w, int h) { return p->host.renderPart (index, scale, bgra, w, h); }
void jp_mouse (JpPanel* p, int kind, float x, float y, int mods, int clicks) { p->host.mouse (kind, x, y, mods, clicks); }
void jp_wheel (JpPanel* p, float x, float y, float dx, float dy, int mods) { p->host.wheel (x, y, dx, dy, mods); }
int jp_key (JpPanel* p, const char* key, const char* chars, int down, int mods) { return p->host.key (key, chars, down != 0, mods); }
void jp_modifiers (JpPanel* p, int mods) { p->host.setModifiers (mods); }
float jp_output_level (JpPanel* p) { return p->host.adapter->outputLevel(); }
void jp_test_sound (JpPanel* p, int start) { p->host.adapter->testSound (start != 0); }
}
