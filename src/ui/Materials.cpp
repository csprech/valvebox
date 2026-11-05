#include "ui/Materials.h"
#include <cmath>
#include <map>

namespace valvebox::materials
{
namespace
{
    struct V3
    {
        float x, y, z;
    };
    V3 norm (V3 v)
    {
        const float l = std::sqrt (v.x * v.x + v.y * v.y + v.z * v.z);
        return { v.x / l, v.y / l, v.z / l };
    }
    float dot (V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

    // key light from the top-left, slightly in front (screen y grows downwards)
    const V3 kLight = norm ({ -0.40f, -0.62f, 0.68f });
    const V3 kHalf = norm ({ kLight.x, kLight.y, kLight.z + 1.0f });
    // soft fill from the right so shadowed sides aren't dead
    const V3 kFill = norm ({ 0.7f, 0.2f, 0.7f });

    float hash (int x, int y, uint32_t seed)
    {
        uint32_t h = (uint32_t) x * 374761393u + (uint32_t) y * 668265263u + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= h >> 16;
        return (float) (h & 0xffffff) / 16777215.0f;
    }

    float valueNoise (float x, float y, uint32_t seed, int period = 0)
    {
        const int xi = (int) std::floor (x), yi = (int) std::floor (y);
        const float fx = x - xi, fy = y - yi;
        auto wrap = [period] (int v) { return period > 0 ? ((v % period) + period) % period : v; };
        const float a = hash (wrap (xi), wrap (yi), seed), b = hash (wrap (xi + 1), wrap (yi), seed);
        const float c = hash (wrap (xi), wrap (yi + 1), seed), d = hash (wrap (xi + 1), wrap (yi + 1), seed);
        const float sx = fx * fx * (3.0f - 2.0f * fx), sy = fy * fy * (3.0f - 2.0f * fy);
        return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy;
    }

    struct Rgb
    {
        float r, g, b;
    };
    Rgb toRgb (juce::Colour c) { return { c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue() }; }

    Rgb shade (V3 n, Rgb albedo, const Shading& s, float specMask = 1.0f)
    {
        const float diff = std::max (0.0f, dot (n, kLight));
        const float fill = std::max (0.0f, dot (n, kFill)) * 0.18f;
        const float spec = std::pow (std::max (0.0f, dot (n, kHalf)), s.shininess) * s.specular * specMask;
        const float k = s.ambient + s.diffuse * diff + fill;
        return { albedo.r * k + spec, albedo.g * k + spec, albedo.b * k + spec };
    }

    void put (juce::Image::BitmapData& bd, int x, int y, Rgb c, float alpha)
    {
        auto clamp8 = [] (float v) { return (juce::uint8) juce::jlimit (0, 255, (int) std::lround (v * 255.0f)); };
        alpha = juce::jlimit (0.0f, 1.0f, alpha);
        auto* p = reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y));
        p->setARGB (clamp8 (alpha), clamp8 (c.r * alpha), clamp8 (c.g * alpha), clamp8 (c.b * alpha)); // premultiplied
    }

    /** Renders any height field: height(x,y) in px, coverage(x,y) 0..1, albedo(x,y). */
    juce::Image renderField (int w, int h, const std::function<float (float, float)>& height,
                             const std::function<float (float, float)>& coverage,
                             const std::function<Rgb (float, float)>& albedo, const Shading& s,
                             const std::function<float (float, float)>& specMask = nullptr)
    {
        juce::Image img (juce::Image::ARGB, std::max (1, w), std::max (1, h), true);
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const float px = x + 0.5f, py = y + 0.5f;
                const float a = coverage (px, py);
                if (a <= 0.0f)
                    continue;
                const float dx = height (px + 0.75f, py) - height (px - 0.75f, py);
                const float dy = height (px, py + 0.75f) - height (px, py - 0.75f);
                const V3 n = norm ({ -dx / 1.5f, -dy / 1.5f, 1.0f });
                put (bd, x, y, shade (n, albedo (px, py), s, specMask ? specMask (px, py) : 1.0f), a);
            }
        return img;
    }

    // signed distance to a rounded rectangle centred at the origin (negative inside)
    float sdRoundRect (float px, float py, float hw, float hh, float r)
    {
        const float qx = std::abs (px) - (hw - r), qy = std::abs (py) - (hh - r);
        const float ox = std::max (qx, 0.0f), oy = std::max (qy, 0.0f);
        return std::sqrt (ox * ox + oy * oy) + std::min (std::max (qx, qy), 0.0f) - r;
    }

    float roundOver (float e) // quarter-circle profile, e in 0..1
    {
        e = juce::jlimit (0.0f, 1.0f, e);
        return std::sqrt (1.0f - (1.0f - e) * (1.0f - e));
    }
} // namespace

