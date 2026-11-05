/* A JUCE panel behind a C interface, for a GPUI front end (shared by the devices' gpui branches).

   The device's own JUCE panel runs off-screen: it stays the controller (every key, knob and menu behaves as in
   the app) and draws its parts. The front end composites the parts on the GPU and sends the mouse and the
   computer keyboard back here, in panel coordinates. Each device supplies an Adapter (its parts, and a cheap
   signature per part that changes whenever the part would look different).

   Everything runs on the main thread, which is JUCE's message thread. Pixels are straight-alpha BGRA. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct JpPanel JpPanel;

JpPanel* jp_create (void);
void jp_destroy (JpPanel*);
/* {"width","height","title","parts":[{"index","kind","x","y","w","h"}]}; kinds: key, drag, display, button, static. */
const char* jp_layout_json (JpPanel*);
int jp_render_background (JpPanel*, float scale, uint8_t* bgra, int w, int h);
/* Changes whenever the part would look different; render it again then. */
uint64_t jp_signature (JpPanel*, int index);
int jp_render_part (JpPanel*, int index, float scale, uint8_t* bgra, int w, int h);

/* The mouse, in panel coordinates. kind: 0 down, 1 drag, 2 up, 3 move. mods: 1 shift, 2 command, 4 option,
   8 control. */
void jp_mouse (JpPanel*, int kind, float x, float y, int mods, int clicks);
void jp_wheel (JpPanel*, float x, float y, float dx, float dy, int mods);
/* The computer keyboard: GPUI's key name ("a", "space", "left", "f1"...) and the typed characters. */
int jp_key (JpPanel*, const char* key, const char* chars, int down, int mods);
void jp_modifiers (JpPanel*, int mods);

/* For the self-test: the output level, and the device's own way to make it sound (start 1) and stop (0). */
float jp_output_level (JpPanel*);
void jp_test_sound (JpPanel*, int start);

#ifdef __cplusplus
}

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <set>

namespace panelbridge
{
struct Adapter
{
    virtual ~Adapter() = default;
    virtual juce::String title() = 0;
    /** The panel, at its natural size: the coordinates the front end uses. */
    virtual juce::Component& panel() = 0;
    /** The parts the front end composites: kind (key, drag, display, button, static) and the component. */
    virtual std::vector<std::pair<juce::String, juce::Component*>> parts() = 0;
    /** What a custom widget holds that changes how it looks (standard JUCE widgets, visibility and bounds are
        hashed already, through every part's children). */
    virtual uint64_t custom (juce::Component&) { return 0; }
    /** Parts drawn from state the adapter can't see (a screen page painting the engine's state): drawn again
        ten times a second. */
    virtual bool live (juce::Component&) { return false; }
    virtual float outputLevel() = 0;
    virtual void testSound (bool start) = 0;
    /** What gets the computer keyboard (the panel, unless keys go to a component inside it). */
    virtual juce::Component& keyTarget() { return panel(); }
    /** The panel asks this whether a character key is held (in place of juce::KeyPress::isKeyCurrentlyDown). */
    virtual void setKeyHook (std::function<bool (int)>) {}
    /** Before quitting: the panel's settings. */
    virtual void prepareToQuit() {}
};

/** Made by the device (its adapter file). */
std::unique_ptr<Adapter> makeAdapter();

/** Mixes values into a signature. */
inline uint64_t mix (uint64_t h, uint64_t v) { return (h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2))) * 0x100000001b3ull; }
inline uint64_t mixFloat (uint64_t h, float v, float quantum = 1.0e-3f) { return mix (h, (uint64_t) (int64_t) std::llround (v / quantum)); }
inline uint64_t mixString (uint64_t h, const juce::String& s) { return mix (h, (uint64_t) s.hashCode64()); }
} // namespace panelbridge
#endif
