// The shared half of the bridge: drawing the parts, and the mouse and keyboard sent into the off-screen panel.

#include "PanelBridge.h"

namespace panelbridge
{
/** A window that isn't one: the peer JUCE gives the panel. It never draws (GPUI shows the panel), but it sits where
    the panel is on screen, so JUCE delivers the mouse the normal way (with mouse listeners, hover, double-clicks and
    buttons as on screen) and opens menus and pop-ups over the panel, at its size. */
struct HeadlessPeer final : juce::ComponentPeer
{
    HeadlessPeer (juce::Component& c, int flags) : juce::ComponentPeer (c, flags) {}
    juce::Rectangle<int> bounds;

    void* getNativeHandle() const override { return nullptr; }
    void setVisible (bool) override {}
    void setTitle (const juce::String&) override {}
    void setBounds (const juce::Rectangle<int>& r, bool) override { bounds = r; }
    juce::Rectangle<int> getBounds() const override { return bounds; }
    juce::Point<float> localToGlobal (juce::Point<float> p) override { return p + bounds.getPosition().toFloat(); }
    juce::Point<float> globalToLocal (juce::Point<float> p) override { return p - bounds.getPosition().toFloat(); }
    void setMinimised (bool) override {}
    bool isMinimised() const override { return false; }
    bool isShowing() const override { return true; }
    void setFullScreen (bool) override {}
    bool isFullScreen() const override { return false; }
    void setIcon (const juce::Image&) override {}
    bool contains (juce::Point<int> p, bool) const override { return bounds.withZeroOrigin().contains (p); }
    OptionalBorderSize getFrameSizeIfPresent() const override { return {}; }
    juce::BorderSize<int> getFrameSize() const override { return {}; }
    bool setAlwaysOnTop (bool) override { return true; }
    void toFront (bool) override {}
    void toBehind (juce::ComponentPeer*) override {}
    bool isFocused() const override { return true; }
    void grabFocus() override {}
    void repaint (const juce::Rectangle<int>&) override {}
    void performAnyPendingRepaintsNow() override {}
    void setAlpha (float) override {}
    juce::StringArray getAvailableRenderingEngines() override { return { "Software Renderer" }; }
    void textInputRequired (juce::Point<int>, juce::TextInputTarget&) override {}
};

/** What holds the panel on the (pretend) desktop: in screen points, with the panel scaled into it. */
struct Mount final : juce::Component
{
    Mount() { setAccessible (false); }
    juce::ComponentPeer* createNewPeer (int flags, void*) override { return new HeadlessPeer (*this, flags); }
};

struct Host
{
    std::unique_ptr<juce::ScopedJuceInitialiser_GUI> juce = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
    std::unique_ptr<Adapter> adapter = makeAdapter();
    std::vector<std::pair<juce::String, juce::Component*>> parts = adapter->parts();
    std::string layout;
    // where the panel is on screen (see HeadlessPeer)
    Mount mount;
    juce::Point<float> windowOrigin, originFix, placedAt;
    float placedScale = 0.0f;
    // the mouse
    bool buttonDown = false;
    std::string tip;
    // the keyboard
    std::set<int> held;
    int modBits = 0;

