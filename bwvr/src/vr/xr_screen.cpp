#include "xr_screen.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#include <string>
#include <vector>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "../common/version.h"

namespace bwvr {
namespace {

void NoLog(const char*, ...) {}

const char* StateName(XrSessionState state) {
  switch (state) {
    case XR_SESSION_STATE_IDLE: return "IDLE";
    case XR_SESSION_STATE_READY: return "READY";
    case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED (not visible yet)";
    case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
    case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
    case XR_SESSION_STATE_STOPPING: return "STOPPING";
    case XR_SESSION_STATE_EXITING: return "EXITING";
    case XR_SESSION_STATE_LOSS_PENDING: return "LOSS PENDING";
    default: return "UNKNOWN";
  }
}

// The name of a result that can come before an instance exists: the runtime
// cannot give a name then, so the loader errors that a player meets have
// their names here.
const char* ResultNameNoInstance(XrResult result) {
  switch (result) {
    case XR_ERROR_RUNTIME_FAILURE: return "XR_ERROR_RUNTIME_FAILURE";
    case XR_ERROR_OUT_OF_MEMORY: return "XR_ERROR_OUT_OF_MEMORY";
    case XR_ERROR_API_VERSION_UNSUPPORTED:
      return "XR_ERROR_API_VERSION_UNSUPPORTED";
    case XR_ERROR_INITIALIZATION_FAILED:
      return "XR_ERROR_INITIALIZATION_FAILED";
    case XR_ERROR_EXTENSION_NOT_PRESENT:
      return "XR_ERROR_EXTENSION_NOT_PRESENT";
    case XR_ERROR_FILE_ACCESS_ERROR: return "XR_ERROR_FILE_ACCESS_ERROR";
    case XR_ERROR_FILE_CONTENTS_INVALID:
      return "XR_ERROR_FILE_CONTENTS_INVALID";
    case XR_ERROR_API_LAYER_NOT_PRESENT:
      return "XR_ERROR_API_LAYER_NOT_PRESENT";
    case XR_ERROR_RUNTIME_UNAVAILABLE: return "XR_ERROR_RUNTIME_UNAVAILABLE";
    default: return nullptr;
  }
}

const char* ResultName(XrInstance instance, XrResult result) {
  static char buffer[XR_MAX_RESULT_STRING_SIZE];
  if (instance != XR_NULL_HANDLE &&
      XR_SUCCEEDED(xrResultToString(instance, result, buffer))) {
    return buffer;
  }
  const char* name = ResultNameNoInstance(result);
  if (name != nullptr) {
    _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%s, XrResult %d", name,
                static_cast<int>(result));
  } else {
    _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "XrResult %d",
                static_cast<int>(result));
  }
  return buffer;
}

template <typename T>
void Release(T*& object) {
  if (object != nullptr) {
    object->Release();
    object = nullptr;
  }
}

// THE ACTIONS AND THEIR BINDINGS. See docs/controllers.md, "2. OpenXR input
// in XrScreen". Every action has both hand subaction paths, so one action serves both
// hands.
enum Action {
  kGripPose,
  kAimPose,
  kTrigger,
  kSqueeze,
  kStick,
  kStickClick,
  kPrimary,
  kSecondary,
  kMenu,
  kHaptic,
  kActionCount
};

struct ActionInfo {
  const char* name;
  const char* label;
  XrActionType type;
};

