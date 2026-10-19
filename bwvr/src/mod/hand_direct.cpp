// The direct hand: the game's Hand sits at the player's controller
// in 3D, not on the land under a ray. See docs/hand.md, "The direct hand".
//
// THE GAME BUILDS THE HAND FROM A RAY AND A DEPTH. In the Normal and the
// Holding states, ObtainRequiredHandPosition 0x005B5E70 casts a ray through
// the Hand's screen point (+0x485C) and puts the Hand at a distance along it
// with ScreenPointToWorldAtDistance 0x0074CAF0. Then the pose builder
// fn_0046E160 commits the position. So the direct hand feeds all three:
//
//   THE RAY. UnprojectScreenPoint 0x0081B370 gets the Hand's screen point
//   from ScreenPointToWorldAtDistance, and from the yaw in PrepareForDrawing.
//   pointer_map.cpp hooks it and calls HandDirectUnproject, which turns the
//   ray to the exact target point. The pixel alone is half a pixel off.
//
//   THE DEPTH. A post hook on CHand::SetDistanceFromView 0x0046C0D0 writes
//   +0x4854 = |target - camera| over the game's clamp of 2 units, and the
//   Hand's size to +0x4834.
//
//   THE POSE. A hook on the pose builder writes the target through its
//   position argument and to +0x78. In the Holding state the game lifts the
//   Hand so the object hangs at the pointer, and this puts the Hand back at
//   the controller, with the object under it.
//
// The pointer is the target seen from the eye (controllers.cpp), so the
// render pick and the land lookup see what is under the Hand.
//
// THE PICK. The game picks what is under the pointer pixel, one frame late,
// in GInterface+0x400 (next) and +0x3C8 (now). With the direct hand, a
// holdable object far behind the Hand is also under that pixel. So the draw's
// candidates are kept from the first eye pass, the nearest holdable object
// within a grab radius of the Hand goes in the next record, and a holdable
// object that is not near the Hand comes out of it. Buildings, signposts and
// scrolls keep the ray.
//
// ONE POINT (docs/grab.md, "One point for each interaction"). The Hand's
// point is the tip of the real index finger, where the glow is. A press of
// the grip acts on the nearest object within hand_pick_cm of that point: an
// object to hold, a bulk source, or an object that the game taps (a scroll,
// a house). While such an object or the Creature is near the point, the grab
// from a distance is off. A forest gives a tree, at the Hand and at a
// distance. The rope of the leash ends at the palm of the mod's hand.
//
// ONCE A FRAME. The game can place the Hand once per eye. The target, the
// size and the land test are latched once a frame, in HandDirectFrame and on
// the first use. The hooks only write those values, so a second eye pass
// writes the same numbers again.
//
// Only the Normal and the Holding states are placed. The Camera state (the
// land drag) and every other state place the Hand as the game does.
//
// THE THROW. The game throws with the velocity in CHand+0x48C8..0x48D0,
// world units per second. It reads that in the release action FUN_005D4DB0
// and cuts its length to 124 (0x00C371FC). While the controller places the
// Hand, a hook on the release action writes the hand's velocity there: the
// direction of the reach point and the real speed of the grip, over 80 ms.
// throw_gain_pct scales it, and the mod cuts the length to the cap first, so
// it can log what it sent.
//
// THE GRIP HOLDS. controllers.cpp lets go with a click when the grip opens,
// and the game runs the release some frames later. So the velocity is
// latched at the opening, over the 80 ms before it (HandDirectGripOpened),
// and the release uses that. HandDirectGripFacts tells the grip whether an
// object is in the Hand, whether the pick up is over and whether the game
// shows the throw spring. See docs/grab.md, "The grip holds".
//
// THE LAND. The point stops 5 mm of the room over the land, so it touches
// the land and the objects on it at every scale (ResolveLand).
//
// THE CREATURE BY TOUCH (docs/creature.md, "The Creature by touch"). A press on
// the Creature puts the Hand in the Creature state (7). There the game moves
// its camera to the Creature and places the Hand by the Creature's depth.
// With the controller:
//
//   NO CAMERA. A hook on CHand::OperateInteractionCamera keeps the game from
//   pushing CameraModeCtrInteract (creature_camera). A short press still
//   clicks the leash, by the real time of the press.
//
//   THE HAND STAYS AT THE CONTROLLER. The depth hook writes the depth and
//   the size in the Creature state too. The state multiplies the size by
//   the Creature's size^0.5, so the hook divides by it first.
//
//   CONTACT. The body is a capsule. The game's mesh ray test counts a hit
//   only while the Hand's point is in it, so the Creature cannot be touched
//   from a distance. A Hand that went into the capsule since the last frame
//   forces the game's slap branch, through its test of the screen center.
//
//   THE SPEED. The game reads the speed from two points at the Hand's depth.
//   The mod aims the ray of the last point across the line of sight, so the
//   game reads the real speed of the hand times k, and a slap needs
//   creature_slap_mps of real speed at any size and scale.
//
// While the Hand is in the Creature state, the pick keeps the Creature, so
// the state lasts until the trigger opens.
//
// ON THE TABLE (docs/tabletop.md, "The hand on the table"). controllers.cpp
// puts the target on the table with no reach curve. Here the Hand gets the
// length table_hand_cm in the room. Two hooks, only with play_mode=3 and 4,
// make the pick work at that scale: the screen test offers the objects near the
// Hand, and the interface step gets the land under the Hand for the influence
// test. The throw turns the speed of the real hand into units at the table
// scale. The game lets go only inside the player's influence, and a swing at
// that scale often ends outside it. So a throw that ends outside goes from the
// last point of the swing inside it (docs/tabletop.md, "The throw from inside
// the influence"). The Creature's test keeps the speed at the life size scale,
// so a stroke and a slap need the same hand motion as off the table.
//
// THE GLOW. hand_glow.cpp draws a small disc at the target in each eye pass,
// so the player sees the point the Hand interacts from. HandDirectGlowFacts
// gives it the point and what the game says a press does there.
//
// THE OVERLAY. overlay.cpp draws a test hand at the grip, and logs it beside
// the game's Hand. HandDirectHandPosition gives it the game's Hand position.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma intrinsic(_ReturnAddress)

#include "../common/bw_addresses.h"
#include "MinHook.h"
#include "catch_math.h"
#include "controller_math.h"
#include "controllers.h"
#include "creature_command.h"
#include "creature_stay.h"
#include "far_choice_math.h"
#include "far_grab.h"
#include "fight_math.h"
#include "take_stream.h"
#include "forest_take.h"
#include "haptics.h"
#include "miracle.h"
#include "village.h"
#include "near_math.h"
#include "table_math.h"
#include "throw_physics.h"
#include "tree_body.h"
#include "tree_plant_math.h"

extern void ModLog(const char* format, ...);

// controllers.cpp
bool ControllerDirectHand(float point_out[3], float* reach_factor,
                          float* real_distance);
bool ControllerDirectMotion(float reach_room[3], float grip_room[3],
                            float room[9]);
int ControllersHandScalePct();
int ControllersHandCm();
// head_pose.cpp: game units in a real meter.
float HeadPoseScale();
int ControllersThrowGainPct();
// The Creature by touch: the real speed of a slap, the reach of the body
// past its 2D radius, and whether the game's wait of 1 s stays.
float ControllersCreatureSlapMps();
int ControllersCreatureTouchCm();
bool ControllersCreatureSlapWait();
// head_pose.cpp
float HeadPoseScale();
// tabletop.cpp: the Hand's size and the scale while the table owns the view,
// and 0 otherwise. See docs/tabletop.md, "The hand on the table".
float TabletopHandSize();
float TabletopViewScale();
// tabletop.cpp: true while the table or the God view owns the view.
bool TabletopOwnsView();
bool TabletopGodOwnsView();
// head_camera.cpp: the rotation and the position that the scene was drawn
// from, in the world.
bool HeadCameraRenderBasis(float basis_out[9], float position_out[3]);
// tabletop.cpp: the room of the God view in this frame. False when the God
// view does not own this frame.
bool TabletopRoom(float origin[3], float basis[9], float center[3],
                  float* gain);
// The state of the game camera at this moment, for a script that asks about
// the camera between two frames (docs/god-view.md, "The player that a script
// sees"). script_camera.cpp: 0 a camera mode of the player is on top, 1 a story
// or a follow mode, -1 not known. cutscene_window.cpp: the cinema bars are up.
// comfort_camera.cpp: the game moves its own camera to a new place.
int ScriptCameraOwnerNow();
bool CutsceneWindowBarsUp();
bool ComfortCameraGameMoves();
// comfort_camera.cpp: the clock of the help at the last camera help event
// 0x300 + low, and now. controllers.cpp: a grip holds the land.
bool ComfortCameraHelpClock(int low, int32_t* at, int32_t* now);
bool ControllersGrabHeld();
// tabletop.cpp: the teleport of the God view, for the test word "go".
bool TabletopGodTeleport(const float target[3]);
// temple.cpp: the door of the Temple (0) or the next leash post (1), for the
// test words "door" and "post".
bool TempleTestPlace(int place, float out[3]);
// hand_glow.cpp: the place of a pixel pair of the two eye pictures, for the
// test word "picture".
bool HandGlowPicturePoint(float lx, float ly, float rx, float ry,
                          float out[3], float* half_x, float* half_y);
// hand_exact.cpp: the exact Hand. See docs/hand.md, "The exact Hand".
void HandExactInstall();
void HandExactReport();
// hand_exact.cpp: the middle of the palm of the mod's hand in the world, with
// the frame and the Hand state of the exact root that it comes from.
bool HandExactPalm(float out[3], uint64_t* frame, int32_t* state);
// controllers.cpp: the grab from a distance (docs/grab.md, "The grab from a
// distance"). The aim ray in the world, and whether the pointing hand may act
// on a far object now.
bool ControllersFarRay(float origin[3], float direction[3],
                       float* units_per_meter, float eye[3]);
bool ControllersFarAllowed(const char** why);
// controllers.cpp: a pulse on the pointing hand.
void ControllersGesturePulse(float amplitude, float seconds);
// controllers.cpp: the number that a test key with a frame compares with it.
uint64_t ControllersTestFrame(uint64_t key_frame, uint64_t frame);
// head_pose.cpp: the head center in the room, in units at the life size scale.
bool HeadPoseHeadInRoom(float translation_out[3]);
// The facts of the press of the game, for the grip and for the log of a rock
// that splits.
bool HandDirectGripPress(bool* game_holds, bool* moving,
                         uint32_t* press_age_ms);

namespace {

// CHand::SetDistanceFromView, thiscall (float), ret 4.
const uintptr_t kSetDistanceFromView = 0x0046C0D0;
// The pose builder, thiscall (LHMatrix* out, const LHPoint* position,
// const LHPoint* up), ret 0xC.
const uintptr_t kPoseBuilder = 0x0046E160;
// GInterface::SendObjectDrawCollision, thiscall (Object*, float distance,
// LH3DObject*), ret 0xC.
const uintptr_t kSendObjectDrawCollision = 0x005D56C0;
// A pick record: thiscall (Object*, float distance), ret 8. It writes the
// object at +0x18 and the distance at +0x1C when IsInteractable is 1, and
// clears both otherwise.
const uintptr_t kRecordPick = 0x005D5E40;
// LH3DIsland::GetAltitude, fastcall (const LH3DMapCoords*), the height in
// st0. The map coordinates are ints of x * 6553.6 and z * 6553.6.
const uintptr_t kGetAltitude = 0x00803090;
const float kMapScale = 6553.6f;
// The release action, thiscall on GInterface, no arguments, plain ret. It
// reads the throw velocity from the CHand and sends packet 0x4D.
const uintptr_t kReleaseAction = 0x005D4DB0;
// HandStateCreature::Update, thiscall (float dt, LHMatrix*), ret 8.
const uintptr_t kCreatureUpdate = 0x005B17B0;
// The game's cap on the throw speed, a float in .data (124).
const uintptr_t kThrowCap = 0x00C371FC;
const uintptr_t kGame = 0x00D0195C;
const uintptr_t kMyInterface = 0x00555850;
// Written by the pose builder only.
const uintptr_t kHandWorld = 0x00C5E680;
// THE PICK ON THE TABLE. fn_00868C80 is the screen test of each object that
// the draw adds: fastcall, the bounds in ecx, the LH3DObject on the stack,
// ret 4. It returns 1 when the object is in view. It sets 0x00EA1AF0 then,
// and it sets 0x00EA1AD0 when the pick pixel (0x00EA1AC8) is inside the
// object's circle on the screen. The draw offers an object to
// SendObjectDrawCollision only with 0x00EA1AD0 set (Game3DObject::
// AddForDrawing, 0x0063B653). On the table a villager is 3 pixels across, so
// the draw offers almost nothing.
const uintptr_t kScreenTest = 0x00868C80;
const uintptr_t kPickInView = 0x00EA1AF0;
const uintptr_t kPickUnder = 0x00EA1AD0;
// THE LAND UNDER THE HAND ON THE TABLE. fn_005D1120, a step of the
// interface in the game logic (thiscall, no arguments, plain ret), sets
// GInterface+0x48 from the player's influence at the land point of the next
// pick record (+0x3F0). Outside the influence the hand mode of a villager is
// 0x12 (touch) and not 9 (pick up), because of
// Object::InterfaceMustBeInInfluenceForInteraction. On the table the land
// point is where the pointer ray meets the land behind the Hand, and in runs
// th3 and th4 the mode went from 9 to 0x12 and back on every frame.
const uintptr_t kInterfaceStep = 0x005D1120;
const int kFaceNextLand = 0x3F0;  // MapCoords: x and z times 6553.6, then 0
const int kFaceNextLandDistance = 0x3FC;  // a float: the land under the pointer
// THE THROW FROM INSIDE THE INFLUENCE (docs/tabletop.md, "The throw from inside
// the influence"). The release action lets go only with GInterface+0x48 set
// (0x005D4E24). fn_005D1120 sets +0x48 from the influence at +0x3F0 (0x005D1144
// to 0x005D1164) and then runs the step of the interface action +0x44, which
// for action 12 is the release action (the table of steps at 0x00D17DA8, 0x30
// bytes an action). Without +0x48 the release action turns the throw spring off
// (0x005D5150), and fn_005D18F0 refuses the drop, so the object stays in the
// Hand. Influence::CalculatePlayerInfluence is cdecl (const MapCoords*,
// GPlayer*, int, int, int) with the value in st0, and fn_005D1120 calls it with
// 0, 1,
// 1. Slot +0x1C of GInterface gives the GPlayer.
const uintptr_t kPlayerInfluence = 0x005CD170;
const int kFaceAction = 0x44;
const int32_t kReleaseActionIndex = 12;
const int kFaceGetPlayer = 0x1C;  // a vtable slot, thiscall, no arguments
const int kIsSpellSeed = 0x4C4;   // a vtable slot of Object
const int kIsAnyKindOfTree = 0x478;  // a vtable slot of Object

// THE BULK TAKE (docs/grab.md, "The bulk take"). A press on a pile of food or
// wood, a field or a fish farm starts the game's locked select. The start
// packet runs FUN_005DA950: the object's NetworkFriendlyStartLockedSelect
// (slot 0x6D0) puts a new Pot with the first amount in the Hand, and
// FUN_005DC110 writes the object to GInterfaceStatus+0x3C and 0 to +0x40.
// Each game turn GInterfaceStatus::Process 0x005DC4E0 adds 1 to +0x40 and
// calls ProcessInInteract (slot 0x808). That moves more of the resource into
// the Pot, plays the tick, and shows the count with ForceToolTips(0xEEA, Pot
// amount), the tooltip "Amount: %3.0f". The release of the button
// (GInterface+0x39 bit 0x40, FUN_005D4830) sends packet 0x1C, and FUN_005DAA10
// ends it: slot 0x6EC stops the effect and the sound, and +0x3C goes back
// to 0. The Pot stays in the Hand. [V, all of it]
const int kStatusTakeObject = 0x3C;   // GInterfaceStatus: the object taken from
const int kStatusTakeTurns = 0x40;    // the game turns of the take
const int kIsLockedInInteract = 0x6A0;  // a vtable slot: 1 for a bulk source
const int kValidForTake = 0x6CC;  // ValidForLockedSelectProcess(status), ret 4
const int kPotAmount = 0x70;      // Pot: the amount, an unsigned int
// THE TAKE ENDS WHEN THE BUTTON GOES UP (docs/grab.md, "The bulk take"). The
// step of the take, FUN_005D4830 (action 4 in the table at 0x00D17DA8), acts
// on the release bit of GInterface+0x39. It reads the object of the NEXT PICK
// RECORD (+0x400, at 0x005D483A), not the source of the take, and calls
// FUN_005D49B0. That sends the end packet 0x1C (FUN_005D2210) only when the
// object of the record has bit 0x10 of +0x24 set (a take holds it) and its
// slot 0x6E4 gives 1. Else it sets the interface action to 5 and sends
// nothing, and the take goes on with the button up. With the mouse the game
// holds the Hand over the source, so the record holds the source. With the
// controller the Hand is at the real hand, which can be anywhere. [V]
// FUN_005D49B0 is thiscall on GInterface with no arguments and a plain ret.
const uintptr_t kTakeEndStep = 0x005D49B0;
// The handler of packet 0x1C, FUN_005DAA10: thiscall on GInterface (Object*),
// ret 4. It calls slot 0x6EC of the source (StopMultiPickup), writes 0 to
// GInterfaceStatus+0x3C and clears bit 0x10 of Object+0x24. [V]
const uintptr_t kTakeEndPacket = 0x005DAA10;
// GInterface::ResetActionState, thiscall with no arguments and a plain ret.
const uintptr_t kResetActionState = 0x005D29C0;
// The interface actions of a take (GInterface+0x44), from the table of steps
// at 0x00D17DA8: 4 while the take runs, 5 after a release that did not find
// the source, and 6 while the end packet is on its way. [V]
const int32_t kTakeAction = 4;
const int32_t kTakeWaitAction = 5;
const int32_t kTakeEndAction = 6;
// A GAME TURN NOW (docs/throw.md, "The release at once").
// GGame::LocalTimerSaysDoATurn, thiscall on GGame with no arguments and a
// plain ret. It gives 1 when the game time is at or past 100 ms times the
// turn count (GGame+0x205A40), and 0 in a single player game that is paused
// (bit 4 of GGame+0x14). GGame::ProcessNetworkPackets 0x0054CC30 asks it at
// 0x0054CD45 and runs one game turn for a yes. [V]
const uintptr_t kDoATurn = 0x0054C4A0;
const int kGameFlags = 0x14;  // GGame: bit 4 is the pause
// THE CLOCK OF A PRESS (docs/grab.md, "The grip holds"). fn_005D1740 keeps
// the clock and the game turn of a press on an object in GInterface+0x454
// and +0x458. The clock is the one that LH3DRender::StartFrame writes at
// 0x0082F188, in ms. The game turn is GGame+0x205A40. The step of the pick up
// action compares the two with 225 ms (controller_math::GamePressAge). [V]
const uintptr_t kGameClock = 0x00EA9EB0;
const int kGameTurn = 0x205A40;
const int kFacePressClock = 0x454;
const int kFacePressTurn = 0x458;
// GInterfaceStatus: a byte, not 0 while the game has an object in its Hand.
// fn_005D1330 reads it at 0x005D133B to tell the press of a release from a
// press on an object. [V]
const int kStatusHolding = 0x90;

typedef void(__fastcall* SetDistanceFn)(void* self, void* unused,
                                        float distance);
typedef void(__fastcall* ReleaseFn)(void* self, void* unused);
typedef void(__fastcall* CreatureUpdateFn)(void* self, void* unused, float dt,
                                           float* matrix);
typedef void(__fastcall* PoseBuilderFn)(void* self, void* unused, float* out,
                                        float* position, const float* up);
typedef int(__fastcall* SendCollisionFn)(void* self, void* unused,
                                         void* object, float distance,
                                         void* mesh);
typedef void(__fastcall* RecordPickFn)(void* record, void* unused,
                                       void* object, float distance);
typedef float(__fastcall* GetAltitudeFn)(const int32_t* coords);
typedef void*(__fastcall* MyInterfaceFn)(void* game, void* unused);
typedef int(__fastcall* VirtualIntFn)(void* self, void* unused);
typedef int(__fastcall* VirtualIntArgFn)(void* self, void* unused, void* arg);
typedef float(__fastcall* VirtualFloatFn)(void* self, void* unused);
typedef int(__fastcall* ScreenTestFn)(const void* bounds, void* unused,
                                      const uint8_t* object);
typedef void(__fastcall* InterfaceStepFn)(void* self, void* unused);
typedef float(__cdecl* PlayerInfluenceFn)(const int32_t* coords, void* player,
                                          int a, int b, int c);
typedef void*(__fastcall* GetPlayerFn)(void* self, void* unused);

// THE CREATURE BY TOUCH. See docs/creature.md, "The Creature by touch".
//
// CHand::OperateInteractionCamera, thiscall (int hand mode), ret 4. Its one
// caller is CHand::GetRequiredState (0x0046CEA0), for an empty Hand.
const uintptr_t kInteractCamera = 0x0046CB60;
// The mesh ray test, thiscall on a hit record, 8 stack arguments, ret 0x20.
// The Creature state calls it at two places: the ray of the Hand's point,
// and when that misses, the ray of the pixel between the current and the
// last screen point.
const uintptr_t kMeshRay = 0x00865020;
const uintptr_t kMeshRayHand = 0x005B19F1;    // the return address of each
const uintptr_t kMeshRayMiddle = 0x005B1ABC;
// The press of a fight on a Creature (fn_005D3EF0) calls it at 0x005D4033 for
// the ray from the stored camera through the pointer of the interface.
const uintptr_t kMeshRayFight = 0x005D4038;
// fn_004813D0, thiscall on an LH3DCreature, no arguments, plain ret: the
// position of the body bone (+0x51D8), through GetSafeBuffer.
const uintptr_t kBodyBone = 0x004813D0;
// GGame::SetPacket, thiscall (int packet), ret 4.
const uintptr_t kSetPacket = 0x00550DD0;
const int32_t kPacketLeash = 0x5F;
// The game's width, the low 16 bits. The Creature state takes half of it as
// the screen center (0x005B198A).
const uintptr_t kGameWidth = 0x00E85058;

typedef void(__fastcall* InteractCameraFn)(void* self, void* unused,
                                           int32_t mode);
typedef int(__fastcall* MeshRayFn)(void* self, void* unused, void* a,
                                   void* b, void* c, void* d, void* e,
                                   void* f, void* g, void* h);
typedef const float*(__fastcall* BodyBoneFn)(void* self, void* unused);
typedef void(__fastcall* SetPacketFn)(void* self, void* unused,
                                      int32_t packet);

InteractCameraFn o_InteractCamera = nullptr;
MeshRayFn o_MeshRay = nullptr;

ScreenTestFn o_ScreenTest = nullptr;
InterfaceStepFn o_InterfaceStep = nullptr;
// GGame::LocalTimerSaysDoATurn (kDoATurn).
VirtualIntFn o_DoATurn = nullptr;
SetDistanceFn o_SetDistanceFromView = nullptr;
PoseBuilderFn o_PoseBuilder = nullptr;
SendCollisionFn o_SendObjectDrawCollision = nullptr;
ReleaseFn o_ReleaseAction = nullptr;
CreatureUpdateFn o_CreatureUpdate = nullptr;

// CHand fields. See docs/hand.md, "CHand fields".
const int kHandPosition = 0x78;
const int kHandSize = 0x90;
const int kHandScale = 0x4834;
const int kHandMaxDistance = 0x4838;
const int kHandDistance = 0x4854;
const int kHandScreen = 0x485C;
const int kHandState = 0x4878;
const int kHandSpring = 0x48B8;  // the smoothed point, then its speed
const int kHandHeld = 0x4904;
const int kHandPrevScreen = 0x4864;  // the screen point of the last frame
const int kHandThrow = 0x48C8;       // the throw velocity, three floats
const int kHandCreature = 0x49B8;    // the Creature, an LH3DCreature
const int kHandCreatureObject = 0x49B4;  // the same Creature, as an Object
const int kHandCameraPushed = 0x49C0;    // 1 while CtrInteract is pushed
const int32_t kStateNormal = 1;
const int32_t kStateHolding = 4;
const int32_t kStateCreature = 7;
const int32_t kStateGrain = 8;     // a miracle (a SpellSeed) is in the Hand
const int32_t kModeCreature = 0x13;  // the interface hand mode of state 7

// HandStateCreature fields, from the state object. See docs/creature.md, "How
// the plain game does it".
const int kStateHand = 4;        // the CHand
const int kStateTotal = 0xD8;    // the stroke and slap total, -1 to 1
const int kStateTime = 0xDC;     // ms in the state, int. A slap needs 1000
const int kCreatureSize = 0x90;  // LH3DCreature: its size
// LH3DCreature+0x526C: 1 while the Creature is ready for the Hand, and 0
// skips the state's update (0x005B17EA). A press on the Creature asks for it
// (Creature+0x3CC = 1). Creature::ProcessState then calls fn_00477D00
// (0x00472FAB), which waits until the Creature does no body action and sets
// the flag (fn_004806C0). The end of the press clears it (0x00476EF1). So it
// is 0 at each time before a press: it does not say that there is a body. [V]
const int kCreatureLive = 0x526C;
const int kCreatureGetPlayer = 0x1C;  // a vtable slot of the Creature object
// GGame: the players from +0x18, 0xA60 bytes each, and the local player's
// index in a byte at +0x205A59 (0x0046CC92).
const int kGamePlayers = 0x18;
const int kGamePlayerSize = 0xA60;
const int kGameLocalPlayer = 0x205A59;

// GInterface fields. See docs/hand.md, "The records in GInterface".
const int kFaceLocked = 0x38;       // a byte: no pick while it is set
const int kFaceHand = 0x3A0;        // the CHand
const int kFaceStatus = 0x39C;      // the local GInterfaceStatus
const int kFaceObject = 0x3C8;      // the object under the Hand, now
const int kFaceNextRecord = 0x3E8;  // the record the draw fills
const int kFaceNextObject = 0x400;
const int kFaceHandMode = 0x3AC;    // the interface hand mode
const int kFaceFlags = 0x39;        // a byte: bit 0x40 asks for the release
const int kFaceInfluence = 0x48;    // 1 while the record's land point is in
                                    // the player's influence (fn_005D1120)
const int32_t kModeThrowSpring = 0x17;  // the hand mode of a press that holds

// Object and LH3DObject. The LH3DObject of a Game3DObject is at +0x40, and
// its translation is at +0x38.
const int kObjectMesh = 0x40;
const int kObjectFlags = 0x24;       // a byte: bits 0x04 and 0x20
const int kObjectOtherFlags = 0x0A;  // a byte: bit 0x10
const int kMeshTranslation = 0x38;
const int kIsInteractable = 0x190;
const int kIsCannotBePickedUp = 0x180;
const int kValidForPlaceInHand = 0x6FC;
const int kGet2DRadius = 0x64;
// THE PICK AT THE GLOW (docs/grab.md, "One point for each interaction").
// Object::GetHeight 0x00638120 (slot 0x42C, a float): the mesh's height times
// the object's scale. Object::GetBoundingSphere 0x00637730 puts the middle of
// an object half of it over the object's point, so the point is the base. [V]
const int kGetHeight = 0x42C;
// THE BOX OF A MESH. Slot 0xF8 of an LH3DObject (thiscall, no arguments)
// gives the box of its mesh, or null: the middle at +0x18 (3 floats) and the
// half sizes at +0x24. Object::Get2DRadius reads +0x24 and +0x2C, and
// Object::GetHeight reads +0x28. ScriptHighlight::Draw takes the middle
// through the matrix of the LH3DObject (rows at +0x14, +0x20 and +0x2C, the
// translation at +0x38) at 0x00709ED7, and gives that point to
// GInterface::SendInvisibleDrawCollision as the place of the scroll for the
// game's pick (0x0070A2D7). So the middle of the box in the world is where
// the game itself has a scroll. [V]
const int kMeshBox = 0xF8;
const int kBoxMiddle = 0x18;
const int kBoxHalf = 0x24;
const int kMeshRows = 0x14;
// GameThing::IsCreature (slot 0x34): 1 for a Creature.
const int kIsCreature = 0x34;
// GameThingWithPos::IsAnimal (slot 0x454, no arguments, a plain return): 1 for
// an Animal (Animal::IsAnimal 0x00417880). Found as the slot of
// IsAnyKindOfTree (0x478) less the distance of the two stubs in the vtable of
// GameThingWithPos (0x008A9EB8 and 0x008A9EDC). [V]
const int kIsAnimal = 0x454;
// GameThingWithPos::IsLiving (slot 0x3C4, no arguments, a plain return): 0 for
// GameThingWithPos (0x00402350, xor eax, eax) and 1 for Living (0x00417460,
// mov eax, 1). Living is the base of each villager (Villager and
// SpecialVillager, the class of a quest character), of each animal and of the
// Creature. The slot is the entry 0x008A9E28 of the vtable of
// GameThingWithPos, 0x90 before its IsAnimal entry 0x008A9EB8. 32 vtables
// have Living::IsLiving and Animal::IsAnimal 0x90 apart. [V]
const int kIsLiving = 0x3C4;
// InterfaceValidToTap (slot 0x740, thiscall (GInterfaceStatus*), ret 4): 1
// when a press of the Action button taps the object. A scroll gives 1 while it
// has a script (ScriptHighlight 0x0070ADD0), and a house gives 1 (Abode
// 0x00406820). The right press FUN_005D1330 calls fn_005D1740 for such an
// object (0x005D150F), that calls fn_005D3930, and fn_005D38A0 sends packet
// 0x20 when the slot gives 1, the object can be picked up (slot 0x180 gives
// 0), and the land under the pointer is in the influence or the object does
// not need it (slot 0x714). The handler fn_005DA650 then calls InterfaceTap
// (slot 0x744). The left press fn_005D1AB0 grabs the land and taps nothing.
// So the grip activates a scroll, and the trigger does not. [V]
const int kInterfaceValidToTap = 0x740;
// THE LEASH (docs/grab.md, "The leash at the mod's hand").
// GInterface::GetLeashStartPoint, thiscall (LHPoint* out, bool), ret 8.
// GInterface::UpdateAllLeashes 0x005D9130 calls it with 0 once a frame, from
// GInterface::PostDrawProcess (0x005CEAB3), and gives the point to the rope
// (fn_00848970) as its end at the Hand. With the leash on the Hand
// (GLeashStatus+0x24 is null) and the local interface, the point is the
// translation of the first matrix of the Hand's pose array, *(CHand+0x47F0) +
// 0x24 (0x005D90B9): the wrist bone. [V]
const uintptr_t kGetLeashStartPoint = 0x005D8EA0;
const int kStatusLeash = 0x12C;  // GInterfaceStatus: the GLeashStatus
const int kLeashOn = 0x14;       // GLeashStatus: 1 while the leash is on
const int kLeashObject = 0x24;   // GLeashStatus: the object it is tied to
// THE GAME'S SEARCH AT THE PALM (docs/grab.md, "Why the game's own paths must
// not act"). fn_005D39E0, thiscall on GInterface (const MapCoords*), ret 4. The
// right press FUN_005D1330 calls it when the next pick record holds no object
// (0x005D13A1 and 0x005D14A6). GLandscape::Draw casts a ray from the camera
// through the middle of the bones of the Hand's mesh (0x005E4848), and keeps
// the land point that it meets in 0x00D1A3A0 (x) and 0x00D1A3A8 (z), with the
// flag 0x00D2017C. The search takes the nearest Object within 5 units of that
// land point (0x005D3C41). When that object is not farther from the point than
// the land point of the record (+0x3F0) is (0x005D3CD0), the search puts the
// object in the record (0x005D3CE9), and the press acts on it. [V]
//
// With the mouse the land point of the record is the land under the finger,
// a few units from the land under the palm, so the search finds only an
// object that the Hand covers. With the real hand the record's land point is
// the land straight under the Hand's point, and the land behind the palm is
// the land that the eye sees through the hand. With the hand 20 cm over the
// land, the two are far apart. So each press with no object at the Hand's
// point took the nearest object to the land behind the palm: a tree under a
// scroll.
const uintptr_t kPalmSearch = 0x005D39E0;
const uintptr_t kPalmLandFlag = 0x00D2017C;
const uintptr_t kPalmLandX = 0x00D1A3A0;
const uintptr_t kPalmLandZ = 0x00D1A3A8;
// GameThingWithPos::IsScriptHighlight (slot 0x48C): 1 for a scroll and for a
// sign.
const int kIsScriptHighlight = 0x48C;
// THE SCROLL FROM ITS DRAW (docs/village.md, "How the game draws a scroll").
// ScriptHighlight::Draw 0x00709C60, thiscall, no arguments, plain ret. [V]
//  - It does nothing when +0x5C is not 0, when the scroll has no mesh
//    (+0x40), and for a scroll that is not silver while the flag at
//    *(GGame+0x250090)+0x80 is 0 (0x00709C8B to 0x00709CB9).
//  - The type is the index of the info at +0x28 in the table at 0x00D96390,
//    136 bytes each: 0 bronze, 1 silver, 2 gold, 3 and 4 a scoreboard, 5 and
//    6 the cancel scrolls, 7 and 8 the Creature scrolls, 9 the brotherhood.
//  - It places the mesh with LH3DObject::SetPosition (0x00709ED2): the point
//    of the scroll on the land, an angle and a scale. The angle of each call
//    is the angle of the mesh (+0x48) and 0.00314 radians for each ms of the
//    frame, so each eye pass turns the scroll a step further. A silver scroll
//    that is active faces the camera of the eye pass.
//  - The scale of a scroll that is not silver is its own scale times the
//    distance from the game's camera to the scroll, held between 10 and 30
//    units, over 30 (0x00709E60 to 0x00709EA6). The camera is the point of
//    GGame::GetCamera at +0x118, +0x148 and +0x178, the game's own camera,
//    and not the camera of the eye pass. So both eyes get one size.
//  - It takes the middle and the half sizes of the box of the mesh through
//    that matrix (0x00709ED7 to 0x00709FC0), draws the mesh
//    (SingleMapFixed::Draw, which offers the scroll to the pick as each
//    object), and gives the middle and the half diagonal plus 0.5 to
//    GInterface::SendInvisibleDrawCollision (0x0070A2D7). That function
//    offers the scroll only when the pick pixel is inside the circle of that
//    ball on the screen (0x00519A6D to 0x00519A9F).
//  - A sprite goes 1.4 times the half diagonal from the middle toward the
//    camera of the eye pass, 2 times the half diagonal in size (0x0070A2DF
//    to 0x0070A447): the glow of the scroll.
// ScriptHighlight::InterfaceValidToTap 0x0070ADD0: a silver scroll taps when
// it has a script (+0x78). Each other scroll taps only while it is active
// too (+0x60, ScriptHighlight::SetActivated 0x0070A630). [V]
const uintptr_t kScrollDraw = 0x00709C60;
const int kScrollHidden = 0x5C;
const int kScrollActive = 0x60;
const int kScrollScript = 0x78;
const int kObjectInfo = 0x28;
const uintptr_t kScrollInfos = 0x00D96390;
const int kScrollInfoSize = 272;  // 0x00709CC8: times 0x78787879, down 39 bits
const int kScrollSilver = 1;
const int kGameScrollGate = 0x250090;         // GGame: +0x80, 0 hides them
const uintptr_t kGetGameCamera = 0x0054C180;  // GGame::GetCamera, thiscall
const int kGameCameraPoint = 0x118;           // x, then y and z 0x30 apart

// The game's length of the Hand at size 1 (fn_0046C040 returns 3.2). The
// game keeps the Hand half of it above the land.
const float kHandLength = 3.2f;
// How far from the Hand's point an object can be taken, as a part of the
// Hand's length at its size now.
const float kGrabShare = 0.6f;
// THE LIMITS. The reach stops at the CHand far limit (+0x4838), the upper
// clamp of SetDistanceFromView (0x0046C0E4). The logs read 1800 units there,
// and that value is used until the CHand is found. The size grows with the
// reach factor up to kMaxSizeFactor, which a real arm does not pass: a hand
// 80 cm from the anchor gives about 55. A controller 1.2 m away, for example
// on the desk, would otherwise give a Hand of size 65 with a grab radius of
// 17 meters.
const float kDefaultFarLimit = 1800.0f;
// The Hand's length at size 1, in game units, is kHandLength. So the real
// hand's length, at the world scale, divided by it is the size.
float RealHandSize() {
  float units_per_meter = HeadPoseScale();
  if (!(units_per_meter > 0.1f)) units_per_meter = 7.5f;
  return ControllersHandCm() * 0.01f * units_per_meter / kHandLength *
         ControllersHandScalePct() * 0.01f;
}
const float kMaxSizeFactor = 80.0f;
float g_far_limit = kDefaultFarLimit;

bool g_installed = false;

// Latched once a frame by HandDirectFrame.
bool g_active = false;
uint8_t* g_hand = nullptr;
uint8_t* g_face = nullptr;
float g_point[3] = {0.0f, 0.0f, 0.0f};   // the reach point
float g_target[3] = {0.0f, 0.0f, 0.0f};  // the point, kept above the land
// The tip of the index finger, kept above the land: the point that the mod's
// hand stands on (HandDirectExactFacts). It is the target, but not while the
// point of the flat hand is the palm. Then the target is the palm, and the
// hand stays at the tip.
float g_tip[3] = {0.0f, 0.0f, 0.0f};
float g_factor = 1.0f;
float g_real_distance = 0.0f;
float g_scale = 0.5f;
bool g_land_done = false;
bool g_on_land = false;
int g_eye = -1;
uint64_t g_frame = 0;

// The candidates of the first eye pass. The draw offers them near to far
// from the eye. On the table it draws the whole island, about 2000 objects,
// so the list holds more there. The first 256 would all be on the near side
// of the table, and the Hand could not pick on the far side.
struct Candidate {
  void* object;
  float position[3];
  const uint8_t* model;  // the LH3DObject of the draw, for this frame only
};
const int kMaxCandidates = 4096;
const int kLifeCandidates = 256;
Candidate g_candidates[kMaxCandidates];
int g_candidate_count = 0;
int g_candidate_limit = kLifeCandidates;
// An object the draw offers twice goes in once. A table of the objects of
// this frame, open addressing, with the round of the frame in each slot, so
// it needs no clear.
const uint32_t kSeenSlots = 8192;
void* g_seen[kSeenSlots];
uint32_t g_seen_round[kSeenSlots];
uint32_t g_round = 1;
int g_most_candidates = 0;

// THE SCROLLS OF THE FRAME (h_ScrollDraw): each scroll that the game drew,
// with the ball that its draw made in each eye pass. The first eye pass gives
// the body of the choice.
struct Scroll {
  void* object;
  const uint8_t* model;
  float point[3];    // where the draw put the mesh
  float center[3];   // the middle of its box in the world, first eye pass
  float radius;      // the half diagonal of that box
  float other[3];    // the same of the second eye pass
  float other_radius;
  bool second;       // the second eye pass drew it
  int list;          // how it came to the list of the candidates, or not
  int type;          // 1 is silver
  bool active;
  int32_t script;
  float camera;      // units from the game's camera to the scroll
};
enum {
  kScrollFromPick = 0,   // the game's pick offered it
  kScrollFromDraw = 1,   // the pick did not, and its draw gave it to the list
  kScrollNotListed = 2,  // the pick did not, and scroll_draw is 0
  kScrollLocked = 3      // the pick is locked, so the list takes no object
};
const int kMaxScrolls = 32;
Scroll g_scrolls[kMaxScrolls];
int g_scroll_count = 0;
// scroll_draw: each scroll that the game draws is a candidate of the choice.
// scroll_reach_cm: the reach of the pick for a scroll.
bool g_scroll_draw = true;
int g_scroll_reach_cm = near_math::kScrollReachCm;
// scroll_click: a scroll that is not active yet is a target of the grip, and
// the press is a click for the scripts of the game (ScrollClick). The choice
// is such a scroll now, the press that clicked it is still down, and the
// clicks of this window.
bool g_scroll_click = true;
bool g_choice_click = false;
bool g_click_press = false;
uint64_t g_scroll_clicks = 0;
// The report: the most scrolls of one frame, the scrolls that only the draw
// gave to the list, and the largest difference between the two eye passes.
int g_scroll_most = 0;
uint64_t g_scroll_added = 0;
uint64_t g_scroll_frames = 0;
float g_scroll_eye_middle = 0.0f;
float g_scroll_eye_radius = 0.0f;
// The scroll nearest to the Hand's point in the last choice, tapped or not,
// and what the log said about it last.
int g_scroll_near = -1;
float g_scroll_near_gap = 0.0f;
bool g_scroll_near_taps = false;
void* g_scroll_said = nullptr;
int g_scroll_said_state = -1;
int g_scroll_logs = 0;
const int kMaxScrollLogs = 120;
// The nearest object that a press acts on, of each object that the draw
// offered, for the line of a press with no target.
void* g_any_object = nullptr;
float g_any_gap = 0.0f;
int g_any_kind = 0;
int g_any_count = 0;
Scroll g_scroll_near_copy = {};
char g_scroll_near_words[200] = "";

// The pick on the table: the objects in view within this distance of the
// Hand are offered to the pick too, in the first eye pass. Latched once a
// frame by HandDirectFrame.
bool g_table_hooked = false;
bool g_near_pick = false;
float g_near_bound_sq = 0.0f;
int g_wide_frames = 0;
uint64_t g_near_offers = 0;
uint64_t g_kept_offers = 0;
// The table owned the last frame and its size went to the Hand.
bool g_table_hand = false;
uint64_t g_land_writes = 0;

// THE GRAB FROM A DISTANCE (docs/grab.md, "The grab from a distance"). The aim
// ray, latched once a frame by HandDirectFrame, and what the selection of the
// first eye pass needs.
const int kMustBeInInfluence = 0x714;  // a vtable slot of Object: 1 when the
                                       // influence is needed to interact
const uintptr_t kMultiplayerFlag = 0x00D46ABA;  // the byte of -MULTIPLAYER
const int kGameMode = 0x25017C;                 // GGame: 3 is multiplayer
const int32_t kModeMultiplayer = 3;
bool g_far_offer = false;   // the cone offers its objects to the pick
bool g_far_ready = false;   // the selection runs on this frame
bool g_far_ray = false;
float g_far_origin[3] = {0.0f, 0.0f, 0.0f};
float g_far_direction[3] = {0.0f, 0.0f, 1.0f};
float g_far_units = 0.0f;   // world units in a meter of the room
float g_far_eye[3] = {0.0f, 0.0f, 0.0f};  // the eye in the world
uint64_t g_far_hidden = 0;  // candidates the land hides, this window
// The test word "aimfar": the object it aimed at, so the log says on the next
// frames why that object is, or is not, the target.
void* g_far_debug_object = nullptr;
int g_far_debug_frames = 0;
float g_far_offer_deg = 12.0f;
uint64_t g_far_offers = 0;
uint64_t g_far_land_writes = 0;
bool g_said_multiplayer = false;
bool FarTargetLives();
// The wide pick of the word "aimfar": the objects in this ball of the room
// around the Hand are offered to the pick for a few frames.
float g_wide_meters = 0.0f;
const int kMaxFarScores = 4096;
controller_math::FarScore g_far_scores[kMaxFarScores];
int g_far_which[kMaxFarScores];
float g_far_radius[kMaxFarScores];
bool g_far_bulk[kMaxFarScores];   // the candidate is a bulk source
bool g_far_touches[kMaxFarScores];  // a scroll: the grip activates it
bool g_far_forests[kMaxFarScores];  // a forest: the fetch gives a new tree
int g_far_leashes[kMaxFarScores];   // the Creature: near_math::LeashKind
bool g_far_leash = true;            // far_leash: the Creature is a far target
uint64_t g_far_leash_clicks = 0;    // leash clicks from a distance, this window
int LeashKind(void* creature);
// THE BODY OF THE CREATURE BEFORE A PRESS (BodyPlace). creature_bone: the
// body bone counts at each time. The object and the answer of the last line
// in the log, the lines so far, and the answers of this window (none, the
// bone, the place on the map).
bool g_creature_bone = true;
void* g_bone_object = nullptr;
int g_bone_source = -1;
int g_bone_logs = 0;
uint64_t g_bone_counts[3] = {0, 0, 0};
// HAPTICS (haptics.cpp). The target of the last tick, the time of that tick,
// the frame of the last tick of a release, and what is left of the path of a
// stroke.
void* g_haptic_choice = nullptr;
ULONGLONG g_haptic_target_tick = 0;
uint64_t g_haptic_release_frame = 0;
float g_stroke_path_cm = 0.0f;
// far_grab_choice (far_choice_math.h). The class of each candidate, the
// score with no bias, and the text of the second candidate for the log. For
// the ranks: whether the candidate is of the first class (a living thing, a
// scroll), and its angle for the order in the first rank.
bool g_far_living[kMaxFarScores];
controller_math::FarScore g_far_raw[kMaxFarScores];
bool g_far_first[kMaxFarScores];
float g_far_near[kMaxFarScores];
// THE CLASSES OF THE RANKS (far_choice_math.h, ClassOf and PickClassed): the
// class of each candidate, whether it is a signpost or a miracle, and the
// room that PickClassed works in.
int g_far_class[kMaxFarScores];
bool g_far_signs[kMaxFarScores];
bool g_far_miracles[kMaxFarScores];
controller_math::FarScore g_far_work[kMaxFarScores];
// far_grab_sign: 1 puts a signpost in the last rank. far_grab_miracle: 1 puts
// a miracle that the Hand can take in the first rank. far_grab_creature: 1
// measures your Creature by its body and takes it from the game.
bool g_far_sign_last = true;
bool g_far_miracle_first = true;
bool g_far_creature_body = true;
// YOUR CREATURE AS A FAR CANDIDATE (far_grab_creature=1). The candidate that
// stands for it when the draw did not offer it, the te that the point of its
// mesh gives (less than 0 when the draw did not offer it), and the lines of
// the log so far.
Candidate g_far_own = {};
float g_far_own_point_te = -1.0f;
float g_far_own_point[3] = {0.0f, 0.0f, 0.0f};
int g_far_own_logs = 0;
const int kMaxFarOwnLogs = 60;
char g_far_runner[160];
const char* g_far_why = nullptr;
bool g_choice_key_down = false;
bool g_stay_key_down = false;
bool g_take_carry_key_down = false;
// The pulses that tell the value of far_grab_choice after its test key: how
// many are left, and the tick of the next one.
int g_choice_pulses = 0;
ULONGLONG g_choice_pulse_tick = 0;
// The test key far_test_aim_blends: a list of percents. Each word "aimfar"
// takes the next one in place of far_test_aim_blend_pct.
int g_far_blends[24];
int g_far_blend_count = -1;
int g_far_blend_next = 0;
// The target whose hold has its log line (far_grab_stay=1), and the lines so
// far.
void* g_far_hold_logged = nullptr;
int g_far_hold_logs = 0;
const int kMaxFarHoldLogs = 60;
float g_far_toward[3];  // the base of the tree of far_test_aim_toward
uint64_t g_far_out_of_range = 0;  // candidates past far_grab_max_m, this window
char g_far_name[48];
// A TARGET THAT MOVES OR DIES (far_grab.cpp). The mesh of the locked target:
// the screen test offers it to the pick on each frame, in the cone or not, so
// the lock follows an object that drifts or rolls. The object that the Hand
// threw last, for the test word "aimfar" with far_test_thrown=1.
const uint8_t* g_far_keep_model = nullptr;
uint64_t g_far_keep_offers = 0;
void* g_last_thrown = nullptr;
void* g_last_thrown_vtable = nullptr;
double g_last_thrown_time = 0.0;  // NowSeconds() at the release
// The object left a hand that was still: it fell by the height rule, or the
// game set it down. It is no throw (near_math::GuardHolds).
bool g_last_thrown_fell = false;
// The grip was fully open at some time after the opening that let the last
// object go (HandDirectGripFullyOpen). With catch_moving=3 the guard of a
// throw ends there.
bool g_grip_reopened = true;

bool HandEmpty();
void Inject();
void AssertRecord();
bool ChoiceLives();
bool KeepLiveChoice(const char* when);
int PlanCount();
float RoomUnits();

// True when the object is new this frame, and then it is in the table.
bool FirstOffer(void* object) {
  uint32_t slot = static_cast<uint32_t>(
                      (reinterpret_cast<uintptr_t>(object) >> 3) *
                      2654435761u) & (kSeenSlots - 1);
  for (uint32_t probe = 0; probe < kSeenSlots; ++probe) {
    if (g_seen_round[slot] != g_round) {
      g_seen_round[slot] = g_round;
      g_seen[slot] = object;
      return true;
    }
    if (g_seen[slot] == object) return false;
    slot = (slot + 1) & (kSeenSlots - 1);
  }
  return false;
}
void* g_choice = nullptr;
float g_choice_position[3] = {0.0f, 0.0f, 0.0f};
float g_choice_gap = 0.0f;
bool g_choice_take = false;  // the choice is a bulk source, not holdable
bool g_choice_far = false;   // the choice is the object of a far grab
bool g_choice_tap = false;   // the choice is an object that a press taps
void* g_logged_choice = nullptr;
// ONE TARGET (near_rule=2). The kind of the choice, whether a press acts on
// it now, and its body for the halo: the middle, the radius and half the
// height. Choose makes them after the first eye pass.
int g_choice_kind = 0;       // near_math::Kind
bool g_choice_acts = false;  // the influence lets the press act
float g_choice_center[3] = {0.0f, 0.0f, 0.0f};
float g_choice_radius = 0.0f;
float g_choice_half = 0.0f;
bool g_choice_touched = false;
int g_logged_acts = -1;
// The first word of the choice at the draw that offered it (ChoiceLives).
void* g_choice_table = nullptr;
// THE TARGET CHECK (target_check, docs/grab.md, "A target that the game
// deletes"). The game turn runs between two draws, and a script or the
// physics can delete the object of the choice in it. The choice of the last
// draw then points to freed memory. 1: at the start of each frame and before
// Inject puts the choice in the pick record, a choice that does not live
// (ChoiceLives) is dropped. 0: no check.
bool g_target_check = true;
uint64_t g_target_gone = 0;  // choices dropped, this window
int g_target_gone_logs = 0;
const int kMaxTargetGoneLogs = 40;
// target_check_test_frame (a test key): at the first eye pass from that
// frame on with a choice at the Hand, the choice becomes a dead object of
// the mod's own, with a first word that is not the table of a class.
uint64_t g_gone_test_frame = 0;
bool g_gone_test_done = false;

// THE PICK AT THE GLOW (docs/grab.md, "One point for each interaction").
// The keys, in centimeters of the room. The A/B key (test_keys=1, the point
// key of the number pad) changes the rule while the game runs.
//
// near_rule: 0 is the old pick (a reach of 60 percent of the Hand's length).
// 1 is the nearest body within hand_pick_cm, and for an object that is
// not near, the game's own pick at the pixel of the Hand's point. 2 is one
// target: the pick record holds the mod's choice and nothing else, a body
// that the point touches wins, a scroll in the reach wins, and the game's
// search at the palm is off.
int g_rule = 2;
int g_rule_ini = 2;
bool g_near_rule = true;    // near_rule is 1 or 2
bool g_one_target = true;   // near_rule is 2
int g_touch_cm = near_math::kTouchCm;  // hand_touch_cm
bool g_palm_search = false;  // palm_search: 1 leaves the game's search on
// hand_glow_idle: 1 draws the glow also with no target and while the Hand
// holds an object. The A/B key (test_keys=1) is the 7
// key of the number pad.
bool g_glow_idle = false;
bool g_glow_key_down = false;
// The test key near_test_flips: at each frame of the list the rule changes
// between 1 and 2, so one desktop run shows both.
uint64_t g_rule_flips[8] = {0};
int g_rule_flip_count = 0;
// The search at the palm, for the log and the report.
typedef void*(__fastcall* PalmSearchFn)(void* self, void* unused,
                                        const int32_t* land);
PalmSearchFn o_PalmSearch = nullptr;
uint64_t g_palm_asks = 0;     // the game asked, this window
uint64_t g_palm_refused = 0;  // and the mod gave no object
uint64_t g_palm_given = 0;    // the game's search gave an object to the press
int g_palm_logs = 0;
const int kMaxPalmLogs = 60;
// The pick record before each interface step.
uint64_t g_record_puts = 0;    // the game had dropped the choice: put back
uint64_t g_record_clears = 0;  // an object that is not the choice: taken out
int g_record_logs = 0;
const int kMaxRecordLogs = 40;
int g_pick_cm = near_math::kPickCm;         // hand_pick_cm
int g_near_in_cm = near_math::kNearInCm;    // far_grab_near_cm
int g_near_out_cm = near_math::kNearOutCm;  // far_grab_near_out_cm
// AN OBJECT THAT MOVES (docs/grab.md, "A target that moves, floats or
// dies"): the keys catch_moving, catch_lag_ms, catch_cap_cm, catch_grace_ms.
// g_catch_on is the rule of catch_moving: near_math::kCatchOff, kCatchPick,
// kCatchRest or kCatchOpen.
int g_catch_on = near_math::kCatchOpen;
int g_catch_lag_ms = near_math::kCatchLagMs;
int g_catch_cap_cm = near_math::kCatchCapCm;
int g_catch_grace_ms = near_math::kCatchGraceMs;
bool g_catch_key_down = false;
int g_wide_cm = near_math::kWideCm;       // hand_wide_pick_cm
bool g_wide_key_down = false;
bool g_choice_wide = false;               // the choice is a wide pick
// hand_sign_last: at the Hand a signpost that the point is not in comes last
// (near_math::SignOut). hand_wide_creature: the Creature has the wide reach.
// far_grab_beside_loose: a tree, a building or a pile that the point is not
// in does not turn the grab from a distance off (near_math::LooseOut).
// g_choice_last: the choice of this frame gives way to a target of the aim:
// such a signpost, or such a loose body (then g_choice_loose is true too).
bool g_sign_last = true;
bool g_creature_wide = true;
bool g_beside_loose = true;
bool g_choice_last = false;
bool g_choice_loose = false;
// The loose body that the log said leaves the grab from a distance on.
void* g_loose_said = nullptr;
int g_sign_way_logs = 0;
const int kMaxSignWayLogs = 40;
float g_choice_wide_cm = 0.0f;            // and the wide reach that it had
void* g_wide_logged = nullptr;
uint64_t g_catch_picks = 0;  // choices that the extra reach made, this window
void* g_catch_logged = nullptr;
void* g_wide_catch_logged = nullptr;  // the same for a wide choice
bool g_far_touch = true;                    // far_touch
bool g_near_key_down = false;
bool g_scroll_key_down = false;  // Ctrl and the point key: scroll_draw
// NEAR OBJECTS WIN. Latched by Choose once a frame: an object that a press
// acts on is near the Hand's point, so the grab from a distance is off.
bool g_near_blocked = false;
void* g_near_object = nullptr;   // the nearest such object, and its gap
float g_near_gap = 0.0f;
uint64_t g_near_block_frames = 0;
uint64_t g_near_blocks = 0;
int g_near_logs = 0;
const int kMaxNearLogs = 80;
uint64_t g_lock_keeps = 0;  // passes that left the record to a game pick up

// THE LEASH at the palm of the mod's hand. The point is made once a frame.
typedef void(__fastcall* LeashStartFn)(void* self, void* unused, float* out,
                                       int from_status);
LeashStartFn o_LeashStart = nullptr;
bool g_leash_palm = true;  // leash_at_palm
uint64_t g_leash_frame = 0;
bool g_leash_have = false;
float g_leash_point[3] = {0.0f, 0.0f, 0.0f};
uint64_t g_leash_moves = 0;
bool g_leash_said = false;
uint64_t g_leash_last = 0;  // the frame of the last call
int g_leash_miss_logs = 0;
const int kMaxLeashMissLogs = 8;
uint64_t g_leash_miss_frame = 0;
int g_leash_logs = 0;
const int kMaxLeashLogs = 20;
void* g_logged_held = nullptr;
void* g_logged_now = nullptr;

// THE MOTION HISTORY of the reach point and of the grip, in the room, one
// sample a frame while the controller places the Hand. Oldest first.
const int kMaxHistory = 64;
const double kThrowWindow = 0.080;     // seconds of history for a throw
const double kCreatureWindow = 0.050;  // and for a stroke or a slap
const double kStaleSeconds = 0.25;     // an older newest sample is no motion
// The game sets a thrown object down when its horizontal speed is this or
// less, in units per second (InitialisePhysicsFromHand, docs/hand.md, "Release
// and throw").
const float kDropSpeed = 2.0f;
controller_math::MotionSample g_reach_history[kMaxHistory];
controller_math::MotionSample g_grip_history[kMaxHistory];
int g_history_count = 0;
float g_room[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};  // L, the room basis
// Whether the controller placed the Hand on the last frame. Unlike g_active
// it stays true between the two eye passes, for the release action.
bool g_owns = false;
// Latched once a frame by PrepareThrow: what the release of this frame sends,
// and whether the game throws it (a speed across the land above kDropSpeed).
uint64_t g_throw_frame = 0;
float g_throw_sent[3] = {0.0f, 0.0f, 0.0f};
float g_throw_expected = 0.0f;  // units: s times the real range, no drag
bool g_throw_flies = false;
// The release of this frame is from a hand that was still: the real flight
// lets the object fall (the height rule), or the game sets it down.
bool g_throw_still = false;
int g_throw_logs = 0;
const int kMaxThrowLogs = 60;
uint64_t g_throws = 0;

// THE THROW OF THE OPENING (docs/grab.md, "The grip holds"). The grip lets go
// with a click that the game reads some frames later. So controllers.cpp
// latches the hand's motion at the moment the grip opens, over the
// kThrowWindow before it, and the release of the next kLatchSeconds uses it.
struct ThrowLatch {
  bool valid;
  bool moving;
  double time;
  uint64_t frame;
  float v_room[3];
  float v_grip[3];
  float real;
  float height;  // the Hand's point over the land, world units
};
ThrowLatch g_latch = {};
const double kLatchSeconds = 0.3;
uint64_t g_latched_throws = 0;

// THE HAND'S PATH over the land, one point a frame while the controller
// places the Hand, oldest first. A throw on the table or in the God view that
// ends outside the player's influence goes from the newest point of the last
// kThrowFromWindow seconds that is inside it (docs/tabletop.md, "The throw from
// inside the influence").
const double kThrowFromWindow = 0.5;
double g_path_time[kMaxHistory];
float g_path_x[kMaxHistory];
float g_path_z[kMaxHistory];
int g_path_count = 0;
// Latched once a frame: the land point that the release of this frame uses.
uint64_t g_from_frame = 0;
float g_from_x = 0.0f;
float g_from_z = 0.0f;
uint64_t g_throws_from = 0;

// THE CREATURE BY TOUCH. The speed test and the contact of this frame.
bool g_in_creature = false;     // inside HandStateCreature::Update, ours
bool g_prev_valid = false;
float g_prev_point[3] = {0.0f, 0.0f, 0.0f};
float g_creature_speed = 0.0f;  // what the game reads, units per second
float g_creature_real = 0.0f;   // the real hand, meters per second
float g_creature_k = 0.0f;      // units per second for 1 m/s
float g_creature_total = 0.0f;
int g_creature_logs = 0;
const int kMaxCreatureLogs = 80;
uint64_t g_strokes = 0;
uint64_t g_slaps = 0;
// Latched by the first Update of a frame: the body capsule, whether the
// Hand's point is in it, and whether the Hand went into it since the last
// frame. The later Updates of the frame (the second eye) use the same.
uint64_t g_touch_frame = 0;
uint64_t g_touch_last = 0;       // the last frame with a Creature Update
const void* g_touch_state = nullptr;
bool g_touch_valid = false;      // the capsule is known this frame
controller_math::Capsule g_touch_body = {};
float g_touch_gap = 0.0f;        // the Hand's point outside the body, units
bool g_touch_inside = false;
bool g_touch_entered = false;    // forces the game's slap branch
bool g_touch_fix_x = false;      // the speed ray puts the last x back
int32_t g_touch_step_x = 1;      // 1 for a hand that moves right, else -1
float g_touch_from[3] = {0.0f, 0.0f, 0.0f};  // the point on the last frame
bool g_touch_from_valid = false;
float g_touch_across[3] = {1.0f, 0.0f, 0.0f};
void* g_touch_object = nullptr;  // the Creature object of this state
float g_touch_radius = 0.0f;     // its 2D radius
int g_touch_frames = 0;          // frames in this state, for the test log
bool g_touch_said = false;
uint64_t g_touch_vetoes = 0;     // mesh hits refused: the Hand is not in it
uint64_t g_touch_forced = 0;     // slap branches forced by the swept test
uint64_t g_touch_kept = 0;       // frames the pick kept the Creature
uint64_t g_touch_waits = 0;      // states that started with no wait
int g_touch_logs = 0;
const int kMaxTouchLogs = 60;
// THE OPEN HAND ON THE CREATURE (docs/creature.md, "The open hand"). The hand
// of the stroke, the wave of the tickle, and the two speeds of the hand in the
// last Update.
int g_stroke_mode = 0;            // creature_stroke_hand: 0 by the size of the
                                  // Creature, 1 the flat hand, 2 the finger,
                                  // 3 the old stroke
// THE OLD STROKE (creature_stroke_hand=3), an A/B for the open hand: a fist
// while the grip is closed and the flat hand while it is open, the contact at
// the Hand's point, no tickle, the speed of the grip only, and a slap at 1.5
// m/s when the ini has no creature_slap_mps.
const int kStrokeBefore = 3;
const float kSlapBeforeMps = 1.5f;
bool g_slap_ini_set = false;      // the ini has the key creature_slap_mps
bool g_stroke_finger = true;
bool g_stroke_was_finger = true;
// THE LIMITS OF THE HAND OF A STROKE, as parts of the hand's length
// (near_math::StrokeLimits): creature_finger_below_pct and
// creature_flat_above_pct. Ctrl and the 3 key of the number pad change
// between the limits of the ini and the fixed limits (80 and 120).
int g_stroke_finger_pct = near_math::kStrokeFingerBelowPct;
int g_stroke_flat_pct = near_math::kStrokeFlatAbovePct;
bool g_stroke_limits_before = false;
bool g_stroke_limit_key_down = false;
// creature_open_wait: the hand opens when a press on the Creature is past the
// time of a leash click, before the game starts the Creature state. The time
// of the press that the log named last.
bool g_stroke_wait_open = true;
double g_stroke_wait_said = -1.0;
// THE PLAYER THAT A SCRIPT SEES (see h_ScriptDistance). script_distance: in
// the God view a script that measures the distance from the camera gets the
// distance from the player. script_view: a script that asks if the camera
// sees a thing gets the view of the head. Ctrl and the 7 key of the number
// pad turn the two off and on together (g_script_on).
bool g_script_hooked = false;
int g_script_distance = 1;
int g_script_view = 1;
// script_camera_moves: a script that asks how the player holds the land, or
// if the rotate arrow shows, gets the moves of the God view (h_ScriptHandState
// and h_ScriptArrows).
int g_script_moves = 1;
bool g_script_on = true;
bool g_script_key_down = false;
void ScriptPlayerInstall();
// creature_palm_point: with the flat hand of a stroke the Hand's point is
// the palm, and not the tip of the index finger (near_math::PalmPoint). The
// offset from the tip to the palm, the frame that made it, and whether the
// point of this frame is at the palm.
//
// THE PALM MUST NOT FOLLOW THE POINT. The palm comes from the mod's hand
// (HandExactPalm), and the mod's hand stands on the point that
// HandDirectExactFacts gives. If that point were the target, the palm of the
// next frame would be one offset farther, and the point would run away 11.4 cm
// a frame, with no touch and no stroke. So the mod's hand stands on the tip
// (g_tip), and the offset is from that tip.
bool g_palm_point = true;
bool g_palm_key_down = false;
float g_palm_offset[3] = {0.0f, 0.0f, 0.0f};
uint64_t g_palm_offset_frame = 0;
bool g_palm_now = false;
bool g_palm_said = false;
int g_palm_point_logs = 0;
const int kMaxPalmPointLogs = 40;
uint64_t g_palm_frames = 0;
void StrokeLimitsNow(float* finger_below, float* flat_above) {
  near_math::StrokeLimits(
      g_stroke_limits_before ? near_math::kStrokeFingerBelowPctBefore
                             : g_stroke_finger_pct,
      g_stroke_limits_before ? near_math::kStrokeFlatAbovePctBefore
                             : g_stroke_flat_pct,
      finger_below, flat_above);
}
float g_stroke_phase = 0.0f;
uint64_t g_stroke_frame = 0;      // the last frame of a Creature Update
float g_creature_grip = 0.0f;     // the speed of the grip, in m/s
float g_creature_tip = 0.0f;      // and of the Hand's point
float g_creature_way[3] = {0.0f, 0.0f, 0.0f};  // the motion, in the room
// THE SPEED OF A SLAP: creature_slap_mps, or the value of the test key (the
// 9 key of the number pad, test_keys=1).
const float kSlapKeyMps[4] = {0.7f, 1.0f, 1.3f, 1.6f};
int g_slap_key = -1;
bool g_slap_key_down = false;
bool g_stroke_key_down = false;
int g_miss_logs = 0;
const int kMaxMissLogs = 60;
float SlapMps() {
  if (g_slap_key >= 0) return kSlapKeyMps[g_slap_key];
  if (g_stroke_mode == kStrokeBefore && !g_slap_ini_set) return kSlapBeforeMps;
  return ControllersCreatureSlapMps();
}

// NO CAMERA. The mod holds the Creature state with no camera mode from the
// press to the release, even when the owner of the pointer changes.
int g_creature_camera = 2;       // creature_camera
// A FIGHT OF CREATURES (fight_grip, docs/creature.md, "A fight of Creatures").
bool g_fight_grip = true;
bool g_fight_seen = false;        // the far aim saw a fight at its last frame
uint64_t g_fight_rays = 0;        // steps of a press of a fight, this window
uint64_t g_fight_hits = 0;        // and the ray from the Hand's point hit
uint64_t g_fight_game_hits = 0;   // and the game's ray hit
uint64_t g_fight_ray_frame = 0;   // the frame of the last such step
int g_fight_ray_hit = -1;         // and its result, for the log
int g_fight_logs = 0;
const int kMaxFightLogs = 120;
bool g_virtual = false;
const void* g_virtual_hand = nullptr;
void* g_virtual_creature = nullptr;
double g_virtual_since = 0.0;
uint64_t g_virtual_count = 0;
uint64_t g_leash_clicks = 0;
// The leash click: a press shorter than this, in real time. The game's own
// test is 450 ms of its frame time, which grows in both eye passes.
const double kLeashSeconds = 0.450;

// For the report and the test log.
uint64_t g_active_frames = 0;
uint64_t g_depth_writes = 0;
uint64_t g_pose_writes = 0;
uint64_t g_rays = 0;
uint64_t g_picks = 0;
uint64_t g_cleared = 0;
bool g_said_active = false;
bool g_said_sizes = false;
int g_check_logs = 0;
const int kMaxCheckLogs = 60;
int g_pick_logs = 0;
const int kMaxPickLogs = 400;
float g_logged_target[3] = {1e30f, 1e30f, 1e30f};
int g_settle = -1;
int g_state_logs = 0;
const int kMaxStateLogs = 300;
int32_t g_logged_state = -1;
int32_t g_logged_mode = -1;
int g_logged_locked = -1;
uint64_t g_offers = 0;
const int kSettleFrames = 20;

// THE GLOW (hand_glow.cpp). What it draws, latched at the first ask of a
// frame, so both eye passes draw the same point with the same look.
uint64_t g_glow_frame = 0;
bool g_glow_valid = false;
float g_glow_point[3] = {0.0f, 0.0f, 0.0f};
float g_glow_length = 0.0f;
int32_t g_glow_mode = 0;
bool g_glow_holding = false;
int g_glow_kind = 0;  // near_math::Kind: what a press of the grip does there
// THE HALO OF THE TARGET AT THE HAND (one target), latched with the glow.
bool g_near_target = false;
float g_near_target_center[3] = {0.0f, 0.0f, 0.0f};
float g_near_target_radius = 0.0f;
float g_near_target_half = 0.0f;
bool g_near_target_acts = false;
void* g_near_target_object = nullptr;  // for the glow on its mesh

// THE BULK TAKE. The object of the take now, for the log and the report.
void* g_take_object = nullptr;
uint32_t g_take_amount = 0;
int32_t g_take_turns = 0;
int g_take_logs = 0;
const int kMaxTakeLogs = 400;
// The amount goes to the log on each of the first turns, then on each tenth
// turn. A log line for each turn of a long take would fill the log.
const int32_t kTakeEachTurn = 5;
uint64_t g_takes = 0;
uint64_t g_take_frames = 0;
uint64_t g_take_grip_frames = 0;  // frames the grip was the button for it
// The source of the take goes back in the next pick record before each
// interface step, so the game's own release step ends the take.
uint64_t g_take_keeps = 0;        // the record already held the source
uint64_t g_take_puts = 0;         // the mod put the source back
// What the record held then, for the log. kNoOther before the first put of a
// take, because the record can hold nothing (null) too.
void* const kNoOther = reinterpret_cast<void*>(static_cast<uintptr_t>(1));
void* g_take_other = kNoOther;
// The record, the pick lock and the interface action of the last frame of a
// take, for the log of each change.
void* g_take_seen_next = kNoOther;
int g_take_seen_lock = -1;
int32_t g_take_seen_action = -1;
int g_take_state_logs = 0;
const int kMaxTakeStateLogs = 200;
bool g_take_no_keep = false;      // test key bulk_test_no_keep
// The grip asked for the end of a take that still runs with the button up.
int g_take_end_asks = 0;
int g_take_end_stage = 0;         // 1 after the game's step, 2 after the handler
double g_take_end_time = 0.0;     // NowSeconds() at the last try
// The time between two tries of the end of a take, in seconds of real time:
// two game turns.
const double kTakeEndWait = 0.2;
uint64_t g_take_ends_forced = 0;
uint64_t g_take_ends_direct = 0;
// TEST (far_test_take_gone=N, for the desktop runs only): the game turn of a
// take at a distance at which the test ends the take with the grip closed, or
// 0. See FarTestTakeGone.
int g_far_test_take_gone = 0;
bool g_far_test_take_asked = false;  // the test asked for the end of this take
bool g_take_end_test = false;        // the ask of this frame is the test's
// A GAME TURN NOW. The frames left in which the next ask of the game for a
// turn gets a yes.
int g_turn_now = 0;
bool g_turn_on = true;            // release_at_once
uint64_t g_turns_asked = 0;
uint64_t g_turns_given = 0;
uint64_t g_release_frame = 0;     // the frame of the last release action
void* g_release_object = nullptr; // what the Hand held then
int g_release_logs = 0;
const int kMaxReleaseLogs = 60;
uint64_t g_release_count = 0;      // releases that left the Hand, this window
uint64_t g_release_frames_sum = 0;
uint64_t g_release_worst = 0;
bool g_turn_hooked = false;
// THE A/B KEY of the release at once (test_keys=1): the star key of the
// number pad.
bool g_release_key = false;       // test_keys
bool g_release_key_down = false;

float Distance(const float a[3], const float b[3]) {
  const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
  return sqrtf(dx * dx + dy * dy + dz * dz);
}

const float* Camera() {
  return reinterpret_cast<const float*>(bw::tech::kCameraPos);
}

int32_t State(const uint8_t* hand) {
  return *reinterpret_cast<const int32_t*>(hand + kHandState);
}

// The states that the controller places the Hand in: Normal, Holding, and
// with miracle_hand=1 the Grain state of a miracle in the Hand. The Grain
// state runs the update of the Holding state (0x005B3361), so the same hooks
// place it (docs/miracles.md, "A miracle in the Hand in the plain game").
bool DirectState(const uint8_t* hand) {
  const int32_t state = State(hand);
  return state == kStateNormal || state == kStateHolding ||
         (state == kStateGrain && MiraclePlacesHand());
}

// The states whose ray the mod aims. The Creature state casts the Hand's ray
// at the Creature, and it reads the speed of the Hand from the screen point.
bool RayState(const uint8_t* hand) {
  return DirectState(hand) || State(hand) == kStateCreature;
}

// The hook acts on this Hand now.
bool Acting(const void* self) {
  return g_active && self != nullptr && self == g_hand &&
         DirectState(g_hand);
}

// The controller places this Hand, and the Hand is in the Creature state.
bool CreatureActing(const void* self) {
  return g_active && self != nullptr && self == g_hand &&
         State(g_hand) == kStateCreature;
}

// The Creature's size, from the LH3DCreature of the Creature state. 0 when
// there is none.
float CreatureSize(const uint8_t* hand) {
  const uint8_t* creature =
      *reinterpret_cast<uint8_t* const*>(hand + kHandCreature);
  if (creature == nullptr) return 0.0f;
  const float size = *reinterpret_cast<const float*>(creature + kCreatureSize);
  return (size > 0.0f && size < 1000.0f) ? size : 0.0f;
}

void** VTable(void* object) { return *static_cast<void***>(object); }

uint8_t* Interface() {
  void* const* game = reinterpret_cast<void* const*>(kGame);
  if (IsBadReadPtr(game, sizeof(void*)) || *game == nullptr) return nullptr;
  const MyInterfaceFn my_interface =
      reinterpret_cast<MyInterfaceFn>(kMyInterface);
  return static_cast<uint8_t*>(my_interface(*game, nullptr));
}

// The object that the game's bulk take takes from now, or null. The object
// must say that it is a bulk source (IsLockedInInteract), as the Holding
// state asks at 0x005B3EB3.
void* TakeObject(const uint8_t* face) {
  if (face == nullptr) return nullptr;
  const uint8_t* status =
      *reinterpret_cast<uint8_t* const*>(face + kFaceStatus);
  if (status == nullptr) return nullptr;
  void* object = *reinterpret_cast<void* const*>(status + kStatusTakeObject);
  if (object == nullptr) return nullptr;
  return reinterpret_cast<VirtualIntFn>(VTable(object)[kIsLockedInInteract / 4])(
             object, nullptr) != 0
             ? object
             : nullptr;
}

// The class of a game object, from its RTTI, for the log: "PileFood" from
// ".?AVPileFood@@". The locator is at vtable[-1], its type at +0x0C, and the
// name at +0x08 of the type.
const char* ClassName(void* object) {
  static char name[48];
  strcpy_s(name, "unknown");
  if (object == nullptr || IsBadReadPtr(object, 4)) return name;
  void** table = VTable(object);
  if (IsBadReadPtr(table - 1, 4)) return name;
  const uint8_t* locator = static_cast<const uint8_t*>(table[-1]);
  if (locator == nullptr || IsBadReadPtr(locator, 0x10)) return name;
  const uint8_t* type = *reinterpret_cast<const uint8_t* const*>(locator + 0x0C);
  if (type == nullptr || IsBadReadPtr(type, 0x30)) return name;
  const char* raw = reinterpret_cast<const char*>(type + 0x08);
  if (strncmp(raw, ".?AV", 4) != 0) return name;
  size_t n = 0;
  for (const char* c = raw + 4; *c != 0 && *c != '@' && n + 1 < sizeof(name);
       ++c) {
    name[n++] = *c;
  }
  name[n] = 0;
  return name;
}

// A test key of bwvr.ini, read once from the game folder. These keys are for
// the desktop runs and are not in the launcher.
int IniKey(const char* key, int fallback) {
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return fallback;
  char* slash = strrchr(path, '\\');
  if (slash == nullptr) return fallback;
  slash[1] = 0;
  if (strlen(path) + 9 >= MAX_PATH) return fallback;
  strcat_s(path, "bwvr.ini");
  return static_cast<int>(GetPrivateProfileIntA("bwvr", key, fallback, path));
}

int TestKey(const char* key) { return IniKey(key, 0); }

// seek_bulk=1: the word "seek" goes to the nearest bulk source, not to the
// nearest holdable object.
bool SeekBulk() {
  static int value = -1;
  if (value < 0) {
    value = TestKey("seek_bulk") != 0 ? 1 : 0;
    if (value != 0) {
      ModLog("hand bulk    : test key seek_bulk=1, so the word \"seek\" goes "
             "to the nearest bulk source");
    }
  }
  return value != 0;
}

// seek_forest=1 (the answer 1): the word "seek" goes to the nearest forest.
// seek_tap=1 (the answer 2): it goes to the nearest object that a press taps
// and that the Hand cannot hold. seek_scroll=1 (the answer 3): it goes to the
// nearest scroll that a press taps. 0 with no such key.
int SeekOther() {
  static int value = -1;
  if (value < 0) {
    value = TestKey("seek_forest") != 0   ? 1
            : TestKey("seek_tap") != 0    ? 2
            : TestKey("seek_scroll") != 0 ? 3
                                          : 0;
    if (value != 0) {
      ModLog("hand pick    : test key %s, so the word \"seek\" goes to the "
             "nearest %s",
             value == 1 ? "seek_forest=1"
             : value == 2 ? "seek_tap=1" : "seek_scroll=1",
             value == 1 ? "forest"
             : value == 2 ? "object that a press taps" : "scroll");
    }
  }
  return value;
}

// far_test_forest=1 (the answer 1): only a forest is a far target, and the
// word "aimfar" aims at one. far_test_scroll=1 (the answer 2): the same for a
// scroll. far_test_creature=1 (3): the same for the player's Creature.
// far_test_tree=1 (4): the same for a tree. 0 with no such key.
int FarTestOnly() {
  static int value = -1;
  if (value < 0) {
    value = TestKey("far_test_forest") != 0   ? 1
            : TestKey("far_test_scroll") != 0 ? 2
            : TestKey("far_test_creature") != 0 ? 3
            : TestKey("far_test_tree") != 0 ? 4
                                            : 0;
  }
  return value;
}

// The game's tests of a press that starts a bulk take (FUN_005D1330 at
// 0x005D13BB): ValidForLockedSelectProcess is 1, the object can be picked
// up, and no take holds it now (bit 0x10 of Object+0x24).
bool TakeValid(void* object, void* status) {
  if (object == nullptr || status == nullptr) return false;
  const uint8_t* bytes = static_cast<const uint8_t*>(object);
  if ((bytes[kObjectFlags] & 0x10) != 0) return false;
  void** table = VTable(object);
  if (reinterpret_cast<VirtualIntArgFn>(table[kValidForTake / 4])(
          object, nullptr, status) != 1) {
    return false;
  }
  return reinterpret_cast<VirtualIntFn>(table[kIsCannotBePickedUp / 4])(
             object, nullptr) == 0;
}

// Once a frame: each start and end of a bulk take, and the amount in the
// Hand while it grows. With the mouse too, as a reference.
void LogTake(const uint8_t* face, const uint8_t* hand) {
  void* object = TakeObject(face);
  const uint8_t* status =
      face != nullptr ? *reinterpret_cast<uint8_t* const*>(face + kFaceStatus)
                      : nullptr;
  void* held = hand != nullptr ? *reinterpret_cast<void* const*>(hand + kHandHeld)
                               : nullptr;
  const uint32_t amount =
      held != nullptr ? *reinterpret_cast<const uint32_t*>(
                            static_cast<const uint8_t*>(held) + kPotAmount)
                      : 0;
  const int32_t turns =
      status != nullptr
          ? *reinterpret_cast<const int32_t*>(status + kStatusTakeTurns)
          : 0;
  if (object != g_take_object) {
    if (object != nullptr) {
      ++g_takes;
      if (g_take_logs < kMaxTakeLogs) {
        ++g_take_logs;
        char name[48];
        strcpy_s(name, ClassName(object));
        ModLog("hand bulk    : the game's bulk take starts on a %s, and the "
               "Hand holds a %s of %u, frame %llu", name, ClassName(held),
               amount, g_frame);
      }
    } else if (g_take_logs < kMaxTakeLogs) {
      ++g_take_logs;
      ModLog("hand bulk    : the bulk take ends after %d game turns. The Hand "
             "holds %s of %u, frame %llu", g_take_turns,
             held != nullptr ? ClassName(held) : "nothing", amount, g_frame);
    }
    g_take_object = object;
    g_take_amount = amount;
    g_take_turns = turns;
    g_take_end_stage = 0;
    g_take_other = kNoOther;
    g_take_seen_next = kNoOther;
    g_take_seen_action = -1;
    g_take_seen_lock = -1;
    return;
  }
  if (object == nullptr) return;
  ++g_take_frames;
  // The states of the interface during a take, on each change: the object of
  // the next pick record (the game's release step reads it), the pick lock
  // and the interface action. A log of a take that does not end shows here
  // what the record held when the button went up.
  {
    void* next = *reinterpret_cast<void* const*>(face + kFaceNextObject);
    const int lock = face[kFaceLocked];
    const int32_t action = *reinterpret_cast<const int32_t*>(face + kFaceAction);
    if ((next != g_take_seen_next || lock != g_take_seen_lock ||
         action != g_take_seen_action) &&
        g_take_state_logs < kMaxTakeStateLogs) {
      ++g_take_state_logs;
      char name[48];
      strcpy_s(name, next != nullptr ? ClassName(next) : "nothing");
      ModLog("hand bulk    : during the take the next pick record holds %p "
             "(%s, %s), the pick is %s and the interface action is %d, frame "
             "%llu", next, name,
             next == object ? "the source" : "not the source",
             lock != 0 ? "locked" : "open", action, g_frame);
    }
    g_take_seen_next = next;
    g_take_seen_lock = lock;
    g_take_seen_action = action;
  }
  if (amount != g_take_amount && g_take_logs < kMaxTakeLogs &&
      (turns <= kTakeEachTurn || turns % 10 == 0)) {
    ++g_take_logs;
    ModLog("hand bulk    : the amount in the Hand is %u after %d game turns, "
           "frame %llu", amount, turns, g_frame);
  }
  g_take_amount = amount;
  g_take_turns = turns;
}

// THE POINT TOUCHES THE LAND. A controller pushed into the ground stops on
// it: the target goes straight up to kLandClearanceM of the room over the
// land, at the scale now. The game keeps its own Hand half its length over
// the land. That would hold the point 9.5 cm of the room over the land at
// every scale (5 cm on the table), so in the God view the point could not
// reach the land or a villager on it. Once a frame,
// from inside a Hand state, where the game itself asks the land for its
// height. See docs/hand.md, "The touch of the land".
const float kLandClearanceM = 0.005f;
float g_land_y = 0.0f;  // the land under the point, for the test log
void ResolveLand() {
  if (g_land_done) return;
  g_land_done = true;
  float x = g_point[0], z = g_point[2];
  if (!(x > -1000.0f && x < 6000.0f && z > -1000.0f && z < 6000.0f)) return;
  const int32_t coords[3] = {static_cast<int32_t>(x * kMapScale),
                             static_cast<int32_t>(z * kMapScale), 0};
  const GetAltitudeFn altitude = reinterpret_cast<GetAltitudeFn>(kGetAltitude);
  const float land = altitude(coords);
  if (!(land == land)) return;
  g_land_y = land;
  const float table = TabletopViewScale();
  const float lowest = controller_math::HandLandFloor(
      land, table > 0.0f ? table : HeadPoseScale(), kLandClearanceM);
  g_on_land = g_point[1] < lowest;
  if (!g_on_land) return;
  // The land lifts the hand, so the palm goes up as far as the tip does.
  if (g_palm_now) {
    g_target[1] += lowest - g_point[1];
  } else {
    g_target[1] = lowest;
  }
  g_tip[1] = lowest;
}

// THE HAND STAYS AT THE CONTROLLER IN THE CREATURE STATE TOO. The state
// calls SetDistanceFromView with the Creature's distance (0x005B196C and
// 0x005B2044), and then multiplies the size by the Creature's size^0.5
// (0x005B1975 and 0x005B204D). So the hook writes the size divided by that
// root, and the Hand keeps the size of the real hand. With the depth of the
// target, the state's own ScreenPointToWorldAtDistance (0x005B23C4) puts
// the Hand at the target.
void __fastcall h_SetDistanceFromView(void* self, void* unused,
                                      float distance) {
  o_SetDistanceFromView(self, unused, distance);
  const bool creature = CreatureActing(self);
  if (!Acting(self) && !creature) return;
  ResolveLand();
  uint8_t* hand = static_cast<uint8_t*>(self);
  float depth = Distance(g_target, Camera());
  if (depth > g_far_limit) depth = g_far_limit;
  *reinterpret_cast<float*>(hand + kHandDistance) = depth;
  float scale = g_scale;
  if (creature) {
    const float size = CreatureSize(hand);
    if (size > 0.0f) scale = g_scale / sqrtf(size);
  }
  *reinterpret_cast<float*>(hand + kHandScale) = scale;
  ++g_depth_writes;
}

void __fastcall h_PoseBuilder(void* self, void* unused, float* out,
                              float* position, const float* up) {
  if (Acting(self) && position != nullptr) {
    ResolveLand();
    uint8_t* hand = static_cast<uint8_t*>(self);
    float* at = reinterpret_cast<float*>(hand + kHandPosition);
    for (int i = 0; i < 3; ++i) {
      position[i] = g_target[i];
      at[i] = g_target[i];
    }
    // The lean. The game tilts the Hand by how far a spring on the screen
    // point lags the point, which is the mouse's motion. A tracked Hand has
    // no such lag, so the spring sits on the point with no speed.
    float* spring = reinterpret_cast<float*>(hand + kHandSpring);
    const int32_t* screen = reinterpret_cast<const int32_t*>(hand + kHandScreen);
    spring[0] = static_cast<float>(screen[0]);
    spring[1] = static_cast<float>(screen[1]);
    spring[2] = 0.0f;
    spring[3] = 0.0f;
    ++g_pose_writes;
  }
  o_PoseBuilder(self, unused, out, position, up);
}

double NowSeconds() {
  static LARGE_INTEGER frequency = {};
  if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
  LARGE_INTEGER count;
  QueryPerformanceCounter(&count);
  return static_cast<double>(count.QuadPart) /
         static_cast<double>(frequency.QuadPart);
}

// One sample a frame, from HandDirectFrame.
void PushHistory(const float reach_room[3], const float grip_room[3]) {
  const double now = NowSeconds();
  if (g_history_count > 0 &&
      now - g_reach_history[g_history_count - 1].time > kStaleSeconds) {
    g_history_count = 0;  // a gap: the old motion has nothing to do with now
  }
  if (g_history_count == kMaxHistory) {
    memmove(g_reach_history, g_reach_history + 1,
            sizeof(g_reach_history[0]) * (kMaxHistory - 1));
    memmove(g_grip_history, g_grip_history + 1,
            sizeof(g_grip_history[0]) * (kMaxHistory - 1));
    --g_history_count;
  }
  controller_math::MotionSample& r = g_reach_history[g_history_count];
  controller_math::MotionSample& g = g_grip_history[g_history_count];
  r.time = g.time = now;
  for (int i = 0; i < 3; ++i) {
    r.point[i] = reach_room[i];
    g.point[i] = grip_room[i];
  }
  ++g_history_count;
}

// One point of the Hand's path over the land a frame, from HandDirectFrame.
void PushPath(float x, float z) {
  const double now = NowSeconds();
  if (g_path_count > 0 &&
      now - g_path_time[g_path_count - 1] > kStaleSeconds) {
    g_path_count = 0;
  }
  if (g_path_count == kMaxHistory) {
    memmove(g_path_time, g_path_time + 1,
            sizeof(g_path_time[0]) * (kMaxHistory - 1));
    memmove(g_path_x, g_path_x + 1, sizeof(g_path_x[0]) * (kMaxHistory - 1));
    memmove(g_path_z, g_path_z + 1, sizeof(g_path_z[0]) * (kMaxHistory - 1));
    --g_path_count;
  }
  g_path_time[g_path_count] = now;
  g_path_x[g_path_count] = x;
  g_path_z[g_path_count] = z;
  ++g_path_count;
}

// The mean velocity of the reach point and of the grip over a window, in the
// room's axes (units per second), and the real speed of the grip in meters
// per second. False without motion: too few samples, or the newest one is old.
bool HandVelocity(double window, float v_room[3], float v_grip[3],
                  float* real_mps) {
  *real_mps = 0.0f;
  if (g_history_count < 2 ||
      NowSeconds() - g_reach_history[g_history_count - 1].time >
          kStaleSeconds) {
    return false;
  }
  if (!controller_math::MeanVelocity(g_reach_history, g_history_count, window,
                                     v_room)) {
    return false;
  }
  if (!controller_math::MeanVelocity(g_grip_history, g_history_count, window,
                                     v_grip)) {
    return false;
  }
  const float scale = HeadPoseScale();
  if (scale > 0.0f) *real_mps = controller_math::Length(v_grip) / scale;
  return true;
}

// THE SPEED OF THE HAND'S POINT (docs/creature.md, "The open hand"). The
// history above has the grip and the reach point, and on the table and in the
// God view the reach point is the grip. The Hand's point is the index tip, 17
// cm from the wrist, and a turn of the wrist moves it faster than the grip. So
// the Hand's point has its own history, in the world, one sample a frame while
// the controller places the Hand. The speed is in meters of the room a second,
// at the scale now. A move of the view (a teleport, a zoom) in the window gives
// a false speed for 50 ms.
controller_math::MotionSample g_tip_history[kMaxHistory];
int g_tip_count = 0;

void PushTip(const float world[3]) {
  if (g_tip_count == kMaxHistory) {
    memmove(g_tip_history, g_tip_history + 1,
            sizeof(g_tip_history[0]) * (kMaxHistory - 1));
    --g_tip_count;
  }
  g_tip_history[g_tip_count].time = NowSeconds();
  for (int i = 0; i < 3; ++i) g_tip_history[g_tip_count].point[i] = world[i];
  ++g_tip_count;
}

float TipSpeedMps() {
  if (g_tip_count < 2 ||
      NowSeconds() - g_tip_history[g_tip_count - 1].time > kStaleSeconds) {
    return 0.0f;
  }
  float v[3];
  if (!controller_math::MeanVelocity(g_tip_history, g_tip_count,
                                     kCreatureWindow, v)) {
    return 0.0f;
  }
  const float units = RoomUnits();
  return units > 0.0f ? controller_math::Length(v) / units : 0.0f;
}

// WHERE A THROWN OBJECT GOES, for the test log. The draw offers the object
// each frame while it is in view, so its position is read from the candidates
// and no pointer is followed. Logs the distance from the Hand at the release.
void* g_track_object = nullptr;
uint64_t g_track_frame = 0;
float g_track_from[3] = {0.0f, 0.0f, 0.0f};
int g_track_step = 0;
const uint64_t kTrackFrames[5] = {40, 100, 200, 300, 450};

// Whether an object that the mod kept from an earlier frame still lives. The
// game deletes an object in a game turn: a rock 4 radii under the sea, a
// villager that dies. The destructor writes the table of the base class to
// the first word, and the heap can write its own data there, so the first
// word of a deleted object is not the table of its class.
bool ObjectLives(void* object, void* vtable) {
  if (object == nullptr || vtable == nullptr) return false;
  if (IsBadReadPtr(object, kObjectMesh + sizeof(void*))) return false;
  return *static_cast<void* const*>(object) == vtable;
}

// The target of the grab from a distance still lives. When it does not, the
// far grab lets it go, and no code reads the object again.
bool FarTargetLives() {
  void* object = nullptr;
  void* vtable = nullptr;
  const void* model = nullptr;
  if (!FarGrabTargetFacts(&object, &vtable, &model)) return false;
  if (ObjectLives(object, vtable)) return true;
  FarGrabGone();
  return false;
}

bool CandidatePosition(void* object, float out[3]) {
  for (int i = 0; i < g_candidate_count; ++i) {
    if (g_candidates[i].object != object) continue;
    for (int k = 0; k < 3; ++k) out[k] = g_candidates[i].position[k];
    return true;
  }
  return false;
}

// THE PLACE OF THE THROWN OBJECT, for the test words "seek" and "aimfar"
// (seek_thrown=1, far_test_thrown=1): where the draw of the last frame saw
// the object, or the translation of its mesh now. The draw offers an object
// to the pick only near the Hand or in the cone of the aim, and a rock that
// rolls away from a still hand is in neither. The caller has tested that the
// object lives. drawn says which of the two the place is.
bool ThrownPosition(float out[3], bool* drawn) {
  *drawn = CandidatePosition(g_last_thrown, out);
  if (*drawn) return true;
  uint8_t* const* link = reinterpret_cast<uint8_t* const*>(
      static_cast<uint8_t*>(g_last_thrown) + kObjectMesh);
  if (IsBadReadPtr(link, sizeof(void*)) || *link == nullptr) return false;
  const float* at = reinterpret_cast<const float*>(*link + kMeshTranslation);
  if (IsBadReadPtr(at, 3 * sizeof(float))) return false;
  for (int k = 0; k < 3; ++k) out[k] = at[k];
  return true;
}

void StartTrack(void* object) {
  g_track_object = object;
  g_track_frame = g_frame;
  g_track_step = 0;
  for (int i = 0; i < 3; ++i) g_track_from[i] = g_target[i];
}

void TrackThrow() {
  if (g_track_object == nullptr) return;
  if (g_track_step >= 5) {
    g_track_object = nullptr;
    return;
  }
  const uint64_t wait = kTrackFrames[g_track_step];
  if (g_frame - g_track_frame < wait) return;
  ++g_track_step;
  float at[3];
  if (!CandidatePosition(g_track_object, at)) {
    ModLog("hand throw   : %llu frames after the release the object is not "
           "drawn", wait);
    return;
  }
  const float dx = at[0] - g_track_from[0], dz = at[2] - g_track_from[2];
  ModLog("hand throw   : %llu frames after the release the object is %.1f "
         "units from the Hand at the release (%.1f across, %.1f up). The "
         "real throw lands %.0f units across with no drag", wait,
         Distance(at, g_track_from), sqrtf(dx * dx + dz * dz),
         at[1] - g_track_from[1], g_throw_expected);
}

// THE THROW. Work out the velocity the game reads at the release, once a
// frame. After a grip opening it is the motion latched at the opening (see
// ThrowLatch). Else it comes from the history before the release, so a hand
// that stopped before the release throws softly. With no motion the velocity
// is zero and the game sets the object down.
void PrepareThrow() {
  if (g_throw_frame == g_frame) return;
  g_throw_frame = g_frame;
  ++g_throws;
  float cap = *reinterpret_cast<const float*>(kThrowCap);
  if (IsBadReadPtr(reinterpret_cast<const void*>(kThrowCap), sizeof(float)) ||
      !(cap > 0.0f && cap < 100000.0f)) {
    cap = 124.0f;
  }
  const float gain = ControllersThrowGainPct() * 0.01f;
  float v_room[3], v_grip[3], real = 0.0f, before = 0.0f;
  float height = g_target[1] - g_land_y;
  float sent[3] = {0.0f, 0.0f, 0.0f};
  bool moving = false;
  const double now = NowSeconds();
  const bool latched = g_latch.valid && now - g_latch.time >= 0.0 &&
                       now - g_latch.time <= kLatchSeconds;
  if (latched) {
    moving = g_latch.moving;
    for (int i = 0; i < 3; ++i) {
      v_room[i] = g_latch.v_room[i];
      v_grip[i] = g_latch.v_grip[i];
    }
    real = g_latch.real;
    height = g_latch.height;
    ++g_latched_throws;
  } else {
    moving = HandVelocity(kThrowWindow, v_room, v_grip, &real);
  }
  // One release uses a latch once.
  const double latch_ms = latched ? (now - g_latch.time) * 1000.0 : 0.0;
  const uint64_t latch_frames = latched ? g_frame - g_latch.frame : 0;
  g_latch.valid = false;
  const float hand_speed = moving ? controller_math::Length(v_room) : 0.0f;
  // ON THE TABLE (docs/tabletop.md, "The hand on the table") the speed of the
  // real hand is turned into units at the table scale, and the game's cap still
  // holds. A hand that the life size scale would set down sets the object down.
  const float table = TabletopViewScale();
  const float life = HeadPoseScale();
  bool drop = false;
  g_throw_expected = 0.0f;
  g_throw_still = false;
  // THE REAL FLIGHT (throw_law=2, docs/throw.md, "The throw physics"). The
  // speed is the top speed of the swing, and throw_physics.cpp flies the
  // object in the room as a real object flies. It logs the release too.
  bool real_flight = false;
  if (moving && table > 0.0f && life > 0.0f && ThrowPhysicsLaw() == 2) {
    ThrowPlanIn in = {};
    in.frame = g_frame;
    in.held = *reinterpret_cast<void* const*>(g_hand + kHandHeld);
    in.grip = g_grip_history;
    in.count = g_history_count;
    in.now = now;
    in.latched = latched;
    in.opened = latched ? now - latch_ms * 0.001 : now;
    for (int i = 0; i < 3; ++i) in.v_end_room[i] = v_grip[i];
    for (int i = 0; i < 9; ++i) in.room[i] = g_room[i];
    in.gain = gain;
    in.life = life;
    in.units_per_meter = table;
    in.cap = cap;
    in.drop_speed = kDropSpeed;
    // A thing at the Hand takes the object (a store, a building, an altar):
    // the opening of the grip puts the object on it at each height, so the
    // height rule does not act.
    in.height_m = VillageTakesObject() ? 0.0f : height / table;
    ThrowPlanOut plan = {};
    real_flight = ThrowPhysicsPlan(in, &plan);
    if (real_flight) {
      for (int i = 0; i < 3; ++i) sent[i] = plan.sent[i];
      drop = plan.drop;
      real = plan.real_mps;
      g_throw_still = plan.fall || plan.drop;
    }
  }
  if (real_flight) {
    // The plan of the real flight gave the velocity.
  } else if (moving && table > 0.0f && life > 0.0f) {
    if (FarGrabThrowLaw()) {
      // THE SQUARE ROOT LAW (docs/throw.md, "Why the square root law is not
      // enough"): the speed sent is sqrt(s) times the real speed in m/s, so the
      // object lands where the real throw lands, times s.
      controller_math::SqrtThrowVelocity(v_room, v_grip, g_room, gain, life,
                                         table, cap, kDropSpeed, sent, &before,
                                         &drop);
      // The landing of the real throw with no drag, from the height of the
      // Hand over the land, times s: the test log compares it with the
      // distance that the object flies.
      const double g = 9.81;
      const double vx = sqrt(static_cast<double>(v_grip[0]) * v_grip[0] +
                             static_cast<double>(v_grip[2]) * v_grip[2]) / life;
      const double vy = static_cast<double>(v_grip[1]) / life;
      double h = (g_target[1] - g_land_y) / table;
      if (!(h > 0.0)) h = 0.0;
      const double t = (vy + sqrt(vy * vy + 2.0 * g * h)) / g;
      if (t > 0.0) g_throw_expected = static_cast<float>(vx * t * table);
    } else {
      controller_math::TableThrowVelocity(v_room, v_grip, g_room, gain,
                                          table / life, cap, kDropSpeed, sent,
                                          &before, &drop);
    }
  } else if (moving) {
    controller_math::ThrowVelocity(v_room, v_grip, g_room, gain, cap, sent,
                                   &before);
  }
  for (int i = 0; i < 3; ++i) g_throw_sent[i] = sent[i];
  // The game throws when the speed across the land is above kDropSpeed
  // (InitialisePhysicsFromHand), and sets the object down otherwise.
  g_throw_flies =
      sent[0] * sent[0] + sent[2] * sent[2] > kDropSpeed * kDropSpeed;
  if (drop || !moving) g_throw_still = true;
  if (real_flight) return;
  if (g_throw_logs >= kMaxThrowLogs) return;
  ++g_throw_logs;
  void* held = *reinterpret_cast<void* const*>(g_hand + kHandHeld);
  if (latched) {
    ModLog("hand throw   : the grip opened %.0f ms (%llu frames) before this "
           "release, and the motion is from the %.0f ms before the opening",
           latch_ms, latch_frames, kThrowWindow * 1000.0);
  }
  if (moving && table > 0.0f) {
    if (FarGrabThrowLaw()) {
      ModLog("hand throw   : release on the table at frame %llu with the "
             "square root law: the real hand moved at %.2f m/s, so sqrt(s) "
             "v is %.1f units/s at %.0f units per meter. %s %.1f %.1f %.1f, "
             "%.1f units/s. The game caps at %.0f. The real throw would land "
             "%.0f units away with no drag (s times the real range). Holding "
             "%p", g_frame, real, before, table,
             drop ? "Slow enough to set it down, sent" : "Sent", sent[0],
             sent[1], sent[2], controller_math::Length(sent), cap,
             g_throw_expected, held);
    } else {
      ModLog("hand throw   : release on the table at frame %llu: the real "
             "hand moved at %.2f m/s, which is %.0f units/s at %.0f units per "
             "meter. Gain %d%%. %s %.1f %.1f %.1f, %.1f units/s. The game "
             "caps at %.0f. Holding %p", g_frame, real, before, table,
             ControllersThrowGainPct(),
             drop ? "Slow enough to set it down, sent" : "Sent", sent[0],
             sent[1], sent[2], controller_math::Length(sent), cap, held);
    }
    if (!drop) StartTrack(held);
  } else if (moving) {
    ModLog("hand throw   : release at frame %llu: the real hand moved at "
           "%.2f m/s (%.1f units/s), the Hand at %.1f units/s (reach x%.1f). "
           "Gain %d%%. Sent %.1f %.1f %.1f, %.1f units/s%s. The game caps at "
           "%.0f. Holding %p", g_frame, real, real * HeadPoseScale(),
           hand_speed, g_factor, ControllersThrowGainPct(), sent[0], sent[1],
           sent[2], controller_math::Length(sent),
           before > cap ? " (cut from the gain speed)" : "", cap, held);
    StartTrack(held);
  } else {
    ModLog("hand throw   : release at frame %llu with no hand motion (%d "
           "samples), sent zero. Holding %p", g_frame, g_history_count, held);
  }
}

// Write the velocity of this frame's release where the release action reads
// it. A second run of the action in one frame writes the same numbers.
void WriteThrow() {
  PrepareThrow();
  float* out = reinterpret_cast<float*>(g_hand + kHandThrow);
  for (int i = 0; i < 3; ++i) out[i] = g_throw_sent[i];
}

float ClampToMap(float v) {
  if (!(v >= 0.0f)) return 0.0f;
  return v > 5119.0f ? 5119.0f : v;
}

// Whether the land point x, z is inside the player's influence, by the same
// call that fn_005D1120 makes for +0x48.
bool InsideInfluence(uint8_t* face, float x, float z) {
  void* player = reinterpret_cast<GetPlayerFn>(
      VTable(face)[kFaceGetPlayer / 4])(face, nullptr);
  if (player == nullptr) return false;
  const int32_t coords[3] = {static_cast<int32_t>(x * kMapScale),
                             static_cast<int32_t>(z * kMapScale), 0};
  const PlayerInfluenceFn influence =
      reinterpret_cast<PlayerInfluenceFn>(kPlayerInfluence);
  return influence(coords, player, 0, 1, 1) > 0.0f;
}

// True when the interface step that starts now runs the release of a throw:
// the release is asked for (bit 0x40 of +0x39), the interface action is the
// release action, and the Hand holds an object that is not a miracle and that
// the game throws.
bool ThrowReleaseNow(uint8_t* face) {
  if ((face[kFaceFlags] & 0x40) == 0) return false;
  if (*reinterpret_cast<const int32_t*>(face + kFaceAction) !=
      kReleaseActionIndex) {
    return false;
  }
  void* held = *reinterpret_cast<void* const*>(g_hand + kHandHeld);
  if (held == nullptr) return false;
  if (reinterpret_cast<VirtualIntFn>(VTable(held)[kIsSpellSeed / 4])(
          held, nullptr) != 0) {
    return false;
  }
  PrepareThrow();
  return g_throw_flies;
}

// THE THROW FROM INSIDE THE INFLUENCE (docs/tabletop.md, "The throw from inside
// the influence"). The game lets go of an object only when the land point of
// the release is inside the player's influence. At a large scale a swing
// carries the Hand far over the land: a step of 2 cm is 39.5 units at 1973
// units per meter, and 3 mm at the life size scale. So a throw that ends
// outside the influence goes from the newest point of the Hand's path in the
// last half second that is inside it. The object still flies from the Hand,
// because the throw takes its start and its velocity from packet 0x4D, not from
// this point. A set down, a miracle, and a throw whose whole swing was outside
// keep the game's rule. Changes x and z, once a frame.
void ThrowFrom(uint8_t* face, float* x, float* z) {
  if (g_from_frame == g_frame) {
    *x = g_from_x;
    *z = g_from_z;
    return;
  }
  g_from_frame = g_frame;
  g_from_x = *x;
  g_from_z = *z;
  if (InsideInfluence(face, *x, *z)) return;
  const double now = NowSeconds();
  const int i = table_math::ThrowFromIndex(
      g_path_time, g_path_count, now, kThrowFromWindow, [&](int k) {
        return InsideInfluence(face, ClampToMap(g_path_x[k]),
                               ClampToMap(g_path_z[k]));
      });
  if (i < 0) {
    if (g_throw_logs < kMaxThrowLogs) {
      ++g_throw_logs;
      ModLog("hand throw   : the throw ends at %.1f %.1f, outside your "
             "influence, and the Hand was not inside it in the last %.1f s. "
             "The game keeps the object, as it does for the mouse. Frame %llu",
             *x, *z, kThrowFromWindow, g_frame);
    }
    return;
  }
  g_from_x = ClampToMap(g_path_x[i]);
  g_from_z = ClampToMap(g_path_z[i]);
  ++g_throws_from;
  if (g_throw_logs < kMaxThrowLogs) {
    ++g_throw_logs;
    ModLog("hand throw   : the throw ends at %.1f %.1f, outside your "
           "influence. It goes from %.1f %.1f, the last point inside, %.0f ms "
           "before the release and %.1f units back. Frame %llu", *x, *z,
           g_from_x, g_from_z, (now - g_path_time[i]) * 1000.0,
           sqrtf((*x - g_from_x) * (*x - g_from_x) +
                 (*z - g_from_z) * (*z - g_from_z)),
           g_frame);
  }
  *x = g_from_x;
  *z = g_from_z;
}

void __fastcall h_ReleaseAction(void* self, void* unused) {
  // Only while the controller places the Hand, and only when the game will
  // run the release (bit 6 of +0x39) with an object in the Hand. A mouse
  // throw does not pass through here.
  void* thrown = nullptr;
  if (g_owns && g_hand != nullptr && self != nullptr && self == g_face &&
      (static_cast<const uint8_t*>(self)[kFaceFlags] & 0x40) != 0 &&
      *reinterpret_cast<void* const*>(g_hand + kHandHeld) != nullptr) {
    WriteThrow();
    thrown = *reinterpret_cast<void* const*>(g_hand + kHandHeld);
  }
  o_ReleaseAction(self, unused);
  // THE REAL FLIGHT (throw_law=2). When the game let go of the object,
  // throw_physics.cpp flies it from this moment.
  if (thrown != nullptr) {
    // The tick of a release: the grip opened, and the game lets the object
    // go now. Once a frame.
    if (g_haptic_release_frame != g_frame) {
      g_haptic_release_frame = g_frame;
      HapticsPulse(haptics_math::kLetGo);
    }
    g_last_thrown = thrown;
    g_last_thrown_vtable = *static_cast<void* const*>(thrown);
    g_last_thrown_time = NowSeconds();
    g_last_thrown_fell = g_throw_still;
    ThrowPhysicsReleased(thrown);
  }
}

// THE CREATURE BY TOUCH (docs/creature.md, "The Creature by touch").

// The Get2DRadius of an object, or 0 when it gives no sane value.
float Radius2D(void* object) {
  if (object == nullptr) return 0.0f;
  const float radius = reinterpret_cast<VirtualFloatFn>(
      VTable(object)[kGet2DRadius / 4])(object, nullptr);
  return (radius > 0.0f && radius < 1000.0f) ? radius : 0.0f;
}

// The LH3DCreature of a Creature object: [[object+0x160]+0x58], as
// OperateInteractionCamera finds it (0x0046CBC0). Null when it cannot be
// read.
uint8_t* CreatureModel(void* object) {
  if (object == nullptr) return nullptr;
  uint8_t* const* link = reinterpret_cast<uint8_t* const*>(
      static_cast<uint8_t*>(object) + 0x160);
  if (IsBadReadPtr(link, sizeof(void*)) || *link == nullptr) return nullptr;
  uint8_t* const* model = reinterpret_cast<uint8_t* const*>(*link + 0x58);
  if (IsBadReadPtr(model, sizeof(void*)) || *model == nullptr) return nullptr;
  if (IsBadReadPtr(*model, kCreatureLive + 0x14)) return nullptr;
  return *model;
}

// The body bone of an LH3DCreature, in the world, with no test of the
// Creature. The game reads it so for a Creature that the Hand does not have
// (fn_005D3DD0 at 0x005D3E33, fn_005CFDE0 at 0x005CFF29).
bool BodyBoneRaw(uint8_t* model, float out[3]) {
  if (model == nullptr) return false;
  const float* bone =
      reinterpret_cast<BodyBoneFn>(kBodyBone)(model, nullptr);
  if (bone == nullptr || IsBadReadPtr(bone, 3 * sizeof(float))) return false;
  for (int i = 0; i < 3; ++i) {
    if (!(bone[i] > -100000.0f && bone[i] < 100000.0f)) return false;
    out[i] = bone[i];
  }
  return true;
}

// The body bone while the Creature is ready for the Hand: the state's own
// test (0x005B17EA). False at each time before a press on the Creature.
bool BodyBone(uint8_t* model, float out[3]) {
  if (model == nullptr) return false;
  if (*reinterpret_cast<const int32_t*>(model + kCreatureLive) == 0) {
    return false;
  }
  return BodyBoneRaw(model, out);
}

// WHERE THE BODY OF A CREATURE IS, AT EACH TIME (docs/creature.md, "The body of
// the Creature before a press"). The answer is 1 for the body bone, 2 for the
// place of the Creature on the map (the bone is not sane, near_math::
// BoneSane), and 0 for no place. With creature_bone=0 the bone counts only
// while the Creature is ready for the Hand.
int BodyPlace(void* object, float out[3]) {
  uint8_t* model = CreatureModel(object);
  if (model == nullptr) return 0;
  if (!g_creature_bone) return BodyBone(model, out) ? 1 : 0;
  const float size = *reinterpret_cast<const float*>(model + kCreatureSize);
  if (!(size > 0.0f && size < 1000.0f)) return 0;
  const int32_t* map = reinterpret_cast<const int32_t*>(
      static_cast<const uint8_t*>(object) + 0x14);
  if (IsBadReadPtr(map, 2 * sizeof(int32_t))) return 0;
  const float x = map[0] / kMapScale, z = map[1] / kMapScale;
  float bone[3];
  const bool have = BodyBoneRaw(model, bone);
  const bool sane = have && near_math::BoneSane(bone[0] - x, bone[2] - z, size);
  const int32_t coords[3] = {map[0], map[1], 0};
  const float land = sane ? 0.0f
                          : reinterpret_cast<GetAltitudeFn>(kGetAltitude)(
                                coords);
  const int source = sane ? 1 : (land == land ? 2 : 0);
  if (source == 1) {
    for (int i = 0; i < 3; ++i) out[i] = bone[i];
  } else if (source == 2) {
    out[0] = x;
    out[1] = land + 0.5f * 15.0f * size;
    out[2] = z;
  }
  // One line for each change of the answer.
  if ((object != g_bone_object || source != g_bone_source) &&
      g_bone_logs < 24) {
    ++g_bone_logs;
    g_bone_object = object;
    g_bone_source = source;
    const int ready = *reinterpret_cast<const int32_t*>(model + kCreatureLive);
    if (have) {
      const float dx = bone[0] - x, dz = bone[2] - z;
      ModLog("hand creature: the body of the Creature %p is at %s. Its body "
             "bone is at %.2f %.2f %.2f, %.2f units from its place on the map "
             "(%.2f %.2f) and %.2f above the land there. A sane bone is not "
             "more than %.2f units from that place. Size %.2f, ready for the "
             "Hand %d, frame %llu", object,
             source == 1 ? "its body bone" : "its place on the map",
             bone[0], bone[1], bone[2], sqrtf(dx * dx + dz * dz), x, z,
             bone[1] - reinterpret_cast<GetAltitudeFn>(kGetAltitude)(coords),
             15.0f * size + 2.0f, size, ready, g_frame);
    } else {
      ModLog("hand creature: the body of the Creature %p is at %s. It has no "
             "body bone that the mod can read. Size %.2f, ready for the Hand "
             "%d, frame %llu", object,
             source == 2 ? "its place on the map" : "no place", size, ready,
             g_frame);
    }
  }
  ++g_bone_counts[source];
  return source;
}

// The Creature object of the state: +0x49B4, or the last one of this state
// when the game has cleared it.
void* TouchObject(const uint8_t* hand) {
  void* object = *reinterpret_cast<void* const*>(hand + kHandCreatureObject);
  return object != nullptr ? object : g_touch_object;
}

// The first Creature Update of a frame works out the touch of the frame: the
// body capsule, the contact, the swept test and the speed. A later Update of
// the same frame (the second eye) gets speed zero, as with the mouse, and the
// same contact.
void TouchFrame(uint8_t* state, uint8_t* hand, float dt) {
  if (g_touch_frame == g_frame) {
    for (int i = 0; i < 3; ++i) g_prev_point[i] = g_target[i];
    g_prev_valid = true;
    g_touch_fix_x = false;
    return;
  }
  g_touch_frame = g_frame;
  const bool fresh = state != g_touch_state || g_touch_last + 1 < g_frame;
  g_touch_last = g_frame;
  g_touch_state = state;
  if (fresh) {
    g_touch_from_valid = false;
    g_touch_frames = 0;
    g_touch_said = false;
    g_touch_object = nullptr;
    // THE WAIT. The state counts +0xDC up from 0 and slaps only past 1000
    // ms. Without creature_slap_wait the count starts at 1000, so the first
    // slap counts at once. A stroke sets it to 0 again (0x005B226C).
    int32_t* time = reinterpret_cast<int32_t*>(state + kStateTime);
    const bool wait = ControllersCreatureSlapWait();
    if (!wait && *time < 1000) {
      *time = 1000;
      ++g_touch_waits;
    }
    if (g_touch_logs < kMaxTouchLogs) {
      ++g_touch_logs;
      ModLog("hand creature: the Creature state starts, frame %llu. %s",
             g_frame, wait ? "The game's wait of 1 s before a slap stays."
                           : "A slap can count at once.");
    }
  }
  ++g_touch_frames;
  void* object = *reinterpret_cast<void* const*>(hand + kHandCreatureObject);
  if (object != nullptr) g_touch_object = object;
  ResolveLand();

  // THE BODY: a vertical capsule through the body bone, from the land up to
  // 1.1 S, the top of the game's slap test, with S = 15 * size. Its radius is
  // the Creature's 2D radius and creature_touch_cm of the room.
  g_touch_valid = false;
  uint8_t* model = *reinterpret_cast<uint8_t**>(hand + kHandCreature);
  const float size = CreatureSize(hand);
  float bone[3];
  if (size > 0.0f && BodyBone(model, bone)) {
    const int32_t coords[3] = {static_cast<int32_t>(bone[0] * kMapScale),
                               static_cast<int32_t>(bone[2] * kMapScale), 0};
    const float land =
        reinterpret_cast<GetAltitudeFn>(kGetAltitude)(coords);
    float radius = Radius2D(TouchObject(hand));
    // No radius: a guess of a third of the height.
    if (!(radius > 0.0f)) radius = 0.35f * 15.0f * size;
    g_touch_radius = radius;
    const float table = TabletopViewScale();
    const float room = table > 0.0f ? table : HeadPoseScale();
    g_touch_body.base[0] = bone[0];
    g_touch_body.base[1] = land;
    g_touch_body.base[2] = bone[2];
    g_touch_body.height = 1.1f * 15.0f * size;
    g_touch_body.radius = radius + ControllersCreatureTouchCm() * 0.01f * room;
    g_touch_valid = land == land;
  }
  // THE HAND OF THE STROKE (docs/creature.md, "The open hand").
  // The flat hand for a Creature that is large against the hand, and the
  // index finger for a small one. The contact is at the palm of the flat hand
  // and at the tip of the finger.
  const float hand_length = kHandLength * g_scale;
  const float ratio = g_touch_valid && hand_length > 0.0f
                          ? g_touch_body.height / hand_length
                          : 0.0f;
  float finger_below = 0.0f, flat_above = 0.0f;
  StrokeLimitsNow(&finger_below, &flat_above);
  const bool was_finger =
      fresh ? near_math::StrokeFingerAtStart(ratio, finger_below, flat_above)
            : g_stroke_finger;
  // With the fist stroke (creature_stroke_hand=3) the contact is at the
  // Hand's point, as for the finger.
  g_stroke_finger = g_stroke_mode == 1 ? false
                    : g_stroke_mode == 2 || g_stroke_mode == kStrokeBefore
                        ? true
                        : near_math::StrokeFinger(ratio, was_finger,
                                                  finger_below, flat_above);
  float contact[3] = {g_target[0], g_target[1], g_target[2]};
  if (!g_stroke_finger && !g_palm_now) {
    float palm[3];
    uint64_t palm_frame = 0;
    int32_t palm_state = -1;
    if (HandExactPalm(palm, &palm_frame, &palm_state) &&
        palm_frame + 1 >= g_frame && palm_frame <= g_frame) {
      for (int i = 0; i < 3; ++i) contact[i] = palm[i];
    }
  }
  g_touch_gap = g_touch_valid
                    ? controller_math::CapsuleGap(g_touch_body, contact)
                    : 0.0f;
  const bool was_inside = g_touch_inside && !fresh;
  g_touch_inside = g_touch_valid && g_touch_gap <= 0.0f;
  // One tick when the contact point goes into the body.
  if (g_touch_inside && !was_inside) HapticsPulse(haptics_math::kTouch);
  // A change of the hand moves the contact point, and that is no slap.
  g_touch_entered = g_touch_valid && g_touch_from_valid &&
                    g_stroke_finger == g_stroke_was_finger &&
                    controller_math::CapsuleEntered(g_touch_body,
                                                    g_touch_from, contact);
  // THE TICKLE: the wave of the fingers goes on with the path of the contact
  // point while it is in the body.
  if (fresh) {
    g_stroke_phase = 0.0f;
    g_stroke_path_cm = 0.0f;
  }
  const float room_units = RoomUnits();
  if (g_touch_inside && g_touch_from_valid && room_units > 0.0f &&
      g_stroke_finger == g_stroke_was_finger) {
    const float moved_cm =
        Distance(contact, g_touch_from) / room_units * 100.0f;
    g_stroke_phase = near_math::TicklePhase(g_stroke_phase, moved_cm);
    // THE TEXTURE OF A STROKE: a faint tick for each 1.5 cm of path of the
    // contact point in the body. A hand that is still gives none.
    if (haptics_math::StrokeTick(&g_stroke_path_cm, moved_cm)) {
      HapticsPulse(haptics_math::kStroke);
    }
  }
  if (g_stroke_finger != g_stroke_was_finger || fresh) {
    if (g_touch_logs < kMaxTouchLogs) {
      ++g_touch_logs;
      ModLog("hand creature: the hand of the stroke is %s. The body is %.1f "
             "cm of the room high, %.2f of the hand's length (%s: the finger "
             "below %.2f, the flat hand over %.2f, and a stroke starts with "
             "the flat hand from %.2f. creature_stroke_hand=%d). The contact "
             "point is %.2f %.2f %.2f, %.1f cm of the room from the tip of "
             "the index finger and %.1f cm %s the body, frame %llu",
             g_stroke_mode == kStrokeBefore
                 ? "a fist while the grip is closed (the fist stroke), and "
                   "the Hand's point touches"
                 : g_stroke_finger ? "the index finger, and its tip touches"
                                   : "the flat hand, and its palm touches",
             room_units > 0.0f ? g_touch_body.height / room_units * 100.0f
                               : 0.0f,
             ratio,
             fresh ? "the start of a stroke" : "a change in a stroke",
             finger_below, flat_above, 0.5f * (finger_below + flat_above),
             g_stroke_mode, contact[0], contact[1], contact[2],
             room_units > 0.0f
                 ? Distance(contact, g_point) / room_units * 100.0f
                 : 0.0f,
             room_units > 0.0f ? fabsf(g_touch_gap) / room_units * 100.0f
                               : 0.0f,
             g_touch_inside ? "inside" : "outside", g_frame);
    }
  }
  g_stroke_was_finger = g_stroke_finger;
  g_stroke_frame = g_frame;
  for (int i = 0; i < 3; ++i) g_touch_from[i] = contact[i];
  g_touch_from_valid = true;

  // THE SPEED. The last point goes across the line of sight by k times the
  // real speed times the game's dt, on the side the hand comes from.
  float v_room[3], v_grip[3], real = 0.0f;
  const bool step = dt > 0.0f && dt < 0.5f;
  const bool moving =
      step && HandVelocity(kCreatureWindow, v_room, v_grip, &real);
  float v_world[3] = {0.0f, 0.0f, 0.0f};
  if (moving) controller_math::Apply(g_room, false, v_room, v_world);
  // A line of sight straight down keeps the axis of the last frame.
  controller_math::SlapAcross(Camera(), g_target, g_touch_across);
  const float along = v_world[0] * g_touch_across[0] +
                      v_world[2] * g_touch_across[2];
  const float sign = along < 0.0f ? -1.0f : 1.0f;
  g_touch_step_x = along < 0.0f ? -1 : 1;
  // THE SPEED IS THE SPEED OF THE PART THAT HITS. A slap with the wrist, as a
  // backhand is, keeps the grip almost still and moves the finger tips. So the
  // speed is the larger of the grip's and the tip's.
  g_creature_grip = moving ? real : 0.0f;
  g_creature_tip = moving ? TipSpeedMps() : 0.0f;
  for (int i = 0; i < 3; ++i) g_creature_way[i] = moving ? v_room[i] : 0.0f;
  g_creature_k = controller_math::SlapUnitsPerMps(size, SlapMps());
  g_creature_real = g_stroke_mode == kStrokeBefore
                        ? g_creature_grip
                        : near_math::SlapSpeed(g_creature_grip, g_creature_tip);
  g_creature_speed = g_creature_k * g_creature_real;
  // The game starts its slap effect inside the Update that follows. The
  // pulse of that effect takes its strength from this speed (haptics.cpp).
  HapticsSlapSpeed(g_creature_real, SlapMps());
  controller_math::SlapPreviousPoint(g_target, g_touch_across, sign,
                                     g_creature_k, g_creature_real,
                                     step ? dt : 0.0f, g_prev_point);
  g_prev_valid = true;

  // THE SCREEN CENTER TEST. The game also takes the slap branch when the x
  // of the screen point crossed the center since the last point
  // (0x005B1C0A). The last pixel is from the last eye pass, so a head turn
  // could slap. The mod puts the last x one pixel behind the current one,
  // on the side the hand comes from: two integers next to each other never
  // cross the center, and the game reads the direction of the slap from
  // their difference (0x005B1E15). When the Hand went into the body since
  // the last frame, the last x goes to the other side of the center, which
  // forces the slap branch. The ray of the speed test puts it one pixel
  // behind again, before the game reads the direction.
  int32_t* now = reinterpret_cast<int32_t*>(hand + kHandScreen);
  int32_t* last = reinterpret_cast<int32_t*>(hand + kHandPrevScreen);
  const int32_t half = static_cast<int32_t>(
      (*reinterpret_cast<const uint32_t*>(kGameWidth) & 0xFFFFu) >> 1);
  g_touch_fix_x = false;
  if (g_touch_entered && now[0] != half) {
    last[0] = 2 * half - now[0];
    g_touch_fix_x = true;
    ++g_touch_forced;
  } else {
    last[0] = now[0] - g_touch_step_x;
  }

  // The test log, once a state: the body, and where the game put the Hand.
  if (g_touch_frames == 30 && !g_touch_said && g_touch_logs < kMaxTouchLogs) {
    g_touch_said = true;
    ++g_touch_logs;
    const float* at = reinterpret_cast<const float*>(hand + kHandPosition);
    ModLog("hand creature: 30 frames in the state. The body is %.2f units "
           "high and %.2f across (2D radius %.2f and %d cm of the room). The "
           "Hand's point is %.2f units %s it. hand+0x78 is %.4f units from "
           "the target, and the size +0x4834 is %.4f for the real hand's "
           "%.4f. Creature size %.3f, k %.2f units/s for 1 m/s",
           g_touch_body.height, 2.0f * g_touch_body.radius, g_touch_radius,
           ControllersCreatureTouchCm(), fabsf(g_touch_gap),
           g_touch_inside ? "inside" : "outside", Distance(at, g_target),
           *reinterpret_cast<const float*>(hand + kHandScale), g_scale,
           size, g_creature_k);
  }
}

// Before the state's Update: the touch of the frame. After it: what the
// game did with it, for the log.
void __fastcall h_CreatureUpdate(void* self, void* unused, float dt,
                                 float* matrix) {
  uint8_t* state = static_cast<uint8_t*>(self);
  uint8_t* hand = state != nullptr
                      ? *reinterpret_cast<uint8_t**>(state + kStateHand)
                      : nullptr;
  const bool ours = g_active && hand != nullptr && hand == g_hand &&
                    State(hand) == kStateCreature;
  if (!ours) {
    o_CreatureUpdate(self, unused, dt, matrix);
    return;
  }
  const float before = *reinterpret_cast<const float*>(state + kStateTotal);
  const bool first = g_touch_frame != g_frame;
  TouchFrame(state, hand, dt);
  const bool entered = first && g_touch_entered;
  g_in_creature = true;
  o_CreatureUpdate(self, unused, dt, matrix);
  g_in_creature = false;
  g_prev_valid = false;
  g_touch_fix_x = false;
  const float after = *reinterpret_cast<const float*>(state + kStateTotal);
  g_creature_total = after;
  const bool slap = after < before - 0.05f;
  const bool stroke = after > before + 0.05f;
  const float slap_mps = SlapMps();
  const float s = 15.0f * CreatureSize(hand);
  // The motion of the hand in the room, as a unit: right, up and forward.
  float way[3] = {0.0f, 0.0f, 0.0f};
  const float way_length = controller_math::Length(g_creature_way);
  if (way_length > 1e-6f) {
    for (int i = 0; i < 3; ++i) way[i] = g_creature_way[i] / way_length;
  }
  if (!slap && !stroke) {
    // A HAND THAT WENT INTO THE BODY AND DID NOT SLAP, so a log shows how
    // hard the player swung when a slap did not count, and why.
    if (!entered || g_miss_logs >= kMaxMissLogs) return;
    ++g_miss_logs;
    const int32_t waited = *reinterpret_cast<const int32_t*>(state + kStateTime);
    ModLog("hand creature: the hand went into the body at %.2f m/s (the grip "
           "%.2f, the Hand's point %.2f), and that was no slap. A slap needs "
           "%.2f m/s. %s. The motion in the room: %.2f right, %.2f up, %.2f "
           "forward. The hand is %s, frame %llu", g_creature_real,
           g_creature_grip, g_creature_tip, slap_mps,
           g_creature_real < slap_mps
               ? "The hand was too slow"
               : waited < 1000 ? "The game waits 1 s after a stroke"
                               : "The speed was enough: the game's own test "
                                 "of the height or of the hit said no",
           way[0], way[1], way[2],
           g_stroke_finger ? "the index finger" : "the flat hand", g_frame);
    return;
  }
  if (slap) ++g_slaps; else ++g_strokes;
  // Each slap goes to the log. A long stroke is many steps of the total, so
  // the strokes have a limit of their own.
  static int slap_logs = 0;
  if (slap ? slap_logs >= 200 : g_creature_logs >= kMaxCreatureLogs) return;
  if (slap) ++slap_logs; else ++g_creature_logs;
  ModLog("hand creature: a %s with %s, %s as the eye sees it. The hand moved "
         "at %.2f m/s (the grip %.2f, the Hand's point %.2f), which the game "
         "read as %.1f units/s. A slap needs %.2f m/s (%.1f units/s) and a "
         "hard slap %.2f m/s (%.1f). The motion in the room: %.2f right, "
         "%.2f up, %.2f forward. The contact point is %.2f units %s the "
         "body%s. Total %.2f to %.2f, dt %.4f. The state after it: contact "
         "%d, animation 0x%X, ms since a stroke %d, %s Update of the frame, "
         "frame %llu",
         slap ? (g_creature_speed >= 9.0f * s ? "hard slap" : "slap")
              : "stroke",
         g_stroke_finger ? "the index finger" : "the flat hand",
         g_touch_step_x > 0 ? "to the right" : "to the left", g_creature_real,
         g_creature_grip, g_creature_tip, g_creature_speed, slap_mps,
         5.0f * s, 1.8f * slap_mps, 9.0f * s, way[0], way[1], way[2],
         fabsf(g_touch_gap), g_touch_inside ? "inside" : "outside",
         entered ? ", and it went into the body since the last frame" : "",
         before, after, dt,
         *reinterpret_cast<const int32_t*>(state + 0x110),
         *reinterpret_cast<const int32_t*>(state + 0x114),
         *reinterpret_cast<const int32_t*>(state + kStateTime),
         first ? "the first" : "a later", g_frame);
}

// For overlay.cpp (docs/creature.md, "The open hand"): the hand
// of a stroke. 0 while the Hand is not in the Creature state. 1 for the flat
// hand, 2 for the index finger and 3 for the fist of the fist stroke
// (creature_stroke_hand=3). curl gets what the tickle adds to the curl
// of each finger (the thumb, then the index to the little finger): for the
// flat hand each finger, and for the index finger only that one.
bool CreatureBody(void* object, controller_math::Capsule* body);

int StrokeHand(float curl[5]) {
  for (int i = 0; i < 5; ++i) curl[i] = 0.0f;
  if (!g_owns || g_hand == nullptr) return 0;
  if (State(g_hand) != kStateCreature || g_stroke_frame + 2 < g_frame) {
    // THE WAIT BEFORE THE STATE. The game starts the Creature state only
    // when the Creature has ended its body action, 0.5 to 2.5 s after the
    // press (docs/creature.md, "The body of the Creature before a press"). The
    // grip is closed for that time, so the hand was a fist. A press that is
    // past the time of a leash click is a stroke, so the hand opens then:
    // the hand that the stroke starts with, by the size of the Creature.
    if (!g_stroke_wait_open || !g_virtual || g_virtual_creature == nullptr ||
        g_stroke_mode == kStrokeBefore ||
        NowSeconds() - g_virtual_since < kLeashSeconds) {
      return 0;
    }
    bool finger = g_stroke_mode == 2;
    if (g_stroke_mode == 0) {
      controller_math::Capsule body;
      const float hand_length = kHandLength * g_scale;
      if (!CreatureBody(g_virtual_creature, &body) || !(hand_length > 0.0f)) {
        return 0;
      }
      float finger_below = 0.0f, flat_above = 0.0f;
      StrokeLimitsNow(&finger_below, &flat_above);
      finger = near_math::StrokeFingerAtStart(body.height / hand_length,
                                              finger_below, flat_above);
    }
    if (g_stroke_wait_said != g_virtual_since && g_touch_logs < kMaxTouchLogs) {
      ++g_touch_logs;
      g_stroke_wait_said = g_virtual_since;
      ModLog("hand creature: the press is %.0f ms old and the Creature state "
             "has not started. The hand is open now: %s "
             "(creature_open_wait=1), frame %llu",
             (NowSeconds() - g_virtual_since) * 1000.0,
             finger ? "the index finger" : "the flat hand", g_frame);
    }
    return finger ? 2 : 1;
  }
  if (g_stroke_mode == kStrokeBefore) return kStrokeBefore;
  if (g_stroke_finger) {
    curl[1] = near_math::TickleCurl(g_stroke_phase, 1);
    return 2;
  }
  for (int i = 1; i < 5; ++i) {
    curl[i] = near_math::TickleCurl(g_stroke_phase, i);
  }
  return 1;
}

// NO TOUCH AT A DISTANCE. The state's mesh ray goes from the eye through the
// Hand's point, so it also hits a Creature far behind the Hand. While the
// controller places the Hand, a hit counts only with the Hand's point in the
// body. The ray of the middle pixel counts not at all when the swept test
// forced the slap branch, because that pixel is then the screen center.
// Only the two calls of the Creature state, by their return address.
int FightMeshRay(void* self, void* unused, void* a, void* b, void* c, void* d,
                 void* e, void* f, void* g, void* h);

int __fastcall h_MeshRay(void* self, void* unused, void* a, void* b, void* c,
                         void* d, void* e, void* f, void* g, void* h) {
  if (reinterpret_cast<uintptr_t>(_ReturnAddress()) == kMeshRayFight) {
    return FightMeshRay(self, unused, a, b, c, d, e, f, g, h);
  }
  if (g_in_creature && g_touch_valid) {
    const uintptr_t from = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const bool at_hand = from == kMeshRayHand;
    const bool middle = from == kMeshRayMiddle;
    const bool forced = middle && g_touch_entered && g_touch_frame == g_frame;
    if ((at_hand || middle) && (!g_touch_inside || forced)) {
      ++g_touch_vetoes;
      return 0;
    }
  }
  return o_MeshRay(self, unused, a, b, c, d, e, f, g, h);
}

// The local player's GPlayer, as OperateInteractionCamera finds it
// (0x0046CC8A).
const void* LocalPlayer() {
  const uint8_t* game = *reinterpret_cast<uint8_t* const*>(kGame);
  if (game == nullptr) return nullptr;
  return game + kGamePlayers + kGamePlayerSize * game[kGameLocalPlayer];
}

// THE LEASH CLICK (docs/creature.md, "The leash from a distance"). What a click
// on this Creature does to the leash now, as a near_math::LeashKind. The
// handler of packet 0x5F, fn_005D06E0, takes the Creature of the player
// (GPlayer+0xA4C). It does nothing when the Creature does not live (slot
// 0x2C), when it can have no leash (Creature+0x1110 is 0), or when the leash
// of another player is on it (Creature::GetInterfaceStatusLeashOn
// 0x004CF060). Then fn_005E7140 takes a leash that is tied to an object back
// to the Hand, and else it turns the leash on or off. [V]
const int kPlayerCreature = 0xA4C;     // GPlayer: the Creature of the player
const int kCreatureLives = 0x2C;       // a vtable slot of the Creature
const int kCreatureCanLeash = 0x1110;  // Creature: not 0 when a leash can go on
const uintptr_t kLeashStatusOn = 0x004CF060;  // thiscall, no arguments

// True for the Creature of the local player.
bool IsOwnCreature(void* creature) {
  const uint8_t* player = static_cast<const uint8_t*>(LocalPlayer());
  return creature != nullptr && player != nullptr &&
         *reinterpret_cast<void* const*>(player + kPlayerCreature) == creature;
}

int LeashKind(void* creature) {
  if (creature == nullptr || g_face == nullptr) return near_math::kLeashNone;
  const uint8_t* player = static_cast<const uint8_t*>(LocalPlayer());
  const uint8_t* status =
      *reinterpret_cast<uint8_t* const*>(g_face + kFaceStatus);
  if (player == nullptr || status == nullptr) return near_math::kLeashNone;
  const uint8_t* leash =
      *reinterpret_cast<uint8_t* const*>(status + kStatusLeash);
  if (leash == nullptr || IsBadReadPtr(creature, kCreatureCanLeash + 4)) {
    return near_math::kLeashNone;
  }
  const bool mine =
      *reinterpret_cast<void* const*>(player + kPlayerCreature) == creature;
  const bool ready =
      mine &&
      reinterpret_cast<VirtualIntFn>(VTable(creature)[kCreatureLives / 4])(
          creature, nullptr) != 0 &&
      *reinterpret_cast<const int32_t*>(static_cast<const uint8_t*>(creature) +
                                        kCreatureCanLeash) != 0;
  const void* holder =
      mine ? reinterpret_cast<GetPlayerFn>(kLeashStatusOn)(creature, nullptr)
           : nullptr;
  return near_math::LeashClick(
      mine, ready, holder != nullptr && holder != status,
      *reinterpret_cast<const int32_t*>(leash + kLeashOn) != 0,
      *reinterpret_cast<void* const*>(leash + kLeashObject) != nullptr);
}

// A FIGHT OF CREATURES (docs/creature.md, "A fight of Creatures").
// Creature::IsFighting 0x0047B1C0 reads the LH3DCreature of the Creature
// (+0x160, then +0x58): in a fight the LH3DCreature of the other Creature is
// at +0x528C. [V]
const int kCreatureFoe = 0x528C;     // LH3DCreature: the other Creature
const int kCreatureLod = 0x98;       // LH3DCreature: the index of the mesh
const int kCreatureMeshes = 0xB4;    // LH3DCreature: the meshes, by index
const int kMaxCreatureLod = 16;

bool IsCreatureObject(void* object);

// True while the Creature of the local player is in a fight.
bool InFight() {
  const uint8_t* player = static_cast<const uint8_t*>(LocalPlayer());
  if (player == nullptr) return false;
  void* creature = *reinterpret_cast<void* const*>(player + kPlayerCreature);
  const uint8_t* model = CreatureModel(creature);
  if (model == nullptr || IsBadReadPtr(model + kCreatureFoe, sizeof(void*))) {
    return false;
  }
  return *reinterpret_cast<void* const*>(model + kCreatureFoe) != nullptr;
}

// THE PRESS OF A FIGHT ON A CREATURE. The interface step of a fight
// (0x005D4AC0) takes a press of either button with a Creature in the pick
// record to fn_005D3EF0 (packet 0x23 for the left button and 0x22 for the
// right button). That function casts a ray from the stored camera through the
// pointer of the interface (GInterface+0x418) against the mesh of the
// Creature (the call at 0x005D4033). Only when the ray hits does the press
// act: an attack at that place of the other Creature, or a block at that
// place of your Creature, and a mark there. With the direct hand that
// pointer is the Hand's point seen from the eye, and the step runs with the
// camera of the game. So the ray is not the ray of the Hand, and it misses
// the body that the Hand is at.
//
// With fight_grip=1, while the controller placed the Hand and the record holds
// the Creature that is the target at the Hand, the ray goes from the Hand's
// point to the body (fight_math::BodyRay). When it misses, the game's ray
// stays. The game makes all the rest from the place that the ray hits.
bool FightBodyRay(const void* mesh, void** creature, float origin[3],
                  float direction[3]) {
  if (!g_fight_grip || !g_owns || g_face == nullptr || g_hand == nullptr) {
    return false;
  }
  void* object = *reinterpret_cast<void* const*>(g_face + kFaceNextObject);
  if (object == nullptr || object != g_choice || g_choice_far ||
      !IsCreatureObject(object)) {
    return false;
  }
  const uint8_t* model = CreatureModel(object);
  if (model == nullptr) return false;
  const int32_t lod = *reinterpret_cast<const int32_t*>(model + kCreatureLod);
  if (lod < 0 || lod >= kMaxCreatureLod ||
      IsBadReadPtr(model + kCreatureMeshes + 4 * lod, sizeof(void*)) ||
      *reinterpret_cast<const void* const*>(model + kCreatureMeshes +
                                            4 * lod) != mesh) {
    return false;
  }
  controller_math::Capsule body;
  if (!CreatureBody(object, &body)) return false;
  *creature = object;
  return fight_math::BodyRay(body.base, body.height, body.radius, g_target,
                             Camera(), origin, direction);
}

int FightMeshRay(void* self, void* unused, void* a, void* b, void* c, void* d,
                 void* e, void* f, void* g, void* h) {
  void* creature = nullptr;
  float origin[3], direction[3];
  if (!FightBodyRay(a, &creature, origin, direction)) {
    return o_MeshRay(self, unused, a, b, c, d, e, f, g, h);
  }
  // The game's ray first, for the log. A copy, so that its own arrays stay.
  float game_origin[3], game_direction[3];
  memcpy(game_origin, c, sizeof(game_origin));
  memcpy(game_direction, d, sizeof(game_direction));
  const int game =
      o_MeshRay(self, unused, a, b, game_origin, game_direction, e, f, g, h);
  const int ours = o_MeshRay(self, unused, a, b, origin, direction, e, f, g, h);
  ++g_fight_rays;
  if (ours != 0) ++g_fight_hits;
  if (game != 0) ++g_fight_game_hits;
  // One line for each press, and one more when the result changes: the step
  // runs on each frame while the button is down and the ray misses.
  const int hit = ours != 0 ? 1 : 0;
  if ((g_frame > g_fight_ray_frame + 1 || hit != g_fight_ray_hit) &&
      g_fight_logs < kMaxFightLogs) {
    ++g_fight_logs;
    controller_math::Capsule body;
    const bool have = CreatureBody(creature, &body);
    const float units = RoomUnits();
    float top[3] = {0.0f, 0.0f, 0.0f};
    if (have) {
      for (int i = 0; i < 3; ++i) top[i] = body.base[i];
      top[1] += body.height;
    }
    const float gap =
        have ? controller_math::SegmentPointDistance(body.base, top,
                                                     g_target) -
                   body.radius
             : 0.0f;
    ModLog("hand fight   : a press of the fight on %s %p: the ray from the "
           "Hand's point %s its mesh, and the game's ray through the pointer "
           "%s it. The Hand's point is %.1f cm of the room from its body, at "
           "%.0f%% of its height (fight_grip=1), frame %llu",
           IsOwnCreature(creature) ? "your Creature (a block)"
                                   : "the other Creature (an attack)",
           creature, ours != 0 ? "hits" : "misses",
           game != 0 ? "hits" : "misses",
           units > 0.0f ? gap / units * 100.0f : 0.0f,
           have ? 100.0f * fight_math::BodyLevel(body.base, body.height,
                                                 g_target)
                : 0.0f,
           g_frame);
  }
  g_fight_ray_frame = g_frame;
  g_fight_ray_hit = hit;
  if (ours != 0) return ours;
  // The ray of the Hand missed: the game's ray once more, so that the hit
  // record is the record of the game's ray.
  return game != 0 ? o_MeshRay(self, unused, a, b, c, d, e, f, g, h) : 0;
}

// creature_camera: 0 never moves the camera, 1 is the game's camera move, 2
// moves no camera while the controller placed the Hand on the last frame or
// while the table or the God view owns the view.
bool NoCreatureCamera() {
  if (g_creature_camera == 0) return true;
  if (g_creature_camera == 1) return false;
  return g_owns || TabletopOwnsView();
}

// NO CAMERA. For the hand mode 0x13 the game casts the object under the Hand
// to a Creature, writes it to +0x49B4 and +0x49B8, and pushes
// CameraModeCtrInteract once, guarded by the flag +0x49C0. With the flag set
// around the call, the game writes the Creature and pushes nothing. For any
// other mode it pops the camera mode when the flag is set, and on a press of
// less than 450 ms it clicks the leash (packet 0x5F). Here the flag is clear,
// so the mod clicks the leash itself, by the real time of the press.
void __fastcall h_InteractCamera(void* self, void* unused, int32_t mode) {
  uint8_t* hand = static_cast<uint8_t*>(self);
  if (hand == nullptr) {
    o_InteractCamera(self, unused, mode);
    return;
  }
  // A new CHand: a new game.
  if (g_virtual && hand != g_virtual_hand) g_virtual = false;
  int32_t* pushed = reinterpret_cast<int32_t*>(hand + kHandCameraPushed);
  if (mode == kModeCreature) {
    if (g_virtual || (*pushed == 0 && NoCreatureCamera())) {
      *pushed = 1;
      o_InteractCamera(self, unused, mode);
      *pushed = 0;
      if (!g_virtual) {
        void* creature =
            *reinterpret_cast<void* const*>(hand + kHandCreatureObject);
        const uint8_t* face = Interface();
        if (creature != nullptr && face != nullptr &&
            *reinterpret_cast<void* const*>(face + kFaceObject) == creature) {
          g_virtual = true;
          g_virtual_hand = hand;
          g_virtual_creature = creature;
          g_virtual_since = NowSeconds();
          ++g_virtual_count;
          if (g_touch_logs < kMaxTouchLogs) {
            ++g_touch_logs;
            ModLog("hand creature: a press on the Creature %p. The camera "
                   "does not move (creature_camera=%d), frame %llu",
                   creature, g_creature_camera, g_frame);
          }
        }
      }
      return;
    }
    o_InteractCamera(self, unused, mode);
    return;
  }
  if (g_virtual) {
    g_virtual = false;
    const double held = NowSeconds() - g_virtual_since;
    void* creature = g_virtual_creature;
    bool mine = false;
    if (creature != nullptr) {
      const void* player = reinterpret_cast<GetPlayerFn>(
          VTable(creature)[kCreatureGetPlayer / 4])(creature, nullptr);
      mine = player != nullptr && player == LocalPlayer();
    }
    const bool click = held < kLeashSeconds && mine;
    if (click) {
      void* game = *reinterpret_cast<void* const*>(kGame);
      if (game != nullptr) {
        reinterpret_cast<SetPacketFn>(kSetPacket)(game, nullptr,
                                                  kPacketLeash);
        ++g_leash_clicks;
        // The pulse comes when the game changed the leash.
        CreatureCommandLeashAsked();
      }
    }
    if (g_touch_logs < kMaxTouchLogs) {
      ++g_touch_logs;
      ModLog("hand creature: the press ends after %.0f ms (hand mode now "
             "0x%X). %s, frame %llu", held * 1000.0, mode,
             click ? "A short press on your Creature: the leash click "
                     "(packet 0x5F)"
                   : (mine ? "Not a short press, so no leash click"
                           : "Not your Creature, so no leash click"),
             g_frame);
    }
  }
  o_InteractCamera(self, unused, mode);
}

// Whether the Hand could take this object, by the tests of
// GInterface::PlaceObjectInMagicHand 0x005DA6F0.
bool Holdable(void* object, void* status) {
  if (object == nullptr || status == nullptr) return false;
  const uint8_t* bytes = static_cast<const uint8_t*>(object);
  if ((bytes[kObjectFlags] & 0x24) != 0 || (bytes[kObjectOtherFlags] & 0x10) != 0) {
    return false;
  }
  void** table = VTable(object);
  if (reinterpret_cast<VirtualIntFn>(table[kIsInteractable / 4])(
          object, nullptr) == 0) {
    return false;
  }
  if (reinterpret_cast<VirtualIntArgFn>(table[kValidForPlaceInHand / 4])(
          object, nullptr, status) != 1) {
    return false;
  }
  return reinterpret_cast<VirtualIntFn>(table[kIsCannotBePickedUp / 4])(
             object, nullptr) == 0;
}

// Whether a press of the Action button taps this object, by the tests of the
// game's tap (fn_005D38A0 and its handler fn_005DA650, see
// kInterfaceValidToTap). The draw does not offer an object with bit 4 of
// +0x24 to the pick, so such an object is out here too.
bool Tappable(void* object, void* status) {
  if (object == nullptr || status == nullptr) return false;
  const uint8_t* bytes = static_cast<const uint8_t*>(object);
  if ((bytes[kObjectFlags] & 0x04) != 0) return false;
  void** table = VTable(object);
  if (reinterpret_cast<VirtualIntFn>(table[kIsInteractable / 4])(
          object, nullptr) == 0) {
    return false;
  }
  if (reinterpret_cast<VirtualIntArgFn>(table[kInterfaceValidToTap / 4])(
          object, nullptr, status) != 1) {
    return false;
  }
  return reinterpret_cast<VirtualIntFn>(table[kIsCannotBePickedUp / 4])(
             object, nullptr) == 0;
}

// What Tappable says, as words for the log: which of its tests says no.
const char* TapWords(void* object, void* status) {
  if (object == nullptr || status == nullptr) return "does not tap it";
  const uint8_t* bytes = static_cast<const uint8_t*>(object);
  if ((bytes[kObjectFlags] & 0x04) != 0) {
    return "does not tap it (bit 0x04 of the byte at +0x24 is set)";
  }
  void** table = VTable(object);
  if (reinterpret_cast<VirtualIntFn>(table[kIsInteractable / 4])(
          object, nullptr) == 0) {
    return "does not tap it (the object is not available)";
  }
  if (reinterpret_cast<VirtualIntArgFn>(table[kInterfaceValidToTap / 4])(
          object, nullptr, status) != 1) {
    return "does not tap it (InterfaceValidToTap says no)";
  }
  if (reinterpret_cast<VirtualIntFn>(table[kIsCannotBePickedUp / 4])(
          object, nullptr) != 0) {
    return "does not tap it (the object cannot be picked)";
  }
  return "taps it";
}

// THE STATE OF A SCROLL IN PLAIN WORDS, the same in each line of the log. A
// scroll of a challenge is not active until its first click: a script of the
// game waits for the click and then makes it active (docs/village.md, "A scroll
// is not active until the first click"). silver: the type that taps with no
// active flag.
void ScrollWords(void* object, void* status, bool active, int script,
                 bool silver, bool click_on, char* out, size_t size) {
  const bool taps = Tappable(object, status);
  if (taps) {
    _snprintf_s(out, size, _TRUNCATE, "%s",
                active ? "The scroll is active: a press taps it"
                       : "A press taps the scroll");
  } else if (!active && script != 0 && !silver) {
    _snprintf_s(out, size, _TRUNCATE,
                "The scroll is not active: the game makes it active at the "
                "first click%s",
                click_on ? "" : ", and scroll_click=0 sends no click");
  } else {
    _snprintf_s(out, size, _TRUNCATE, "The game says that a press %s",
                TapWords(object, status));
  }
}

bool IsCreatureObject(void* object) {
  return object != nullptr &&
         reinterpret_cast<VirtualIntFn>(VTable(object)[kIsCreature / 4])(
             object, nullptr) != 0;
}

// WHAT A PRESS OF THE GRIP DOES to an object (near_math::Kind): the Hand
// picks it up, the game takes from it, the game taps it, or nothing. The
// Creature has its own kind: the game's pick and the touch of its body stay
// as they are (KeepCreature, h_MeshRay), and it only turns the far grab off.
// With the rule off (the A/B key) only the objects of the old rule count.
// With one target the Creature is a choice as each other object is, so its
// own kind comes first: the game says that the Creature is a bulk source
// (Creature::ValidForLockedSelectProcess), which is how its touch starts.
//
// A SCROLL THAT IS NOT ACTIVE YET (docs/village.md, "A scroll is not active
// until the first click"). The game does not tap such a scroll, but it counts a
// click on it: a short press with the scroll in the pick record calls
// fn_005D3930, which records the clicked object for the scripts before each
// test of a tap. The script of the challenge waits for that click
// (GAME_THING_CLICKED) and makes the scroll active after it. So a scroll that
// the game draws is a target of the grip also while a press does not tap it
// (ClickScroll).
bool IsScrollObject(void* object);
bool ClickScroll(void* object) {
  bool drawn = false;
  for (int i = 0; i < g_scroll_count; ++i) {
    if (g_scrolls[i].object == object) drawn = IsScrollObject(object);
  }
  return near_math::ScrollPress(
             false, drawn, g_one_target && g_scroll_draw && g_scroll_click) ==
         near_math::kScrollPressClick;
}

int PressKind(void* object, void* status) {
  if (g_one_target && IsCreatureObject(object)) return near_math::kCreature;
  if (Holdable(object, status)) return near_math::kHold;
  if (TakeValid(object, status)) return near_math::kTake;
  if (!g_near_rule) return near_math::kNone;
  if (IsCreatureObject(object)) return near_math::kCreature;
  if (Tappable(object, status)) return near_math::kTap;
  if (ClickScroll(object)) return near_math::kTap;
  return near_math::kNone;
}

// World units in a meter of the room, at the scale now: the table or the God
// view, or the life size scale.
float RoomUnits() {
  const float table = TabletopViewScale();
  if (table > 0.0f) return table;
  const float life = HeadPoseScale();
  return life > 0.1f ? life : 7.5f;
}

// The least distance of a far target from the Hand's point, to the object's
// point, in meters of the room: the cone's own 35 cm, or with the rule of the
// near objects the distance at which the far grab comes back.
float FarMinMeters() {
  return near_math::FarLeastMeters(g_near_rule,
                                   static_cast<float>(g_near_out_cm),
                                   controller_math::kFarMinHandM);
}

// The reach of the pick from the Hand's point, in world units: hand_pick_cm
// of the room. With the rule off it is 60 percent of the Hand's length (11.4
// cm of the room for a hand of 19 cm).
float PickReach() {
  if (!g_near_rule) return kGrabShare * kHandLength * g_scale;
  return near_math::CmToUnits(static_cast<float>(g_pick_cm), RoomUnits());
}

int __fastcall h_SendObjectDrawCollision(void* self, void* unused,
                                         void* object, float distance,
                                         void* mesh) {
  // ON THE TABLE, while the empty Hand has a holdable object in its grab
  // radius, the game's pick keeps that object. Another object under the pick
  // pixel is not offered to it. On the table the pixel pick can find a larger
  // object behind the villager, and then the hand mode goes from 9 (pick up)
  // to 0x12 (touch) and back each frame, so the grip does not pick up.
  const bool keep = g_near_pick && g_choice != nullptr && self == g_face &&
                    object != g_choice && HandEmpty();
  const int result =
      keep ? 0 : o_SendObjectDrawCollision(self, unused, object, distance,
                                           mesh);
  if (keep) ++g_kept_offers;
  ++g_offers;
  // The first eye pass only: the second eye can see an object the first one
  // does not.
  if (!g_active || g_eye != 0 || self != g_face || object == nullptr ||
      g_candidate_count >= g_candidate_limit) {
    return result;
  }
  if (*(static_cast<const uint8_t*>(self) + kFaceLocked) != 0) return result;
  const uint8_t* model = static_cast<const uint8_t*>(mesh);
  if (model == nullptr) {
    model = *reinterpret_cast<uint8_t* const*>(static_cast<uint8_t*>(object) +
                                               kObjectMesh);
  }
  if (model == nullptr) return result;
  if (!FirstOffer(object)) return result;
  Candidate& c = g_candidates[g_candidate_count++];
  if (g_candidate_count > g_most_candidates) {
    g_most_candidates = g_candidate_count;
  }
  c.object = object;
  c.model = model;
  const float* at = reinterpret_cast<const float*>(model + kMeshTranslation);
  for (int i = 0; i < 3; ++i) c.position[i] = at[i];
  return result;
}

// ON THE TABLE, after the game's own screen test: a visible object near the
// Hand is offered to the pick, as if the pick pixel were on it. The draw then
// calls SendObjectDrawCollision for it. With the distance 0, that asks the
// mesh whether the pick pixel hits it, so the game's own pick record gets no
// false hit. The hook above keeps the object as a candidate. Only in the
// first eye pass, where the candidates are kept.
int __fastcall h_ScreenTest(const void* bounds, void* unused,
                            const uint8_t* object) {
  const int result = o_ScreenTest(bounds, unused, object);
  if ((!g_near_pick && !g_far_offer) || g_eye != 0 || result == 0 ||
      object == nullptr) {
    return result;
  }
  int32_t* under = reinterpret_cast<int32_t*>(kPickUnder);
  if (*under != 0 || *reinterpret_cast<const int32_t*>(kPickInView) == 0) {
    return result;
  }
  // The object's world point. The test itself read the matrix, so it is
  // there.
  const float* at = reinterpret_cast<const float*>(object + kMeshTranslation);
  const float dx = at[0] - g_target[0], dy = at[1] - g_target[1],
              dz = at[2] - g_target[2];
  if (g_near_pick && dx * dx + dy * dy + dz * dz <= g_near_bound_sq) {
    *under = 1;
    ++g_near_offers;
    return result;
  }
  // THE LOCKED TARGET of a far grab goes to the pick on each frame, in the
  // cone or not, so the lock has its place while it moves.
  if (g_far_keep_model != nullptr && object == g_far_keep_model) {
    *under = 1;
    ++g_far_keep_offers;
    return result;
  }
  // THE CONE OF A FAR GRAB: an object whose point is inside the cone of the aim
  // ray, and not near the Hand, goes to the pick too. The distance 0 of the
  // offer asks the mesh whether the pick pixel hits it, so the game's own
  // pick gets no false hit. The score has the object's radius, which is not
  // known here, so the cone is a little wider than the selection.
  if (g_far_offer && g_far_ray) {
    // The least distance of a far target is in FarSelect, so the offer has
    // none: the score takes kFarMinHandM for a nearer object.
    float hand_m = sqrtf(dx * dx + dy * dy + dz * dz) / g_far_units;
    if (hand_m < controller_math::kFarMinHandM) {
      hand_m = controller_math::kFarMinHandM;
    }
    const controller_math::FarScore s = controller_math::FarConeScore(
        g_far_origin, g_far_direction, at, 0.0f, hand_m);
    if (s.valid && s.te <= g_far_offer_deg) {
      *under = 1;
      ++g_far_offers;
    }
  }
  return result;
}

bool Multiplayer();

// A GAME TURN NOW (docs/throw.md, "The release at once"). The game runs its
// packets once a game turn, 10 times a second. A release of the Hand is two
// packets (0x4D and the drop), and the end of a take is one (0x1C). So after
// the release action the object stayed in the Hand until the next turn, up to
// 100 ms, and it went on with the real hand.
//
// After an interface step that sends such packets, the next ask of the game
// for a turn gets a yes (h_DoATurn). The game counts that turn as each turn,
// and a turn is due at 100 ms times the count, so the turn after it comes at
// its own time. The game does not run faster. Never in a multiplayer game,
// where all players must run the same turns, and never while the game is
// paused.
const int kTurnNowFrames = 3;
void TurnNow() {
  if (!g_turn_on || !g_turn_hooked || !g_owns) return;
  g_turn_now = kTurnNowFrames;
  ++g_turns_asked;
}

int __fastcall h_DoATurn(void* game, void* unused) {
  const int due = o_DoATurn(game, unused);
  if (due != 0 || g_turn_now <= 0 || game == nullptr) return due;
  const uint32_t flags = *reinterpret_cast<const uint32_t*>(
      static_cast<const uint8_t*>(game) + kGameFlags);
  if ((flags & 4) != 0 || Multiplayer()) return due;
  g_turn_now = 0;
  ++g_turns_given;
  return 1;
}

// True when the interface step that starts now sends packets that the player
// waits for: the release action with an object in the Hand (the release bit
// and action 12), or the release step of a bulk take (the release bit while a
// take runs). A release of the Hand is kept for the log.
bool StepSendsPackets(uint8_t* face) {
  if (!g_owns || g_hand == nullptr || (face[kFaceFlags] & 0x40) == 0) {
    return false;
  }
  if (TakeObject(face) != nullptr) return true;
  void* held = *reinterpret_cast<void* const*>(g_hand + kHandHeld);
  if (held == nullptr || *reinterpret_cast<const int32_t*>(face + kFaceAction) !=
                             kReleaseActionIndex) {
    return false;
  }
  g_release_frame = g_frame;
  g_release_object = held;
  return true;
}

// THE A/B KEY (docs/throw.md, "The release at once"). With test_keys=1, the
// star key of the number pad turns the release at once off and on while the
// game runs: the game turn after a release, and the fast opening of the grip
// (HandDirectReleaseAtOnce). So the player can feel the old release and the
// new one in one session. The pointing controller gives a long pulse for on
// and a short pulse for off. The key is read only while a window of the game
// is in front, and the mod does not take it from the game. F11 and F12 have
// no free combination left (stereo.cpp, PollHotkeys), and Scroll Lock is the
// A/B key of the throw law. Ctrl must be up: Ctrl and the star key is the key
// of the flight of a camera in the Temple (temple.cpp).
void PollReleaseKey() {
  if (!g_release_key) return;
  const bool down = (GetAsyncKeyState(VK_MULTIPLY) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) == 0;
  const bool pressed = down && !g_release_key_down;
  g_release_key_down = down;
  if (!pressed) return;
  HWND window = GetForegroundWindow();
  DWORD process = 0;
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  g_turn_on = !g_turn_on;
  ControllersGesturePulse(0.5f, g_turn_on ? 0.30f : 0.08f);
  ModLog("hand throw   : the star key: the release at once is %s, frame "
         "%llu. %s", g_turn_on ? "ON" : "OFF", g_frame,
         g_turn_on ? "A game turn runs at once after a release, and the grip "
                     "opens on a fast fall"
                   : "A release waits for the next game turn, and the grip "
                     "opens only below 35%");
}

// THE A/B KEY of the pick at the glow (docs/grab.md, "One point for each
// interaction"). With test_keys=1, the point key of the number pad turns the
// rule off and on while the game runs. Off is the old rule: the pick
// reaches 60 percent of the Hand's length, an object that a press taps is no
// choice, a near object does not turn the far grab off, and a scroll is no far
// target. The pointing controller gives a long pulse for on and a short pulse
// for off. The key is read only while a window of the game is in front.
void SetRule(int rule) {
  g_rule = rule < 0 ? 0 : rule > 2 ? 2 : rule;
  g_near_rule = g_rule >= 1;
  g_one_target = g_rule >= 2;
}

void PollCatchKey() {
  if (!g_release_key) return;
  const bool down = (GetAsyncKeyState(VK_PRIOR) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_SHIFT) & 0x8000) == 0;
  const bool pressed = down && !g_catch_key_down;
  g_catch_key_down = down;
  if (!pressed) return;
  HWND window = GetForegroundWindow();
  DWORD process = 0;
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  g_catch_on = g_catch_on == near_math::kCatchOff ? near_math::kCatchOpen
                                                  : g_catch_on - 1;
  g_catch_logged = nullptr;
  g_wide_catch_logged = nullptr;
  ControllersGesturePulse(0.5f, g_catch_on == near_math::kCatchOpen   ? 0.45f
                                : g_catch_on == near_math::kCatchRest ? 0.30f
                                : g_catch_on == near_math::kCatchPick ? 0.15f
                                                                      : 0.08f);
  ModLog("hand pick    : Ctrl and Page Up: catch_moving is %d now, frame %llu. %s",
         g_catch_on, g_frame,
         g_catch_on == near_math::kCatchOpen
             ? "An object that moves has the reach that it has at rest and "
               "the extra reach. An object that fell from a still hand has "
               "no guard, and the guard of a throw ends when the grip is "
               "fully open"
         : g_catch_on == near_math::kCatchRest
             ? "An object that moves has the reach that it has at rest and "
               "the extra reach. An object that fell from a still hand has "
               "no guard, and a throw has the guard for the whole of "
               "catch_grace_ms"
         : g_catch_on == near_math::kCatchPick
             ? "An object that moves has the extra reach on the reach of the "
               "pick only, and each object that you let go has the guard"
             : "Each object has the same reach, moving or still");
}

// The test key Ctrl Shift Page Up: hand_wide_pick_cm goes between its value
// and 0 (the wide reach off). A long pulse for on and a short pulse for off.
void PollWideKey() {
  if (!g_release_key) return;
  const bool down = (GetAsyncKeyState(VK_PRIOR) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  const bool pressed = down && !g_wide_key_down;
  g_wide_key_down = down;
  if (!pressed) return;
  HWND window = GetForegroundWindow();
  DWORD process = 0;
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  static int saved = 0;
  if (g_wide_cm > 0) {
    saved = g_wide_cm;
    g_wide_cm = 0;
  } else {
    g_wide_cm = saved > 0 ? saved : near_math::kWideCm;
  }
  g_wide_logged = nullptr;
  ControllersGesturePulse(0.5f, g_wide_cm > 0 ? 0.30f : 0.08f);
  ModLog("hand pick    : Ctrl, Shift and Page Up: hand_wide_pick_cm is %d now, "
         "frame %llu. %s", g_wide_cm, g_frame,
         g_wide_cm > 0
             ? "A holdable object up to that gap is the target when nothing "
               "is nearer"
             : "The wide reach is off: a gap between the reach of the pick "
               "and the far grab has no target");
}

// THE A/B KEY OF THE CHOICE OF A FAR TARGET (test_keys=1): Ctrl with the Delete
// key steps far_grab_choice from 2 to 3 to 4 to 0 to 1 and back to 2
// (far_choice_math.h has the values). The pointing controller gives one short
// pulse for each unit of the value, and one long pulse for 0: a hand can count
// pulses, and it cannot tell five lengths apart. The key is read once a frame,
// only while a window of the game is in front, and the mod does not take it
// from the game.
const float kChoicePulseSeconds = 0.08f;
const float kChoiceLongPulseSeconds = 0.40f;
// From the start of a pulse to the start of the next one.
const ULONGLONG kChoicePulseStepMs = 250;
void PollChoiceKey() {
  if (!g_release_key) return;
  // Shift must be up: Ctrl, Shift and Delete is the key of god_zoom_about.
  const bool down = (GetAsyncKeyState(VK_DELETE) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_SHIFT) & 0x8000) == 0;
  const bool pressed = down && !g_choice_key_down;
  g_choice_key_down = down;
  const ULONGLONG now = GetTickCount64();
  if (pressed) {
    HWND window = GetForegroundWindow();
    DWORD process = 0;
    if (window != nullptr) GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId()) return;
    FarGrabSetChoice(FarGrabChoice() >= far_choice_math::kChoiceMost
                         ? 0
                         : FarGrabChoice() + 1);
    g_choice_pulses = FarGrabChoice();
    g_choice_pulse_tick = now;
    if (FarGrabChoice() == 0) {
      ControllersGesturePulse(0.5f, kChoiceLongPulseSeconds);
    }
    ModLog("far grab    : Ctrl and Delete: far_grab_choice is %d now, frame "
           "%llu. %s", FarGrabChoice(), g_frame,
           FarGrabChoiceWords(FarGrabChoice()));
  }
  if (g_choice_pulses > 0 && now >= g_choice_pulse_tick) {
    --g_choice_pulses;
    g_choice_pulse_tick = now + kChoicePulseStepMs;
    ControllersGesturePulse(0.5f, kChoicePulseSeconds);
  }
}

// THE A/B KEY OF THE TARGET THAT STAYS (test_keys=1): Ctrl with the Backspace
// key changes far_grab_stay between 1 and 0. The pointing controller gives a
// long pulse for 1 and a short pulse for 0. The default control map of the
// game (ControlMap::LoadDefaults 0x0046F890) has no entry with the code of
// Backspace. Read once a frame, only while a window of the game is in front.
void PollStayKey() {
  if (!g_release_key) return;
  const bool down = (GetAsyncKeyState(VK_BACK) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_SHIFT) & 0x8000) == 0;
  const bool pressed = down && !g_stay_key_down;
  g_stay_key_down = down;
  if (!pressed) return;
  HWND window = GetForegroundWindow();
  DWORD process = 0;
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  FarGrabSetStay(FarGrabStay() != 0 ? 0 : 1);
  ControllersGesturePulse(0.5f, FarGrabStay() != 0 ? 0.30f : 0.08f);
  ModLog("far grab    : Ctrl and Backspace: far_grab_stay is %d now, frame "
         "%llu. %s", FarGrabStay(), g_frame, FarGrabStayWords(FarGrabStay()));
}

// THE A/B KEY OF A TAKE WHOSE SOURCE GOES AWAY (test_keys=1): Shift with the
// Backspace key changes far_take_carry between 1 and 0. The pointing
// controller gives a long pulse for 1 and a short pulse for 0. Ctrl must be
// up. Read once a frame, only while a window of the game is in front.
void PollTakeCarryKey() {
  if (!g_release_key) return;
  const bool down = (GetAsyncKeyState(VK_BACK) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) == 0;
  const bool pressed = down && !g_take_carry_key_down;
  g_take_carry_key_down = down;
  if (!pressed) return;
  HWND window = GetForegroundWindow();
  DWORD process = 0;
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  FarGrabSetTakeCarry(FarGrabTakeCarry() != 0 ? 0 : 1);
  ControllersGesturePulse(0.5f, FarGrabTakeCarry() != 0 ? 0.30f : 0.08f);
  ModLog("far grab    : Shift and Backspace: far_take_carry is %d now, frame "
         "%llu. %s", FarGrabTakeCarry(), g_frame,
         FarGrabTakeCarryWords(FarGrabTakeCarry()));
}


void PollNearKey() {
  // The test key near_test_flips: the rule changes between 1 and 2 at each
  // frame of the list. A frame counts as a step of the script counts it
  // (fake_controller_play_frame).
  for (int i = 0; i < g_rule_flip_count; ++i) {
    if (ControllersTestFrame(g_rule_flips[i], g_frame) != g_rule_flips[i]) {
      continue;
    }
    SetRule(g_rule == 2 ? 1 : 2);
    ModLog("hand pick    : test key near_test_flips: near_rule is %d from "
           "frame %llu", g_rule, g_frame);
  }
  if (!g_release_key) return;
  // The point key of the number pad: alone it changes the rule of the choice,
  // and with Ctrl it changes scroll_draw. A long pulse for the scroll from
  // its draw and a short pulse for the old scroll.
  const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
  const bool point_down = (GetAsyncKeyState(VK_DECIMAL) & 0x8000) != 0;
  const bool down = point_down && !ctrl;
  const bool pressed = down && !g_near_key_down;
  g_near_key_down = down;
  const bool scroll_down = point_down && ctrl;
  const bool scroll_pressed = scroll_down && !g_scroll_key_down;
  g_scroll_key_down = scroll_down;
  // THE A/B KEY OF THE GLOW: the 7 key of the number pad turns the glow with
  // no target on and off. A long pulse for the new look (no glow with no
  // target) and a short pulse for the old glow.
  // With Ctrl the 7 key changes what a script of the game gets when it asks
  // about the camera: the player (a long pulse), or the game's camera.
  const bool seven_down = (GetAsyncKeyState(VK_NUMPAD7) & 0x8000) != 0;
  const bool glow_down = seven_down && !ctrl;
  const bool glow_pressed = glow_down && !g_glow_key_down;
  g_glow_key_down = glow_down;
  const bool script_down = seven_down && ctrl && g_script_hooked;
  const bool script_pressed = script_down && !g_script_key_down;
  g_script_key_down = script_down;
  // THE KEYS OF THE CREATURE: the 9 key of the number pad takes the next
  // speed of a slap (0.7, 1.0, 1.3 and 1.6 m/s), and the 3 key takes the next
  // hand of a stroke (by the size of the Creature, the flat hand, the index
  // finger). The pointing controller gives a pulse that is longer for each
  // next value.
  // With Ctrl the 9 key changes creature_palm_point: the point of the flat
  // hand at the palm (a long pulse), or at the tip of the index finger
  // (a short pulse).
  const bool nine_down = (GetAsyncKeyState(VK_NUMPAD9) & 0x8000) != 0;
  const bool slap_down = nine_down && !ctrl;
  const bool slap_pressed = slap_down && !g_slap_key_down;
  g_slap_key_down = slap_down;
  const bool palm_down = nine_down && ctrl;
  const bool palm_pressed = palm_down && !g_palm_key_down;
  g_palm_key_down = palm_down;
  // The 3 key alone takes the next hand of a stroke. With Ctrl it changes the
  // limits of the hand by the size of the Creature: the limits of the ini,
  // or the fixed limits (80 and 120 percent).
  const bool three_down = (GetAsyncKeyState(VK_NUMPAD3) & 0x8000) != 0;
  const bool stroke_down = three_down && !ctrl;
  const bool stroke_pressed = stroke_down && !g_stroke_key_down;
  g_stroke_key_down = stroke_down;
  const bool limit_down = three_down && ctrl;
  const bool limit_pressed = limit_down && !g_stroke_limit_key_down;
  g_stroke_limit_key_down = limit_down;
  if (!pressed && !glow_pressed && !slap_pressed && !stroke_pressed &&
      !scroll_pressed && !limit_pressed && !script_pressed &&
      !palm_pressed) {
    return;
  }
  HWND window = GetForegroundWindow();
  DWORD process = 0;
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  if (scroll_pressed) {
    g_scroll_draw = !g_scroll_draw;
    ControllersGesturePulse(0.5f, g_scroll_draw ? 0.30f : 0.08f);
    ModLog("hand scroll  : Ctrl and the point key: scroll_draw is %d, frame "
           "%llu. %s", g_scroll_draw ? 1 : 0, g_frame,
           g_scroll_draw ? "Each scroll that the game draws is a candidate, "
                           "with the reach of scroll_reach_cm"
                         : "A scroll is a candidate only when the game's pick "
                           "offers it, with the reach of each object");
  }
  if (script_pressed) {
    g_script_on = !g_script_on;
    ControllersGesturePulse(0.5f, g_script_on ? 0.30f : 0.08f);
    if (g_script_on) {
      ModLog("scripts      : Ctrl and the 7 key: a script that asks about the "
             "camera gets the player again (script_distance=%d, "
             "script_view=%d, script_camera_moves=%d), frame %llu",
             g_script_distance, g_script_view, g_script_moves, g_frame);
    } else {
      ModLog("scripts      : Ctrl and the 7 key: a script that asks about the "
             "camera gets the game's camera, as with script_distance=0, "
             "script_view=0 and script_camera_moves=0, frame %llu", g_frame);
    }
  }
  if (palm_pressed) {
    g_palm_point = !g_palm_point;
    ControllersGesturePulse(0.5f, g_palm_point ? 0.30f : 0.08f);
    ModLog("hand creature: Ctrl and the 9 key: creature_palm_point is %d, "
           "frame %llu. %s", g_palm_point ? 1 : 0, g_frame,
           g_palm_point ? "The point of the flat hand of a stroke is the palm"
                        : "The point of the flat hand of a stroke is the tip "
                          "of the index finger");
  }
  if (limit_pressed) {
    g_stroke_limits_before = !g_stroke_limits_before;
    float finger_below = 0.0f, flat_above = 0.0f;
    StrokeLimitsNow(&finger_below, &flat_above);
    ControllersGesturePulse(0.5f, g_stroke_limits_before ? 0.08f : 0.30f);
    ModLog("hand creature: Ctrl and the 3 key: the hand of a stroke is the "
           "finger below %.2f of the hand's length and the flat hand over "
           "%.2f, and a stroke starts with the flat hand from %.2f (%s), "
           "frame %llu", finger_below, flat_above,
           0.5f * (finger_below + flat_above),
           g_stroke_limits_before
               ? "the fixed limits of the test key"
               : "creature_finger_below_pct and creature_flat_above_pct",
           g_frame);
  }
  if (slap_pressed) {
    g_slap_key = (g_slap_key + 1) % 4;
    ControllersGesturePulse(0.5f, 0.08f * (g_slap_key + 1));
    ModLog("hand creature: the 9 key: a slap needs %.1f m/s now, and a hard "
           "slap %.2f m/s (creature_slap_mps is %.2f in the ini), frame %llu",
           SlapMps(), 1.8f * SlapMps(), ControllersCreatureSlapMps(),
           g_frame);
  }
  if (stroke_pressed) {
    g_stroke_mode = (g_stroke_mode + 1) % 4;
    ControllersGesturePulse(0.5f, 0.08f * (g_stroke_mode + 1));
    ModLog("hand creature: the 3 key: creature_stroke_hand is %d now: %s. A "
           "slap needs %.2f m/s, frame %llu", g_stroke_mode,
           g_stroke_mode == 0   ? "the hand of a stroke follows the size of "
                                  "the Creature"
           : g_stroke_mode == 1 ? "the flat hand"
           : g_stroke_mode == 2 ? "the index finger"
                                : "the fist stroke: a fist while the grip is "
                                  "closed, and the speed of the grip only",
           SlapMps(), g_frame);
  }
  if (glow_pressed) {
    g_glow_idle = !g_glow_idle;
    ControllersGesturePulse(0.5f, g_glow_idle ? 0.08f : 0.30f);
    ModLog("hand glow    : the 7 key: hand_glow_idle is %d, frame %llu. %s",
           g_glow_idle ? 1 : 0, g_frame,
           g_glow_idle ? "The glow shows on each frame, pale blue with no "
                         "target"
                       : "The glow shows only while a press of the grip has "
                         "a target at the Hand's point");
  }
  if (!pressed) return;
  // Between one target and the old rule. With near_rule=0 or
  // 1 in the ini, between that rule and one target.
  SetRule(g_rule == 2 ? (g_rule_ini < 2 ? g_rule_ini : 1) : 2);
  ControllersGesturePulse(0.5f, g_one_target ? 0.30f : 0.08f);
  ModLog("hand pick    : the point key: near_rule is %d, frame %llu. %s",
         g_rule, g_frame,
         g_one_target
             ? "One target: the pick record holds only the object with the "
               "halo, and the game's search at the palm is off"
             : g_near_rule
                   ? "The nearest body within the reach, the game's pick at "
                     "the pixel for an object that is not near, and the "
                     "game's search at the palm"
                   : "The pick has a reach of 60 percent of the Hand's "
                     "length");
}

// Once a frame: how long the Hand kept the object after the release action.
void LogRelease() {
  if (g_release_object == nullptr || g_hand == nullptr) return;
  void* held = *reinterpret_cast<void* const*>(g_hand + kHandHeld);
  const uint64_t frames = g_frame - g_release_frame;
  if (held == g_release_object && frames < 90) return;
  if (g_release_logs < kMaxReleaseLogs) {
    ++g_release_logs;
    if (held != g_release_object) {
      ModLog("hand throw   : the object left the Hand %llu frames after the "
             "release action, frame %llu", frames, g_frame);
    } else {
      ModLog("hand throw   : the Hand still holds the object %llu frames after "
             "the release action, so the game refused the drop, frame %llu",
             frames, g_frame);
    }
  }
  if (held != g_release_object) {
    g_release_frames_sum += frames;
    ++g_release_count;
    if (frames > g_release_worst) g_release_worst = frames;
  }
  g_release_object = nullptr;
}

// THE TAKE ENDS WHEN THE BUTTON GOES UP. While the game takes from a bulk
// source and the controller places the Hand, the source goes in the next pick
// record. The game's release step reads that record (see kTakeEndStep), so it
// then ends the take on the frame the button goes up, as it does for the
// mouse. Before each interface step, and after each eye pass. True when the
// record holds the source.
bool KeepTakeSource(uint8_t* face) {
  if (!g_owns || face == nullptr || face != g_face || g_take_no_keep) {
    return false;
  }
  void* source = TakeObject(face);
  if (source == nullptr) return false;
  void* next = *reinterpret_cast<void* const*>(face + kFaceNextObject);
  if (next == source) {
    ++g_take_keeps;
    return true;
  }
  // The distance of the record is half the land distance, so
  // UpdateInterfaceCollide keeps the object (see Inject, the far choice).
  float distance = 1.0f;
  const float land_distance =
      *reinterpret_cast<const float*>(face + kFaceNextLandDistance);
  if (land_distance > 0.02f && land_distance < 1e30f) {
    distance = 0.5f * land_distance;
  }
  reinterpret_cast<RecordPickFn>(kRecordPick)(face + kFaceNextRecord, nullptr,
                                              source, distance);
  ++g_take_puts;
  if (next != g_take_other && g_take_logs < kMaxTakeLogs) {
    ++g_take_logs;
    char name[48];
    strcpy_s(name, next != nullptr ? ClassName(next) : "nothing");
    ModLog("hand bulk    : the next pick record held %p (%s), not the source "
           "of the take %p (%s). The game's release step reads the record, so "
           "the source goes back in it, frame %llu", next, name, source,
           ClassName(source), g_frame);
  }
  g_take_other = next;
  return *reinterpret_cast<void* const*>(face + kFaceNextObject) == source;
}

// The grip machine asked for the end of a take that still runs with the
// button up (GripStepOut::end_take). This runs before the interface step,
// where the game itself ends a take. The game's states [V]:
//
//   action 4  The take runs. Its step FUN_005D4830 acts on the release bit:
//             it sets the pick lock and goes to action 6 with the end packet,
//             or to action 5 when the record does not hold the source.
//   action 5  The step FUN_005D49B0 sends the end packet when the record
//             holds the source, and goes to action 6.
//   action 6  The step FUN_005D4A10 clears the pick lock and resets the
//             action when the take does not hold the record's object.
//
// So with the source in the record (KeepTakeSource), actions 5 and 6 end the
// take without help. In action 4 the release bit did not come, so the step
// gets it now: the return value true asks the caller to set the bit for this
// one step. In another action the mod sets the pick lock and calls
// FUN_005D49B0. If the take still runs kTakeEndWait (200 ms) later, the handler
// of the end packet runs at once, and the mod clears the pick lock and resets
// the action as action 6 does. The handler never runs in a multiplayer game.
bool EndTakeNow(uint8_t* face) {
  if (g_take_end_asks <= 0) return false;
  g_take_end_asks = 0;
  if (!g_owns || face == nullptr || face != g_face || g_take_no_keep) {
    return false;
  }
  void* source = TakeObject(face);
  if (source == nullptr) {
    g_take_end_stage = 0;
    return false;
  }
  // One try of each kind, kTakeEndWait apart: the end packet needs a game
  // turn.
  if (g_take_end_stage != 0 &&
      NowSeconds() - g_take_end_time < kTakeEndWait) {
    return false;
  }
  // KeepTakeSource ran just before this, so the record holds the source
  // unless the game refused it.
  const bool kept =
      *reinterpret_cast<void* const*>(face + kFaceNextObject) == source;
  int32_t* action = reinterpret_cast<int32_t*>(face + kFaceAction);
  const int32_t before = *action;
  const bool direct = g_take_end_stage != 0 || !kept;
  bool release_bit = false;
  const char* how = "";
  if (!direct) {
    if (before == kTakeAction) {
      release_bit = true;
      how = "the release bit for the game's take step (FUN_005D4830)";
    } else if (before == kTakeWaitAction || before == kTakeEndAction) {
      how = "the source in the pick record, which the game's step reads";
    } else {
      *reinterpret_cast<uint32_t*>(face + kFaceLocked) = 0x10;
      reinterpret_cast<VirtualIntFn>(kTakeEndStep)(face, nullptr);
      how = "the pick lock and the game's end step (FUN_005D49B0)";
    }
    ++g_take_ends_forced;
  } else if (!Multiplayer()) {
    reinterpret_cast<VirtualIntArgFn>(kTakeEndPacket)(face, nullptr, source);
    if (before == kTakeAction || before == kTakeWaitAction ||
        before == kTakeEndAction) {
      *reinterpret_cast<uint32_t*>(face + kFaceLocked) = 0;
      reinterpret_cast<VirtualIntFn>(kResetActionState)(face, nullptr);
    }
    how = "the handler of the end packet (FUN_005DAA10)";
    ++g_take_ends_direct;
  } else {
    return false;
  }
  g_take_end_stage = direct ? 2 : 1;
  g_take_end_time = NowSeconds();
  if (g_take_logs < kMaxTakeLogs) {
    ++g_take_logs;
    ModLog("hand bulk    : %s, so the mod ends it with %s. The interface "
           "action was %d and is %d, and the take %s, frame %llu",
           g_take_end_test ? "the test key far_test_take_gone asks for the "
                             "end of the take with the grip closed"
                           : "the take still runs with the grip open",
           how, before, *action,
           TakeObject(face) != nullptr ? "waits for the game" : "is over",
           g_frame);
  }
  return release_bit;
}

// ON THE TABLE, before the interface step: the land point of the next record
// is the land under the Hand, where the Hand is on the model. The influence,
// the hand mode and the drop then follow the Hand. Only while the controller
// placed the Hand on the last frame, in the Normal or the Holding state. The
// release of a throw can use a point further back on the Hand's path.
void __fastcall h_InterfaceStep(void* self, void* unused) {
  // A FETCH FROM A DISTANCE: the land point of the next record is the land
  // under the far object, so the influence, the hand mode 9 and the pick up
  // of the game follow the object and not the Hand.
  void* far_object = nullptr;
  float far_at[3], far_land[2];
  if (g_owns && self != nullptr && self == g_face && g_hand != nullptr &&
      FarGrabFetching(&far_object, far_at, far_land)) {
    int32_t* land =
        reinterpret_cast<int32_t*>(static_cast<uint8_t*>(self) + kFaceNextLand);
    land[0] = static_cast<int32_t>(ClampToMap(far_land[0]) * kMapScale);
    land[1] = static_cast<int32_t>(ClampToMap(far_land[1]) * kMapScale);
    land[2] = 0;
    ++g_far_land_writes;
  } else if (g_table_hand && g_owns && self != nullptr && self == g_face &&
             g_hand != nullptr && DirectState(g_hand)) {
    float x = ClampToMap(g_target[0]), z = ClampToMap(g_target[2]);
    if (ThrowReleaseNow(g_face)) ThrowFrom(g_face, &x, &z);
    int32_t* land =
        reinterpret_cast<int32_t*>(static_cast<uint8_t*>(self) + kFaceNextLand);
    land[0] = static_cast<int32_t>(x * kMapScale);
    land[1] = static_cast<int32_t>(z * kMapScale);
    land[2] = 0;
    ++g_land_writes;
  }
  // THE BULK TAKE: the source of the take is in the next record when the
  // game's release step reads it, and a take that the grip must end ends
  // here.
  bool release_bit = false;
  uint8_t* face = static_cast<uint8_t*>(self);
  if (self != nullptr && self == g_face) {
    // ONE TARGET: the record is the choice when the step reads it.
    AssertRecord();
    // A MIRACLE IN THE HAND: a cancel or a tap of the mod runs here, and the
    // record holds the target of the miracle.
    MiracleInterfaceStep(self);
    // A SCAFFOLD IN THE HAND: the record holds the object that it joins.
    VillageKeepRecord();
    // MANY TREES FROM A FOREST: the take of the mod puts a tree in the
    // bundle of the Hand here (forest_take.cpp).
    ForestTakeInterfaceStep(self, g_hand, g_owns && !Multiplayer());
    KeepTakeSource(face);
    release_bit = EndTakeNow(face);
  }
  // A GAME TURN NOW. The step that starts now sends the packets of a release
  // or of the end of a take. The next game turn runs them.
  const bool sends = self != nullptr && self == g_face && StepSendsPackets(face);
  const uint8_t flags = release_bit ? face[kFaceFlags] : 0;
  if (release_bit) face[kFaceFlags] |= 0x40;
  o_InterfaceStep(self, unused);
  if (release_bit) {
    face[kFaceFlags] = static_cast<uint8_t>((face[kFaceFlags] & ~0x40) |
                                            (flags & 0x40));
  }
  if (sends || release_bit) TurnNow();
}

// THE BODY of an object for the pick: an upright cylinder from the object's
// point, with the game's 2D radius and the game's height (near_math.h). With
// the rule off the height is two radii.
// AN OBJECT WITH NO MESH (temple.cpp: the door of the Temple and a leash
// post). The game offers such an object to its own pick as a ball.
// HandDirectOffer makes it a candidate, and its body is that ball about the
// place of the candidate.
struct BallBody {
  void* object;
  float radius;
};
const int kMaxBalls = 8;
BallBody g_balls[kMaxBalls];
int g_ball_count = 0;

bool BallOf(void* object, float* radius) {
  for (int i = 0; i < g_ball_count; ++i) {
    if (g_balls[i].object == object) {
      *radius = g_balls[i].radius;
      return true;
    }
  }
  return false;
}

struct Body {
  float radius;
  float height;
};

Body BodyOf(void* object) {
  Body body;
  if (BallOf(object, &body.radius)) {
    body.height = 2.0f * body.radius;
    return body;
  }
  body.radius = reinterpret_cast<VirtualFloatFn>(
      VTable(object)[kGet2DRadius / 4])(object, nullptr);
  if (!(body.radius >= 0.0f && body.radius < 1000.0f)) body.radius = 0.0f;
  body.height = 2.0f * body.radius;
  if (g_near_rule) {
    const float height = reinterpret_cast<VirtualFloatFn>(
        VTable(object)[kGetHeight / 4])(object, nullptr);
    if (height > 0.0f && height < 2000.0f) body.height = height;
  }
  return body;
}

// The middle of the box of a candidate's mesh in the world, as
// ScriptHighlight::Draw makes it (see kMeshBox). The rows of the matrix can
// hold the scale, so each row counts as a direction, and the scale is the
// game's height over the height of the box. False with no box.
typedef const uint8_t*(__fastcall* MeshBoxFn)(const void* self, void* unused);
bool BoxMiddle(const Candidate& c, const Body& body, float out[3]) {
  if (c.model == nullptr) return false;
  void** table = *reinterpret_cast<void** const*>(c.model);
  const uint8_t* box =
      reinterpret_cast<MeshBoxFn>(table[kMeshBox / 4])(c.model, nullptr);
  if (box == nullptr) return false;
  const float* middle = reinterpret_cast<const float*>(box + kBoxMiddle);
  const float* half = reinterpret_cast<const float*>(box + kBoxHalf);
  if (!(half[1] > 1e-4f) || !(body.height > 0.0f)) return false;
  const float scale = body.height / (2.0f * half[1]);
  if (!(scale > 1e-4f && scale < 1e4f)) return false;
  const float* rows = reinterpret_cast<const float*>(c.model + kMeshRows);
  for (int k = 0; k < 3; ++k) out[k] = c.position[k];
  for (int j = 0; j < 3; ++j) {
    const float* row = rows + j * 3;
    const float length =
        sqrtf(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
    if (!(length > 1e-4f) || !(middle[j] == middle[j])) return false;
    for (int k = 0; k < 3; ++k) {
      out[k] += scale * middle[j] * row[k] / length;
    }
  }
  return true;
}

// The gap from the Hand's point to the body, and the distance to its middle.
// low is for an object that a press taps: a scroll floats, and its point need
// not be the base of its mesh, so its body also goes half its height below
// the point.
near_math::Gap GapTo(const Body& body, const float position[3], bool low) {
  float base[3] = {position[0], position[1], position[2]};
  float height = body.height;
  if (low) {
    base[1] -= 0.5f * body.height;
    height += 0.5f * body.height;
  }
  near_math::Gap gap = near_math::BodyGap(g_target, base, body.radius, height);
  // With the rule off the kind breaks a tie.
  if (!g_near_rule) gap.middle = 0.0f;
  return gap;
}

// Turn a ray of the game (camera, direction, length) to a point of the
// world. The origin and the length stay. False when the point is the camera.
bool AimAt(const float point[3], float* out) {
  const float* camera = Camera();
  float offset[3], to[3];
  for (int i = 0; i < 3; ++i) {
    offset[i] = out[i] - camera[i];
    to[i] = point[i] - camera[i];
  }
  const float length = sqrtf(offset[0] * offset[0] + offset[1] * offset[1] +
                             offset[2] * offset[2]);
  const float span = sqrtf(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
  if (!(length > 1e-4f) || !(span > 1e-4f)) return false;
  for (int i = 0; i < 3; ++i) out[i] = camera[i] + to[i] / span * length;
  return true;
}

bool HandEmpty() {
  return *reinterpret_cast<void* const*>(g_hand + kHandHeld) == nullptr;
}

// ONE TARGET (near_rule=2): THE BODIES. Each kind of object has the body that
// the player sees (near_math.h).
bool IsForest(void* object);

// The ball of a scroll: the middle of the box of its mesh in the world, and
// the half diagonal of the box, through the matrix of the mesh as it is. The
// game draws a scroll smaller when the camera is near it, and the matrix
// holds that scale (ScriptHighlight::Draw, 0x00709E9C to 0x00709FC0). False
// with no box.
bool BoxBall(const Candidate& c, float center[3], float* radius) {
  if (c.model == nullptr) return false;
  void** table = *reinterpret_cast<void** const*>(c.model);
  const uint8_t* box =
      reinterpret_cast<MeshBoxFn>(table[kMeshBox / 4])(c.model, nullptr);
  if (box == nullptr) return false;
  const float* middle = reinterpret_cast<const float*>(box + kBoxMiddle);
  const float* half = reinterpret_cast<const float*>(box + kBoxHalf);
  const float* rows = reinterpret_cast<const float*>(c.model + kMeshRows);
  float reach[3] = {0.0f, 0.0f, 0.0f};
  for (int k = 0; k < 3; ++k) center[k] = c.position[k];
  for (int j = 0; j < 3; ++j) {
    for (int k = 0; k < 3; ++k) {
      center[k] += middle[j] * rows[j * 3 + k];
      reach[k] += half[j] * rows[j * 3 + k];
    }
  }
  *radius = sqrtf(reach[0] * reach[0] + reach[1] * reach[1] +
                  reach[2] * reach[2]);
  for (int k = 0; k < 3; ++k) {
    if (!(center[k] > -100000.0f && center[k] < 100000.0f)) return false;
  }
  return *radius > 0.01f && *radius < 500.0f;
}

bool IsScrollObject(void* object) {
  return reinterpret_cast<VirtualIntFn>(
             VTable(object)[kIsScriptHighlight / 4])(object, nullptr) != 0;
}

// THE SCROLL FROM ITS DRAW (see kScrollDraw, and docs/village.md, "How the game
// draws a scroll"). The body of a scroll is what its own draw made in this
// frame: after the draw of the first eye pass, the mesh holds the matrix that
// the draw used, and the ball of the box through that matrix is the scroll in
// the picture. Each scroll that the game drew is a candidate of the choice. The
// game's pick does not have to offer it: that depends on the pick pixel, on the
// lock of the pick and on the distance of the mesh point from the Hand.
typedef void(__fastcall* ScrollDrawFn)(void* self, void* unused);
typedef const uint8_t*(__fastcall* GameCameraFn)(void* game, void* unused);
ScrollDrawFn o_ScrollDraw = nullptr;

int ScrollType(const void* scroll) {
  const uintptr_t info = *reinterpret_cast<const uintptr_t*>(
      static_cast<const uint8_t*>(scroll) + kObjectInfo);
  if (info < kScrollInfos) return -1;
  const uintptr_t index = (info - kScrollInfos) / kScrollInfoSize;
  return index < 10 ? static_cast<int>(index) : -1;
}

// The names of the game's own list (HIGHLIGHT_INFO). The look can differ:
// the scrolls of the challenges of Land 1 are type 2 and look white.
const char* ScrollTypeName(int type) {
  static const char* const kNames[10] = {
      "type 0, bronze", "type 1, silver", "type 2, gold",
      "type 3, a scoreboard", "type 4, a large scoreboard",
      "type 5, silver cancel", "type 6, gold cancel",
      "type 7, swap Creature", "type 8, fight Creature",
      "type 9, brotherhood"};
  return type >= 0 && type < 10 ? kNames[type] : "of no known type";
}

// EACH CHANGE OF THE ACTIVE FLAG of a scroll goes to the log, with the two
// cameras: a press taps a scroll of a challenge only while it is active, and
// a script of the game makes it active.
struct ScrollSeen {
  void* object;
  int active;
};
const int kMaxScrollSeen = 96;
ScrollSeen g_scroll_seen[kMaxScrollSeen];
int g_scroll_seen_count = 0;
int g_scroll_active_logs = 0;
const int kMaxScrollActiveLogs = 80;

// Whether ScriptHighlight::Draw draws this scroll now (its own three tests).
bool ScrollIsDrawn(const void* scroll, int type) {
  const uint8_t* bytes = static_cast<const uint8_t*>(scroll);
  if (*reinterpret_cast<const int32_t*>(bytes + kScrollHidden) != 0) {
    return false;
  }
  if (*reinterpret_cast<uint8_t* const*>(bytes + kObjectMesh) == nullptr) {
    return false;
  }
  if (type == kScrollSilver) return true;
  const uint8_t* game = *reinterpret_cast<uint8_t* const*>(kGame);
  if (game == nullptr) return false;
  const uint8_t* gate =
      *reinterpret_cast<uint8_t* const*>(game + kGameScrollGate);
  return gate != nullptr && !IsBadReadPtr(gate + 0x80, 4) &&
         *reinterpret_cast<const int32_t*>(gate + 0x80) != 0;
}

// The ball of a mesh as it stands now: the middle of its box through its
// matrix, and the half diagonal. False with no box.
bool MeshBall(const uint8_t* model, float center[3], float* radius) {
  Candidate c = {};
  c.model = model;
  const float* at = reinterpret_cast<const float*>(model + kMeshTranslation);
  for (int k = 0; k < 3; ++k) c.position[k] = at[k];
  return BoxBall(c, center, radius);
}

// The distance from the game's own camera to a point, which decides the size
// of a scroll. -1 when the camera is not known.
float GameCameraDistance(const float point[3]) {
  void* game = *reinterpret_cast<void* const*>(kGame);
  if (game == nullptr) return -1.0f;
  const uint8_t* camera =
      reinterpret_cast<GameCameraFn>(kGetGameCamera)(game, nullptr);
  if (camera == nullptr || IsBadReadPtr(camera + kGameCameraPoint, 0x64)) {
    return -1.0f;
  }
  const float* x = reinterpret_cast<const float*>(camera + kGameCameraPoint);
  const float at[3] = {x[0], x[12], x[24]};  // 0x30 apart
  return Distance(at, point);
}

void __fastcall h_ScrollDraw(void* self, void* unused) {
  o_ScrollDraw(self, unused);
  if (self == nullptr || !g_active || g_face == nullptr || g_eye < 0 ||
      g_eye > 1) {
    return;
  }
  const int type = ScrollType(self);
  if (!ScrollIsDrawn(self, type)) return;
  const uint8_t* bytes = static_cast<const uint8_t*>(self);
  const uint8_t* model = *reinterpret_cast<uint8_t* const*>(bytes + kObjectMesh);
  float center[3], radius = 0.0f;
  if (!MeshBall(model, center, &radius)) return;
  int index = -1;
  for (int i = 0; i < g_scroll_count; ++i) {
    if (g_scrolls[i].object == self) index = i;
  }
  if (g_eye != 0) {
    // The second eye pass: what its draw made, for the report.
    if (index < 0) return;
    Scroll& s = g_scrolls[index];
    for (int k = 0; k < 3; ++k) s.other[k] = center[k];
    s.other_radius = radius;
    s.second = true;
    const float apart = Distance(s.center, s.other);
    const float wider = fabsf(s.radius - s.other_radius);
    if (apart > g_scroll_eye_middle) g_scroll_eye_middle = apart;
    if (wider > g_scroll_eye_radius) g_scroll_eye_radius = wider;
    return;
  }
  if (index < 0) {
    if (g_scroll_count >= kMaxScrolls) return;
    index = g_scroll_count++;
    if (g_scroll_count > g_scroll_most) g_scroll_most = g_scroll_count;
  }
  Scroll& s = g_scrolls[index];
  s.object = self;
  s.model = model;
  const float* at = reinterpret_cast<const float*>(model + kMeshTranslation);
  for (int k = 0; k < 3; ++k) {
    s.point[k] = at[k];
    s.center[k] = center[k];
  }
  s.radius = radius;
  s.second = false;
  s.type = type;
  s.active = *reinterpret_cast<const int32_t*>(bytes + kScrollActive) != 0;
  s.script = *reinterpret_cast<const int32_t*>(bytes + kScrollScript);
  s.camera = GameCameraDistance(at);
  {
    int seen = -1;
    for (int i = 0; i < g_scroll_seen_count; ++i) {
      if (g_scroll_seen[i].object == self) seen = i;
    }
    if (seen < 0 && g_scroll_seen_count < kMaxScrollSeen) {
      seen = g_scroll_seen_count++;
      g_scroll_seen[seen].object = self;
      g_scroll_seen[seen].active = s.active ? 1 : 0;
    } else if (seen >= 0 && g_scroll_seen[seen].active != (s.active ? 1 : 0)) {
      g_scroll_seen[seen].active = s.active ? 1 : 0;
      if (g_scroll_active_logs < kMaxScrollActiveLogs) {
        ++g_scroll_active_logs;
        ModLog("hand scroll  : the scroll %p (%s, script %d) at %.1f %.1f "
               "%.1f is %s. The game's camera is %.1f units from it, the "
               "camera of the eye %.1f, and the Hand's point %.1f, frame %llu",
               self, ScrollTypeName(type), s.script, center[0], center[1],
               center[2],
               s.active ? "active now: a press taps it"
                        : "not active now: the game makes it active at the "
                          "first click",
               s.camera, Distance(Camera(), center),
               Distance(g_target, center), g_frame);
      }
    }
  }
  // THE CANDIDATE. The same tests as an offer of the pick has
  // (h_SendObjectDrawCollision), and the scroll goes in once. Each offer of
  // the pick for this scroll in this eye pass came in the draw above.
  s.list = kScrollFromPick;
  if (*(g_face + kFaceLocked) != 0) {
    s.list = kScrollLocked;
    return;
  }
  if (g_candidate_count >= g_candidate_limit) {
    s.list = kScrollNotListed;
    return;
  }
  if (!FirstOffer(self)) return;
  ++g_scroll_added;
  if (!g_scroll_draw) {
    s.list = kScrollNotListed;
    return;
  }
  Candidate& c = g_candidates[g_candidate_count++];
  if (g_candidate_count > g_most_candidates) {
    g_most_candidates = g_candidate_count;
  }
  c.object = self;
  c.model = model;
  for (int k = 0; k < 3; ++k) c.position[k] = at[k];
  s.list = kScrollFromDraw;
}

bool IsTreeObject(void* object) {
  return reinterpret_cast<VirtualIntFn>(VTable(object)[kIsAnyKindOfTree / 4])(
             object, nullptr) != 0;
}

// The capsule of the Creature for the choice: through the body bone, from the
// land up to 1.1 S, with the 2D radius. The touch of the body in the Creature
// state (TouchFrame) has the same capsule and creature_touch_cm of the room
// more. The choice does not add that. With it the capsule of a small Creature
// can have a radius of 6 cm at 100 units per meter, and by the rule of
// the choice such a Creature takes the press from a tree that the Hand's
// point is in. False when the mod does not know where the body is
// (BodyPlace).
bool CreatureBody(void* object, controller_math::Capsule* body) {
  uint8_t* model = CreatureModel(object);
  float bone[3];
  if (BodyPlace(object, bone) == 0) return false;
  const float size = *reinterpret_cast<const float*>(model + kCreatureSize);
  if (!(size > 0.0f && size < 1000.0f)) return false;
  const int32_t coords[3] = {static_cast<int32_t>(bone[0] * kMapScale),
                             static_cast<int32_t>(bone[2] * kMapScale), 0};
  const float land = reinterpret_cast<GetAltitudeFn>(kGetAltitude)(coords);
  float radius = Radius2D(object);
  if (!(radius > 0.0f)) radius = 0.35f * 15.0f * size;
  body->base[0] = bone[0];
  body->base[1] = land;
  body->base[2] = bone[2];
  body->height = 1.1f * 15.0f * size;
  body->radius = radius;
  return land == land;
}

// A candidate with its body: the gap from the Hand's point, the size, and the
// middle, the radius and half the height for the halo.
struct Measured {
  near_math::Target target;
  float center[3];
  float radius;
  float half;
};

void Measure(const Candidate& c, int kind, Measured* out) {
  const Body body = BodyOf(c.object);
  out->target.kind = kind;
  out->target.scroll = false;
  out->target.outer = false;
  out->radius = body.radius;
  out->half = 0.5f * body.height;
  out->center[0] = c.position[0];
  out->center[1] = c.position[1] + 0.5f * body.height;
  out->center[2] = c.position[2];
  controller_math::Capsule capsule;
  float ball[3], ball_radius = 0.0f;
  if (kind == near_math::kCreature && CreatureBody(c.object, &capsule)) {
    out->radius = capsule.radius;
    out->half = 0.5f * capsule.height;
    out->center[0] = capsule.base[0];
    out->center[1] = capsule.base[1] + out->half;
    out->center[2] = capsule.base[2];
    const float gap = controller_math::CapsuleGap(capsule, g_target);
    out->target.gap.gap = gap > 0.0f ? gap : 0.0f;
    out->target.gap.middle = Distance(g_target, out->center);
  } else if (kind == near_math::kTap && BallOf(c.object, &ball_radius)) {
    // The door of the Temple or a leash post: the ball of HandDirectOffer.
    out->radius = ball_radius;
    out->half = ball_radius;
    for (int k = 0; k < 3; ++k) out->center[k] = c.position[k];
    out->target.gap = near_math::BallGap(g_target, c.position, ball_radius);
  } else if (kind == near_math::kTap && IsScrollObject(c.object) &&
             BoxBall(c, ball, &ball_radius)) {
    out->target.scroll = true;
    out->radius = ball_radius;
    out->half = ball_radius;
    for (int k = 0; k < 3; ++k) out->center[k] = ball[k];
    out->target.gap = near_math::BallGap(g_target, ball, ball_radius);
  } else if (kind == near_math::kTap) {
    // A house, and each other object that a press taps: the two cylinders of
    // the old rule. The nearer one counts.
    out->target.gap = GapTo(body, c.position, true);
    out->center[1] = c.position[1] + 0.25f * body.height;
    out->half = 0.75f * body.height;
    float middle[3];
    if (BoxMiddle(c, body, middle)) {
      const float base[3] = {middle[0], middle[1] - 0.5f * body.height,
                             middle[2]};
      const near_math::Gap boxed =
          near_math::BodyGap(g_target, base, body.radius, body.height);
      if (boxed.gap < out->target.gap.gap ||
          (boxed.gap == out->target.gap.gap &&
           boxed.middle < out->target.gap.middle)) {
        out->target.gap = boxed;
        for (int k = 0; k < 3; ++k) out->center[k] = middle[k];
        out->half = 0.5f * body.height;
      }
    }
  } else if (IsTreeObject(c.object) && !IsForest(c.object)) {
    out->target.gap =
        near_math::TreeGap(g_target, c.position, body.radius, body.height);
  } else {
    out->target.gap = GapTo(body, c.position, false);
  }
  out->target.size = out->radius > out->half ? out->radius : out->half;
  // A loose body: large, and wider than what the player sees (near_math.h,
  // THE TARGET OF A PRESS).
  out->target.loose = !out->target.scroll && kind != near_math::kCreature &&
                      (kind == near_math::kTap || kind == near_math::kTake ||
                       IsTreeObject(c.object) || IsForest(c.object));
}

// Whether a press of the grip acts on the object now. The game acts only
// when the land point of the pick record is in the player's influence, or
// when the object does not need the influence (fn_005D7F20 at 0x005D7F97,
// FUN_005D1330 at 0x005D13E6 and fn_005D38A0). The land point of the record
// is the land under the Hand's point (h_InterfaceStep).
bool PressActs(void* object) {
  if (object == nullptr || g_face == nullptr) return false;
  if (reinterpret_cast<VirtualIntFn>(VTable(object)[kMustBeInInfluence / 4])(
          object, nullptr) == 0) {
    return true;
  }
  return InsideInfluence(g_face, ClampToMap(g_target[0]),
                         ClampToMap(g_target[2]));
}

const char* KindName(int kind) {
  switch (kind) {
    case near_math::kHold: return "the grip picks it up";
    case near_math::kTake: return "the grip takes from it";
    case near_math::kTap: return "the grip taps it";
    case near_math::kCreature: return "the grip touches the Creature";
    default: return "a press does nothing";
  }
}

// After the first eye pass: the object of a press at the Hand's point.
//
// ONE POINT (docs/grab.md, "One point for each interaction"). Each candidate
// that a press of the grip acts on is measured from the Hand's point, where
// the glow is: an object that the Hand can hold, a bulk source, and an object
// that the game taps (a scroll, a house). The nearest body inside the reach of
// the pick is the choice (near_math::Beats). The reach is hand_pick_cm of the
// room. A reach of 60 percent of the Hand's length, 11.4 cm, would be a pick at
// the palm to the player, not at the glow.
//
// THE BULK TAKE. A pile of a store, a field or a fish farm cannot be held,
// but the grip takes from it. The game starts the take from the object of the
// next record (+0x400, FUN_005D1330 at 0x005D13BB). With only holdable
// objects here, a villager near a store pile went in the record, and the
// table hook kept the pile from the game's own pick, so the grip on a pile
// did nothing. So a bulk source counts too.
//
// A SCROLL. The game taps an object only through its own pick at the pointer
// pixel. While a tree was the choice, the table hook kept the scroll from that
// pick, and the grip took the tree. So an object that a press taps counts
// too, and the nearest object wins.
//
// NEAR OBJECTS WIN. The gap of the nearest of these objects, the Creature
// too, turns the grab from a distance off (near_math::NearBlocks).
// THE SPEED OF AN OBJECT, from its place in the draw of this frame and the
// place in the draw of an earlier frame. A table of 512 slots by the address of
// the object. The speed is worked out once a frame. An object that was not
// offered in the last 3 frames has no speed (0).
struct SpeedSlot {
  void* object;
  float position[3];
  double time;
  uint64_t frame;
  float speed;
};
const int kSpeedSlots = 512;
SpeedSlot g_speeds[kSpeedSlots];

float ObjectSpeedNow(const Candidate& c) {
  SpeedSlot& slot =
      g_speeds[(reinterpret_cast<uintptr_t>(c.object) >> 4) % kSpeedSlots];
  if (slot.object == c.object && slot.frame == g_frame) return slot.speed;
  const double now = NowSeconds();
  float speed = 0.0f;
  if (slot.object == c.object && g_frame > slot.frame &&
      g_frame - slot.frame <= 3) {
    speed = near_math::ObjectSpeed(slot.position, c.position,
                                   static_cast<float>(now - slot.time));
  }
  slot.object = c.object;
  for (int k = 0; k < 3; ++k) slot.position[k] = c.position[k];
  slot.time = now;
  slot.frame = g_frame;
  slot.speed = speed;
  return speed;
}

// The extra reach of the pick for a candidate, in world units: for an object
// that the Hand can hold and that moves, the extra reach of
// near_math::MovingExtra. The object that the Hand threw a moment ago
// (catch_grace_ms, near_math::GuardHolds) gets none, so a throw does not come
// back in the same closing of the grip.
float CatchExtra(const Candidate& c, int kind, float units) {
  if (g_catch_on == near_math::kCatchOff || kind != near_math::kHold) {
    return 0.0f;
  }
  if (c.object == g_last_thrown &&
      near_math::GuardHolds(NowSeconds() - g_last_thrown_time,
                            g_catch_grace_ms * 0.001, g_last_thrown_fell,
                            g_catch_on, g_grip_reopened)) {
    return 0.0f;
  }
  const float cmps = ObjectSpeedNow(c) / units * 100.0f;
  const float extra_cm = near_math::MovingExtra(
      cmps, static_cast<float>(near_math::kCatchStillCmps),
      g_catch_lag_ms * 0.001f, static_cast<float>(g_catch_cap_cm));
  return near_math::CmToUnits(extra_cm, units);
}

void Choose() {
  g_choice = nullptr;
  g_choice_take = false;
  g_choice_tap = false;
  g_choice_click = false;
  g_choice_kind = near_math::kNone;
  g_choice_acts = false;
  g_choice_touched = false;
  g_near_object = nullptr;
  g_near_gap = 0.0f;
  g_any_object = nullptr;
  g_any_count = 0;
  g_scroll_near = -1;
  const bool was_blocked = g_near_blocked;
  g_near_blocked = false;
  g_choice_wide = false;
  g_choice_last = false;
  g_choice_loose = false;
  if (!g_active || g_face == nullptr || !HandEmpty()) return;
  // In the Creature state the pick keeps the Creature (KeepCreature).
  if (State(g_hand) == kStateCreature) return;
  void* status = *reinterpret_cast<void* const*>(g_face + kFaceStatus);
  const float units = RoomUnits();
  const float reach = PickReach();
  const float near_in =
      near_math::CmToUnits(static_cast<float>(g_near_in_cm), units);
  const float near_out =
      near_math::CmToUnits(static_cast<float>(g_near_out_cm), units);
  const float limit =
      (g_near_rule && near_out > reach ? near_out : reach) +
      (g_near_rule && g_wide_cm > g_near_out_cm
           ? near_math::CmToUnits(static_cast<float>(g_wide_cm - g_near_out_cm), units)
           : 0.0f) +
      (g_catch_on != 0 ? near_math::CmToUnits(
                             static_cast<float>(g_catch_cap_cm), units)
                       : 0.0f);
  const float eps = near_math::CmToUnits(0.1f, units);
  const float touch =
      near_math::CmToUnits(static_cast<float>(g_touch_cm), units);
  // A scroll has a longer reach (near_math::ScrollReach). With scroll_draw=0
  // it has the reach of each object.
  const float scroll_reach =
      g_scroll_draw ? near_math::ScrollReach(
                          reach, near_math::CmToUnits(
                                     static_cast<float>(g_scroll_reach_cm),
                                     units))
                    : reach;
  near_math::Gap best = {reach, 1e30f};
  int best_kind = near_math::kNone;
  float nearest = 1e30f;
  // The gap of the nearest body that turns the grab from a distance off
  // (near_math::StopsFar): the gap that NearBlocks reads.
  float block_gap = 1e30f;
  // The nearest loose body that the point is not in, for the log.
  float loose_gap = 1e30f;
  void* loose_object = nullptr;
  float creature_gap = 1e30f;
  Measured chosen = {};
  float chosen_reach = reach, chosen_speed = 0.0f;
  // THE WIDE PICK (near_math::WidePick): the nearest holdable object past the
  // reach of the pick and inside the wide reach.
  const float wide_reach =
      g_near_rule ? near_math::CmToUnits(static_cast<float>(g_wide_cm), units)
                  : 0.0f;
  void* wide_object = nullptr;
  int wide_kind = near_math::kHold;
  float wide_position[3] = {0, 0, 0};
  near_math::Gap wide_gap = {1e30f, 1e30f};
  Measured wide_measured = {};
  // The wide reach of that object: with catch_moving=2 or 3 the extra reach
  // of an object that moves is on it (near_math::WideMoving).
  float wide_own = wide_reach, wide_speed = 0.0f;
  // The nearest object that a press acts on, past the first cut too, for the
  // line of a press with no target.
  g_any_object = nullptr;
  g_any_gap = 1e30f;
  g_any_kind = near_math::kNone;
  for (int i = 0; i < g_candidate_count; ++i) {
    const Candidate& c = g_candidates[i];
    const Body body = BodyOf(c.object);
    near_math::Gap gap = GapTo(body, c.position, false);
    // The body of a tap is larger, and it can stand about the middle of its
    // box and not about its point. So this first cut is wider by three
    // heights and a radius. It only saves the tests of PressKind.
    if (gap.gap > limit + 3.0f * body.height + body.radius) {
      // Past the cut the cylinder from the point of the object is the
      // measure, and the tests of a press run only for a new nearest object.
      if (gap.gap < g_any_gap) {
        const int far_kind = PressKind(c.object, status);
        if (far_kind != near_math::kNone) {
          g_any_gap = gap.gap;
          g_any_object = c.object;
          g_any_kind = far_kind;
        }
      }
      continue;
    }
    const int kind = PressKind(c.object, status);
    if (kind == near_math::kNone) continue;
    const float c_extra = CatchExtra(c, kind, units);
    const float c_reach = reach + c_extra;
    const float c_wide = near_math::WideMoving(wide_reach, c_extra, g_catch_on);
    if (g_one_target) {
      // ONE TARGET: the body that the player sees, and the rule of
      // near_math::BeatsTarget. The Creature is a choice too.
      Measured m;
      Measure(c, kind, &m);
      // A SIGNPOST THAT THE POINT IS NOT IN COMES LAST (near_math::SignOut,
      // rule 6). It is no near object: it does not stop the wide pick, and it
      // does not turn the grab from a distance off.
      m.target.last = near_math::SignOut(
          g_sign_last, m.target.scroll && ScrollType(c.object) == kScrollSilver,
          m.target.gap.gap, touch);
      if (!m.target.last && m.target.gap.gap < nearest) {
        nearest = m.target.gap.gap;
      }
      // A TREE, A BUILDING OR A PILE THAT THE POINT IS NOT IN does not turn
      // the grab from a distance off either (near_math::LooseOut).
      const bool loose_out = near_math::LooseOut(
          g_beside_loose, m.target.loose, m.target.gap.gap, touch);
      if (near_math::StopsFar(m.target.last, loose_out) &&
          m.target.gap.gap < block_gap) {
        block_gap = m.target.gap.gap;
        g_near_object = c.object;
      }
      if (loose_out && m.target.gap.gap < loose_gap) {
        loose_gap = m.target.gap.gap;
        loose_object = c.object;
      }
      if (m.target.gap.gap < g_any_gap) {
        g_any_gap = m.target.gap.gap;
        g_any_object = c.object;
        g_any_kind = kind;
      }
      if (!(m.target.gap.gap < (m.target.scroll ? scroll_reach : c_reach))) {
        // The wide reach: an object that the Hand can hold, and the Creature
        // (hand_wide_creature).
        if ((kind == near_math::kHold ||
             (kind == near_math::kCreature && g_creature_wide)) &&
            !m.target.scroll && m.target.gap.gap < c_wide &&
            (wide_object == nullptr || m.target.gap.gap < wide_gap.gap)) {
          wide_object = c.object;
          wide_kind = kind;
          wide_gap = m.target.gap;
          wide_measured = m;
          wide_own = c_wide;
          wide_speed = c_wide > wide_reach ? ObjectSpeedNow(c) : 0.0f;
          for (int k = 0; k < 3; ++k) wide_position[k] = c.position[k];
        }
        continue;
      }
      // A scroll past the reach of the pick is the last choice (rule 5).
      m.target.outer = m.target.scroll && !(m.target.gap.gap < reach);
      if (g_choice != nullptr &&
          !near_math::BeatsTarget(m.target, chosen.target, touch, eps)) {
        continue;
      }
      chosen = m;
      best = m.target.gap;
      best_kind = kind;
      g_choice = c.object;
      for (int k = 0; k < 3; ++k) g_choice_position[k] = c.position[k];
      g_choice_gap = m.target.gap.gap;
      chosen_reach = c_reach;
      chosen_speed = c_reach > reach ? ObjectSpeedNow(c) : 0.0f;
      continue;
    }
    if (kind == near_math::kTap) {
      // The body of a tap: the cylinder from its point, made larger, and the
      // cylinder about the middle of its box, where the game's own pick has
      // a scroll. The nearer of the two counts, so the second one can only
      // make a tap easier.
      gap = GapTo(body, c.position, true);
      float middle[3];
      if (BoxMiddle(c, body, middle)) {
        const float base[3] = {middle[0], middle[1] - 0.5f * body.height,
                               middle[2]};
        const near_math::Gap boxed =
            near_math::BodyGap(g_target, base, body.radius, body.height);
        if (boxed.gap < gap.gap ||
            (boxed.gap == gap.gap && boxed.middle < gap.middle)) {
          gap = boxed;
        }
      }
    }
    if (gap.gap < nearest) {
      nearest = gap.gap;
      block_gap = gap.gap;
      g_near_object = c.object;
    }
    // The Creature stays with the game's own pick.
    if (kind == near_math::kCreature) {
      if (gap.gap < creature_gap) creature_gap = gap.gap;
      continue;
    }
    if (!(gap.gap < c_reach)) {
      if (kind == near_math::kHold && gap.gap < c_wide &&
          (wide_object == nullptr || gap.gap < wide_gap.gap)) {
        wide_object = c.object;
        wide_gap = gap;
        wide_own = c_wide;
        wide_speed = c_wide > wide_reach ? ObjectSpeedNow(c) : 0.0f;
        for (int k = 0; k < 3; ++k) wide_position[k] = c.position[k];
      }
      continue;
    }
    if (g_choice != nullptr &&
        !near_math::Beats(gap, kind, best, best_kind, eps)) {
      continue;
    }
    best = gap;
    best_kind = kind;
    g_choice = c.object;
    for (int k = 0; k < 3; ++k) g_choice_position[k] = c.position[k];
    g_choice_gap = gap.gap;
    chosen_reach = c_reach;
    chosen_speed = c_reach > reach ? ObjectSpeedNow(c) : 0.0f;
  }
  // A signpost of rule 6 is the choice only when no object is in the wide
  // reach: nearest does not count such a signpost.
  if ((g_choice == nullptr || chosen.target.last) && wide_object != nullptr &&
      near_math::WidePick(wide_gap.gap, reach, wide_own, nearest, eps)) {
    g_choice = wide_object;
    chosen = wide_measured;
    best = wide_gap;
    best_kind = wide_kind;
    for (int k = 0; k < 3; ++k) g_choice_position[k] = wide_position[k];
    g_choice_gap = wide_gap.gap;
    chosen_reach = wide_own;
    chosen_speed = wide_speed;
    g_choice_wide = true;
    g_choice_wide_cm = wide_own / units * 100.0f;
    if (g_wide_logged != g_choice && g_choice_gap < wide_reach) {
      g_wide_logged = g_choice;
      ModLog("hand pick    : the %s %p is %.1f cm of the room from the Hand's "
             "point, past the reach of %d cm and inside the wide reach of %d "
             "cm (hand_wide_pick_cm). No object is nearer, so it is the "
             "target, frame %llu", ClassName(g_choice), g_choice,
             g_choice_gap / units * 100.0f, g_pick_cm, g_wide_cm, g_frame);
    }
    // A wide choice that only the extra reach of a moving object made: one
    // line for each new object.
    if (!(g_choice_gap < wide_reach) && g_wide_catch_logged != g_choice) {
      g_wide_catch_logged = g_choice;
      ++g_catch_picks;
      ModLog("hand pick    : the %s %p moves at %.0f cm of the room a second, "
             "so its wide reach is %.1f cm of the room and not %d "
             "(catch_moving=%d), gap %.1f cm, frame %llu", ClassName(g_choice),
             g_choice, wide_speed / units * 100.0f, g_choice_wide_cm,
             g_wide_cm, g_catch_on, g_choice_gap / units * 100.0f, g_frame);
    }
  } else if (g_choice == nullptr) {
    g_wide_logged = nullptr;
    g_wide_catch_logged = nullptr;
  }
  // A SIGNPOST OF RULE 6 GIVES WAY TO THE AIM (near_math::SignGivesWay). It is
  // no choice while the aim has a target at a distance from the last frame.
  // FarSelect runs with such a choice, and it takes the choice away in the
  // frame in which the aim gets its target. A loose body that the point is
  // not in gives way the same (far_grab_beside_loose).
  g_choice_loose = g_choice != nullptr && g_one_target && !g_choice_wide &&
                   near_math::LooseOut(g_beside_loose, chosen.target.loose,
                                       chosen.target.gap.gap, touch);
  g_choice_last = g_choice != nullptr && (chosen.target.last || g_choice_loose);
  if (near_math::SignGivesWay(g_choice_last,
                              g_far_ready && g_far_ray && FarGrabOn() &&
                                  FarGrabTargetObject() != nullptr)) {
    g_choice = nullptr;
    best_kind = near_math::kNone;
    g_choice_last = false;
    g_choice_loose = false;
  }
  // While a choice stands, the table hook keeps each other object from the
  // game's pick, the Creature too. A house beside the Creature must not do
  // that, so an object that a press taps is no choice while the Creature is
  // as near as it. An object to hold or a bulk source stays the choice.
  if (g_choice != nullptr && best_kind == near_math::kTap &&
      creature_gap <= best.gap + eps) {
    g_choice = nullptr;
    best_kind = near_math::kNone;
  }
  // A choice that only the extra reach of a moving object made: one line for
  // each new object.
  if (g_choice != nullptr && best_kind == near_math::kHold &&
      !g_choice_wide && g_choice_gap >= reach && g_catch_logged != g_choice) {
    g_catch_logged = g_choice;
    ++g_catch_picks;
    ModLog("hand pick    : the %s %p moves at %.0f cm of the room a second, so "
           "its reach is %.1f cm of the room and not %d (catch_moving), gap "
           "%.1f cm, frame %llu", ClassName(g_choice), g_choice,
           chosen_speed / units * 100.0f, chosen_reach / units * 100.0f,
           g_pick_cm, g_choice_gap / units * 100.0f, g_frame);
  } else if (g_choice == nullptr) {
    g_catch_logged = nullptr;
  }
  g_choice_take = g_choice != nullptr && best_kind == near_math::kTake;
  g_choice_tap = g_choice != nullptr && best_kind == near_math::kTap;
  // A scroll that is not active yet: the press is a click for the scripts.
  g_choice_click = g_choice_tap && !Tappable(g_choice, status) &&
                   ClickScroll(g_choice);
  // What the halo and the glow show (one target).
  g_choice_kind = g_choice != nullptr ? best_kind : near_math::kNone;
  g_choice_acts = g_one_target && g_choice != nullptr && PressActs(g_choice);
  g_choice_touched = g_choice != nullptr && best.gap <= touch;
  for (int k = 0; k < 3; ++k) g_choice_center[k] = chosen.center[k];
  g_choice_radius = chosen.radius;
  g_choice_half = chosen.half;
  g_near_gap = nearest;
  // THE SCROLL NEAREST TO THE POINT, with what the game says of a press on
  // it. One line for each change while it is within 30 cm of the room, so a
  // log shows why a scroll was or was not the target.
  g_scroll_near = -1;
  g_scroll_near_gap = 1e30f;
  for (int i = 0; i < g_scroll_count; ++i) {
    const float gap =
        near_math::BallGap(g_target, g_scrolls[i].center, g_scrolls[i].radius)
            .gap;
    if (gap < g_scroll_near_gap) {
      g_scroll_near_gap = gap;
      g_scroll_near = i;
    }
  }
  if (g_scroll_count > 0) ++g_scroll_frames;
  g_any_count = g_candidate_count;
  if (g_scroll_near >= 0) {
    const Scroll& s = g_scrolls[g_scroll_near];
    g_scroll_near_copy = s;
    g_scroll_near_taps = Tappable(s.object, status);
    ScrollWords(s.object, status, s.active, s.script,
                s.type == kScrollSilver, g_scroll_click, g_scroll_near_words,
                sizeof(g_scroll_near_words));
    // A press acts on it: a tap, or the first click.
    const bool acts = g_scroll_near_taps || ClickScroll(s.object);
    const bool close = g_scroll_near_gap < 0.30f * units;
    const int state = !close ? 0
                             : (s.object == g_choice ? 3
                                : (acts ? 2 : 1));
    if ((close || g_scroll_said != nullptr) &&
        (s.object != g_scroll_said || state != g_scroll_said_state) &&
        g_scroll_logs < kMaxScrollLogs && DirectState(g_hand)) {
      ++g_scroll_logs;
      g_scroll_said = close ? s.object : nullptr;
      g_scroll_said_state = state;
      const float cm = units > 0.0f ? 100.0f / units : 0.0f;
      ModLog("hand scroll  : the scroll %p (%s) is %.1f cm of the room from "
             "the Hand's point: %s. Its ball: middle %.2f %.2f %.2f, radius "
             "%.2f units (%.1f cm). The draw put its mesh at %.2f %.2f %.2f, "
             "and the game's camera is %.1f units from it. Script %d. %s. "
             "The pick %s. %d scrolls in this frame, frame %llu", s.object,
             ScrollTypeName(s.type), g_scroll_near_gap * cm,
             state == 3   ? "it is the target of the grip"
             : state == 2 ? "a press acts on it, and it is not the target"
             : state == 1 ? "no press acts on it"
                          : "it is not near now",
             s.center[0], s.center[1], s.center[2], s.radius, s.radius * cm,
             s.point[0], s.point[1], s.point[2], s.camera, s.script,
             g_scroll_near_words,
             s.list == kScrollFromDraw
                 ? "did not offer it, and the mod's list has it from its draw"
             : s.list == kScrollNotListed
                 ? "did not offer it, so it is not in the mod's list"
             : s.list == kScrollLocked ? "is locked, so the list is empty"
                                       : "offered it",
             g_scroll_count, g_frame);
    }
  }
  g_near_blocked =
      g_near_rule && g_near_object != nullptr &&
      near_math::NearBlocks(block_gap, was_blocked, near_in, near_out);
  if (g_near_blocked) ++g_near_block_frames;
  // A loose body that the point is not in, as near as the gap at which the
  // grab from a distance goes off: one line for each new such body while the
  // grab from a distance stays on.
  void* spared = g_near_rule && !g_near_blocked && loose_gap <= near_in
                     ? loose_object
                     : nullptr;
  if (spared != g_loose_said && FarGrabOn()) {
    g_loose_said = spared;
    if (spared != nullptr && g_near_logs < kMaxNearLogs) {
      ++g_near_logs;
      ModLog("far grab    : stays on. The Hand's point is %.1f cm of the room "
             "from %p (%s) and not in it (far_grab_beside_loose=1), frame %llu",
             units > 0.0f ? loose_gap / units * 100.0f : 0.0f, spared,
             ClassName(spared), g_frame);
    }
  }
  if (g_near_blocked != was_blocked && FarGrabOn()) {
    if (g_near_blocked) ++g_near_blocks;
    if (g_near_logs < kMaxNearLogs) {
      ++g_near_logs;
      if (g_near_blocked) {
        ModLog("far grab    : off. The Hand's point is %.1f cm of the room "
               "from %p (%s), and a press acts on that object. Near objects "
               "win to %d cm, frame %llu",
               units > 0.0f ? block_gap / units * 100.0f : 0.0f,
               g_near_object, ClassName(g_near_object), g_near_out_cm,
               g_frame);
      } else {
        ModLog("far grab    : on again. No object that a press acts on is "
               "within %d cm of the Hand's point, frame %llu", g_near_out_cm,
               g_frame);
      }
    }
  }
  // A FETCH FROM A DISTANCE beats the Hand's own choice: the player pulled
  // this object, and the game's pick up takes what the next record holds.
  g_choice_far = false;
  {
    void* far_object = nullptr;
    float far_at[3], far_land[2];
    if (FarGrabFetching(&far_object, far_at, far_land)) {
      g_choice = far_object;
      g_choice_take = false;
      g_choice_tap = false;
      g_choice_far = true;
      // The far touch on a scroll that is not active yet: a click.
      g_choice_click = IsScrollObject(far_object) &&
                       !Tappable(far_object, status) && ClickScroll(far_object);
      g_choice_kind = near_math::kNone;
      g_choice_acts = false;
      for (int k = 0; k < 3; ++k) g_choice_position[k] = far_at[k];
      g_choice_gap = 0.0f;
      g_logged_choice = g_choice;
    }
  }
  g_choice_table =
      g_choice != nullptr && !IsBadReadPtr(g_choice, sizeof(void*))
          ? *static_cast<void* const*>(g_choice)
          : nullptr;
  // ONE TARGET: each change of the target, and each change of "a press acts
  // on it", with the body, so a log shows what the halo was on at each press.
  const int acts = g_choice_acts ? 1 : 0;
  if (g_one_target && !g_choice_far &&
      (g_choice != g_logged_choice ||
       (g_choice != nullptr && acts != g_logged_acts)) &&
      g_pick_logs < kMaxPickLogs && DirectState(g_hand)) {
    ++g_pick_logs;
    if (g_choice != nullptr) {
      char name[48];
      strcpy_s(name, ClassName(g_choice));
      ModLog("hand pick    : the target is %p (%s): %s%s. Its body is %.1f cm "
             "of the room from the Hand's point (%s the reach of %d cm). The "
             "body has its middle at %.2f %.2f %.2f, a radius of %.2f and a "
             "half height of %.2f units. %d candidates, frame %llu", g_choice,
             name,
             g_choice_click ? "the grip clicks it (the scroll is not "
                              "active: the game makes it active at the "
                              "first click)"
                            : KindName(g_choice_kind),
             g_choice_acts ? ""
                           : ", but the land under the Hand is outside your "
                             "influence, so a press does nothing",
             units > 0.0f ? g_choice_gap / units * 100.0f : 0.0f,
             g_choice_touched ? "the point touches it, in"
                              : "the point does not touch it, in",
             g_choice_wide ? static_cast<int>(g_choice_wide_cm + 0.5f)
             : chosen.target.scroll && g_scroll_reach_cm > g_pick_cm
                 ? g_scroll_reach_cm
                 : g_pick_cm,
             g_choice_center[0], g_choice_center[1],
             g_choice_center[2], g_choice_radius, g_choice_half,
             g_candidate_count, g_frame);
      if (g_choice_kind == near_math::kCreature) {
        // The game starts the touch of a Creature only when the Creature has
        // a player (Creature::ValidForLockedSelectProcess 0x00476E10). At the
        // Creature select of a new game it has none.
        const void* player = reinterpret_cast<GetPlayerFn>(
            VTable(g_choice)[kCreatureGetPlayer / 4])(g_choice, nullptr);
        ModLog("hand pick    : the Creature %p has %s, and the game's test of "
               "a press on it says %s (the hand mode is 0xD for yes)",
               g_choice,
               player == nullptr ? "no player"
               : player == LocalPlayer() ? "the local player"
                                         : "another player",
               TakeValid(g_choice, status) ? "yes" : "no");
      }
    } else {
      ModLog("hand pick    : no target: nothing that a press acts on within "
             "%d cm of the room of the Hand's point, of %d candidates, frame "
             "%llu", g_pick_cm, g_candidate_count, g_frame);
    }
    g_logged_choice = g_choice;
  }
  g_logged_acts = g_choice != nullptr ? acts : -1;
  // A faint tick when the Hand's point comes to a new target that a press
  // acts on, as the far grab gives for a new far target. At most one in 80
  // ms, so a hand that moves through a wood does not buzz.
  if (g_one_target && !g_choice_far && g_choice != g_haptic_choice) {
    g_haptic_choice = g_choice;
    const ULONGLONG tick = GetTickCount64();
    if (g_choice != nullptr && g_choice_acts &&
        tick - g_haptic_target_tick >= 80) {
      g_haptic_target_tick = tick;
      HapticsPulse(haptics_math::kTarget);
    }
  }
  if (g_choice != g_logged_choice && g_pick_logs < kMaxPickLogs &&
      DirectState(g_hand)) {
    ++g_pick_logs;
    if (g_choice != nullptr) {
      char name[48];
      strcpy_s(name, ClassName(g_choice));
      ModLog("hand pick    : %p (%s)%s is %.2f units from the Hand's point "
             "(%.1f cm of the room), inside %.2f, at %.2f %.2f %.2f, of %d "
             "candidates, frame %llu", g_choice, name,
             g_choice_tap ? ", which the grip taps,"
             : !g_choice_take ? ""
             : InsideInfluence(g_face, g_choice_position[0],
                               g_choice_position[2])
                 ? " (a bulk source inside your influence)"
                 : " (a bulk source outside your influence: the game gives "
                   "no take there)",
             g_choice_gap,
             units > 0.0f ? g_choice_gap / units * 100.0f : 0.0f, reach,
             g_choice_position[0], g_choice_position[1], g_choice_position[2],
             g_candidate_count, g_frame);
    } else {
      ModLog("hand pick    : nothing that a press acts on within %.2f units "
             "of the Hand's point, of %d candidates, frame %llu", reach,
             g_candidate_count, g_frame);
    }
  }
  g_logged_choice = g_choice;
}

// ONCE A FRAME for one list of candidates. The glow asks for the choice at
// the end of the world of the first eye pass (HandDirectChooseNow), so the
// picture of a frame shows the choice of that frame, and the next press acts
// on the object that the halo was on. The end of the pass asks again, and the
// choice is made again only when the draw offered more objects since.
uint64_t g_choose_frame = 0;
int g_choose_count = -1;
void ChooseOnce() {
  if (g_choose_frame == g_frame && g_choose_count == g_candidate_count) return;
  g_choose_frame = g_frame;
  g_choose_count = g_candidate_count;
  Choose();
}

// Whether the land hides an object from the eye. The line from the eye to a
// point half a radius over the object's base is tested against the land at
// 24 points. The last 5 percent is left out, because the object stands on the
// land there. A hill 2 cm of the room higher than the line hides it. The draw
// offers every object in the frustum, hidden or not, so without this test the
// hand could pick an object behind a hill.
//
// THE SEA DOES NOT HIDE. GetAltitude gives 0 for the sea, and the sea is a
// plane at y 0 that the player sees through. An object that floats can have
// its point under that plane, and then the line to it goes under 0 before the
// object. So a point of the line over the sea never hides the object.
bool FarVisible(const float at[3], float radius) {
  const float top[3] = {at[0], at[1] + 0.5f * radius, at[2]};
  const float slack = 0.02f * g_far_units;
  const GetAltitudeFn altitude = reinterpret_cast<GetAltitudeFn>(kGetAltitude);
  const int steps = 24;
  for (int k = 0; k < steps; ++k) {
    const float t = (static_cast<float>(k) + 0.5f) / steps * 0.95f;
    const float x = g_far_eye[0] + (top[0] - g_far_eye[0]) * t;
    const float y = g_far_eye[1] + (top[1] - g_far_eye[1]) * t;
    const float z = g_far_eye[2] + (top[2] - g_far_eye[2]) * t;
    if (!(x > 0.0f && x < 5119.0f && z > 0.0f && z < 5119.0f)) continue;
    const int32_t coords[3] = {static_cast<int32_t>(x * kMapScale),
                               static_cast<int32_t>(z * kMapScale), 0};
    if (controller_math::FarLandHides(altitude(coords), y, slack)) {
      return false;
    }
  }
  return true;
}

// A FOREST GIVES ONE NEW TREE (docs/grab.md, "A forest gives a tree"). A
// press on a BigForest does not put the forest in the Hand. The forest is not
// tuggable (slot 0x718 gives 0), so fn_005D1740 sets only the Tug flag
// (CHand+0x4908) and GInterface+0x450. The step of the pick up action
// (action 13, fn_005D5250) reads the object of the next pick record on each
// frame. After 225 ms with the button down it clears the Tug flag and sends
// the pick up packet (fn_005D2800, packet 0x13). The handler calls
// BigForest::InterfaceSetInMagicHand 0x004393C0, which makes a Tree at the
// forest's point and puts it in the Hand. [V]
//
// So the record must hold the forest for those 225 ms. The pick is locked
// then, the draw offers no candidates, and Inject took the forest out of the
// record as "a holdable object far behind the Hand". The step found no
// object and gave 0, so the interface step cancelled the pick up on the next
// frame (fn_005D1260, called at 0x005D1246), and the grip on a forest gave
// nothing. A single Tree did not have this fault: it is tuggable, so it is in
// the Hand from the press. Inject now leaves the record alone while the pick
// is locked.
//
// At a distance a forest is a target again. The fetch is done when a new
// object is in the Hand, and the glide draws that tree from the forest's
// point. The build of 8fbebc3 left the forest out, because the fetch waited
// for the forest itself and ended with "the game did not take the object".
// By the class name of the RTTI.
bool IsForest(void* object) {
  const char* name = ClassName(object);
  return strcmp(name, "BigForest") == 0 || strcmp(name, "Forest") == 0;
}

// A SCROLL (ScriptHighlight) is a target of the aim ray too, when a press
// taps it: the far touch. No other object that a press taps is, so the aim
// does not knock on each house that it crosses.
bool IsScroll(void* object) {
  return strcmp(ClassName(object), "ScriptHighlight") == 0;
}

// A SIGNPOST: a scroll of the kind "did you know", the wooden sign with a
// text of the help system. The test is the game's own
// (ScriptHighlight::IsDidYouKnow 0x0070AC20: the info of the scroll is entry
// 1 of the table at 0x00D96390) [V]. The caller has tested that the object is
// a scroll.
bool IsSignpost(void* scroll) { return ScrollType(scroll) == kScrollSilver; }

// A MIRACLE ON THE LAND THAT THE HAND CAN TAKE: a one-off seed
// (docs/miracles.md), by the class name of the RTTI. The game makes it from a
// reward or a dispenser, and the scripts of the tutorial put it on the land.
bool IsMiracleSeed(void* object) {
  return strcmp(ClassName(object), "OneOffSpellSeed") == 0;
}

// A candidate of FarSelect by its index: a candidate of the draw, or after
// them your Creature when the draw did not offer it (g_far_own).
const Candidate& FarAt(int index) {
  return index < g_candidate_count ? g_candidates[index] : g_far_own;
}

// A LIVING THING FOR THE BIAS OF far_grab_choice=1: a Creature, an animal, or
// an object of the class Villager, found by the class name. A quest character
// is a SpecialVillager, so this test does not know it. The test key
// far_test_quest=1 makes the test answer no for each villager, as it does for
// a quest character: a desktop run then shows value 1 with a living thing
// that its bias does not know.
bool IsLivingObject(void* object) {
  if (IsCreatureObject(object)) return true;
  if (reinterpret_cast<VirtualIntFn>(VTable(object)[kIsAnimal / 4])(
          object, nullptr) != 0) {
    return true;
  }
  static const bool quest = TestKey("far_test_quest") != 0;
  return !quest && strcmp(ClassName(object), "Villager") == 0;
}

// A LIVING THING FOR THE RANKS AND THE BALLS (far_grab_choice 2 to 4): each
// object of a class that has Living as a base, by the game's own test
// IsLiving. That is a villager of each kind and age, a quest character, an
// animal and a Creature.
bool IsLivingThing(void* object) {
  if (object == nullptr) return false;
  if (IsCreatureObject(object)) return true;
  return reinterpret_cast<VirtualIntFn>(VTable(object)[kIsLiving / 4])(
             object, nullptr) != 0;
}

// The class test of the choice in use. Value 0 knows no class.
bool FarLiving(void* object, int choice) {
  if (choice == far_choice_math::kChoicePoint) return false;
  return choice == far_choice_math::kChoiceBias ? IsLivingObject(object)
                                                : IsLivingThing(object);
}

// The height of the body of a far candidate: the game's height, or two radii.
float FarBodyHeight(void* object, float radius) {
  const float height = reinterpret_cast<VirtualFloatFn>(
      VTable(object)[kGetHeight / 4])(object, nullptr);
  return far_choice_math::BodyHeight(height, radius);
}

// A CREATURE THAT IS NOT YOURS (docs/creature.md, "The leash from a
// distance"). The leash click, the stay and each other grip from a distance
// are for the Creature of the local player only: LeashKind compares the
// Creature with GPlayer+0xA4C of the local player, as the handler of the
// leash packet does. The Creature of a different god, and a Creature with no
// player, is no far target. One line for each such Creature that comes into
// the cone of the aim, so a log shows that the test ran.
void* g_other_creature = nullptr;
int g_other_creature_logs = 0;
const int kMaxOtherCreatureLogs = 12;

void LogOtherCreature(void* creature, float te) {
  const uint8_t* local = static_cast<const uint8_t*>(LocalPlayer());
  void* mine = local != nullptr
                   ? *reinterpret_cast<void* const*>(local + kPlayerCreature)
                   : nullptr;
  // Your own Creature that the game would not leash now has its own lines
  // (creature_stay.cpp, creature_command.cpp).
  if (creature == mine || creature == g_other_creature ||
      g_other_creature_logs >= kMaxOtherCreatureLogs) {
    return;
  }
  g_other_creature = creature;
  ++g_other_creature_logs;
  const void* player = reinterpret_cast<GetPlayerFn>(
      VTable(creature)[kCreatureGetPlayer / 4])(creature, nullptr);
  ModLog("far grab    : the Creature %p is in the aim, %.1f degrees from the "
         "ray. It is %s (its player is %p, the local player is %p, and your "
         "Creature is %p). So it is no far target: no leash click, no stay "
         "and no grip from a distance acts on it, frame %llu", creature, te,
         player == nullptr ? "a Creature with no player"
                           : "the Creature of a different player",
         player, local, mine, g_frame);
}

// THE SELECTION OF A FAR GRAB. After the first eye pass, from the candidates
// of the cone: a holdable object, a bulk source or a scroll that scores in
// the cone, is not near the Hand, and is in the range of far_grab_max_m from
// the head. A near object always wins: there is no far target while Choose
// has a choice, and while an object that a press acts on is near the Hand's
// point (g_near_blocked). The hysteresis is in controller_math.h
// (FarPickTarget). While the lock holds, the target does not change, and its
// position follows the draw.
void FarSelect() {
  if (!g_far_ready || g_face == nullptr || !g_far_ray) return;
  if (FarGrabLocked()) {
    // The draw offered the locked object in this frame, so it lives: its
    // place, the land under it and the influence there follow it.
    void* locked = FarGrabTargetObject();
    float at[3];
    if (locked != nullptr && CandidatePosition(locked, at)) {
      const float land[2] = {ClampToMap(at[0]), ClampToMap(at[2])};
      const bool inside =
          reinterpret_cast<VirtualIntFn>(
              VTable(locked)[kMustBeInInfluence / 4])(locked, nullptr) == 0 ||
          InsideInfluence(g_face, land[0], land[1]);
      FarGrabTrack(at, land, inside);
    }
    return;
  }
  // A signpost of rule 6 at the Hand, or a loose body that the point is not
  // in, does not stop the aim (g_choice_last).
  if ((g_choice != nullptr && !g_choice_last) || g_near_blocked) {
    FarGrabNoTarget();
    return;
  }
  void* status = *reinterpret_cast<void* const*>(g_face + kFaceStatus);
  void* current = FarGrabTargetObject();
  int current_index = -1;
  int count = 0;
  const float keep = FarGrabKeepDeg();
  // THE CHOICE (far_choice_math.h): one point, the body or a ball, and a
  // bias, ranks or none.
  const int choice = FarGrabChoice();
  const bool fair = choice != far_choice_math::kChoicePoint;
  const far_choice_math::ChoiceSizes sizes = {
      FarGrabLivingDeg(), FarGrabBallLivingDeg(), FarGrabBallOtherDeg()};
  // The first cut is wider by the largest ball: the middle of a ball is on
  // the body of its object.
  const float cut_deg =
      keep + 4.0f +
      (far_choice_math::ChoiceBalls(choice)
           ? (sizes.ball_living_deg > sizes.ball_other_deg
                  ? sizes.ball_living_deg
                  : sizes.ball_other_deg)
           : 0.0f);
  // YOUR CREATURE BY ITS BODY (far_grab_creature=1). The mod knows the body of
  // your Creature with no draw: the capsule of the choice at the Hand
  // (CreatureBody), from the land under its body bone up to 1.1 S, with its 2D
  // radius. The aim measures the Creature by that body, as it measures each
  // living thing. The point of the mesh of the Creature is at its feet. With
  // the point as the measure, the aim on the head of the Creature is out of
  // the cone of a living thing when the Creature is near or large, and a tree
  // behind it has the halo. The draw offers an object by that point too. So
  // the Creature is a candidate also when the draw did not offer it.
  void* own = nullptr;
  controller_math::Capsule own_body = {};
  bool own_offered = false;
  if (g_far_creature_body && fair && g_far_leash) {
    const uint8_t* local = static_cast<const uint8_t*>(LocalPlayer());
    void* mine = local != nullptr
                     ? *reinterpret_cast<void* const*>(local + kPlayerCreature)
                     : nullptr;
    if (mine != nullptr && LeashKind(mine) != near_math::kLeashNone &&
        CreatureBody(mine, &own_body)) {
      own = mine;
    }
  }
  // A FIGHT OF CREATURES (fight_grip=1). A press on a Creature is the attack
  // or the block of the fight, so no Creature is a far target: no leash click
  // and no stay. The grip at the Creature goes to the game.
  const bool fight = g_fight_grip && InFight();
  if (fight != g_fight_seen) {
    g_fight_seen = fight;
    ModLog("far grab    : %s, frame %llu",
           fight ? "a fight of Creatures starts, so no Creature is a far "
                   "target until it ends (fight_grip=1). The grip at a "
                   "Creature is the attack or the block of the game"
                 : "the fight of Creatures ended, so your Creature is a far "
                   "target again",
           g_frame);
  }
  for (int i = 0; own != nullptr && i < g_candidate_count; ++i) {
    if (g_candidates[i].object == own) own_offered = true;
  }
  if (own != nullptr && !own_offered) {
    g_far_own.object = own;
    g_far_own.model = nullptr;
    for (int k = 0; k < 3; ++k) g_far_own.position[k] = own_body.base[k];
  }
  g_far_own_point_te = -1.0f;
  const int total =
      g_candidate_count + (own != nullptr && !own_offered ? 1 : 0);
  for (int i = 0; i < total && count < kMaxFarScores; ++i) {
    const Candidate& c = FarAt(i);
    // The base of the body: the point of the object, or the land under the
    // body bone of your Creature. The distance from the Hand's point is to the
    // point of the object, or to the middle of the body of your Creature.
    const bool by_body = own != nullptr && c.object == own;
    float base[3] = {c.position[0], c.position[1], c.position[2]};
    float from[3] = {c.position[0], c.position[1], c.position[2]};
    if (by_body) {
      for (int k = 0; k < 3; ++k) base[k] = from[k] = own_body.base[k];
      from[1] += 0.5f * own_body.height;
    }
    const float hand_m = Distance(from, g_target) / g_far_units;
    // NO BAND WITH NO TARGET. The cone leaves out an object whose point is
    // less than kFarMinHandM (35 cm) from the Hand's point. With the rule of
    // the near objects, the Hand takes only what is 4 cm from its point, so
    // an object 20 cm away was no target of the Hand and no target of the
    // aim, and the aim went to an object behind it. So with the rule on, the
    // least distance is the distance at which the far grab comes back
    // (far_grab_near_out_cm). The score takes 35 cm for a nearer object.
    if (hand_m < FarMinMeters()) continue;
    const float score_m = controller_math::FarScoreMeters(hand_m);
    // A first cut with no radius: te with a radius is never more than it. The
    // choice by the body cuts with a radius that holds the whole body.
    float body_radius = 0.0f, body_height = 0.0f;
    if (by_body) {
      body_radius = own_body.radius;
      body_height = own_body.height;
    } else if (fair) {
      body_radius = Radius2D(c.object);
      body_height = FarBodyHeight(c.object, body_radius);
    }
    const controller_math::FarScore rough = controller_math::FarConeScore(
        g_far_origin, g_far_direction, base,
        fair ? far_choice_math::CutRadius(body_height, body_radius) : 0.0f,
        score_m);
    if (!rough.valid || rough.te > cut_deg) continue;
    // THE RANGE: at most far_grab_max_m meters of the room from the head,
    // along the floor.
    if (!controller_math::FarInRange(
            controller_math::FarFloorMeters(g_far_eye, base, g_far_units),
            FarGrabMaxMeters(), c.object == current)) {
      ++g_far_out_of_range;
      continue;
    }
    // A bulk source (a pile of a store, a field, a fish farm) is a target
    // too: the grip takes from it at a distance. A scroll that a press taps
    // is one too: the grip activates it (the far touch). Else the object
    // must be holdable. A forest is holdable: the fetch gives a new tree.
    // THE CREATURE IS A FAR TARGET OF ITS OWN KIND: THE LEASH
    // (docs/creature.md, "The leash from a distance"). The game says that the
    // Creature is a bulk source (Creature::ValidForLockedSelectProcess
    // 0x00476E10 gives 1 for a Creature with a player), because a press on it
    // starts its touch with the locked select. So the aim took it as a pile:
    // the halo and the hint said "collect", and a grip from a distance started
    // the Creature state with the hand far from the body. A short grip changed
    // the leash, and that part was useful. So the player's own Creature is a
    // target of its own kind: a short grip sends the leash click (packet
    // 0x5F), a grip that stays closed makes the Creature stay, and no press
    // goes to the game (far_grab.cpp). A Creature of another player, and a
    // Creature that the game would not leash now, is no target. far_leash=0
    // takes the Creature out of the far targets.
    const bool creature = IsCreatureObject(c.object);
    if (creature && fight) continue;
    const int leash = creature && g_far_leash ? LeashKind(c.object) : 0;
    if (creature && leash == 0) {
      LogOtherCreature(c.object, rough.te);
      continue;
    }
    const bool bulk = !creature && TakeValid(c.object, status);
    // A scroll that is not active yet is a target too: the press is then a
    // click for the scripts of the game (ClickScroll).
    const bool touch = !creature && !bulk && g_near_rule && g_far_touch &&
                       IsScroll(c.object) &&
                       (Tappable(c.object, status) || ClickScroll(c.object));
    if (!creature && !bulk && !touch && !Holdable(c.object, status)) continue;
    // The test key far_test_no_trees=1: a tree is planted when it is let go,
    // not thrown, so the test of the throw selects other objects.
    // A forest gives a tree, so the key leaves it out too. The test keys
    // far_test_forest=1 and far_test_scroll=1 select only a forest or only a
    // scroll.
    static const bool no_trees = TestKey("far_test_no_trees") != 0;
    const int only = FarTestOnly();
    const bool forest = !creature && !bulk && !touch && IsForest(c.object);
    // far_test_creature=1 selects only the player's own Creature, and
    // far_test_tree=1 only a tree.
    if ((only == 1 && !forest) || (only == 2 && !touch) ||
        (only == 3 && !creature) ||
        (only == 4 && (creature || bulk || touch || forest ||
                       !IsTreeObject(c.object)))) {
      continue;
    }
    if (no_trees &&
        (forest || reinterpret_cast<VirtualIntFn>(
                       VTable(c.object)[kIsAnyKindOfTree / 4])(c.object,
                                                               nullptr) != 0)) {
      continue;
    }
    const float radius = fair ? body_radius : Radius2D(c.object);
    // A LIVING THING, by the class test of the choice (FarLiving). Value 1
    // knows a Creature, an animal and the class Villager. The ranks and the
    // balls know each class that the game calls living. A ball needs the
    // class before the score. The test of value 1 reads the class name, so it
    // runs only for a candidate in the cone.
    const bool may_live = fair && !bulk && !touch && !forest;
    const bool by_bias = choice == far_choice_math::kChoiceBias;
    bool living = may_live && !by_bias && FarLiving(c.object, choice);
    // The choice measures the angle to the upright body of an object, or to
    // its ball. A forest and a scroll keep the point: a forest is a field of
    // trees, and a scroll floats. Your Creature has the body of its capsule.
    // With far_grab_creature=0, and when the mod does not know that body, the
    // Creature keeps the point of its mesh.
    float near_deg = 0.0f;
    controller_math::FarScore s = far_choice_math::ChoiceScore(
        choice, sizes, g_far_origin, g_far_direction, base, body_height,
        radius, living, forest || touch || (creature && !by_body), score_m,
        &near_deg);
    if (by_body && own_offered) {
      // What the point of the mesh gives, for the log line of the Creature.
      const controller_math::FarScore point = controller_math::FarConeScore(
          g_far_origin, g_far_direction, c.position, Radius2D(c.object),
          controller_math::FarScoreMeters(
              Distance(c.position, g_target) / g_far_units));
      g_far_own_point_te = point.valid ? point.te : 180.0f;
      for (int k = 0; k < 3; ++k) g_far_own_point[k] = c.position[k];
    }
    if (!s.valid || s.te > keep) continue;
    if (fair && forest) s.score += far_choice_math::SizePenalty(radius, 0.0f);
    if (may_live && by_bias) living = FarLiving(c.object, choice);
    g_far_raw[count] = s;
    g_far_living[count] = living;
    if (living && by_bias) {
      s.score = far_choice_math::RankedScore(s.score, true);
    }
    // THE CLASS FOR THE RANKS (far_choice_math.h, ClassOf). A scroll of a
    // quest is in the first rank, as a living thing is: the character of a
    // quest stands under his scroll, and the nearest of the two to the ray
    // wins. A miracle that the Hand can take is in the first rank too
    // (far_grab_miracle). A signpost is in the last rank (far_grab_sign).
    const bool sign = touch && IsSignpost(c.object);
    const bool miracle =
        !creature && !bulk && !touch && !forest && IsMiracleSeed(c.object);
    const int cls = far_choice_math::ClassOf(
        far_choice_math::ChoiceRanks(choice), living, fair && touch, sign,
        miracle, g_far_sign_last, g_far_miracle_first);
    // A scroll in the cone scores better than its angle, and a signpost of
    // the last rank does not (TouchBias).
    s.score -= far_choice_math::TouchBias(touch, cls);
    g_far_class[count] = cls;
    g_far_signs[count] = sign;
    g_far_miracles[count] = miracle;
    g_far_first[count] = cls == far_choice_math::kClassFirst;
    g_far_near[count] = near_deg;
    g_far_scores[count] = s;
    g_far_which[count] = i;
    g_far_radius[count] = radius;
    g_far_bulk[count] = bulk;
    g_far_touches[count] = touch;
    g_far_forests[count] = forest;
    g_far_leashes[count] = leash;
    if (c.object == current) current_index = count;
    ++count;
  }
  // The best one that is seen. A hill between the eye and an object hides
  // it, and the draw offers every object in the frustum. So the winner is
  // tested against the land, and a hidden one is out for this frame. The test
  // costs 24 land heights, so it runs for the winners only.
  // THE RANKS AND THE TARGET THAT STAYS (far_choice_math.h, PickRanked), with
  // the classes of the candidates (PickClassed).
  const far_choice_math::StayRule stay_rule = far_choice_math::MakeStayRule(
      choice, sizes.living_deg, FarGrabStay(), FarGrabStayDeg(),
      FarGrabSelectDeg(), keep, controller_math::kFarSwitchDeg);
  int pick = -1;
  int why_pick = far_choice_math::kWhyPlain;
  for (int tries = 0; tries < 12; ++tries) {
    pick = far_choice_math::PickClassed(g_far_scores, g_far_near, g_far_class,
                                        count, current_index, stay_rule,
                                        g_far_work, g_far_first, &why_pick);
    if (pick < 0) break;
    const Candidate& w = FarAt(g_far_which[pick]);
    if (FarVisible(w.position, g_far_radius[pick])) break;
    ++g_far_hidden;
    g_far_scores[pick].valid = false;
    pick = -1;
  }
  if (g_far_debug_frames > 0 && g_far_debug_object != nullptr) {
    --g_far_debug_frames;
    int found = -1;
    for (int i = 0; i < g_candidate_count; ++i) {
      if (g_candidates[i].object == g_far_debug_object) found = i;
    }
    if (found < 0) {
      ModLog("far grab    : test: the object of aimfar %p is not among the %d "
             "candidates of the draw, frame %llu", g_far_debug_object,
             g_candidate_count, g_frame);
    } else {
      const Candidate& d = g_candidates[found];
      const float hand_m = Distance(d.position, g_target) / g_far_units;
      const controller_math::FarScore e = controller_math::FarConeScore(
          g_far_origin, g_far_direction, d.position, Radius2D(d.object),
          hand_m);
      ModLog("far grab    : test: the object of aimfar %p is candidate %d, "
             "valid %d, te %.2f, score %.2f, holdable %d, seen %d, chosen "
             "%p, scored %d, frame %llu", g_far_debug_object, found, e.valid,
             e.te, e.score, Holdable(d.object, status),
             FarVisible(d.position, Radius2D(d.object)),
             pick >= 0 ? FarAt(g_far_which[pick]).object : nullptr, count,
             g_frame);
    }
  }
  if (pick < 0) {
    FarGrabNoTarget();
    return;
  }
  const Candidate& c = FarAt(g_far_which[pick]);
  // WHY THIS TARGET, for the log of a new target: the second best candidate
  // of the cone and the reason (far_choice_math.h, WhyWinner). A candidate
  // that a hill hides is out of the list. The rank and the hold give their
  // own reason (PickRanked).
  const int runner =
      far_choice_math::RunnerUp(g_far_scores, count, pick, FarGrabSelectDeg());
  int why =
      runner < 0 ? far_choice_math::kWhyOnly
      : why_pick != far_choice_math::kWhyPlain
            ? why_pick
            : far_choice_math::WhyWinner(g_far_raw, g_far_scores, g_far_living,
                                         count, pick, current_index,
                                         FarGrabSelectDeg());
  // A miracle that has the halo by its rank.
  if (why == far_choice_math::kWhyRank && g_far_miracles[pick]) {
    why = far_choice_math::kWhyMiracle;
  }
  g_far_why = far_choice_math::WhyWords(why);
  g_far_runner[0] = 0;
  if (runner >= 0) {
    const Candidate& r = FarAt(g_far_which[runner]);
    char runner_name[48];
    strcpy_s(runner_name, ClassName(r.object));
    _snprintf_s(g_far_runner, sizeof(g_far_runner), _TRUNCATE,
                ". The second one is %p (%s%s) at %.1f degrees, score %.2f "
                "against %.2f", r.object, runner_name,
                g_far_living[runner]     ? ", living"
                : g_far_signs[runner]    ? ", a signpost"
                : g_far_miracles[runner] ? ", a miracle"
                                         : "",
                g_far_scores[runner].te, g_far_scores[runner].score,
                g_far_scores[pick].score);
  }
  FarPick out = {};
  out.why = g_far_why;
  out.runner = g_far_runner[0] != 0 ? g_far_runner : nullptr;
  out.object = c.object;
  strcpy_s(g_far_name, ClassName(c.object));
  out.name = g_far_name;
  out.sign = g_far_signs[pick];
  // YOUR CREATURE BY ITS BODY (far_grab_creature=1): one line for each time
  // that it becomes the target, with what the point of its mesh gives.
  if (own != nullptr && c.object == own && c.object != current &&
      g_far_own_logs < kMaxFarOwnLogs) {
    ++g_far_own_logs;
    char by_point[200];
    if (own_offered) {
      _snprintf_s(by_point, sizeof(by_point), _TRUNCATE,
                  "The point of its mesh is at %.1f %.1f %.1f, %.1f units "
                  "below the middle of the body, and by that point the aim is "
                  "%.1f degrees from the Creature (far_grab_creature=0)",
                  g_far_own_point[0], g_far_own_point[1], g_far_own_point[2],
                  own_body.base[1] + 0.5f * own_body.height -
                      g_far_own_point[1],
                  g_far_own_point_te);
    } else {
      strcpy_s(by_point, "The draw did not offer it, so with "
                         "far_grab_creature=0 it is no target");
    }
    ModLog("far grab    : your Creature %p is the target by its body: the "
           "land under its body bone at %.1f %.1f %.1f, %.1f units tall, a "
           "radius of %.2f. The aim is %.1f degrees from the line of the "
           "body, and %.1f past its cover. %s, frame %llu", c.object,
           own_body.base[0], own_body.base[1], own_body.base[2],
           own_body.height, own_body.radius, g_far_near[pick],
           g_far_scores[pick].te, by_point, g_frame);
  }
  // THE HOLD (far_grab_stay=1): one line for each target that keeps the halo
  // past its cone, or against a different thing with a lower score.
  if (c.object != current) g_far_hold_logged = nullptr;
  if (why_pick == far_choice_math::kWhyHeld &&
      c.object != g_far_hold_logged && g_far_hold_logs < kMaxFarHoldLogs) {
    g_far_hold_logged = c.object;
    ++g_far_hold_logs;
    ModLog("far grab    : the target %p (%s) keeps the halo "
           "(far_grab_stay=1). The aim is %.1f degrees from it. Its cone is "
           "%.1f degrees, and it stays to %.1f%s, frame %llu", c.object,
           g_far_name, g_far_scores[pick].te, stay_rule.first_deg,
           stay_rule.hold_deg, g_far_runner, g_frame);
  }
  for (int k = 0; k < 3; ++k) out.position[k] = c.position[k];
  out.radius = g_far_radius[pick];
  out.bulk = g_far_bulk[pick];
  out.touch = g_far_touches[pick];
  out.forest = g_far_forests[pick];
  out.leash = g_far_leashes[pick];
  out.te = g_far_scores[pick].te;
  out.hand_m = Distance(c.position, g_target) / g_far_units;
  out.head_m =
      controller_math::FarFloorMeters(g_far_eye, c.position, g_far_units);
  out.land[0] = ClampToMap(c.position[0]);
  out.land[1] = ClampToMap(c.position[2]);
  out.vtable = *static_cast<void* const*>(c.object);
  out.model = c.model;
  // The Hand can take it inside the influence, or when the object does not
  // need the influence (fn_005D7F20, 0x005D7FA2).
  out.inside =
      reinterpret_cast<VirtualIntFn>(VTable(c.object)[kMustBeInInfluence / 4])(
          c.object, nullptr) == 0 ||
      InsideInfluence(g_face, out.land[0], out.land[1]);
  // A take at a distance also needs the Hand over the player's influence. On
  // each game turn the game ends a take when the Hand's own position
  // (GInterfaceStatus+0xC8) is outside it (0x005DC540). [V]
  if (out.bulk && out.inside) {
    out.inside = InsideInfluence(g_face, ClampToMap(g_target[0]),
                                 ClampToMap(g_target[2]));
  }
  // The leash click has no test of the influence (fn_005D06E0).
  if (out.leash != 0) out.inside = true;
  // THE AIM HAS ITS TARGET, so a signpost of rule 6, or a loose body that the
  // point is not in, is no choice at the Hand from this frame
  // (near_math::SignGivesWay). The grip acts on the far target.
  if (g_choice_last) {
    if (g_sign_way_logs < kMaxSignWayLogs && g_choice_loose) {
      ++g_sign_way_logs;
      ModLog("hand pick    : the %s %p is %.1f cm of the room from the Hand's "
             "point, and the point is not in it. The aim has the target %p "
             "(%s), so the %s is no target now (far_grab_beside_loose=1), "
             "frame %llu", ClassName(g_choice), g_choice,
             g_choice_gap / g_far_units * 100.0f, c.object, g_far_name,
             ClassName(g_choice), g_frame);
    } else if (g_sign_way_logs < kMaxSignWayLogs) {
      ++g_sign_way_logs;
      ModLog("hand pick    : the signpost %p is %.1f cm of the room from the "
             "Hand's point, and the point is not in it. The aim has the "
             "target %p (%s), so the signpost is no target now "
             "(hand_sign_last=1), frame %llu", g_choice,
             g_choice_gap / g_far_units * 100.0f, c.object, g_far_name,
             g_frame);
    }
    g_choice = nullptr;
    g_choice_take = false;
    g_choice_tap = false;
    g_choice_click = false;
    g_choice_kind = near_math::kNone;
    g_choice_acts = false;
    g_choice_touched = false;
    g_choice_table = nullptr;
    g_choice_last = false;
    g_choice_loose = false;
  }
  FarGrabSelect(out);
}

// Put the choice in the next record, or take a far holdable object out of it.
// After each eye pass, because the second pass fills the record too.
//
// A PICK UP OF THE GAME RUNS while the pick is locked (GInterface+0x38). The
// step of the pick up action reads the object of the next record on each
// frame (fn_005D5250), and the draw offers no candidates then, so the mod has
// no choice. The record is the game's own then, and the mod leaves it alone.
// Without this a grip on a forest gave no tree: see IsForest. A fetch from a
// distance keeps its object in the record through the lock.
void Inject() {
  if (!g_active || g_face == nullptr || !HandEmpty()) return;
  if (State(g_hand) == kStateCreature) return;
  // The pick record calls IsInteractable through the first word of the
  // object (fn_005D5E40 at 0x005D5E50).
  if (!KeepLiveChoice("before the mod puts it in the pick record")) return;
  // The object of a fetch can die in the game turn of this frame, after the
  // test of FarFrameStep. Then the record does not get it.
  if (g_choice_far && !FarTargetLives()) {
    g_choice = nullptr;
    g_choice_far = false;
    return;
  }
  if (g_face[kFaceLocked] != 0 && !g_choice_far) {
    ++g_lock_keeps;
    return;
  }
  void* record = g_face + kFaceNextRecord;
  const RecordPickFn record_pick = reinterpret_cast<RecordPickFn>(kRecordPick);
  if (g_choice != nullptr) {
    float distance = Distance(g_choice_position, Camera());
    if (g_choice_far || g_choice_tap || g_one_target) {
      // UpdateInterfaceCollide (0x005D5A70) keeps the object of the record
      // only when it is nearer than the land under the pointer
      // (+0x404 < +0x3FC), or when it stands above that land. The pointer
      // is the Hand, and the far object is behind the land that the Hand's
      // ray meets. So the distance is cut to half of the land distance, and
      // the game keeps the object. The distance is read again nowhere else
      // in the pick up. The point of a scroll that floats can be behind that
      // land too, so an object that a press taps gets the same cut.
      //
      // ONE TARGET: each choice gets the cut. A tree can stand on lower land
      // than the land under the pointer, and then its point is the farther
      // one.
      distance = near_math::RecordDistance(
          distance,
          *reinterpret_cast<const float*>(g_face + kFaceNextLandDistance));
    }
    record_pick(record, nullptr, g_choice, distance);
    ++g_picks;
    return;
  }
  void* picked = *reinterpret_cast<void* const*>(g_face + kFaceNextObject);
  void* status = *reinterpret_cast<void* const*>(g_face + kFaceStatus);
  // ONE TARGET: with no choice the record holds no object. The game's own
  // pick at the pixel of the Hand's point found a scroll, a house or a pile
  // at each depth behind the point, and a press acted on it.
  if (picked != nullptr && (g_one_target || Holdable(picked, status))) {
    record_pick(record, nullptr, nullptr, 0.0f);
    ++g_cleared;
  }
}

// The choice still lives. The game deletes an object in a game turn, and the
// first word of a deleted object is not the table of its class.
bool ChoiceLives() {
  return g_choice != nullptr && g_choice_table != nullptr &&
         !IsBadReadPtr(g_choice, sizeof(void*)) &&
         *static_cast<void* const*>(g_choice) == g_choice_table;
}

// THE TARGET CHECK (target_check=1). True when the choice lives, when there
// is no choice, or when the key is 0. Else the choice is dropped with each of
// its facts, and the log names it. `when` is the place in the frame. The
// object of a fetch from a distance has its own check, which also ends the
// fetch (FarTargetLives).
bool KeepLiveChoice(const char* when) {
  if (!g_target_check || g_choice == nullptr || g_choice_far ||
      ChoiceLives()) {
    return true;
  }
  ++g_target_gone;
  if (g_target_gone_logs < kMaxTargetGoneLogs) {
    ++g_target_gone_logs;
    const void* first = !IsBadReadPtr(g_choice, sizeof(void*))
                            ? *static_cast<void* const*>(g_choice)
                            : nullptr;
    ModLog("hand pick    : the target %p is gone: its first word is %p and "
           "not the table %p of its class. The game deleted it after the "
           "draw that offered it. The mod drops it %s (target_check=1), "
           "frame %llu", g_choice, first, g_choice_table, when, g_frame);
  }
  g_choice = nullptr;
  g_choice_take = false;
  g_choice_tap = false;
  g_choice_click = false;
  g_choice_kind = near_math::kNone;
  g_choice_acts = false;
  g_choice_touched = false;
  g_choice_table = nullptr;
  g_choice_last = false;
  g_choice_loose = false;
  return false;
}

// ONE TARGET: THE RECORD IS THE CHOICE WHEN THE PRESS COMES. The game reads
// the object of the next pick record in the interface step (the hand mode
// chooser fn_005D7F20) and in the right press (FUN_005D1330). Between the
// draw and those two, GInterface::PostDrawProcess can take the object out
// (UpdateInterfaceCollide), and the game's pick of the second eye pass can
// put another object in. So before each of the two the mod makes the record
// agree with the choice of the last draw: the object that the halo shows.
// Only for the empty Hand in its Normal state, with no pick up, no take and
// no fetch from a distance in work.
void AssertRecord() {
  if (!g_one_target || !g_owns || g_face == nullptr || g_hand == nullptr) {
    return;
  }
  if (State(g_hand) != kStateNormal || !HandEmpty() || g_choice_far ||
      g_face[kFaceLocked] != 0 || TakeObject(g_face) != nullptr) {
    return;
  }
  void* next = *reinterpret_cast<void* const*>(g_face + kFaceNextObject);
  void* want = ChoiceLives() ? g_choice : nullptr;
  if (next == want) return;
  const RecordPickFn record_pick = reinterpret_cast<RecordPickFn>(kRecordPick);
  if (want != nullptr) {
    record_pick(g_face + kFaceNextRecord, nullptr, want,
                near_math::RecordDistance(
                    Distance(g_choice_position, Camera()),
                    *reinterpret_cast<const float*>(g_face +
                                                    kFaceNextLandDistance)));
    ++g_record_puts;
  } else {
    record_pick(g_face + kFaceNextRecord, nullptr, nullptr, 0.0f);
    ++g_record_clears;
  }
  if (g_record_logs < kMaxRecordLogs) {
    ++g_record_logs;
    char held[48];
    strcpy_s(held, next != nullptr ? ClassName(next) : "nothing");
    ModLog("hand pick    : before the interface step the pick record held %p "
           "(%s), and the target is %p (%s). The record now holds the "
           "target, frame %llu", next, held, want,
           want != nullptr ? ClassName(want) : "nothing", g_frame);
  }
}

// THE GAME'S SEARCH AT THE PALM (see kPalmSearch). While the controller places
// the Hand and the rule is one target, the search gives the press no object:
// the game's function runs, so the log can say what it found, and then the
// record is the mod's choice again. With palm_search=1, with near_rule below
// 2 and with the mouse, the game's answer stays.
void* __fastcall h_PalmSearch(void* self, void* unused, const int32_t* land) {
  void* found = o_PalmSearch(self, unused, land);
  if (self == nullptr || self != g_face || !g_owns || g_hand == nullptr) {
    return found;
  }
  ++g_palm_asks;
  const bool refuse = g_one_target && !g_palm_search;
  // One line for each press and each object: the press handler asks twice in
  // a call, and on each frame while the button is down and no action started.
  static uint64_t said_frame = 0;
  static void* said_found = nullptr;
  const bool fresh = g_frame > said_frame + 1 || found != said_found;
  said_frame = g_frame;
  said_found = found;
  static int said_none = 0;
  if (found == nullptr && fresh && said_none < 12) {
    ++said_none;
    ModLog("hand press   : the grip pressed with no object in the pick "
           "record, and the game's search at the palm found no object within "
           "5 units of the land behind the palm (%.1f %.1f), frame %llu",
           *reinterpret_cast<const float*>(kPalmLandX),
           *reinterpret_cast<const float*>(kPalmLandZ), g_frame);
  }
  if (found != nullptr && fresh && g_palm_logs < kMaxPalmLogs) {
    ++g_palm_logs;
    const float units = RoomUnits();
    const uint8_t* mesh = *reinterpret_cast<uint8_t* const*>(
        static_cast<uint8_t*>(found) + kObjectMesh);
    const float* at = mesh != nullptr
                          ? reinterpret_cast<const float*>(mesh +
                                                           kMeshTranslation)
                          : nullptr;
    ModLog("hand press   : the grip pressed with no object in the pick "
           "record, and the game's search at the palm found %p (%s) near the "
           "land behind the palm (%.1f %.1f). That object is %.1f cm of the "
           "room from the Hand's point. %s, frame %llu", found,
           ClassName(found), *reinterpret_cast<const float*>(kPalmLandX),
           *reinterpret_cast<const float*>(kPalmLandZ),
           at != nullptr && units > 0.0f
               ? Distance(at, g_target) / units * 100.0f
               : 0.0f,
           refuse ? "The mod gives the press no object (near_rule=2, "
                    "palm_search=0)"
                  : "The press acts on it (the game's own rule)",
           g_frame);
  }
  if (found == nullptr) return nullptr;
  if (!refuse) {
    ++g_palm_given;
    return found;
  }
  // The search put the object in the record. Take it out again.
  reinterpret_cast<RecordPickFn>(kRecordPick)(
      static_cast<uint8_t*>(self) + kFaceNextRecord, nullptr, nullptr, 0.0f);
  ++g_palm_refused;
  return nullptr;
}

// THE RIGHT PRESS, FUN_005D1330 (thiscall on GInterface, no arguments, plain
// ret): the game's handler of a press of the Action button, the grip. The
// step of the idle interface calls it while bit 2 of GInterface+0x39 is set
// (0x005D5207). Before it the record is made the choice, and the log gets one
// line for each press: the target, or that there was none. So a log shows
// what each press acted on.
typedef int(__fastcall* RightPressFn)(void* self, void* unused);
const uintptr_t kRightPress = 0x005D1330;
RightPressFn o_RightPress = nullptr;
uint64_t g_press_frame = 0;
uint64_t g_presses = 0;
uint64_t g_presses_none = 0;
int g_press_logs = 0;
const int kMaxPressLogs = 600;

// THE CLICK OF THE GAME (docs/village.md, "A scroll is not active until the
// first click"). fn_005D3930, thiscall on GInterface (Object*), ret 4. [V]
//  - It records the object as the clicked object of the scripts
//    (fn_005D36D0: GInterface+0x45C, with the game turn at +0x468). The
//    script word GAME_THING_CLICKED (GScript::GameThingClicked 0x0070AEB0)
//    compares its object with that record.
//  - With the leash on the Hand it ties the leash to an object that can have
//    it (fn_005D4180).
//  - Then fn_005D38A0 sends the tap (packet 0x20) when
//    InterfaceValidToTap gives 1, and resets the action of the interface.
// The game calls it from three places: the right press on an object that can
// be tapped (fn_005D1740, 0x005D1833), and two places of the interface step
// for a press that ended within 225 ms with an object in the pick record
// (0x005D531B and 0x005D5426). So a short click on a scroll that is not
// active is a click for the scripts, with no tap. The mod calls the function
// at the press, so the click does not depend on the length of the press.
typedef int(__fastcall* InterfaceClickFn)(void* self, void* unused,
                                          void* object);
const uintptr_t kInterfaceClick = 0x005D3930;
bool ScrollClick(void* scroll) {
  static const uint8_t kClickBytes[10] = {0x56, 0x57, 0x8B, 0x7C, 0x24,
                                          0x0C, 0x85, 0xFF, 0x8B, 0xF1};
  const void* at = reinterpret_cast<const void*>(kInterfaceClick);
  if (g_face == nullptr || scroll == nullptr ||
      IsBadReadPtr(at, sizeof(kClickBytes)) ||
      memcmp(at, kClickBytes, sizeof(kClickBytes)) != 0) {
    return false;
  }
  reinterpret_cast<InterfaceClickFn>(kInterfaceClick)(g_face, nullptr, scroll);
  ++g_scroll_clicks;
  return true;
}

// A ROCK THAT SPLITS (docs/grab.md, "The grip holds"). Rock::SplitInTwo
// 0x006E7560, thiscall on the Rock, no arguments and a plain ret. It makes two
// rocks of 0.79 times the size. The game calls it from the tap of a rock
// (Rock::InterfaceTap, the call at 0x006E748A), from a hard impact
// (Rock::ReactToPhysicsImpact, 0x006E79CF) and from three other places. [V]
// The tap of a rock is the click of the Action button: a press that the game
// measures as shorter than 225 ms. So each split goes to the log with its
// cause and the age of the last press, and a test counts the lines.
typedef void(__fastcall* RockSplitFn)(void* self, void* unused);
const uintptr_t kRockSplit = 0x006E7560;
const uintptr_t kRockSplitFromTap = 0x006E748F;
const uintptr_t kRockSplitFromImpact = 0x006E79D4;
RockSplitFn o_RockSplit = nullptr;
int g_rock_split_logs = 0;
const int kMaxRockSplitLogs = 60;
uint64_t g_rock_taps = 0;    // splits by a tap, this window
uint64_t g_rock_splits = 0;  // each other split, this window

void __fastcall h_RockSplit(void* self, void* unused) {
  const uintptr_t from = reinterpret_cast<uintptr_t>(_ReturnAddress());
  const bool tap = from == kRockSplitFromTap;
  if (tap) {
    ++g_rock_taps;
  } else {
    ++g_rock_splits;
  }
  if (g_rock_split_logs < kMaxRockSplitLogs) {
    ++g_rock_split_logs;
    bool game_holds = false, moving = false;
    uint32_t age = 0;
    const bool have = HandDirectGripPress(&game_holds, &moving, &age);
    char press[96] = "";
    if (tap && have) {
      sprintf_s(press, ". The last press on an object is %u ms old on the "
                       "clock of the game", age);
    }
    ModLog("rock split   : the Rock %p splits in two: %s%s, frame %llu", self,
           tap ? "the game taps it, which is the click of the Action button "
                 "(Rock::InterfaceTap)"
           : from == kRockSplitFromImpact
                 ? "it hit hard (Rock::ReactToPhysicsImpact)"
                 : "a different call of the game",
           press, g_frame);
  }
  o_RockSplit(self, unused);
}

int __fastcall h_RightPress(void* self, void* unused) {
  if (self == nullptr || self != g_face || !g_owns || g_hand == nullptr) {
    return o_RightPress(self, unused);
  }
  AssertRecord();
  // A MIRACLE IN THE HAND: the press casts on the target with the halo.
  MiracleKeepRecord();
  VillageKeepRecord();
  // One line for each press: the handler runs on each frame while the button
  // is down and no action started.
  const bool fresh = g_frame > g_press_frame + 1;
  g_press_frame = g_frame;
  if (fresh) g_click_press = false;
  // A SCROLL THAT IS NOT ACTIVE YET: the press is a click for the scripts of
  // the game, and nothing more. The game's handler would take the press as a
  // press on the land.
  if (fresh && g_choice != nullptr && g_choice_click &&
      *reinterpret_cast<void* const*>(g_hand + kHandHeld) == nullptr &&
      *reinterpret_cast<void* const*>(g_face + kFaceNextObject) == g_choice) {
    const bool sent = ScrollClick(g_choice);
    g_click_press = sent;
    // The far touch gave its own pulse when the grip closed.
    if (sent && !g_choice_far) HapticsPulse(haptics_math::kActivate);
    ++g_presses;
    if (g_press_logs < kMaxPressLogs) {
      ++g_press_logs;
      const float units = RoomUnits();
      ModLog("hand press   : the grip pressed on the target %p (%s)%s: the "
             "scroll is not active: the game makes it active at the first "
             "click. %s. Its "
             "body is %.1f cm of the room from the Hand's point. Hand state "
             "%d, hand mode 0x%X, rule %d, frame %llu", g_choice,
             ClassName(g_choice),
             g_choice_far ? ", the target of the far touch" : "",
             sent ? "This press is that click (fn_005D3930 of the game), so "
                    "the script that waits for it goes on"
                  : "The click function of the game does not have the bytes "
                    "of the disassembly, so the mod sends no click",
             units > 0.0f
                 ? (g_choice_far ? Distance(g_choice_position, g_target)
                                 : g_choice_gap) / units * 100.0f
                 : 0.0f,
             State(g_hand),
             *reinterpret_cast<const int32_t*>(g_face + kFaceHandMode),
             g_rule, g_frame);
    }
  }
  if (g_click_press) return 1;
  if (fresh) {
    void* next = *reinterpret_cast<void* const*>(g_face + kFaceNextObject);
    void* held = *reinterpret_cast<void* const*>(g_hand + kHandHeld);
    ++g_presses;
    if (next == nullptr && held == nullptr) ++g_presses_none;
    // THE TICK OF THE GRIP, at the press and not some frames later when the
    // object is in the Hand: a take, a tap, or a very faint tick for a press
    // with no target. The press of a fetch from a distance has the ticks of
    // the far grab, and a press with an object in the Hand lets it go.
    if (held == nullptr && !g_choice_far) {
      if (next != nullptr && next == g_choice) {
        if (!g_one_target || g_choice_acts) {
          HapticsPulse(g_choice_kind == near_math::kHold ||
                               g_choice_kind == near_math::kTake
                           ? haptics_math::kTake
                           : haptics_math::kActivate);
        }
      } else if (next == nullptr) {
        HapticsPulse(haptics_math::kNoTarget);
      }
    }
    if (g_press_logs < kMaxPressLogs) {
      ++g_press_logs;
      const float units = RoomUnits();
      char name[48];
      strcpy_s(name, next != nullptr ? ClassName(next) : "nothing");
      if (held != nullptr) {
        ModLog("hand press   : the grip pressed with %p (%s) in the Hand. "
               "The pick record holds %p (%s). Hand state %d, hand mode 0x%X, "
               "frame %llu", held, ClassName(held), next, name, State(g_hand),
               *reinterpret_cast<const int32_t*>(g_face + kFaceHandMode),
               g_frame);
      } else if (next != nullptr && next == g_choice && !g_choice_far) {
        // An object that the physics of the game has. The line of the catch
        // says how the game takes it (ThrowPhysicsCatchPress).
        const uint8_t* bytes = static_cast<const uint8_t*>(next);
        const bool moves = (bytes[kObjectFlags] & 0x40) != 0 &&
                           (bytes[kObjectOtherFlags] & 0x10) == 0;
        ModLog("hand press   : the grip pressed on the target %p (%s): %s%s. "
               "Its body is %.1f cm of the room from the Hand's point%s. Hand "
               "state %d, hand mode 0x%X, rule %d, frame %llu", next, name,
               KindName(g_choice_kind),
               g_one_target && !g_choice_acts
                   ? ", but the land under the Hand is outside your influence"
                   : "",
               units > 0.0f ? g_choice_gap / units * 100.0f : 0.0f,
               moves ? ". The physics of the game has the object: it moves"
                     : "",
               State(g_hand),
               *reinterpret_cast<const int32_t*>(g_face + kFaceHandMode),
               g_rule, g_frame);
      } else {
        // THE NEAREST OBJECT THAT A PRESS ACTS ON, at each press with no
        // target, so a log shows a miss at once: what it is, how far its body
        // is from the point, and the reach that it has.
        const float cm = units > 0.0f ? 100.0f / units : 0.0f;
        char nearest[224];
        if (g_any_object != nullptr) {
          const bool scroll = g_any_kind == near_math::kTap &&
                              IsScrollObject(g_any_object);
          sprintf_s(nearest, "The nearest object that a press acts on is %p "
                             "(%s, %s), %.1f cm of the room from the point, "
                             "and its reach is %d cm",
                    g_any_object, ClassName(g_any_object),
                    KindName(g_any_kind), g_any_gap * cm,
                    scroll && g_scroll_reach_cm > g_pick_cm ? g_scroll_reach_cm
                    : g_any_kind == near_math::kHold && g_near_rule &&
                              g_wide_cm > g_pick_cm
                        ? g_wide_cm
                        : g_pick_cm);
        } else {
          sprintf_s(nearest, "The draw offered %d objects, and a press acts "
                             "on none of them", g_any_count);
        }
        char scroll_text[400] = "";
        if (g_scroll_near >= 0) {
          const Scroll& s = g_scroll_near_copy;
          _snprintf_s(scroll_text, _TRUNCATE,
                      ". The nearest scroll in view is %p (%s, script %d), "
                      "%.1f cm of the room from the point, and its reach is "
                      "%d cm. %s",
                      s.object, ScrollTypeName(s.type), s.script,
                      g_scroll_near_gap * cm,
                      g_scroll_reach_cm > g_pick_cm ? g_scroll_reach_cm
                                                    : g_pick_cm,
                      g_scroll_near_words);
        }
        ModLog("hand press   : the grip pressed with no target at the Hand's "
               "point. The pick record holds %p (%s)%s. %s%s. Hand state %d, "
               "hand mode 0x%X, rule %d, frame %llu", next, name,
               g_choice_far ? ", the object of a fetch from a distance" : "",
               nearest, scroll_text, State(g_hand),
               *reinterpret_cast<const int32_t*>(g_face + kFaceHandMode),
               g_rule, g_frame);
      }
    }
  }
  // A CATCH (docs/grab.md, "A catch at once"). The press put an object in
  // the Hand of the picture that the physics of the game still has
  // (fn_0046DC30): it falls, flies or rolls.
  void* const held_before =
      *reinterpret_cast<void* const*>(g_hand + kHandHeld);
  const int result = o_RightPress(self, unused);
  const uint8_t* held_now =
      *reinterpret_cast<uint8_t* const*>(g_hand + kHandHeld);
  if (held_before == nullptr && held_now != nullptr &&
      !IsBadReadPtr(held_now, kObjectFlags + 1) &&
      (held_now[kObjectFlags] & 0x40) != 0 &&
      (held_now[kObjectOtherFlags] & 0x10) == 0) {
    ThrowPhysicsCatchPress(const_cast<uint8_t*>(held_now), g_choice_far);
  }
  return result;
}

// THE PICK UP AT ONCE (catch_at_once, docs/grab.md, "A catch at once"). The
// step of the pick up action, fn_005D5250 (thiscall on GInterface, no
// arguments, a plain ret), runs on each frame after a press on an object that
// the Hand can hold. For an object that the physics of the game has it only
// waits until the press is 225 ms old on the clock of the game, and then it
// calls fn_005D2800, which sends the pick up (packet 0x13) after its own
// tests. A button that goes up before that is a click. [V]
// While the catch of the object of the record runs, the step gets a press
// that is old enough: the clock and the game turn of the press
// (GInterface+0x454 and +0x458) are 225 ms and 3 game turns back for this one
// call, and then they have their values again. So the game sends its pick up
// on the first step after the press, with each test that it makes, and a
// press on an object that moves is never a click.
typedef int(__fastcall* PickStepFn)(void* self, void* unused);
const uintptr_t kPickStep = 0x005D5250;
const int32_t kActionPickedUp = 7;  // fn_005D2800 starts this action
PickStepFn o_PickStep = nullptr;

int __fastcall h_PickStep(void* self, void* unused) {
  uint8_t* face = static_cast<uint8_t*>(self);
  if (self == nullptr || self != g_face || !g_owns || g_hand == nullptr) {
    return o_PickStep(self, unused);
  }
  uint8_t* object = *reinterpret_cast<uint8_t* const*>(face + kFaceNextObject);
  const uint8_t* game = *reinterpret_cast<uint8_t* const*>(kGame);
  if (object == nullptr || game == nullptr ||
      !ThrowPhysicsCatchActs(object) ||
      IsBadReadPtr(object, kObjectFlags + 1) ||
      (object[kObjectFlags] & 0x40) == 0 ||
      (object[kObjectOtherFlags] & 0x10) != 0) {
    return o_PickStep(self, unused);
  }
  uint32_t* press_clock = reinterpret_cast<uint32_t*>(face + kFacePressClock);
  uint32_t* press_turn = reinterpret_cast<uint32_t*>(face + kFacePressTurn);
  const uint32_t clock = *reinterpret_cast<const uint32_t*>(kGameClock);
  const uint32_t turn = *reinterpret_cast<const uint32_t*>(game + kGameTurn);
  const uint32_t age =
      controller_math::GamePressAge(clock, *press_clock, turn, *press_turn);
  if (age >= controller_math::kGripClickMs) return o_PickStep(self, unused);
  const uint32_t kept_clock = *press_clock;
  const uint32_t kept_turn = *press_turn;
  catch_math::OldPress(clock, turn,
                       static_cast<uint32_t>(controller_math::kGripClickMs),
                       press_clock, press_turn);
  const int result = o_PickStep(self, unused);
  *press_clock = kept_clock;
  *press_turn = kept_turn;
  ThrowPhysicsCatchSent(
      object, age,
      *reinterpret_cast<const int32_t*>(face + kFaceAction) == kActionPickedUp);
  return result;
}

// THE LEASH AT THE MOD'S HAND (docs/grab.md, "The leash at the mod's hand").
// The game ends the rope of the leash at the wrist bone of its own Hand (see
// kGetLeashStartPoint). The exact Hand puts that bone at the real wrist. But
// the mesh of the game's Hand is hidden, and the player holds the rope in the
// mod's hand, so a rope that ends at the wrist joint hangs behind and below
// the hand that he sees. While the mod's hand stands for the game's Hand, the
// rope ends at the middle of the palm of the mod's hand. The point is made
// once a frame, so each call of a frame gets the same point. With the mouse,
// with the game's mesh shown, for the leash of another player and for a leash
// that is tied to an object, the game's point stays.
void __fastcall h_LeashStart(void* self, void* unused, float* out,
                             int from_status) {
  o_LeashStart(self, unused, out, from_status);
  if (!g_leash_palm || out == nullptr || self == nullptr || !g_owns ||
      self != g_face || g_hand == nullptr || (from_status & 0xFF) != 0) {
    return;
  }
  const uint8_t* status = *reinterpret_cast<uint8_t* const*>(
      static_cast<uint8_t*>(self) + kFaceStatus);
  if (status == nullptr) return;
  const uint8_t* leash =
      *reinterpret_cast<uint8_t* const*>(status + kStatusLeash);
  if (leash == nullptr ||
      *reinterpret_cast<void* const*>(leash + kLeashObject) != nullptr) {
    return;
  }
  if (g_leash_frame != g_frame) {
    g_leash_frame = g_frame;
    uint64_t root_frame = 0;
    int32_t root_state = -1;
    float palm[3];
    // The root is from the compose of this frame. When the game asks before
    // the Hand state ran in a frame, it is from the frame before.
    g_leash_have = HandExactPalm(palm, &root_frame, &root_state) &&
                   root_frame + 1 >= g_frame && root_frame <= g_frame &&
                   root_state == State(g_hand);
    if (g_leash_have) {
      for (int i = 0; i < 3; ++i) g_leash_point[i] = palm[i];
    } else if (g_leash_miss_logs < kMaxLeashMissLogs &&
               g_frame > g_leash_miss_frame + 450) {
      // Why the rope stays at the game's Hand, so a log shows it.
      ++g_leash_miss_logs;
      g_leash_miss_frame = g_frame;
      ModLog("leash        : the rope stays at the game's Hand: the mod's "
             "hand does not stand for it now (the root of the exact Hand is "
             "from frame %llu in state %d, and this is frame %llu in state "
             "%d)", root_frame, root_state, g_frame, State(g_hand));
    }
  }
  if (!g_leash_have) return;
  // One line for each time the leash comes on: a call after a second with no
  // call.
  const bool fresh = !g_leash_said || g_frame > g_leash_last + 90;
  g_leash_said = true;
  g_leash_last = g_frame;
  ++g_leash_moves;
  if (fresh && g_leash_logs < kMaxLeashLogs) {
    ++g_leash_logs;
    const float units = RoomUnits();
    ModLog("leash        : the rope ends at the palm of the mod's hand, %.1f "
           "%.1f %.1f. The game's point, the wrist bone of its Hand, is %.1f "
           "cm of the room from it and %.1f cm lower. The Hand's point (the "
           "glow) is %.1f cm from the palm. Hand state %d, frame %llu",
           g_leash_point[0], g_leash_point[1], g_leash_point[2],
           units > 0.0f ? Distance(out, g_leash_point) / units * 100.0f : 0.0f,
           units > 0.0f ? (g_leash_point[1] - out[1]) / units * 100.0f : 0.0f,
           units > 0.0f ? Distance(g_target, g_leash_point) / units * 100.0f
                        : 0.0f,
           State(g_hand), g_frame);
  }
  for (int i = 0; i < 3; ++i) out[i] = g_leash_point[i];
}

// WHAT A PRESS OF THE GRIP DOES AT THE HAND'S POINT, as a near_math::Kind,
// from the game's own hand mode (the returns of fn_005D7F20). The mode 0x12
// is "an object that the Hand cannot take". The game gives it for a scroll
// and for a house, where a press taps. It gives it also where a press does
// nothing: the Temple, and each object outside the influence that needs the
// influence. So for 0x12 the object of the pick record and the influence at
// the record's land point (GInterface+0x48) decide, as in fn_005D38A0. With
// the rule off, 0x12 is a tap.
//
// A FAR TARGET HIDES THE GAME'S PICK. While the aim ray has a target that the
// grip can take, a grip that closes goes to that target (FarGrabPumpGrip). So
// the point shows nothing then, unless the mod has its own choice at the
// point, which turns the far target off.
int PressKindAtPoint(int32_t mode) {
  int kind = near_math::kNone;
  // ONE TARGET: the mod's choice is the object of a press, and the look
  // comes from it and not from the game's hand mode, which is one frame
  // late. With no choice only a bubble and a leash are left: the game finds
  // them at the pixel of the Hand's point (0x1C and 0x20).
  if (g_one_target && State(g_hand) != kStateCreature) {
    if (g_choice != nullptr && !g_choice_far) {
      return g_choice_acts ? g_choice_kind : near_math::kNone;
    }
    kind = mode == 0x1C || mode == 0x20 ? near_math::kTap : near_math::kNone;
    if (kind != near_math::kNone) {
      FarDraw far_draw = {};
      if (FarGrabDrawState(&far_draw) && far_draw.active && far_draw.valid) {
        kind = near_math::kNone;
      }
    }
    return kind;
  }
  switch (mode) {
    case 0x09:
      kind = near_math::kHold;
      break;
    case 0x0D:
      kind = near_math::kTake;
      break;
    case 0x13:
      kind = near_math::kCreature;
      break;
    case 0x0F:
    case 0x10:
    case 0x1C:
    case 0x20:
      kind = near_math::kTap;
      break;
    case 0x12: {
      kind = near_math::kTap;
      if (!g_near_rule) break;
      void* object = *reinterpret_cast<void* const*>(g_face + kFaceObject);
      if (object == nullptr) {
        object = *reinterpret_cast<void* const*>(g_face + kFaceNextObject);
      }
      void* status = *reinterpret_cast<void* const*>(g_face + kFaceStatus);
      if (object == nullptr) {
        kind = near_math::kNone;
      } else if (IsCreatureObject(object)) {
        kind = near_math::kCreature;
      } else if (!Tappable(object, status) ||
                 (*reinterpret_cast<const int32_t*>(g_face + kFaceInfluence) ==
                      0 &&
                  reinterpret_cast<VirtualIntFn>(
                      VTable(object)[kMustBeInInfluence / 4])(object,
                                                              nullptr) != 0)) {
        kind = near_math::kNone;
      }
      break;
    }
    default:
      break;
  }
  if (kind != near_math::kNone && g_near_rule && g_choice == nullptr &&
      State(g_hand) != kStateCreature) {
    FarDraw far_draw = {};
    if (FarGrabDrawState(&far_draw) && far_draw.active && far_draw.valid) {
      kind = near_math::kNone;
    }
  }
  return kind;
}

// The halo of the target at the Hand: what both eye passes of this frame
// draw. Only for the empty Hand in its Normal state, with the choice of the
// mod at the Hand's point.
void LatchNearTarget() {
  g_near_target = g_one_target && g_glow_valid && g_choice != nullptr &&
                  !g_choice_far && State(g_hand) == kStateNormal &&
                  HandEmpty();
  g_near_target_object = g_near_target ? g_choice : nullptr;
  if (!g_near_target) return;
  for (int i = 0; i < 3; ++i) g_near_target_center[i] = g_choice_center[i];
  g_near_target_radius = g_choice_radius;
  g_near_target_half = g_choice_half;
  g_near_target_acts = g_choice_acts;
}

// THE PICK KEEPS THE CREATURE. In the Creature state the interface gives the
// hand mode 0x13 only while the next pick record holds the Creature
// (fn_005D80C0). Another object gives 0xE and no object gives 3, and both end
// the state. A slap goes through the Creature and out, and a Hand pulled back
// to the body leaves it, so the pixel of the Hand does not stay on it. So
// while the controller places the Hand in the Creature state, the next
// record keeps the Creature, after each eye pass. The state then lasts until
// the trigger opens.
void KeepCreature() {
  if (!g_active || g_face == nullptr || g_hand == nullptr ||
      State(g_hand) != kStateCreature || !HandEmpty()) {
    return;
  }
  void* creature = TouchObject(g_hand);
  if (creature == nullptr) return;
  if (*reinterpret_cast<void* const*>(g_face + kFaceNextObject) == creature) {
    return;
  }
  const RecordPickFn record_pick = reinterpret_cast<RecordPickFn>(kRecordPick);
  record_pick(g_face + kFaceNextRecord, nullptr, creature,
              Distance(g_target, Camera()));
  ++g_touch_kept;
}

// The test log: once the target has stood still for kSettleFrames, the
// target, the Hand's position and the pose builder's copy.
void LogCheck() {
  if (g_check_logs >= kMaxCheckLogs || g_hand == nullptr) return;
  // Only the states the direct hand places. The others do not follow it.
  if (!DirectState(g_hand)) {
    g_settle = -1;
    for (int i = 0; i < 3; ++i) g_logged_target[i] = 1e30f;
    return;
  }
  if (Distance(g_target, g_logged_target) > 0.01f) {
    for (int i = 0; i < 3; ++i) g_logged_target[i] = g_target[i];
    g_settle = kSettleFrames;
    return;
  }
  if (g_settle < 0) return;
  if (g_settle > 0) {
    --g_settle;
    return;
  }
  g_settle = -1;
  ++g_check_logs;
  const float* at = reinterpret_cast<const float*>(g_hand + kHandPosition);
  const float* world = reinterpret_cast<const float*>(kHandWorld);
  const float scale = HeadPoseScale();
  ModLog("hand         : target %.3f %.3f %.3f, hand+0x78 %.3f %.3f %.3f, "
         "0x00C5E680 %.3f %.3f %.3f", g_target[0], g_target[1], g_target[2],
         at[0], at[1], at[2], world[0], world[1], world[2]);
  // The height of the point over the land, in cm of the room at the scale
  // now, and how far the controller is under it.
  const float table = TabletopViewScale();
  const float room = table > 0.0f ? table : scale;
  if (room > 0.0f) {
    ModLog("hand         : the point is %.2f cm of the room over the land "
           "(land %.3f), the controller %.2f cm under the point, at %.1f "
           "units per meter", (g_target[1] - g_land_y) / room * 100.0f,
           g_land_y, (g_target[1] - g_point[1]) / room * 100.0f, room);
  }
  ModLog("hand         : off by %.4f and %.4f units, state %d, depth %.2f, "
         "size %.3f, real %.1f cm, reach x%.2f, %s, object now %p, held %p",
         Distance(g_target, at), Distance(g_target, world), State(g_hand),
         *reinterpret_cast<const float*>(g_hand + kHandDistance),
         *reinterpret_cast<const float*>(g_hand + kHandScale),
         scale > 0.0f ? g_real_distance / scale * 100.0f : 0.0f, g_factor,
         g_on_land ? "held up by the land" : "free",
         g_face != nullptr
             ? *reinterpret_cast<void* const*>(g_face + kFaceObject)
             : nullptr,
         *reinterpret_cast<void* const*>(g_hand + kHandHeld));
}

// A TREE IN THE HAND AND ITS RELEASE (docs/grab.md, "A tree in the Hand").
// Once a frame, while the Hand holds a tree, this works out what the opening
// of the grip does with the same tests as the game (tree_plant_math.h) and
// latches the answer for the hint card. After the release it logs what the
// tree did. It also logs each change of the roots piles of the game, the
// pieces of mesh that the Tug of a tree puts in the land.
// tree_plant_hint=0 keeps the text of the game. Ctrl and Page Down
// (test_keys=1) switch it. tree_plant_bodies=1 adds the test of a body in the
// way (tree_body.cpp) to the tests of the answer. 0 leaves it out.
typedef void(__fastcall* GetYXZFn)(const void* matrix, void*, float* y,
                                   float* x, float* z);
typedef int(__fastcall* CoordsTestFn)(const int32_t* coords, void*);
const uintptr_t kGetYXZ = 0x007FAB30;         // LHMatrix::GetYXZ
const uintptr_t kIsDryLand = 0x00603620;      // MapCoords::IsDryLand
const uintptr_t kRootsPileList = 0x00EB9A00;  // the head of the roots piles
const int kTreeBurning = 0x44;                // Tree: nonzero when on fire
const int kTreeFlags = 0x24;                  // Object: bit 0x40 is "in physics"
const int kTreeForest = 0x68;                 // Tree: its Forest

int g_tree_hint = 1;
int g_tree_bodies = 1;  // tree_plant_bodies
bool g_tree_hint_read = false;
int g_tree_verdict = -1;  // -1: no tree in the Hand now
uint64_t g_tree_frame = 0;
float g_tree_height_cm = 0.0f;
void* g_tree_held = nullptr;
int g_tree_held_verdict = -1;
void* g_tree_watch = nullptr;  // the tree that the Hand let go
int g_tree_watch_verdict = -1;
bool g_tree_watch_physics = false;
float g_tree_watch_height_cm = 0.0f;  // the Hand's point before the release
float g_tree_lean[2] = {0.0f, 0.0f};  // the lean of the tree in the Hand
uint64_t g_tree_watch_start = 0;
int g_roots_count = -1;
const uint8_t* g_roots_head = nullptr;
int g_roots_at_tree = 1;  // far_roots_at_tree, and its test key
bool g_roots_read = false;
int g_tree_logs = 0;

void ReadTreeHintKey() {
  if (g_tree_hint_read) return;
  g_tree_hint_read = true;
  g_roots_at_tree = IniKey("far_roots_at_tree", 1) != 0 ? 1 : 0;
  ModLog("hand tree    : far_roots_at_tree=%d. %s", g_roots_at_tree,
         g_roots_at_tree != 0
             ? "The roots pile of a tree that comes from a distance stands at "
               "the roots, on the land where the tree stood"
             : "The roots pile stays where the game puts it, on the way of the "
               "glide");
  g_tree_hint = IniKey("tree_plant_hint", 1) != 0 ? 1 : 0;
  ModLog("hand tree    : tree_plant_hint=%d. %s", g_tree_hint,
         g_tree_hint != 0
             ? "The card says what the opening of the grip does with a tree "
               "in the Hand, by the tests of the game and the height rule"
             : "The card shows the text of the game for a tree in the Hand");
  g_tree_bodies = IniKey("tree_plant_bodies", 1) != 0 ? 1 : 0;
  ModLog("hand tree    : tree_plant_bodies=%d. %s", g_tree_bodies,
         g_tree_bodies != 0
             ? "The answer for a tree in the Hand has the test of the game for "
               "a body in the way of the tree"
             : "The answer for a tree in the Hand has no test of a body in the "
               "way of the tree");
}

// The roots piles of the game: a list of entries (next, LH3DObject, life).
void WatchRootsPiles() {
  const void* const* head =
      reinterpret_cast<const void* const*>(kRootsPileList);
  if (IsBadReadPtr(head, sizeof(void*))) return;
  int count = 0;
  const uint8_t* newest = static_cast<const uint8_t*>(*head);
  for (const uint8_t* e = newest; e != nullptr && count < 64; ++count) {
    if (IsBadReadPtr(e, 12)) break;
    e = *reinterpret_cast<const uint8_t* const*>(e);
  }
  if (newest == g_roots_head && count == g_roots_count) return;
  const bool added = newest != g_roots_head && count > 0 && newest != nullptr;
  g_roots_head = newest;
  g_roots_count = count;
  float* at = nullptr;
  if (newest != nullptr && !IsBadReadPtr(newest, 12)) {
    uint8_t* mesh = *reinterpret_cast<uint8_t* const*>(newest + 4);
    if (mesh != nullptr && !IsBadWritePtr(mesh + kMeshTranslation, 12)) {
      at = reinterpret_cast<float*>(mesh + kMeshTranslation);
    }
  }
  float was[3] = {0.0f, 0.0f, 0.0f};
  if (at != nullptr) {
    for (int i = 0; i < 3; ++i) was[i] = at[i];
  }
  // A far grab: the Tug of the game breaks the roots after the glide has begun
  // to move the tree, so the new pile stands in the air on the way. The pile
  // belongs at the roots, on the land where the tree stood.
  void* glider = nullptr;
  float origin[3];
  bool moved = false;
  if (added && at != nullptr && g_roots_at_tree != 0 &&
      FarGrabGlideOrigin(&glider, origin) && IsTreeObject(glider)) {
    for (int i = 0; i < 3; ++i) at[i] = origin[i];
    moved = true;
  }
  if (g_tree_logs >= 80) return;
  ++g_tree_logs;
  if (moved) {
    ModLog("hand tree    : the game put a roots pile at %.1f %.1f %.1f, on the "
           "way of the glide of the tree %p. far_roots_at_tree=1 moved it to "
           "%.1f %.1f %.1f, where the tree stood. Piles now %d, frame %llu",
           was[0], was[1], was[2], glider, origin[0], origin[1], origin[2],
           count, g_frame);
  } else {
    ModLog("hand tree    : the game has %d roots piles now, the newest at %.1f "
           "%.1f %.1f%s, frame %llu", count, was[0], was[1], was[2],
           added && g_roots_at_tree == 0 && FarGrabGlideOrigin(&glider, origin)
               ? " (far_roots_at_tree=0: it stays where the game put it)"
               : "",
           g_frame);
  }
}

// THE BODIES THAT THE RELEASE COMPARED WITH THE TREE, for the log. The game's
// physics is an array of 0x1DC bytes an entry at *0x00D47814, with the count
// at 0x00D47818 (throw_physics.cpp). An entry has its Object at +0x18, its
// center at +0xC8, its radius at +0x178 and its flags at +0x1D8.
// PhysicsObject::RaiseUntilNotIntersecting 0x00644800 compares the tree with
// each other entry whose center is nearer than the sum of the two radii. The
// release sets the flag 8 of the tree only when it did not raise the tree:
// with that flag the landing plants the tree.
void LogTreeBodies(void* tree) {
  const uintptr_t kArray = 0x00D47814, kCount = 0x00D47818;
  const int kEntry = 0x1DC, kEntryObject = 0x18, kEntryCenter = 0xC8;
  const int kEntryRadius = 0x178, kEntryFlags = 0x1D8;
  const uint8_t* base = *reinterpret_cast<const uint8_t* const*>(kArray);
  const int32_t count = *reinterpret_cast<const int32_t*>(kCount);
  if (base == nullptr || count <= 0 || count > 4096 ||
      IsBadReadPtr(base, static_cast<size_t>(count) * kEntry)) {
    return;
  }
  const uint8_t* own = nullptr;
  for (int i = 0; i < count; ++i) {
    const uint8_t* entry = base + i * kEntry;
    if (*reinterpret_cast<void* const*>(entry + kEntryObject) == tree) {
      own = entry;
    }
  }
  if (own == nullptr) {
    ModLog("hand tree    : the physics of the game has no entry for the tree "
           "%p, frame %llu", tree, g_frame);
    return;
  }
  const float* at = reinterpret_cast<const float*>(own + kEntryCenter);
  const float radius = *reinterpret_cast<const float*>(own + kEntryRadius);
  int near_count = 0;
  float nearest = 1e30f;
  void* nearest_object = nullptr;
  for (int i = 0; i < count; ++i) {
    const uint8_t* entry = base + i * kEntry;
    if (entry == own) continue;
    const float* p = reinterpret_cast<const float*>(entry + kEntryCenter);
    const float reach =
        radius + *reinterpret_cast<const float*>(entry + kEntryRadius);
    const float gap = Distance(at, p);
    if (!(gap < reach)) continue;
    ++near_count;
    if (gap < nearest) {
      nearest = gap;
      nearest_object = *reinterpret_cast<void* const*>(entry + kEntryObject);
    }
  }
  const uint32_t flags = *reinterpret_cast<const uint32_t*>(own + kEntryFlags);
  char name[48] = "nothing";
  if (nearest_object != nullptr && !IsBadReadPtr(nearest_object, 0x44)) {
    strcpy_s(name, ClassName(nearest_object));
  }
  ModLog("hand tree    : the physics of the game has %d objects, and %d of "
         "them have their center nearer to the tree than the sum of the two "
         "radii (the radius of the tree is %.2f units). The nearest is %p "
         "(%s), %.2f units from the center of the tree. The flags of the "
         "tree are 0x%X: the flag 8 (the landing plants the tree) is %s",
         count, near_count, radius, nearest_object, name,
         near_count > 0 ? nearest : 0.0f, flags,
         (flags & 8) != 0 ? "set" : "not set");
}

void TreeReleaseFrame() {
  ReadTreeHintKey();
  WatchRootsPiles();
  if (g_tree_frame == g_frame) return;
  g_tree_frame = g_frame;
  g_tree_verdict = -1;
  void* held = g_hand != nullptr
                   ? *reinterpret_cast<void* const*>(g_hand + kHandHeld)
                   : nullptr;
  const bool holding_tree =
      g_hand != nullptr && State(g_hand) == kStateHolding && held != nullptr &&
      !IsBadReadPtr(held, 0x70) && IsTreeObject(held) && !IsForest(held);
  if (holding_tree) {
    uint8_t* object = static_cast<uint8_t*>(held);
    const uint8_t* mesh =
        *reinterpret_cast<const uint8_t* const*>(object + 0x40);
    float y = 0.0f, lean_x = 0.0f, lean_z = 0.0f;
    if (mesh != nullptr && !IsBadReadPtr(mesh, 0x50)) {
      reinterpret_cast<GetYXZFn>(kGetYXZ)(mesh + 0x14, nullptr, &y, &lean_x,
                                          &lean_z);
    }
    const float table = TabletopViewScale();
    float height_m = 0.0f;
    if (table > 0.0f && !VillageTakesObject()) {
      height_m = (g_target[1] - g_land_y) / table;
    }
    const int32_t coords[3] = {static_cast<int32_t>(g_target[0] * 6553.6f),
                               static_cast<int32_t>(g_target[2] * 6553.6f), 0};
    const bool dry =
        reinterpret_cast<CoordsTestFn>(kIsDryLand)(coords, nullptr) != 0;
    const bool burning =
        *reinterpret_cast<const int32_t*>(object + kTreeBurning) != 0;
    g_tree_verdict = tree_plant_math::ReleaseVerdict(
        height_m, ThrowPhysicsFallLimitM(), lean_x, lean_z, dry, burning);
    // The release raises the tree over a body in its way, and then it does
    // not plant the tree (docs/grab.md, "The test of a body in the way").
    bool in_way = false;
    if (g_tree_verdict == tree_plant_math::kPlants && g_tree_bodies != 0) {
      TreeBodyAnswer answer;
      if (TreeBodyTest(held, &answer) && answer.in_way) {
        in_way = true;
        g_tree_verdict = tree_plant_math::kInTheWay;
      }
    }
    g_tree_height_cm = height_m * 100.0f;
    g_tree_lean[0] = lean_x;
    g_tree_lean[1] = lean_z;
    if (held != g_tree_held || g_tree_verdict != g_tree_held_verdict) {
      g_tree_held_verdict = g_tree_verdict;
      if (g_tree_logs < 80) {
        ++g_tree_logs;
        ModLog("hand tree    : the tree %p in the Hand: an opening of the grip "
               "now %s (the hand is %.1f cm over the land and the limit is "
               "%.0f cm, the tree leans %.2f and %.2f rad, dry land %d, fire "
               "%d, a body in the way %d), frame %llu",
               held,
               g_tree_verdict == tree_plant_math::kPlants
                   ? "plants it"
                   : "does not plant it, and the tree falls",
               g_tree_height_cm, ThrowPhysicsFallLimitM() * 100.0f, lean_x,
               lean_z, dry ? 1 : 0, burning ? 1 : 0, in_way ? 1 : 0, g_frame);
      }
    }
    g_tree_held = held;
    g_tree_watch = nullptr;
    return;
  }
  // The Hand let go of a tree: watch it until it leaves the physics.
  if (g_tree_held != nullptr && held == g_tree_held) return;  // in the Tug
  if (g_tree_held != nullptr) {
    g_tree_watch = g_tree_held;
    g_tree_watch_verdict = g_tree_held_verdict;
    g_tree_watch_height_cm = g_tree_height_cm;
    g_tree_watch_physics = false;
    g_tree_watch_start = g_frame;
    if (g_tree_logs < 80) {
      ++g_tree_logs;
      ModLog("hand tree    : the Hand let go of the tree %p, frame %llu. The "
             "card said %s before the release, and the tree leaned %.2f and "
             "%.2f rad on its last frame in the Hand. The mod watches the "
             "tree until it leaves the physics", g_tree_watch, g_frame,
             g_tree_watch_verdict == tree_plant_math::kPlants ? "plant"
                                                               : "it falls",
             g_tree_lean[0], g_tree_lean[1]);
    }
    g_tree_held = nullptr;
    g_tree_held_verdict = -1;
  }
  if (g_tree_watch != nullptr) {
    uint8_t* object = static_cast<uint8_t*>(g_tree_watch);
    if (IsBadReadPtr(object, 0x70)) {
      ModLog("hand tree    : the tree %p can not be read now, frame %llu",
             g_tree_watch, g_frame);
      g_tree_watch = nullptr;
      return;
    }
    const bool physics =
        (*reinterpret_cast<const uint16_t*>(object + kTreeFlags) & 0x40) != 0;
    if (physics && !g_tree_watch_physics && g_tree_logs < 80) {
      ++g_tree_logs;
      // The release puts a tree that the Hand sets down on the land, and
      // then raises it until it is in no other body (docs/grab.md, "The
      // test of a body in the way"). So a mesh that is over the land now,
      // after a set down, is a tree that the game raised.
      float over = 0.0f, y = 0.0f, lean_x = 0.0f, lean_z = 0.0f;
      const uint8_t* mesh =
          *reinterpret_cast<const uint8_t* const*>(object + kObjectMesh);
      if (mesh != nullptr && !IsBadReadPtr(mesh, 0x50)) {
        const float* at =
            reinterpret_cast<const float*>(mesh + kMeshTranslation);
        const int32_t coords[3] = {static_cast<int32_t>(at[0] * kMapScale),
                                   static_cast<int32_t>(at[2] * kMapScale), 0};
        over = at[1] - reinterpret_cast<GetAltitudeFn>(kGetAltitude)(coords);
        reinterpret_cast<GetYXZFn>(kGetYXZ)(mesh + 0x14, nullptr, &y, &lean_x,
                                            &lean_z);
      }
      ModLog("hand tree    : the tree %p is in the physics, frame %llu. Its "
             "mesh is %.2f units over the land now and leans %.2f and %.2f "
             "rad, and the Hand's point was %.1f cm of the room over the "
             "land before the release",
             g_tree_watch, g_frame, over, lean_x, lean_z,
             g_tree_watch_height_cm);
      LogTreeBodies(g_tree_watch);
      // The bodies of the last test, beside the bodies that the game made.
      if (g_tree_bodies != 0 &&
          (g_tree_watch_verdict == tree_plant_math::kPlants ||
           g_tree_watch_verdict == tree_plant_math::kInTheWay)) {
        TreeBodyLogAfterRelease(g_tree_watch);
      }
    }
    if (physics) g_tree_watch_physics = true;
    static void* said = nullptr;
    if (g_frame - g_tree_watch_start >= 25 && said != g_tree_watch &&
        g_tree_logs < 80) {
      said = g_tree_watch;
      ++g_tree_logs;
      ModLog("hand tree    : 25 frames after the release the tree %p is in the "
             "physics %d, its forest is %p (a forest means that it is "
             "planted), class %s", g_tree_watch, physics ? 1 : 0,
             *reinterpret_cast<void* const*>(object + kTreeForest),
             ClassName(g_tree_watch));
    }
    if (!physics && g_tree_watch_physics) {
      void* forest = *reinterpret_cast<void* const*>(object + kTreeForest);
      if (g_tree_logs < 80) {
        ++g_tree_logs;
        ModLog("hand tree    : the tree %p is out of the physics %llu frames "
               "after the release. The card said %s. Class now %s, forest %p "
               "(a forest means that it is planted), frame %llu",
               g_tree_watch, g_frame - g_tree_watch_start,
               g_tree_watch_verdict == tree_plant_math::kPlants ? "plant"
                                                                 : "it falls",
               ClassName(g_tree_watch), forest, g_frame);
      }
      g_tree_watch = nullptr;
    } else if (g_frame - g_tree_watch_start > 1500) {
      g_tree_watch = nullptr;
    }
  }
}

// THE TEST LOG OF A FETCH. On each frame of a pull or a fetch: the Hand's
// state and the interface hand mode, the object under the Hand now (+0x3C8),
// the next record (+0x400), the object in the Hand, the Hand's position
// (+0x78) and the mesh translation of the object. This shows what the game's
// Tug state does with an object far away.
int g_fetch_logs = 0;
const int kMaxFetchLogs = 120;
void LogFetch() {
  void* object = nullptr;
  float at[3], land[2];
  if (!FarGrabFetching(&object, at, land)) return;
  if (g_fetch_logs >= kMaxFetchLogs || g_hand == nullptr || g_face == nullptr) {
    return;
  }
  if (!FarTargetLives()) return;
  ++g_fetch_logs;
  const uint8_t* mesh_link = *reinterpret_cast<uint8_t* const*>(
      static_cast<uint8_t*>(object) + kObjectMesh);
  const float* mesh = mesh_link != nullptr
                          ? reinterpret_cast<const float*>(mesh_link +
                                                           kMeshTranslation)
                          : nullptr;
  const float* hand_at = reinterpret_cast<const float*>(g_hand + kHandPosition);
  const int32_t* land_now =
      reinterpret_cast<const int32_t*>(g_face + kFaceNextLand);
  ModLog("far grab    : fetch frame %llu: Hand state %d, hand mode 0x%X, "
         "now %p, record %p, held %p, bit 0x10 of the pick lock %d, +0x48 %d, "
         "Hand at %.1f %.1f %.1f, the object's mesh at %s%.1f %.1f %.1f, land "
         "point %.1f %.1f", g_frame, State(g_hand),
         *reinterpret_cast<const int32_t*>(g_face + kFaceHandMode),
         *reinterpret_cast<void* const*>(g_face + kFaceObject),
         *reinterpret_cast<void* const*>(g_face + kFaceNextObject),
         *reinterpret_cast<void* const*>(g_hand + kHandHeld),
         (g_face[kFaceLocked] & 0x10) != 0,
         *reinterpret_cast<const int32_t*>(g_face + 0x48), hand_at[0],
         hand_at[1], hand_at[2], mesh != nullptr ? "" : "(none) ",
         mesh != nullptr ? mesh[0] : 0.0f, mesh != nullptr ? mesh[1] : 0.0f,
         mesh != nullptr ? mesh[2] : 0.0f, land_now[0] / kMapScale,
         land_now[1] / kMapScale);
}

// What the pick did, as the game sees it: the object under the Hand now, and
// the object in it.
void LogPickState() {
  if (g_face == nullptr || g_hand == nullptr || g_pick_logs >= kMaxPickLogs ||
      !DirectState(g_hand)) {
    return;
  }
  void* now = *reinterpret_cast<void* const*>(g_face + kFaceObject);
  void* held = *reinterpret_cast<void* const*>(g_hand + kHandHeld);
  if (now != g_logged_now) {
    ++g_pick_logs;
    ModLog("hand pick    : the game has %p under the Hand (+0x3C8), frame "
           "%llu", now, g_frame);
    g_logged_now = now;
  }
  if (held != g_logged_held && g_pick_logs < kMaxPickLogs) {
    ++g_pick_logs;
    ModLog("hand pick    : the Hand holds %p (+0x4904), frame %llu", held,
           g_frame);
    g_logged_held = held;
  }
}

bool Hook(uintptr_t address, void* replacement, void** original,
          const char* name) {
  void* target = reinterpret_cast<void*>(address);
  MH_STATUS status = MH_CreateHook(target, replacement, original);
  if (status == MH_OK) status = MH_EnableHook(target);
  ModLog("hooks        : %s at 0x%08X %s", name, address,
         status == MH_OK ? "ok" : "FAILED");
  return status == MH_OK;
}

// THE GRAB FROM A DISTANCE is off in multiplayer: the mod's own pick of an
// object far away is not a thing that other players can follow [G]. The flag
// of -MULTIPLAYER, and the game mode 3 of GGame.
bool Multiplayer() {
  bool on = *reinterpret_cast<const uint8_t*>(kMultiplayerFlag) != 0;
  void* const* game = reinterpret_cast<void* const*>(kGame);
  if (!IsBadReadPtr(game, sizeof(void*)) && *game != nullptr) {
    on |= *reinterpret_cast<const int32_t*>(static_cast<const uint8_t*>(*game) +
                                            kGameMode) == kModeMultiplayer;
  }
  if (on && !g_said_multiplayer) {
    g_said_multiplayer = true;
    ModLog("far grab    : this is a multiplayer game, so the grab from a "
           "distance is off");
  }
  return on;
}

// TEST (far_test_take_gone=N, for the desktop runs only). The game deletes a
// pile that is used up in the game turn in which it ends the take, with the
// grip still closed. The test makes that case with a pile that is not used
// up. At game turn N of a take at a distance it asks for the end of the take
// on each frame, as the grip machine does for a take that still runs with
// the button up (EndTakeNow). The grip stays closed. In the frame in which
// the take is over, the far grab gets the answer that its source is gone.
// Before the far grab reads the frame, as the test that the target lives.
void FarTestTakeGone() {
  g_take_end_test = false;
  if (g_far_test_take_gone <= 0 || g_face == nullptr) return;
  if (!FarGrabTaking()) {
    g_far_test_take_asked = false;
    return;
  }
  if (TakeObject(g_face) != nullptr) {
    if (g_take_turns < g_far_test_take_gone) return;
    if (!g_far_test_take_asked) {
      g_far_test_take_asked = true;
      ModLog("far grab    : test key far_test_take_gone=%d: the take at a "
             "distance is at game turn %d, so the test asks for its end with "
             "the grip closed, frame %llu", g_far_test_take_gone,
             g_take_turns, g_frame);
    }
    g_take_end_test = true;
    if (g_owns) ++g_take_end_asks;
    return;
  }
  if (!g_far_test_take_asked) return;
  g_far_test_take_asked = false;
  ModLog("far grab    : test key far_test_take_gone: the take is over, and "
         "the far grab gets the answer that its source is gone, as for a "
         "pile that the game deletes, frame %llu", g_frame);
  FarGrabGone();
}

// The grab from a distance, once a frame: the facts of the frame go to
// far_grab.cpp, and the cone and the selection latch what they need.
void FarFrameStep(bool active) {
  g_far_ready = false;
  g_far_offer = false;
  g_far_ray = false;
  FarTestTakeGone();
  // THE TARGET LIVES, or the far grab lets it go now. The mesh of a locked
  // target stays in the pick of this frame.
  g_far_keep_model = nullptr;
  if (FarTargetLives() && FarGrabLocked()) {
    void* locked = FarGrabTargetObject();
    g_far_keep_model = *reinterpret_cast<uint8_t* const*>(
        static_cast<uint8_t*>(locked) + kObjectMesh);
  }
  FarFrame f = {};
  f.frame = g_frame;
  f.now = NowSeconds();
  f.why = "the controller does not place the Hand";
  f.held = g_hand != nullptr
               ? *reinterpret_cast<void* const*>(g_hand + kHandHeld)
               : nullptr;
  f.hand_state = g_hand != nullptr ? State(g_hand) : 0;
  f.take = g_face != nullptr ? TakeObject(g_face) : nullptr;
  const float life = HeadPoseScale();
  if (active && FarGrabOn()) {
    f.usable = true;
    if (!TabletopOwnsView()) {
      f.usable = false;
      f.why = "the God view or the table does not own the view";
    } else if (Multiplayer()) {
      f.usable = false;
      f.why = "this is a multiplayer game";
    } else if (!g_table_hooked) {
      f.usable = false;
      f.why = "the cone is not installed";
    }
  }
  if (f.usable) {
    f.ready = true;
    if (State(g_hand) != kStateNormal) {
      f.ready = false;
      f.why = "the Hand is not in its Normal state";
    } else if (!HandEmpty()) {
      f.ready = false;
      f.why = "the Hand holds an object";
    } else if (!ControllersFarAllowed(&f.why)) {
      f.ready = false;
    }
    float units = 0.0f;
    float eye[3] = {0.0f, 0.0f, 0.0f};
    f.have_ray = ControllersFarRay(f.ray_origin, f.ray_direction, &units, eye);
    for (int i = 0; i < 3; ++i) g_far_eye[i] = eye[i];
    f.units_per_meter = units;
    for (int i = 0; i < 3; ++i) f.hand_point[i] = g_target[i];
    if (!(units > 0.0f)) f.have_ray = false;
    if (life > 0.1f && g_history_count >= 2) {
      float v[3];
      if (controller_math::MeanVelocity(g_grip_history, g_history_count, 0.060,
                                        v) &&
          NowSeconds() - g_grip_history[g_history_count - 1].time <
              kStaleSeconds) {
        for (int i = 0; i < 3; ++i) f.grip_velocity[i] = v[i] / life;
        f.have_motion = true;
      }
    }
    if (life > 0.1f && g_history_count >= 1) {
      for (int i = 0; i < 3; ++i) {
        f.grip_room[i] = g_grip_history[g_history_count - 1].point[i] / life;
      }
    }
    float head[3] = {0.0f, 0.0f, 0.0f};
    if (life > 0.1f && HeadPoseHeadInRoom(head)) {
      for (int i = 0; i < 3; ++i) f.head_room[i] = head[i] / life;
    }
    if (f.ready && !f.have_ray) {
      f.ready = false;
      f.why = "the aim ray is not known";
    }
  }
  FarGrabFrame(f);
  // THE CREATURE COMMAND (creature_command.cpp) aims with the same cone. It
  // needs the cone while its trigger is down too, when a far grab is not
  // ready.
  // A MIRACLE IN THE HAND (miracle.cpp) aims with the same cone: the Hand is
  // not empty then, so the far grab is not ready.
  const bool command = f.usable && f.have_ray &&
                       (CreatureCommandWantsCone() || MiracleWantsCone() ||
                        ForestTakeWantsCone());
  if (!f.ready && !command) return;
  g_far_ready = f.ready;
  g_far_ray = true;
  g_far_units = f.units_per_meter;
  for (int i = 0; i < 3; ++i) {
    g_far_origin[i] = f.ray_origin[i];
    g_far_direction[i] = f.ray_direction[i];
  }
  g_far_offer = true;
  g_far_offer_deg = FarGrabSelectDeg() + 4.0f;
}

// THE PICK AT THE GLOW and THE LEASH: the keys of bwvr.ini, and the hook of
// the leash. The hook goes in only when the first bytes of the function are
// the bytes that the disassembly gave.
void InstallNear() {
  g_rule_ini = IniKey("near_rule", 2);
  if (g_rule_ini < 0 || g_rule_ini > 2) g_rule_ini = 2;
  SetRule(g_rule_ini);
  g_pick_cm = IniKey("hand_pick_cm", near_math::kPickCm);
  if (g_pick_cm < 1) g_pick_cm = 1;
  if (g_pick_cm > 20) g_pick_cm = 20;
  g_touch_cm = IniKey("hand_touch_cm", near_math::kTouchCm);
  if (g_touch_cm < 0) g_touch_cm = 0;
  if (g_touch_cm > g_pick_cm) g_touch_cm = g_pick_cm;
  g_palm_search = IniKey("palm_search", 0) != 0;
  g_target_check = IniKey("target_check", 1) != 0;
  g_gone_test_frame =
      static_cast<uint64_t>(IniKey("target_check_test_frame", 0));
  ModLog("hand pick    : target_check=%d. %s", g_target_check ? 1 : 0,
         g_target_check
             ? "At the start of each frame and before the mod puts its "
               "target in the pick record, a target that the game deleted "
               "is dropped"
             : "The mod puts its target of the last draw in the pick record "
               "with no check that it lives");
  g_stroke_mode = IniKey("creature_stroke_hand", 0);
  if (g_stroke_mode < 0 || g_stroke_mode > 3) g_stroke_mode = 0;
  // -1 says that the ini has no creature_slap_mps: the fist stroke then has
  // its own speed (kSlapBeforeMps).
  g_slap_ini_set = IniKey("creature_slap_mps", -1) != -1;
  if (g_stroke_mode == kStrokeBefore) {
    ModLog("hand creature: creature_stroke_hand=3, the fist stroke: a fist "
           "while the grip is closed, the contact at the Hand's point, and a "
           "slap at %.2f m/s of the grip", SlapMps());
  } else {
    ModLog("hand creature: a slap needs %.2f m/s of the grip or of the Hand's "
           "point, the larger of the two (creature_slap_mps). The hand of a "
           "stroke is open: %s (creature_stroke_hand=%d)", SlapMps(),
           g_stroke_mode == 0   ? "the flat hand for a large Creature and "
                                  "the index finger for a small one"
           : g_stroke_mode == 1 ? "the flat hand"
                                : "the index finger",
           g_stroke_mode);
  }
  g_stroke_finger_pct = IniKey("creature_finger_below_pct",
                               near_math::kStrokeFingerBelowPct);
  g_stroke_flat_pct = IniKey("creature_flat_above_pct",
                             near_math::kStrokeFlatAbovePct);
  {
    float finger_below = 0.0f, flat_above = 0.0f;
    StrokeLimitsNow(&finger_below, &flat_above);
    ModLog("hand creature: the hand of a stroke by the size of the Creature: "
           "the index finger while the body is below %.2f of the hand's "
           "length, and the flat hand over %.2f (creature_finger_below_pct, "
           "creature_flat_above_pct). A stroke starts with the flat hand from "
           "%.2f. The body is the height of the Creature. Ctrl and the 3 key "
           "of the number pad change to the fixed limits 0.80 and 1.20",
           finger_below, flat_above,
           0.5f * (finger_below + flat_above));
  }
  g_palm_point = IniKey("creature_palm_point", 1) != 0;
  ModLog("hand creature: creature_palm_point=%d. %s. Ctrl and the 9 key of "
         "the number pad change it", g_palm_point ? 1 : 0,
         g_palm_point
             ? "With the flat hand of a stroke the Hand's point is the palm: "
               "the glow and the ray of the game's stroke are there"
             : "With the flat hand of a stroke the Hand's point stays at the "
               "tip of the index finger");
  g_stroke_wait_open = IniKey("creature_open_wait", 1) != 0;
  ModLog("hand creature: creature_open_wait=%d. %s",
         g_stroke_wait_open ? 1 : 0,
         g_stroke_wait_open
             ? "The hand opens when a press on the Creature is 450 ms old, "
               "before the game starts the Creature state"
             : "The hand is a fist from the press to the start of the "
               "Creature state");
  g_glow_idle = IniKey("hand_glow_idle", 0) != 0;
  ModLog("hand glow    : hand_glow_idle=%d. %s", g_glow_idle ? 1 : 0,
         g_glow_idle ? "The glow shows on each frame"
                     : "The glow shows only while a press of the grip has a "
                       "target at the Hand's point");
  {
    // The test key near_test_flips: up to 8 frames, with commas between them.
    char path[MAX_PATH], text[128] = "";
    const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = length > 0 && length < MAX_PATH ? strrchr(path, '\\')
                                                  : nullptr;
    if (slash != nullptr && (slash - path) + 10 < MAX_PATH) {
      slash[1] = 0;
      strcat_s(path, "bwvr.ini");
      GetPrivateProfileStringA("bwvr", "near_test_flips", "", text,
                               sizeof(text), path);
    }
    for (const char* at = text; *at != 0 && g_rule_flip_count < 8;) {
      char* end = nullptr;
      const unsigned long frame = strtoul(at, &end, 10);
      if (end == at) break;
      if (frame > 0) g_rule_flips[g_rule_flip_count++] = frame;
      at = *end == ',' ? end + 1 : end;
    }
    if (g_rule_flip_count > 0) {
      SetRule(1);
      ModLog("hand pick    : test key near_test_flips: near_rule starts at 1 "
             "and changes at %d frames", g_rule_flip_count);
    }
  }
  if (g_one_target) {
    ModLog("hand pick    : one target (near_rule=2). The pick record holds "
           "the object with the halo and no other object. Within %d cm of "
           "the Hand's point: a body that the point touches (%d cm, "
           "hand_touch_cm) and that is no tree, building or pile wins. Then "
           "a scroll. Then a tree, a building or a pile that the point "
           "touches. Then the nearest body. palm_search=%d: %s",
           g_pick_cm, g_touch_cm, g_palm_search ? 1 : 0,
           g_palm_search ? "the game's search at the palm stays on"
                         : "a press with no target acts on nothing");
  }
  {
    // The two hooks of one target. Each goes in only when the first bytes of
    // the function are the bytes of the disassembly. They act only with
    // near_rule=2 while the controller places the Hand, and the log of each
    // press is there for each rule.
    static const uint8_t kPalmBytes[12] = {0x83, 0xEC, 0x4C, 0xA1, 0x7C, 0x01,
                                           0xD2, 0x00, 0x53, 0x55, 0x56, 0x57};
    static const uint8_t kPressBytes[11] = {0x53, 0x56, 0x57, 0x8B, 0xF9, 0x8B,
                                            0x87, 0x9C, 0x03, 0x00, 0x00};
    const void* palm = reinterpret_cast<const void*>(kPalmSearch);
    const void* press = reinterpret_cast<const void*>(kRightPress);
    if (IsBadReadPtr(palm, sizeof(kPalmBytes)) ||
        memcmp(palm, kPalmBytes, sizeof(kPalmBytes)) != 0) {
      ModLog("hooks        : the search at the palm at 0x%08X does not have "
             "the bytes of the disassembly, so the game's search stays on",
             static_cast<unsigned>(kPalmSearch));
    } else {
      Hook(kPalmSearch, reinterpret_cast<void*>(&h_PalmSearch),
           reinterpret_cast<void**>(&o_PalmSearch),
           "the search at the palm (a press with no target)");
    }
    if (IsBadReadPtr(press, sizeof(kPressBytes)) ||
        memcmp(press, kPressBytes, sizeof(kPressBytes)) != 0) {
      ModLog("hooks        : the right press at 0x%08X does not have the "
             "bytes of the disassembly, so no press goes to the log",
             static_cast<unsigned>(kRightPress));
    } else {
      Hook(kRightPress, reinterpret_cast<void*>(&h_RightPress),
           reinterpret_cast<void**>(&o_RightPress),
           "the right press (the target of each press)");
    }
    // The step of the pick up action, for a catch at once.
    static const uint8_t kStepBytes[16] = {0x55, 0x56, 0x8B, 0xF1, 0x8B, 0x86,
                                           0x54, 0x04, 0x00, 0x00, 0x8B, 0x8E,
                                           0x58, 0x04, 0x00, 0x00};
    const void* step = reinterpret_cast<const void*>(kPickStep);
    if (IsBadReadPtr(step, sizeof(kStepBytes)) ||
        memcmp(step, kStepBytes, sizeof(kStepBytes)) != 0) {
      ModLog("hooks        : the step of the pick up action at 0x%08X does "
             "not have the bytes of the disassembly, so the game sends the "
             "pick up of an object that moves 225 ms after the press",
             static_cast<unsigned>(kPickStep));
    } else {
      Hook(kPickStep, reinterpret_cast<void*>(&h_PickStep),
           reinterpret_cast<void**>(&o_PickStep),
           "the step of the pick up action (a catch at once)");
    }
    // A rock that splits goes to the log. The hook changes nothing.
    static const uint8_t kSplitBytes[10] = {0x83, 0xEC, 0x5C, 0x56, 0x57,
                                            0x68, 0xFB, 0x00, 0x00, 0x00};
    const void* split = reinterpret_cast<const void*>(kRockSplit);
    if (IsBadReadPtr(split, sizeof(kSplitBytes)) ||
        memcmp(split, kSplitBytes, sizeof(kSplitBytes)) != 0) {
      ModLog("hooks        : Rock::SplitInTwo at 0x%08X does not have the "
             "bytes of the disassembly, so the log does not name a rock that "
             "splits", static_cast<unsigned>(kRockSplit));
    } else {
      Hook(kRockSplit, reinterpret_cast<void*>(&h_RockSplit),
           reinterpret_cast<void**>(&o_RockSplit),
           "Rock::SplitInTwo (the log of a rock that splits)");
    }
  }
  g_catch_on = IniKey("catch_moving", near_math::kCatchOpen);
  if (g_catch_on < near_math::kCatchOff) g_catch_on = near_math::kCatchOff;
  if (g_catch_on > near_math::kCatchOpen) g_catch_on = near_math::kCatchOpen;
  g_catch_lag_ms = IniKey("catch_lag_ms", near_math::kCatchLagMs);
  if (g_catch_lag_ms < 0) g_catch_lag_ms = 0;
  if (g_catch_lag_ms > 500) g_catch_lag_ms = 500;
  g_catch_cap_cm = IniKey("catch_cap_cm", near_math::kCatchCapCm);
  if (g_catch_cap_cm < 0) g_catch_cap_cm = 0;
  if (g_catch_cap_cm > 20) g_catch_cap_cm = 20;
  g_catch_grace_ms = IniKey("catch_grace_ms", near_math::kCatchGraceMs);
  if (g_catch_grace_ms < 0) g_catch_grace_ms = 0;
  if (g_catch_grace_ms > 3000) g_catch_grace_ms = 3000;
  ModLog("hand pick    : catch_moving=%d. The reach of the pick for an object "
         "that moves grows by its speed times %d ms, at most %d cm of the "
         "room (catch_lag_ms, catch_cap_cm). %s for %d ms (catch_grace_ms)%s. "
         "Ctrl and the Page Up key go through the values 3, 2, 1 and 0 of "
         "catch_moving (test_keys=1)", g_catch_on, g_catch_lag_ms,
         g_catch_cap_cm,
         g_catch_on >= near_math::kCatchRest
             ? "The extra reach is on the reach that the object has at rest, "
               "the wide reach too. The object that you threw has the reach "
               "at rest"
             : "The extra reach is on the reach of the pick only. The object "
               "that you let go has the reach at rest",
         g_catch_grace_ms,
         g_catch_on >= near_math::kCatchOpen
             ? ", or until the grip is fully open"
             : "");
  g_wide_cm = IniKey("hand_wide_pick_cm", near_math::kWideCm);
  if (g_wide_cm < 0) g_wide_cm = 0;
  if (g_wide_cm > 30) g_wide_cm = 30;
  g_near_in_cm = IniKey("far_grab_near_cm", near_math::kNearInCm);
  if (g_near_in_cm < 0) g_near_in_cm = 0;
  if (g_near_in_cm > 100) g_near_in_cm = 100;
  g_near_out_cm = IniKey("far_grab_near_out_cm", near_math::kNearOutCm);
  if (g_near_out_cm < g_near_in_cm) g_near_out_cm = g_near_in_cm;
  if (g_near_out_cm > 150) g_near_out_cm = 150;
  g_far_touch = IniKey("far_touch", 1) != 0;
  // THE SCROLL FROM ITS DRAW.
  g_scroll_draw = IniKey("scroll_draw", 1) != 0;
  g_scroll_click = IniKey("scroll_click", 1) != 0;
  ModLog("hand scroll  : scroll_click=%d. %s", g_scroll_click ? 1 : 0,
         g_scroll_click
             ? "A scroll that is not active is a target of the grip: the "
               "game makes it active at the first click, and the press is "
               "that click"
             : "A scroll that is not active is no target");
  g_scroll_reach_cm = IniKey("scroll_reach_cm", near_math::kScrollReachCm);
  if (g_scroll_reach_cm < 0) g_scroll_reach_cm = 0;
  if (g_scroll_reach_cm > 30) g_scroll_reach_cm = 30;
  {
    static const uint8_t kScrollBytes[10] = {0x81, 0xEC, 0x9C, 0x00, 0x00,
                                             0x00, 0x55, 0x8B, 0xE9, 0x56};
    const void* draw = reinterpret_cast<const void*>(kScrollDraw);
    if (IsBadReadPtr(draw, sizeof(kScrollBytes)) ||
        memcmp(draw, kScrollBytes, sizeof(kScrollBytes)) != 0) {
      ModLog("hooks        : ScriptHighlight::Draw at 0x%08X does not have "
             "the bytes of the disassembly, so a scroll is a target only "
             "when the game's pick offers it",
             static_cast<unsigned>(kScrollDraw));
    } else if (Hook(kScrollDraw, reinterpret_cast<void*>(&h_ScrollDraw),
                    reinterpret_cast<void**>(&o_ScrollDraw),
                    "ScriptHighlight::Draw (the scroll from its draw)")) {
      ModLog("hand scroll  : scroll_draw=%d. %s. The reach of the pick for a "
             "scroll is %d cm of the room from its ball (scroll_reach_cm), "
             "and %d cm for each other object. Ctrl and the point key of the "
             "number pad change scroll_draw",
             g_scroll_draw ? 1 : 0,
             g_scroll_draw
                 ? "Each scroll that the game draws is a candidate of the "
                   "choice, with the ball that its draw made in this frame"
                 : "A scroll is a candidate only when the game's pick offers "
                   "it",
             g_scroll_reach_cm > g_pick_cm ? g_scroll_reach_cm : g_pick_cm,
             g_pick_cm);
    }
  }
  ScriptPlayerInstall();
  g_far_leash = IniKey("far_leash", 1) != 0;
  g_fight_grip = IniKey("fight_grip", 1) != 0;
  ModLog("hand fight   : fight_grip=%d. %s", g_fight_grip ? 1 : 0,
         g_fight_grip
             ? "In a fight of Creatures, a press on a Creature at the Hand "
               "hits the place of the body that is nearest to the Hand's "
               "point: an attack on the other Creature, a block on your "
               "Creature. No Creature is a far target in a fight"
             : "In a fight of Creatures, the game casts the ray of a press "
               "through the pointer, and the far aim keeps your Creature as "
               "a target of the leash");
  ModLog("far grab    : far_leash=%d. %s", g_far_leash ? 1 : 0,
         g_far_leash ? "Your Creature in the aim is a target of the leash: "
                       "the grip sends the leash click, and no press"
                     : "The Creature is no far target");
  // THE CLASSES OF THE RANKS and the body of the Creature (docs/grab.md, "The
  // cone and the choice").
  g_far_creature_body = IniKey("far_grab_creature", 1) != 0;
  g_far_miracle_first = IniKey("far_grab_miracle", 1) != 0;
  g_far_sign_last = IniKey("far_grab_sign", 1) != 0;
  ModLog("far grab    : far_grab_creature=%d. %s", g_far_creature_body ? 1 : 0,
         g_far_creature_body
             ? "The aim measures your Creature by its body, as it measures "
               "each living thing: the line from the land under its body "
               "bone to its top, with its radius. The Creature is a "
               "candidate also when the draw did not offer it"
             : "The aim measures your Creature by the point of its mesh, "
               "which is at its feet, and only when the draw offered it");
  ModLog("far grab    : far_grab_miracle=%d. %s. far_grab_sign=%d. %s. The two "
         "keys act with ranks (far_grab_choice 2 and 4)",
         g_far_miracle_first ? 1 : 0,
         g_far_miracle_first
             ? "A miracle that the Hand can take (OneOffSpellSeed) is in the "
               "first rank, as a living thing is"
             : "A miracle that the Hand can take has no rank",
         g_far_sign_last ? 1 : 0,
         g_far_sign_last
             ? "A signpost is in the last rank: it takes the halo only when "
               "the aim is on it and on no other target, or when no other "
               "target is in the cone"
             : "A signpost is a scroll of the first rank");
  g_sign_last = IniKey("hand_sign_last", 1) != 0;
  g_creature_wide = IniKey("hand_wide_creature", 1) != 0;
  g_beside_loose = IniKey("far_grab_beside_loose", 1) != 0;
  ModLog("hand pick    : hand_sign_last=%d. %s. hand_wide_creature=%d. %s",
         g_sign_last ? 1 : 0,
         g_sign_last
             ? "At the Hand a signpost that the point is not in comes last: "
               "each other object in its reach and each object in the wide "
               "reach comes before it, it does not turn the grab from a "
               "distance off, and it is no target while the aim has a target"
             : "At the Hand a signpost is a scroll as each other scroll",
         g_creature_wide ? 1 : 0,
         g_creature_wide
             ? "The Creature has the wide reach of an object that the Hand "
               "can hold (hand_wide_pick_cm), when no object that a press "
               "acts on is nearer"
             : "The reach of the Creature at the Hand is hand_pick_cm");
  CreatureStayInstall();
  g_creature_bone = IniKey("creature_bone", 1) != 0;
  ModLog("hand creature: creature_bone=%d. %s", g_creature_bone ? 1 : 0,
         g_creature_bone
             ? "The body of a Creature is known before a press: the body "
               "bone, or the place on the map when the bone is not sane. The "
               "capsule of the choice and the Creature as a target of a "
               "command use it"
             : "The body of a Creature is known only while the Creature is "
               "ready for the Hand. Before a press the choice measures the "
               "Creature as each other object");
  g_leash_palm = IniKey("leash_at_palm", 1) != 0;
  if (g_near_rule) {
    ModLog("hand pick    : one point. A press of the grip acts on the nearest "
           "object within %d cm of the room of the Hand's point, where the "
           "glow is (hand_pick_cm): an object to hold, a bulk source, or an "
           "object that the game taps, such as a scroll", g_pick_cm);
    ModLog("far grab    : near objects win. The grab from a distance is off "
           "while such an object or the Creature is %d cm or less from the "
           "Hand's point, and it comes back past %d cm (far_grab_near_cm, "
           "far_grab_near_out_cm). far_touch=%d: %s", g_near_in_cm,
           g_near_out_cm, g_far_touch ? 1 : 0,
           g_far_touch ? "a scroll in the aim is a target, and the grip "
                         "activates it with no pull"
                       : "a scroll is no target of the aim");
    ModLog("far grab    : far_grab_beside_loose=%d. %s", g_beside_loose ? 1 : 0,
           g_beside_loose
               ? "A tree, a building or a pile that the Hand's point is not "
                 "in does not turn the grab from a distance off. It is the "
                 "target at the Hand only while the aim has no target"
               : "A tree, a building or a pile near the Hand's point turns "
                 "the grab from a distance off as each other object");
  } else {
    ModLog("hand pick    : near_rule=0, so the pick reaches 60 percent of the "
           "Hand's length");
  }
  if (!g_leash_palm) {
    ModLog("leash        : leash_at_palm=0, so the rope ends at the wrist "
           "bone of the game's Hand");
    return;
  }
  static const uint8_t kLeashStartBytes[13] = {0x83, 0xEC, 0x3C, 0x56, 0x57,
                                               0x8B, 0xF9, 0x8B, 0x87, 0x9C,
                                               0x03, 0x00, 0x00};
  const void* at = reinterpret_cast<const void*>(kGetLeashStartPoint);
  if (IsBadReadPtr(at, sizeof(kLeashStartBytes)) ||
      memcmp(at, kLeashStartBytes, sizeof(kLeashStartBytes)) != 0) {
    ModLog("hooks        : GInterface::GetLeashStartPoint at 0x%08X does not "
           "have the bytes of the disassembly, so the leash stays at the "
           "game's Hand", static_cast<unsigned>(kGetLeashStartPoint));
    return;
  }
  Hook(kGetLeashStartPoint, reinterpret_cast<void*>(&h_LeashStart),
       reinterpret_cast<void**>(&o_LeashStart),
       "GInterface::GetLeashStartPoint (the leash at the mod's hand)");
}

}  // namespace

// Hook the three functions. Only with the direct hand on. With controllers off
// the game runs as the plain game does.
void HandDirectInstall() {
  if (g_installed) return;
  const bool a = Hook(kSetDistanceFromView,
                      reinterpret_cast<void*>(&h_SetDistanceFromView),
                      reinterpret_cast<void**>(&o_SetDistanceFromView),
                      "CHand::SetDistanceFromView");
  const bool b = Hook(kPoseBuilder, reinterpret_cast<void*>(&h_PoseBuilder),
                      reinterpret_cast<void**>(&o_PoseBuilder),
                      "the Hand pose builder");
  const bool c = Hook(kSendObjectDrawCollision,
                      reinterpret_cast<void*>(&h_SendObjectDrawCollision),
                      reinterpret_cast<void**>(&o_SendObjectDrawCollision),
                      "GInterface::SendObjectDrawCollision");
  g_installed = a && b && c;
  if (g_installed) {
    // The throw and the Creature stand apart from the placement. Without
    // them the Hand still sits at the controller.
    Hook(kReleaseAction, reinterpret_cast<void*>(&h_ReleaseAction),
         reinterpret_cast<void**>(&o_ReleaseAction),
         "the release action (the throw)");
    Hook(kCreatureUpdate, reinterpret_cast<void*>(&h_CreatureUpdate),
         reinterpret_cast<void**>(&o_CreatureUpdate),
         "HandStateCreature::Update");
    // No touch at a distance. It acts only inside a Creature Update that
    // the controller placed.
    Hook(kMeshRay, reinterpret_cast<void*>(&h_MeshRay),
         reinterpret_cast<void**>(&o_MeshRay),
         "the mesh ray test (the Creature's contact)");
    // The game's own Hand mesh on the real hand (hand_exact.cpp).
    HandExactInstall();
    InstallNear();
  }
  if (!g_installed) {
    // A hook without its partners would place half a Hand. Turn all off.
    if (a) MH_DisableHook(reinterpret_cast<void*>(kSetDistanceFromView));
    if (b) MH_DisableHook(reinterpret_cast<void*>(kPoseBuilder));
    if (c) MH_DisableHook(reinterpret_cast<void*>(kSendObjectDrawCollision));
  }
}

// THE CREATURE WITHOUT CAMERA MOVES (docs/creature.md, "The Creature by
// touch"). mode is creature_camera. With 1 the game moves its camera,
// and nothing is hooked. With 0 and 2 the hook goes in with or without the
// controllers, because a mouse in the God view needs it too.
void HandDirectInstallCreatureCamera(int mode) {
  static bool installed = false;
  if (mode < 0 || mode > 2) mode = 2;
  g_creature_camera = mode;
  if (mode == 1) {
    ModLog("hand creature: creature_camera=1, the game moves its camera to "
           "the Creature as the plain game does");
    return;
  }
  if (installed) return;
  installed = Hook(kInteractCamera, reinterpret_cast<void*>(&h_InteractCamera),
                   reinterpret_cast<void**>(&o_InteractCamera),
                   "CHand::OperateInteractionCamera (the Creature camera)");
  ModLog("hand creature: creature_camera=%d, the camera does not move to the "
         "Creature %s", mode,
         mode == 0 ? "at all"
                   : "while the controller places the Hand or the table or "
                     "the God view owns the view");
}

// The test hook of bulk_test_influence (HandDirectInstallTable).
PlayerInfluenceFn o_PlayerInfluence = nullptr;
uint64_t g_influence_lifts = 0;

// The test key far_test_influence=1: every land point is inside the player's
// influence, so the desktop run can fetch an object at a place the new game
// has not reached. Never for play.
bool g_far_test_influence = false;

float __cdecl h_PlayerInfluence(const int32_t* coords, void* player, int a,
                                int b, int c) {
  const float value = o_PlayerInfluence(coords, player, a, b, c);
  if (value > 0.0f) return value;
  if (g_far_test_influence) return 1.0f;
  if (g_face == nullptr) return value;
  if (!(g_choice_take && g_choice != nullptr) &&
      TakeObject(g_face) == nullptr && (g_hand == nullptr || HandEmpty())) {
    return value;
  }
  ++g_influence_lifts;
  return 1.0f;
}

// Only with play_mode=3 and 4, after HandDirectInstall: the screen test hook
// of the pick on the table, and the interface step hook (the land under the
// Hand, and the throw from inside the influence). They act only while the
// table owns the view and the controller places the Hand.
void HandDirectInstallTable() {
  if (!g_installed || g_table_hooked) return;
  // A test key for the desktop runs: the store of Land 1 is outside the
  // player's influence for minutes after a new game, and the game gives no
  // bulk take there (fn_005D7F20 gives hand mode 0x12, and
  // GInterfaceStatus::Process ends a take each turn outside it).
  // bulk_test_influence=1 makes the influence 1 while the mod's pick has a
  // bulk source, while a take runs and while the Hand holds an object, so
  // the drop of the bundle can be tested too. Never for play.
  g_far_test_influence = TestKey("far_test_influence") != 0;
  if (TestKey("bulk_test_influence") != 0 || g_far_test_influence) {
    Hook(kPlayerInfluence, reinterpret_cast<void*>(&h_PlayerInfluence),
         reinterpret_cast<void**>(&o_PlayerInfluence),
         "the player's influence (test key bulk_test_influence=1 or "
         "far_test_influence=1)");
  }
  g_table_hooked = Hook(kScreenTest, reinterpret_cast<void*>(&h_ScreenTest),
                        reinterpret_cast<void**>(&o_ScreenTest),
                        "the object screen test (the pick on the table)");
  Hook(kInterfaceStep, reinterpret_cast<void*>(&h_InterfaceStep),
       reinterpret_cast<void**>(&o_InterfaceStep),
       "the interface step (the land under the Hand on the table)");
  // THE BULK TAKE: the test key bulk_test_no_keep=1 gives a take where the
  // game's release step reads a record that the mod did not fill. For the
  // desktop run that shows the fault. Never for play.
  g_take_no_keep = TestKey("bulk_test_no_keep") != 0;
  if (g_take_no_keep) {
    ModLog("hand bulk    : test key bulk_test_no_keep=1, so the source of a "
           "take does not go back in the pick record and the mod ends no take");
  }
  // A TAKE WHOSE SOURCE GOES AWAY: the test key far_test_take_gone=N ends a
  // take at a distance at its game turn N with the grip closed, and tells the
  // far grab that the source is gone. For the desktop run that shows who has
  // the grip then (far_take_carry). Never for play.
  g_far_test_take_gone = TestKey("far_test_take_gone");
  if (g_far_test_take_gone > 0) {
    ModLog("far grab    : test key far_test_take_gone=%d, so a take at a "
           "distance ends at that game turn with the grip closed, and the far "
           "grab gets the answer that its source is gone",
           g_far_test_take_gone);
  }
  // A GAME TURN NOW: release_at_once=0 leaves the game turns as they are.
  // With test_keys=1 the hook goes in for both values, so the A/B key can
  // turn the release at once on.
  g_turn_on = IniKey("release_at_once", 1) != 0;
  g_release_key = IniKey("test_keys", 0) != 0;
  if (g_turn_on || g_release_key) {
    g_turn_hooked = Hook(kDoATurn, reinterpret_cast<void*>(&h_DoATurn),
                         reinterpret_cast<void**>(&o_DoATurn),
                         "GGame::LocalTimerSaysDoATurn (a game turn at once "
                         "after a release)");
  }
  if (!g_turn_on) {
    ModLog("hand throw   : release_at_once=0, so a release waits for the "
           "next game turn, up to 100 ms");
  }
  if (g_release_key) {
    ModLog("keys         : the star key of the number pad turns the release "
           "at once off and on (test_keys=1). It is %s now",
           g_turn_on ? "on" : "off");
  }
}

namespace {

// THE PLAYER THAT A SCRIPT SEES (docs/god-view.md, "The player that a script
// sees").
//
// A script of the game asks about the camera to learn what the player does:
// "is the camera less than 100 units from the scroll", "is the guide in the
// field of view". challenge.chl has 109 tests of the first kind and 107 of
// the second. In the God view the game's own camera is not the player. After
// the player moves the view it is 400 units from the place of the player
// (TabletopGodGameCamera), which is 257 units on the map, and it looks along
// the heading of the room. So a test of the distance was false with the
// player at the thing, and a test of the view did not follow the head.
//
// THE WORDS OF THE SCRIPTS [V]. Each one is cdecl with no arguments. It takes
// its values from the stack of the script machine with ScriptDLL::POP
// 0x006F6BC0, and it gives its answer back with ScriptDLL::PUSH 0x006F6BA0.
// - GScript::GetDistance 0x006F8CA0 takes two points, six values. It
//   measures them with GUtils::GetDistance 0x0074CDE0, which uses x and z
//   only: the distance on the map. A script gets the camera from
//   GScript::GetCameraPos 0x006ECFF0, which gives LH3DTech::g_camera.
// - GScript::IsPosFieldOfView 0x006F8060 takes a point, three values, and
//   asks fn_0081F1D0: the point is in front of the camera, and its picture
//   is on the screen. The test uses the matrix that the engine has at that
//   time. Between two frames that is the matrix of the game's own camera
//   (TabletopGraphicsDone).
// - GScript::IsGameThingFieldOfView 0x006F8130 takes a thing. For an object
//   with a mesh it asks LH3DBoundingBox::CheckRegionOnScreen 0x00868C80 for
//   the ball of the mesh, and for each other thing fn_0081F1D0 for its place.
// - GScript::GameThingCanViewCamera 0x006FE4D0 takes a thing and an angle.
//   It asks if LH3DTech::g_camera is in front of the thing.
//
// WHAT THE MOD DOES, while the player stands for the camera
// (near_math::PlayerIsCamera, asked at the moment of the word):
// - script_distance: when one of the two points of GetDistance is exactly
//   the engine camera, the nearest point of the player goes in its place:
//   the head, the Hand's point or the grip of a controller
//   (near_math::PlayerDistance). A script that keeps the camera position to
//   put the camera back after a cut scene gets the game's camera.
// - script_view: the two field of view words give the view of the head
//   (near_math::HeadSees) for the place of the thing, as the game's own word
//   GetPosition 0x006F88A0 gives it. GameThingCanViewCamera runs with the
//   head in the place of the camera.
//
// THE PLACE OF NOW. The player is latched once a frame, inside the draw
// (LatchPlayer). The scripts run in the game turn, before the next main
// camera call, and only that call moves the view. So the latch is the view
// that the player has when a script asks.
const uintptr_t kScriptDistance = 0x006F8CA0;
const uintptr_t kScriptPosView = 0x006F8060;
const uintptr_t kScriptThingView = 0x006F8130;
const uintptr_t kScriptThingSees = 0x006FE4D0;
const uintptr_t kScriptPosition = 0x006F88A0;  // GScript::GetPosition
// GScript::GetScriptGameThing, cdecl (id): the thing, or null.
const uintptr_t kScriptThing = 0x0070D220;
const uintptr_t kScriptMachine = 0x00D95C10;  // the ScriptDLL of the game
const uintptr_t kScriptPop = 0x006F6BC0;      // thiscall (VMType*), ret 4
const uintptr_t kScriptPush = 0x006F6BA0;     // thiscall (value, VMType)
// GGame + this is 1 while the camera is out of the world. GetCameraPos then
// gives a point out of the world, and the field of view words say no.
const uintptr_t kScriptGame = 0x00D0195C;
const int kGameCameraOut = 0x205A28;
typedef void(__cdecl* ScriptWordFn)();
typedef uint32_t(__fastcall* ScriptPopFn)(void* self, void* unused, int* type);
typedef void(__fastcall* ScriptPushFn)(void* self, void* unused,
                                       uint32_t value, int type);
typedef void*(__cdecl* ScriptThingFn)(uint32_t id);
ScriptWordFn o_ScriptDistance = nullptr;
ScriptWordFn o_ScriptPosView = nullptr;
ScriptWordFn o_ScriptThingView = nullptr;
ScriptWordFn o_ScriptThingSees = nullptr;
// THE CAMERA MOVES THAT A SCRIPT SEES (script_camera_moves). Two words of the
// scripts ask what the player does with the camera [V: the disassembly and
// challenge.chl]:
// - GScript::GetHandState pushes the interface hand mode (GInterface +
//   0x3AC) as an int. The interface state LANDSCAPE_LOCK (0x005D8090) gives
//   0x14 while the Move Button holds the land, and 0x16 while both buttons
//   hold it and the camera can zoom. TeachRotate, TeachZoom,
//   MoveAroundMountain and ZoomInAndOut read it.
// - GScript::WithinRotation pushes the arrows of the camera that show
//   (0x00C5B0F4, which CameraModeNew3::UpdateTricons sets in each frame).
//   Bit 2 is the rotate arrow. MoveAroundMountain of the Gods' Playground
//   waits for it.
// In the God view a zoom, a turn and a grip on the land give the value that
// the game gives for the same move with the mouse (near_math::ScriptHandState
// and ScriptArrows). The game's word runs first, and the mod changes its
// answer on the stack.
const uintptr_t kScriptHandState = 0x006FF730;
const uintptr_t kScriptArrows = 0x0070B9D0;
const uintptr_t kCameraFeatures = 0x009CDD6C;  // CameraHelp::EnabledFeatures
const int kHelpTurnLow = 0x0;  // camera help event 0x300: the camera turned
const int kHelpZoomLow = 0x4;  // 0x304: the camera zoomed
ScriptWordFn o_ScriptHandState = nullptr;
ScriptWordFn o_ScriptArrows = nullptr;
// The changed answers of this report window, and the last pair of the game's
// answer and the mod's answer that the log named, for each word.
uint64_t g_moves_answers = 0;
unsigned g_moves_said[2][2] = {{~0u, ~0u}, {~0u, ~0u}};
int g_moves_logs = 0;
const int kMaxMovesLogs = 24;
// The player of the last frame: the head and the hands in the world, the
// axes of the head (right, up and forward), and the view angle of the frame.
struct ScriptPlayer {
  bool valid;
  near_math::PlayerPoints points;
  float basis[9];
  float inv_tan_x;
  float inv_tan_y;
};
ScriptPlayer g_player = {};
// The answers that a script got from the player, this window.
uint64_t g_script_answers = 0;

// Once a frame, inside the draw: where the player is in the world. The God
// view must own this frame. A frame with no main camera call has no room.
void LatchPlayer() {
  ScriptPlayer p = {};
  float origin[3], room[9], center[3], gain = 1.0f;
  if (TabletopGodOwnsView() && TabletopRoom(origin, room, center, &gain)) {
    p.points.head = HeadCameraRenderBasis(p.basis, p.points.head_at);
    // A hand counts only within 2.5 m of the room of the head, so a grip
    // that the room of this frame did not place is no point of the player.
    OverlayHands hands;
    if (p.points.head && ControllersOverlayHands(&hands)) {
      const float arm = 2.5f * hands.units_per_meter;
      for (int i = 0; i < 2; ++i) {
        if (!hands.hand[i].valid) continue;
        if (!(Distance(hands.hand[i].position, p.points.head_at) <= arm)) {
          continue;
        }
        p.points.hand[i + 1] = true;
        for (int k = 0; k < 3; ++k) {
          p.points.hand_at[i + 1][k] = hands.hand[i].position[k];
        }
      }
    }
    const bw::tech::InfoTransform* info =
        reinterpret_cast<const bw::tech::InfoTransform*>(
            bw::tech::kInfoTransform);
    p.inv_tan_x = near_math::HeadViewInvTan(info->inv_half_tan_fov_x);
    p.inv_tan_y = near_math::HeadViewInvTan(info->inv_half_tan_fov_y);
  }
  p.valid = p.points.head;
  g_player = p;
}

// True when the player stands for the camera of a script at this moment.
bool ScriptPlayerNow() {
  if (!g_script_on || !g_player.valid) return false;
  return near_math::PlayerIsCamera(true, TabletopGodOwnsView(),
                                   ScriptCameraOwnerNow(),
                                   CutsceneWindowBarsUp(),
                                   ComfortCameraGameMoves());
}

bool ScriptCameraOut() {
  const uint8_t* game = *reinterpret_cast<const uint8_t* const*>(kScriptGame);
  return game == nullptr ||
         *reinterpret_cast<const int32_t*>(game + kGameCameraOut) == 1;
}

bool ScriptHeadSees(const uint32_t z_y_x[3]) {
  float point[3];
  memcpy(&point[0], &z_y_x[2], 4);
  memcpy(&point[1], &z_y_x[1], 4);
  memcpy(&point[2], &z_y_x[0], 4);
  return near_math::HeadSees(g_player.points.head_at, g_player.basis, point,
                             g_player.inv_tan_x, g_player.inv_tan_y);
}

void __cdecl h_ScriptDistance() {
  uint8_t* machine = *reinterpret_cast<uint8_t* const*>(kScriptMachine);
  if (machine == nullptr || g_script_distance == 0 || !ScriptPlayerNow()) {
    o_ScriptDistance();
    return;
  }
  const ScriptPopFn pop = reinterpret_cast<ScriptPopFn>(kScriptPop);
  const ScriptPushFn push = reinterpret_cast<ScriptPushFn>(kScriptPush);
  // A point goes on the stack as x, y, z, so it comes off as z, y, x.
  uint32_t v[6];
  int t[6];
  for (int i = 0; i < 6; ++i) {
    t[i] = 0;
    v[i] = pop(machine, nullptr, &t[i]);
  }
  const uint32_t* camera =
      reinterpret_cast<const uint32_t*>(bw::tech::kCameraPos);
  int at = -1;
  if (v[5] == camera[0] && v[4] == camera[1] && v[3] == camera[2]) {
    at = 3;
  } else if (v[2] == camera[0] && v[1] == camera[1] && v[0] == camera[2]) {
    at = 0;
  }
  if (at >= 0) {
    const int other = at == 3 ? 0 : 3;
    float thing[3];
    memcpy(&thing[0], &v[other + 2], 4);
    memcpy(&thing[1], &v[other + 1], 4);
    memcpy(&thing[2], &v[other], 4);
    float distance = 0.0f;
    const int which =
        near_math::PlayerDistance(g_player.points, thing, &distance);
    if (which >= 0) {
      // The point of the player goes in the place of the camera. The game
      // does not measure the height, so it stays.
      const float* point = which == 0 ? g_player.points.head_at
                                      : g_player.points.hand_at[which - 1];
      memcpy(&v[at + 2], &point[0], 4);
      memcpy(&v[at], &point[2], 4);
      ++g_script_answers;
    }
  }
  for (int i = 5; i >= 0; --i) push(machine, nullptr, v[i], t[i]);
  o_ScriptDistance();
}

void __cdecl h_ScriptPosView() {
  uint8_t* machine = *reinterpret_cast<uint8_t* const*>(kScriptMachine);
  if (machine == nullptr || g_script_view == 0 || !ScriptPlayerNow() ||
      ScriptCameraOut()) {
    o_ScriptPosView();
    return;
  }
  const ScriptPopFn pop = reinterpret_cast<ScriptPopFn>(kScriptPop);
  const ScriptPushFn push = reinterpret_cast<ScriptPushFn>(kScriptPush);
  uint32_t v[3];
  int t[3];
  for (int i = 0; i < 3; ++i) {
    t[i] = 0;
    v[i] = pop(machine, nullptr, &t[i]);
  }
  for (int i = 2; i >= 0; --i) push(machine, nullptr, v[i], t[i]);
  // The game's word runs, and the answer of the head goes in the
  // place of its answer.
  o_ScriptPosView();
  int type = 0;
  pop(machine, nullptr, &type);
  push(machine, nullptr, ScriptHeadSees(v) ? 1u : 0u, type);
  ++g_script_answers;
}

void __cdecl h_ScriptThingView() {
  uint8_t* machine = *reinterpret_cast<uint8_t* const*>(kScriptMachine);
  if (machine == nullptr || g_script_view == 0 || !ScriptPlayerNow() ||
      ScriptCameraOut()) {
    o_ScriptThingView();
    return;
  }
  const ScriptPopFn pop = reinterpret_cast<ScriptPopFn>(kScriptPop);
  const ScriptPushFn push = reinterpret_cast<ScriptPushFn>(kScriptPush);
  int t = 0;
  const uint32_t id = pop(machine, nullptr, &t);
  push(machine, nullptr, id, t);
  o_ScriptThingView();
  // With no thing the game says no, and that answer stays.
  if (reinterpret_cast<ScriptThingFn>(kScriptThing)(id) == nullptr) return;
  int type = 0;
  pop(machine, nullptr, &type);
  // The place of the thing, from the game's own word.
  push(machine, nullptr, id, t);
  reinterpret_cast<ScriptWordFn>(kScriptPosition)();
  uint32_t v[3];
  for (int i = 0; i < 3; ++i) {
    int unused = 0;
    v[i] = pop(machine, nullptr, &unused);
  }
  push(machine, nullptr, ScriptHeadSees(v) ? 1u : 0u, type);
  ++g_script_answers;
}

void __cdecl h_ScriptThingSees() {
  if (g_script_view == 0 || !ScriptPlayerNow() || ScriptCameraOut()) {
    o_ScriptThingSees();
    return;
  }
  // The game's word reads the engine camera. The head is there for the time
  // of the word.
  float* camera = reinterpret_cast<float*>(bw::tech::kCameraPos);
  const float keep[3] = {camera[0], camera[1], camera[2]};
  for (int i = 0; i < 3; ++i) camera[i] = g_player.points.head_at[i];
  o_ScriptThingSees();
  for (int i = 0; i < 3; ++i) camera[i] = keep[i];
  ++g_script_answers;
}

// The moves of the God view, at the moment of the word.
near_math::ViewMoves ScriptViewMoves() {
  near_math::ViewMoves moves = {false, false, false};
  int32_t at = 0, now = 0;
  if (ComfortCameraHelpClock(kHelpTurnLow, &at, &now)) {
    moves.turned = near_math::HelpRecent(true, at, now);
  }
  if (ComfortCameraHelpClock(kHelpZoomLow, &at, &now)) {
    moves.zoomed = near_math::HelpRecent(true, at, now);
  }
  moves.land_held = ControllersGrabHeld();
  return moves;
}

const char* ScriptMoveWhy(const near_math::ViewMoves& moves, int word,
                          unsigned answer) {
  if (word == 0 && answer == near_math::kHandZoomHold) {
    return "the view zoomed in the last two game turns, which is the hold of "
           "both buttons";
  }
  if (moves.turned) {
    return word == 0 ? "the view turned in the last two game turns, which is "
                       "the hold of the Move Button at the rotate arrow"
                     : "the view turned in the last two game turns";
  }
  return "a grip holds the land, which is the hold of the Move Button";
}

// The game's word pushed its answer. The mod takes it off the stack and
// pushes the answer of the moves of the God view, with the same type.
void ScriptMoveAnswer(int word) {
  uint8_t* machine = *reinterpret_cast<uint8_t* const*>(kScriptMachine);
  if (machine == nullptr || g_script_moves == 0 || !ScriptPlayerNow()) return;
  const ScriptPopFn pop = reinterpret_cast<ScriptPopFn>(kScriptPop);
  const ScriptPushFn push = reinterpret_cast<ScriptPushFn>(kScriptPush);
  int type = 0;
  const uint32_t game = pop(machine, nullptr, &type);
  const near_math::ViewMoves moves = ScriptViewMoves();
  const unsigned features =
      *reinterpret_cast<const uint32_t*>(kCameraFeatures);
  const unsigned answer =
      word == 0 ? near_math::ScriptHandState(game, moves, features)
                : near_math::ScriptArrows(game, moves, features);
  push(machine, nullptr, answer, type);
  if (answer == game) return;
  ++g_moves_answers;
  if (g_moves_said[word][0] == game && g_moves_said[word][1] == answer) {
    return;
  }
  g_moves_said[word][0] = game;
  g_moves_said[word][1] = answer;
  if (g_moves_logs >= kMaxMovesLogs) return;
  ++g_moves_logs;
  ModLog("scripts      : a script asked %s. The game said 0x%X, and the "
         "script gets 0x%X: %s (script_camera_moves=1, camera controls 0x%X), "
         "frame %llu",
         word == 0 ? "for the hand state" : "if the rotate arrow shows",
         static_cast<unsigned>(game), answer,
         ScriptMoveWhy(moves, word, answer), features, g_frame);
}

void __cdecl h_ScriptHandState() {
  o_ScriptHandState();
  ScriptMoveAnswer(0);
}

void __cdecl h_ScriptArrows() {
  o_ScriptArrows();
  ScriptMoveAnswer(1);
}

bool ScriptBytes(uintptr_t address, const uint8_t* bytes, size_t count) {
  const void* at = reinterpret_cast<const void*>(address);
  return !IsBadReadPtr(at, count) && memcmp(at, bytes, count) == 0;
}

void ScriptPlayerInstall() {
  g_script_distance = IniKey("script_distance", 1) != 0 ? 1 : 0;
  g_script_view = IniKey("script_view", 1) != 0 ? 1 : 0;
  static const uint8_t kDistanceBytes[10] = {0x83, 0xEC, 0x30, 0x56, 0x8B,
                                             0x35, 0x10, 0x5C, 0xD9, 0x00};
  static const uint8_t kPosViewBytes[10] = {0x8B, 0x0D, 0x5C, 0x19, 0xD0,
                                            0x00, 0x83, 0xEC, 0x10, 0xE8};
  static const uint8_t kThingViewBytes[10] = {0x8B, 0x0D, 0x5C, 0x19, 0xD0,
                                              0x00, 0x83, 0xEC, 0x14, 0xE8};
  static const uint8_t kThingSeesBytes[10] = {0x83, 0xEC, 0x24, 0x8B, 0x0D,
                                              0x10, 0x5C, 0xD9, 0x00, 0x56};
  static const uint8_t kPositionBytes[10] = {0x8B, 0x0D, 0x10, 0x5C, 0xD9,
                                             0x00, 0x83, 0xEC, 0x14, 0x56};
  static const uint8_t kThingBytes[9] = {0x8B, 0x44, 0x24, 0x04, 0x56,
                                         0x33, 0xF6, 0x85, 0xC0};
  static const uint8_t kPopBytes[8] = {0x8B, 0x44, 0x24, 0x04,
                                       0x50, 0xFF, 0x51, 0x50};
  static const uint8_t kPushBytes[13] = {0x8B, 0x44, 0x24, 0x08, 0x8B,
                                         0x54, 0x24, 0x04, 0x50, 0x52,
                                         0xFF, 0x51, 0x54};
  if (!ScriptBytes(kScriptPop, kPopBytes, sizeof(kPopBytes)) ||
      !ScriptBytes(kScriptPush, kPushBytes, sizeof(kPushBytes))) {
    ModLog("hooks        : the stack functions of the script machine do not "
           "have the bytes of the disassembly, so a script that asks about "
           "the camera gets the game's camera");
    return;
  }
  bool distance = false, view = false;
  if (!ScriptBytes(kScriptDistance, kDistanceBytes, sizeof(kDistanceBytes))) {
    ModLog("hooks        : GScript::GetDistance at 0x%08X does not have the "
           "bytes of the disassembly, so a script measures from the game's "
           "camera", static_cast<unsigned>(kScriptDistance));
  } else {
    distance =
        Hook(kScriptDistance, reinterpret_cast<void*>(&h_ScriptDistance),
             reinterpret_cast<void**>(&o_ScriptDistance),
             "GScript::GetDistance (the distance from the player)");
  }
  if (!ScriptBytes(kScriptPosView, kPosViewBytes, sizeof(kPosViewBytes)) ||
      !ScriptBytes(kScriptThingView, kThingViewBytes,
                   sizeof(kThingViewBytes)) ||
      !ScriptBytes(kScriptThingSees, kThingSeesBytes,
                   sizeof(kThingSeesBytes)) ||
      !ScriptBytes(kScriptPosition, kPositionBytes, sizeof(kPositionBytes)) ||
      !ScriptBytes(kScriptThing, kThingBytes, sizeof(kThingBytes))) {
    ModLog("hooks        : the view words of the scripts do not have the "
           "bytes of the disassembly, so a script that asks if the camera "
           "sees a thing gets the game's camera");
  } else {
    view = Hook(kScriptPosView, reinterpret_cast<void*>(&h_ScriptPosView),
                reinterpret_cast<void**>(&o_ScriptPosView),
                "GScript::IsPosFieldOfView (the view of the head)") &&
           Hook(kScriptThingView, reinterpret_cast<void*>(&h_ScriptThingView),
                reinterpret_cast<void**>(&o_ScriptThingView),
                "GScript::IsGameThingFieldOfView (the view of the head)") &&
           Hook(kScriptThingSees, reinterpret_cast<void*>(&h_ScriptThingSees),
                reinterpret_cast<void**>(&o_ScriptThingSees),
                "GScript::GameThingCanViewCamera (the place of the head)");
  }
  if (!distance) g_script_distance = 0;
  if (!view) g_script_view = 0;
  // The two words that ask how the player moves the camera.
  static const uint8_t kHandStateBytes[17] = {
      0x8B, 0x0D, 0x5C, 0x19, 0xD0, 0x00, 0xE8, 0x15, 0x61,
      0xE5, 0xFF, 0x8B, 0x80, 0xAC, 0x03, 0x00, 0x00};
  static const uint8_t kArrowsBytes[13] = {0xA1, 0xF4, 0xB0, 0xC5, 0x00,
                                           0x8B, 0x0D, 0x10, 0x5C, 0xD9,
                                           0x00, 0x6A, 0x06};
  bool moves = false;
  g_script_moves = IniKey("script_camera_moves", 1) != 0 ? 1 : 0;
  if (g_script_moves != 0) {
    if (!ScriptBytes(kScriptHandState, kHandStateBytes,
                     sizeof(kHandStateBytes)) ||
        !ScriptBytes(kScriptArrows, kArrowsBytes, sizeof(kArrowsBytes))) {
      ModLog("hooks        : GScript::GetHandState or GScript::WithinRotation "
             "does not have the bytes of the disassembly, so a script that "
             "asks how you move the camera gets the game's answer");
    } else {
      moves = Hook(kScriptHandState,
                   reinterpret_cast<void*>(&h_ScriptHandState),
                   reinterpret_cast<void**>(&o_ScriptHandState),
                   "GScript::GetHandState (the moves of the God view)") &&
              Hook(kScriptArrows, reinterpret_cast<void*>(&h_ScriptArrows),
                   reinterpret_cast<void**>(&o_ScriptArrows),
                   "GScript::WithinRotation (the turns of the God view)");
    }
  }
  if (!moves) g_script_moves = 0;
  ModLog("scripts      : script_camera_moves=%d. %s", g_script_moves,
         g_script_moves != 0
             ? "In the God view a script that asks how you hold the land, or "
               "if the rotate arrow shows, gets the moves of the view: a zoom "
               "is the hold of both buttons (hand state 0x16), a turn and a "
               "grip on the land are the hold of the Move Button (0x14), and "
               "a turn shows the rotate arrow. 0 gives the game's answers"
             : "A script that asks how you hold the land, or if the rotate "
               "arrow shows, gets the game's answer");
  g_script_hooked = distance || view || moves;
  if (!g_script_hooked) return;
  ModLog("scripts      : script_distance=%d, script_view=%d. In the God view "
         "a script of the game that asks about the camera gets the player. "
         "With script_distance=1 the distance from the camera to a thing is "
         "the distance on the map from your head or your nearest hand. With "
         "script_view=1 the camera sees a thing when your head sees it. In a "
         "cut scene, and while the game moves its own camera, a script gets "
         "the game's camera. 0 gives the game's camera%s", g_script_distance,
         g_script_view,
         IniKey("test_keys", 0) != 0
             ? ". Ctrl and the 7 key of the number pad change both"
             : "");
}

}  // namespace

// For the fake controller's word "seek" on the table: offer the objects
// within 50 cm of the room of the Hand to the pick, for a few frames.
void HandDirectWidenPick(int frames) {
  g_wide_frames = frames;
  g_wide_meters = 0.0f;
}

// The word "aimfar": the same for a ball of the given meters of the room.
void HandDirectWidenFar(int frames, float meters) {
  g_wide_frames = frames;
  g_wide_meters = meters;
}

// Once a frame, after the hands of the frame are known and before the eye
// passes.
void HandDirectFrame() {
  ++g_frame;
  if (g_script_hooked) LatchPlayer();
  g_candidate_count = 0;
  g_ball_count = 0;
  g_scroll_count = 0;
  ++g_round;
  g_near_pick = false;
  // On the table (docs/tabletop.md, "The hand on the table"), the Hand has a
  // fixed length in the room, and the draw offers the whole island.
  const float table_size = TabletopHandSize();
  g_candidate_limit = table_size > 0.0f ? kMaxCandidates : kLifeCandidates;
  g_table_hand = g_table_hooked && table_size > 0.0f;
  g_land_done = false;
  g_on_land = false;
  float point[3], factor = 1.0f, real = 0.0f;
  const bool want = g_installed && ControllerDirectHand(point, &factor, &real);
  g_face = g_installed ? Interface() : nullptr;
  g_hand = g_face != nullptr
               ? *reinterpret_cast<uint8_t* const*>(g_face + kFaceHand)
               : nullptr;
  if (g_hand != nullptr) {
    const float limit =
        *reinterpret_cast<const float*>(g_hand + kHandMaxDistance);
    g_far_limit = (limit >= 10.0f && limit <= 100000.0f) ? limit
                                                         : kDefaultFarLimit;
  }
  // Each change of the Hand's state and of the interface's hand mode, so a
  // test can tell when the player has the Hand.
  if (g_hand != nullptr && g_state_logs < kMaxStateLogs) {
    const int32_t state = State(g_hand);
    const int32_t mode = *reinterpret_cast<const int32_t*>(g_face + kFaceHandMode);
    const int locked = *(g_face + kFaceLocked);
    if (state != g_logged_state || mode != g_logged_mode ||
        locked != g_logged_locked) {
      ++g_state_logs;
      ModLog("hand         : state %d, interface hand mode 0x%X, pick %s, "
             "frame %llu", state, mode, locked != 0 ? "locked" : "open",
             g_frame);
      g_logged_state = state;
      g_logged_mode = mode;
      g_logged_locked = locked;
    }
  }
  if (g_face != nullptr) LogTake(g_face, g_hand);
  if (g_turn_now > 0) --g_turn_now;
  PollReleaseKey();
  PollNearKey();
  PollChoiceKey();
  PollStayKey();
  PollTakeCarryKey();
  TakeStreamFrame(g_frame);
  PollCatchKey();
  PollWideKey();
  LogRelease();
  const bool active = want && g_hand != nullptr;
  g_owns = active;
  if (!want) g_face = nullptr;
  if (active != g_said_active) {
    g_said_active = active;
    ModLog("hand         : the %s places the Hand, frame %llu",
           active ? "controller" : "game", g_frame);
  }
  g_active = active;
  if (!active) {
    FarFrameStep(false);
    g_choice = nullptr;
    g_history_count = 0;
    g_tip_count = 0;
    g_path_count = 0;
    g_in_creature = false;
    g_touch_valid = false;
    g_touch_inside = false;
    g_touch_from_valid = false;
    return;
  }
  ++g_active_frames;
  // The game turn before this draw can have deleted the choice of the last
  // draw. Each reader of the choice in this frame gets a live object or none.
  KeepLiveChoice("at the start of the frame");
  {
    // The motion history of the throw and of the Creature.
    float reach_room[3], grip_room[3], room[9];
    if (ControllerDirectMotion(reach_room, grip_room, room)) {
      for (int i = 0; i < 9; ++i) g_room[i] = room[i];
      PushHistory(reach_room, grip_room);
    } else {
      g_history_count = 0;
    }
  }
  // THE POINT OF THE FLAT HAND. The palm of the frame before gives the offset
  // from the tip to the palm. In the Creature state with the flat hand the
  // point of this frame is the tip of this frame and that offset.
  {
    float palm[3];
    uint64_t palm_frame = 0;
    int32_t palm_state = -1;
    // g_tip is still the tip of the frame before: the point that the mod's
    // hand of that frame stood on.
    if (HandExactPalm(palm, &palm_frame, &palm_state) &&
        palm_frame + 1 == g_frame) {
      near_math::PalmOffset(palm, g_tip, g_palm_offset);
      g_palm_offset_frame = g_frame;
    }
  }
  const bool flat_hand = State(g_hand) == kStateCreature &&
                         g_stroke_frame + 2 >= g_frame && !g_stroke_finger &&
                         g_stroke_mode != kStrokeBefore;
  float palm_point[3];
  g_palm_now = near_math::PalmPoint(
      point, g_palm_offset,
      g_palm_offset_frame != 0
          ? static_cast<int>(g_frame - g_palm_offset_frame)
          : -1,
      flat_hand, g_palm_point, palm_point);
  for (int i = 0; i < 3; ++i) {
    g_point[i] = point[i];
    g_tip[i] = point[i];
    g_target[i] = palm_point[i];
  }
  if (g_palm_now) ++g_palm_frames;
  if (g_palm_now != g_palm_said && g_palm_point_logs < kMaxPalmPointLogs) {
    ++g_palm_point_logs;
    g_palm_said = g_palm_now;
    const float units = RoomUnits();
    if (g_palm_now) {
      ModLog("hand creature: the point of the flat hand is the palm now: the "
             "glow shows there, and the ray of the game's stroke goes through "
             "it. The palm is %.1f cm of the room from the tip of the index "
             "finger (creature_palm_point=1), frame %llu",
             units > 0.0f ? Distance(g_target, g_point) / units * 100.0f
                          : 0.0f,
             g_frame);
    } else {
      ModLog("hand creature: the point of the Hand is the tip of the index "
             "finger again%s, frame %llu",
             flat_hand ? (g_palm_point ? ": the palm of the mod's hand is not "
                                         "known now"
                                       : " (creature_palm_point=0)")
                       : "",
             g_frame);
    }
  }
  // The Hand's point is a point of the player for the scripts.
  if (g_player.valid &&
      Distance(g_point, g_player.points.head_at) <= 2.5f * RoomUnits()) {
    g_player.points.hand[0] = true;
    for (int i = 0; i < 3; ++i) g_player.points.hand_at[0][i] = g_point[i];
  }
  PushTip(g_point);
  PushPath(g_target[0], g_target[2]);
  g_factor = factor;
  g_real_distance = real;
  FarFrameStep(true);
  // The Hand is the size of the real hand at the world scale, and it never
  // grows with the distance, or it would be the size of a couch. On the table
  // the size gives table_hand_cm in the room at the table scale.
  g_scale = table_size > 0.0f ? table_size : RealHandSize();
  // The pick on the table: the objects within twice the grab radius, and 5
  // units more, go to the pick. The word "seek" widens that to 50 cm of the
  // room for a few frames.
  if (g_table_hooked && table_size > 0.0f) {
    // With the rule of the near objects, the bound also holds the distance
    // at which the far grab comes back, and the point of a large object is
    // farther from the Hand than its body.
    const float reach = PickReach();
    float bound = 2.0f * reach + 5.0f;
    if (g_near_rule) {
      const float out = near_math::CmToUnits(static_cast<float>(g_near_out_cm),
                                             RoomUnits());
      if (out > 2.0f * reach) bound = out + 5.0f;
      bound += 5.0f;
    }
    if (g_wide_frames > 0) {
      --g_wide_frames;
      // A seek of a bulk source looks 2 m of the room around the Hand, so
      // the log can name one that is out of reach.
      float wide =
          (SeekBulk() || SeekOther() != 0 || PlanCount() > 0 ? 2.0f : 0.5f) *
          TabletopViewScale();
      if (g_wide_meters > 0.0f) wide = g_wide_meters * TabletopViewScale();
      if (wide > bound) bound = wide;
    }
    g_near_bound_sq = bound * bound;
    g_near_pick = true;
  }
  if (!g_said_sizes) {
    g_said_sizes = true;
    ModLog("hand         : the game's Hand has size %.3f and a far limit of "
           "%.1f units. The direct hand uses size %.3f up close.",
           *reinterpret_cast<const float*>(g_hand + kHandSize),
           *reinterpret_cast<const float*>(g_hand + kHandMaxDistance),
           ControllersHandScalePct() * 0.01f);
  }
}

// The ray of the Hand's own screen point, from UnprojectScreenPoint
// (pointer_map.cpp). The origin and the length stay as the game made them.
// Only the direction changes, to the target.
void HandDirectUnproject(const int32_t* screen, float* out) {
  if (!g_active || g_hand == nullptr) return;
  const uint8_t* at = reinterpret_cast<const uint8_t*>(screen);
  if (!RayState(g_hand)) return;
  if (at == g_hand + kHandScreen) {
    if (AimAt(g_target, out)) ++g_rays;
  } else if (at == g_hand + kHandPrevScreen && g_in_creature &&
             g_prev_valid) {
    // The Creature's speed test asks for the last point. It gets the point
    // one game step back across the line of sight (TouchFrame). After a
    // forced slap branch the last x goes back one pixel behind the current
    // one, before the game reads the direction of the slap from it.
    if (g_touch_fix_x) {
      g_touch_fix_x = false;
      int32_t* last = const_cast<int32_t*>(screen);
      const int32_t* now =
          reinterpret_cast<const int32_t*>(g_hand + kHandScreen);
      last[0] = now[0] - g_touch_step_x;
    }
    AimAt(g_prev_point, out);
  }
}

// For controllers.cpp: the far limit of the reach, in world units.
float HandDirectFarLimit() { return g_far_limit; }

// For hand_glow.cpp (docs/hand-model.md, "The glow at the Hand's point"): the
// point the Hand interacts from, the Hand's length in world units, the
// interface hand mode (GInterface+0x3AC), whether the Hand holds an object,
// the eye pass and the frame. The point is the target that the render pick
// looks through and that the pick of holdable objects measures from. The
// first ask of a frame latches the values, so both eye passes get the same
// ones. False unless the controller places the Hand in the Normal, the
// Holding or the Creature state and the hooks placed it in this frame. Then
// the land test is done too.
bool HandDirectGlowFacts(float point[3], float* length, int32_t* mode,
                         bool* holding, int* eye, uint64_t* frame) {
  if (!g_active || g_hand == nullptr || g_face == nullptr || g_eye < 0) {
    return false;
  }
  if (g_glow_frame != g_frame) {
    g_glow_frame = g_frame;
    // In the Creature state the glow has the activate look while the Hand's
    // point is in the body (the mode 0x13 of a touch), and it is pale blue
    // when it is not.
    const bool creature = State(g_hand) == kStateCreature;
    g_glow_valid = (DirectState(g_hand) || creature) && g_land_done;
    if (g_glow_valid) {
      for (int i = 0; i < 3; ++i) g_glow_point[i] = g_target[i];
      g_glow_length = kHandLength * g_scale;
      g_glow_mode = *reinterpret_cast<const int32_t*>(g_face + kFaceHandMode);
      if (creature) g_glow_mode = g_touch_inside ? kModeCreature : 3;
      g_glow_holding = !HandEmpty();
      g_glow_kind = PressKindAtPoint(g_glow_mode);
    }
    LatchNearTarget();
  }
  if (!g_glow_valid) return false;
  for (int i = 0; i < 3; ++i) point[i] = g_glow_point[i];
  *length = g_glow_length;
  *mode = g_glow_mode;
  *holding = g_glow_holding;
  *eye = g_eye;
  *frame = g_frame;
  return true;
}

// For hand_glow.cpp, at the end of the world of the first eye pass, before it
// asks for the facts of the glow: with one target the choice of this frame is
// made now, from the candidates that the draw of this pass offered. So the
// glow and the halo of this frame show the object that the next press acts
// on. The second eye pass draws the same.
void HandDirectChooseNow() {
  if (!g_one_target || !g_active || g_hand == nullptr || g_face == nullptr ||
      g_eye != 0) {
    return;
  }
  ChooseOnce();
  if (g_glow_frame == g_frame && g_glow_valid) {
    g_glow_kind = PressKindAtPoint(g_glow_mode);
    LatchNearTarget();
  }
}

// For hand_glow.cpp: hand_glow_idle, and its A/B key.
bool HandDirectGlowIdle() { return g_glow_idle; }

// For far_grab.cpp (docs/creature.md, "The leash from a distance"): the leash
// click on the player's own Creature, as the game sends it after a short
// press on the Creature (packet 0x5F, GGame::SetPacket). No press goes to the
// game, so the Creature state does not start. False when the game would do
// nothing with the click: then the mod sends none.
bool HandDirectLeashClick(void* creature) {
  if (!g_installed || !g_owns || g_face == nullptr) return false;
  if (LeashKind(creature) == near_math::kLeashNone) return false;
  void* game = *reinterpret_cast<void* const*>(kGame);
  if (game == nullptr) return false;
  reinterpret_cast<SetPacketFn>(kSetPacket)(game, nullptr, kPacketLeash);
  ++g_leash_clicks;
  ++g_far_leash_clicks;
  CreatureCommandLeashAsked();
  return true;
}

// For hint_card.cpp: what an opening of the grip does with the tree in the
// Hand now, as a tree_plant_math::Verdict, or -1 with no tree in the Hand or
// with tree_plant_hint=0. Latched once a frame.
int HandDirectTreeVerdict() {
  return g_tree_hint != 0 ? g_tree_verdict : -1;
}

// The A/B key of the roots pile (test_keys=1, Ctrl, Shift and Page Down).
void HandDirectRootsToggle() {
  ReadTreeHintKey();
  g_roots_at_tree = g_roots_at_tree != 0 ? 0 : 1;
  ControllersGesturePulse(0.5f, g_roots_at_tree != 0 ? 0.24f : 0.08f);
  ModLog("test key     : Ctrl, Shift and Page Down: far_roots_at_tree is %d now "
         "(%s)", g_roots_at_tree,
         g_roots_at_tree != 0 ? "the roots pile stands at the roots"
                              : "the roots pile stays on the way of the glide");
}

// The A/B key of the card (test_keys=1, Ctrl and Page Down).
void HandDirectTreeHintToggle() {
  ReadTreeHintKey();
  g_tree_hint = g_tree_hint != 0 ? 0 : 1;
  ControllersGesturePulse(0.5f, g_tree_hint != 0 ? 0.24f : 0.08f);
  ModLog("test key     : Ctrl and Page Down: tree_plant_hint is %d now (%s)",
         g_tree_hint, g_tree_hint != 0 ? "the card says what the opening of "
                                         "the grip does with a tree"
                                       : "the card shows the text of the game");
}

// For overlay.cpp: the hand of a stroke of the Creature, and what the tickle
// adds to the curl of each finger (StrokeHand).
int HandDirectStrokeHand(float curl[5]) { return StrokeHand(curl); }

// For hand_glow.cpp: the target of the grip at the Hand's point, for the halo
// (one target). The middle of its body, its radius and half its height in
// world units, and whether a press acts on it now (gold) or the influence
// stops it (red). Latched with the facts of the glow. False with no target.
bool HandDirectNearTarget(float center[3], float* radius, float* half_height,
                          bool* acts) {
  if (!g_near_target) return false;
  for (int i = 0; i < 3; ++i) center[i] = g_near_target_center[i];
  *radius = g_near_target_radius;
  *half_height = g_near_target_half;
  *acts = g_near_target_acts;
  return true;
}

// For hand_glow.cpp and target_glow.cpp: the object of that target, or null.
// The answer is from the latch of this frame or of the frame before: the
// glow on the mesh takes its targets before the eye passes of a frame, and
// the latch of that frame comes in its first eye pass.
void* HandDirectNearTargetObject(bool* acts) {
  if (!g_near_target || !g_active || g_hand == nullptr ||
      g_glow_frame + 1 < g_frame) {
    return nullptr;
  }
  if (acts != nullptr) *acts = g_near_target_acts;
  return g_near_target_object;
}

// For hand_glow.cpp, and for the hint card: what a press of the grip does at
// the Hand's point now, as a near_math::Kind. 0 is nothing, kHold the pick
// up, kTake the bulk take, kTap a tap (a scroll, a house, a bubble, a leash)
// and kCreature the Creature. Latched with the facts of the glow, so ask
// HandDirectGlowFacts first.
int HandDirectPressKind() { return g_glow_valid ? g_glow_kind : 0; }

// For hand_exact.cpp (docs/hand.md, "The exact Hand"): the CHand of this
// frame, and whether the controller places it now.
void* HandDirectHand(bool* active) {
  *active = g_active && g_hand != nullptr;
  return g_hand;
}

// For hand_exact.cpp: the tip of the index finger after the land test, the
// Hand's size and the frame, while the controller places the Hand. The mod's
// hand stands on this point. It is the target, but not while the point of the
// flat hand is the palm: the hand does not move to the palm. Called from
// inside a Hand state, as the land test needs. The values are latched once a
// frame.
bool HandDirectExactFacts(float target[3], float* size, uint64_t* frame) {
  if (!g_active || g_hand == nullptr) return false;
  ResolveLand();
  for (int i = 0; i < 3; ++i) target[i] = g_tip[i];
  *size = g_scale;
  *frame = g_frame;
  return true;
}

// For overlay.cpp (docs/hand-model.md, "Overlay renderer"): the game's Hand
// position (CHand+0x78), and whether the controller places the Hand in this
// frame. With the controller, the position is the target, the point that the
// glow marks. False with no CHand.
bool HandDirectHandPosition(float out[3], bool* by_controller) {
  if (g_hand == nullptr) return false;
  const float* at = reinterpret_cast<const float*>(g_hand + kHandPosition);
  for (int i = 0; i < 3; ++i) out[i] = at[i];
  *by_controller = g_owns;
  return true;
}

// For the grab of the world: whether the pointing hand's grip may grab
// the world in place of a pick up. The Hand must be the controller's, in its
// Normal state, empty, and over nothing it can hold, by the mod's own pick
// and by the game's. These are from the last frame, which is the frame the
// player saw. why says the first reason for a no.
bool HandDirectClaimFree(const char** why) {
  if (!g_installed || !g_owns || g_hand == nullptr || g_face == nullptr) {
    *why = "the controller does not place the Hand";
    return false;
  }
  if (State(g_hand) != kStateNormal) {
    *why = "the Hand is not in its Normal state";
    return false;
  }
  if (!HandEmpty()) {
    *why = "the Hand holds an object";
    return false;
  }
  if (g_choice != nullptr) {
    *why = "the Hand is over an object it can hold";
    return false;
  }
  // NEAR OBJECTS WIN. The pick now reaches only 4 cm, so a grip that closes
  // 7 cm from a tree has no object. It must not move the world then: the
  // player reaches for the tree. So near an object that a press acts on, the
  // grip is no grab of the world, as it is no grab from a distance.
  if (g_near_blocked) {
    *why = "an object that a press acts on is near the Hand's point";
    return false;
  }
  if (*reinterpret_cast<void* const*>(g_face + kFaceObject) != nullptr) {
    *why = "the game has an object under the Hand";
    return false;
  }
  *why = "the Hand is empty and over nothing it can hold";
  return true;
}

// For the grip (controllers.cpp, docs/grab.md, "The grip holds"): what the
// grip needs to know of the Hand, read now, at the pump after the frame.
// holding: the Holding state with an object that is no Miracle. settled: the
// pick lock GInterface+0x38 is clear, so the game's pick up is over. spring:
// the interface hand mode is 0x17, the throw spring that a press while
// holding starts. False when the controller did not place the Hand on the
// last frame.
//
// THE BULK TAKE (docs/grab.md, "The bulk take"). While the game takes from a
// pile, a field or a fish farm, the take runs only while the right button is
// down, as a held mouse button. The Pot is in the Hand from the first turn,
// and a carry lets the button go up, which ends the take with the first
// amount. So while a take runs, take is true and the machine makes the grip
// the game's button, one to one (GripStep).
// The opening of the grip ends the take, the Pot stays in the Hand, and the
// machine then sees an object in the Hand with the grip open. The next close
// carries it and the next opening lets go.
bool HandDirectGripFacts(bool* holding, bool* settled, bool* spring,
                         bool* take) {
  *holding = false;
  *settled = false;
  *spring = false;
  *take = false;
  if (!g_installed || !g_owns || g_hand == nullptr || g_face == nullptr) {
    return false;
  }
  if (TakeObject(g_face) != nullptr) {
    ++g_take_grip_frames;
    *take = true;
    return true;
  }
  void* held = *reinterpret_cast<void* const*>(g_hand + kHandHeld);
  *holding = State(g_hand) == kStateHolding && held != nullptr &&
             reinterpret_cast<VirtualIntFn>(VTable(held)[kIsSpellSeed / 4])(
                 held, nullptr) == 0;
  *settled = g_face[kFaceLocked] == 0;
  *spring = *reinterpret_cast<const int32_t*>(g_face + kFaceHandMode) ==
            kModeThrowSpring;
  return true;
}

// For the grip (GripStep, docs/grab.md, "The grip holds"): the facts of the
// press of the game, read now, at the pump after the frame.
//
// game_holds: the game has an object in its Hand (GInterfaceStatus+0x90). A
// press of the Action button is then the press of a release
// (fn_005D1330 calls fn_005D1560), and not a press on an object.
//
// moving: the Hand of the picture has an object (CHand+0x4904) that the
// physics of the game has. That is the test of the game itself: bit 0x40 of
// Object+0x24 is set, and bit 0x10 of Object+0x0A is clear (fn_0046DC30, and
// the step of the pick up action at 0x005D529F). The game takes such an
// object only after the button was down for 225 ms.
//
// press_age_ms: the age of the last press on an object, on the clock of the
// game (controller_math::GamePressAge). False when the controller did not
// place the Hand on the last frame.
bool HandDirectGripPress(bool* game_holds, bool* moving,
                         uint32_t* press_age_ms) {
  *game_holds = false;
  *moving = false;
  *press_age_ms = 0xFFFFFFFFu;
  if (!g_installed || !g_owns || g_hand == nullptr || g_face == nullptr) {
    return false;
  }
  const uint8_t* status =
      *reinterpret_cast<uint8_t* const*>(g_face + kFaceStatus);
  const uint8_t* game = *reinterpret_cast<uint8_t* const*>(kGame);
  if (status == nullptr || game == nullptr) return false;
  *game_holds = status[kStatusHolding] != 0;
  const uint8_t* held = *reinterpret_cast<uint8_t* const*>(g_hand + kHandHeld);
  *moving = held != nullptr && !IsBadReadPtr(held, kObjectFlags + 1) &&
            (held[kObjectFlags] & 0x40) != 0 &&
            (held[kObjectOtherFlags] & 0x10) == 0;
  *press_age_ms = controller_math::GamePressAge(
      *reinterpret_cast<const uint32_t*>(kGameClock),
      *reinterpret_cast<const uint32_t*>(g_face + kFacePressClock),
      *reinterpret_cast<const uint32_t*>(game + kGameTurn),
      *reinterpret_cast<const uint32_t*>(g_face + kFacePressTurn));
  return true;
}

// For the grip: the control of the game's right button is fully open now
// (its value is at or below the level that opens a slow grip). The guard of
// a throw ends here (near_math::GuardHolds, catch_moving=3): the next close
// is a new press of the player, and no dip of the squeeze.
void HandDirectGripFullyOpen() { g_grip_reopened = true; }

// For the grip: the take of the mod from a forest runs (forest_take.cpp). The
// closed grip collects with no button of the game, and the opening keeps the
// bundle (GripFacts::mod_take).
bool HandDirectModTake() {
  return g_installed && g_owns && ForestTakeRuns();
}

// For controllers.cpp: the release at once is on now (release_at_once, and
// the A/B key). The fast opening of the grip follows it.
bool HandDirectReleaseAtOnce() { return g_turn_on; }

// For the grip: a bulk take still runs, and the button was up for
// kGripTakeEndMs (GripStepOut::end_take). The next interface step ends
// the take (EndTakeNow). Once a frame while the machine asks.
void HandDirectEndTake() {
  if (!g_installed || !g_owns) return;
  ++g_take_end_asks;
}

// For the grip: it opened now. Latch the motion of the kThrowWindow before
// this moment for the release that follows (see ThrowLatch). The history's
// newest sample is from this frame, as is the grip.
void HandDirectGripOpened() {
  // The grip opened by the rule of the grip machine. It is fully open only
  // when the pump says so (HandDirectGripFullyOpen): a fast fall opens the
  // grip at a value that is still high.
  g_grip_reopened = false;
  if (!g_installed || !g_owns) return;
  g_latch.valid = true;
  g_latch.time = NowSeconds();
  g_latch.frame = g_frame;
  // The Hand's point over the land (the sea gives 0), for the height rule of
  // the release (docs/throw.md, "The height of a release").
  g_latch.height = g_target[1] - g_land_y;
  g_latch.moving =
      HandVelocity(kThrowWindow, g_latch.v_room, g_latch.v_grip, &g_latch.real);
  if (!g_latch.moving) {
    for (int i = 0; i < 3; ++i) g_latch.v_room[i] = g_latch.v_grip[i] = 0.0f;
    g_latch.real = 0.0f;
  }
}

// stereo.cpp, around each eye pass. On the table the choice of the last pass
// goes into the next record before the pass draws too, so the game sees the
// choice all through the pass.
void HandDirectPassBegin(int eye) {
  g_eye = eye;
  // The test key target_check_test_frame: the choice of the last draw is a
  // dead object now, as after a delete of the game in the game turn. Its
  // first word points to a table of zeros, so a virtual call through it goes
  // to address 0.
  if (eye == 0 && g_gone_test_frame > 0 && !g_gone_test_done &&
      g_active && g_near_pick && g_choice != nullptr && !g_choice_far &&
      ControllersTestFrame(g_gone_test_frame, g_frame) >=
          g_gone_test_frame) {
    g_gone_test_done = true;
    static void* s_zeros[0x200 / sizeof(void*)] = {};
    static void* s_dead[0x100 / sizeof(void*)] = {};
    s_dead[0] = s_zeros;
    ModLog("hand pick    : test key target_check_test_frame=%llu. The target "
           "%p (%s) becomes the dead object %p, frame %llu",
           g_gone_test_frame, g_choice, ClassName(g_choice),
           static_cast<void*>(s_dead), g_frame);
    g_choice = s_dead;
  }
  if (g_active && g_near_pick && g_choice != nullptr) Inject();
}

void HandDirectPassDone(int eye) {
  g_eye = -1;
  if (!g_active) return;
  if (eye == 0) {
    TrackThrow();
    ChooseOnce();
    FarSelect();
    CreatureCommandSelect();
    MiracleSelect();
    VillageSelect();
    ForestTakeSelect();
    LogFetch();
    TreeReleaseFrame();
    LogCheck();
    LogPickState();
  }
  Inject();
  KeepCreature();
  KeepTakeSource(g_face);
  MiracleKeepRecord();
  VillageKeepRecord();
  // The frame is over. A walk of the scene outside the two eye passes, for
  // example the single pass the mod falls back to, places the Hand as the
  // game does.
  if (eye == 1) g_active = false;
}

// For creature_command.cpp (docs/creature.md, "The commands"): the
// candidates of the last first eye pass, the 2D radius of an object, whether
// the land hides a point from the eye of the aim, the class of an object, and
// the body bone of a Creature.
int HandDirectConeCount() { return g_candidate_count; }

bool HandDirectConeAt(int index, void** object, float position[3]) {
  if (index < 0 || index >= g_candidate_count) return false;
  *object = g_candidates[index].object;
  for (int i = 0; i < 3; ++i) position[i] = g_candidates[index].position[i];
  return true;
}

float HandDirectRadius(void* object) { return Radius2D(object); }

// For village.cpp (docs/grab.md, "An object in the Hand that goes onto a
// thing"): the body of a candidate of the first eye pass as the choice of the
// grip measures it (Measure), with the gap from a point to it in world units (0
// in the body), its middle, its radius and half its height. And whether the
// game takes from a bulk source now.
bool HandDirectConeBody(int index, const float point[3], float* gap,
                        float center[3], float* radius, float* half) {
  if (index < 0 || index >= g_candidate_count) return false;
  const Candidate& c = g_candidates[index];
  // Measure reads the Hand's point, so the point stands in for it.
  float keep[3];
  for (int k = 0; k < 3; ++k) {
    keep[k] = g_target[k];
    g_target[k] = point[k];
  }
  Measured m;
  Measure(c, IsCreatureObject(c.object) ? near_math::kCreature
                                        : near_math::kTake, &m);
  for (int k = 0; k < 3; ++k) g_target[k] = keep[k];
  *gap = m.target.gap.gap;
  for (int k = 0; k < 3; ++k) center[k] = m.center[k];
  *radius = m.radius;
  *half = m.half;
  return true;
}

bool HandDirectTakeRuns() {
  return g_face != nullptr && TakeObject(g_face) != nullptr;
}

// For miracle.cpp (docs/miracles.md, "A miracle in the Hand in the plain
// game"): world units in a meter of the room now, the point of the real hand of
// this frame, and the object of the next pick record. The record gets the
// target of a miracle with a distance that the game keeps
// (near_math::RecordDistance), or no object. False while the controller does
// not place the Hand.
float HandDirectRoomUnits() { return RoomUnits(); }

bool HandDirectRealPoint(float out[3]) {
  if (!g_owns || g_hand == nullptr) return false;
  for (int i = 0; i < 3; ++i) out[i] = g_target[i];
  return true;
}

// For temple.cpp: an object that the game offers to its pick with no mesh
// becomes a candidate of the choice, and its body is a ball about position.
// As each candidate, only in the first eye pass and one time a frame. words
// says what the game's tests say that a press does to it. False when the
// object is no candidate now.
bool HandDirectOffer(void* object, const float position[3], float radius,
                     const char** words) {
  *words = "";
  if (!g_active || g_eye != 0 || g_face == nullptr || object == nullptr ||
      g_candidate_count >= g_candidate_limit || g_ball_count >= kMaxBalls) {
    return false;
  }
  if (*(g_face + kFaceLocked) != 0) return false;
  void* status = *reinterpret_cast<void* const*>(g_face + kFaceStatus);
  *words = TapWords(object, status);
  if (!FirstOffer(object)) return false;
  g_balls[g_ball_count].object = object;
  g_balls[g_ball_count].radius = radius;
  ++g_ball_count;
  Candidate& c = g_candidates[g_candidate_count++];
  if (g_candidate_count > g_most_candidates) {
    g_most_candidates = g_candidate_count;
  }
  c.object = object;
  c.model = nullptr;
  for (int i = 0; i < 3; ++i) c.position[i] = position[i];
  return true;
}

bool HandDirectRecordTarget(void* object, const float position[3]) {
  if (!g_installed || !g_owns || g_face == nullptr) return false;
  const RecordPickFn record_pick = reinterpret_cast<RecordPickFn>(kRecordPick);
  if (object == nullptr) {
    record_pick(g_face + kFaceNextRecord, nullptr, nullptr, 0.0f);
    return true;
  }
  record_pick(g_face + kFaceNextRecord, nullptr, object,
              near_math::RecordDistance(
                  Distance(position, Camera()),
                  *reinterpret_cast<const float*>(g_face +
                                                  kFaceNextLandDistance)));
  return true;
}

// The object at the Hand: the object that a press of the grip acts on now,
// within hand_pick_cm of the Hand's point, where the glow is (Choose). It is
// from the draw of this frame. Not the object of a fetch from a distance.
bool HandDirectNearObject(void** object, float position[3]) {
  if (g_choice == nullptr || g_choice_far) return false;
  *object = g_choice;
  for (int i = 0; i < 3; ++i) position[i] = g_choice_position[i];
  return true;
}

// Whether an object from an earlier frame still lives (ObjectLives). table is
// the first word of the object from a frame in which the draw offered it.
bool HandDirectObjectLives(void* object, void* table) {
  return ObjectLives(object, table);
}

bool HandDirectSeen(const float at[3], float radius) {
  return FarVisible(at, radius);
}

const char* HandDirectClass(void* object) { return ClassName(object); }

// For crash_report.cpp: the Hand at a fault. The CHand and the GInterface of
// the last frame, the Hand state, the hand mode, the object in the Hand and
// the object of the next pick record, each with its class. The function only
// reads, and it checks each pointer before the read. False with no CHand.
bool HandDirectCrashFacts(int32_t* state, int32_t* mode, void** held,
                          char* held_class, size_t held_size, void** next,
                          char* next_class, size_t next_size,
                          uint64_t* frame) {
  *frame = g_frame;
  if (g_hand == nullptr || IsBadReadPtr(g_hand, kHandHeld + sizeof(void*))) {
    return false;
  }
  *state = State(g_hand);
  *held = *reinterpret_cast<void* const*>(g_hand + kHandHeld);
  strcpy_s(held_class, held_size,
           *held != nullptr ? ClassName(*held) : "nothing");
  *mode = 0;
  *next = nullptr;
  strcpy_s(next_class, next_size, "nothing");
  if (g_face != nullptr &&
      !IsBadReadPtr(g_face, kFaceNextObject + sizeof(void*))) {
    *mode = *reinterpret_cast<const int32_t*>(g_face + kFaceHandMode);
    *next = *reinterpret_cast<void* const*>(g_face + kFaceNextObject);
    if (*next != nullptr) strcpy_s(next_class, next_size, ClassName(*next));
  }
  return true;
}

// For creature_command.cpp: a living thing, by the class test of the value of
// far_grab_choice in use (FarLiving).
bool HandDirectLiving(void* object) {
  return object != nullptr && FarLiving(object, FarGrabChoice());
}

bool HandDirectCreatureBody(void* creature, float out[3]) {
  return creature != nullptr && BodyPlace(creature, out) != 0;
}

// For forest_take.cpp (docs/grab.md, "Many trees from a forest"): the trees
// and the forests that the draw offered in this frame and that the Hand can
// take, with the body of each. A tree in a hand is not in the list. And
// whether an object is a tree of some kind.
int HandDirectForestTrees(ForestCandidate* out, int max) {
  if (!g_owns || g_face == nullptr) return 0;
  void* status = *reinterpret_cast<void* const*>(g_face + kFaceStatus);
  int count = 0;
  for (int i = 0; i < g_candidate_count && count < max; ++i) {
    const Candidate& c = g_candidates[i];
    const bool forest = IsForest(c.object);
    if (!forest && !IsTreeObject(c.object)) continue;
    if (!Holdable(c.object, status)) continue;
    const Body body = BodyOf(c.object);
    ForestCandidate& t = out[count++];
    t.object = c.object;
    for (int k = 0; k < 3; ++k) t.base[k] = c.position[k];
    t.radius = body.radius;
    t.height = body.height;
    t.forest = forest;
    t.inside = InsideInfluence(g_face, ClampToMap(c.position[0]),
                               ClampToMap(c.position[2]));
  }
  return count;
}

bool HandDirectIsTree(void* object) {
  return object != nullptr && IsTreeObject(object);
}

// For hand_feel.cpp (docs/hand-model.md, "Hands that react to the world"): the
// body of a candidate of the last first eye pass, as the choice measures it.
// kind 0 is an upright cylinder from the point of the object, 1 a tree (the
// cylinder of the choice, for a trunk and a crown), 2 the capsule of a Creature
// and 3 the ball of a scroll, with its middle in base. kind 4 is the box of the
// mesh of each other object that has one: base is its middle, axis has its
// three axes of length 1 and half has half its sizes, as BoxMiddle finds them.
// False for a forest, which is one object with many trees, and for a body that
// is not known.
bool HandDirectFeelBody(int index, void** object, float base[3], float* radius,
                        float* height, int* kind, float axis[9],
                        float half[3]) {
  if (index < 0 || index >= g_candidate_count) return false;
  const Candidate& c = g_candidates[index];
  *object = c.object;
  if (IsForest(c.object)) return false;
  if (IsCreatureObject(c.object)) {
    controller_math::Capsule capsule;
    if (!CreatureBody(c.object, &capsule)) return false;
    for (int i = 0; i < 3; ++i) base[i] = capsule.base[i];
    *radius = capsule.radius;
    *height = capsule.height;
    *kind = 2;
    return true;
  }
  if (IsScrollObject(c.object)) {
    *height = 0.0f;
    *kind = 3;
    return BoxBall(c, base, radius);
  }
  const Body body = BodyOf(c.object);
  for (int i = 0; i < 3; ++i) base[i] = c.position[i];
  *radius = body.radius;
  *height = body.height;
  *kind = IsTreeObject(c.object) ? 1 : 0;
  if (!(body.radius > 0.0f && body.height > 0.0f)) return false;
  if (*kind == 1 || c.model == nullptr) return true;
  // The box of the mesh, with the scale that BoxMiddle uses: the game's
  // height over the height of the box.
  void** table = *reinterpret_cast<void** const*>(c.model);
  const uint8_t* box =
      reinterpret_cast<MeshBoxFn>(table[kMeshBox / 4])(c.model, nullptr);
  if (box == nullptr) return true;
  const float* middle = reinterpret_cast<const float*>(box + kBoxMiddle);
  const float* box_half = reinterpret_cast<const float*>(box + kBoxHalf);
  if (!(box_half[1] > 1e-4f)) return true;
  const float scale = body.height / (2.0f * box_half[1]);
  if (!(scale > 1e-4f && scale < 1e4f)) return true;
  const float* rows = reinterpret_cast<const float*>(c.model + kMeshRows);
  float center[3] = {c.position[0], c.position[1], c.position[2]};
  for (int j = 0; j < 3; ++j) {
    const float* row = rows + j * 3;
    const float length =
        sqrtf(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
    if (!(length > 1e-4f) || !(middle[j] == middle[j]) ||
        !(box_half[j] >= 0.0f && box_half[j] < 1e5f)) {
      return true;
    }
    for (int k = 0; k < 3; ++k) {
      axis[j * 3 + k] = row[k] / length;
      center[k] += scale * middle[j] * row[k] / length;
    }
    half[j] = scale * box_half[j];
  }
  for (int i = 0; i < 3; ++i) base[i] = center[i];
  *kind = 4;
  return true;
}

namespace {

// THE TEST KEY seek_plan, for the desktop runs of one target. A list of
// words with commas between them, one for each "seek" of the script. After
// the last word the list starts again.
//
//   scroll  The Hand's point goes to the middle of the ball of a scroll that
//           a press taps: of the scrolls within 2 m of the room of the Hand,
//           the one with a tree nearest to it.
//   air     The Hand goes over the land where a press has no target: the palm
//           of the mod's hand goes on the line from the camera to the base of
//           the nearest tree, 25 cm of the room from that base. The game's
//           search at the palm then finds that tree.
//   crown   The Hand's point goes to the crown of the nearest tree: on its
//           axis, at 80 percent of its height.
//   palm    The hand moves across the view, so that the land behind the palm
//           (the land point of the game's search at the palm) goes onto the
//           base of the tree nearest to it. Some seeks one after the other
//           bring it there. The Hand's point stays over the land.
//   creature  The Hand's point goes to the body bone of the nearest Creature
//           in view, within 2 m of the room of the Hand. The log gets each
//           Creature in view, with its player and the game's test of a press.
//   go      The God view teleports: the head goes over the next land point
//           of the test key seek_go ("x,z;x,z"). The hand does not move.
//   picture The Hand's point goes to a place of the picture: the next four
//           numbers of the test key seek_picture ("lx,ly,rx,ry;...") are the
//           pixel of one thing in the picture of the left eye and in the
//           picture of the right eye. The place is where the two eyes see
//           it (near_math::PicturePoint), from the cameras of the eye passes
//           and nothing that the mod knows about an object.
//   scrolls The log gets each scroll that the game drew in this frame. The
//           Hand's point goes to the middle of the ball of the one nearest
//           to the eye, also when a press does not tap it.
//   gosite, gototem  The God view teleports: the head goes 60 cm of the room
//           before the Worship Site, or before the totem, of the first town
//           of the player (village.cpp). The hand does not move.
//   icon    The Hand's point goes to the next miracle icon of that Worship
//           Site (the test key village_test_icon gives one seed type).
//   totem   The Hand's point goes to the middle of the pole of that totem.
//   scaffold  The Hand's point goes to the next scaffold of the test key
//           village_test_scaffolds that the Hand does not hold.
//   gostore, store  The God view teleports to 60 cm of the room before the
//           Village Store of that town, and the Hand's point goes 3 units
//           above the point of the store.
//   pile    The Hand's point goes to the nearest bulk source (a pile of the
//           store) within 2 m of the room of the Hand.
//   villager  The Hand's point goes to the nearest villager that the Hand
//           can take and that the game can give a job, within 2 m of the room
//           of the Hand.
//   goshop, shop  As gostore and store, for the Workshop of that town.
//   treebase  The Hand's point goes 2 units above the base of the nearest
//           tree, within 2 m of the room of the Hand.
//   up, down  The Hand's point goes 4 cm of the room up or down from where it
//           is (the test key seek_step_cm gives a different step).
//   hold    The Hand's point goes to the nearest object that the Hand can
//           hold, within 2 m of the room. The test key seek_class has a
//           class for each word "hold" in its turn ("Rock,Villager").
enum {
  kPlanNone = 0, kPlanScroll, kPlanAir, kPlanCrown, kPlanPalm, kPlanCreature,
  kPlanCreaturePalm, kPlanGo, kPlanPicture, kPlanScrolls, kPlanGoSite,
  kPlanGoTotem, kPlanIcon, kPlanTotem, kPlanUp, kPlanDown, kPlanScaffold,
  kPlanGoStore, kPlanStore, kPlanPile, kPlanVillager,
  // The door of the Temple and the next leash post (temple.cpp).
  kPlanDoor, kPlanPost,
  kPlanHold,
  // The Workshop and the base of a tree (village.cpp).
  kPlanGoShop, kPlanShop, kPlanTreeBase
};
int g_plan[16];
int g_plan_count = -1;
int g_plan_used = 0;
int g_plan_go_used = 0;
int g_plan_picture_used = 0;

// The numbers of one group of a test key such as "a,b;c,d": the group at
// index, count numbers with commas between them. False with no such group.
bool PlanNumbers(const char* key, int index, int count, float* out) {
  char path[MAX_PATH], text[400] = "";
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  char* slash = length > 0 && length < MAX_PATH ? strrchr(path, '\\') : nullptr;
  if (slash == nullptr || (slash - path) + 10 >= MAX_PATH) return false;
  slash[1] = 0;
  strcat_s(path, "bwvr.ini");
  GetPrivateProfileStringA("bwvr", key, "", text, sizeof(text), path);
  // The places have ';' between them, or '|': a run script that gives its
  // keys as one list with ';' between the keys cannot give a ';' in a key.
  const char* at = text;
  for (int i = 0; i < index; ++i) {
    at = strpbrk(at, ";|");
    if (at == nullptr) return false;
    ++at;
  }
  for (int i = 0; i < count; ++i) {
    char* end = nullptr;
    out[i] = static_cast<float>(strtod(at, &end));
    if (end == at) return false;
    at = end;
    if (i + 1 < count) {
      if (*at != ',') return false;
      ++at;
    }
  }
  return true;
}

int PlanCount() {
  if (g_plan_count >= 0) return g_plan_count;
  g_plan_count = 0;
  char path[MAX_PATH], text[200] = "";
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  char* slash = length > 0 && length < MAX_PATH ? strrchr(path, '\\') : nullptr;
  if (slash == nullptr || (slash - path) + 10 >= MAX_PATH) return 0;
  slash[1] = 0;
  strcat_s(path, "bwvr.ini");
  GetPrivateProfileStringA("bwvr", "seek_plan", "", text, sizeof(text), path);
  for (const char* at = text; *at != 0 && g_plan_count < 16;) {
    const char* end = strchr(at, ',');
    const size_t n = end != nullptr ? static_cast<size_t>(end - at) : strlen(at);
    const int word = n == 6 && strncmp(at, "scroll", 6) == 0   ? kPlanScroll
                     : n == 3 && strncmp(at, "air", 3) == 0    ? kPlanAir
                     : n == 5 && strncmp(at, "crown", 5) == 0  ? kPlanCrown
                     : n == 4 && strncmp(at, "palm", 4) == 0   ? kPlanPalm
                     : n == 8 && strncmp(at, "creature", 8) == 0
                         ? kPlanCreature
                     : n == 12 && strncmp(at, "creaturepalm", 12) == 0
                         ? kPlanCreaturePalm
                     : n == 2 && strncmp(at, "go", 2) == 0 ? kPlanGo
                     : n == 7 && strncmp(at, "picture", 7) == 0
                         ? kPlanPicture
                     : n == 7 && strncmp(at, "scrolls", 7) == 0
                         ? kPlanScrolls
                     : n == 6 && strncmp(at, "gosite", 6) == 0 ? kPlanGoSite
                     : n == 7 && strncmp(at, "gototem", 7) == 0
                         ? kPlanGoTotem
                     : n == 4 && strncmp(at, "icon", 4) == 0   ? kPlanIcon
                     : n == 5 && strncmp(at, "totem", 5) == 0  ? kPlanTotem
                     : n == 8 && strncmp(at, "scaffold", 8) == 0
                         ? kPlanScaffold
                     : n == 7 && strncmp(at, "gostore", 7) == 0
                         ? kPlanGoStore
                     : n == 5 && strncmp(at, "store", 5) == 0  ? kPlanStore
                     : n == 4 && strncmp(at, "pile", 4) == 0   ? kPlanPile
                     : n == 8 && strncmp(at, "villager", 8) == 0
                         ? kPlanVillager
                     : n == 4 && strncmp(at, "door", 4) == 0   ? kPlanDoor
                     : n == 4 && strncmp(at, "post", 4) == 0   ? kPlanPost
                     : n == 8 && strncmp(at, "treebase", 8) == 0
                         ? kPlanTreeBase
                     : n == 6 && strncmp(at, "goshop", 6) == 0 ? kPlanGoShop
                     : n == 4 && strncmp(at, "shop", 4) == 0   ? kPlanShop
                     : n == 2 && strncmp(at, "up", 2) == 0     ? kPlanUp
                     : n == 4 && strncmp(at, "hold", 4) == 0   ? kPlanHold
                     : n == 4 && strncmp(at, "down", 4) == 0   ? kPlanDown
                                                                : kPlanNone;
    if (word != kPlanNone) g_plan[g_plan_count++] = word;
    if (end == nullptr) break;
    at = end + 1;
  }
  if (g_plan_count > 0) {
    ModLog("hand pick    : test key seek_plan=%s, so each word \"seek\" takes "
           "the next of %d places", text, g_plan_count);
  }
  return g_plan_count;
}

// The gap from a point to the body of a candidate, with the bodies of one
// target. Measure reads the Hand's point, so the point stands in for it.
near_math::Gap GapFrom(const float point[3], const Candidate& c, int kind) {
  float keep[3];
  for (int k = 0; k < 3; ++k) {
    keep[k] = g_target[k];
    g_target[k] = point[k];
  }
  Measured m;
  Measure(c, kind, &m);
  for (int k = 0; k < 3; ++k) g_target[k] = keep[k];
  return m.target.gap;
}

// The census of a place of the plan: the nearest objects that a press acts on,
// with the gap of the body of one target and of the upright cylinder.
void PlanCensus(const float point[3], void* status, float units) {
  int shown[4] = {-1, -1, -1, -1};
  float gaps[4] = {1e30f, 1e30f, 1e30f, 1e30f};
  for (int i = 0; i < g_candidate_count; ++i) {
    const Candidate& c = g_candidates[i];
    const int kind = PressKind(c.object, status);
    if (kind == near_math::kNone) continue;
    const float gap = GapFrom(point, c, kind).gap;
    for (int s = 0; s < 4; ++s) {
      if (!(gap < gaps[s])) continue;
      for (int t = 3; t > s; --t) {
        gaps[t] = gaps[t - 1];
        shown[t] = shown[t - 1];
      }
      gaps[s] = gap;
      shown[s] = i;
      break;
    }
  }
  for (int s = 0; s < 4; ++s) {
    if (shown[s] < 0) continue;
    const Candidate& c = g_candidates[shown[s]];
    const Body body = BodyOf(c.object);
    const near_math::Gap old =
        near_math::BodyGap(point, c.position, body.radius, body.height);
    ModLog("hand pick    : seek census: %p (%s), %s. Its body is %.1f cm of "
           "the room from the place (its upright cylinder: %.1f cm). Point "
           "%.2f %.2f %.2f, radius %.2f, height %.2f units",
           c.object, ClassName(c.object),
           KindName(PressKind(c.object, status)),
           units > 0.0f ? gaps[s] / units * 100.0f : 0.0f,
           units > 0.0f ? old.gap / units * 100.0f : 0.0f, c.position[0],
           c.position[1], c.position[2], body.radius, body.height);
  }
}

// One place of the plan. False when the candidates have no such place.
bool PlanPoint(int word, float out[3]) {
  void* status = *reinterpret_cast<void* const*>(g_face + kFaceStatus);
  const float units = RoomUnits();
  const float limit = 2.0f * units;
  // THE VILLAGE: the places are from the data of the game (village.cpp), so
  // the draw does not have to offer the object first.
  if (word == kPlanPile || word == kPlanVillager || word == kPlanTreeBase) {
    int found = -1;
    float best = limit;
    for (int i = 0; i < g_candidate_count; ++i) {
      const Candidate& c = g_candidates[i];
      if (word == kPlanTreeBase) {
        if (!IsTreeObject(c.object) || IsForest(c.object)) continue;
      } else if (word == kPlanVillager) {
        if (!Holdable(c.object, status) ||
            strcmp(ClassName(c.object), "Villager") != 0 ||
            !VillageCanHaveJob(c.object)) {
          continue;
        }
      } else if (!TakeValid(c.object, status)) {
        continue;
      }
      const float d = Distance(c.position, g_target);
      if (d < best) {
        best = d;
        found = i;
      }
    }
    if (found < 0) {
      ModLog("hand pick    : seek plan: %s. No such object within 2 m of the "
             "room of the Hand (%d candidates), frame %llu",
             word == kPlanVillager    ? "villager"
             : word == kPlanTreeBase ? "treebase"
                                     : "pile",
             g_candidate_count, g_frame);
      return false;
    }
    const Candidate& c = g_candidates[found];
    out[0] = c.position[0];
    out[1] = c.position[1] + (word == kPlanTreeBase ? 2.0f : 0.5f);
    out[2] = c.position[2];
    ModLog("hand pick    : seek plan: %s. The Hand's point goes to the %s "
           "%p at %.2f %.2f %.2f, %.1f cm of the room from where it is, "
           "frame %llu",
           word == kPlanVillager    ? "villager"
           : word == kPlanTreeBase ? "treebase"
                                   : "pile",
           ClassName(c.object), c.object, out[0], out[1],
           out[2], units > 0.0f ? best / units * 100.0f : 0.0f, g_frame);
    return true;
  }
  if (word == kPlanGoSite || word == kPlanGoTotem || word == kPlanGoStore ||
      word == kPlanGoShop) {
    float to[3] = {0.0f, 0.0f, 0.0f};
    const bool have = VillageTestPlace(
        word == kPlanGoSite    ? kVillageSite
        : word == kPlanGoStore ? kVillageStore
        : word == kPlanGoShop  ? kVillageWorkshop
                               : kVillageCentre, to);
    // The head goes 60 cm of the room behind the place, on the line from the
    // camera to the Hand's point, so the place is in the view and in reach.
    float back[2] = {g_target[0] - Camera()[0], g_target[2] - Camera()[2]};
    const float flat = sqrtf(back[0] * back[0] + back[1] * back[1]);
    if (flat > 0.01f) {
      to[0] -= back[0] / flat * 0.6f * units;
      to[2] -= back[1] / flat * 0.6f * units;
    }
    const float target[3] = {to[0], 0.0f, to[2]};
    const bool went = have && TabletopGodTeleport(target);
    ModLog("hand pick    : seek plan: %s. %s %.1f %.1f, frame %llu",
           word == kPlanGoSite    ? "gosite"
           : word == kPlanGoStore ? "gostore"
           : word == kPlanGoShop  ? "goshop"
                                  : "gototem",
           !have ? "The player has no such place, so no teleport to"
           : went ? "The God view teleports: the head goes over the land at"
                  : "The God view does not own the view, so no teleport to",
           to[0], to[2], g_frame);
    return false;
  }
  if (word == kPlanIcon || word == kPlanTotem || word == kPlanScaffold ||
      word == kPlanStore || word == kPlanShop) {
    const char* name = word == kPlanIcon       ? "icon"
                       : word == kPlanTotem    ? "totem"
                       : word == kPlanStore    ? "store"
                       : word == kPlanShop     ? "shop"
                                               : "scaffold";
    const bool have = VillageTestPlace(word == kPlanIcon    ? kVillageIcon
                                       : word == kPlanTotem ? kVillageTotem
                                       : word == kPlanStore ? kVillageStore
                                       : word == kPlanShop  ? kVillageWorkshop
                                                            : kVillageScaffold,
                                       out);
    if (have && (word == kPlanStore || word == kPlanShop)) out[1] += 3.0f;
    if (!have) {
      ModLog("hand pick    : seek plan: %s. The first town of the player has "
             "no such thing, frame %llu", name, g_frame);
      return false;
    }
    ModLog("hand pick    : seek plan: %s. The Hand's point goes to %.2f %.2f "
           "%.2f, %.1f cm of the room from where it is, frame %llu",
           name, out[0], out[1], out[2],
           units > 0.0f ? Distance(out, g_target) / units * 100.0f : 0.0f,
           g_frame);
    PlanCensus(out, status, units);
    return true;
  }
  if (word == kPlanDoor || word == kPlanPost) {
    const char* name = word == kPlanDoor ? "door" : "post";
    if (!TempleTestPlace(word == kPlanDoor ? 0 : 1, out)) {
      ModLog("hand pick    : seek plan: %s. The draw of this frame has no "
             "such thing of the Temple of the player, frame %llu", name,
             g_frame);
      return false;
    }
    ModLog("hand pick    : seek plan: %s. The Hand's point goes to %.2f %.2f "
           "%.2f, %.1f cm of the room from where it is, frame %llu",
           name, out[0], out[1], out[2],
           units > 0.0f ? Distance(out, g_target) / units * 100.0f : 0.0f,
           g_frame);
    PlanCensus(out, status, units);
    return true;
  }
  if (word == kPlanUp || word == kPlanDown) {
    static const int step_cm = TestKey("seek_step_cm");
    const float step = (step_cm > 0 ? step_cm : 4) * 0.01f * units;
    for (int k = 0; k < 3; ++k) out[k] = g_target[k];
    out[1] += word == kPlanUp ? step : -step;
    ModLog("hand pick    : seek plan: %s. The Hand's point goes %.1f cm of "
           "the room %s, to %.2f %.2f %.2f, frame %llu",
           word == kPlanUp ? "up" : "down",
           units > 0.0f ? step / units * 100.0f : 0.0f,
           word == kPlanUp ? "up" : "down", out[0], out[1], out[2], g_frame);
    return true;
  }
  if (word == kPlanGo) {
    float to[2];
    if (!PlanNumbers("seek_go", g_plan_go_used++, 2, to)) {
      ModLog("hand pick    : seek plan: go. The test key seek_go has no "
             "place for this word, frame %llu", g_frame);
      return false;
    }
    const float target[3] = {to[0], 0.0f, to[1]};
    const bool went = TabletopGodTeleport(target);
    ModLog("hand pick    : seek plan: go. %s %.1f %.1f, frame %llu",
           went ? "The God view teleports: the head goes over the land at"
                : "The God view does not own the view, so no teleport to",
           to[0], to[1], g_frame);
    // The hand stays where it is.
    return false;
  }
  if (word == kPlanScrolls) {
    const float* camera = Camera();
    int found = -1;
    float best = 1e30f;
    for (int i = 0; i < g_scroll_count; ++i) {
      const Scroll& s = g_scrolls[i];
      const float d = Distance(camera, s.center);
      ModLog("hand pick    : seek plan: scrolls: %p (%s). Its ball: middle "
             "%.2f %.2f %.2f, radius %.2f units (%.1f cm of the room). The "
             "draw put its mesh at %.2f %.2f %.2f. The game's camera is %.1f "
             "units from it, and the camera of the eye %.1f. Active %d, "
             "script %d: the game says that a press %s. The pick %s",
             s.object, ScrollTypeName(s.type), s.center[0], s.center[1],
             s.center[2], s.radius,
             units > 0.0f ? s.radius / units * 100.0f : 0.0f, s.point[0],
             s.point[1], s.point[2], s.camera, d, s.active ? 1 : 0, s.script,
             TapWords(s.object, status),
             s.list == kScrollFromDraw    ? "did not offer it, and the mod's "
                                            "list has it from its draw"
             : s.list == kScrollNotListed ? "did not offer it, so it is not "
                                            "in the mod's list"
             : s.list == kScrollLocked    ? "is locked"
                                          : "offered it");
      if (d < best) {
        best = d;
        found = i;
      }
    }
    if (found < 0) {
      ModLog("hand pick    : seek plan: scrolls. The game drew no scroll in "
             "this frame (%d candidates), frame %llu", g_candidate_count,
             g_frame);
      return false;
    }
    for (int k = 0; k < 3; ++k) out[k] = g_scrolls[found].center[k];
    ModLog("hand pick    : seek plan: scrolls. %d scrolls in this frame. The "
           "Hand's point goes to the middle of the ball of %p, the nearest "
           "one to the eye, frame %llu", g_scroll_count,
           g_scrolls[found].object, g_frame);
    return true;
  }
  if (word == kPlanPicture) {
    float px[4], half_x = 0.0f, half_y = 0.0f;
    if (!PlanNumbers("seek_picture", g_plan_picture_used++, 4, px)) {
      ModLog("hand pick    : seek plan: picture. The test key seek_picture "
             "has no pixels for this word, frame %llu", g_frame);
      return false;
    }
    if (!HandGlowPicturePoint(px[0], px[1], px[2], px[3], out, &half_x,
                              &half_y)) {
      ModLog("hand pick    : seek plan: picture. The pixel %.1f %.1f of the "
             "left eye and %.1f %.1f of the right eye give no place (a render "
             "of %.0f by %.0f), frame %llu", px[0], px[1], px[2], px[3],
             2.0f * half_x, 2.0f * half_y, g_frame);
      return false;
    }
    // What the mod has there, for the log only: the nearest scroll of the
    // frame and its ball. The place itself is from the pixels.
    char near_text[200] = "No scroll is in the mod's list of this frame";
    float best = 1e30f;
    for (int i = 0; i < g_scroll_count; ++i) {
      const Scroll& s = g_scrolls[i];
      const float d = Distance(out, s.center);
      if (!(d < best)) continue;
      best = d;
      sprintf_s(near_text, "The ball of the scroll %p in the mod's list has "
                           "its middle %.2f units (%.1f cm of the room) from "
                           "that place, and a radius of %.2f units",
                s.object, d, units > 0.0f ? d / units * 100.0f : 0.0f,
                s.radius);
    }
    ModLog("hand pick    : seek plan: picture. The pixel %.1f %.1f of the "
           "left eye and %.1f %.1f of the right eye (a render of %.0f by "
           "%.0f) are the place %.2f %.2f %.2f, %.1f cm of the room from the "
           "Hand's point. %s. The Hand's point goes to that place, frame %llu",
           px[0], px[1], px[2], px[3], 2.0f * half_x, 2.0f * half_y, out[0],
           out[1], out[2],
           units > 0.0f ? Distance(out, g_target) / units * 100.0f : 0.0f,
           near_text, g_frame);
    return true;
  }
  if (word == kPlanScroll) {
    int found = -1;
    float found_tree = 1e30f;
    float found_ball[3] = {0.0f, 0.0f, 0.0f}, found_radius = 0.0f;
    int scrolls = 0, taps = 0;
    for (int i = 0; i < g_candidate_count; ++i) {
      const Candidate& c = g_candidates[i];
      if (!IsScrollObject(c.object)) continue;
      ++scrolls;
      if (!Tappable(c.object, status)) continue;
      ++taps;
      float ball[3], radius = 0.0f;
      if (!BoxBall(c, ball, &radius)) continue;
      if (!(Distance(ball, g_target) < limit)) continue;
      // The tree nearest to the ball.
      float tree = 1e29f;
      for (int j = 0; j < g_candidate_count; ++j) {
        const Candidate& t = g_candidates[j];
        if (!IsTreeObject(t.object) || IsForest(t.object)) continue;
        const float gap = GapFrom(ball, t, near_math::kHold).gap;
        if (gap < tree) tree = gap;
      }
      if (tree < found_tree) {
        found_tree = tree;
        found = i;
        found_radius = radius;
        for (int k = 0; k < 3; ++k) found_ball[k] = ball[k];
      }
    }
    if (found < 0) {
      ModLog("hand pick    : seek plan: no scroll that a press taps within 2 "
             "m of the room of the Hand (%d scrolls among %d candidates, %d "
             "that a press taps)", scrolls, g_candidate_count, taps);
      return false;
    }
    const Candidate& c = g_candidates[found];
    const Body body = BodyOf(c.object);
    ModLog("hand pick    : seek plan: the scroll %p. Its ball: middle %.2f "
           "%.2f %.2f, radius %.2f units (%.1f cm of the room). Its point is "
           "%.2f %.2f %.2f, its 2D radius %.2f and its height %.2f. The "
           "nearest tree is %.1f cm of the room from the middle of the ball. "
           "%d scrolls, %d that a press taps, frame %llu", c.object,
           found_ball[0], found_ball[1], found_ball[2], found_radius,
           units > 0.0f ? found_radius / units * 100.0f : 0.0f, c.position[0],
           c.position[1], c.position[2], body.radius, body.height,
           units > 0.0f ? found_tree / units * 100.0f : 0.0f, scrolls, taps,
           g_frame);
    for (int k = 0; k < 3; ++k) out[k] = found_ball[k];
    PlanCensus(out, status, units);
    return true;
  }
  if (word == kPlanCreature || word == kPlanCreaturePalm) {
    int found = -1, seen = 0;
    float best = limit;
    float found_bone[3] = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < g_candidate_count; ++i) {
      const Candidate& c = g_candidates[i];
      if (!IsCreatureObject(c.object)) continue;
      float bone[3];
      const int source = BodyPlace(c.object, bone);
      const bool have_bone = source != 0;
      const void* player = reinterpret_cast<GetPlayerFn>(
          VTable(c.object)[kCreatureGetPlayer / 4])(c.object, nullptr);
      const float d = Distance(have_bone ? bone : c.position, g_target);
      if (seen++ < 6) {
        ModLog("hand pick    : seek plan: the Creature %p at %.1f %.1f %.1f, "
               "%.0f cm of the room from the Hand's point. It has %s, the "
               "game's test of a press says %s, and its body bone is %s",
               c.object, c.position[0], c.position[1], c.position[2],
               units > 0.0f ? d / units * 100.0f : 0.0f,
               player == nullptr ? "no player"
               : player == LocalPlayer() ? "the local player"
                                         : "another player",
               TakeValid(c.object, status) ? "yes" : "no",
               source == 1   ? "known"
               : source == 2 ? "not sane, so its place on the map counts"
                             : "not known");
      }
      if (!have_bone || !(d < best)) continue;
      best = d;
      found = i;
      for (int k = 0; k < 3; ++k) found_bone[k] = bone[k];
    }
    if (found < 0) {
      // The draw offered no Creature near the Hand. The Creature of the local
      // player is known from the player (GPlayer+0xA4C), at each distance.
      // The Hand goes to its body bone, and the log says where it is from
      // the camera, so a run can turn the view to it.
      const uint8_t* player = static_cast<const uint8_t*>(LocalPlayer());
      void* mine = player != nullptr
                       ? *reinterpret_cast<void* const*>(player +
                                                         kPlayerCreature)
                       : nullptr;
      float bone[3];
      if (mine == nullptr || IsBadReadPtr(mine, 0x170)) {
        ModLog("hand pick    : seek plan: no Creature with a body bone in "
               "view within 2 m of the room of the Hand (%d Creatures among "
               "%d candidates), and the player has no Creature, frame %llu",
               seen, g_candidate_count, g_frame);
        return false;
      }
      if (BodyPlace(mine, bone) == 0) {
        // With creature_bone=0 the mod knows no body before a press. The
        // place of the Creature on the map is in the object (Object+0x14, x
        // and z times 6553.6).
        const int32_t* map = reinterpret_cast<const int32_t*>(
            static_cast<const uint8_t*>(mine) + 0x14);
        const int32_t coords[3] = {map[0], map[1], 0};
        bone[0] = map[0] / kMapScale;
        bone[2] = map[1] / kMapScale;
        bone[1] = reinterpret_cast<GetAltitudeFn>(kGetAltitude)(coords) + 1.0f;
        ModLog("hand pick    : seek plan: the mod does not know the body of "
               "the player's Creature %p now, so its place is its place on "
               "the map", mine);
      }
      const float* camera = Camera();
      const float dx = bone[0] - camera[0], dz = bone[2] - camera[2];
      ModLog("hand pick    : seek plan: the draw offered no Creature near the "
             "Hand (%d candidates). The player's Creature %p has its body "
             "at %.1f %.1f %.1f: %.0f units from the camera at %.1f %.1f "
             "%.1f, at a bearing of %.0f degrees (atan2 of x and z), and "
             "%.0f cm of the room from the Hand's point. Its test of a press "
             "says %s, and the leash click would be kind %d. The Hand goes "
             "to it, frame %llu", g_candidate_count, mine, bone[0], bone[1],
             bone[2], sqrtf(dx * dx + dz * dz), camera[0], camera[1],
             camera[2], atan2f(dx, dz) * 57.29578f,
             units > 0.0f ? Distance(bone, g_target) / units * 100.0f : 0.0f,
             TakeValid(mine, status) ? "yes" : "no", LeashKind(mine), g_frame);
      for (int k = 0; k < 3; ++k) out[k] = bone[k];
      if (word == kPlanCreaturePalm) {
        float palm[3];
        uint64_t palm_frame = 0;
        int32_t palm_state = -1;
        if (HandExactPalm(palm, &palm_frame, &palm_state) &&
            palm_frame + 1 >= g_frame) {
          for (int k = 0; k < 3; ++k) out[k] += g_target[k] - palm[k];
        }
      }
      return true;
    }
    for (int k = 0; k < 3; ++k) out[k] = found_bone[k];
    // creaturepalm: the palm of the mod's hand goes to the body bone, for the
    // flat hand, where the palm is the contact point.
    if (word == kPlanCreaturePalm) {
      float palm[3];
      uint64_t palm_frame = 0;
      int32_t palm_state = -1;
      if (HandExactPalm(palm, &palm_frame, &palm_state) &&
          palm_frame + 1 >= g_frame) {
        for (int k = 0; k < 3; ++k) out[k] += g_target[k] - palm[k];
      }
    }
    return true;
  }
  if (word == kPlanPalm) {
    // The land behind the palm, as the game has it (kPalmLandX), goes onto
    // the base of the tree nearest to it. A move of the hand across the view
    // moves that land point by the ratio of the two distances from the
    // camera, so some seeks one after the other bring it there.
    if (*reinterpret_cast<const int32_t*>(kPalmLandFlag) == 0) {
      ModLog("hand pick    : seek plan: palm. The game has no land point "
             "behind the palm now, frame %llu", g_frame);
      return false;
    }
    const float lx = *reinterpret_cast<const float*>(kPalmLandX);
    const float lz = *reinterpret_cast<const float*>(kPalmLandZ);
    int near_tree = -1;
    float near_d = 1e30f;
    for (int i = 0; i < g_candidate_count; ++i) {
      const Candidate& c = g_candidates[i];
      if (!IsTreeObject(c.object) || IsForest(c.object) ||
          !Holdable(c.object, status)) {
        continue;
      }
      const float dx = c.position[0] - lx, dz = c.position[2] - lz;
      const float d = sqrtf(dx * dx + dz * dz);
      if (d < near_d) {
        near_d = d;
        near_tree = i;
      }
    }
    if (near_tree < 0) {
      ModLog("hand pick    : seek plan: palm. No tree that the Hand can hold "
             "among %d candidates", g_candidate_count);
      return false;
    }
    const Candidate& c = g_candidates[near_tree];
    const int32_t coords[3] = {
        static_cast<int32_t>(ClampToMap(lx) * kMapScale),
        static_cast<int32_t>(ClampToMap(lz) * kMapScale), 0};
    const float land[3] = {
        lx, reinterpret_cast<GetAltitudeFn>(kGetAltitude)(coords), lz};
    const float* camera = Camera();
    const float far_d = Distance(camera, land);
    const float share = far_d > 1e-3f ? Distance(camera, g_target) / far_d
                                      : 0.0f;
    out[0] = g_target[0] + (c.position[0] - lx) * share;
    out[1] = g_target[1];
    out[2] = g_target[2] + (c.position[2] - lz) * share;
    ModLog("hand pick    : seek plan: palm. The land behind the palm is %.1f "
           "%.1f. The tree %p at %.2f %.2f %.2f is %.1f units from it (%.1f "
           "cm of the room), and %.1f cm of the room from the Hand's point. "
           "The Hand's point is %.1f cm of the room over the land. The hand "
           "moves by %.2f of the way, frame %llu", lx, lz, c.object,
           c.position[0], c.position[1], c.position[2], near_d,
           units > 0.0f ? near_d / units * 100.0f : 0.0f,
           units > 0.0f ? Distance(c.position, g_target) / units * 100.0f
                        : 0.0f,
           units > 0.0f ? (g_target[1] - g_land_y) / units * 100.0f : 0.0f,
           share, g_frame);
    return true;
  }
  // The nearest tree to the Hand that the Hand can hold.
  int tree = -1;
  float best = limit;
  for (int i = 0; i < g_candidate_count; ++i) {
    const Candidate& c = g_candidates[i];
    if (!IsTreeObject(c.object) || IsForest(c.object) ||
        !Holdable(c.object, status)) {
      continue;
    }
    const float d = Distance(c.position, g_target);
    if (d < best) {
      best = d;
      tree = i;
    }
  }
  if (tree < 0) {
    ModLog("hand pick    : seek plan: no tree that the Hand can hold within 2 "
           "m of the room of the Hand, of %d candidates", g_candidate_count);
    return false;
  }
  const Candidate& c = g_candidates[tree];
  const Body body = BodyOf(c.object);
  if (word == kPlanCrown) {
    out[0] = c.position[0];
    out[1] = c.position[1] + 0.8f * body.height;
    out[2] = c.position[2];
    ModLog("hand pick    : seek plan: the crown of the tree %p at %.2f %.2f "
           "%.2f, radius %.2f, height %.2f units, frame %llu", c.object,
           c.position[0], c.position[1], c.position[2], body.radius,
           body.height, g_frame);
    PlanCensus(out, status, units);
    return true;
  }
  // air: the palm on the line from the camera to the base of the tree.
  const float* camera = Camera();
  float line[3];
  for (int k = 0; k < 3; ++k) line[k] = camera[k] - c.position[k];
  const float span = sqrtf(line[0] * line[0] + line[1] * line[1] +
                           line[2] * line[2]);
  if (!(span > 1e-3f)) return false;
  float palm[3];
  uint64_t palm_frame = 0;
  int32_t palm_state = -1;
  const bool have_palm = HandExactPalm(palm, &palm_frame, &palm_state) &&
                         palm_frame + 1 >= g_frame;
  for (int k = 0; k < 3; ++k) {
    out[k] = c.position[k] + line[k] / span * 0.25f * units +
             (have_palm ? g_target[k] - palm[k] : 0.0f);
  }
  ModLog("hand pick    : seek plan: air. The tree %p at %.2f %.2f %.2f, "
         "radius %.2f, height %.2f units. The %s goes on the line from the "
         "camera to its base, 25 cm of the room from the base. The Hand's "
         "point goes to %.2f %.2f %.2f, frame %llu", c.object, c.position[0],
         c.position[1], c.position[2], body.radius, body.height,
         have_palm ? "palm" : "Hand's point (the palm is not known)", out[0],
         out[1], out[2], g_frame);
  PlanCensus(out, status, units);
  return true;
}

}  // namespace

// For the fake controller's word "seek": a point just above the nearest
// holdable object of the last first eye pass, within 150 units of the eye.
// Only while the controller places the Hand.
bool HandDirectSeekPoint(float out[3]) {
  if (g_face == nullptr || g_hand == nullptr) return false;
  // The plan runs with no candidate too: the words "go" and "picture" need
  // none.
  // The plan word "hold" is the search below: the nearest object that the
  // Hand can hold, within 2 m of the room of the Hand. The test key
  // seek_class has a class for each word "hold" in its turn, with commas
  // between ("Rock,Villager"). A word with no class takes each class.
  bool plan_hold = false;
  char want_class[32] = "";
  if (PlanCount() > 0) {
    const int word = g_plan[g_plan_used++ % g_plan_count];
    if (word != kPlanHold) return PlanPoint(word, out);
    plan_hold = true;
    static int holds_used = 0;
    char path[MAX_PATH], text[200] = "";
    const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash =
        length > 0 && length < MAX_PATH ? strrchr(path, '\\') : nullptr;
    if (slash != nullptr && (slash - path) + 10 < MAX_PATH) {
      slash[1] = 0;
      strcat_s(path, "bwvr.ini");
      GetPrivateProfileStringA("bwvr", "seek_class", "", text, sizeof(text),
                               path);
    }
    const char* at = text;
    for (int i = 0; i < holds_used && at != nullptr; ++i) {
      at = strchr(at, ',');
      if (at != nullptr) ++at;
    }
    ++holds_used;
    if (at != nullptr) {
      size_t n = strcspn(at, ",");
      if (n >= sizeof(want_class)) n = sizeof(want_class) - 1;
      memcpy(want_class, at, n);
      want_class[n] = 0;
    }
  }
  if (g_candidate_count == 0) return false;
  // The test key seek_thrown=1: the word steps the hand onto the object that
  // the Hand threw last, where the draw of the last frame saw it. So a test
  // can close the grip on an object that flies or rolls.
  static const bool seek_thrown = TestKey("seek_thrown") != 0;
  if (seek_thrown && g_last_thrown != nullptr && !plan_hold) {
    bool drawn = false;
    if (!ObjectLives(g_last_thrown, g_last_thrown_vtable) ||
        !ThrownPosition(out, &drawn)) {
      ModLog("hand pick    : seek: the thrown object %p is gone, frame %llu",
             g_last_thrown, g_frame);
      return false;
    }
    const float units = RoomUnits();
    const float cm =
        units > 0.0f ? Distance(out, g_target) / units * 100.0f : 0.0f;
    out[1] += 0.5f;
    ModLog("hand pick    : seek goes to the thrown object %p (%s) at %.1f %.1f "
           "%.1f, %.0f cm of the room from the Hand's point. %s, frame %llu",
           g_last_thrown, ClassName(g_last_thrown), out[0], out[1], out[2], cm,
           drawn ? "The draw of the last frame saw it there"
                 : "The draw did not offer it, so the place is from its mesh",
           g_frame);
    return true;
  }
  void* status = *reinterpret_cast<void* const*>(g_face + kFaceStatus);
  // On the table the eye is far from the land, and the fake hand has no
  // reach curve. So the word seeks the nearest holdable object to the Hand,
  // within 50 cm of the room.
  const float table = TabletopViewScale();
  const float* from = table > 0.0f ? g_target : Camera();
  // With seek_bulk=1 the word goes to the nearest bulk source instead, within
  // 2 m of the room on the table (the wide pick of HandDirectFrame). The log
  // names the nearest one.
  const bool bulk = SeekBulk();
  // The test keys seek_forest=1 and seek_tap=1: the word goes to the nearest
  // forest, or to the nearest object that a press taps and the Hand cannot
  // hold (a house, a scroll), within 2 m of the room on the table.
  const int other = SeekOther();
  const float limit =
      table > 0.0f ? (bulk || other != 0 || plan_hold ? 2.0f : 0.5f) * table
                   : 150.0f;
  float best = limit;
  bool found = false;
  void* found_object = nullptr;
  int found_index = -1;
  // The game takes only inside the player's influence, so a bulk seek goes
  // to the nearest source inside it, if there is one.
  bool any_inside = false;
  if (bulk) {
    int listed = 0;
    for (int i = 0; i < g_candidate_count; ++i) {
      const Candidate& c = g_candidates[i];
      if (!TakeValid(c.object, status)) continue;
      const bool inside = InsideInfluence(g_face, c.position[0], c.position[2]);
      any_inside |= inside;
      if (listed++ < 8) {
        ModLog("hand bulk    : a %s at %.1f %.1f %.1f, %.0f units from the "
               "Hand, %s your influence", ClassName(c.object), c.position[0],
               c.position[1], c.position[2], Distance(c.position, from),
               inside ? "inside" : "outside");
      }
    }
  }
  float nearest = 1e30f;
  void* nearest_object = nullptr;
  int sources = 0;
  for (int i = 0; i < g_candidate_count; ++i) {
    const Candidate& c = g_candidates[i];
    const float d = Distance(c.position, from);
    if (bulk) {
      if (!TakeValid(c.object, status)) continue;
      if (any_inside &&
          !InsideInfluence(g_face, c.position[0], c.position[2])) {
        continue;
      }
      ++sources;
      if (d < nearest) {
        nearest = d;
        nearest_object = c.object;
      }
    } else if (other == 1) {
      if (!IsForest(c.object)) continue;
    } else if (other == 2) {
      if (Holdable(c.object, status) || !Tappable(c.object, status)) continue;
    } else if (other == 3) {
      if (!IsScroll(c.object) ||
          (!Tappable(c.object, status) && !ClickScroll(c.object))) {
        continue;
      }
    } else if (!Holdable(c.object, status)) {
      continue;
    }
    if (want_class[0] != 0 && strcmp(ClassName(c.object), want_class) != 0) {
      continue;
    }
    if (d >= best) continue;
    best = d;
    found = true;
    found_object = c.object;
    found_index = i;
    out[0] = c.position[0];
    out[1] = c.position[1] + 0.5f;
    out[2] = c.position[2];
  }
  if (bulk || other != 0 || plan_hold) {
    // A census of the candidates by class, for a test that looks for a store,
    // a field, a forest or a house: the count and the nearest of each class,
    // and what a press of the grip does to it.
    struct Row {
      char name[32];
      int count;
      float nearest;
      bool take;
      bool tap;
    };
    Row rows[24];
    int row_count = 0;
    for (int i = 0; i < g_candidate_count; ++i) {
      const Candidate& c = g_candidates[i];
      const char* name = ClassName(c.object);
      int r = 0;
      while (r < row_count && strcmp(rows[r].name, name) != 0) ++r;
      if (r == row_count) {
        if (row_count == 24) continue;
        strncpy_s(rows[r].name, name, _TRUNCATE);
        rows[r].count = 0;
        rows[r].nearest = 1e30f;
        rows[r].take = false;
        rows[r].tap = false;
        ++row_count;
      }
      ++rows[r].count;
      const float d = Distance(c.position, from);
      if (d < rows[r].nearest) rows[r].nearest = d;
      rows[r].take |= TakeValid(c.object, status);
      rows[r].tap |= !Holdable(c.object, status) && Tappable(c.object, status);
    }
    for (int r = 0; r < row_count; ++r) {
      ModLog("hand %s    : candidates: %d %s, the nearest %.0f units%s%s",
             bulk ? "bulk" : "pick", rows[r].count, rows[r].name,
             rows[r].nearest, rows[r].take ? ", a bulk source" : "",
             rows[r].tap ? ", a press taps it" : "");
    }
  }
  if (bulk) {
    ModLog("hand bulk    : %d candidates, %d bulk sources. The nearest is %s "
           "%.0f units (%.2f m of the room) from the %s, at %.1f %.1f %.1f, "
           "so the hand %s", g_candidate_count, sources,
           nearest_object != nullptr ? ClassName(nearest_object) : "none",
           nearest_object != nullptr ? nearest : 0.0f,
           nearest_object != nullptr && table > 0.0f ? nearest / table : 0.0f,
           table > 0.0f ? "Hand" : "camera", found ? out[0] : 0.0f,
           found ? out[1] : 0.0f, found ? out[2] : 0.0f,
           found ? "steps to it" : "stays");
    return found;
  }
  if (other != 0) {
    const Body body = found ? BodyOf(found_object) : Body();
    float middle[3] = {0.0f, 0.0f, 0.0f};
    const bool boxed =
        found && BoxMiddle(g_candidates[found_index], body, middle);
    ModLog("hand pick    : %d candidates, the nearest %s to the Hand is %s "
           "(%s), %.0f units away. Its body: radius %.2f, height %.2f units. "
           "Its point is %.2f %.2f %.2f, and the middle of its box is %s%.2f "
           "%.2f %.2f", g_candidate_count,
           other == 1 ? "forest"
           : other == 2 ? "object that a press taps" : "scroll",
           found ? "within 2 m of the room" : "not within 2 m of the room",
           found ? ClassName(found_object) : "none", found ? best : 0.0f,
           found ? body.radius : 0.0f, found ? body.height : 0.0f,
           found ? out[0] : 0.0f, found ? out[1] - 0.5f : 0.0f,
           found ? out[2] : 0.0f, boxed ? "" : "not known: ", middle[0],
           middle[1], middle[2]);
  } else if (plan_hold) {
    ModLog("hand pick    : seek plan: hold. %d candidates, the nearest object "
           "of the class %s that the Hand can hold is %s (%s), %.0f units "
           "away, at %.2f %.2f %.2f, frame %llu", g_candidate_count,
           want_class[0] != 0 ? want_class : "(each class)",
           found ? "within 2 m of the room" : "not within 2 m of the room",
           found ? ClassName(found_object) : "none", found ? best : 0.0f,
           found ? out[0] : 0.0f, found ? out[1] - 0.5f : 0.0f,
           found ? out[2] : 0.0f, g_frame);
  } else if (table > 0.0f) {
    ModLog("hand pick    : %d candidates, the nearest holdable one to the Hand "
           "is %s, %.0f units", g_candidate_count,
           found ? "within 50 cm of the room" : "not within 50 cm of the room",
           found ? best : 0.0f);
  } else {
    ModLog("hand pick    : %d candidates, the nearest holdable one is %s",
           g_candidate_count,
           found ? "within 150 units" : "not within 150 units");
  }
  return found;
}

// For the fake controller's word "seekc": a point on the Creature, for the
// stroke and slap tests. The game tells a Creature by the type field at +0x10
// of the attribute block at Object+0x28 (1 for the Creature, the test of
// fn_005D80C0 that gives hand mode 0x13). The type is 1 for the
// Creature, 6 for villagers and 28 for a building. Bit 0x10 of the byte at
// Object+0x24, which the press action at 0x005D4960 tests, was not set on it.
// The point is the object's origin, raised by half its 2D radius.
// For the fake controller's word "aimfar": the nearest far target of the last
// first eye pass, on the table or in the God view. It is more than 0.5 m of
// the room from the Hand's point and in the range of far_grab_max_m from the
// head (6 m from the Hand with no range). It is a holdable object that is not
// a tree, or with seek_bulk=1 a bulk source. Only while the controller places
// the Hand. The test aims the fake hand at it, from where the hand is.
bool HandDirectSeekFarPoint(float out[3]) {
  if (g_face == nullptr || g_hand == nullptr || g_candidate_count == 0) {
    return false;
  }
  const float units = TabletopViewScale();
  if (!(units > 0.0f)) return false;
  void* status = *reinterpret_cast<void* const*>(g_face + kFaceStatus);
  const bool bulk = SeekBulk();
  const int only = FarTestOnly();
  // The tests of a forest and of a scroll run with the view zoomed out, where
  // each object is near. So their least distance is the least distance of a
  // far target, and not 0.5 m.
  // The test key far_test_least_cm: the least distance from the Hand in cm of
  // the room, so a test can aim at an object that is near the Hand.
  static const int least_cm = TestKey("far_test_least_cm");
  const float least = least_cm > 0 ? least_cm * 0.01f
                      : only != 0  ? FarMinMeters() + 0.02f
                                   : 0.5f;
  const float range = FarGrabMaxMeters();
  float best = 1e30f;
  void* best_object = nullptr;
  int found = 0;
  // The test key far_test_thrown=1: the word aims at the object that the
  // Hand threw last, where the draw of the last frame saw it. So a test can
  // fetch an object that flies, rolls or lies in the sea.
  static const bool thrown = TestKey("far_test_thrown") != 0;
  if (thrown && g_last_thrown != nullptr) {
    if (!ObjectLives(g_last_thrown, g_last_thrown_vtable)) {
      ModLog("far grab    : aimfar: the thrown object %p is gone, frame %llu",
             g_last_thrown, g_frame);
      return false;
    }
    bool drawn = false;
    if (!ThrownPosition(out, &drawn)) {
      ModLog("far grab    : aimfar: the thrown object %p has no mesh, frame "
             "%llu", g_last_thrown, g_frame);
      return false;
    }
    if (!drawn) {
      ModLog("far grab    : aimfar: the draw did not offer the thrown object "
             "%p among %d candidates, so its place is from its mesh, frame "
             "%llu", g_last_thrown, g_candidate_count, g_frame);
    }
    const int32_t coords[3] = {
        static_cast<int32_t>(ClampToMap(out[0]) * kMapScale),
        static_cast<int32_t>(ClampToMap(out[2]) * kMapScale), 0};
    const float land_y =
        reinterpret_cast<GetAltitudeFn>(kGetAltitude)(coords);
    ModLog("far grab    : aimfar aims at the thrown object %p (%s), %.2f m of "
           "the room from the Hand, at %.1f %.1f %.1f. The land under it is "
           "at %.1f (0 is the sea), holdable %d, seen %d, inside %d, frame "
           "%llu", g_last_thrown, ClassName(g_last_thrown),
           Distance(out, g_target) / units, out[0], out[1], out[2], land_y,
           Holdable(g_last_thrown, status) ? 1 : 0,
           FarVisible(out, Radius2D(g_last_thrown)) ? 1 : 0,
           InsideInfluence(g_face, ClampToMap(out[0]), ClampToMap(out[2])) ? 1
                                                                          : 0,
           g_frame);
    g_far_debug_object = g_last_thrown;
    g_far_debug_frames = 6;
    return true;
  }
  // The test keys of the choice of a far target (docs/grab.md, "The cone and
  // the choice"). far_test_aim_class=1 aims at the nearest villager, 2 at the
  // nearest living thing, 3 at the nearest rock, 4 at your Creature, 5 at
  // the nearest tree, 6 at the nearest Creature that is not your Creature, 7
  // at the nearest quest character (a SpecialVillager) and 8 at the nearest
  // miracle that the Hand can take,
  // at the middle of its body. far_test_aim_up_pct gives a different height
  // on the body, in percent of its height: 100 is the top. The body of your
  // Creature is its capsule. far_test_aim_toward=1 then
  // turns the aim toward the nearest tree, 2 toward the nearest rock and 3
  // toward the nearest signpost (20
  // units or less from it), by far_test_aim_blend_pct percent of the way from
  // the body to the base of that object. So a run can point a little off a
  // living thing toward a tree, with the same aim for both choices.
  //
  // far_test_aim_pair=1 takes the object of the class that has such a tree
  // (or rock) nearest to it, 2 to 20 units away, and not the object nearest
  // to the Hand. far_test_aim_blends is a list of percents ("0,30,60"): each
  // word takes the next one in place of far_test_aim_blend_pct, the last one
  // stays, and each later word keeps the object and the tree of the first
  // word. So one run steps the aim along the line from a villager to a tree
  // and back.
  static const int aim_class = TestKey("far_test_aim_class");
  static const int aim_toward = TestKey("far_test_aim_toward");
  static const int aim_blend = TestKey("far_test_aim_blend_pct");
  static const int aim_pair = TestKey("far_test_aim_pair");
  static const int aim_up = TestKey("far_test_aim_up_pct");
  const char* toward_word = aim_toward == 3 ? "signpost" : "tree";
  const auto toward_is = [&](void* object) {
    return aim_toward == 1   ? IsTreeObject(object)
           : aim_toward == 3 ? IsScroll(object) && IsSignpost(object)
                             : strcmp(ClassName(object), "Rock") == 0;
  };
  if (g_far_blend_count < 0) {
    g_far_blend_count = 0;
    char path[MAX_PATH], text[128] = "";
    const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash =
        length > 0 && length < MAX_PATH ? strrchr(path, '\\') : nullptr;
    if (slash != nullptr && strlen(path) + 9 < MAX_PATH) {
      slash[1] = 0;
      strcat_s(path, "bwvr.ini");
      GetPrivateProfileStringA("bwvr", "far_test_aim_blends", "", text,
                               sizeof(text), path);
    }
    for (char* at = text; *at != 0 && g_far_blend_count < 24;) {
      char* end = nullptr;
      const long value = strtol(at, &end, 10);
      if (end == at) break;
      g_far_blends[g_far_blend_count++] = static_cast<int>(value);
      at = end;
      while (*at == ',' || *at == ' ') ++at;
    }
  }
  // The object and the tree of the first word of a list of blends, while the
  // draw still offers them.
  static void* kept_object = nullptr;
  static void* kept_tree = nullptr;
  if (g_far_blend_count > 0 && kept_object != nullptr) {
    bool here = false;
    for (int i = 0; i < g_candidate_count; ++i) {
      if (g_candidates[i].object == kept_object) here = true;
    }
    if (!here) kept_object = kept_tree = nullptr;
  } else {
    kept_object = kept_tree = nullptr;
  }
  // The trees (or rocks) of far_test_aim_toward, for far_test_aim_pair.
  static int toward_list[512];
  int toward_count = 0;
  const bool pair = aim_pair != 0 && aim_class != 0 && aim_toward != 0 &&
                    kept_object == nullptr;
  for (int i = 0; pair && i < g_candidate_count && toward_count < 512; ++i) {
    void* object = g_candidates[i].object;
    if (IsForest(object)) continue;
    if (toward_is(object)) toward_list[toward_count++] = i;
  }
  float best_meters = 0.0f;
  int class_counts[4] = {0, 0, 0, 0};  // villagers, living, rocks, trees
  for (int i = 0; i < g_candidate_count; ++i) {
    const Candidate& c = g_candidates[i];
    const float meters = Distance(c.position, g_target) / units;
    if (meters < least || meters > 6.0f) continue;
    if (kept_object != nullptr && c.object != kept_object) continue;
    if (!controller_math::FarInRange(
            controller_math::FarFloorMeters(g_far_eye, c.position, units),
            range, false)) {
      continue;
    }
    if (bulk) {
      if (!TakeValid(c.object, status)) continue;
    } else if (only == 1) {
      // The test of a fetch from a forest aims at the nearest forest.
      if (!IsForest(c.object)) continue;
    } else if (only == 2) {
      // The test of the far touch aims at the nearest scroll.
      if (!IsScroll(c.object) ||
          (!Tappable(c.object, status) && !ClickScroll(c.object))) {
        continue;
      }
    } else if (only == 3) {
      // The test of the leash from a distance aims at the player's Creature.
      if (!IsCreatureObject(c.object) ||
          LeashKind(c.object) == near_math::kLeashNone) {
        continue;
      }
    } else if (only == 4) {
      // The test of a take from a forest at a distance aims at the nearest
      // tree.
      if (!IsTreeObject(c.object) || IsForest(c.object) ||
          !Holdable(c.object, status)) {
        continue;
      }
    } else if (aim_class != 0) {
      if ((aim_class != 4 && aim_class != 6 &&
           !Holdable(c.object, status)) ||
          IsForest(c.object)) {
        continue;
      }
      const bool villager = strcmp(ClassName(c.object), "Villager") == 0;
      const bool quest = strcmp(ClassName(c.object), "SpecialVillager") == 0;
      const bool living = IsLivingObject(c.object);
      const bool rock = strcmp(ClassName(c.object), "Rock") == 0;
      if (villager) ++class_counts[0];
      if (living) ++class_counts[1];
      if (rock) ++class_counts[2];
      if ((aim_class == 1 && !villager) || (aim_class == 2 && !living) ||
          (aim_class == 3 && !rock) || (aim_class == 7 && !quest) ||
          (aim_class == 4 && (!IsCreatureObject(c.object) ||
                              LeashKind(c.object) == near_math::kLeashNone)) ||
          (aim_class == 5 && !IsTreeObject(c.object)) ||
          (aim_class == 8 && !IsMiracleSeed(c.object)) ||
          (aim_class == 6 && (!IsCreatureObject(c.object) ||
                              IsOwnCreature(c.object)))) {
        continue;
      }
    } else {
      if (!Holdable(c.object, status) || IsForest(c.object)) continue;
      // A tree is planted when it is let go, not thrown, so the test aims at
      // other objects.
      if (reinterpret_cast<VirtualIntFn>(
              VTable(c.object)[kIsAnyKindOfTree / 4])(c.object, nullptr) != 0) {
        continue;
      }
    }
    ++found;
    if (!InsideInfluence(g_face, ClampToMap(c.position[0]),
                         ClampToMap(c.position[2]))) {
      continue;
    }
    if (!FarVisible(c.position, Radius2D(c.object))) continue;
    // The order is the distance from the Hand. With far_test_aim_pair it is
    // the gap to the nearest tree (or rock) of far_test_aim_toward.
    float order = meters;
    if (pair) {
      order = 1e30f;
      for (int t = 0; t < toward_count; ++t) {
        const Candidate& o = g_candidates[toward_list[t]];
        if (o.object == c.object) continue;
        const float dx = o.position[0] - c.position[0];
        const float dz = o.position[2] - c.position[2];
        const float gap = sqrtf(dx * dx + dz * dz);
        if (gap >= 2.0f && gap < order) order = gap;
      }
      if (order > 20.0f) continue;
    }
    if (order < best) {
      best = order;
      best_meters = meters;
      best_object = c.object;
      for (int k = 0; k < 3; ++k) out[k] = c.position[k];
    }
  }
  if (aim_class != 0) {
    // The classes among the candidates, for the test: up to 12 names.
    char names[12][40];
    int counts[12] = {0};
    int kinds = 0;
    for (int i = 0; i < g_candidate_count; ++i) {
      const char* name = ClassName(g_candidates[i].object);
      int k = 0;
      while (k < kinds && strcmp(names[k], name) != 0) ++k;
      if (k == kinds) {
        if (kinds == 12) continue;
        strcpy_s(names[kinds], name);
        ++kinds;
      }
      ++counts[k];
    }
    char text[400];
    text[0] = 0;
    for (int k = 0; k < kinds; ++k) {
      char one[64];
      _snprintf_s(one, sizeof(one), _TRUNCATE, "%s%s %d", k ? ", " : "",
                  names[k], counts[k]);
      strcat_s(text, one);
    }
    ModLog("far grab    : test: the classes of the %d candidates: %s",
           g_candidate_count, text);
  }
  ModLog("far grab    : the word aimfar found %d %s more than %.2f m of the "
         "room from the Hand and at most %.1f m from the head, among %d "
         "candidates. %s", found, bulk ? "bulk sources" : "holdable objects",
         least, range, g_candidate_count,
         best_object != nullptr
             ? "The nearest one inside your influence"
             : "None inside your influence");
  g_far_debug_object = best_object;
  g_far_debug_frames = best_object != nullptr ? 6 : 0;
  if (best_object != nullptr) {
    ModLog("far grab    : aimfar aims at %p (%s), %.2f m of the room from the "
           "Hand, at %.1f %.1f %.1f", best_object, ClassName(best_object),
           best_meters, out[0], out[1], out[2]);
    if (aim_class != 0) {
      // The middle of the body, or the height of far_test_aim_up_pct, and
      // then the turn toward a tree. The body of your Creature is its
      // capsule.
      const float up = aim_up != 0 ? aim_up * 0.01f : 0.5f;
      controller_math::Capsule capsule;
      if (aim_class == 4 && CreatureBody(best_object, &capsule)) {
        for (int k = 0; k < 3; ++k) out[k] = capsule.base[k];
        out[1] += up * capsule.height;
      } else {
        out[1] += up * FarBodyHeight(best_object, Radius2D(best_object));
      }
      void* tree = nullptr;
      float tree_gap = 20.0f;
      for (int i = 0; aim_toward != 0 && i < g_candidate_count; ++i) {
        const Candidate& c = g_candidates[i];
        if (c.object == best_object || IsForest(c.object) ||
            (kept_tree != nullptr && c.object != kept_tree) ||
            !toward_is(c.object)) {
          continue;
        }
        const float dx = c.position[0] - out[0], dz = c.position[2] - out[2];
        const float gap = sqrtf(dx * dx + dz * dz);
        // With far_test_aim_pair the tree is 2 units or more from the body.
        if (pair && gap < 2.0f) continue;
        if (gap < tree_gap || c.object == kept_tree) {
          tree_gap = gap;
          tree = c.object;
          for (int k = 0; k < 3; ++k) g_far_toward[k] = c.position[k];
        }
      }
      // The next percent of far_test_aim_blends, and the last one stays.
      int blend = aim_blend;
      if (g_far_blend_count > 0) {
        blend = g_far_blends[g_far_blend_next < g_far_blend_count
                                 ? g_far_blend_next
                                 : g_far_blend_count - 1];
        ++g_far_blend_next;
        kept_object = best_object;
        kept_tree = tree;
      }
      if (tree != nullptr) {
        const float share = blend * 0.01f;
        for (int k = 0; k < 3; ++k) {
          out[k] += share * (g_far_toward[k] - out[k]);
        }
      }
      ModLog("far grab    : test: aimfar takes the body of the %s %p (%d "
             "villagers, %d living things and %d rocks among the candidates "
             "past the least distance). %s %s %s. The aim point is %.1f %.1f "
             "%.1f",
             ClassName(best_object), best_object, class_counts[0],
             class_counts[1], class_counts[2], tree != nullptr ? "A" : "No",
             toward_word,
             tree != nullptr ? "stands near it, and the aim turns toward it"
                             : "stands near it, or the aim does not turn",
             out[0], out[1], out[2]);
      if (tree != nullptr) {
        ModLog("far grab    : test: the %s %p is %.1f units from the body, "
               "and the aim turns %d percent of the way toward its base",
               toward_word, tree, tree_gap, blend);
      }
    }
  }
  return best_object != nullptr;
}

// For far_grab.cpp: the mesh translation that the draw of the first eye pass
// saw for an object, from this frame or the one before (the list of the draw
// is cleared at the start of each frame). False when it was not offered.
bool HandDirectCandidatePosition(void* object, float out[3]) {
  return CandidatePosition(object, out);
}

// For hand_glow.cpp: the eye pass and the frame, while the controller places
// the Hand.
bool HandDirectEyeFrame(int* eye, uint64_t* frame) {
  if (!g_active || g_hand == nullptr || g_eye < 0) return false;
  *eye = g_eye;
  *frame = g_frame;
  return true;
}

bool HandDirectSeekCreaturePoint(float out[3]) {
  if (g_face == nullptr || g_hand == nullptr || g_candidate_count == 0) {
    return false;
  }
  const float* camera = Camera();
  // On the table, as for "seek": the nearest to the Hand, within 50 cm of
  // the room, and the point on the Creature itself.
  const float table = TabletopViewScale();
  float best = table > 0.0f ? 0.5f * table : 400.0f;
  bool found = false;
  int typed = 0;
  for (int i = 0; i < g_candidate_count; ++i) {
    const Candidate& c = g_candidates[i];
    const uint8_t* bytes = static_cast<const uint8_t*>(c.object);
    const uint8_t* attribute =
        *reinterpret_cast<uint8_t* const*>(bytes + 0x28);
    const bool readable =
        attribute != nullptr && !IsBadReadPtr(attribute, 0x14);
    const int type =
        readable ? *reinterpret_cast<const int32_t*>(attribute + 0x10) : -1;
    if (i < 12) {
      ModLog("hand pick    : candidate %p, flags 0x%02X, type field %d, "
             "%.1f units from the camera", c.object, bytes[kObjectFlags],
             type, Distance(c.position, camera));
    }
    if (type != 1) continue;
    ++typed;
    const float d = Distance(c.position, table > 0.0f ? g_target : camera);
    if (d >= best) continue;
    float radius = reinterpret_cast<VirtualFloatFn>(
        VTable(c.object)[kGet2DRadius / 4])(c.object, nullptr);
    if (!(radius >= 0.0f && radius < 1000.0f)) radius = 0.0f;
    best = d;
    found = true;
    // The Hand keeps half its length above the land, and a Hand at the
    // Creature is high above it. So the point goes on the line from the
    // camera to the Creature, a share of the way (0.5, then 0.35, then 0.65
    // on each call), where it stays over the land and its pixel is on the
    // Creature.
    static const float kShare[3] = {0.5f, 0.35f, 0.65f};
    static int call = 0;
    const float share = table > 0.0f ? 1.0f : kShare[call++ % 3];
    const float middle[3] = {c.position[0], c.position[1] + 0.5f * radius,
                             c.position[2]};
    for (int k = 0; k < 3; ++k) {
      out[k] = camera[k] + (middle[k] - camera[k]) * share;
    }
    ModLog("hand pick    : the Creature %p, radius %.2f, %.1f units from the "
           "camera, the point is %.0f%% of the way", c.object, radius, d,
           share * 100.0f);
  }
  ModLog("hand pick    : %d candidates, %d of type 1 (the Creature), the "
         "nearest is %s", g_candidate_count, typed,
         found ? "within 400 units" : "not within 400 units");
  return found;
}

// For the fake controller's word "touchc": the body bone of the nearest
// Creature (type 1) of the last first eye pass, in the world. The touch
// tests put the Hand's point in the body with it. On the table the nearest
// to the Hand within 50 cm of the room, else the nearest to the camera
// within 400 units.
bool HandDirectTouchCreaturePoint(float out[3]) {
  if (g_face == nullptr || g_hand == nullptr || g_candidate_count == 0) {
    return false;
  }
  const float table = TabletopViewScale();
  const float* from = table > 0.0f ? g_target : Camera();
  float best = table > 0.0f ? 0.5f * table : 400.0f;
  bool found = false;
  for (int i = 0; i < g_candidate_count; ++i) {
    const Candidate& c = g_candidates[i];
    const uint8_t* bytes = static_cast<const uint8_t*>(c.object);
    const uint8_t* attribute =
        *reinterpret_cast<uint8_t* const*>(bytes + 0x28);
    if (attribute == nullptr || IsBadReadPtr(attribute, 0x14) ||
        *reinterpret_cast<const int32_t*>(attribute + 0x10) != 1) {
      continue;
    }
    const float d = Distance(c.position, from);
    if (d >= best) continue;
    float bone[3];
    if (BodyPlace(c.object, bone) == 0) continue;
    best = d;
    found = true;
    for (int k = 0; k < 3; ++k) out[k] = bone[k];
    ModLog("hand pick    : the Creature %p has its body bone at %.2f %.2f "
           "%.2f, %.1f units from %s", c.object, bone[0], bone[1], bone[2], d,
           table > 0.0f ? "the Hand" : "the camera");
  }
  if (!found) {
    ModLog("hand pick    : no Creature with a body bone among %d candidates",
           g_candidate_count);
  }
  return found;
}

// The answers that the scripts of the game got from the player since the
// last call, for the report of the God view (tabletop.cpp).
uint64_t HandDirectTakeScriptAnswers() {
  const uint64_t n = g_script_answers;
  g_script_answers = 0;
  return n;
}

// Once a report window.
void HandDirectReport() {
  ForestTakeReport();
  if (g_moves_answers != 0) {
    ModLog("scripts      : %llu answers to a script that asked how you hold "
           "the land or if the rotate arrow shows came from the moves of the "
           "God view (script_camera_moves=1)", g_moves_answers);
    g_moves_answers = 0;
  }
  if (!g_installed || g_active_frames == 0) return;
  ModLog("hand         : the controller placed the Hand on %llu frames: %llu "
         "depths, %llu poses, %llu rays, %llu picks put in, %llu far picks "
         "taken out, %llu objects offered by the draw", g_active_frames,
         g_depth_writes, g_pose_writes, g_rays, g_picks, g_cleared, g_offers);
  if (g_throws != 0 || g_strokes != 0 || g_slaps != 0) {
    ModLog("hand         : %llu throws (%llu with the motion of the grip "
           "opening, %llu from the last point inside your influence), %llu "
           "strokes and %llu slaps of the Creature this window", g_throws,
           g_latched_throws, g_throws_from, g_strokes, g_slaps);
    g_throws = g_throws_from = g_strokes = g_slaps = 0;
    g_latched_throws = 0;
  }
  if (g_far_offers != 0 || g_far_land_writes != 0) {
    ModLog("far grab    : %llu objects offered to the pick by the cone, %llu "
           "candidates in the cone that the land hides, %llu past the range "
           "of %.1f m from the head, and the land point of the record written "
           "on %llu game turns of a fetch, this window. The locked target "
           "went to the pick %llu times",
           g_far_offers, g_far_hidden, g_far_out_of_range, FarGrabMaxMeters(),
           g_far_land_writes, g_far_keep_offers);
    g_far_offers = g_far_land_writes = g_far_hidden = 0;
    g_far_out_of_range = 0;
    g_far_keep_offers = 0;
  }
  FarGrabReport();
  if (g_near_block_frames != 0 || g_lock_keeps != 0 || g_leash_moves != 0) {
    ModLog("hand pick    : near objects turned the far grab off %llu times, "
           "for %llu frames. The mod left the pick record to a pick up of the "
           "game on %llu passes. The rope of the leash ended at the palm of "
           "the mod's hand on %llu calls, this window", g_near_blocks,
           g_near_block_frames, g_lock_keeps, g_leash_moves);
    g_near_blocks = g_near_block_frames = g_lock_keeps = g_leash_moves = 0;
  }
  if (g_scroll_frames != 0) {
    ModLog("hand scroll  : scrolls were in view on %llu frames, at most %d in "
           "one frame. The game's pick did not offer a scroll %llu times. "
           "With scroll_draw=1 its draw gives it to the mod's list "
           "(scroll_draw=%d). Between the two eye passes of a frame the ball "
           "of a scroll was at most %.3f units apart and %.3f units larger or "
           "smaller, this window",
           g_scroll_frames, g_scroll_most, g_scroll_added,
           g_scroll_draw ? 1 : 0, g_scroll_eye_middle, g_scroll_eye_radius);
    if (g_scroll_clicks != 0) {
      ModLog("hand scroll  : %llu presses were a click on a scroll that was "
             "not active (scroll_click=%d), this window", g_scroll_clicks,
             g_scroll_click ? 1 : 0);
      g_scroll_clicks = 0;
    }
    g_scroll_frames = g_scroll_added = 0;
    g_scroll_most = 0;
    g_scroll_eye_middle = g_scroll_eye_radius = 0.0f;
  }
  if (g_fight_rays != 0) {
    ModLog("hand fight   : %llu interface steps of a press of the fight on a "
           "Creature at the Hand. The ray from the Hand's point hit the body "
           "%llu times, and the game's ray through the pointer %llu times "
           "(fight_grip=%d), this window", g_fight_rays, g_fight_hits,
           g_fight_game_hits, g_fight_grip ? 1 : 0);
    g_fight_rays = g_fight_hits = g_fight_game_hits = 0;
  }
  if (g_target_gone != 0) {
    ModLog("hand pick    : %llu targets were gone at the start of a frame or "
           "before the pick record, and the mod dropped them (target_check=1), "
           "this window", g_target_gone);
    g_target_gone = 0;
  }
  if (g_presses != 0 || g_palm_asks != 0 || g_record_puts != 0 ||
      g_record_clears != 0) {
    ModLog("hand press   : %llu presses of the grip reached the game, %llu "
           "with no target and an empty Hand (near_rule=%d). The game's "
           "search at the palm ran %llu times: it gave an object to the "
           "press %llu times, and the mod refused its object %llu times. "
           "Before an interface step the mod put the target back in the pick "
           "record %llu times and took another object out %llu times, this "
           "window", g_presses, g_presses_none, g_rule, g_palm_asks,
           g_palm_given, g_palm_refused, g_record_puts, g_record_clears);
    g_presses = g_presses_none = g_palm_asks = g_palm_given = 0;
    g_palm_refused = g_record_puts = g_record_clears = 0;
  }
  if (g_takes != 0 || g_take_frames != 0) {
    ModLog("hand bulk    : %llu bulk takes started, %llu frames of a take, "
           "and on %llu pumps the grip was the game's button for it, this "
           "window. The test key lifted the influence %llu times.", g_takes,
           g_take_frames, g_take_grip_frames, g_influence_lifts);
    ModLog("hand bulk    : the pick record held the source of the take on "
           "%llu steps, and the mod put it back on %llu. The mod ended %llu "
           "takes through the game's step and %llu through the handler of "
           "the end packet, this window", g_take_keeps, g_take_puts,
           g_take_ends_forced, g_take_ends_direct);
    g_takes = g_take_frames = g_take_grip_frames = 0;
    g_influence_lifts = 0;
    g_take_keeps = g_take_puts = 0;
    g_take_ends_forced = g_take_ends_direct = 0;
  }
  if (g_bone_counts[0] + g_bone_counts[1] + g_bone_counts[2] != 0) {
    ModLog("hand creature: the mod asked %llu times where the body of a "
           "Creature is: %llu times it was the body bone, %llu times the "
           "place on the map, and %llu times there was no place, this window",
           g_bone_counts[0] + g_bone_counts[1] + g_bone_counts[2],
           g_bone_counts[1], g_bone_counts[2], g_bone_counts[0]);
    g_bone_counts[0] = g_bone_counts[1] = g_bone_counts[2] = 0;
  }
  if (g_palm_frames != 0) {
    ModLog("hand creature: the Hand's point was the palm of the flat hand on "
           "%llu frames (creature_palm_point=%d), this window", g_palm_frames,
           g_palm_point ? 1 : 0);
    g_palm_frames = 0;
  }
  HapticsReport();
  if (g_turns_asked != 0 || g_release_count != 0) {
    ModLog("hand throw   : %llu objects left the Hand, %.1f frames after the "
           "release action as a mean and %llu at most. The mod asked for a "
           "game turn at once %llu times, and the game ran %llu, this window",
           g_release_count,
           g_release_count != 0
               ? static_cast<double>(g_release_frames_sum) / g_release_count
               : 0.0,
           g_release_worst, g_turns_asked, g_turns_given);
    g_release_count = g_release_frames_sum = g_release_worst = 0;
    g_turns_asked = g_turns_given = 0;
  }
  if (g_virtual_count != 0 || g_touch_vetoes != 0 || g_touch_forced != 0 ||
      g_touch_kept != 0) {
    ModLog("hand creature: %llu presses with no camera move, %llu leash "
           "clicks, %llu states with no wait, %llu mesh hits refused at a "
           "distance, %llu slap branches forced by the swept test, %llu "
           "frames the pick kept the Creature, this window", g_virtual_count,
           g_leash_clicks, g_touch_waits, g_touch_vetoes, g_touch_forced,
           g_touch_kept);
    g_virtual_count = g_leash_clicks = g_touch_waits = 0;
    g_touch_vetoes = g_touch_forced = g_touch_kept = 0;
  }
  if (g_candidate_limit == kMaxCandidates) {
    ModLog("hand         : on the table, at most %d objects offered in one "
           "pass, of room for %d, %llu of them near the Hand, and %llu kept "
           "from the game's pick for the choice. The land under the Hand "
           "went to the interface %llu times. The Hand's size is %.2f.",
           g_most_candidates, kMaxCandidates, g_near_offers, g_kept_offers,
           g_land_writes, g_scale);
  }
  g_most_candidates = 0;
  g_near_offers = 0;
  g_kept_offers = 0;
  g_land_writes = 0;
  g_active_frames = g_depth_writes = g_pose_writes = g_rays = 0;
  g_picks = g_cleared = g_offers = 0;
  HandExactReport();
}