    Host()
    {
        panel().setVisible (true);
        mount.setSize (panel().getWidth(), panel().getHeight());
        mount.addAndMakeVisible (panel());
        mount.setVisible (true);
        mount.addToDesktop (0); // (a peer that never draws)
        adapter->setKeyHook ([this] (int c) { return held.count (juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) c)) != 0; });
    }

    ~Host()
    {
        adapter->prepareToQuit();
        juce::PopupMenu::dismissAllActiveMenus();
        mount.removeChildComponent (&panel());
        mount.removeFromDesktop();
        adapter.reset();
    }

    /** Where the panel is in the window now (its top left, in window points, and points per panel pixel). */
    void place (float x, float y, float scale)
    {
        const auto origin = windowOrigin + originFix + juce::Point<float> (x, y);
        if (scale <= 0.0f || (origin == placedAt && scale == placedScale))
            return;
        placedAt = origin;
        placedScale = scale;
        const auto topLeft = origin.toInt();
        panel().setTransform (juce::AffineTransform::scale (scale).translated (origin - topLeft.toFloat()));
        mount.setBounds (topLeft.x, topLeft.y, (int) std::ceil ((float) panel().getWidth() * scale + 1.0f),
                         (int) std::ceil ((float) panel().getHeight() * scale + 1.0f));
    }

    void originFromPointer (float x, float y)
    {
        // (from a real mouse event: where the pointer is on screen, less where it is in the window; GPUI's own idea of
        // the window's position is only right on the main display)
        originFix = juce::Desktop::getInstance().getMainMouseSource().getRawScreenPosition() - juce::Point<float> (x, y) - windowOrigin;
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

    juce::Component* componentAt (juce::Point<float> p)
    {
        auto* c = panel().getComponentAt (p.roundToInt());
        return c != nullptr ? c : &panel();
    }

    /** The mouse, through the panel's peer (kind 0 down, 1 drag, 2 up, 3 moved with no button, 4 left the panel). */
    void mouse (int kind, float x, float y, int bits, int)
    {
        buttonDown = kind == 0 || (kind == 1 && buttonDown);
        juce::ModifierKeys::currentModifiers = modsFrom (bits, buttonDown);
        auto* peer = mount.getPeer();
        if (peer == nullptr)
            return;
        const auto pos = kind == 4 ? juce::Point<float> (-100000.0f, -100000.0f) : mount.getLocalPoint (&panel(), juce::Point<float> (x, y));
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, pos, juce::ModifierKeys::currentModifiers,
                                juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, juce::Time::currentTimeMillis());
    }

    void wheel (float x, float y, float dx, float dy, int bits)
    {
        auto* peer = mount.getPeer();
        if (peer == nullptr)
            return;
        juce::ModifierKeys::currentModifiers = modsFrom (bits, buttonDown);
        juce::MouseWheelDetails d;
        d.deltaX = dx;
        d.deltaY = dy;
        d.isReversed = false;
        d.isSmooth = true;
        d.isInertial = false;
        peer->handleMouseWheel (juce::MouseInputSource::InputSourceType::mouse, mount.getLocalPoint (&panel(), juce::Point<float> (x, y)),
                                juce::Time::currentTimeMillis(), d);
    }

    // ---------------------------------------------------------------- tooltips

    /** The caption that names a part: a label right against it, above or below (as on the screens and knobs). */
    static juce::String captionFor (juce::Component& c)
    {
        auto* parent = c.getParentComponent();
        if (parent == nullptr)
            return {};
        const auto b = c.getBounds();
        juce::String best;
        int bestGap = 15;
        for (auto* sib : parent->getChildren())
            if (auto* l = dynamic_cast<juce::Label*> (sib); l != nullptr && sib != &c && l->isVisible() && l->getText().trim().isNotEmpty())
            {
                const auto lb = l->getBounds();
                if (lb.getRight() <= b.getX() || lb.getX() >= b.getRight())
                    continue;
                const int gap = lb.getBottom() <= b.getCentreY() ? b.getY() - lb.getBottom() : lb.getY() - b.getBottom();
                if (gap >= -4 && gap < bestGap)
                {
                    bestGap = gap;
                    best = l->getText().trim();
                }
            }
        return best;
    }

    juce::String describe (juce::Component& c)
    {
        if (auto* tc = dynamic_cast<juce::TooltipClient*> (&c))
            if (auto t = tc->getTooltip(); t.isNotEmpty())
                return t;
        auto name = adapter->tooltip (c);
        if (name.isEmpty())
            name = c.getName().replace ("\n", " ").trim();
        if (name.isEmpty())
            name = c.getTitle().trim();
        if (name.isEmpty())
            name = captionFor (c);
        if (auto* sl = dynamic_cast<juce::Slider*> (&c))
        {
            const auto v = sl->getTextFromValue (sl->getValue()).trim();
            return name.isNotEmpty() ? name + ": " + v : v;
        }
        if (auto* cb = dynamic_cast<juce::ComboBox*> (&c))
            return (name.isNotEmpty() ? name + ": " : juce::String()) + cb->getText() + "  (click to choose)";
        if (auto* bt = dynamic_cast<juce::Button*> (&c); bt != nullptr && name.isEmpty())
            name = bt->getButtonText().trim();
        return name;
    }

    const char* tooltip (float x, float y)
    {
        tip.clear();
        for (auto* c = panel().getComponentAt (juce::Point<float> (x, y).roundToInt()); c != nullptr && c != &mount; c = c->getParentComponent())
            if (auto t = describe (*c); t.isNotEmpty())
            {
                tip = t.toStdString();
                break;
            }
        return tip.c_str();
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

    /** Where keys go: what has the keyboard inside the panel (a text field being typed in), else the panel's own target. */
    juce::Component* keyTarget()
    {
        auto* f = juce::Component::getCurrentlyFocusedComponent();
        return f != nullptr && panel().isParentOf (f) ? f : &adapter->keyTarget();
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
                for (auto* c = keyTarget(); c != nullptr && ! handled; c = c == &panel() ? nullptr : c->getParentComponent())
                    handled = c->keyPressed (juce::KeyPress (code, juce::ModifierKeys::currentModifiers, ch));
        }
        else
            held.erase (heldKey);
        handled = keyTarget()->keyStateChanged (down) || handled;
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
void jp_window_origin (JpPanel* p, float x, float y) { p->host.windowOrigin = { x, y }; }
void jp_window_origin_from_pointer (JpPanel* p, float x, float y) { p->host.originFromPointer (x, y); }
void jp_place (JpPanel* p, float x, float y, float scale) { p->host.place (x, y, scale); }
const char* jp_tooltip (JpPanel* p, float x, float y) { return p->host.tooltip (x, y); }
int jp_menu_open (JpPanel*)
{
    auto* mcm = juce::ModalComponentManager::getInstanceWithoutCreating();
    return mcm != nullptr && mcm->getNumModalComponents() > 0 ? 1 : 0;
}
void jp_close_menus (JpPanel*)
{
    juce::PopupMenu::dismissAllActiveMenus();
    if (auto* mcm = juce::ModalComponentManager::getInstanceWithoutCreating())
        mcm->cancelAllModalComponents();
}
void jp_wheel (JpPanel* p, float x, float y, float dx, float dy, int mods) { p->host.wheel (x, y, dx, dy, mods); }
int jp_key (JpPanel* p, const char* key, const char* chars, int down, int mods) { return p->host.key (key, chars, down != 0, mods); }
void jp_modifiers (JpPanel* p, int mods) { p->host.setModifiers (mods); }
float jp_output_level (JpPanel* p) { return p->host.adapter->outputLevel(); }
void jp_test_sound (JpPanel* p, int start) { p->host.adapter->testSound (start != 0); }
}