Shading rubber() { return { 0.30f, 0.80f, 0.10f, 7.0f, 0.10f, 0.35f, 0.9f }; }
Shading plastic() { return { 0.36f, 0.74f, 0.30f, 34.0f, 0.04f, 0.10f, 1.4f }; }
Shading metal() { return { 0.28f, 0.70f, 0.85f, 26.0f, 0.03f, 0.05f, 1.0f }; }

float scaleOf (juce::Graphics& g) { return juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor()); }

const juce::Image& cached (const juce::String& key, const std::function<juce::Image()>& make)
{
    static std::map<juce::String, juce::Image> cache;
    auto it = cache.find (key);
    if (it == cache.end())
    {
        if (cache.size() > 600)
            cache.clear(); // window resizing generates new sizes; don't grow forever
        it = cache.emplace (key, make()).first;
    }
    return it->second;
}

juce::Image pillow (int w, int h, float corner, float bevel, float dome, juce::Colour albedoColour, const Shading& s,
                    uint32_t seed, float wear)
{
    const float hw = w * 0.5f, hh = h * 0.5f;
    const Rgb base = toRgb (albedoColour);
    // where fingers land: an uneven patch a little below the centre, polished smooth and slightly oily
    auto wearMask = [=] (float x, float y) {
        if (wear <= 0.0f)
            return 0.0f;
        const float nx = (x - hw) / hw, ny = (y - hh * 1.08f) / hh;
        const float fall = juce::jlimit (0.0f, 1.0f, 1.0f - (nx * nx * 1.3f + ny * ny * 1.6f));
        const float blot = valueNoise (x / (w * 0.16f), y / (h * 0.16f), seed + 41);
        return wear * fall * fall * (0.45f + 0.55f * blot);
    };
    auto heightFn = [=] (float x, float y) {
        const float px = x - hw, py = y - hh;
        const float d = -sdRoundRect (px, py, hw - 0.5f, hh - 0.5f, corner);
        const float edge = roundOver (d / bevel);
        const float nx = px / hw, ny = py / hh;
        const float domeH = dome * (1.0f - 0.5f * (nx * nx + ny * ny)) * edge;
        const float grain = s.bump * (1.0f - 0.8f * wearMask (x, y)) * (valueNoise (x * s.bumpScale, y * s.bumpScale, seed) - 0.5f) * edge;
        return bevel * edge + domeH + grain;
    };
    auto coverage = [=] (float x, float y) { return juce::jlimit (0.0f, 1.0f, -sdRoundRect (x - hw, y - hh, hw - 0.5f, hh - 0.5f, corner) + 0.5f); };
    auto albedo = [=] (float x, float y) {
        const float g = 1.0f + s.grain * (hash ((int) x, (int) y, seed + 11) - 0.5f) * 2.0f;
        const float k = g * (1.0f - 0.07f * wearMask (x, y)); // skin oil darkens the surface a touch
        return Rgb { base.r * k, base.g * k, base.b * k };
    };
    auto specMask = [=] (float x, float y) { return 1.0f + 3.5f * wearMask (x, y); };
    return renderField (w, h, heightFn, coverage, albedo, s, specMask);
}

