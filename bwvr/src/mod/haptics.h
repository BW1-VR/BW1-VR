// The pulses of the hands. See haptics.cpp and docs/hand-model.md, "Haptics".

#pragma once

#include "haptics_math.h"

// One pulse of a kind (haptics_math::Kind) on the pointing hand. strength is
// 0 to 1: the share of the kind's amplitude for this event.
void HapticsPulse(int kind, float strength = 1.0f);
// The same on the hands of the mask: bit 0 the other hand, bit 1 the pointing
// hand.
void HapticsPulseHands(int hands, int kind, float strength = 1.0f);
// An effect of the game (GInterface::StartImmersion, 0 to 0x2F): the pulse of
// its kind on the pointing hand, or none for an effect that the mod has its
// own pulse for.
void HapticsGameEffect(int effect);
// For the slap of the game's effects 1 and 2: the speed of the hand in the
// last slap that the mod saw, and the least speed of a slap, in m/s.
void HapticsSlapSpeed(float mps, float slap_mps);
// haptics_pct, 0 to 200.
int HapticsPct();
// Once a report window.
void HapticsReport();
