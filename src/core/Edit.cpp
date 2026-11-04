#include "core/Edit.h"
#include <algorithm>
#include <random>
#include <vector>

namespace valvebox::edit
{
namespace
{
    template <typename T>
    void rotate (std::array<T, kMaxSteps>& a, int n, int amount)
    {
        if (n <= 1)
            return;
        amount = ((amount % n) + n) % n;
        std::rotate (a.begin(), a.begin() + (n - amount), a.begin() + n);
    }

    bool motionSelected (const Motion& m, PartMask parts, FxMask fx)
    {
        if (! m.used())
            return false;
        if (m.target >= kFxTarget)
            return fx.test ((size_t) (m.target - kFxTarget));
        return parts.test ((size_t) m.target);
    }
} // namespace

void moveData (Pattern& p, PartMask parts, FxMask fxMotion, int amount)
{
    const int n = p.numSteps();
    for (int i = 0; i < kNumParts; ++i)
        if (parts.test ((size_t) i))
            rotate (p.parts[(size_t) i].steps, n, amount);
    for (auto& m : p.motion)
        if (motionSelected (m, parts, fxMotion))
            rotate (m.values, n, amount);
}

void repeatData (Pattern& p, PartMask parts, FxMask fxMotion, int count)
{
    const int n = p.numSteps();
    count = std::clamp (count, 1, n);
    for (int i = 0; i < kNumParts; ++i)
        if (parts.test ((size_t) i))
        {
            auto& st = p.parts[(size_t) i].steps;
            for (int s = count; s < n; ++s)
                st[(size_t) s] = st[(size_t) (s % count)];
        }
    for (auto& m : p.motion)
        if (motionSelected (m, parts, fxMotion))
            for (int s = count; s < n; ++s)
                m.values[(size_t) s] = m.values[(size_t) (s % count)];
}

void randomData (Pattern& p, PartMask parts, uint32_t seed)
{
    const int n = p.numSteps();
    std::mt19937 rng (seed);
    for (int i = 0; i < kNumParts; ++i)
        if (parts.test ((size_t) i))
        {
            auto& st = p.parts[(size_t) i].steps;
            std::shuffle (st.begin(), st.begin() + n, rng);
        }
}

void shiftNotes (Pattern& p, PartMask synthParts, int semitones)
{
    for (int i = kFirstSynth; i < kFirstSynth + kNumSynthParts; ++i)
        if (synthParts.test ((size_t) i))
            for (auto& st : p.parts[(size_t) i].steps)
                st.note = (uint8_t) std::clamp ((int) st.note + semitones, 0, 127);
}

void randomNotes (Pattern& p, PartMask synthParts, uint32_t seed)
{
    const int n = p.numSteps();
    std::mt19937 rng (seed);
    for (int i = kFirstSynth; i < kFirstSynth + kNumSynthParts; ++i)
    {
        if (! synthParts.test ((size_t) i))
            continue;
        auto& st = p.parts[(size_t) i].steps;
        std::vector<int> idx;
        std::vector<uint8_t> notes;
        for (int s = 0; s < n; ++s)
            if (st[(size_t) s].on)
            {
                idx.push_back (s);
                notes.push_back (st[(size_t) s].note);
            }
        std::shuffle (notes.begin(), notes.end(), rng);
        for (size_t k = 0; k < idx.size(); ++k)
            st[(size_t) idx[k]].note = notes[k];
    }
}

int freeMotionSlot (const Pattern& p)
{
    for (int i = 0; i < kMaxMotion; ++i)
        if (! p.motion[(size_t) i].used())
            return i;
    return -1;
}

void copyPart (Pattern& dst, int dstPart, const Pattern& src, int srcPart)
{
    if (dstPart < 0 || dstPart >= kNumParts || srcPart < 0 || srcPart >= kNumParts)
        return;
    const auto& from = src.parts[(size_t) srcPart];
    auto& to = dst.parts[(size_t) dstPart];
    const bool sameKind = (isDrum (dstPart) && isDrum (srcPart)) || (isSynth (dstPart) && isSynth (srcPart))
                          || (isAccent (dstPart) && isAccent (srcPart));
    if (! sameKind)
    {
        // between kinds only the trigger data copies (p.56)
        for (int s = 0; s < kMaxSteps; ++s)
            to.steps[(size_t) s].on = from.steps[(size_t) s].on;
        return;
    }
    const Pattern srcCopy = &dst == &src ? src : Pattern(); // (copying within one pattern: read from a copy)
    const Pattern& s = &dst == &src ? srcCopy : src;
    const auto& f = s.parts[(size_t) srcPart];
    to.sound = f.sound;
    to.steps = f.steps;
    to.accentSw = f.accentSw;
    to.swingSw = f.swingSw;
    to.outBus34 = f.outBus34;
    // motion: drop the destination's, then copy the source's while there's room
    for (auto& m : dst.motion)
        if (m.target == dstPart)
            m = Motion();
    for (const auto& m : s.motion)
        if (m.target == srcPart)
        {
            const int slot = freeMotionSlot (dst);
            if (slot < 0)
                break; // more than 24: not copied
            dst.motion[(size_t) slot] = m;
            dst.motion[(size_t) slot].target = (int8_t) dstPart;
        }
}

bool copySound (Pattern& dst, int dstPart, const Pattern& src, int srcPart)
{
    if (dstPart < 0 || dstPart >= kNumParts || srcPart < 0 || srcPart >= kNumParts)
        return false;
    if (isDrum (dstPart) != isDrum (srcPart) || isSynth (dstPart) != isSynth (srcPart))
        return false;
    dst.parts[(size_t) dstPart].sound = src.parts[(size_t) srcPart].sound;
    return true;
}

void clearMotion (Pattern& p, PartMask parts, FxMask fx)
{
    for (auto& m : p.motion)
        if (motionSelected (m, parts, fx))
            m = Motion();
}

void clearParts (Pattern& p, PartMask parts)
{
    for (int i = 0; i < kNumParts; ++i)
        if (parts.test ((size_t) i))
            clearPartSteps (p.parts[(size_t) i]);
    clearMotion (p, parts, {});
}

void fillOnLengthen (Pattern& p, int oldLength, int newLength)
{
    if (newLength <= oldLength || oldLength < 1)
        return;
    const int per = p.lastStep, oldSteps = oldLength * per, newSteps = newLength * per;
    for (auto& part : p.parts)
        for (int s = oldSteps; s < newSteps && s < kMaxSteps; ++s)
            part.steps[(size_t) s] = part.steps[(size_t) (s % oldSteps)];
    for (auto& m : p.motion)
        if (m.used())
            for (int s = oldSteps; s < newSteps && s < kMaxSteps; ++s)
                m.values[(size_t) s] = m.values[(size_t) (s % oldSteps)];
}
} // namespace valvebox::edit