juce::Image keyFace (int w, int h, float corner, float fillet, juce::Colour albedoColour, uint32_t seed)
{
    const float hw = w * 0.5f, hh = h * 0.5f;
    const Rgb base = toRgb (albedoColour);
    auto inside = [=] (float x, float y) { return -sdRoundRect (x - hw, y - hh, hw - 0.5f, hh - 0.5f, corner); };
    auto heightFn = [=] (float x, float y) {
        const float d = inside (x, y);
        const float edge = juce::jlimit (0.0f, 1.0f, d / fillet);
        return fillet * roundOver (d / fillet) + 0.12f * (valueNoise (x * 0.8f, y * 0.8f, seed) - 0.5f) * edge;
    };
    auto coverage = [=] (float x, float y) { return juce::jlimit (0.0f, 1.0f, inside (x, y) + 0.5f); };
    auto albedo = [=] (float x, float y) {
        const float g = 1.0f + 0.022f * (hash ((int) x, (int) y, seed + 11) - 0.5f) * 2.0f;
        return Rgb { base.r * g, base.g * g, base.b * g };
    };
    const Shading s { 0.56f, 0.46f, 0.08f, 9.0f, 0.0f, 0.0f, 1.0f };
    return renderField (w, h, heightFn, coverage, albedo, s);
}

juce::Image led (int d, juce::Colour c, bool on)
{
    const float R = d * 0.5f, lensR = R * 0.74f;
    const Rgb col = toRgb (c);
    Shading s = { 0.25f, 0.6f, 1.1f, 70.0f, 0.0f, 0.0f, 1.0f };
    auto heightFn = [=] (float x, float y) {
        const float r = std::hypot (x - R, y - R);
        if (r < lensR)
            return R * 0.25f + std::sqrt (std::max (0.0f, lensR * lensR - r * r)) * 0.55f;
        // black bezel ring: a small torus
        const float t = (r - lensR) / (R - lensR);
        return R * 0.25f * std::sin (juce::MathConstants<float>::pi * juce::jlimit (0.0f, 1.0f, t));
    };
    auto coverage = [=] (float x, float y) { return juce::jlimit (0.0f, 1.0f, R - std::hypot (x - R, y - R) + 0.5f); };
    auto albedo = [=] (float x, float y) {
        const float r = std::hypot (x - R, y - R);
        if (r >= lensR)
            return Rgb { 0.07f, 0.07f, 0.07f };
        if (! on)
            return Rgb { col.r * 0.22f + 0.04f, col.g * 0.22f + 0.04f, col.b * 0.22f + 0.04f };
        const float core = 1.0f - r / lensR; // emissive hot centre
        return Rgb { col.r * 1.9f + core * 1.4f, col.g * 1.9f + core * 1.4f, col.b * 1.9f + core * 1.4f };
    };
    return renderField (d, d, heightFn, coverage, albedo, s);
}

juce::Image screw (int d, float angle)
{
    const float R = d * 0.5f;
    const float ca = std::cos (angle), sa = std::sin (angle);
    auto heightFn = [=] (float x, float y) {
        const float px = x - R, py = y - R;
        const float r = std::hypot (px, py) / R;
        float h = R * 0.38f * std::sqrt (std::max (0.0f, 1.0f - r * r));
        // Phillips cross recess
        const float u = px * ca + py * sa, v = -px * sa + py * ca;
        const float slotW = R * 0.13f, slotL = R * 0.62f;
        auto inSlot = [&] (float a, float b) {
            const float edge = juce::jlimit (0.0f, 1.0f, (slotW - std::abs (b)) / 1.2f) * juce::jlimit (0.0f, 1.0f, (slotL - std::abs (a)) / 1.2f);
            return edge;
        };
        const float cross = std::max (inSlot (u, v), inSlot (v, u));
        return h - cross * R * 0.3f;
    };
    auto coverage = [=] (float x, float y) { return juce::jlimit (0.0f, 1.0f, R - std::hypot (x - R, y - R) + 0.5f); };
    auto albedo = [=] (float x, float y) {
        const float g = 0.70f + 0.06f * (hash ((int) x, (int) y, 5) - 0.5f);
        return Rgb { g, g * 0.99f, g * 0.96f };
    };
    return renderField (d, d, heightFn, coverage, albedo, metal());
}

