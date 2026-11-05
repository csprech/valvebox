#pragma once

// Pattern edit operations (the SHIFT + step key functions, p.54-57). They act on the pattern's
// valid steps (length x last step); data moved past the end wraps to the start.

#include "Model.h"
#include <bitset>

namespace valvebox::edit
{
using PartMask = std::bitset<kNumParts>;
using FxMask = std::bitset<kNumFx>;

/** MOVE: shift the selected parts' steps (and their motion sequences) by `amount` steps, wrapping. */
void moveData (Pattern& p, PartMask parts, FxMask fxMotion, int amount);
/** REPEAT: copy the first `count` steps over the rest of the pattern, again and again. */
void repeatData (Pattern& p, PartMask parts, FxMask fxMotion, int count);
/** RANDOM: shuffle the selected parts' steps to other steps. */
void randomData (Pattern& p, PartMask parts, uint32_t seed);
/** SHIFT NOTE: transpose the selected synth parts' notes (clamped to C-1..G9). */
void shiftNotes (Pattern& p, PartMask synthParts, int semitones);
/** SHIFT NOTE > RANDOM: shuffle the notes among a part's triggered steps (the same notes, new order). */
void randomNotes (Pattern& p, PartMask synthParts, uint32_t seed);
/** COPY PART: sound, steps and motion sequences of src's part onto dst's part. Between a drum and
    a synth part only the triggers copy. Motion sequences that would go past 24 are left out. */
void copyPart (Pattern& dst, int dstPart, const Pattern& src, int srcPart);
/** COPY SOUND: the sound only; false between a drum and a synth part (not allowed). */
bool copySound (Pattern& dst, int dstPart, const Pattern& src, int srcPart);
/** CLEAR MOTION for parts / effects. */
void clearMotion (Pattern& p, PartMask parts, FxMask fx);
/** CLEAR PART: the steps and motion sequences of the parts (sounds stay). */
void clearParts (Pattern& p, PartMask parts);
/** Erase (SHIFT + hold RESET/ERASE): the trigger at one step (synth notes and gates stay). */
inline void eraseTrigger (Pattern& p, int part, int step)
{
    if (part >= 0 && part < kNumParts && step >= 0 && step < kMaxSteps)
        p.parts[(size_t) part].steps[(size_t) step].on = false;
}
/** A free motion slot, or -1 (24 in a pattern). */
int freeMotionSlot (const Pattern& p);
/** WRITE copies data within the pattern to fill its length (p.64): a pattern lengthened after
    recording repeats its first bar(s) rather than playing silence. */
void fillOnLengthen (Pattern& p, int oldLength, int newLength);
} // namespace valvebox::edit
