// The Temple with the controllers: the ray pointer in a room, its buttons
// and the camera of the room. See temple.cpp and docs/controllers.md, "The
// Temple".

#pragma once

// Once a frame, from ControllersFrame, before the pointer is worked out. It
// latches the state of the Temple for the frame, and it writes the log.
void TempleFrame(unsigned long long frame);

// The player is in the Temple (the latch of the last TempleFrame).
bool TempleInside();

// The pointing hand points with a ray in a room of the Temple now: the
// player is in the Temple, no guide of the game plays, and temple_pointer is
// 1. The direct hand then steps aside, as it does for a dialog box.
bool TempleRayOn();

// temple_click now: 1 the trigger and the grip are both the Action button of
// the game, 0 the plain buttons.
int TempleClick();

// The distance along the ray of the pointing hand to the thing that the
// player sees it on. origin and direction are the ray in the world, and eye
// is the eye. units is the world units in a meter of the room. Called once a
// frame, from Compute. False when the ray is off.
bool TempleRayDistance(const float origin[3], const float direction[3],
                       const float eye[3], float units, float* along);

// THE TURN IN A ROOM (temple_turn). The stick of the pointing hand turns the
// player in a room, and not the camera of the land: the player is in the
// Temple and temple_turn is 1.
bool TempleTurnOn();
// One step of the turn: 1 to the right, -1 to the left. False when no turn
// was made (a guide plays, a dialog box is up, or the head has no pose).
bool TempleTurn(int direction);

// For the cards of the hands: the room of the Temple that the ray points in
// (0 is the hall), and -1 when the ray is off or a dialog box is up.
int TempleCardRoom();

// The report of a window, from ControllersReport.
void TempleReport();

// THE TAPS ON THE LAND (docs/village.md, "The door of the Temple and the leash
// posts"). For the cards of the hands: the lines while the Hand's point is at
// the door of the Temple or at a leash post. The return value is the count of
// lines, 0 with no such thing at the Hand. A line is a MiracleLine of
// miracle.h.
struct MiracleLine;
int TempleLandHint(MiracleLine* lines);
// A test place for the word "seek" (seek_plan): 0 the door of the Temple, 1
// the next leash post. False when the draw of this frame did not have it.
bool TempleTestPlace(int place, float out[3]);

// TEST KEYS for desktop runs. The pixel of a scan of the room, in the place
// of the pixel of the ray. False when no scan runs.
bool TempleTestPixel(float* x, float* y);
// The next target of temple_test_targets, a point in the world, for the fake
// controller word "aimtemple". False when there is none.
bool TempleTestTarget(float world[3]);