juce::Image knob (int d, float angle, juce::Colour capColour)
{
    const float R = d * 0.5f;
    const int teeth = 30;
    const float capR = R * 0.80f;
    auto skirtR = [=] (float theta) {
        const float t = 0.5f + 0.5f * std::cos (teeth * (theta - angle));
        return R * (0.93f + 0.06f * t);
    };
    auto heightFn = [=] (float x, float y) {
        const float px = x - R, py = y - R;
        const float r = std::hypot (px, py);
        const float theta = std::atan2 (px, -py);
        const float sr = skirtR (theta);
        if (r > capR)
        {
            // knurled skirt: ridges plus a downward slope to the panel
            const float t = juce::jlimit (0.0f, 1.0f, (sr - r) / (sr - capR));
            return R * 0.50f * roundOver (t * 1.6f) + R * 0.04f * (0.5f + 0.5f * std::cos (teeth * (theta - angle)));
        }
        // top cap: chamfered edge, very slightly dished
        const float e = roundOver ((capR - r) / (R * 0.14f));
        const float dish = R * 0.03f * (r / capR) * (r / capR);
        return R * 0.50f + R * 0.10f * e + dish;
    };
    auto coverage = [=] (float x, float y) {
        const float px = x - R, py = y - R;
        const float r = std::hypot (px, py);
        return juce::jlimit (0.0f, 1.0f, skirtR (std::atan2 (px, -py)) - r + 0.5f);
    };
    auto onIndicator = [=] (float x, float y) {
        const float px = x - R, py = y - R;
        // rotate into knob space: indicator points "up" at angle 0
        const float u = px * std::cos (-angle) - py * std::sin (-angle);
        const float v = px * std::sin (-angle) + py * std::cos (-angle);
        return std::abs (u) < R * 0.055f && v < -capR * 0.28f && v > -capR * 0.92f;
    };
    const Rgb capRgb = toRgb (capColour);
    auto albedo = [=] (float x, float y) {
        if (onIndicator (x, y))
            return Rgb { 0.93f, 0.92f, 0.88f };
        const float n = 1.0f + 0.15f * (hash ((int) x, (int) y, 9) - 0.5f);
        const float r = std::hypot (x - R, y - R);
        if (r < capR) // coloured top cap; the knurled skirt stays black
            return Rgb { capRgb.r * n, capRgb.g * n, capRgb.b * n };
        const float g = 0.10f * n;
        return Rgb { g, g, g * 1.02f };
    };
    Shading s = plastic();
    s.specular = 0.45f;
    s.shininess = 28.0f;
    s.ambient = 0.5f;
    return renderField (d, d, heightFn, coverage, albedo, s);
}

juce::Image metalKnob (int d, float angle, bool knurled)
{
    const float R = d * 0.5f;
    const float topR = R * (knurled ? 0.80f : 0.84f), chamfer = R * 0.07f, H = R * 0.55f;
    const int ridges = 48;
    const float ca = std::cos (-angle), sa = std::sin (-angle);
    auto rim = [=] (float theta) { return knurled ? R * (0.975f + 0.025f * std::cos (ridges * (theta - angle))) : R; };
    auto heightFn = [=] (float x, float y) {
        const float px = x - R, py = y - R;
        const float r = std::hypot (px, py);
        const float theta = std::atan2 (px, -py);
        if (r > topR)
        {
            // the tapered side, steep near the panel, rounding over into the chamfer
            const float t = juce::jlimit (0.0f, 1.0f, (rim (theta) - r) / (rim (theta) - topR));
            float h = (H - chamfer * 0.4f) * std::pow (t, 0.75f);
            if (knurled)
                h += R * 0.035f * (0.5f + 0.5f * std::cos (ridges * (theta - angle))) * t;
            return h;
        }
        // the top: a chamfered edge, then flat with fine concentric turning marks
        const float e = roundOver ((topR - r) / chamfer);
        const float turning = R * 0.0035f * (valueNoise (r * 1.1f, 3.0f, 21) - 0.5f) + R * 0.0015f * std::sin (r * 2.4f);
        float h = H - chamfer * 0.4f + chamfer * 0.4f * e + turning * e;
        if (! knurled)
        {
            // the pointer: an engraved groove from near the centre to the edge
            const float u = px * ca - py * sa, v = px * sa + py * ca;
            const float w = R * 0.045f;
            if (v < -topR * 0.18f && v > -topR * 0.97f)
                h -= R * 0.03f * juce::jlimit (0.0f, 1.0f, (w - std::abs (u)) / (w * 0.6f));
        }
        return h;
    };
    auto coverage = [=] (float x, float y) {
        const float px = x - R, py = y - R;
        return juce::jlimit (0.0f, 1.0f, rim (std::atan2 (px, -py)) - std::hypot (px, py) + 0.5f);
    };
    auto onPointer = [=] (float x, float y) {
        if (knurled)
            return 0.0f;
        const float px = x - R, py = y - R;
        const float u = px * ca - py * sa, v = px * sa + py * ca;
        const float w = R * 0.045f;
        if (v >= -topR * 0.18f || v <= -topR * 0.97f)
            return 0.0f;
        return juce::jlimit (0.0f, 1.0f, (w - std::abs (u)) / 1.0f + 0.5f);
    };
    auto albedo = [=] (float x, float y) {
        const float r = std::hypot (x - R, y - R);
        const float n = 1.0f + 0.05f * (hash ((int) x, (int) y, 31) - 0.5f);
        // the side is bead-blasted a shade darker than the turned top
        const float base = (r > topR ? 0.66f : 0.80f) * n;
        const float p = onPointer (x, y);
        const float v = base * (1.0f - p) + 0.10f * p; // the groove is filled with black paint
        return Rgb { v * 0.98f, v * 0.99f, v * 1.02f };
    };
    auto specMask = [=] (float x, float y) { return 1.0f - 0.85f * onPointer (x, y); };
    Shading s = { 0.42f, 0.58f, 0.55f, 16.0f, 0.0f, 0.0f, 1.0f };
    return renderField (d, d, heightFn, coverage, albedo, s, specMask);
}

