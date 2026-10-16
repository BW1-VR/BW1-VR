// Verified addresses in runblack.exe, fan patch v1.42.
//
// The image has relocations stripped and no ASLR, and is fixed at base
// 0x00400000, so these are constant on every machine. They came from three
// agreeing sources: an RTTI walk, assert string attribution, and the
// bw1-decomp BW1W120 symbol file. See bwvr/docs/address-map.md.
#pragma once

#include <stdint.h>

namespace bw {

constexpr uintptr_t kImageBase = 0x00400000;

// SHA1 of the executable these addresses were verified against.
constexpr char kExpectedSha1[] = "9c9aea96f046911ab1b7e496fae511ed5ebbfefe";
// Its size in bytes. Every other known build has a different size.
constexpr uint64_t kExpectedSize = 8663040;

namespace render {

// LH3DRender, the engine's renderer. Function addresses.
constexpr uintptr_t kOpenD3D        = 0x0082CD80;  // static int OpenD3D()
constexpr uintptr_t kCloseD3D       = 0x0082D3F0;
constexpr uintptr_t kOpen           = 0x0082B3C0;
constexpr uintptr_t kClose          = 0x0082B570;
constexpr uintptr_t kStartFrame     = 0x0082F0E0;  // static void StartFrame()
constexpr uintptr_t kFinishFrame    = 0x0082F460;  // static void FinishFrame()
constexpr uintptr_t kDrawTriangle   = 0x0082F810;  // static, takes Vertex3D*
constexpr uintptr_t kSetProjMatrix  = 0x0082B2D0;  // __thiscall, see note below
constexpr uintptr_t kSetD3DMatrix   = 0x0082B220;
constexpr uintptr_t kSetRenderState = 0x00412940;

constexpr uintptr_t kRegisterFinishFrameCallback = 0x0082F2C0;
constexpr uintptr_t kRemoveFinishFrameCallback   = 0x0082F3B0;

// Free functions, not members of LH3DRender. Both take an explicit FVF, which
// is what tells us whether geometry arrives pre-transformed.
constexpr uintptr_t kDrawAndClip   = 0x0082A500;
constexpr uintptr_t kDrawAndClip2D = 0x0082A5B0;
constexpr uintptr_t kProjectPoint  = 0x005B7C10;

// Globals. Note that the two matrices here are NOT what the scene renders
// from. LH3DTech::UpdateCamera writes them through LH3DRender::SetD3DMatrix
// as a copy for Direct3D, but the scene is transformed on the CPU against
// bw::tech::kWorldToCamera. Writing these changes nothing on screen.
constexpr uintptr_t kViewToProj      = 0x00EC7FD0;  // LHMatrix, 12 floats
constexpr uintptr_t kWorldToView     = 0x00EC8010;  // LHMatrix, 12 floats
constexpr uintptr_t kDirect3DDevice7 = 0x00ECA638;  // IDirect3DDevice7*
constexpr uintptr_t kStartedFrame    = 0x00ECA644;  // int
constexpr uintptr_t kRenderStates    = 0x00ECA1F0;
constexpr uintptr_t kTextureStage    = 0x00EC81F0;
constexpr uintptr_t kEnableCallbacks = 0x00C386D0;  // bool

}  // namespace render

namespace hand {

constexpr uintptr_t kVTable = 0x008CBECC;  // CHand, 10 virtual functions

constexpr uintptr_t kSetDistanceFromView   = 0x0046C0D0;  // void(float)
constexpr uintptr_t kStartFixedPosAnim     = 0x0046C050;  // void(LHPoint&, int)
constexpr uintptr_t kGetRequiredState      = 0x0046CD10;
constexpr uintptr_t kThrowObject           = 0x0046DDD0;
constexpr uintptr_t kGetMaxForce           = 0x0046BFB0;
constexpr uintptr_t kUpdateHeldObject      = 0x0046E490;
constexpr uintptr_t kOperateInteractionCam = 0x0046CB60;
constexpr uintptr_t kToggleLeftRight       = 0x0046C260;  // __thiscall
// CHand+0x484C is 1 while the game draws its Hand as a right hand. The
// stored mesh is a left hand. ToggleLeftRight flips this flag and the
// winding of the triangles of the mesh.
constexpr uintptr_t kMirrorOffset          = 0x484C;
constexpr uintptr_t kShow                  = 0x0046C1B0;
constexpr uintptr_t kDraw                  = 0x0046D210;
constexpr uintptr_t kGameTurnUpdate        = 0x0046E4E0;

// HandState vtables. All 5 virtuals except Grain and Holding, which have 8.
constexpr uintptr_t kStateCamera       = 0x00900A54;
constexpr uintptr_t kStateCitadel      = 0x00900A80;
constexpr uintptr_t kStateCreature     = 0x00900AAC;
constexpr uintptr_t kStateGrain        = 0x00900B00;
constexpr uintptr_t kStateHolding      = 0x00900B40;
constexpr uintptr_t kStateMultiPickUp  = 0x00900B6C;
constexpr uintptr_t kStateNormal       = 0x00900BA0;
constexpr uintptr_t kStatePlayAnim     = 0x00900BC4;
constexpr uintptr_t kStateTotem        = 0x00900BE4;
constexpr uintptr_t kStateTug          = 0x00900C1C;
constexpr uintptr_t kStateInvisible    = 0x008CBEB4;

}  // namespace hand

namespace tech {

// LH3DTech is the software transform and lighting engine, and it is where the
// scene is really projected. LH3DRender only submits the result to Direct3D.
// The whole draw path uses FVF 0x1C4 (XYZRHW), so the Direct3D VIEW transform
// has no effect on the scene. See bwvr/docs/software-tnl.md.

constexpr uintptr_t kUpdateCamera        = 0x00819920;  // (LHPoint& pos, LHPoint& focus)
// A second writer of the same matrix, with the same argument shape and its
// own up vector at 0x00EA1B28. Unnamed in the decomp.
constexpr uintptr_t kUpdateCamera2       = 0x00819F50;
constexpr uintptr_t kUpdateWorldToCamera = 0x00819690;
constexpr uintptr_t kProjectPoint        = 0x00819390;
constexpr uintptr_t kChangeFov           = 0x008195B0;  // __thiscall void(float)
constexpr uintptr_t kUpdateViewPort      = 0x00819030;  // (long w, long h)
constexpr uintptr_t kRenderInitialization = 0x00818C60;

// The world to camera matrix that every draw actually reads. 12 floats,
// 4 rows of 3, with the translation in the last row. The camera basis lives
// in the COLUMNS: column 0 is right, column 1 is up, column 2 is forward.
// Read out of LH3DTech::ProjectPoint, not guessed.
constexpr uintptr_t kWorldToCamera = 0x00EA9E40;
// NOT a camera. 0x00EA9E80 is the light position in CAMERA space, written by
// the light setter at 0x0081E1F0, which keeps the same point in world space at
// 0x00EA9E90. Dumped from the running game it reads 869515, 618082, -443068
// beside three 500000s, which are the light's world coordinates. Left here
// under its real name so nobody writes a camera into it, as very nearly
// happened.
constexpr uintptr_t kLightInCameraSpace = 0x00EA9E80;
constexpr uintptr_t kLightInWorldSpace  = 0x00EA9E90;
constexpr uintptr_t kCameraPos     = 0x00EA1DB8;  // LHPoint, 184 readers

// The camera's forward direction, normalised, written by UpdateWorldToCamera
// at 0x00819773. GScript::GetFacingCameraPosition reads it, which is what
// confirms the meaning. LH3DWater reads it for a sub-pixel swell.
constexpr uintptr_t kCameraForward = 0x00EA1DD4;

// THE SEA. LH3DWater, an unnamed module named by its own Report3D strings.
// Not world geometry: a screen space band of horizontal scanline strips whose
// texture coordinates come from unprojecting each screen row onto the plane
// y=0. It assumes the camera has no roll, in four separate ways, so head roll
// and this renderer cannot both be had. See docs/backend.md.
constexpr uintptr_t kWaterDraw       = 0x00879930;  // __fastcall, material in ecx
constexpr uintptr_t kWaterDrawSimple = 0x0087A090;  // one world quad, roll safe
constexpr uintptr_t kWaterOpen       = 0x008792E0;
constexpr uintptr_t kWaterTiling     = 0x00C38228;  // non-zero picks the band

// The reverse of LH3DTech::ProjectPoint. __fastcall(screenXY in ecx, out in
// edx, z on the stack), ret 4. Reads ONLY the half resolution, the two field
// of view scalars, the near plane, the unscaled rotation at kWorldToCameraUnscaled
// applied transposed, and kCameraPos. This is what places the Hand, so
// restoring those two globals around it makes the Hand ignore the head.
constexpr uintptr_t kUnprojectScreenPoint = 0x0081B370;
constexpr uintptr_t kScreenPointToWorldAtDistance = 0x0074CAF0;

// The save game thumbnail. Takes its pixel extent from LHScreen::width and
// height while taking the base pointer and pitch from the locked surface, and
// clamps nothing, so a square window makes it read off the end of the row.
constexpr uintptr_t kScriptedScreenShotProcess = 0x00821750;
constexpr uintptr_t kSnapshotRequested         = 0x00EB99B4;

// The camera the whole engine is built from, once per frame, BEFORE the scene
// walk. __FASTCALL, position in ecx and focus in edx, despite ending C3.
constexpr uintptr_t kUpdateCameraFn = 0x00819920;

// The rest of the camera family. UpdateCamera builds all of these from one
// camera position, and different parts of the engine read different members,
// so a mod that moves one and not the others tears the picture in half. Every
// address here was read out of UpdateCamera, not assumed. See eye_camera.cpp.

// World to camera WITHOUT the field of view scaling, built first at
// 0x00819A58. LH3DSprite::Draw reads this one and nothing else: it loads m[9]
// from 0x00EA1D4C at 0x008405B4. Scaled m[9] = unscaled m[9] * fov_x, proved
// at 0x00819B40: fld [0xea1d4c] / fmul st(1) / fstp [0xea9e64].
constexpr uintptr_t kWorldToCameraUnscaled = 0x00EA1D28;

// A verbatim copy of the scaled matrix, made at 0x00819BF6 and 0x00819C50.
// GLandscape::Draw projects through the copy.
constexpr uintptr_t kWorldToCameraCopy = 0x00EA1D58;

// inverse(kWorldToCamera), built at 0x00819BB0 by LHMatrix::SetInverse. 17
// readers, including fn_008801D0, which bakes it into every shadow's texture
// matrix. Leaving this stale while moving kWorldToCamera is what made shadows
// change shape between the eyes.
constexpr uintptr_t kCameraToWorldInverse = 0x00EA9DE0;

// LHMatrix::SetInverse, __fastcall(destination in ecx, source in edx).
constexpr uintptr_t kSetInverse = 0x007FB290;

// Animation steps. Windmill::PreDraw and everything like it read these, step
// their own state by them and store the result, so a second scene pass with
// these left alone spins every animation at double speed.
constexpr uintptr_t kGameTimeInc   = 0x00EA9EC0;  // uint32, 157 readers
constexpr uintptr_t kDeltaTime     = 0x00C38134;  // uint32, 80 readers
constexpr uintptr_t kNearPlane     = 0x00E839E0;  // float, clip in ProjectPoint

// g_MaxScreen: two floats, res_x - 1 and res_y - 1, written by
// LH3DTech::UpdateViewPort (0x00819030). About 160 places clamp a projected x
// or y into 0..g_MaxScreen, for example LH3DTech::Draw3DWorldTriangle at
// 0x0081C24C and DrawAndClip at 0x0082A503. A clamp, not a clip: a vertex
// past the edge is put on the edge.
constexpr uintptr_t kMaxScreen     = 0x00C2AB00;

// InfoTransform, 0x3C bytes. Holds the screen mapping and the culling cone.
constexpr uintptr_t kInfoTransform = 0x00E839E4;

struct InfoTransform {
  int32_t res_x, res_y;           // 0x00
  float aspect_x_over_y;          // 0x08
  float half_res_x, half_res_y;   // 0x0C, screen_x = (1 + cx/cz) * half_res_x
  float inv_half_res_x, inv_half_res_y;  // 0x14
  // Measured: 0x1C holds the horizontal term and 0x20 the vertical one, which
  // is the opposite of the names in the bw1-decomp header. At 1280x720 they
  // read 1.4281 and 2.5389, giving 70 degrees horizontal and 43 vertical, and
  // the world to camera matrix scales its columns by exactly these.
  float inv_half_tan_fov_x;       // 0x1C
  float inv_half_tan_fov_y;       // 0x20
  float cos_half_fov_sqr;         // 0x24
  float field_0x28;               // 0x28
  float cos_half_fov;             // 0x2C
  float field_0x30;               // 0x30
  float inv_aspect_sqr_hypo_inv_times_inv_aspect;  // 0x34
  float inv_aspect_sqr_hypo_inv;  // 0x38
};

}  // namespace tech

namespace camera {

constexpr uintptr_t kCameraModeVTable = 0x008C76E0;  // 18 virtual functions
constexpr uintptr_t kFreeVTable       = 0x008C7958;
constexpr uintptr_t kGCameraVTable    = 0x008C7110;  // 80 virtual functions

constexpr uintptr_t kGCameraUpdate    = 0x00441F80;
constexpr uintptr_t kSetCameraFov     = 0x00443680;  // void(float fov, float time)
constexpr uintptr_t kHeadingPitchFrom = 0x004428D0;
constexpr uintptr_t kPointFromDHP     = 0x00442810;

}  // namespace camera

namespace game {

// GGame::Process3dEngine walks and draws the whole 3D scene. It brackets
// LH3DRender::StartFrame at 0x0054DAB5 and FinishFrame at 0x0054E41C, and
// mixes simulation updates in with the drawing, so it cannot simply be run
// twice for a second eye.
constexpr uintptr_t kProcess3dEngine = 0x0054DA80;  // __thiscall, no arguments
constexpr uintptr_t kGameLoop        = 0x0054CF20;  // GGame::Loop
constexpr uintptr_t kGGame           = 0x00D0195C;  // GGame* g_game

// The random seed GRand::LocalFloatRand steps, at g_game + this offset. Read
// out of 0x006DE5AE: mov eax, [0xd0195c] / add eax, 0x205a3c. Particle DrawAt
// functions consume it while drawing, so a second scene pass gives the second
// eye different jitter unless the seed is put back first.
constexpr uintptr_t kRandomSeedOffset = 0x205A3C;

constexpr uintptr_t kNewZObject      = 0x0083F310;  // LH3DZSorter::NewZObject
constexpr uintptr_t kZSorter         = 0x00ECA648;  // LH3DZSorter*

// The left hand option of the game: the profile value HAND_ORIENTATION, 1
// for a left hand. PlayerProfile::GetProfileByName reads it into
// PlayerProfile::Profile (0x00D4BD38) + 0x81C at 0x0066BC50, and the
// options screen writes it there at 0x00514925. The game uses it only for
// its Hand: fn_0046E5F0 (from GInterface::SetupHand and GPlayer::InitReal)
// and the packet 0x64 call CHand::ToggleLeftRight until CHand+0x484C is 0
// for a left hand and 1 for a right hand. It does not swap the buttons.
constexpr uintptr_t kHandOrientation = 0x00D4C554;  // int32

}  // namespace game

namespace gesture {
constexpr uintptr_t kVTable = 0x008DF800;  // GestureSystem, 7 virtual functions
}

// The engine's matrix: 4 rows of 3 columns, with the translation in the last
// row at m[9..11]. LH3DTech::ProjectPoint computes
//   cam.x = p.x*m[0] + p.y*m[3] + p.z*m[6] + m[9]
//   cam.y = p.x*m[1] + p.y*m[4] + p.z*m[7] + m[10]
//   cam.z = p.x*m[2] + p.y*m[5] + p.z*m[8] + m[11]
// so the camera basis vectors are the columns, not the rows. This matches the
// decomp's LHMatrix::SetIdentityMatrix, which writes 1.0 into m[0], m[4], m[8].
struct LHMatrix {
  float m[12];
};

struct LHPoint {
  float x, y, z;
};

}  // namespace bw
