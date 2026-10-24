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

// THE WORLD (world_haptics, docs/hand-model.md, "Haptics from the world").
// With world_haptics=0 these functions give no pulse.
// One pulse of a world kind on the hands of the mask, now. A hand that had a
// pulse of a control in the last 80 ms gets none.
void HapticsWorld(int hands, int kind, float strength = 1.0f);
// A pattern (haptics_math::Pattern) on the hands of the mask. The first pulse
// goes now, and HapticsWorldFrame sends the others. A pattern that runs
// starts again.
void HapticsWorldStart(int pattern, int hands, float strength = 1.0f);
void HapticsWorldStop(int pattern);
// Once a frame from hand_direct.cpp: true while the Hand holds a villager or
// an animal. The beat runs while the calls say true, and it stops when they
// say false or stop.
void HapticsWorldHold(bool living);
// Once a frame, from controllers.cpp: the next pulses of the patterns.
void HapticsWorldFrame();
// world_haptics, 0 or 1.
int HapticsWorldOn();
// Once a report window.
void HapticsReport();