const ActionInfo kActions[kActionCount] = {
    {"grip_pose", "Grip pose", XR_ACTION_TYPE_POSE_INPUT},
    {"aim_pose", "Aim pose", XR_ACTION_TYPE_POSE_INPUT},
    {"trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT},
    {"squeeze", "Squeeze", XR_ACTION_TYPE_FLOAT_INPUT},
    {"stick", "Stick", XR_ACTION_TYPE_VECTOR2F_INPUT},
    {"stick_click", "Stick click", XR_ACTION_TYPE_BOOLEAN_INPUT},
    {"primary", "Primary button", XR_ACTION_TYPE_BOOLEAN_INPUT},
    {"secondary", "Secondary button", XR_ACTION_TYPE_BOOLEAN_INPUT},
    {"menu", "Menu button", XR_ACTION_TYPE_BOOLEAN_INPUT},
    {"haptic", "Haptic", XR_ACTION_TYPE_VIBRATION_OUTPUT},
};

// Which hands a row applies to.
const int kLeftHand = 1;
const int kRightHand = 2;
const int kBothHands = 3;

struct Binding {
  int action;
  const char* path;  // under /user/hand/left or /user/hand/right
  int hands;
};

// A float action bound to a boolean input reads 0 or 1, so the same action
// works on a controller with a click grip.
const Binding kTouch[] = {
    {kGripPose, "input/grip/pose", kBothHands},
    {kAimPose, "input/aim/pose", kBothHands},
    {kTrigger, "input/trigger/value", kBothHands},
    {kSqueeze, "input/squeeze/value", kBothHands},
    {kStick, "input/thumbstick", kBothHands},
    {kStickClick, "input/thumbstick/click", kBothHands},
    {kPrimary, "input/a/click", kRightHand},
    {kPrimary, "input/x/click", kLeftHand},
    {kSecondary, "input/b/click", kRightHand},
    {kSecondary, "input/y/click", kLeftHand},
    {kMenu, "input/menu/click", kLeftHand},
    {kHaptic, "output/haptic", kBothHands},
};

const Binding kSimple[] = {
    {kGripPose, "input/grip/pose", kBothHands},
    {kAimPose, "input/aim/pose", kBothHands},
    {kTrigger, "input/select/click", kBothHands},
    {kMenu, "input/menu/click", kBothHands},
    {kHaptic, "output/haptic", kBothHands},
};

const Binding kIndex[] = {
    {kGripPose, "input/grip/pose", kBothHands},
    {kAimPose, "input/aim/pose", kBothHands},
    {kTrigger, "input/trigger/value", kBothHands},
    {kSqueeze, "input/squeeze/value", kBothHands},
    {kStick, "input/thumbstick", kBothHands},
    {kStickClick, "input/thumbstick/click", kBothHands},
    {kPrimary, "input/a/click", kBothHands},
    {kSecondary, "input/b/click", kBothHands},
    {kHaptic, "output/haptic", kBothHands},
};

const Binding kVive[] = {
    {kGripPose, "input/grip/pose", kBothHands},
    {kAimPose, "input/aim/pose", kBothHands},
    {kTrigger, "input/trigger/value", kBothHands},
    {kSqueeze, "input/squeeze/click", kBothHands},
    {kStick, "input/trackpad", kBothHands},
    {kStickClick, "input/trackpad/click", kBothHands},
    {kMenu, "input/menu/click", kBothHands},
    {kHaptic, "output/haptic", kBothHands},
};

const Binding kMotion[] = {
    {kGripPose, "input/grip/pose", kBothHands},
    {kAimPose, "input/aim/pose", kBothHands},
    {kTrigger, "input/trigger/value", kBothHands},
    {kSqueeze, "input/squeeze/click", kBothHands},
    {kStick, "input/thumbstick", kBothHands},
    {kStickClick, "input/thumbstick/click", kBothHands},
    {kMenu, "input/menu/click", kBothHands},
    {kHaptic, "output/haptic", kBothHands},
};

struct Profile {
  const char* name;  // after /interaction_profiles/
  const Binding* rows;
  int count;
};

#define BWVR_ROWS(a) a, static_cast<int>(sizeof(a) / sizeof(a[0]))

// The Touch profile first: Virtual Desktop presents Quest controllers with it.
const Profile kProfiles[] = {
    {"oculus/touch_controller", BWVR_ROWS(kTouch)},
    {"khr/simple_controller", BWVR_ROWS(kSimple)},
    {"valve/index_controller", BWVR_ROWS(kIndex)},
    {"htc/vive_controller", BWVR_ROWS(kVive)},
    {"microsoft/motion_controller", BWVR_ROWS(kMotion)},
};

#undef BWVR_ROWS

const char kProfilePrefix[] = "/interaction_profiles/";
const char* const kHandName[2] = {"/user/hand/left", "/user/hand/right"};

float Clamp01(float value) {
  return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

float Clamp1(float value) {
  return value < -1.0f ? -1.0f : (value > 1.0f ? 1.0f : value);
}

// The third panel quad, the Creature panel, set by the mod before
// SetPanelQuads in the same frame (docs/ui-layer.md, "The Creature panel").
PanelQuad g_staged_quad = {};
bool g_have_staged_quad = false;

// The cards of this frame, set by the mod before EndFrame (xr_screen.h, THE
// CARDS).
CardLayer g_staged_card[kCards] = {};
bool g_have_staged_card[kCards] = {false, false, false, false};
bool g_card_failed[kCards] = {false, false, false, false};
const char* const kCardName[kCards] = {
    "hint card of the pointing hand", "hint card of the other hand",
    "subtitle card", "ring"};
// What a card holds, for the line that says where it goes after a failure.
const char* const kCardHolds[kCards] = {"hints", "hints", "subtitles",
                                        "items of the ring"};

// The cut scene window of this frame, set by the mod before EndFrame
// (xr_screen.h, THE CUT SCENE WINDOW).
WindowLayer g_staged_window = {};
bool g_have_staged_window = false;
bool g_mask_failed = false;
// The picture of the walls: one color, so it is small.
const uint32_t kMaskSize = 16;

}  // namespace

struct XrScreen::Impl {
  LogFn log = &NoLog;

  XrInstance instance = XR_NULL_HANDLE;
  XrSystemId system = XR_NULL_SYSTEM_ID;
  XrSession session = XR_NULL_HANDLE;
  XrSpace space = XR_NULL_HANDLE;
  XrSwapchain swapchain[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
  XrSpace view_space = XR_NULL_HANDLE;
  XrSessionState state = XR_SESSION_STATE_UNKNOWN;
  bool session_running = false;

  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  ID3D11Texture2D* staging = nullptr;
  ID3D11Texture2D** images[2] = {nullptr, nullptr};
  uint32_t image_count[2] = {0, 0};
  int acquired[2] = {-1, -1};
  bool logged_fov = false;
  bool logged_projection = false;

  // A recenter from the headset menu takes effect at a set time. Poses
  // before it are still in the old space.
  bool recenter_waiting = false;
  XrTime recenter_at = 0;

  // The frame stays open between BeginFrame and EndFrame.
  XrFrameState frame_state = {XR_TYPE_FRAME_STATE};
  bool frame_open = false;

  uint32_t width = 0;
  uint32_t height = 0;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

  float distance = 2.0f;
  float quad_width = 2.6f;

  PFN_xrGetD3D11GraphicsRequirementsKHR get_requirements = nullptr;

  // The upload, timed in three parts.
  LARGE_INTEGER frequency = {0};
  uint64_t wait_ticks = 0;    // xrAcquireSwapchainImage and xrWaitSwapchainImage
  uint64_t copy_ticks = 0;    // Map, the row copies, Unmap
  uint64_t submit_ticks = 0;  // CopyResource into the swapchain image
  uint64_t uploads = 0;
  // xrEndFrame, which hands the two pictures to the compositor. At a headset
  // native resolution the compositor has to encode what it is given, so this
  // is one of the few costs left that can still grow with the picture.
  uint64_t end_ticks = 0;
  uint64_t ends = 0;

  // The interface panel: a third swapchain, and the quads cut from it: the
  // menu, the help and the Creature panel.
  bool panel_wanted = false;
  XrSwapchain panel_swapchain = XR_NULL_HANDLE;
  ID3D11Texture2D** panel_images = nullptr;
  uint32_t panel_image_count = 0;
  int panel_acquired = -1;  // released this frame, so a layer may show it
  PanelQuad panel_quads[kPanelQuads] = {};
  bool logged_panel = false;
  uint64_t panel_layers[kPanelQuads] = {0, 0, 0};  // for the report
  uint64_t panel_frames = 0;

  // The cards: each has a swapchain of its own, made at its first layer.
  struct CardChain {
    XrSwapchain swapchain = XR_NULL_HANDLE;
    ID3D11Texture2D** images = nullptr;
    uint32_t image_count = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t* swizzle = nullptr;  // for a swapchain with red first
    bool logged = false;
    uint64_t layers = 0;  // for the report
    int misses = 0;       // frames in a row with no image for the card
  };
  CardChain cards[kCards];

  // The walls of the cut scene window: one swapchain, made at the first
  // frame with walls, and four quads of it.
  XrSwapchain mask_swapchain = XR_NULL_HANDLE;
  ID3D11Texture2D** mask_images = nullptr;
  uint32_t mask_image_count = 0;
  bool logged_mask = false;
  bool logged_window = false;
  int32_t logged_window_crop = 0;
  int32_t logged_window_edge = 0;
  bool logged_window_card = false;
  uint64_t mask_frames = 0;    // for the report
  uint64_t window_frames = 0;

  // Motion controllers. All of these stay null when CreateInput fails, and
  // the screen works without them.
  XrActionSet action_set = XR_NULL_HANDLE;
  XrAction actions[kActionCount] = {};
  XrPath hand_path[2] = {XR_NULL_PATH, XR_NULL_PATH};
  XrSpace grip_space[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
  XrSpace aim_space[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
  bool input_ready = false;
  bool focused = false;
  bool profiles_logged = false;
  char profile[2][XR_MAX_PATH_LENGTH] = {{0}, {0}};

  // The layers of one frame. BuildFrame fills them, and SendFrame gives them
  // to xrEndFrame. They are here, and not on the stack, because SendFrame
  // can run on the second thread.
  struct Built {
    XrCompositionLayerProjectionView eye_views[2];
    XrCompositionLayerProjection projection;
    XrCompositionLayerQuad panels[kPanelQuads];
    XrCompositionLayerQuad card_quads[kCards];
    XrCompositionLayerQuad walls[4];
    XrCompositionLayerQuad quads[2];
    XrCompositionLayerQuad subtitle_quad;
    const XrCompositionLayerBaseHeader* layers[1 + kPanelQuads + kCards + 4];
    uint32_t layer_count;
    XrTime display_time;
    bool world;               // a projection layer, and not a screen
    bool with_card[kCards];
    bool with_subtitles;      // a screen with the subtitle card
    LARGE_INTEGER waited_at;  // the end of xrWaitFrame of this frame
    LARGE_INTEGER before;     // the start of BuildFrame
    LARGE_INTEGER built;      // its end
    LARGE_INTEGER end_start;  // the start of xrEndFrame
    LARGE_INTEGER end_done;   // its end
    XrResult result;          // of xrEndFrame
  };
  Built built = {};

  // A LATE FRAME (EndFrameLate). RunLate waits for the GPU and copies the
  // textures to the swapchain images, and SendFrame calls xrEndFrame. Both
  // run on the frame thread or on the second thread. While the second thread
  // has a frame, the frame thread calls only xrPollEvent, xrWaitFrame and
  // Haptic, and it does not use the Direct3D 11 context.
  LateFrame late = {};
  XrResult late_result[3] = {XR_SUCCESS, XR_SUCCESS, XR_SUCCESS};
  uint64_t late_gpu = 0;   // the last frame: the ticks of the wait for the GPU
  uint64_t late_copy = 0;  // and of the copies
  HANDLE send_thread = nullptr;
  DWORD send_thread_id = 0;
  HANDLE send_go = nullptr;    // the second thread has a frame to send
  HANDLE send_done = nullptr;  // the runtime has that frame
  bool send_busy = false;      // only the frame thread writes it
  bool send_quit = false;
  bool send_failed = false;    // the second thread did not start
  // For the report.
  uint64_t late_frames = 0;
  uint64_t late_thread_frames = 0;
  uint64_t late_gpu_ticks = 0;
  uint64_t late_copy_ticks = 0;
  uint64_t late_end_ticks = 0;
  uint64_t join_ticks = 0;
  uint64_t join_worst = 0;
  // THE AGE OF A FRAME: from the end of xrWaitFrame to the end of
  // xrEndFrame. The runtime shows a frame that it gets later at a later
  // time, so this number says what a way of the frame end costs.
  LARGE_INTEGER waited_at = {};
  uint64_t age_ticks = 0;
  uint64_t age_worst = 0;
  uint64_t age_frames = 0;

  static DWORD WINAPI SendThreadMain(void* param);
  bool StartSendThread();
  void StopSendThread();
  XrResult CopyLate(XrSwapchain chain, ID3D11Texture2D** chain_images,
                    void* texture);
  void RunLate();
  void SendFrame();

  bool CreateInput();                  // Start, after the LOCAL space
  void SuggestProfile(const Profile& info);
  void SyncInput(HandInput* hands, XrTime when);  // after xrWaitFrame
  void ClearInput(HandInput* hands);   // not running, or not focused
  void LogProfiles();                  // on INTERACTION_PROFILE_CHANGED
  void DestroyInput();                 // Stop, before the session
  void DestroyActionSet();             // Stop, after the session
  bool SendCard(int index, const CardLayer& card,
                XrCompositionLayerQuad* quad);
  void DestroyCard(int index);
  bool SendMask(const WindowLayer& window, XrCompositionLayerQuad quads[4]);
  void DestroyMask();

  bool Check(XrResult result, const char* what) {
    if (XR_SUCCEEDED(result)) return true;
    log("  openxr: %s failed (%s)", what, ResultName(instance, result));
    return false;
  }
};

// --- motion controllers -----------------------------------------------------

bool XrScreen::Impl::CreateInput() {
  // Paths belong to the instance.
  for (int h = 0; h < 2; ++h) {
    if (!Check(xrStringToPath(instance, kHandName[h], &hand_path[h]),
               "xrStringToPath(hand)")) {
      return false;
    }
  }

  XrActionSetCreateInfo set_info = {XR_TYPE_ACTION_SET_CREATE_INFO};
  strcpy_s(set_info.actionSetName, "play");
  strcpy_s(set_info.localizedActionSetName, "Play");
  set_info.priority = 0;
  if (!Check(xrCreateActionSet(instance, &set_info, &action_set),
             "xrCreateActionSet")) {
    return false;
  }

  // Every action must exist before the first attach.
  for (int a = 0; a < kActionCount; ++a) {
    XrActionCreateInfo info = {XR_TYPE_ACTION_CREATE_INFO};
    strcpy_s(info.actionName, kActions[a].name);
    strcpy_s(info.localizedActionName, kActions[a].label);
    info.actionType = kActions[a].type;
    info.countSubactionPaths = 2;
    info.subactionPaths = hand_path;
    if (!Check(xrCreateAction(action_set, &info, &actions[a]),
               "xrCreateAction")) {
      return false;
    }
  }

  // A runtime may refuse a profile it does not know. That is logged and
  // skipped, and never fails the session.
  for (const Profile& p : kProfiles) SuggestProfile(p);

  XrSessionActionSetsAttachInfo attach = {
      XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
  attach.countActionSets = 1;
  attach.actionSets = &action_set;
  if (!Check(xrAttachSessionActionSets(session, &attach),
             "xrAttachSessionActionSets")) {
    return false;
  }

  for (int h = 0; h < 2; ++h) {
    XrActionSpaceCreateInfo space_info = {XR_TYPE_ACTION_SPACE_CREATE_INFO};
    space_info.subactionPath = hand_path[h];
    space_info.poseInActionSpace.orientation.w = 1.0f;
    space_info.action = actions[kGripPose];
    if (!Check(xrCreateActionSpace(session, &space_info, &grip_space[h]),
               "xrCreateActionSpace(grip)")) {
      return false;
    }
    space_info.action = actions[kAimPose];
    if (!Check(xrCreateActionSpace(session, &space_info, &aim_space[h]),
               "xrCreateActionSpace(aim)")) {
      return false;
    }
  }
  input_ready = true;
  return true;
}

void XrScreen::Impl::SuggestProfile(const Profile& info) {
  char full[XR_MAX_PATH_LENGTH];
  _snprintf_s(full, sizeof(full), _TRUNCATE, "%s%s", kProfilePrefix, info.name);
  XrPath profile_path = XR_NULL_PATH;
  if (XR_FAILED(xrStringToPath(instance, full, &profile_path))) {
    log("  controllers  : could not make the path for %s", info.name);
    return;
  }

  XrActionSuggestedBinding bound[2 * kActionCount + 4];
  const uint32_t capacity = sizeof(bound) / sizeof(bound[0]);
  uint32_t count = 0;
  for (int i = 0; i < info.count; ++i) {
    const Binding& row = info.rows[i];
    for (int h = 0; h < 2; ++h) {
      if ((row.hands & (h == 0 ? kLeftHand : kRightHand)) == 0) continue;
      if (count >= capacity) continue;
      char path[XR_MAX_PATH_LENGTH];
      _snprintf_s(path, sizeof(path), _TRUNCATE, "%s/%s", kHandName[h],
                  row.path);
      bound[count].action = actions[row.action];
      if (XR_FAILED(xrStringToPath(instance, path, &bound[count].binding))) {
        continue;
      }
      ++count;
    }
  }

  XrInteractionProfileSuggestedBinding suggest = {
      XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
  suggest.interactionProfile = profile_path;
  suggest.countSuggestedBindings = count;
  suggest.suggestedBindings = bound;
  const XrResult result = xrSuggestInteractionProfileBindings(instance, &suggest);
  if (XR_FAILED(result)) {
    log("  controllers  : the runtime does not take %s (%s), skipped",
        info.name, ResultName(instance, result));
  }
}

void XrScreen::Impl::ClearInput(HandInput* hands) {
  for (int h = 0; h < 2; ++h) {
    const uint64_t serial = hands[h].serial;
    memset(&hands[h], 0, sizeof(HandInput));
    hands[h].serial = serial;
    hands[h].aim_orientation[3] = 1.0f;
    hands[h].grip_orientation[3] = 1.0f;
  }
}

void XrScreen::Impl::LogProfiles() {
  if (!input_ready) return;
  for (int h = 0; h < 2; ++h) {
    profile[h][0] = 0;
    XrInteractionProfileState now = {XR_TYPE_INTERACTION_PROFILE_STATE};
    if (XR_SUCCEEDED(
            xrGetCurrentInteractionProfile(session, hand_path[h], &now)) &&
        now.interactionProfile != XR_NULL_PATH) {
      char full[XR_MAX_PATH_LENGTH];
      uint32_t length = 0;
      if (XR_SUCCEEDED(xrPathToString(instance, now.interactionProfile,
                                      sizeof(full), &length, full))) {
        const size_t prefix = sizeof(kProfilePrefix) - 1;
        const char* name =
            (strncmp(full, kProfilePrefix, prefix) == 0) ? full + prefix : full;
        strcpy_s(profile[h], name);
      }
    }
    log("  controllers  : %s hand profile \"%s\"", h == 0 ? "left" : "right",
        profile[h][0] ? profile[h] : "none");
  }
}

void XrScreen::Impl::SyncInput(HandInput* hands, XrTime when) {
  if (!input_ready) return;
  focused = (state == XR_SESSION_STATE_FOCUSED);

  XrActiveActionSet active = {action_set, XR_NULL_PATH};
  XrActionsSyncInfo sync = {XR_TYPE_ACTIONS_SYNC_INFO};
  sync.countActiveActionSets = 1;
  sync.activeActionSets = &active;
  const XrResult result = xrSyncActions(session, &sync);
  // XR_SESSION_NOT_FOCUSED is a success code. Every action then reads
  // inactive, and the hands must read as released.
  if (XR_FAILED(result) || result == XR_SESSION_NOT_FOCUSED || !focused) {
    ClearInput(hands);
    for (int h = 0; h < 2; ++h) ++hands[h].serial;
    return;
  }

  if (!profiles_logged) {
    profiles_logged = true;
    LogProfiles();
  }

  for (int h = 0; h < 2; ++h) {
    HandInput& out = hands[h];
    const uint64_t serial = out.serial + 1;
    memset(&out, 0, sizeof(out));
    out.serial = serial;
    out.aim_orientation[3] = 1.0f;
    out.grip_orientation[3] = 1.0f;

    XrActionStateGetInfo get = {XR_TYPE_ACTION_STATE_GET_INFO};
    get.subactionPath = hand_path[h];

    XrActionStatePose aim_state = {XR_TYPE_ACTION_STATE_POSE};
    XrActionStatePose grip_state = {XR_TYPE_ACTION_STATE_POSE};
    get.action = actions[kAimPose];
    xrGetActionStatePose(session, &get, &aim_state);
    get.action = actions[kGripPose];
    xrGetActionStatePose(session, &get, &grip_state);
    out.active = (aim_state.isActive == XR_TRUE) ||
                 (grip_state.isActive == XR_TRUE);
    if (!out.active) continue;

    XrActionStateFloat value = {XR_TYPE_ACTION_STATE_FLOAT};
    get.action = actions[kTrigger];
    if (XR_SUCCEEDED(xrGetActionStateFloat(session, &get, &value)) &&
        value.isActive) {
      out.trigger = Clamp01(value.currentState);
    }
    value = {XR_TYPE_ACTION_STATE_FLOAT};
    get.action = actions[kSqueeze];
    if (XR_SUCCEEDED(xrGetActionStateFloat(session, &get, &value)) &&
        value.isActive) {
      out.squeeze = Clamp01(value.currentState);
    }

    XrActionStateVector2f vector = {XR_TYPE_ACTION_STATE_VECTOR2F};
    get.action = actions[kStick];
    if (XR_SUCCEEDED(xrGetActionStateVector2f(session, &get, &vector)) &&
        vector.isActive) {
      out.stick[0] = Clamp1(vector.currentState.x);
      out.stick[1] = Clamp1(vector.currentState.y);
    }

    bool* const flags[4] = {&out.stick_click, &out.primary, &out.secondary,
                            &out.menu};
    const Action ids[4] = {kStickClick, kPrimary, kSecondary, kMenu};
    for (int i = 0; i < 4; ++i) {
      XrActionStateBoolean pressed = {XR_TYPE_ACTION_STATE_BOOLEAN};
      get.action = actions[ids[i]];
      *flags[i] = XR_SUCCEEDED(xrGetActionStateBoolean(session, &get, &pressed)) &&
                  pressed.isActive && pressed.currentState == XR_TRUE;
    }

    // Both poses are in the LOCAL space and at the display time of the eyes,
    // so the hands and the head share one moment.
    XrSpaceLocation location = {XR_TYPE_SPACE_LOCATION};
    if (aim_state.isActive &&
        XR_SUCCEEDED(xrLocateSpace(aim_space[h], space, when, &location))) {
      const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT |
                                        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
      out.aim_valid = (location.locationFlags & need) == need;
      const XrSpaceLocationFlags tracked =
          XR_SPACE_LOCATION_POSITION_TRACKED_BIT |
          XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
      out.aim_tracked = (location.locationFlags & tracked) == tracked;
      out.aim_position[0] = location.pose.position.x;
      out.aim_position[1] = location.pose.position.y;
      out.aim_position[2] = location.pose.position.z;
      out.aim_orientation[0] = location.pose.orientation.x;
      out.aim_orientation[1] = location.pose.orientation.y;
      out.aim_orientation[2] = location.pose.orientation.z;
      out.aim_orientation[3] = location.pose.orientation.w;
    }
    location = {XR_TYPE_SPACE_LOCATION};
    if (grip_state.isActive &&
        XR_SUCCEEDED(xrLocateSpace(grip_space[h], space, when, &location))) {
      const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT |
                                        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
      out.grip_valid = (location.locationFlags & need) == need;
      const XrSpaceLocationFlags tracked =
          XR_SPACE_LOCATION_POSITION_TRACKED_BIT |
          XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
      out.grip_tracked = (location.locationFlags & tracked) == tracked;
      out.grip_position[0] = location.pose.position.x;
      out.grip_position[1] = location.pose.position.y;
      out.grip_position[2] = location.pose.position.z;
      out.grip_orientation[0] = location.pose.orientation.x;
      out.grip_orientation[1] = location.pose.orientation.y;
      out.grip_orientation[2] = location.pose.orientation.z;
      out.grip_orientation[3] = location.pose.orientation.w;
    }
  }
}

void XrScreen::Impl::DestroyInput() {
  for (int h = 0; h < 2; ++h) {
    if (grip_space[h] != XR_NULL_HANDLE) xrDestroySpace(grip_space[h]);
    if (aim_space[h] != XR_NULL_HANDLE) xrDestroySpace(aim_space[h]);
    grip_space[h] = XR_NULL_HANDLE;
    aim_space[h] = XR_NULL_HANDLE;
  }
  input_ready = false;
  focused = false;
}

// Destroying the set destroys its actions.
void XrScreen::Impl::DestroyActionSet() {
  if (action_set != XR_NULL_HANDLE) xrDestroyActionSet(action_set);
  action_set = XR_NULL_HANDLE;
  for (int a = 0; a < kActionCount; ++a) actions[a] = XR_NULL_HANDLE;
}

XrScreen::XrScreen()
    : impl_(new Impl()),
      running_(false),
      should_render_(false),
      eyes_(1),
      presented_(0),
      skipped_(0),
      projection_(false),
      recommended_w_(0),
      recommended_h_(0),
      render_half_angle_(0.0f),
      have_render_pose_(false) {
  for (int i = 0; i < 2; ++i) render_views_[i].valid = false;
  for (int i = 0; i < 2; ++i) views_[i].valid = false;
  memset(hands_, 0, sizeof(hands_));
  for (int i = 0; i < 2; ++i) {
    hands_[i].aim_orientation[3] = 1.0f;
    hands_[i].grip_orientation[3] = 1.0f;
  }
}

XrScreen::~XrScreen() {
  Stop();
  delete impl_;
}

bool XrScreen::RecommendedSize(uint32_t* width, uint32_t* height) const {
  if (recommended_w_ == 0 || recommended_h_ == 0) return false;
  *width = recommended_w_;
  *height = recommended_h_;
  return true;
}

const char* XrScreen::HandProfile(int index) const {
  return impl_->profile[index ? 1 : 0];
}

bool XrScreen::InputFocused() const {
  return impl_->input_ready && impl_->focused;
}

void XrScreen::Haptic(int index, float amplitude, float seconds) {
  Impl& s = *impl_;
  if (!s.input_ready || !s.session_running) return;
  const int h = index ? 1 : 0;
  XrHapticActionInfo info = {XR_TYPE_HAPTIC_ACTION_INFO};
  info.action = s.actions[kHaptic];
  info.subactionPath = s.hand_path[h];
  XrHapticVibration vibration = {XR_TYPE_HAPTIC_VIBRATION};
  vibration.amplitude = Clamp01(amplitude);
  vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
  vibration.duration = (seconds > 0.0f)
                           ? static_cast<XrDuration>(seconds * 1e9f)
                           : XR_MIN_HAPTIC_DURATION;
  // XR_SESSION_NOT_FOCUSED is a success code and means nothing was played.
  xrApplyHapticFeedback(s.session, &info,
                        reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
}

void XrScreen::SetLog(LogFn log) {
  impl_->log = (log != nullptr) ? log : &NoLog;
}

void XrScreen::SetPlacement(float distance_meters, float width_meters) {
  impl_->distance = distance_meters;
  impl_->quad_width = width_meters;
}

bool XrScreen::Start(uint32_t width, uint32_t height, int eyes, LogFn log) {
  Impl& s = *impl_;
  eyes_ = (eyes >= 2) ? 2 : 1;
  s.log = (log != nullptr) ? log : &NoLog;
  s.width = width;
  s.height = height;

  // --- instance -----------------------------------------------------------
  // Which extensions the runtime lists. A model of the
  // controller needs XR_MSFT_controller_model or XR_FB_render_model.
  {
    uint32_t count = 0;
    if (XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(nullptr, 0, &count,
                                                            nullptr)) &&
        count > 0 && count < 512) {
      std::vector<XrExtensionProperties> list(count,
                                              {XR_TYPE_EXTENSION_PROPERTIES});
      if (XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(
              nullptr, count, &count, list.data()))) {
        std::string names;
        bool model = false;
        for (uint32_t i = 0; i < count; ++i) {
          names += list[i].extensionName;
          names += ' ';
          if (names.size() > 200) {
            s.log("  hints        : extensions: %s", names.c_str());
            names.clear();
          }
          if (strcmp(list[i].extensionName, "XR_MSFT_controller_model") == 0 ||
              strcmp(list[i].extensionName, "XR_FB_render_model") == 0) {
            model = true;
          }
        }
        s.log("  hints        : the runtime lists %u instance extensions, %s "
              "a controller model", count, model ? "with" : "without");
        if (!names.empty()) s.log("  hints        : extensions: %s", names.c_str());
      }
    }
  }
  const char* extensions[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};

  XrInstanceCreateInfo create = {XR_TYPE_INSTANCE_CREATE_INFO};
  create.enabledExtensionCount = 1;
  create.enabledExtensionNames = extensions;
  strcpy_s(create.applicationInfo.applicationName, BWVR_PRODUCT_NAME);
  // A uint32_t, so not XR_MAKE_VERSION, which is 64 bit.
  create.applicationInfo.applicationVersion =
      (BWVR_VERSION_MAJOR << 16) | (BWVR_VERSION_MINOR << 8) |
      BWVR_VERSION_PATCH;
  strcpy_s(create.applicationInfo.engineName, "bwvr");
  create.applicationInfo.engineVersion = 1;
  create.applicationInfo.apiVersion = XR_API_VERSION_1_0;

  XrResult result = xrCreateInstance(&create, &s.instance);
  if (XR_FAILED(result)) {
    s.log("  openxr: xrCreateInstance failed (%s)",
          ResultName(XR_NULL_HANDLE, result));
    if (result == XR_ERROR_FILE_ACCESS_ERROR ||
        result == XR_ERROR_FILE_CONTENTS_INVALID) {
      // The runtime gave its extensions, so the loader found it. At the
      // instance the loader also loads each implicit API layer, and a layer
      // whose file cannot be loaded or read stops the instance. The game is
      // a 32 bit program, so a layer that has only a 64 bit DLL can do it.
      s.log("  The OpenXR loader cannot load or read a file: most often an");
      s.log("  OpenXR API layer for 32 bit programs that is broken or left");
      s.log("  behind. The tool \"OpenXR API Layers GUI\" shows such a layer.");
      s.log("  The list is in the registry at HKLM\\SOFTWARE\\WOW6432Node\\");
      s.log("  Khronos\\OpenXR\\1\\ApiLayers\\Implicit and at HKCU\\SOFTWARE\\");
      s.log("  Khronos\\OpenXR\\1\\ApiLayers\\Implicit.");
    } else {
      s.log("  Make sure that the OpenXR runtime of your headset runs and");
      s.log("  that the headset is connected.");
    }
    return false;
  }

  XrInstanceProperties properties = {XR_TYPE_INSTANCE_PROPERTIES};
  if (XR_SUCCEEDED(xrGetInstanceProperties(s.instance, &properties))) {
    s.log("  openxr: runtime \"%s\" version %u.%u.%u", properties.runtimeName,
          static_cast<unsigned>(XR_VERSION_MAJOR(properties.runtimeVersion)),
          static_cast<unsigned>(XR_VERSION_MINOR(properties.runtimeVersion)),
          static_cast<unsigned>(XR_VERSION_PATCH(properties.runtimeVersion)));
  }

  // --- system -------------------------------------------------------------
  XrSystemGetInfo system_info = {XR_TYPE_SYSTEM_GET_INFO};
  system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  result = xrGetSystem(s.instance, &system_info, &s.system);
  if (XR_FAILED(result)) {
    s.log("  openxr: no head mounted display (%s)",
          ResultName(s.instance, result));
    s.log("  Connect the headset in Virtual Desktop, then start the game.");
    Stop();
    return false;
  }

  XrSystemProperties system_properties = {XR_TYPE_SYSTEM_PROPERTIES};
  if (XR_SUCCEEDED(
          xrGetSystemProperties(s.instance, s.system, &system_properties))) {
    s.log("  openxr: headset \"%s\"", system_properties.systemName);
  }

  // What the headset would like each eye rendered at. This needs no session
  // and no swapchain, so it is available here, and the launcher reads it back
  // out of the ini to offer a percentage of it.
  {
    uint32_t view_count = 0;
    XrViewConfigurationView views[2];
    for (int i = 0; i < 2; ++i) {
      views[i] = XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW};
    }
    if (XR_SUCCEEDED(xrEnumerateViewConfigurationViews(
            s.instance, s.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2,
            &view_count, views)) &&
        view_count >= 1) {
      recommended_w_ = views[0].recommendedImageRectWidth;
      recommended_h_ = views[0].recommendedImageRectHeight;
      s.log("  openxr: the headset asks for %u x %u per eye, and would take up "
            "to %u x %u",
            recommended_w_, recommended_h_, views[0].maxImageRectWidth,
            views[0].maxImageRectHeight);
    }
  }

  // --- Direct3D 11 on the adapter the runtime wants ------------------------
  if (!s.Check(xrGetInstanceProcAddr(
                   s.instance, "xrGetD3D11GraphicsRequirementsKHR",
                   reinterpret_cast<PFN_xrVoidFunction*>(&s.get_requirements)),
               "xrGetInstanceProcAddr(D3D11 requirements)")) {
    Stop();
    return false;
  }

  XrGraphicsRequirementsD3D11KHR requirements = {
      XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
  if (!s.Check(s.get_requirements(s.instance, s.system, &requirements),
               "xrGetD3D11GraphicsRequirementsKHR")) {
    Stop();
    return false;
  }

  // The runtime names the adapter by LUID. Using a different one would make
  // every present a cross adapter copy, if it worked at all.
  IDXGIFactory1* factory = nullptr;
  if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                reinterpret_cast<void**>(&factory)))) {
    s.log("  openxr: CreateDXGIFactory1 failed");
    Stop();
    return false;
  }

  IDXGIAdapter1* adapter = nullptr;
  for (UINT index = 0;; ++index) {
    IDXGIAdapter1* candidate = nullptr;
    if (factory->EnumAdapters1(index, &candidate) == DXGI_ERROR_NOT_FOUND) break;
    DXGI_ADAPTER_DESC1 description;
    if (SUCCEEDED(candidate->GetDesc1(&description)) &&
        memcmp(&description.AdapterLuid, &requirements.adapterLuid,
               sizeof(LUID)) == 0) {
      adapter = candidate;
      char name[128];
      WideCharToMultiByte(CP_ACP, 0, description.Description, -1, name,
                          sizeof(name), nullptr, nullptr);
      s.log("  openxr: adapter \"%s\"", name);
      break;
    }
    candidate->Release();
  }
  factory->Release();

  if (adapter == nullptr) {
    s.log("  openxr: the runtime asked for an adapter this process cannot see");
    Stop();
    return false;
  }

  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1,
                                      D3D_FEATURE_LEVEL_11_0};
  HRESULT created = D3D11CreateDevice(
      adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, levels,
      sizeof(levels) / sizeof(levels[0]), D3D11_SDK_VERSION, &s.device, nullptr,
      &s.context);
  adapter->Release();
  if (FAILED(created)) {
    s.log("  openxr: D3D11CreateDevice failed (0x%08X)", created);
    Stop();
    return false;
  }

  // --- session ------------------------------------------------------------
  XrGraphicsBindingD3D11KHR binding = {XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
  binding.device = s.device;

  XrSessionCreateInfo session_info = {XR_TYPE_SESSION_CREATE_INFO};
  session_info.next = &binding;
  session_info.systemId = s.system;
  if (!s.Check(xrCreateSession(s.instance, &session_info, &s.session),
               "xrCreateSession")) {
    Stop();
    return false;
  }

  // LOCAL keeps the screen fixed where the player was looking at startup,
  // which is what a cinema screen should do. VIEW would glue it to the face.
  XrReferenceSpaceCreateInfo space_info = {
      XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
  space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
  space_info.poseInReferenceSpace.orientation.w = 1.0f;
  if (!s.Check(xrCreateReferenceSpace(s.session, &space_info, &s.space),
               "xrCreateReferenceSpace")) {
    Stop();
    return false;
  }

  // The eye poses are wanted relative to the head, because the separation between
  // them is all that is needed here, not where the player is standing.
  XrReferenceSpaceCreateInfo view_space_info = {
      XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
  view_space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
  view_space_info.poseInReferenceSpace.orientation.w = 1.0f;
  if (!s.Check(xrCreateReferenceSpace(s.session, &view_space_info,
                                      &s.view_space),
               "xrCreateReferenceSpace(VIEW)")) {
    Stop();
    return false;
  }

  // --- motion controllers ---------------------------------------------------
  // The screen works without them, so a failure here is only logged.
  if (!s.CreateInput()) {
    s.log("  controllers  : not available, the screen works without them");
  }

  // --- swapchain ----------------------------------------------------------
  uint32_t format_count = 0;
  xrEnumerateSwapchainFormats(s.session, 0, &format_count, nullptr);
  int64_t* formats = new int64_t[format_count ? format_count : 1];
  xrEnumerateSwapchainFormats(s.session, format_count, &format_count, formats);

  // BGRA first, because that is what DirectDraw hands us and it avoids a
  // per pixel swizzle. sRGB variants first, because the game's output is
  // already gamma encoded and the compositor should treat it that way.
  const DXGI_FORMAT wanted[] = {
      DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
      DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM};
  for (int want = 0; want < 4 && s.format == DXGI_FORMAT_UNKNOWN; ++want) {
    for (uint32_t i = 0; i < format_count; ++i) {
      if (formats[i] == static_cast<int64_t>(wanted[want])) {
        s.format = wanted[want];
        break;
      }
    }
  }
  delete[] formats;

  if (s.format == DXGI_FORMAT_UNKNOWN) {
    s.log("  openxr: no 8 bit BGRA or RGBA swapchain format offered");
    Stop();
    return false;
  }
  s.log("  openxr: swapchain format %d, %ux%u", static_cast<int>(s.format),
        s.width, s.height);

  XrSwapchainCreateInfo swapchain_info = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
  swapchain_info.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT |
                              XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
  swapchain_info.format = static_cast<int64_t>(s.format);
  swapchain_info.sampleCount = 1;
  swapchain_info.width = s.width;
  swapchain_info.height = s.height;
  swapchain_info.faceCount = 1;
  swapchain_info.arraySize = 1;
  swapchain_info.mipCount = 1;

  for (int eye = 0; eye < eyes_; ++eye) {
    if (!s.Check(xrCreateSwapchain(s.session, &swapchain_info,
                                   &s.swapchain[eye]),
                 "xrCreateSwapchain")) {
      Stop();
      return false;
    }

    xrEnumerateSwapchainImages(s.swapchain[eye], 0, &s.image_count[eye],
                               nullptr);
    const uint32_t count = s.image_count[eye] ? s.image_count[eye] : 1;
    XrSwapchainImageD3D11KHR* raw = new XrSwapchainImageD3D11KHR[count];
    for (uint32_t i = 0; i < count; ++i) {
      raw[i] = {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR};
    }
    xrEnumerateSwapchainImages(
        s.swapchain[eye], s.image_count[eye], &s.image_count[eye],
        reinterpret_cast<XrSwapchainImageBaseHeader*>(raw));

    s.images[eye] = new ID3D11Texture2D*[count];
    for (uint32_t i = 0; i < s.image_count[eye]; ++i) {
      s.images[eye][i] = raw[i].texture;
    }
    delete[] raw;
  }

  // The interface panel. The same format and size as the eyes, because the
  // panel target is the render size and its color is gamma encoded like the
  // eyes. The screen works without it, so a failure is only logged.
  if (s.panel_wanted && eyes_ == 2) {
    if (XR_SUCCEEDED(xrCreateSwapchain(s.session, &swapchain_info,
                                       &s.panel_swapchain))) {
      xrEnumerateSwapchainImages(s.panel_swapchain, 0, &s.panel_image_count,
                                 nullptr);
      const uint32_t count = s.panel_image_count ? s.panel_image_count : 1;
      XrSwapchainImageD3D11KHR* raw = new XrSwapchainImageD3D11KHR[count];
      for (uint32_t i = 0; i < count; ++i) {
        raw[i] = {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR};
      }
      xrEnumerateSwapchainImages(
          s.panel_swapchain, s.panel_image_count, &s.panel_image_count,
          reinterpret_cast<XrSwapchainImageBaseHeader*>(raw));
      s.panel_images = new ID3D11Texture2D*[count];
      for (uint32_t i = 0; i < s.panel_image_count; ++i) {
        s.panel_images[i] = raw[i].texture;
      }
      delete[] raw;
      s.log("  openxr: the interface panel has its own swapchain, %u images",
            s.panel_image_count);
    } else {
      s.panel_swapchain = XR_NULL_HANDLE;
      s.log("  openxr: no swapchain for the interface panel, so the interface "
            "stays in the eye pictures");
    }
  }

  // A dynamic texture is the cheap way to get CPU pixels onto the GPU: map,
  // copy the rows, unmap, then let the GPU do the copy into the swapchain.
  D3D11_TEXTURE2D_DESC staging_desc;
  ZeroMemory(&staging_desc, sizeof(staging_desc));
  staging_desc.Width = s.width;
  staging_desc.Height = s.height;
  staging_desc.MipLevels = 1;
  staging_desc.ArraySize = 1;
  staging_desc.Format = s.format;
  staging_desc.SampleDesc.Count = 1;
  staging_desc.Usage = D3D11_USAGE_DYNAMIC;
  staging_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  if (FAILED(s.device->CreateTexture2D(&staging_desc, nullptr, &s.staging))) {
    s.log("  openxr: could not create the upload texture");
    Stop();
    return false;
  }

  s.log("  openxr: ready, %d eye(s), %u swapchain images each", eyes_,
        s.image_count[0]);
  running_ = true;
  return true;
}

bool XrScreen::BeginFrame() {
  Impl& s = *impl_;
  should_render_ = false;
  if (!running_ || s.session == XR_NULL_HANDLE) return false;

  // --- events -------------------------------------------------------------
  for (;;) {
    XrEventDataBuffer event = {XR_TYPE_EVENT_DATA_BUFFER};
    if (xrPollEvent(s.instance, &event) != XR_SUCCESS) break;
    // An event can end the session, so the second thread gives its frame to
    // the runtime first.
    Join();

    if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
      const XrEventDataSessionStateChanged* changed =
          reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
      s.state = changed->state;
      s.focused = (s.state == XR_SESSION_STATE_FOCUSED);
      s.log("  openxr: session state %d, %s", static_cast<int>(s.state),
            StateName(s.state));
      if (s.state == XR_SESSION_STATE_SYNCHRONIZED) {
        s.log("  The runtime is running us but not showing us. Put the");
        s.log("  headset on, and close the Virtual Desktop menu if it is up.");
      }

      if (s.state == XR_SESSION_STATE_READY && !s.session_running) {
        XrSessionBeginInfo begin = {XR_TYPE_SESSION_BEGIN_INFO};
        begin.primaryViewConfigurationType =
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        if (s.Check(xrBeginSession(s.session, &begin), "xrBeginSession")) {
          s.session_running = true;
        }
      } else if (s.state == XR_SESSION_STATE_STOPPING) {
        xrEndSession(s.session);
        s.session_running = false;
      } else if (s.state == XR_SESSION_STATE_EXITING ||
                 s.state == XR_SESSION_STATE_LOSS_PENDING) {
        running_ = false;
        s.ClearInput(hands_);
        return false;
      }
    } else if (event.type ==
               XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
      // A recenter from the headset's own menu. The runtime turns and moves
      // LOCAL space, so the way the head faces becomes straight ahead. The mod
      // keeps its own origin on top of LOCAL space, and that origin would now
      // be off by the whole turn. So the mod recenters as well, at the first
      // frame whose poses are in the new space.
      const XrEventDataReferenceSpaceChangePending* change =
          reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(
              &event);
      if (change->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL) {
        s.recenter_waiting = true;
        s.recenter_at = change->changeTime;
        s.log("  openxr: the headset recentered its space");
      }
    } else if (event.type == XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED) {
      s.LogProfiles();
    } else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
      s.log("  openxr: the runtime is going away");
      running_ = false;
      s.ClearInput(hands_);
      return false;
    }
  }

  // A sleeping headset must not leave a button held.
  if (!s.session_running) {
    Join();
    s.ClearInput(hands_);
    return true;  // alive, just not presenting yet
  }

  // --- wait and begin -----------------------------------------------------
  XrFrameWaitInfo wait_info = {XR_TYPE_FRAME_WAIT_INFO};
  s.frame_state = {XR_TYPE_FRAME_STATE};
  const XrResult waited = xrWaitFrame(s.session, &wait_info, &s.frame_state);
  LARGE_INTEGER waited_at;
  QueryPerformanceCounter(&waited_at);
  // xrWaitFrame can run while the second thread sends the frame before.
  // xrBeginFrame cannot: the runtime must have that frame first. Join reads
  // the time of that frame, so the time of this frame is set after it.
  Join();
  s.waited_at = waited_at;
  if (!s.Check(waited, "xrWaitFrame")) {
    s.ClearInput(hands_);
    return true;
  }

  if (s.recenter_waiting &&
      (s.recenter_at == 0 ||
       s.frame_state.predictedDisplayTime >= s.recenter_at)) {
    s.recenter_waiting = false;
    recenter_due_ = true;
  }

  // The hands are read at the display time of the eyes, in the same space.
  s.SyncInput(hands_, s.frame_state.predictedDisplayTime);

  XrFrameBeginInfo begin_info = {XR_TYPE_FRAME_BEGIN_INFO};
  if (!s.Check(xrBeginFrame(s.session, &begin_info), "xrBeginFrame")) {
    return true;
  }
  s.frame_open = true;
  should_render_ = (s.frame_state.shouldRender == XR_TRUE);
  if (!should_render_) ++skipped_;

  // --- where the runtime wants each eye -----------------------------------
  for (int i = 0; i < 2; ++i) views_[i].valid = false;

  XrViewLocateInfo locate = {XR_TYPE_VIEW_LOCATE_INFO};
  locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  locate.displayTime = s.frame_state.predictedDisplayTime;
  // LOCAL, not VIEW.
  //
  // In the VIEW space the orientation is identity and the positions never
  // move, whatever the player does, so head tracking would read as no head
  // tracking at all. The mod needs where the head actually is, and the
  // projection layer has to report poses in the same space it names, which
  // is this one.
  locate.space = s.space;

  XrViewState view_state = {XR_TYPE_VIEW_STATE};
  XrView raw_views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
  uint32_t found = 0;
  if (XR_SUCCEEDED(xrLocateViews(s.session, &locate, &view_state, 2, &found,
                                 raw_views))) {
    const bool usable =
        (view_state.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0 &&
        (view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
    for (uint32_t i = 0; i < found && i < 2; ++i) {
      views_[i].position[0] = raw_views[i].pose.position.x;
      views_[i].position[1] = raw_views[i].pose.position.y;
      views_[i].position[2] = raw_views[i].pose.position.z;
      views_[i].orientation[0] = raw_views[i].pose.orientation.x;
      views_[i].orientation[1] = raw_views[i].pose.orientation.y;
      views_[i].orientation[2] = raw_views[i].pose.orientation.z;
      views_[i].orientation[3] = raw_views[i].pose.orientation.w;
      views_[i].fov_left = raw_views[i].fov.angleLeft;
      views_[i].fov_right = raw_views[i].fov.angleRight;
      views_[i].fov_up = raw_views[i].fov.angleUp;
      views_[i].fov_down = raw_views[i].fov.angleDown;
      views_[i].valid = usable;
    }
    // The angles, once. The game renders a symmetric frustum whose vertical
    // half angle is derived from the render aspect, so the render has to be
    // shaped to cover what the headset actually asks for. At 16:9 it almost
    // certainly does not.
    if (!s.logged_fov && found >= 2 && usable) {
      s.logged_fov = true;
      for (int i = 0; i < 2; ++i) {
        s.log("  openxr: eye %d fov left %.1f right %.1f up %.1f down %.1f deg",
              i, raw_views[i].fov.angleLeft * 57.29578f,
              raw_views[i].fov.angleRight * 57.29578f,
              raw_views[i].fov.angleUp * 57.29578f,
              raw_views[i].fov.angleDown * 57.29578f);
      }
      const float ipd = raw_views[1].pose.position.x - raw_views[0].pose.position.x;
      s.log("  openxr: eye separation %.1f mm", ipd * 1000.0f);
    }
  }
  return true;
}

void XrScreen::SetEyeImage(int index, const void* pixels, int pitch) {
  Impl& s = *impl_;
  if (!s.frame_open || !should_render_ || pixels == nullptr) return;
  const int eye = (index >= 1 && eyes_ > 1) ? 1 : 0;
  if (s.swapchain[eye] == XR_NULL_HANDLE) return;

  if (s.frequency.QuadPart == 0) QueryPerformanceFrequency(&s.frequency);
  LARGE_INTEGER t0, t1, t2, t3;
  QueryPerformanceCounter(&t0);

  uint32_t image = 0;
  XrSwapchainImageAcquireInfo acquire = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  if (!s.Check(xrAcquireSwapchainImage(s.swapchain[eye], &acquire, &image),
               "xrAcquireSwapchainImage")) {
    return;
  }
  XrSwapchainImageWaitInfo image_wait = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  image_wait.timeout = XR_INFINITE_DURATION;
  if (!s.Check(xrWaitSwapchainImage(s.swapchain[eye], &image_wait),
               "xrWaitSwapchainImage")) {
    return;
  }

  QueryPerformanceCounter(&t1);

  D3D11_MAPPED_SUBRESOURCE mapped;
  if (SUCCEEDED(s.context->Map(s.staging, 0, D3D11_MAP_WRITE_DISCARD, 0,
                               &mapped))) {
    const uint8_t* source = static_cast<const uint8_t*>(pixels);
    uint8_t* destination = static_cast<uint8_t*>(mapped.pData);
    const size_t row_bytes = static_cast<size_t>(s.width) * 4;
    // One copy when the two pitches agree, which they usually do, because a
    // single large copy beats one call per row.
    if (static_cast<size_t>(mapped.RowPitch) == row_bytes &&
        static_cast<size_t>(pitch) == row_bytes) {
      memcpy(destination, source, row_bytes * s.height);
    } else {
      for (uint32_t y = 0; y < s.height; ++y) {
        memcpy(destination + static_cast<size_t>(y) * mapped.RowPitch,
               source + static_cast<size_t>(y) * pitch, row_bytes);
      }
    }
    s.context->Unmap(s.staging, 0);
    QueryPerformanceCounter(&t2);
    s.context->CopyResource(s.images[eye][image], s.staging);
  } else {
    QueryPerformanceCounter(&t2);
  }

  XrSwapchainImageReleaseInfo release = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  xrReleaseSwapchainImage(s.swapchain[eye], &release);
  s.acquired[eye] = static_cast<int>(image);

  QueryPerformanceCounter(&t3);
  s.wait_ticks += t1.QuadPart - t0.QuadPart;
  s.copy_ticks += t2.QuadPart - t1.QuadPart;
  s.submit_ticks += t3.QuadPart - t2.QuadPart;
  ++s.uploads;
}

void* XrScreen::D3D11Device() const { return impl_->device; }

uint32_t XrScreen::SwapchainFormat() const {
  return static_cast<uint32_t>(impl_->format);
}

void XrScreen::SetEyeTexture(int index, void* texture) {
  Impl& s = *impl_;
  if (!s.frame_open || !should_render_ || texture == nullptr) return;
  const int eye = (index >= 1 && eyes_ > 1) ? 1 : 0;
  if (s.swapchain[eye] == XR_NULL_HANDLE) return;

  if (s.frequency.QuadPart == 0) QueryPerformanceFrequency(&s.frequency);
  LARGE_INTEGER t0, t1, t3;
  QueryPerformanceCounter(&t0);

  uint32_t image = 0;
  XrSwapchainImageAcquireInfo acquire = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  if (!s.Check(xrAcquireSwapchainImage(s.swapchain[eye], &acquire, &image),
               "xrAcquireSwapchainImage")) {
    return;
  }
  XrSwapchainImageWaitInfo image_wait = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  image_wait.timeout = XR_INFINITE_DURATION;
  if (!s.Check(xrWaitSwapchainImage(s.swapchain[eye], &image_wait),
               "xrWaitSwapchainImage")) {
    return;
  }
  QueryPerformanceCounter(&t1);

  // One graphics memory to graphics memory copy, queued on the GPU. The CPU
  // does not read or write a single pixel here, which is the whole point.
  s.context->CopyResource(s.images[eye][image],
                          static_cast<ID3D11Texture2D*>(texture));

  XrSwapchainImageReleaseInfo release = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  xrReleaseSwapchainImage(s.swapchain[eye], &release);
  s.acquired[eye] = static_cast<int>(image);

  QueryPerformanceCounter(&t3);
  s.wait_ticks += t1.QuadPart - t0.QuadPart;
  s.submit_ticks += t3.QuadPart - t1.QuadPart;
  ++s.uploads;
}

void XrScreen::EnablePanel(bool on) { impl_->panel_wanted = on; }

void XrScreen::StageCard(int card, const CardLayer& layer) {
  if (card < 0 || card >= kCards) return;
  g_staged_card[card] = layer;
  g_have_staged_card[card] = true;
}

bool XrScreen::CardFailed(int card) {
  return card < 0 || card >= kCards || g_card_failed[card];
}

void XrScreen::Impl::DestroyCard(int index) {
  CardChain& chain = cards[index];
  if (chain.swapchain != XR_NULL_HANDLE) {
    xrDestroySwapchain(chain.swapchain);
    chain.swapchain = XR_NULL_HANDLE;
  }
  delete[] chain.images;
  chain.images = nullptr;
  chain.image_count = 0;
  delete[] chain.swizzle;
  chain.swizzle = nullptr;
  chain.width = 0;
  chain.height = 0;
}

// One card of this frame: the pixels go into the next image of its
// swapchain, and `quad` gets the layer. The swapchain is made at the first
// layer of the card, with the size of its picture. Each image of the
// swapchain holds an older picture, so each frame writes the part that
// shows. False when the card cannot go out.
bool XrScreen::Impl::SendCard(int index, const CardLayer& card,
                              XrCompositionLayerQuad* quad) {
  if (g_card_failed[index] || card.pixels == nullptr) return false;
  const int32_t x = card.rect[0], y = card.rect[1];
  const int32_t w = card.rect[2], h = card.rect[3];
  if (card.texture_width == 0 || card.texture_height == 0 ||
      card.texture_width > 8192 || card.texture_height > 8192 || x < 0 ||
      y < 0 || w <= 0 || h <= 0 ||
      x + w > static_cast<int32_t>(card.texture_width) ||
      y + h > static_cast<int32_t>(card.texture_height) ||
      !(card.width > 0.0f) || !(card.height > 0.0f)) {
    return false;
  }
  CardChain& chain = cards[index];
  // A picture of a new size needs a new swapchain.
  if (chain.swapchain != XR_NULL_HANDLE &&
      (chain.width != card.texture_width ||
       chain.height != card.texture_height)) {
    DestroyCard(index);
  }
  if (chain.swapchain == XR_NULL_HANDLE) {
    XrSwapchainCreateInfo info = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT |
                      XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format = static_cast<int64_t>(format);
    info.sampleCount = 1;
    info.width = card.texture_width;
    info.height = card.texture_height;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    if (!Check(xrCreateSwapchain(session, &info, &chain.swapchain),
               "xrCreateSwapchain(card)")) {
      chain.swapchain = XR_NULL_HANDLE;
      g_card_failed[index] = true;
      log("  openxr: no swapchain for the %s, so the %s stay in the eye "
          "pictures", kCardName[index], kCardHolds[index]);
      return false;
    }
    xrEnumerateSwapchainImages(chain.swapchain, 0, &chain.image_count,
                               nullptr);
    const uint32_t count = chain.image_count ? chain.image_count : 1;
    XrSwapchainImageD3D11KHR* raw = new XrSwapchainImageD3D11KHR[count];
    for (uint32_t i = 0; i < count; ++i) {
      raw[i] = {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR};
    }
    xrEnumerateSwapchainImages(
        chain.swapchain, chain.image_count, &chain.image_count,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(raw));
    chain.images = new ID3D11Texture2D*[count];
    for (uint32_t i = 0; i < chain.image_count; ++i) {
      chain.images[i] = raw[i].texture;
    }
    delete[] raw;
    chain.width = card.texture_width;
    chain.height = card.texture_height;
    log("  openxr: the %s has its own swapchain, %u by %u, %u images",
        kCardName[index], chain.width, chain.height, chain.image_count);
  }

  // A card that cannot go out must not take its words away for good. After
  // three frames in a row with no image, they go back to the eye pictures.
  const auto miss = [this, index, &chain]() {
    if (++chain.misses >= 3 && !g_card_failed[index]) {
      g_card_failed[index] = true;
      log("  openxr: the %s got no swapchain image 3 times, so the %s stay "
          "in the eye pictures", kCardName[index], kCardHolds[index]);
    }
    return false;
  };
  uint32_t image = 0;
  XrSwapchainImageAcquireInfo acquire = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  if (!Check(xrAcquireSwapchainImage(chain.swapchain, &acquire, &image),
             "xrAcquireSwapchainImage(card)")) {
    return miss();
  }
  XrSwapchainImageWaitInfo image_wait = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  image_wait.timeout = XR_INFINITE_DURATION;
  const bool waited = Check(xrWaitSwapchainImage(chain.swapchain, &image_wait),
                            "xrWaitSwapchainImage(card)");
  if (waited && image < chain.image_count) {
    const uint32_t pitch = chain.width * 4;
    const uint8_t* source = card.pixels;
    // The card is BGRA. A swapchain with red first needs the two swapped.
    if (format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
        format == DXGI_FORMAT_R8G8B8A8_UNORM) {
      if (chain.swizzle == nullptr) {
        chain.swizzle = new uint8_t[pitch * chain.height];
      }
      for (int32_t row = y; row < y + h; ++row) {
        const uint8_t* in = card.pixels + row * pitch + x * 4;
        uint8_t* out = chain.swizzle + row * pitch + x * 4;
        for (int32_t i = 0; i < w; ++i, in += 4, out += 4) {
          out[0] = in[2];
          out[1] = in[1];
          out[2] = in[0];
          out[3] = in[3];
        }
      }
      source = chain.swizzle;
    }
    D3D11_BOX box;
    box.left = static_cast<UINT>(x);
    box.top = static_cast<UINT>(y);
    box.front = 0;
    box.right = static_cast<UINT>(x + w);
    box.bottom = static_cast<UINT>(y + h);
    box.back = 1;
    context->UpdateSubresource(chain.images[image], 0, &box,
                               source + y * pitch + x * 4, pitch, 0);
  }
  XrSwapchainImageReleaseInfo release = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  xrReleaseSwapchainImage(chain.swapchain, &release);
  if (!waited || image >= chain.image_count) return miss();
  chain.misses = 0;

  *quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
  // Without the unpremultiplied flag the runtime reads the color as
  // multiplied by the alpha, which is what the card holds.
  quad->layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
  quad->space = space;
  quad->eyeVisibility = XR_EYE_VISIBILITY_BOTH;
  quad->subImage.swapchain = chain.swapchain;
  quad->subImage.imageRect.offset = {x, y};
  quad->subImage.imageRect.extent = {w, h};
  quad->subImage.imageArrayIndex = 0;
  quad->pose.position = {card.position[0], card.position[1], card.position[2]};
  quad->pose.orientation = {card.orientation[0], card.orientation[1],
                            card.orientation[2], card.orientation[3]};
  quad->size = {card.width, card.height};
  ++chain.layers;
  if (!chain.logged) {
    chain.logged = true;
    log("  openxr: the %s goes out as one quad layer over the projection "
        "layer, %.3f by %.3f m", kCardName[index], card.width, card.height);
  }
  return true;
}

void XrScreen::StageWindow(const WindowLayer& layer) {
  g_staged_window = layer;
  g_have_staged_window = true;
}

void XrScreen::Impl::DestroyMask() {
  if (mask_swapchain != XR_NULL_HANDLE) {
    xrDestroySwapchain(mask_swapchain);
    mask_swapchain = XR_NULL_HANDLE;
  }
  delete[] mask_images;
  mask_images = nullptr;
  mask_image_count = 0;
}

// The walls of the cut scene window for this frame: black with the alpha of
// the frame goes into the next image of the swapchain, and `quads` gets the
// four layers. False when the walls cannot go out. Then the frame has none.
bool XrScreen::Impl::SendMask(const WindowLayer& window,
                              XrCompositionLayerQuad quads[4]) {
  if (g_mask_failed || !(window.alpha > 0.004f)) return false;
  for (int k = 0; k < 4; ++k) {
    if (!(window.wall_size[k][0] > 0.0f) || !(window.wall_size[k][1] > 0.0f)) {
      return false;
    }
  }
  if (mask_swapchain == XR_NULL_HANDLE) {
    XrSwapchainCreateInfo info = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT |
                      XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format = static_cast<int64_t>(format);
    info.sampleCount = 1;
    info.width = kMaskSize;
    info.height = kMaskSize;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    if (!Check(xrCreateSwapchain(session, &info, &mask_swapchain),
               "xrCreateSwapchain(walls)")) {
      mask_swapchain = XR_NULL_HANDLE;
      g_mask_failed = true;
      log("  openxr: no swapchain for the walls of the cut scene window, so "
          "the view changes to the window with no walls");
      return false;
    }
    xrEnumerateSwapchainImages(mask_swapchain, 0, &mask_image_count, nullptr);
    const uint32_t count = mask_image_count ? mask_image_count : 1;
    XrSwapchainImageD3D11KHR* raw = new XrSwapchainImageD3D11KHR[count];
    for (uint32_t i = 0; i < count; ++i) {
      raw[i] = {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR};
    }
    xrEnumerateSwapchainImages(
        mask_swapchain, mask_image_count, &mask_image_count,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(raw));
    mask_images = new ID3D11Texture2D*[count];
    for (uint32_t i = 0; i < mask_image_count; ++i) {
      mask_images[i] = raw[i].texture;
    }
    delete[] raw;
  }

  uint32_t image = 0;
  XrSwapchainImageAcquireInfo acquire = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  if (!Check(xrAcquireSwapchainImage(mask_swapchain, &acquire, &image),
             "xrAcquireSwapchainImage(walls)")) {
    return false;
  }
  XrSwapchainImageWaitInfo image_wait = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  image_wait.timeout = XR_INFINITE_DURATION;
  const bool waited = Check(xrWaitSwapchainImage(mask_swapchain, &image_wait),
                            "xrWaitSwapchainImage(walls)");
  if (waited && image < mask_image_count) {
    // Black has no color to multiply, so only the alpha changes. It is the
    // last byte of a pixel in each format the swapchain can have.
    float alpha = window.alpha > 1.0f ? 1.0f : window.alpha;
    uint8_t pixels[kMaskSize * kMaskSize * 4];
    for (uint32_t i = 0; i < kMaskSize * kMaskSize; ++i) {
      pixels[i * 4 + 0] = 0;
      pixels[i * 4 + 1] = 0;
      pixels[i * 4 + 2] = 0;
      pixels[i * 4 + 3] = static_cast<uint8_t>(alpha * 255.0f + 0.5f);
    }
    context->UpdateSubresource(mask_images[image], 0, nullptr, pixels,
                               kMaskSize * 4, 0);
  }
  XrSwapchainImageReleaseInfo release = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  xrReleaseSwapchainImage(mask_swapchain, &release);
  if (!waited || image >= mask_image_count) return false;

  for (int k = 0; k < 4; ++k) {
    XrCompositionLayerQuad& quad = quads[k];
    quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    // Without the unpremultiplied flag the runtime reads the color as
    // multiplied by the alpha. Black is the same in both.
    quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    quad.space = space;
    quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    quad.subImage.swapchain = mask_swapchain;
    quad.subImage.imageRect.offset = {0, 0};
    quad.subImage.imageRect.extent = {static_cast<int32_t>(kMaskSize),
                                      static_cast<int32_t>(kMaskSize)};
    quad.subImage.imageArrayIndex = 0;
    quad.pose.position = {window.wall_position[k][0],
                          window.wall_position[k][1],
                          window.wall_position[k][2]};
    quad.pose.orientation = {window.orientation[0], window.orientation[1],
                             window.orientation[2], window.orientation[3]};
    quad.size = {window.wall_size[k][0], window.wall_size[k][1]};
  }
  ++mask_frames;
  if (!logged_mask) {
    logged_mask = true;
    log("  openxr: the walls of the cut scene window go out as four quad "
        "layers over the projection layer");
  }
  return true;
}

bool XrScreen::panel_ready() const {
  return impl_->panel_swapchain != XR_NULL_HANDLE;
}

// The panel, the way SetEyeTexture sends an eye: acquire, wait, one copy on
// the GPU, release.
void XrScreen::SetPanelTexture(void* texture) {
  Impl& s = *impl_;
  s.panel_acquired = -1;
  if (!s.frame_open || !should_render_ || texture == nullptr ||
      s.panel_swapchain == XR_NULL_HANDLE) {
    return;
  }
  uint32_t image = 0;
  XrSwapchainImageAcquireInfo acquire = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  if (!s.Check(xrAcquireSwapchainImage(s.panel_swapchain, &acquire, &image),
               "xrAcquireSwapchainImage(panel)")) {
    return;
  }
  XrSwapchainImageWaitInfo image_wait = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  image_wait.timeout = XR_INFINITE_DURATION;
  if (!s.Check(xrWaitSwapchainImage(s.panel_swapchain, &image_wait),
               "xrWaitSwapchainImage(panel)")) {
    return;
  }
  s.context->CopyResource(s.panel_images[image],
                          static_cast<ID3D11Texture2D*>(texture));
  XrSwapchainImageReleaseInfo release = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  xrReleaseSwapchainImage(s.panel_swapchain, &release);
  s.panel_acquired = static_cast<int>(image);
}

void XrScreen::SetPanelQuads(const PanelQuad quads[2]) {
  for (int i = 0; i < 2; ++i) impl_->panel_quads[i] = quads[i];
  // The Creature panel shows only in the frame that staged it.
  impl_->panel_quads[2] = g_staged_quad;
  if (!g_have_staged_quad) impl_->panel_quads[2].visible = false;
  g_have_staged_quad = false;
}

void XrScreen::StagePanelQuad(const PanelQuad& quad) {
  g_staged_quad = quad;
  g_have_staged_quad = true;
}

void XrScreen::ReportUpload(uint64_t frames) {
  Impl& s = *impl_;
  if (s.panel_frames > 0) {
    s.log("  ui panel     : %llu menu and %llu help quad layers over the "
          "projection in %llu frames, and %llu Creature panel quad "
          "layers", s.panel_layers[0], s.panel_layers[1],
          s.panel_frames, s.panel_layers[2]);
    s.panel_layers[0] = 0;
    s.panel_layers[1] = 0;
    s.panel_layers[2] = 0;
    s.panel_frames = 0;
  }
  if (s.cards[kCardHintPointing].layers + s.cards[kCardHintOther].layers >
      0) {
    s.log("  hint card    : %llu quad layers of the hint card of the pointing "
          "hand and %llu of the other hand over the projection",
          s.cards[kCardHintPointing].layers, s.cards[kCardHintOther].layers);
    s.cards[kCardHintPointing].layers = 0;
    s.cards[kCardHintOther].layers = 0;
  }
  if (s.cards[kCardSubtitles].layers > 0) {
    s.log("  subtitles    : %llu quad layers of the subtitle card over the "
          "projection", s.cards[kCardSubtitles].layers);
    s.cards[kCardSubtitles].layers = 0;
  }
  if (s.cards[kCardRing].layers > 0) {
    s.log("  ring         : %llu quad layers of the ring over the projection",
          s.cards[kCardRing].layers);
    s.cards[kCardRing].layers = 0;
  }
  if (s.age_frames > 0 && s.frequency.QuadPart != 0) {
    const double to_ms = 1000.0 / static_cast<double>(s.frequency.QuadPart);
    s.log("  frame age    : from the end of xrWaitFrame to the end of "
          "xrEndFrame: a mean of %.2f ms, and %.2f ms at the most, in %llu "
          "frames",
          (static_cast<double>(s.age_ticks) /
           static_cast<double>(s.age_frames)) * to_ms,
          static_cast<double>(s.age_worst) * to_ms, s.age_frames);
    s.age_ticks = 0;
    s.age_worst = 0;
    s.age_frames = 0;
  }
  if (s.late_frames > 0 && s.frequency.QuadPart != 0) {
    const double to_ms = 1000.0 / static_cast<double>(s.frequency.QuadPart);
    const double count = static_cast<double>(s.late_frames);
    s.log("  frame send   : %llu frames with one wait for the GPU, %llu of "
          "them on the second thread. The mean of a frame: the wait for the "
          "GPU %.2f ms, the copies to the runtime %.2f ms, xrEndFrame %.2f "
          "ms. The frame thread waited for the second thread %.2f ms, and "
          "%.2f ms at the most",
          s.late_frames, s.late_thread_frames,
          (static_cast<double>(s.late_gpu_ticks) / count) * to_ms,
          (static_cast<double>(s.late_copy_ticks) / count) * to_ms,
          (static_cast<double>(s.late_end_ticks) / count) * to_ms,
          (static_cast<double>(s.join_ticks) / count) * to_ms,
          static_cast<double>(s.join_worst) * to_ms);
    s.late_frames = 0;
    s.late_thread_frames = 0;
    s.late_gpu_ticks = 0;
    s.late_copy_ticks = 0;
    s.late_end_ticks = 0;
    s.join_ticks = 0;
    s.join_worst = 0;
    // A late frame is no upload, so the lines below do not clear these.
    if (s.uploads == 0) {
      s.end_ticks = 0;
      s.ends = 0;
    }
  }
  if (s.uploads == 0 || s.frequency.QuadPart == 0 || frames == 0) return;
  const double to_ms = 1000.0 / static_cast<double>(s.frequency.QuadPart);
  const double per_frame = static_cast<double>(frames);
  s.log("  upload       : wait %.2f ms + copy %.2f ms + submit %.2f ms per "
        "frame over %llu uploads",
        (static_cast<double>(s.wait_ticks) / per_frame) * to_ms,
        (static_cast<double>(s.copy_ticks) / per_frame) * to_ms,
        (static_cast<double>(s.submit_ticks) / per_frame) * to_ms, s.uploads);
  if (s.ends > 0) {
    s.log("  submit       : xrEndFrame %.2f ms per frame over %llu frames",
          (static_cast<double>(s.end_ticks) / per_frame) * to_ms, s.ends);
  }
  s.wait_ticks = 0;
  s.copy_ticks = 0;
  s.submit_ticks = 0;
  s.uploads = 0;
  s.end_ticks = 0;
  s.ends = 0;
}

void XrScreen::SetRenderPose(int index, const float position[3],
                             const float orientation[4]) {
  const int eye = (index >= 1) ? 1 : 0;
  for (int i = 0; i < 3; ++i) render_views_[eye].position[i] = position[i];
  for (int i = 0; i < 4; ++i) render_views_[eye].orientation[i] = orientation[i];
  render_views_[eye].valid = true;
  have_render_pose_ = render_views_[0].valid && render_views_[1].valid;
}

void XrScreen::EndFrame() {
  if (!BuildFrame()) return;
  impl_->SendFrame();
  FinishFrame();
}

// Makes the layers of the frame, and sends the cards and the walls of the
// frame. The layers are in Impl::built after it. False when no frame is
// open.
bool XrScreen::BuildFrame() {
  Impl& s = *impl_;
  if (!s.frame_open) return false;
  if (s.frequency.QuadPart == 0) QueryPerformanceFrequency(&s.frequency);
  Impl::Built& b = s.built;
  QueryPerformanceCounter(&b.before);
  b.waited_at = s.waited_at;
  s.frame_open = false;
  b.layer_count = 0;
  b.display_time = s.frame_state.predictedDisplayTime;
  b.world = false;
  b.with_subtitles = false;
  for (int i = 0; i < kCards; ++i) b.with_card[i] = false;

  // PROJECTION, OR A SCREEN.
  //
  // A projection layer is the world: it says this image was rendered from this
  // pose with this frustum, and the runtime places it accordingly and
  // reprojects it to wherever the head is at display time. The poses and the
  // frustums here are the ones xrLocateViews returned at BeginFrame, so they
  // are the same ones the caller rendered with, which is what makes the
  // promise true.
  //
  // A quad layer is a screen hanging in the world. It is kept as the way back,
  // because a wrong pose shears the world when the head moves.
  //
  // THE CUT SCENE WINDOW takes a frame from the first kind to the second:
  // the frame that the mod staged as a screen goes out as quads at the pose
  // of the stage. A frame staged with walls keeps its projection layer.
  const bool window_staged = g_have_staged_window;
  const WindowLayer window = g_staged_window;
  g_have_staged_window = false;
  const bool as_screen = window_staged && window.screen;
  if (projection_ && !as_screen && eyes_ == 2 && s.acquired[0] >= 0 &&
      s.acquired[1] >= 0 && views_[0].valid && views_[1].valid) {
    XrCompositionLayerProjectionView* eye_views = b.eye_views;
    for (int eye = 0; eye < 2; ++eye) {
      XrCompositionLayerProjectionView& v = eye_views[eye];
      v = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
      // Declare what was rendered, which is not always the newest pose known.
      const EyeView& from =
          have_render_pose_ ? render_views_[eye] : views_[eye];
      v.pose.position.x = from.position[0];
      v.pose.position.y = from.position[1];
      v.pose.position.z = from.position[2];
      v.pose.orientation.x = from.orientation[0];
      v.pose.orientation.y = from.orientation[1];
      v.pose.orientation.z = from.orientation[2];
      v.pose.orientation.w = from.orientation[3];
      // The frustum reported has to be the frustum rendered, not the one the
      // runtime asked for. The game renders a symmetric view, so the caller
      // tells us the half angle it used and both sides get it.
      const float half = render_half_angle_ > 0.0f ? render_half_angle_ : 0.0f;
      if (half > 0.0f) {
        const float aspect_half =
            atanf(tanf(half) * static_cast<float>(s.height) /
                  static_cast<float>(s.width));
        v.fov.angleLeft = -half;
        v.fov.angleRight = half;
        v.fov.angleUp = aspect_half;
        v.fov.angleDown = -aspect_half;
      } else {
        v.fov.angleLeft = views_[eye].fov_left;
        v.fov.angleRight = views_[eye].fov_right;
        v.fov.angleUp = views_[eye].fov_up;
        v.fov.angleDown = views_[eye].fov_down;
      }
      v.subImage.swapchain = s.swapchain[eye];
      v.subImage.imageRect.offset = {0, 0};
      v.subImage.imageRect.extent = {static_cast<int32_t>(s.width),
                                     static_cast<int32_t>(s.height)};
      v.subImage.imageArrayIndex = 0;
    }

    if (!s.logged_projection) {
      s.logged_projection = true;
      s.log("  openxr: submitting a projection layer, half angle %.1f deg",
            render_half_angle_ * 57.29578f);
    }
    XrCompositionLayerProjection& projection = b.projection;
    projection = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    projection.layerFlags = 0;
    projection.space = s.space;
    projection.viewCount = 2;
    projection.views = eye_views;

    const XrCompositionLayerBaseHeader** layers = b.layers;
    uint32_t layer_count = 0;
    layers[layer_count++] =
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);

    // THE INTERFACE PANELS, over the world. OpenXR draws a later layer over
    // an earlier one. Without the unpremultiplied flag the runtime reads the
    // color as premultiplied, which is what the panel holds. All the quads
    // read the one image, each through its own rect.
    XrCompositionLayerQuad* panels = b.panels;
    if (s.panel_acquired >= 0) {
      for (int i = 0; i < kPanelQuads; ++i) {
        const PanelQuad& q = s.panel_quads[i];
        if (!q.visible || !(q.width > 0.0f) || !(q.height > 0.0f)) continue;
        // A rect outside the image is an error for the whole frame.
        int32_t x = q.rect[0] < 0 ? 0 : q.rect[0];
        int32_t y = q.rect[1] < 0 ? 0 : q.rect[1];
        int32_t w = q.rect[2];
        int32_t h = q.rect[3];
        if (x + w > static_cast<int32_t>(s.width)) {
          w = static_cast<int32_t>(s.width) - x;
        }
        if (y + h > static_cast<int32_t>(s.height)) {
          h = static_cast<int32_t>(s.height) - y;
        }
        if (w <= 0 || h <= 0) continue;
        XrCompositionLayerQuad& quad = panels[i];
        quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        quad.space = s.space;
        quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        quad.subImage.swapchain = s.panel_swapchain;
        quad.subImage.imageRect.offset = {x, y};
        quad.subImage.imageRect.extent = {w, h};
        quad.subImage.imageArrayIndex = 0;
        quad.pose.position = {q.position[0], q.position[1], q.position[2]};
        quad.pose.orientation = {q.orientation[0], q.orientation[1],
                                 q.orientation[2], q.orientation[3]};
        quad.size = {q.width, q.height};
        layers[layer_count++] =
            reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
        ++s.panel_layers[i];
      }
      if (layer_count > 1) {
        ++s.panel_frames;
        if (!s.logged_panel) {
          s.logged_panel = true;
          s.log("  openxr: the interface goes out as %u quad layer(s) over the "
                "projection layer", layer_count - 1);
        }
      }
    }
    s.panel_acquired = -1;
    for (int i = 0; i < kPanelQuads; ++i) s.panel_quads[i].visible = false;

    // THE CARDS, over the world and the panels. A card shows only in the
    // frame that staged it. The subtitle card is the far one, so it goes
    // first, and the hint cards at the hands draw over it. The ring is open
    // only while the player holds its control, so it draws over all of them.
    XrCompositionLayerQuad* card_quads = b.card_quads;
    bool* with_card = b.with_card;
    const int card_order[kCards] = {kCardSubtitles, kCardHintPointing,
                                    kCardHintOther, kCardRing};
    for (int i = 0; i < kCards; ++i) {
      const int card = card_order[i];
      if (g_have_staged_card[card] && g_staged_card[card].visible &&
          s.SendCard(card, g_staged_card[card], &card_quads[card])) {
        layers[layer_count++] =
            reinterpret_cast<const XrCompositionLayerBaseHeader*>(
                &card_quads[card]);
        with_card[card] = true;
      }
      g_have_staged_card[card] = false;
    }

    // THE WALLS OF THE CUT SCENE WINDOW, over all the other layers, so the
    // room past the opening is black and calm.
    XrCompositionLayerQuad* walls = b.walls;
    if (window_staged && window.mask && s.SendMask(window, walls)) {
      for (int k = 0; k < 4; ++k) {
        layers[layer_count++] =
            reinterpret_cast<const XrCompositionLayerBaseHeader*>(&walls[k]);
      }
    }

    ++presented_;
    for (int eye = 0; eye < 2; ++eye) s.acquired[eye] = -1;

    b.layer_count = layer_count;
    b.world = true;
    QueryPerformanceCounter(&b.built);
    return true;
  }

  // One quad per eye. With two eyes each quad is visible to one eye only, so
  // the two pictures are seen as one screen with depth in it.
  XrCompositionLayerQuad* quads = b.quads;
  const XrCompositionLayerBaseHeader** layers = b.layers;
  uint32_t layer_count = 0;
  // The columns that each eye picture loses in the cut scene window. See
  // WindowLayer.
  int32_t crop = as_screen && eyes_ == 2 ? window.crop : 0;
  if (crop < 0 || crop >= static_cast<int32_t>(s.width) / 2) crop = 0;
  // The columns at each side of what is left, which the flat batches of the
  // game do not fill (window_math::Columns).
  int32_t edge = as_screen && eyes_ == 2 ? window.edge : 0;
  if (edge < 0 ||
      static_cast<int32_t>(s.width) - crop - 2 * edge <
          static_cast<int32_t>(s.width) / 2) {
    edge = 0;
  }
  const int32_t shown = static_cast<int32_t>(s.width) - crop - 2 * edge;

  const float aspect =
      static_cast<float>(s.height) / static_cast<float>(s.width);

  for (int eye = 0; eye < eyes_; ++eye) {
    if (s.acquired[eye] < 0) continue;
    XrCompositionLayerQuad& quad = quads[layer_count];
    quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    quad.layerFlags = 0;
    quad.space = s.space;
    quad.eyeVisibility = (eyes_ == 1)
                             ? XR_EYE_VISIBILITY_BOTH
                             : (eye == 0 ? XR_EYE_VISIBILITY_LEFT
                                         : XR_EYE_VISIBILITY_RIGHT);
    quad.subImage.swapchain = s.swapchain[eye];
    quad.subImage.imageRect.offset = {(eye == 0 ? crop : 0) + edge, 0};
    quad.subImage.imageRect.extent = {shown, static_cast<int32_t>(s.height)};
    quad.subImage.imageArrayIndex = 0;
    if (as_screen) {
      // The screen of the cut scene window, where the mod staged it.
      quad.pose.position = {window.position[0], window.position[1],
                            window.position[2]};
      quad.pose.orientation = {window.orientation[0], window.orientation[1],
                               window.orientation[2], window.orientation[3]};
      quad.size.width = window.width;
      quad.size.height = window.height;
    } else {
      quad.pose.orientation.w = 1.0f;
      quad.pose.position.z = -s.distance;
      quad.size.width = s.quad_width;
      quad.size.height = s.quad_width * aspect;
    }
    layers[layer_count] =
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(
            &quads[layer_count]);
    ++layer_count;
  }

  if (layer_count > 0) ++presented_;
  if (as_screen && layer_count > 0) {
    ++s.window_frames;
    if (!s.logged_window || crop != s.logged_window_crop ||
        edge != s.logged_window_edge) {
      s.logged_window = true;
      s.logged_window_crop = crop;
      s.logged_window_edge = edge;
      s.log("  openxr: the cut scene window goes out as %u quad layer(s), "
            "%.2f by %.2f m at %.2f %.2f %.2f m. Each eye shows %d of the %u "
            "columns of its picture: the left eye from column %d, the right "
            "eye from column %d", layer_count, window.width, window.height,
            window.position[0], window.position[1], window.position[2], shown,
            s.width, crop + edge, edge);
    }
  }
  for (int eye = 0; eye < 2; ++eye) s.acquired[eye] = -1;
  // A screen has no panel. The interface is in its picture.
  s.panel_acquired = -1;
  for (int i = 0; i < kPanelQuads; ++i) s.panel_quads[i].visible = false;
  // THE SUBTITLE CARD OF THE CUT SCENE WINDOW goes over the two eye quads,
  // as one quad for both eyes. A screen has no other card: the hints are in
  // its picture.
  XrCompositionLayerQuad& subtitle_quad = b.subtitle_quad;
  bool& with_subtitles = b.with_subtitles;
  if (as_screen && layer_count > 0 && g_have_staged_card[kCardSubtitles] &&
      g_staged_card[kCardSubtitles].visible &&
      s.SendCard(kCardSubtitles, g_staged_card[kCardSubtitles],
                 &subtitle_quad)) {
    layers[layer_count++] =
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&subtitle_quad);
    with_subtitles = true;
    if (!s.logged_window_card) {
      s.logged_window_card = true;
      s.log("  openxr: the subtitle card goes out as one quad layer over the "
            "cut scene window, %.3f by %.3f m",
            g_staged_card[kCardSubtitles].width,
            g_staged_card[kCardSubtitles].height);
    }
  }
  for (int i = 0; i < kCards; ++i) g_have_staged_card[i] = false;

  b.layer_count = layer_count;
  QueryPerformanceCounter(&b.built);
  return true;
}

// Gives the layers of BuildFrame to the runtime. It reads only Impl::built
// and it writes no line of the log, so the second thread can call it.
void XrScreen::Impl::SendFrame() {
  Built& b = built;
  XrFrameEndInfo end = {XR_TYPE_FRAME_END_INFO};
  end.displayTime = b.display_time;
  end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  end.layerCount = b.layer_count;
  end.layers = b.layer_count ? b.layers : nullptr;
  QueryPerformanceCounter(&b.end_start);
  b.result = xrEndFrame(session, &end);
  QueryPerformanceCounter(&b.end_done);
}

// After SendFrame, on the frame thread: the result of xrEndFrame, and the
// time of BuildFrame and of xrEndFrame for the report.
void XrScreen::FinishFrame() {
  Impl& s = *impl_;
  const Impl::Built& b = s.built;
  if (!s.Check(b.result, "xrEndFrame")) {
    if (b.world) {
      // The runtime refused a frame. A refused frame shows nothing, so a
      // card of that frame must not go in the next one. The newest kind goes
      // first: the ring, then the subtitle card. A frame that fails with
      // neither takes the hint cards away.
      if (b.with_card[kCardRing]) {
        g_card_failed[kCardRing] = true;
        s.log("  openxr: the runtime refused a frame with the ring, so the "
              "ring does not show. Its items still act");
      } else if (b.with_card[kCardSubtitles]) {
        g_card_failed[kCardSubtitles] = true;
        s.log("  openxr: the runtime refused a frame with the subtitle card, "
              "so the subtitles stay in the eye pictures");
      } else if (b.with_card[kCardHintPointing] ||
                 b.with_card[kCardHintOther]) {
        g_card_failed[kCardHintPointing] = true;
        g_card_failed[kCardHintOther] = true;
        s.log("  openxr: the runtime refused a frame with a hint card, so the "
              "hints stay in the eye pictures");
      }
    } else if (b.with_subtitles) {
      // A refused frame shows nothing, so the card must not go in the next
      // one. The subtitles then stay in the picture of the window.
      g_card_failed[kCardSubtitles] = true;
      s.log("  openxr: the runtime refused a frame of the cut scene window "
            "with the subtitle card, so the subtitles stay in the eye "
            "pictures");
    }
  }
  s.end_ticks +=
      static_cast<uint64_t>(b.built.QuadPart - b.before.QuadPart) +
      static_cast<uint64_t>(b.end_done.QuadPart - b.end_start.QuadPart);
  ++s.ends;
  if (b.layer_count > 0 && b.waited_at.QuadPart != 0) {
    const uint64_t age =
        static_cast<uint64_t>(b.end_done.QuadPart - b.waited_at.QuadPart);
    s.age_ticks += age;
    if (age > s.age_worst) s.age_worst = age;
    ++s.age_frames;
  }
}

// A LATE FRAME. The layers come first, so the cards go to the runtime while
// the GPU works on the eye pictures. Then come the wait for the GPU, the
// copies and xrEndFrame, on the second thread or on this thread.
void XrScreen::EndFrameLate(const LateFrame& frame, bool on_thread) {
  Impl& s = *impl_;
  if (!s.frame_open) return;
  s.late = frame;
  // BuildFrame makes a layer only for a picture that the frame has.
  for (int eye = 0; eye < 2; ++eye) {
    if (!should_render_ || eye >= eyes_ ||
        s.swapchain[eye] == XR_NULL_HANDLE) {
      s.late.eye_texture[eye] = nullptr;
    }
    if (s.late.eye_texture[eye] != nullptr) s.acquired[eye] = 0;
  }
  if (!should_render_ || s.panel_swapchain == XR_NULL_HANDLE) {
    s.late.panel_texture = nullptr;
  }
  s.panel_acquired = s.late.panel_texture != nullptr ? 0 : -1;
  if (!BuildFrame()) return;
  ++s.late_frames;
  if (on_thread && s.StartSendThread()) {
    ++s.late_thread_frames;
    s.send_busy = true;
    ResetEvent(s.send_done);
    SetEvent(s.send_go);
    return;
  }
  s.RunLate();
  s.SendFrame();
  FinishLate();
  FinishFrame();
}

void XrScreen::Join() {
  Impl& s = *impl_;
  if (!s.send_busy || GetCurrentThreadId() == s.send_thread_id) return;
  LARGE_INTEGER start, done;
  QueryPerformanceCounter(&start);
  WaitForSingleObject(s.send_done, INFINITE);
  QueryPerformanceCounter(&done);
  s.send_busy = false;
  const uint64_t waited =
      static_cast<uint64_t>(done.QuadPart - start.QuadPart);
  s.join_ticks += waited;
  if (waited > s.join_worst) s.join_worst = waited;
  FinishLate();
  FinishFrame();
}

bool XrScreen::sending() const { return impl_->send_busy; }

bool XrScreen::LastDisplayTime(int64_t* time, int64_t* period) const {
  const Impl& s = *impl_;
  if (s.frame_state.predictedDisplayTime <= 0) return false;
  *time = s.frame_state.predictedDisplayTime;
  *period = s.frame_state.predictedDisplayPeriod;
  return true;
}

uint64_t XrScreen::LateWaitTicks() const {
  return impl_->send_busy ? 0 : impl_->late_gpu;
}

// After RunLate and SendFrame, on the frame thread: the results of the
// copies, and the times for the report.
void XrScreen::FinishLate() {
  Impl& s = *impl_;
  const char* const what[3] = {"the copy of the left eye picture",
                               "the copy of the right eye picture",
                               "the copy of the panel"};
  for (int i = 0; i < 3; ++i) s.Check(s.late_result[i], what[i]);
  s.late_gpu_ticks += s.late_gpu;
  s.late_copy_ticks += s.late_copy;
  s.late_end_ticks += static_cast<uint64_t>(s.built.end_done.QuadPart -
                                            s.built.end_start.QuadPart);
}

// One texture to the next image of its swapchain: acquire, wait, one copy on
// the GPU, release. As SetEyeTexture, with no line of the log.
XrResult XrScreen::Impl::CopyLate(XrSwapchain chain,
                                  ID3D11Texture2D** chain_images,
                                  void* texture) {
  uint32_t image = 0;
  XrSwapchainImageAcquireInfo acquire = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  XrResult result = xrAcquireSwapchainImage(chain, &acquire, &image);
  if (XR_FAILED(result)) return result;
  XrSwapchainImageWaitInfo image_wait = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  image_wait.timeout = XR_INFINITE_DURATION;
  result = xrWaitSwapchainImage(chain, &image_wait);
  if (XR_FAILED(result)) return result;
  context->CopyResource(chain_images[image],
                        static_cast<ID3D11Texture2D*>(texture));
  XrSwapchainImageReleaseInfo release = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  return xrReleaseSwapchainImage(chain, &release);
}

// The wait for the GPU, and then the copies. The other device must not read
// a shared texture before the GPU has written it.
void XrScreen::Impl::RunLate() {
  LARGE_INTEGER start, waited, copied;
  QueryPerformanceCounter(&start);
  if (late.wait != nullptr) late.wait();
  QueryPerformanceCounter(&waited);
  for (int eye = 0; eye < 2; ++eye) {
    late_result[eye] =
        late.eye_texture[eye] != nullptr
            ? CopyLate(swapchain[eye], images[eye], late.eye_texture[eye])
            : XR_SUCCESS;
  }
  late_result[2] =
      late.panel_texture != nullptr
          ? CopyLate(panel_swapchain, panel_images, late.panel_texture)
          : XR_SUCCESS;
  QueryPerformanceCounter(&copied);
  late_gpu = static_cast<uint64_t>(waited.QuadPart - start.QuadPart);
  late_copy = static_cast<uint64_t>(copied.QuadPart - waited.QuadPart);
}

// THE SECOND THREAD. It sends one frame for each signal of send_go, and it
// says with send_done that the runtime has the frame.
DWORD WINAPI XrScreen::Impl::SendThreadMain(void* param) {
  Impl* s = static_cast<Impl*>(param);
  for (;;) {
    WaitForSingleObject(s->send_go, INFINITE);
    if (s->send_quit) break;
    s->RunLate();
    s->SendFrame();
    SetEvent(s->send_done);
  }
  return 0;
}

// Starts the second thread at the first frame that needs it. False when it
// cannot start: the frame thread then sends the frame.
bool XrScreen::Impl::StartSendThread() {
  if (send_thread != nullptr) return true;
  if (send_failed) return false;
  send_quit = false;
  send_go = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  send_done = CreateEventA(nullptr, TRUE, TRUE, nullptr);
  if (send_go != nullptr && send_done != nullptr) {
    send_thread =
        CreateThread(nullptr, 0, &SendThreadMain, this, 0, &send_thread_id);
  }
  if (send_thread == nullptr) {
    send_failed = true;
    if (send_go != nullptr) CloseHandle(send_go);
    if (send_done != nullptr) CloseHandle(send_done);
    send_go = nullptr;
    send_done = nullptr;
    log("  openxr: the second thread did not start (error %lu), so the frame "
        "thread sends each frame", GetLastError());
    return false;
  }
  // The thread must not wait behind the other work of the PC: a late frame
  // is a frame that the headset does not show.
  SetThreadPriority(send_thread, THREAD_PRIORITY_ABOVE_NORMAL);
  log("  openxr: a second thread gives the frames to the runtime");
  return true;
}

// Ends the second thread. The caller made sure that it has no frame. The
// wait has a limit: at the end of the process a thread can not end.
void XrScreen::Impl::StopSendThread() {
  if (send_thread == nullptr) return;
  send_quit = true;
  SetEvent(send_go);
  WaitForSingleObject(send_thread, 500);
  CloseHandle(send_thread);
  CloseHandle(send_go);
  CloseHandle(send_done);
  send_thread = nullptr;
  send_thread_id = 0;
  send_go = nullptr;
  send_done = nullptr;
}

bool XrScreen::SubmitTexture(void* texture) {
  if (!BeginFrame()) return false;
  // Both eyes get the same picture, for the same reason SubmitBgra does: an
  // eye with no image submitted gets no layer, and that eye would go black.
  for (int eye = 0; eye < eyes_; ++eye) SetEyeTexture(eye, texture);
  EndFrame();
  return true;
}

bool XrScreen::SubmitBgra(const void* pixels, int pitch) {
  if (!BeginFrame()) return false;
  // Give both eyes the same picture. With a stereo session configured, an eye
  // with no image submitted gets no layer at all, so a mono frame would only
  // appear on the left and the right eye would go black. That is what happens
  // in menus and loading screens, which never render the scene twice.
  for (int eye = 0; eye < eyes_; ++eye) SetEyeImage(eye, pixels, pitch);
  EndFrame();
  return true;
}

namespace {

// How long the end of a session waits for the STOPPING state. The game must
// not hang at its exit.
const DWORD kStopWaitMs = 500;

double StopMs(const LARGE_INTEGER& start) {
  LARGE_INTEGER hz, now;
  QueryPerformanceFrequency(&hz);
  QueryPerformanceCounter(&now);
  return hz.QuadPart > 0
             ? (now.QuadPart - start.QuadPart) * 1000.0 / hz.QuadPart
             : 0.0;
}

}  // namespace

void XrScreen::Stop() {
  Impl& s = *impl_;
  running_ = false;
  // The second thread gives its frame to the runtime, and then it ends.
  Join();
  s.StopSendThread();
  // The lines of the end are for a session that was made. A start that
  // failed comes here too, many times, and it has nothing to say.
  const bool had_session = s.session != XR_NULL_HANDLE;
  LARGE_INTEGER part;
  QueryPerformanceCounter(&part);

  // xrEndSession is valid only in the STOPPING state. So the mod asks the
  // runtime for the exit of the session, and reads the events for a short
  // time until that state comes. If it does not come, the session is
  // destroyed as it is: xrDestroySession is valid in each state.
  if (s.session_running) {
    const XrResult asked = xrRequestExitSession(s.session);
    bool stopping = s.state == XR_SESSION_STATE_STOPPING;
    const DWORD start = GetTickCount();
    while (XR_SUCCEEDED(asked) && !stopping &&
           GetTickCount() - start < kStopWaitMs) {
      for (;;) {
        XrEventDataBuffer event = {XR_TYPE_EVENT_DATA_BUFFER};
        if (xrPollEvent(s.instance, &event) != XR_SUCCESS) break;
        if (event.type != XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) continue;
        s.state =
            reinterpret_cast<const XrEventDataSessionStateChanged*>(&event)
                ->state;
        if (s.state == XR_SESSION_STATE_STOPPING) stopping = true;
      }
      if (!stopping) Sleep(5);
    }
    const XrResult ended = xrEndSession(s.session);
    s.session_running = false;
    s.log("  openxr: the end of the session. xrRequestExitSession gave %d, "
          "the state STOPPING %s, xrEndSession gave %d (0 is success), "
          "%.1f ms",
          static_cast<int>(asked),
          stopping ? "came" : "did not come in the time limit",
          static_cast<int>(ended), StopMs(part));
    QueryPerformanceCounter(&part);
  }
  for (int eye = 0; eye < 2; ++eye) {
    if (s.swapchain[eye] != XR_NULL_HANDLE) {
      xrDestroySwapchain(s.swapchain[eye]);
      s.swapchain[eye] = XR_NULL_HANDLE;
    }
  }
  if (s.panel_swapchain != XR_NULL_HANDLE) {
    xrDestroySwapchain(s.panel_swapchain);
    s.panel_swapchain = XR_NULL_HANDLE;
  }
  delete[] s.panel_images;
  s.panel_images = nullptr;
  s.panel_image_count = 0;
  s.panel_acquired = -1;
  for (int i = 0; i < kCards; ++i) {
    s.DestroyCard(i);
    g_have_staged_card[i] = false;
  }
  s.DestroyMask();
  g_have_staged_window = false;
  // The action spaces go before the session.
  s.DestroyInput();
  if (s.view_space != XR_NULL_HANDLE) {
    xrDestroySpace(s.view_space);
    s.view_space = XR_NULL_HANDLE;
  }
  if (s.space != XR_NULL_HANDLE) {
    xrDestroySpace(s.space);
    s.space = XR_NULL_HANDLE;
  }
  if (s.session != XR_NULL_HANDLE) {
    xrDestroySession(s.session);
    s.session = XR_NULL_HANDLE;
  }
  // The action set goes before the instance.
  s.DestroyActionSet();
  if (had_session) {
    s.log("  openxr: the swapchains, the spaces and the session are "
          "destroyed, %.1f ms", StopMs(part));
    QueryPerformanceCounter(&part);
  }

  for (int eye = 0; eye < 2; ++eye) {
    delete[] s.images[eye];
    s.images[eye] = nullptr;
    s.image_count[eye] = 0;
  }
  Release(s.staging);
  Release(s.context);
  Release(s.device);
  if (had_session) {
    s.log("  openxr: the Direct3D 11 objects of the session are released, "
          "%.1f ms", StopMs(part));
    QueryPerformanceCounter(&part);
  }

  if (s.instance != XR_NULL_HANDLE) {
    xrDestroyInstance(s.instance);
    s.instance = XR_NULL_HANDLE;
    if (had_session) {
      s.log("  openxr: the instance is destroyed, %.1f ms", StopMs(part));
    }
  }
}

}  // namespace bwvr