juce::Image flakeTile (juce::Colour baseColour, int size, uint32_t seed)
{
    juce::Image img (juce::Image::ARGB, size, size, false);
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    const Rgb base = toRgb (baseColour);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            // a soft mottle (paint thickness), fine grain, and flakes: most dim, a few that catch the light
            const float mottle = valueNoise (x / 9.0f, y / 9.0f, seed, size / 9 > 0 ? size / 9 : 1) - 0.5f;
            const float grain = hash (x, y, seed + 1) - 0.5f;
            const float f = hash (x, y, seed + 2);
            const float flake = f > 0.985f ? 0.35f * (f - 0.985f) / 0.015f + 0.12f : f > 0.93f ? 0.06f : 0.0f;
            const float k = 1.0f + 0.05f * mottle + 0.06f * grain;
            put (bd, x, y, { base.r * k + flake * 0.85f, base.g * k + flake * 0.9f, base.b * k + flake }, 1.0f);
        }
    return img;
}

juce::Image paintTile (juce::Colour baseColour, int size, float roughness, uint32_t seed)
{
    const Rgb base = toRgb (baseColour);
    // lattice periods must divide the tile so it repeats seamlessly (size is a multiple of 30)
    const int pFine = (int) (size / 1.5f), pMid = (int) (size / 2.5f), pWide = size / 5;
    auto heightFn = [=] (float x, float y) {
        auto wx = std::fmod (x + size, (float) size), wy = std::fmod (y + size, (float) size);
        // tight powder-coat "orange peel": fine grit and a 2-3 px peel, only a trace of wider wave
        return roughness * (0.16f * valueNoise (wx / 1.5f, wy / 1.5f, seed, pFine)
                            + 0.14f * valueNoise (wx / 2.5f, wy / 2.5f, seed + 1, pMid)
                            + 0.05f * valueNoise (wx / 5.0f, wy / 5.0f, seed + 2, pWide));
    };
    auto albedo = [=] (float x, float y) {
        const int ix = ((int) x) % size, iy = ((int) y) % size;
        const float g = 1.0f + 0.035f * (hash (ix, iy, seed + 3) - 0.5f) - (hash (ix, iy, seed + 4) > 0.996f ? 0.08f : 0.0f);
        return Rgb { base.r * g, base.g * g, base.b * g };
    };
    Shading s = { 0.72f, 0.36f, 0.06f, 12.0f, 0.0f, 0.0f, 1.0f };
    auto img = renderField (size, size, heightFn, [] (float, float) { return 1.0f; }, albedo, s);
    return img;
}

namespace
{
    constexpr int kWheelTeeth = 90;
    const juce::Colour kWheelColour { 0xffe2ddcd };

    Shading wheelShading() { return { 0.44f, 0.62f, 0.16f, 14.0f, 0.03f, 0.0f, 1.0f }; }

