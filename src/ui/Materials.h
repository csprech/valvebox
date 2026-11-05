#pragma once

// Tiny offline "renderer" for photoreal hardware parts.
//
// Each part is described as a height field. We take its normals and shade them
// with a single overhead key light (top-left, like a studio product shot):
// ambient + Lambert diffuse + Blinn-Phong specular, plus micro-bump grain for
// rubber/paint. Results are cached ARGB images rendered at device-pixel
// resolution, so painting stays cheap.

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace valvebox::materials
{
struct Shading
{
    float ambient = 0.35f;
    float diffuse = 0.75f;
    float specular = 0.2f;
    float shininess = 20.0f;
    float grain = 0.06f;   // albedo noise
    float bump = 0.0f;     // micro-bump height (px)
    float bumpScale = 1.0f;
};

Shading rubber();
Shading plastic();
Shading metal();

/** Device pixel scale of the graphics context (2 on Retina). */
float scaleOf (juce::Graphics& g);

/** Cache by key: images are generated once and reused. */
const juce::Image& cached (const juce::String& key, const std::function<juce::Image()>& make);

/** A rounded "pillow" (pad, key cap, badge): rounded-rect footprint, round-over bevel, slight dome.
    `wear` (0..1) polishes and darkens the patch where fingers land. */
juce::Image pillow (int w, int h, float corner, float bevel, float dome, juce::Colour albedo, const Shading& s,
                    uint32_t seed = 1, float wear = 0.0f);

/** A domed LED lens in a black bezel. */
juce::Image led (int diameter, juce::Colour colour, bool on);

/** A Phillips-head screw. */
juce::Image screw (int diameter, float angle);

/** A knurled black knob with a white indicator; lighting stays fixed while it turns. */
juce::Image knob (int diameter, float angle, juce::Colour cap = juce::Colour (0xff1a1a1a));

/** A turned aluminium knob seen from above: a tapered side, a lathe-brushed flat top with a chamfer, and an engraved
    pointer line at `angle` (0 = up). With `knurled`, the rim carries fine grip ridges that turn with the angle and
    there's no pointer (the data dial). */
juce::Image metalKnob (int diameter, float angle, bool knurled = false);

/** Tileable metallic paint: the base colour with fine aluminium flake that glints (opaque). */
juce::Image flakeTile (juce::Colour base, int size, uint32_t seed = 7);

/** Cream data wheel (no dimple); phase turns the knurled edge. */
juce::Image wheel (int diameter, float knurlPhase);

/** Just the finger dimple at `angle`, rendered in wheel space; draw it at `origin` (device px) over wheel(). */
juce::Image wheelDimple (int diameter, float angle, float knurlPhase, juce::Point<int>& origin);

/** A soft oily smudge (white, alpha only) for glass reflections. */
juce::Image smudge (int w, int h, uint32_t seed);

/** Walnut. The grain runs along y, or along x when `horizontal`. Opaque. */
juce::Image woodTile (int w, int h, uint32_t seed, bool horizontal = false);

/** Tileable powder-coat paint texture (opaque). */
juce::Image paintTile (juce::Colour base, int size, float roughness = 1.0f, uint32_t seed = 7);

/** Draws an image rendered at device resolution into a logical rectangle. */
void drawAt (juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> dest, float opacity = 1.0f);

/** Shadow of a round object standing on the panel (knobs): a tight contact shadow where it meets
    the panel plus a soft cast shadow thrown away from the top-left light. height = how tall it
    stands, in the same units as radius. */
void roundShadow (juce::Graphics& g, juce::Point<float> centre, float radius, float height, float strength = 1.0f);

} // namespace valvebox::materials