    /** Data wheel surface: knurled side skirt, a shallow dished top and (optionally) a finger dimple. */
    float wheelHeight (float x, float y, float R, float phase, float dcx, float dcy, float rd)
    {
        const float px = x - R, py = y - R;
        const float r = std::hypot (px, py);
        const float Rt = R * 0.84f, Hs = R * 0.30f;
        float h;
        if (r > Rt)
        {
            const float t = juce::jlimit (0.0f, 1.0f, (R - r) / (R - Rt));
            const float theta = std::atan2 (px, -py);
            h = Hs * roundOver (t * 1.3f) + R * 0.005f * std::cos (kWheelTeeth * (theta - phase)) * juce::jlimit (0.0f, 1.0f, t * 3.0f);
        }
        else
        {
            const float lip = roundOver ((Rt - r) / (R * 0.07f));
            const float q = r / Rt;
            h = Hs + R * 0.025f * lip - R * 0.05f * (1.0f - q * q);
        }
        if (rd > 0.0f)
        {
            const float q = ((x - dcx) * (x - dcx) + (y - dcy) * (y - dcy)) / (rd * rd);
            if (q < 1.0f)
                h -= rd * 0.42f * (1.0f - q) * std::sqrt (1.0f - q);
        }
        return h;
    }

    float wheelAlbedoGrain (float x, float y) { return 1.0f + 0.03f * (hash ((int) x, (int) y, 77) - 0.5f); }
} // namespace

juce::Image wheel (int d, float phase)
{
    const float R = d * 0.5f;
    const Rgb col = toRgb (kWheelColour);
    return renderField (
        d, d, [=] (float x, float y) { return wheelHeight (x, y, R, phase, 0, 0, 0); },
        [=] (float x, float y) { return juce::jlimit (0.0f, 1.0f, R - std::hypot (x - R, y - R) + 0.5f); },
        [=] (float x, float y) {
            const float g = wheelAlbedoGrain (x, y);
            return Rgb { col.r * g, col.g * g, col.b * g };
        },
        wheelShading());
}

juce::Image wheelDimple (int d, float angle, float phase, juce::Point<int>& origin)
{
    const float R = d * 0.5f, rd = R * 0.22f, dist = R * 0.52f;
    const float dcx = R + std::sin (angle) * dist, dcy = R - std::cos (angle) * dist;
    const int ox = (int) std::floor (dcx - rd - 2.0f), oy = (int) std::floor (dcy - rd - 2.0f);
    const int size = (int) std::ceil (rd * 2.0f + 4.0f);
    origin = { ox, oy };
    const Rgb col = toRgb (kWheelColour);
    // rendered in wheel space so it blends seamlessly into the base image underneath
    return renderField (
        size, size, [=] (float x, float y) { return wheelHeight (x + ox, y + oy, R, phase, dcx, dcy, rd); },
        [=] (float x, float y) { return juce::jlimit (0.0f, 1.0f, rd + 1.0f - std::hypot (x + ox - dcx, y + oy - dcy)); },
        [=] (float x, float y) {
            const float g = wheelAlbedoGrain (x + ox, y + oy);
            return Rgb { col.r * g, col.g * g, col.b * g };
        },
        wheelShading());
}

juce::Image smudge (int w, int h, uint32_t seed)
{
    juce::Image img (juce::Image::ARGB, std::max (1, w), std::max (1, h), true);
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const float nx = (x + 0.5f) / w * 2.0f - 1.0f, ny = (y + 0.5f) / h * 2.0f - 1.0f;
            const float fall = juce::jlimit (0.0f, 1.0f, 1.0f - (nx * nx + ny * ny));
            if (fall <= 0.0f)
                continue;
            // smeared oil: stretched low-frequency blots, with faint parallel swipe marks
            const float blot = valueNoise (x / (w * 0.18f), y / (h * 0.30f), seed);
            const float swipe = 0.5f + 0.5f * std::sin ((x * 0.8f + y * 0.35f) / (w * 0.02f) + 6.0f * valueNoise (x / (w * 0.3f), y / (h * 0.3f), seed + 5));
            const float a = fall * fall * juce::jlimit (0.0f, 1.0f, blot * 1.6f - 0.45f) * (0.7f + 0.3f * swipe);
            put (bd, x, y, { 1.0f, 1.0f, 1.0f }, a);
        }
    return img;
}

juce::Image woodTile (int w, int h, uint32_t seed, bool horizontal)
{
    juce::Image img (juce::Image::ARGB, std::max (1, w), std::max (1, h), false); // ARGB: the native backend's layout
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    const Rgb dark { 0.16f, 0.085f, 0.045f }, mid { 0.33f, 0.18f, 0.09f }, light { 0.46f, 0.27f, 0.13f };
    for (int py = 0; py < h; ++py)
        for (int px = 0; px < w; ++px)
        {
            // (x, y) in grain space: x across the grain, y along it
            const int x = horizontal ? py : px, y = horizontal ? px : py;
            // growth rings: stripes across x, wandering as they run along y
            const float warp = 9.0f * valueNoise (x / 60.0f, y / 140.0f, seed) + 3.0f * valueNoise (x / 18.0f, y / 40.0f, seed + 1);
            const float u = (x + warp * 4.0f) / 4.6f;
            const float ring = 0.5f + 0.5f * std::sin (u + 2.5f * valueNoise (x / 40.0f, y / 300.0f, seed + 2));
            const float figure = valueNoise (x / 25.0f, y / 90.0f, seed + 3);
            float t = std::pow (ring, 1.6f) * 0.7f + figure * 0.3f;
            // fine fibres and open pores (short dark streaks along the grain)
            const float fibre = (hash (x, y / 3, seed + 4) - 0.5f) * 0.10f;
            const float pore = hash (x, y / 9, seed + 5) > 0.985f ? -0.35f : 0.0f;
            t = juce::jlimit (0.0f, 1.0f, t + fibre + pore);
            if (horizontal)
                t = 0.22f + 0.78f * t; // a thin rail shows few rings: keep it in the same tonal range as the cheeks
            Rgb c = t < 0.5f ? Rgb { dark.r + (mid.r - dark.r) * t * 2, dark.g + (mid.g - dark.g) * t * 2, dark.b + (mid.b - dark.b) * t * 2 }
                             : Rgb { mid.r + (light.r - mid.r) * (t - 0.5f) * 2, mid.g + (light.g - mid.g) * (t - 0.5f) * 2,
                                     mid.b + (light.b - mid.b) * (t - 0.5f) * 2 };
            put (bd, px, py, c, 1.0f);
        }
    return img;
}

void drawAt (juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> dest, float opacity)
{
    if (img.isNull())
        return;
    g.setOpacity (opacity);
    g.drawImage (img, dest, juce::RectanglePlacement::stretchToFit);
    g.setOpacity (1.0f);
}

void roundShadow (juce::Graphics& g, juce::Point<float> c, float radius, float height, float strength)
{
    // the light comes from the top-left (kLight), so the shadow falls down-right, further for taller objects
    const float lx = -kLight.x / kLight.z, ly = -kLight.y / kLight.z;
    auto disc = [&] (juce::Point<float> centre, float r0, float r1, float alpha) {
        // opaque-ish up to r0, fading smoothly (eased) to nothing at r1
        juce::ColourGradient grad (juce::Colours::black.withAlpha (alpha), centre, juce::Colours::transparentBlack,
                                   centre.translated (r1, 0.0f), true);
        const float inner = juce::jlimit (0.0f, 0.99f, r0 / r1);
        grad.addColour (inner, juce::Colours::black.withAlpha (alpha));
        for (int i = 1; i < 4; ++i)
        {
            const float t = (float) i / 4.0f;
            grad.addColour (inner + (1.0f - inner) * t, juce::Colours::black.withAlpha (alpha * (1.0f - t) * (1.0f - t)));
        }
        g.setGradientFill (grad);
        g.fillEllipse (juce::Rectangle<float> (r1 * 2.0f, r1 * 2.0f).withCentre (centre));
    };
    // soft cast shadow: offset along the light direction, penumbra growing with height
    disc (c.translated (lx * height * 0.85f, ly * height * 0.85f), radius * 0.85f, radius + height * 1.15f, 0.42f * strength);
    // contact shadow: tight and dark right at the base
    disc (c.translated (lx * height * 0.12f, ly * height * 0.12f), radius * 0.97f, radius * 1.12f, 0.62f * strength);
}

} // namespace valvebox::materials
