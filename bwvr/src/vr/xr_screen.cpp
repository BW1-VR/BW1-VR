#include "xr_screen.h"
#include "bridge/client.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#include <string>
#include <vector>

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "../common/version.h"
#include "refresh_rate_math.h"

namespace bwvr {
namespace {

void NoLog(const char*, ...) {}
bool g_bridge_enabled = false;

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

// THE VULKAN OF A SESSION WITH VULKAN (XrScreen::UseVulkan). The runtime
// makes the instance and the device at the first Start with Vulkan. They
// stay until the end of the process: the renderer of the mod draws with
// them, and a later Start uses them again.
#define BWVR_XR_VK_INSTANCE(X)                \
  X(vkGetDeviceProcAddr)                      \
  X(vkGetPhysicalDeviceProperties)            \
  X(vkGetPhysicalDeviceProperties2)           \
  X(vkGetPhysicalDeviceFeatures2)             \
  X(vkGetPhysicalDeviceQueueFamilyProperties) \
  X(vkGetPhysicalDeviceMemoryProperties)      \
  X(vkEnumerateDeviceExtensionProperties)

#define BWVR_XR_VK_DEVICE(X)              \
  X(vkGetDeviceQueue)                     \
  X(vkDeviceWaitIdle)                     \
  X(vkCreateCommandPool)                  \
  X(vkDestroyCommandPool)                 \
  X(vkAllocateCommandBuffers)             \
  X(vkResetCommandBuffer)                 \
  X(vkBeginCommandBuffer)                 \
  X(vkEndCommandBuffer)                   \
  X(vkCmdPipelineBarrier)                 \
  X(vkCmdCopyImage)                       \
  X(vkQueueSubmit)                        \
  X(vkCreateFence)                        \
  X(vkDestroyFence)                       \
  X(vkWaitForFences)                      \
  X(vkResetFences)                        \
  X(vkCreateImage)                        \
  X(vkDestroyImage)                       \
  X(vkGetImageMemoryRequirements)         \
  X(vkAllocateMemory)                     \
  X(vkFreeMemory)                         \
  X(vkBindImageMemory)                    \
  X(vkGetMemoryWin32HandlePropertiesKHR)  \
  X(vkCreateBuffer)                       \
  X(vkDestroyBuffer)                      \
  X(vkGetBufferMemoryRequirements)        \
  X(vkBindBufferMemory)                   \
  X(vkMapMemory)                          \
  X(vkCmdCopyBufferToImage)               \
  X(vkCmdCopyImageToBuffer)

struct VulkanFunctions {
  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
#define BWVR_XR_VK_DECLARE(name) PFN_##name name;
  BWVR_XR_VK_INSTANCE(BWVR_XR_VK_DECLARE)
  BWVR_XR_VK_DEVICE(BWVR_XR_VK_DECLARE)
#undef BWVR_XR_VK_DECLARE
};
VulkanFunctions vkf = {};

struct VulkanState {
  bool made;
  bool shading_rate;  // VK_KHR_fragment_shading_rate is on the device
  // VK_KHR_surface and VK_KHR_win32_surface are on the instance, and
  // VK_KHR_swapchain on the device (XrVulkan::window_present).
  bool window_present;
  HMODULE library;
  VkInstance instance;
  VkPhysicalDevice physical;
  VkDevice device;
  uint32_t family;
  VkQueue queue;
  float max_anisotropy;
  LUID luid;
  VkPhysicalDeviceMemoryProperties memory;
  char gpu[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
};
VulkanState g_vk = {};

// The lock of the queue (XrScreen::LockQueue).
SRWLOCK g_queue_lock = SRWLOCK_INIT;

// Holds the lock of the queue while it lives, in a session with Vulkan.
class QueueGuard {
 public:
  explicit QueueGuard(bool on) : on_(on) {
    if (on_) AcquireSRWLockExclusive(&g_queue_lock);
  }
  ~QueueGuard() {
    if (on_) ReleaseSRWLockExclusive(&g_queue_lock);
  }
  QueueGuard(const QueueGuard&) = delete;
  QueueGuard& operator=(const QueueGuard&) = delete;

 private:
  bool on_;
};

// The formats of a swapchain, in the order of choice. BGRA first, because
// that is what DirectDraw hands us and it avoids a per pixel swizzle. sRGB
// variants first, because the game's output is already gamma encoded and
// the compositor should treat it that way. A RELAY (Impl::Relay) has the
// UNORM format of the same order of bytes, so a copy of its bytes into the
// swapchain image changes no pixel.
struct FormatChoice {
  DXGI_FORMAT dxgi;
  VkFormat vk;
  DXGI_FORMAT dxgi_unorm;
  VkFormat vk_unorm;
};
const FormatChoice kFormats[] = {
    {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, VK_FORMAT_B8G8R8A8_SRGB,
     DXGI_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM},
    {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, VK_FORMAT_R8G8B8A8_SRGB,
     DXGI_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM},
    {DXGI_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM,
     DXGI_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM},
    {DXGI_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
     DXGI_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM},
};
const int kFormatCount = 4;

}  // namespace

struct XrScreen::Impl {
  LogFn log = &NoLog;

  XrInstance instance = XR_NULL_HANDLE;
  XrSystemId system = XR_NULL_SYSTEM_ID;
  // From StartSystem, for LogSystem.
  char system_name[XR_MAX_SYSTEM_NAME_SIZE] = {0};
  uint32_t max_w = 0;
  uint32_t max_h = 0;
  XrSession session = XR_NULL_HANDLE;
  XrSpace space = XR_NULL_HANDLE;
  XrSwapchain swapchain[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
  XrSpace view_space = XR_NULL_HANDLE;
  XrSessionState state = XR_SESSION_STATE_UNKNOWN;
  bool session_running = false;
  // StartSession came to its end one time. Stop does not clear it.
  bool session_started = false;

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
  int format_index = -1;  // in kFormats

  // A SESSION WITH VULKAN (XrScreen::UseVulkan). The swapchains have Vulkan
  // images. Each image has a RELAY: a Direct3D 11 texture that Vulkan shares
  // through an NT handle. Each write of Direct3D 11 into a swapchain image
  // goes into its relay, and the release of the image (Release) copies the
  // relay into the image with Vulkan. So the textures of the bridge take the
  // way of a session with Direct3D 11. The pixels of the CPU go into the
  // image with a copy of Vulkan (THE PIXELS OF THE CPU), or with
  // xr_vulkan_upload=0 into the relay too.
  bool vulkan = false;
  bool vk_failed = false;  // XrScreen::VulkanFailed
  // XrScreen::UseD3D11. false: no Direct3D 11 device and no relays, and the
  // pixels of the CPU always go with Vulkan.
  bool d3d11 = true;
  struct Relay {
    ID3D11Texture2D* texture;
    HANDLE handle;
    VkImage image;
    VkDeviceMemory memory;
  };
  struct VkChain {
    XrSwapchain chain;
    uint32_t count;
    uint32_t width;
    uint32_t height;
    uint32_t acquired;
    VkImage* images;
    Relay* relays;
  };
  static const int kVkChains = 16;
  VkChain vk_chains[kVkChains] = {};
  static const int kVkSlots = 8;
  VkCommandPool vk_pool = VK_NULL_HANDLE;
  VkCommandBuffer vk_cb[kVkSlots] = {};
  VkFence vk_fence[kVkSlots] = {};
  int vk_slot = 0;
  ID3D11Query* d3d11_done = nullptr;
  // THE PIXELS OF THE CPU (XrScreen::SetVulkanUpload). Each slot of the
  // copies has a buffer that the CPU can write. The fence of the slot says
  // when the GPU has read it.
  bool vk_upload = true;
  struct Stage {
    VkBuffer buffer;
    VkDeviceMemory memory;
    uint8_t* mapped;
    VkDeviceSize size;
  };
  Stage vk_stage[kVkSlots] = {};
  // XrScreen::CaptureSwapchainNext: the eye copies that are still to be
  // read back, and the number of their files.
  int capture_left = 0;
  uint64_t capture_name = 0;
  void SaveChainPicture(VkChain* c, uint32_t index, int eye);
  // For the report.
  uint64_t vk_pushes = 0;
  uint64_t vk_copies = 0;
  uint64_t vk_push_ticks = 0;
  uint64_t vk_uploads = 0;
  uint64_t vk_upload_ticks = 0;
  bool vk_said_push_failed = false;
  bool vk_said_upload_failed = false;

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
  // EYE TRACKING (XR_EXT_eye_gaze_interaction).
  bool gaze_wanted = false;
  bool gaze_extension = false;  // the instance has the extension
  bool gaze_supported = false;  // and the headset has an eye tracker
  XrAction gaze_action = XR_NULL_HANDLE;
  XrSpace gaze_space = XR_NULL_HANDLE;
  bool gaze_valid = false;
  float gaze_direction[3] = {0.0f, 0.0f, -1.0f};
  // THE REFRESH RATE OF THE HEADSET (XR_FB_display_refresh_rate,
  // SetRefreshRate). The instance has the extension when the runtime lists
  // it, so the log has the rates with each value of refresh_rate_hz.
  bool refresh_extension = false;
  int refresh_ask = refresh_rate_math::kAskNone;
  bool refresh_logged = false;  // the rates of the headset are in the log
  PFN_xrEnumerateDisplayRefreshRatesFB enumerate_rates = nullptr;
  PFN_xrGetDisplayRefreshRateFB get_rate = nullptr;
  PFN_xrRequestDisplayRefreshRateFB request_rate = nullptr;
  void ApplyRefreshRate();  // after xrBeginSession
  float RateNow();          // 0 when the runtime does not say
  // The display period and the time in xrWaitFrame of the frames of a
  // report window, and of the first frames of the session (only the frame
  // thread writes them).
  refresh_rate_math::Window display_window;
  static const int kFirstFrames = 6;
  int64_t first_period[kFirstFrames] = {};
  int64_t first_time[kFirstFrames] = {};
  int first_count = 0;
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
    LARGE_INTEGER begun_at;   // the end of BeginFrame of this frame
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
  // THE LONGEST CALLS OF THE RUNTIME in a report window. A frame of 100 ms
  // in a headset can wait inside the runtime: in xrWaitFrame, in
  // xrBeginFrame, in xrEndFrame or at a swapchain image. The report of
  // stereo.cpp puts xrEndFrame and the copies in "everything else", so these
  // numbers say which call held the frame. Only the frame thread writes them.
  enum RuntimeCall { kCallWait, kCallBegin, kCallEnd, kCallCopies, kCalls };
  uint64_t call_worst[kCalls] = {};
  uint64_t call_slow[kCalls] = {};  // calls of 20 ms or more
  void CountCall(int call, uint64_t ticks) {
    if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
    if (ticks > call_worst[call]) call_worst[call] = ticks;
    if (ticks * 50 >= static_cast<uint64_t>(frequency.QuadPart)) {
      ++call_slow[call];
    }
  }

  // THE HOLD (xr_hold_ms, XrScreen::SetHoldMs). When the game gives the
  // runtime no frame for hold_ms (a load, a long game turn), the second
  // thread sends the layers of the last frame again in frames of its own.
  // They carry the poses that the pictures were rendered with, so the
  // runtime turns them with the head, and the headset shows no black. A
  // frame that the frame thread has open for three times as long is ended
  // with those layers too (the frame is "taken"). BuildFrame then begins a
  // new frame of the runtime for the pictures of the frame thread.
  //
  // frame_state_now says who has the frame of the runtime. The frame thread
  // changes it under hold_lock (BeginFrame, BuildFrame), SendFrame changes
  // kFrameSending to kFrameNone with no lock, and the second thread changes
  // it under hold_lock, which it only tries to take, so it never waits for
  // the frame thread.
  enum { kFrameNone = 0, kFrameGame, kFrameSending, kFrameTaken };
  volatile LONG frame_state_now = kFrameNone;
  SRWLOCK hold_lock = SRWLOCK_INIT;
  volatile LONG hold_ms = 0;
  volatile LONG frame_wants = 0;   // the frame thread waits for hold_lock
  volatile LONG hold_live = 0;     // the session runs
  volatile LONGLONG last_end = 0;  // the end of the last frame of the game
  volatile LONGLONG game_open_at = 0;  // the start of its open frame
  // For the report (the second thread writes them).
  uint64_t hold_frames = 0;
  uint64_t hold_taken = 0;
  uint64_t hold_runs = 0;
  uint64_t hold_failed = 0;
  uint64_t hold_worst = 0;  // ticks: the longest time with no frame of the game
  // The predicted display time of the frame of the last BeginFrame (the
  // frame thread writes it). XrScreen::DisplaySeconds.
  XrTime display_seconds_ns = 0;
  // A long time with no frame of the game, and no frame of the hold: why.
  uint64_t hold_lock_busy = 0;  // the frame thread had hold_lock
  uint64_t hold_sending = 0;    // the frame of the game was in SendFrame
  LARGE_INTEGER begun_at = {};  // the end of BeginFrame of the open frame
  int slow_lines = 0;           // the lines of slow frames in the log
  // On the frame thread: hold_lock for a scope, with frame_wants, so the
  // second thread ends a run of the hold.
  class HoldGuard {
   public:
    explicit HoldGuard(Impl& s) : s_(s) {
      InterlockedExchange(&s_.frame_wants, 1);
      AcquireSRWLockExclusive(&s_.hold_lock);
    }
    ~HoldGuard() {
      ReleaseSRWLockExclusive(&s_.hold_lock);
      InterlockedExchange(&s_.frame_wants, 0);
    }
    HoldGuard(const HoldGuard&) = delete;
    HoldGuard& operator=(const HoldGuard&) = delete;

   private:
    Impl& s_;
  };

  static DWORD WINAPI SendThreadMain(void* param);
  void HoldFrames();
  bool HoldOne(LONG state);
  bool StartSendThread();
  void StopSendThread();
  XrResult CopyLate(XrSwapchain chain, ID3D11Texture2D** chain_images,
                    void* texture);
  void RunLate();
  void SendFrame();

  bool MakeD3D11(const LUID& luid);    // Start: the device on that adapter
  bool StartVulkan();                  // Start, with Vulkan
  void StopVulkan();                   // Stop, before the Direct3D 11 device
  int64_t ChainFormat() const;         // for xrCreateSwapchain
  XrSwapchainUsageFlags ChainUsage() const;
  // The images of a new swapchain as Direct3D 11 textures: its own images,
  // or with Vulkan its relays. Null when they cannot be made.
  ID3D11Texture2D** ChainImages(XrSwapchain chain, uint32_t chain_width,
                                uint32_t chain_height, uint32_t* count);
  void DestroyChain(XrSwapchain* chain);
  VkChain* FindChain(XrSwapchain chain);
  bool MakeRelay(uint32_t relay_width, uint32_t relay_height, Relay* relay);
  void FreeRelay(Relay* relay);
  // The acquire and the release of a swapchain image. With Vulkan the
  // release first copies the relay of the image into it (push).
  XrResult AcquireImage(XrSwapchain chain, uint32_t* image);
  XrResult ReleaseImage(XrSwapchain chain, bool push = true);
  bool Push(VkChain& chain, uint32_t image);
  VkCommandBuffer BeginCopy();
  bool EndCopy(VkCommandBuffer cb);
  XrResult CopyVulkan(XrSwapchain chain, uint64_t image, uint32_t layer);
  // True when a write of the CPU into this swapchain goes with Vulkan
  // (THE PIXELS OF THE CPU).
  bool UploadsWithVulkan(XrSwapchain chain) {
    return vulkan && (vk_upload || device == nullptr) &&
           FindChain(chain) != nullptr;
  }
  // The rect x, y, w, h of `pixels` (rows of `pitch` bytes, 4 bytes for each
  // pixel) into the acquired image `image` of the swapchain, at the same
  // place. swap_red_blue swaps bytes 0 and 2 of each pixel.
  bool UploadVulkan(XrSwapchain chain, uint32_t image, const uint8_t* pixels,
                    uint32_t pitch, int32_t x, int32_t y, int32_t w,
                    int32_t h, bool swap_red_blue);
  void FreeStages();

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

  // EYE TRACKING: a pose action with the profile of the eye tracker. A
  // failure only leaves the fovea straight ahead.
  if (gaze_supported) {
    XrActionCreateInfo info = {XR_TYPE_ACTION_CREATE_INFO};
    strcpy_s(info.actionName, "eye_gaze");
    strcpy_s(info.localizedActionName, "Eye gaze");
    info.actionType = XR_ACTION_TYPE_POSE_INPUT;
    XrPath profile_path = XR_NULL_PATH, gaze_path = XR_NULL_PATH;
    bool ok =
        Check(xrCreateAction(action_set, &info, &gaze_action),
              "xrCreateAction(eye_gaze)") &&
        Check(xrStringToPath(instance,
                             "/interaction_profiles/ext/eye_gaze_interaction",
                             &profile_path),
              "xrStringToPath(eye gaze profile)") &&
        Check(xrStringToPath(instance, "/user/eyes_ext/input/gaze_ext/pose",
                             &gaze_path),
              "xrStringToPath(eye gaze)");
    if (ok) {
      XrActionSuggestedBinding binding = {gaze_action, gaze_path};
      XrInteractionProfileSuggestedBinding suggested = {
          XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
      suggested.interactionProfile = profile_path;
      suggested.suggestedBindings = &binding;
      suggested.countSuggestedBindings = 1;
      ok = Check(xrSuggestInteractionProfileBindings(instance, &suggested),
                 "xrSuggestInteractionProfileBindings(eye gaze)");
    }
    if (!ok) gaze_supported = false;
  }

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
  if (gaze_supported) {
    XrActionSpaceCreateInfo space_info = {XR_TYPE_ACTION_SPACE_CREATE_INFO};
    space_info.action = gaze_action;
    space_info.poseInActionSpace.orientation.w = 1.0f;
    if (!Check(xrCreateActionSpace(session, &space_info, &gaze_space),
               "xrCreateActionSpace(eye gaze)")) {
      gaze_supported = false;
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

  // EYE TRACKING: the gaze in the space of the head, at the display time.
  gaze_valid = false;
  if (gaze_supported && gaze_space != XR_NULL_HANDLE &&
      view_space != XR_NULL_HANDLE) {
    XrActionStateGetInfo gaze_get = {XR_TYPE_ACTION_STATE_GET_INFO};
    gaze_get.action = gaze_action;
    XrActionStatePose gaze_state = {XR_TYPE_ACTION_STATE_POSE};
    XrSpaceLocation gaze_location = {XR_TYPE_SPACE_LOCATION};
    if (XR_SUCCEEDED(xrGetActionStatePose(session, &gaze_get, &gaze_state)) &&
        gaze_state.isActive &&
        XR_SUCCEEDED(
            xrLocateSpace(gaze_space, view_space, when, &gaze_location))) {
      const XrSpaceLocationFlags need =
          XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
          XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
      if ((gaze_location.locationFlags & need) == need) {
        // The forward of the pose (0, 0, -1) turned by its orientation.
        const XrQuaternionf& q = gaze_location.pose.orientation;
        gaze_direction[0] = -2.0f * (q.x * q.z + q.w * q.y);
        gaze_direction[1] = -2.0f * (q.y * q.z - q.w * q.x);
        gaze_direction[2] = -(1.0f - 2.0f * (q.x * q.x + q.y * q.y));
        gaze_valid = true;
      }
    }
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
  if (gaze_space != XR_NULL_HANDLE) xrDestroySpace(gaze_space);
  gaze_space = XR_NULL_HANDLE;
  gaze_action = XR_NULL_HANDLE;
  gaze_valid = false;
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

void XrScreen::ConfigureBridge(bool enabled) { g_bridge_enabled = enabled; }

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
  if (remote_) return remote_->packet->state.profiles[index ? 1 : 0];
  return impl_->profile[index ? 1 : 0];
}

bool XrScreen::InputFocused() const {
  if (remote_) return remote_->healthy() && remote_->packet->state.focus != 0;
  return impl_->input_ready && impl_->focused;
}

void XrScreen::Haptic(int index, float amplitude, float seconds) {
  if (remote_) {
    remote_->packet->index = index; remote_->packet->amplitude = amplitude; remote_->packet->duration = seconds;
    remote_->Call(bridge::Haptic); return;
  }
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
  if (remote_) remote_->SetLog(log ? log : NoLog);
  impl_->log = (log != nullptr) ? log : &NoLog;
}

void XrScreen::SetPlacement(float distance_meters, float width_meters) {
  impl_->distance = distance_meters;
  impl_->quad_width = width_meters;
}

bool XrScreen::Start(uint32_t width, uint32_t height, int eyes, LogFn log) {
  if (!StartInstance(log)) return false;
  if (StartSystem() != 1) {
    Stop();
    return false;
  }
  LogSystem();
  if (!StartSession(width, height, eyes)) {
    Stop();
    return false;
  }
  return true;
}

bool XrScreen::has_instance() const {
  if (remote_) return remote_->healthy() && remote_->instance_ready;
  return impl_->instance != XR_NULL_HANDLE;
}

bool XrScreen::StartInstance(LogFn log) {
  // The bridge gives the frames of Direct3D 11 to the host, so a session
  // with Vulkan does not use it.
  if ((g_bridge_enabled && !impl_->vulkan) || remote_) {
    impl_->log = log ? log : NoLog;
    if (!remote_) {
      remote_ = new RemoteScreen(log ? log : NoLog);
      if (!remote_->Launch()) return false;
    }
    remote_->instance_ready = remote_->Call(bridge::Instance) && remote_->packet->result != 0;
    return remote_->instance_ready;
  }
  Impl& s = *impl_;
  s.log = (log != nullptr) ? log : &NoLog;
  if (s.instance != XR_NULL_HANDLE) return true;

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
          if (strcmp(list[i].extensionName,
                     XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME) == 0) {
            s.gaze_extension = s.gaze_wanted;
          }
          if (strcmp(list[i].extensionName,
                     XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME) == 0) {
            s.refresh_extension = true;
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
  const char* extensions[3] = {s.vulkan ? XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME
                                        : XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
  uint32_t extension_count = 1;
  if (s.gaze_extension) {
    extensions[extension_count++] = XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME;
  }
  if (s.refresh_extension) {
    extensions[extension_count++] = XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME;
  }

  XrInstanceCreateInfo create = {XR_TYPE_INSTANCE_CREATE_INFO};
  create.enabledExtensionCount = extension_count;
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
    if (result == XR_ERROR_RUNTIME_UNAVAILABLE) {
      s.log("  The OpenXR loader cannot find or load a runtime for this process.");
      if constexpr (sizeof(void*) == 8) {
        s.log("  This process is 64 bit. Check the active runtime for");
        s.log("  64 bit Windows OpenXR applications and the loader log.");
      } else {
        s.log("  The game is 32 bit. It needs a runtime that supports 32 bit");
        s.log("  Windows OpenXR applications. A 64 bit runtime alone is not enough.");
        s.log("  On Windows, check the active 32 bit OpenXR runtime.");
        s.log("  On Wine or Proton, check support for 32 bit Windows OpenXR");
        s.log("  applications in that build. A running SteamVR alone does not");
        s.log("  confirm this support. Check the loader log for the cause.");
      }
    } else if (result == XR_ERROR_EXTENSION_NOT_PRESENT) {
      s.log("  The runtime does not provide the required XR_KHR_D3D11_enable");
      s.log("  extension. The mod needs OpenXR support for Direct3D 11.");
    } else if (result == XR_ERROR_FILE_ACCESS_ERROR ||
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
  if (s.refresh_extension) {
    xrGetInstanceProcAddr(
        s.instance, "xrEnumerateDisplayRefreshRatesFB",
        reinterpret_cast<PFN_xrVoidFunction*>(&s.enumerate_rates));
    xrGetInstanceProcAddr(s.instance, "xrGetDisplayRefreshRateFB",
                          reinterpret_cast<PFN_xrVoidFunction*>(&s.get_rate));
    xrGetInstanceProcAddr(
        s.instance, "xrRequestDisplayRefreshRateFB",
        reinterpret_cast<PFN_xrVoidFunction*>(&s.request_rate));
  }
  return true;
}

int XrScreen::StartSystem() {
  if (remote_) {
    if (!remote_->Call(bridge::System)) return -1;
    recommended_w_ = remote_->packet->state.recommended_width;
    recommended_h_ = remote_->packet->state.recommended_height;
    return remote_->packet->result;
  }
  Impl& s = *impl_;
  if (s.instance == XR_NULL_HANDLE) return -1;

  // --- system -------------------------------------------------------------
  // The spec lets a runtime give XR_SUCCESS to a later call on the same
  // instance after XR_ERROR_FORM_FACTOR_UNAVAILABLE, for example when the
  // headset connects. So that answer keeps the instance.
  XrSystemGetInfo system_info = {XR_TYPE_SYSTEM_GET_INFO};
  system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  const XrResult result = xrGetSystem(s.instance, &system_info, &s.system);
  if (XR_FAILED(result)) {
    s.system = XR_NULL_SYSTEM_ID;
    s.log("  openxr: no head mounted display (%s)",
          ResultName(s.instance, result));
    s.log("  Connect the headset in Virtual Desktop, then start the game.");
    return result == XR_ERROR_FORM_FACTOR_UNAVAILABLE ? 0 : -1;
  }

  XrSystemProperties system_properties = {XR_TYPE_SYSTEM_PROPERTIES};
  s.system_name[0] = 0;
  XrSystemEyeGazeInteractionPropertiesEXT gaze_properties = {
      XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT};
  if (s.gaze_extension) system_properties.next = &gaze_properties;
  if (XR_SUCCEEDED(
          xrGetSystemProperties(s.instance, s.system, &system_properties))) {
    strcpy_s(s.system_name, system_properties.systemName);
  }
  s.gaze_supported =
      s.gaze_extension && gaze_properties.supportsEyeGazeInteraction == XR_TRUE;
  if (s.gaze_wanted) {
    s.log("  openxr: eye tracking for foveated rendering: %s",
          s.gaze_supported ? "the headset has an eye tracker"
          : s.gaze_extension
              ? "the runtime has XR_EXT_eye_gaze_interaction, but the headset "
                "has no eye tracker, so the fovea is straight ahead"
              : "the runtime has no XR_EXT_eye_gaze_interaction, so the fovea "
                "is straight ahead");
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
      s.max_w = views[0].maxImageRectWidth;
      s.max_h = views[0].maxImageRectHeight;
    }
  }
  return 1;
}

void XrScreen::LogSystem() {
  if (remote_) { impl_->log("xr bridge    : recommended eye size %ux%u", recommended_w_, recommended_h_); return; }
  Impl& s = *impl_;
  if (s.system == XR_NULL_SYSTEM_ID) return;
  if (s.system_name[0] != 0) s.log("  openxr: headset \"%s\"", s.system_name);
  if (recommended_w_ != 0 && recommended_h_ != 0) {
    s.log("  openxr: the headset asks for %u x %u per eye, and would take up "
          "to %u x %u",
          recommended_w_, recommended_h_, s.max_w, s.max_h);
  }
}

float XrScreen::Impl::RateNow() {
  float rate = 0.0f;
  if (get_rate == nullptr || session == XR_NULL_HANDLE ||
      XR_FAILED(get_rate(session, &rate))) {
    return 0.0f;
  }
  return rate;
}

// THE REFRESH RATE OF THE HEADSET, after each xrBeginSession. The first
// time the log has the rates of the headset and the rate now. With
// refresh_rate_hz not 0 the mod asks for its rate, also after a new
// xrBeginSession (a headset that slept).
void XrScreen::Impl::ApplyRefreshRate() {
  using namespace refresh_rate_math;
  if (!refresh_extension || enumerate_rates == nullptr) {
    if (!refresh_logged) {
      refresh_logged = true;
      log("  refresh rate : the runtime has no XR_FB_display_refresh_rate, so "
          "the mod cannot read or ask for the refresh rate "
          "(refresh_rate_hz=%d)", refresh_ask);
    }
    return;
  }
  std::vector<float> rates;
  uint32_t count = 0;
  if (XR_SUCCEEDED(enumerate_rates(session, 0, &count, nullptr)) &&
      count > 0 && count < 64) {
    rates.resize(count);
    if (XR_FAILED(enumerate_rates(session, count, &count, rates.data()))) {
      count = 0;
    }
  } else {
    count = 0;
  }
  rates.resize(count);
  const float now = RateNow();
  if (!refresh_logged) {
    refresh_logged = true;
    std::string list;
    char one[32];
    for (uint32_t i = 0; i < count; ++i) {
      sprintf_s(one, "%s%.1f", i > 0 ? ", " : "", rates[i]);
      list += one;
    }
    if (count > 0) {
      log("  refresh rate : the headset offers %s Hz, and the rate now is "
          "%.1f Hz (XR_FB_display_refresh_rate)", list.c_str(), now);
    } else {
      log("  refresh rate : the runtime gives no rates of the headset, and "
          "the rate now is %.1f Hz (XR_FB_display_refresh_rate)", now);
    }
  }
  if (refresh_ask == kAskNone) {
    log("  refresh rate : refresh_rate_hz=0, the mod asks for no rate");
    return;
  }
  const Choice choice =
      Choose(rates.data(), static_cast<int>(count), refresh_ask);
  if (choice.how == kNone) {
    log("  refresh rate : refresh_rate_hz=%d, but the headset offers no rate, "
        "so the mod asks for none", refresh_ask);
    return;
  }
  const char* why = choice.how == kExact    ? "the rate of the ini"
                    : choice.how == kHigher ? "the headset does not offer the "
                                              "rate of the ini, so the nearest "
                                              "higher rate"
                    : choice.how == kHighest
                        ? "the highest rate of the headset"
                        : "the headset offers no rate at or above the rate of "
                          "the ini, so its highest rate";
  if (now > 0.0f && SameRate(now, choice.hz)) {
    log("  refresh rate : refresh_rate_hz=%d gives %.1f Hz (%s). The headset "
        "has that rate now, so the mod does not ask for it",
        refresh_ask, choice.hz, why);
    return;
  }
  const XrResult result = request_rate != nullptr
                              ? request_rate(session, choice.hz)
                              : XR_ERROR_FUNCTION_UNSUPPORTED;
  log("  refresh rate : refresh_rate_hz=%d gives %.1f Hz (%s). The mod asks "
      "for it: %s. The rate now is %.1f Hz",
      refresh_ask, choice.hz, why, ResultName(instance, result), RateNow());
}

bool XrScreen::StartSession(uint32_t width, uint32_t height, int eyes) {
  if (remote_) {
    auto& p = *remote_->packet;
    p.width = width; p.height = height; p.eyes = eyes >= 2 ? 2 : 1;
    p.distance = impl_->distance; p.screen_width = impl_->quad_width;
    p.panel_enabled = impl_->panel_wanted;
    if (!remote_->Call(bridge::Session) || !p.result || !remote_->MakeDevice()) return false;
    eyes_ = p.eyes; running_ = true;
    impl_->log("xr bridge    : session ready. Frame submission is synchronous in this mode");
    return true;
  }
  Impl& s = *impl_;
  if (s.instance == XR_NULL_HANDLE || s.system == XR_NULL_SYSTEM_ID) {
    return false;
  }
  eyes_ = (eyes >= 2) ? 2 : 1;
  s.width = width;
  s.height = height;

  if (s.vulkan) {
    // --- Vulkan through the runtime, and Direct3D 11 on its GPU when the
    // session has it (UseD3D11) ------------------------------------------
    if (!s.StartVulkan()) {
      s.vk_failed = true;
      return false;
    }
  } else {
    // --- Direct3D 11 on the adapter the runtime wants ----------------------
    if (!s.Check(xrGetInstanceProcAddr(
                     s.instance, "xrGetD3D11GraphicsRequirementsKHR",
                     reinterpret_cast<PFN_xrVoidFunction*>(&s.get_requirements)),
                 "xrGetInstanceProcAddr(D3D11 requirements)")) {
      return false;
    }
    XrGraphicsRequirementsD3D11KHR requirements = {
        XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    if (!s.Check(s.get_requirements(s.instance, s.system, &requirements),
                 "xrGetD3D11GraphicsRequirementsKHR")) {
      return false;
    }
    if (!s.MakeD3D11(requirements.adapterLuid)) return false;
  }

  // --- session ------------------------------------------------------------
  XrGraphicsBindingD3D11KHR binding = {XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
  binding.device = s.device;
  XrGraphicsBindingVulkan2KHR vk_binding = {
      XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
  vk_binding.instance = g_vk.instance;
  vk_binding.physicalDevice = g_vk.physical;
  vk_binding.device = g_vk.device;
  vk_binding.queueFamilyIndex = g_vk.family;
  vk_binding.queueIndex = 0;

  XrSessionCreateInfo session_info = {XR_TYPE_SESSION_CREATE_INFO};
  session_info.next = s.vulkan ? static_cast<const void*>(&vk_binding)
                               : static_cast<const void*>(&binding);
  session_info.systemId = s.system;
  if (!s.Check(xrCreateSession(s.instance, &session_info, &s.session),
               "xrCreateSession")) {
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

  // The order of choice is in kFormats. A session with Vulkan lists the
  // VkFormat of each format, and one with Direct3D 11 its DXGI_FORMAT.
  s.format = DXGI_FORMAT_UNKNOWN;
  s.format_index = -1;
  for (int want = 0; want < kFormatCount && s.format_index < 0; ++want) {
    const int64_t value =
        s.vulkan ? static_cast<int64_t>(kFormats[want].vk)
                 : static_cast<int64_t>(kFormats[want].dxgi);
    for (uint32_t i = 0; i < format_count; ++i) {
      if (formats[i] == value) {
        s.format_index = want;
        s.format = kFormats[want].dxgi;
        break;
      }
    }
  }
  delete[] formats;

  if (s.format == DXGI_FORMAT_UNKNOWN) {
    s.log("  openxr: no 8 bit BGRA or RGBA swapchain format offered");
    return false;
  }
  s.log("  openxr: swapchain format %d, %ux%u", static_cast<int>(s.format),
        s.width, s.height);

  XrSwapchainCreateInfo swapchain_info = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
  swapchain_info.usageFlags = s.ChainUsage();
  swapchain_info.format = s.ChainFormat();
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
      return false;
    }

    s.images[eye] = s.ChainImages(s.swapchain[eye], s.width, s.height,
                                  &s.image_count[eye]);
    if (s.images[eye] == nullptr) {
      if (s.vulkan) s.vk_failed = true;
      return false;
    }
  }

  // The interface panel. The same format and size as the eyes, because the
  // panel target is the render size and its color is gamma encoded like the
  // eyes. The screen works without it, so a failure is only logged.
  if (s.panel_wanted && eyes_ == 2) {
    if (XR_SUCCEEDED(xrCreateSwapchain(s.session, &swapchain_info,
                                       &s.panel_swapchain)) &&
        (s.panel_images = s.ChainImages(s.panel_swapchain, s.width, s.height,
                                        &s.panel_image_count)) != nullptr) {
      s.log("  openxr: the interface panel has its own swapchain, %u images",
            s.panel_image_count);
    } else {
      s.DestroyChain(&s.panel_swapchain);
      s.panel_swapchain = XR_NULL_HANDLE;
      s.log("  openxr: no swapchain for the interface panel, so the interface "
            "stays in the eye pictures");
    }
  }

  // A dynamic texture is the cheap way to get CPU pixels onto the GPU: map,
  // copy the rows, unmap, then let the GPU do the copy into the swapchain.
  // A session with no Direct3D 11 copies them with Vulkan.
  if (s.device != nullptr) {
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
      return false;
    }
  }

  s.log("  openxr: ready, %d eye(s), %u swapchain images each%s", eyes_,
        s.image_count[0], s.device == nullptr ? ", with no Direct3D 11" : "");
  // A layer that was staged before the session is not for its first frame.
  for (int i = 0; i < kCards; ++i) g_have_staged_card[i] = false;
  g_have_staged_window = false;
  s.session_started = true;
  running_ = true;
  return true;
}

bool XrScreen::BeginFrame() {
  if (remote_) {
    if (!remote_->Call(bridge::Begin) || !remote_->packet->result) { running_ = false; should_render_ = false; return false; }
    const auto& state = remote_->packet->state;
    running_ = state.running != 0; should_render_ = state.render != 0; recenter_due_ |= state.recenter != 0;
    for (int i = 0; i < 2; ++i) { views_[i] = state.eyes[i]; hands_[i] = state.hands[i]; }
    impl_->frame_open = true;
    return true;
  }
  Impl& s = *impl_;
  should_render_ = false;
  if (!running_ || s.session == XR_NULL_HANDLE) return false;
  // The second thread sends no frame of the hold while this one reads the
  // events and begins a frame.
  Impl::HoldGuard hold(s);

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
          s.ApplyRefreshRate();
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
    } else if (event.type == XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB) {
      const XrEventDataDisplayRefreshRateChangedFB* changed =
          reinterpret_cast<const XrEventDataDisplayRefreshRateChangedFB*>(
              &event);
      s.log("  refresh rate : the headset changed its refresh rate from %.1f "
            "to %.1f Hz",
            changed->fromDisplayRefreshRate, changed->toDisplayRefreshRate);
    }
  }

  // A sleeping headset must not leave a button held.
  if (!s.session_running) {
    InterlockedExchange(&s.hold_live, 0);
    Join();
    s.ClearInput(hands_);
    return true;  // alive, just not presenting yet
  }

  // --- wait and begin -----------------------------------------------------
  InterlockedExchange(&s.hold_live, 1);
  if (s.hold_ms > 0) s.StartSendThread();
  // A frame of the game that a hold took, and that the caller did not end:
  // the runtime has no frame open for it.
  if (s.frame_open &&
      InterlockedCompareExchange(&s.frame_state_now, Impl::kFrameNone,
                                 Impl::kFrameTaken) == Impl::kFrameTaken) {
    s.frame_open = false;
  }
  XrFrameWaitInfo wait_info = {XR_TYPE_FRAME_WAIT_INFO};
  s.frame_state = {XR_TYPE_FRAME_STATE};
  LARGE_INTEGER wait_start;
  QueryPerformanceCounter(&wait_start);
  const XrResult waited = xrWaitFrame(s.session, &wait_info, &s.frame_state);
  LARGE_INTEGER waited_at;
  QueryPerformanceCounter(&waited_at);
  s.CountCall(Impl::kCallWait,
              static_cast<uint64_t>(waited_at.QuadPart - wait_start.QuadPart));
  // xrWaitFrame can run while the second thread sends the frame before.
  // xrBeginFrame cannot: the runtime must have that frame first. Join reads
  // the time of that frame, so the time of this frame is set after it.
  Join();
  s.waited_at = waited_at;
  if (XR_SUCCEEDED(waited)) {
    s.display_seconds_ns = s.frame_state.predictedDisplayTime;
    // THE PACE OF THE RUNTIME: the display period that it predicts, the
    // time between two display times, and the time in xrWaitFrame. The
    // first frames of the session go in one line of the log.
    if (s.frequency.QuadPart == 0) QueryPerformanceFrequency(&s.frequency);
    const double wait_ms =
        static_cast<double>(waited_at.QuadPart - wait_start.QuadPart) *
        1000.0 / static_cast<double>(s.frequency.QuadPart);
    refresh_rate_math::Add(&s.display_window,
                           s.frame_state.predictedDisplayTime,
                           s.frame_state.predictedDisplayPeriod, wait_ms);
    if (s.first_count < Impl::kFirstFrames) {
      s.first_period[s.first_count] = s.frame_state.predictedDisplayPeriod;
      s.first_time[s.first_count] = s.frame_state.predictedDisplayTime;
      if (++s.first_count == Impl::kFirstFrames) {
        char periods[128] = "";
        char steps[128] = "";
        int used = 0, steps_used = 0;
        for (int i = 0; i < Impl::kFirstFrames; ++i) {
          used += sprintf_s(periods + used, sizeof(periods) - used, "%s%.2f",
                            i > 0 ? " " : "", s.first_period[i] / 1e6);
          if (i > 0) {
            steps_used += sprintf_s(
                steps + steps_used, sizeof(steps) - steps_used, "%s%.2f",
                i > 1 ? " " : "",
                (s.first_time[i] - s.first_time[i - 1]) / 1e6);
          }
        }
        const float rate = s.RateNow();
        char rate_text[64] = "the runtime does not give the refresh rate";
        if (rate > 0.0f) {
          sprintf_s(rate_text, "the refresh rate is %.1f Hz", rate);
        }
        s.log("  refresh rate : the first %d frames of the session: the "
              "display period of xrWaitFrame, ms: %s. The display times "
              "came this far apart, ms: %s. Now %s",
              Impl::kFirstFrames, periods, steps, rate_text);
      }
    }
  }
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
  XrResult begun;
  LARGE_INTEGER begin_start, begin_done;
  QueryPerformanceCounter(&begin_start);
  {
    QueueGuard guard(s.vulkan);
    begun = xrBeginFrame(s.session, &begin_info);
  }
  QueryPerformanceCounter(&begin_done);
  s.CountCall(Impl::kCallBegin, static_cast<uint64_t>(begin_done.QuadPart -
                                                      begin_start.QuadPart));
  if (!s.Check(begun, "xrBeginFrame")) {
    return true;
  }
  {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    InterlockedExchange64(&s.game_open_at, now.QuadPart);
    // xrBeginFrame comes after the end of the frame before (Join).
    InterlockedExchange(&s.frame_state_now, Impl::kFrameGame);
    s.begun_at = now;
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
  if (remote_) { if (impl_->frame_open && should_render_) remote_->Pixels((index >= 1 && eyes_ > 1) ? 1 : 0, pixels, pitch); return; }
  Impl& s = *impl_;
  if (!s.frame_open || !should_render_ || pixels == nullptr) return;
  const int eye = (index >= 1 && eyes_ > 1) ? 1 : 0;
  if (s.swapchain[eye] == XR_NULL_HANDLE) return;

  if (s.frequency.QuadPart == 0) QueryPerformanceFrequency(&s.frequency);
  LARGE_INTEGER t0, t1, t2, t3;
  QueryPerformanceCounter(&t0);

  uint32_t image = 0;
  if (!s.Check(s.AcquireImage(s.swapchain[eye], &image),
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
  if (s.UploadsWithVulkan(s.swapchain[eye])) {
    // THE PIXELS OF THE CPU: the bytes as they are, as into the staging
    // texture of Direct3D 11.
    s.UploadVulkan(s.swapchain[eye], image, static_cast<const uint8_t*>(pixels),
                   static_cast<uint32_t>(pitch), 0, 0,
                   static_cast<int32_t>(s.width),
                   static_cast<int32_t>(s.height), false);
    QueryPerformanceCounter(&t2);
    s.ReleaseImage(s.swapchain[eye], false);
    s.acquired[eye] = static_cast<int>(image);
    QueryPerformanceCounter(&t3);
    s.wait_ticks += t1.QuadPart - t0.QuadPart;
    s.copy_ticks += t2.QuadPart - t1.QuadPart;
    s.submit_ticks += t3.QuadPart - t2.QuadPart;
    ++s.uploads;
    return;
  }
  if (s.context == nullptr) {
    s.ReleaseImage(s.swapchain[eye], false);
    return;
  }
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

  s.ReleaseImage(s.swapchain[eye]);
  s.acquired[eye] = static_cast<int>(image);

  QueryPerformanceCounter(&t3);
  s.wait_ticks += t1.QuadPart - t0.QuadPart;
  s.copy_ticks += t2.QuadPart - t1.QuadPart;
  s.submit_ticks += t3.QuadPart - t2.QuadPart;
  ++s.uploads;
}

void* XrScreen::D3D11Device() const { return remote_ ? remote_->device : impl_->device; }

uint32_t XrScreen::SwapchainFormat() const {
  if (remote_) return remote_->packet->state.format;
  return static_cast<uint32_t>(impl_->format);
}

void XrScreen::SetEyeTexture(int index, void* texture) {
  if (remote_) { if (impl_->frame_open && should_render_) remote_->Texture(texture, (index >= 1 && eyes_ > 1) ? 1 : 0, false); return; }
  Impl& s = *impl_;
  if (!s.frame_open || !should_render_ || texture == nullptr ||
      s.context == nullptr) {
    return;
  }
  const int eye = (index >= 1 && eyes_ > 1) ? 1 : 0;
  if (s.swapchain[eye] == XR_NULL_HANDLE) return;

  if (s.frequency.QuadPart == 0) QueryPerformanceFrequency(&s.frequency);
  LARGE_INTEGER t0, t1, t3;
  QueryPerformanceCounter(&t0);

  uint32_t image = 0;
  if (!s.Check(s.AcquireImage(s.swapchain[eye], &image),
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

  s.ReleaseImage(s.swapchain[eye]);
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
  DestroyChain(&chain.swapchain);
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
    info.usageFlags = ChainUsage();
    info.format = ChainFormat();
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
    chain.images = ChainImages(chain.swapchain, card.texture_width,
                               card.texture_height, &chain.image_count);
    if (chain.images == nullptr) {
      DestroyChain(&chain.swapchain);
      g_card_failed[index] = true;
      log("  openxr: no images for the swapchain of the %s, so the %s stay "
          "in the eye pictures", kCardName[index], kCardHolds[index]);
      return false;
    }
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
  if (!Check(AcquireImage(chain.swapchain, &image),
             "xrAcquireSwapchainImage(card)")) {
    return miss();
  }
  XrSwapchainImageWaitInfo image_wait = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  image_wait.timeout = XR_INFINITE_DURATION;
  const bool waited = Check(xrWaitSwapchainImage(chain.swapchain, &image_wait),
                            "xrWaitSwapchainImage(card)");
  // The card is BGRA. A swapchain with red first needs the two swapped.
  const bool red_first = format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
                         format == DXGI_FORMAT_R8G8B8A8_UNORM;
  const bool with_vulkan = UploadsWithVulkan(chain.swapchain);
  if (waited && image < chain.image_count && with_vulkan) {
    UploadVulkan(chain.swapchain, image, card.pixels, chain.width * 4, x, y, w,
                 h, red_first);
  } else if (waited && image < chain.image_count && context != nullptr) {
    const uint32_t pitch = chain.width * 4;
    const uint8_t* source = card.pixels;
    if (red_first) {
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
  ReleaseImage(chain.swapchain, !with_vulkan);
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
  DestroyChain(&mask_swapchain);
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
    info.usageFlags = ChainUsage();
    info.format = ChainFormat();
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
    mask_images =
        ChainImages(mask_swapchain, kMaskSize, kMaskSize, &mask_image_count);
    if (mask_images == nullptr) {
      DestroyChain(&mask_swapchain);
      g_mask_failed = true;
      log("  openxr: no images for the swapchain of the walls of the cut "
          "scene window, so the view changes to the window with no walls");
      return false;
    }
  }

  uint32_t image = 0;
  if (!Check(AcquireImage(mask_swapchain, &image),
             "xrAcquireSwapchainImage(walls)")) {
    return false;
  }
  XrSwapchainImageWaitInfo image_wait = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  image_wait.timeout = XR_INFINITE_DURATION;
  const bool waited = Check(xrWaitSwapchainImage(mask_swapchain, &image_wait),
                            "xrWaitSwapchainImage(walls)");
  const bool with_vulkan = UploadsWithVulkan(mask_swapchain);
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
    if (with_vulkan) {
      UploadVulkan(mask_swapchain, image, pixels, kMaskSize * 4, 0, 0,
                   static_cast<int32_t>(kMaskSize),
                   static_cast<int32_t>(kMaskSize), false);
    } else if (context != nullptr) {
      context->UpdateSubresource(mask_images[image], 0, nullptr, pixels,
                                 kMaskSize * 4, 0);
    }
  }
  ReleaseImage(mask_swapchain, !with_vulkan);
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
  if (remote_) return remote_->healthy() && remote_->packet->state.panel_ready != 0;
  return impl_->panel_swapchain != XR_NULL_HANDLE;
}

// The panel, the way SetEyeTexture sends an eye: acquire, wait, one copy on
// the GPU, release.
void XrScreen::SetPanelTexture(void* texture) {
  if (remote_) { if (impl_->frame_open && should_render_) remote_->Texture(texture, 2, true); return; }
  Impl& s = *impl_;
  s.panel_acquired = -1;
  if (!s.frame_open || !should_render_ || texture == nullptr ||
      s.panel_swapchain == XR_NULL_HANDLE || s.context == nullptr) {
    return;
  }
  uint32_t image = 0;
  if (!s.Check(s.AcquireImage(s.panel_swapchain, &image),
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
  s.ReleaseImage(s.panel_swapchain);
  s.panel_acquired = static_cast<int>(image);
}

void XrScreen::SetPanelVulkan(uint64_t image) {
  Impl& s = *impl_;
  s.panel_acquired = -1;
  if (!s.vulkan || !s.frame_open || !should_render_ || image == 0 ||
      s.panel_swapchain == XR_NULL_HANDLE) {
    return;
  }
  if (s.Check(s.CopyVulkan(s.panel_swapchain, image, 0),
              "the copy of a Vulkan panel")) {
    s.panel_acquired = 0;
  }
}

void XrScreen::SetPanelQuads(const PanelQuad quads[2]) {
  if (remote_) {
    for (int i = 0; i < 2; ++i) remote_->packet->panels[i] = quads[i];
    remote_->packet->panels[2] = g_staged_quad;
    if (!g_have_staged_quad) remote_->packet->panels[2].visible = false;
    g_have_staged_quad = false;
    remote_->Call(bridge::Panels); return;
  }
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
  if (remote_) return;
  Impl& s = *impl_;
  if (s.vulkan && (s.vk_pushes > 0 || s.vk_copies > 0 || s.vk_uploads > 0)) {
    LARGE_INTEGER hz_now;
    QueryPerformanceFrequency(&hz_now);
    const double hz = static_cast<double>(hz_now.QuadPart);
    if (s.device == nullptr) {
      s.log("  openxr: Vulkan with no Direct3D 11: %llu copies of a Vulkan eye "
            "picture, and %llu copies of pixels of the CPU, %.3f ms each",
            s.vk_copies, s.vk_uploads,
            s.vk_uploads > 0 ? s.vk_upload_ticks * 1000.0 / hz / s.vk_uploads
                             : 0.0);
    } else {
      s.log("  openxr: Vulkan: %llu copies of a relay, %.3f ms each with the "
            "wait for Direct3D 11, %llu copies of a Vulkan eye picture, and "
            "%llu copies of pixels of the CPU, %.3f ms each "
            "(xr_vulkan_upload=%d)",
            s.vk_pushes,
            s.vk_pushes > 0 ? s.vk_push_ticks * 1000.0 / hz / s.vk_pushes
                            : 0.0,
            s.vk_copies, s.vk_uploads,
            s.vk_uploads > 0 ? s.vk_upload_ticks * 1000.0 / hz / s.vk_uploads
                             : 0.0,
            s.vk_upload ? 1 : 0);
    }
    s.vk_pushes = 0;
    s.vk_copies = 0;
    s.vk_push_ticks = 0;
    s.vk_uploads = 0;
    s.vk_upload_ticks = 0;
  }
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
  if (s.hold_ms > 0 && s.frequency.QuadPart != 0) {
    s.log("  hold         : xr_hold_ms=%ld. %llu frames of the hold in %llu runs "
          "(%llu frames of the game taken, %llu failed). The longest time "
          "with no frame of the game was %.1f ms. No frame of the hold after "
          "%ld ms: %llu times the frame thread had the lock, %llu times the "
          "frame of the game was in SendFrame",
          static_cast<long>(s.hold_ms), s.hold_frames, s.hold_runs,
          s.hold_taken, s.hold_failed,
          static_cast<double>(s.hold_worst) * 1000.0 /
              static_cast<double>(s.frequency.QuadPart),
          static_cast<long>(3 * s.hold_ms), s.hold_lock_busy, s.hold_sending);
    s.hold_frames = s.hold_runs = s.hold_taken = s.hold_failed = 0;
    s.hold_worst = 0;
    s.hold_lock_busy = s.hold_sending = 0;
  }
  if (s.display_window.frames > 0) {
    // THE PACE OF THE RUNTIME. A display period of 8.33 ms with display
    // times 10 ms apart is a display of 120 Hz that gets a frame each 10 ms.
    using namespace refresh_rate_math;
    const refresh_rate_math::Window& w = s.display_window;
    const float rate = s.RateNow();
    char rate_text[48] = "not known";
    if (rate > 0.0f) sprintf_s(rate_text, "%.1f Hz", rate);
    s.log("  display      : the refresh rate is %s. In %llu frames xrWaitFrame "
          "gave a display period of %.2f ms (%.1f Hz), %.2f to %.2f ms. The "
          "display times came %.2f ms apart (%.1f Hz) in %llu steps, and "
          "%llu steps were more than 1.5 periods. xrWaitFrame took %.2f ms "
          "in the mean",
          rate_text, w.frames, MeanPeriodMs(w), HzOfMs(MeanPeriodMs(w)),
          w.period_least_ms, w.period_most_ms, MeanStepMs(w),
          HzOfMs(MeanStepMs(w)), w.steps, w.late, MeanWaitMs(w));
    Clear(&s.display_window);
  }
  if (s.frequency.QuadPart != 0) {
    const double to_ms = 1000.0 / static_cast<double>(s.frequency.QuadPart);
    s.log("  runtime calls: the longest call of the window, ms: xrWaitFrame "
          "%.2f, xrBeginFrame %.2f, xrEndFrame %.2f, the copies to the "
          "swapchains %.2f. Calls of 20 ms or more: %llu, %llu, %llu, %llu",
          static_cast<double>(s.call_worst[Impl::kCallWait]) * to_ms,
          static_cast<double>(s.call_worst[Impl::kCallBegin]) * to_ms,
          static_cast<double>(s.call_worst[Impl::kCallEnd]) * to_ms,
          static_cast<double>(s.call_worst[Impl::kCallCopies]) * to_ms,
          s.call_slow[Impl::kCallWait], s.call_slow[Impl::kCallBegin],
          s.call_slow[Impl::kCallEnd], s.call_slow[Impl::kCallCopies]);
    for (int c = 0; c < Impl::kCalls; ++c) {
      s.call_worst[c] = 0;
      s.call_slow[c] = 0;
    }
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
  if (remote_) {
    if (!impl_->frame_open) return;
    if (!remote_->healthy()) { impl_->frame_open = false; running_ = false; return; }
    auto& p = *remote_->packet;
    for (int card = 0; card < kCards; ++card) {
      if (g_have_staged_card[card]) remote_->Card(card, g_staged_card[card]);
      g_have_staged_card[card] = false;
    }
    if (g_have_staged_window) { p.window = g_staged_window; remote_->Call(bridge::Window); }
    g_have_staged_window = false;
    p.projection = projection_; p.half_angle = render_half_angle_; p.have_render_pose = have_render_pose_;
    for (int i = 0; i < 2; ++i) p.render_pose[i] = render_views_[i];
    p.distance = impl_->distance; p.screen_width = impl_->quad_width;
    if (!remote_->Call(bridge::End)) running_ = false;
    presented_ = p.state.presented; skipped_ = p.state.skipped;
    for (int i = 0; i < kCards; ++i) g_card_failed[i] = p.state.card_failed[i] != 0;
    impl_->frame_open = false; return;
  }
  // A frame with no wait for the GPU and no copies of a late frame.
  impl_->late_gpu = 0;
  impl_->late_copy = 0;
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
  // The second thread sends no frame of the hold while this one makes its
  // layers. When a hold took the frame, the pictures go in a new frame.
  Impl::HoldGuard hold(s);
  if (s.frame_state_now == Impl::kFrameTaken) {
    XrFrameWaitInfo wait_info = {XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState wait_state = {XR_TYPE_FRAME_STATE};
    XrResult result = xrWaitFrame(s.session, &wait_info, &wait_state);
    QueryPerformanceCounter(&s.waited_at);
    if (XR_SUCCEEDED(result)) {
      XrFrameBeginInfo begin_info = {XR_TYPE_FRAME_BEGIN_INFO};
      QueueGuard guard(s.vulkan);
      result = xrBeginFrame(s.session, &begin_info);
    }
    if (XR_FAILED(result)) {
      s.frame_open = false;
      InterlockedExchange(&s.frame_state_now, Impl::kFrameNone);
      for (int eye = 0; eye < 2; ++eye) s.acquired[eye] = -1;
      s.panel_acquired = -1;
      return false;
    }
    s.frame_state.predictedDisplayTime = wait_state.predictedDisplayTime;
  }
  // SendFrame comes after this, on this thread or on the second thread.
  InterlockedExchange(&s.frame_state_now, Impl::kFrameSending);
  if (s.frequency.QuadPart == 0) QueryPerformanceFrequency(&s.frequency);
  Impl::Built& b = s.built;
  QueryPerformanceCounter(&b.before);
  b.waited_at = s.waited_at;
  b.begun_at = s.begun_at;
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
  {
    QueueGuard guard(vulkan);
    b.result = xrEndFrame(session, &end);
  }
  QueryPerformanceCounter(&b.end_done);
  InterlockedExchange64(&last_end, b.end_done.QuadPart);
  InterlockedCompareExchange(&frame_state_now, kFrameNone, kFrameSending);
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
  s.CountCall(Impl::kCallEnd, static_cast<uint64_t>(b.end_done.QuadPart -
                                                    b.end_start.QuadPart));
  if (b.layer_count > 0 && b.waited_at.QuadPart != 0) {
    const uint64_t age =
        static_cast<uint64_t>(b.end_done.QuadPart - b.waited_at.QuadPart);
    s.age_ticks += age;
    if (age > s.age_worst) s.age_worst = age;
    ++s.age_frames;
    // A frame of more than 250 ms, in its parts (the first 30).
    const double to_ms =
        s.frequency.QuadPart != 0
            ? 1000.0 / static_cast<double>(s.frequency.QuadPart)
            : 0.0;
    if (age * to_ms > 250.0 && s.slow_lines < 30) {
      ++s.slow_lines;
      const double begin =
          b.begun_at.QuadPart > b.waited_at.QuadPart
              ? (b.begun_at.QuadPart - b.waited_at.QuadPart) * to_ms
              : 0.0;
      const double game =
          b.begun_at.QuadPart != 0 && b.before.QuadPart > b.begun_at.QuadPart
              ? (b.before.QuadPart - b.begun_at.QuadPart) * to_ms
              : 0.0;
      const double between =
          (b.end_start.QuadPart - b.built.QuadPart) * to_ms;
      s.log("  slow frame   : %.1f ms from xrWaitFrame to the end of "
            "xrEndFrame: BeginFrame after the wait %.1f, the game %.1f, "
            "BuildFrame %.1f, from BuildFrame to xrEndFrame %.1f (the wait "
            "for the GPU %.1f, the copies %.1f), xrEndFrame %.1f. Frames of "
            "the hold so far: %llu, %llu taken",
            age * to_ms, begin, game,
            (b.built.QuadPart - b.before.QuadPart) * to_ms, between,
            s.late_gpu * to_ms, s.late_copy * to_ms,
            (b.end_done.QuadPart - b.end_start.QuadPart) * to_ms,
            s.hold_frames, s.hold_taken);
    }
  }
}

// A LATE FRAME. The layers come first, so the cards go to the runtime while
// the GPU works on the eye pictures. Then come the wait for the GPU, the
// copies and xrEndFrame, on the second thread or on this thread.
void XrScreen::EndFrameLate(const LateFrame& frame, bool on_thread) {
  if (remote_) {
    if (!impl_->frame_open) return;
    if (frame.wait) frame.wait();
    for (int i = 0; i < eyes_; ++i) if (frame.eye_texture[i]) SetEyeTexture(i, frame.eye_texture[i]);
    SetPanelTexture(frame.panel_texture);
    EndFrame(); return;
  }
  Impl& s = *impl_;
  if (!s.frame_open) return;
  s.late = frame;
  // BuildFrame makes a layer only for a picture that the frame has.
  for (int eye = 0; eye < 2; ++eye) {
    if (!should_render_ || eye >= eyes_ ||
        s.swapchain[eye] == XR_NULL_HANDLE) {
      s.late.eye_texture[eye] = nullptr;
      s.late.eye_vulkan[eye] = 0;
    }
    if (!s.vulkan) s.late.eye_vulkan[eye] = 0;
    if (s.late.eye_texture[eye] != nullptr || s.late.eye_vulkan[eye] != 0) {
      s.acquired[eye] = 0;
    }
  }
  if (!should_render_ || s.panel_swapchain == XR_NULL_HANDLE) {
    s.late.panel_texture = nullptr;
    s.late.panel_vulkan = 0;
  }
  if (!s.vulkan) s.late.panel_vulkan = 0;
  s.panel_acquired =
      s.late.panel_texture != nullptr || s.late.panel_vulkan != 0 ? 0 : -1;
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
  if (remote_) {
    if (!remote_->healthy() || remote_->packet->state.time <= 0) return false;
    *time = remote_->packet->state.time; *period = remote_->packet->state.period; return true;
  }
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
  s.CountCall(Impl::kCallCopies, s.late_copy);
  s.late_end_ticks += static_cast<uint64_t>(s.built.end_done.QuadPart -
                                            s.built.end_start.QuadPart);
}

// One texture to the next image of its swapchain: acquire, wait, one copy on
// the GPU, release. As SetEyeTexture, with no line of the log.
XrResult XrScreen::Impl::CopyLate(XrSwapchain chain,
                                  ID3D11Texture2D** chain_images,
                                  void* texture) {
  if (context == nullptr) return XR_SUCCESS;
  uint32_t image = 0;
  XrResult result = AcquireImage(chain, &image);
  if (XR_FAILED(result)) return result;
  XrSwapchainImageWaitInfo image_wait = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  image_wait.timeout = XR_INFINITE_DURATION;
  result = xrWaitSwapchainImage(chain, &image_wait);
  if (XR_FAILED(result)) return result;
  context->CopyResource(chain_images[image],
                        static_cast<ID3D11Texture2D*>(texture));
  return ReleaseImage(chain);
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
        late.eye_vulkan[eye] != 0
            ? CopyVulkan(swapchain[eye], late.eye_vulkan[eye],
                         late.eye_layer[eye])
        : late.eye_texture[eye] != nullptr
            ? CopyLate(swapchain[eye], images[eye], late.eye_texture[eye])
            : XR_SUCCESS;
  }
  late_result[2] =
      late.panel_vulkan != 0
          ? CopyVulkan(panel_swapchain, late.panel_vulkan, 0)
      : late.panel_texture != nullptr
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
    // With the hold on, the thread also looks for a frame of the game that
    // does not come.
    const LONG hold = s->hold_ms;
    const DWORD got = WaitForSingleObject(
        s->send_go, hold > 0 ? static_cast<DWORD>(hold) / 2 + 1 : INFINITE);
    if (s->send_quit) break;
    if (got == WAIT_TIMEOUT) {
      s->HoldFrames();
      continue;
    }
    s->RunLate();
    s->SendFrame();
    SetEvent(s->send_done);
  }
  return 0;
}

// THE HOLD, on the second thread: frames with the layers of the last frame
// of the game while the game gives none. Each frame waits for the runtime
// (xrWaitFrame), so a run has the rate of the headset. The run ends when
// the frame thread wants hold_lock, or when the game sent a frame.
void XrScreen::Impl::HoldFrames() {
  LARGE_INTEGER hz;
  QueryPerformanceFrequency(&hz);
  bool in_run = false;
  for (;;) {
    const LONG ms = hold_ms;
    if (send_quit || ms <= 0 || frame_wants != 0 || hold_live == 0) return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const LONGLONG last = InterlockedCompareExchange64(&last_end, 0, 0);
    const LONGLONG limit = hz.QuadPart * ms / 1000;
    if (last == 0 || now.QuadPart - last < limit) return;
    if (!TryAcquireSRWLockExclusive(&hold_lock)) {
      if (now.QuadPart - last >= 3 * limit) ++hold_lock_busy;
      return;
    }
    const LONG owner = frame_state_now;
    if (owner == kFrameSending && now.QuadPart - last >= 3 * limit) {
      ++hold_sending;
    }
    bool ok = false;
    bool tried = false;
    if (frame_wants == 0 && built.layer_count > 0 && session_running) {
      if (owner == kFrameNone || owner == kFrameTaken) {
        tried = true;
        ok = HoldOne(owner);
      } else if (owner == kFrameGame &&
                 now.QuadPart -
                         InterlockedCompareExchange64(&game_open_at, 0, 0) >=
                     3 * limit) {
        // A frame of the game is taken only after three times the time:
        // a slow frame of the game must not wait for a second frame of the
        // runtime.
        tried = true;
        ok = HoldOne(owner);
      }
    }
    ReleaseSRWLockExclusive(&hold_lock);
    if (!tried) return;
    if (!ok) {
      ++hold_failed;
      return;
    }
    if (!in_run) {
      in_run = true;
      ++hold_runs;
    }
    QueryPerformanceCounter(&now);
    const uint64_t gap = static_cast<uint64_t>(now.QuadPart - last);
    if (gap > hold_worst) hold_worst = gap;
  }
}

// One frame of the hold, under hold_lock. With the frame of the game open
// (kFrameGame) it ends that frame with the layers of the last frame, and
// the frame is then taken. Else it waits for a frame of its own, begins it
// and ends it. The layers carry the poses of their pictures.
bool XrScreen::Impl::HoldOne(LONG owner) {
  XrTime display = frame_state.predictedDisplayTime;
  if (owner != kFrameGame) {
    XrFrameWaitInfo wait_info = {XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState wait_state = {XR_TYPE_FRAME_STATE};
    if (XR_FAILED(xrWaitFrame(session, &wait_info, &wait_state))) return false;
    XrFrameBeginInfo begin_info = {XR_TYPE_FRAME_BEGIN_INFO};
    XrResult begun;
    {
      QueueGuard guard(vulkan);
      begun = xrBeginFrame(session, &begin_info);
    }
    if (XR_FAILED(begun)) return false;
    display = wait_state.predictedDisplayTime;
  }
  XrFrameEndInfo end = {XR_TYPE_FRAME_END_INFO};
  end.displayTime = display;
  end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  end.layerCount = built.layer_count;
  end.layers = built.layers;
  XrResult ended;
  {
    QueueGuard guard(vulkan);
    ended = xrEndFrame(session, &end);
  }
  if (owner == kFrameGame) {
    InterlockedExchange(&frame_state_now, kFrameTaken);
    ++hold_taken;
  }
  if (XR_FAILED(ended)) return false;
  ++hold_frames;
  return true;
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

bool XrScreen::SubmitVulkan(uint64_t image, uint32_t layer) {
  if (!BeginFrame()) return false;
  for (int eye = 0; eye < eyes_; ++eye) SetEyeVulkan(eye, image, layer);
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
  if (remote_) {
    delete remote_; remote_ = nullptr; running_ = false; should_render_ = false;
    impl_->frame_open = false;
    for (int i = 0; i < kCards; ++i) { g_have_staged_card[i] = false; g_card_failed[i] = false; }
    g_have_staged_quad = false; g_have_staged_window = false;
    return;
  }
  Impl& s = *impl_;
  // Only a screen whose session started has frames. The end of a screen
  // with no started session can be on a different thread (vr_start.cpp), so
  // it does not touch the staged layers, which belong to the frame thread.
  const bool was_started = s.session_started;
  running_ = false;
  // No more frames of the hold.
  InterlockedExchange(&s.hold_ms, 0);
  InterlockedExchange(&s.hold_live, 0);
  {
    Impl::HoldGuard hold(s);
  }
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
  for (int eye = 0; eye < 2; ++eye) s.DestroyChain(&s.swapchain[eye]);
  s.DestroyChain(&s.panel_swapchain);
  delete[] s.panel_images;
  s.panel_images = nullptr;
  s.panel_image_count = 0;
  s.panel_acquired = -1;
  for (int i = 0; i < kCards; ++i) {
    s.DestroyCard(i);
    if (was_started) g_have_staged_card[i] = false;
  }
  s.DestroyMask();
  if (was_started) g_have_staged_window = false;
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
  s.StopVulkan();
  const bool had_d3d11 = s.device != nullptr;
  Release(s.staging);
  Release(s.context);
  Release(s.device);
  if (had_session && had_d3d11) {
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

// --- Direct3D 11 and Vulkan ---------------------------------------------------

// The Direct3D 11 device on the adapter with this LUID. The runtime names the
// adapter by LUID. Using a different one would make every present a cross
// adapter copy, if it worked at all.
bool XrScreen::Impl::MakeD3D11(const LUID& luid) {
  IDXGIFactory1* factory = nullptr;
  if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                reinterpret_cast<void**>(&factory)))) {
    log("  openxr: CreateDXGIFactory1 failed");
    return false;
  }

  IDXGIAdapter1* adapter = nullptr;
  for (UINT index = 0;; ++index) {
    IDXGIAdapter1* candidate = nullptr;
    if (factory->EnumAdapters1(index, &candidate) == DXGI_ERROR_NOT_FOUND) break;
    DXGI_ADAPTER_DESC1 description;
    if (SUCCEEDED(candidate->GetDesc1(&description)) &&
        memcmp(&description.AdapterLuid, &luid, sizeof(LUID)) == 0) {
      adapter = candidate;
      char name[128];
      WideCharToMultiByte(CP_ACP, 0, description.Description, -1, name,
                          sizeof(name), nullptr, nullptr);
      log("  openxr: adapter \"%s\"", name);
      break;
    }
    candidate->Release();
  }
  factory->Release();

  if (adapter == nullptr) {
    log("  openxr: the runtime asked for an adapter this process cannot see");
    return false;
  }

  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1,
                                      D3D_FEATURE_LEVEL_11_0};
  HRESULT created = D3D11CreateDevice(
      adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, levels,
      sizeof(levels) / sizeof(levels[0]), D3D11_SDK_VERSION, &device, nullptr,
      &context);
  adapter->Release();
  if (FAILED(created)) {
    log("  openxr: D3D11CreateDevice failed (0x%08X)", created);
    return false;
  }
  return true;
}

// A SESSION WITH VULKAN. The runtime makes the Vulkan instance and the
// device (XR_KHR_vulkan_enable2) with what the renderer of the mod needs:
// Vulkan 1.3 with dynamic rendering and synchronization2, multiview, the
// anisotropic filter when the GPU has it, and VK_KHR_external_memory_win32
// for the relays. Then comes a Direct3D 11 device on the same GPU, for the
// bridge and for the relays, when the session has Direct3D 11 (d3d11).
bool XrScreen::Impl::StartVulkan() {
  PFN_xrGetVulkanGraphicsRequirements2KHR get_requirements2 = nullptr;
  PFN_xrCreateVulkanInstanceKHR create_instance = nullptr;
  PFN_xrGetVulkanGraphicsDevice2KHR get_device = nullptr;
  PFN_xrCreateVulkanDeviceKHR create_device = nullptr;
  if (!Check(xrGetInstanceProcAddr(
                 instance, "xrGetVulkanGraphicsRequirements2KHR",
                 reinterpret_cast<PFN_xrVoidFunction*>(&get_requirements2)),
             "xrGetInstanceProcAddr(Vulkan requirements)") ||
      !Check(xrGetInstanceProcAddr(
                 instance, "xrCreateVulkanInstanceKHR",
                 reinterpret_cast<PFN_xrVoidFunction*>(&create_instance)),
             "xrGetInstanceProcAddr(xrCreateVulkanInstanceKHR)") ||
      !Check(xrGetInstanceProcAddr(
                 instance, "xrGetVulkanGraphicsDevice2KHR",
                 reinterpret_cast<PFN_xrVoidFunction*>(&get_device)),
             "xrGetInstanceProcAddr(xrGetVulkanGraphicsDevice2KHR)") ||
      !Check(xrGetInstanceProcAddr(
                 instance, "xrCreateVulkanDeviceKHR",
                 reinterpret_cast<PFN_xrVoidFunction*>(&create_device)),
             "xrGetInstanceProcAddr(xrCreateVulkanDeviceKHR)")) {
    return false;
  }
  XrGraphicsRequirementsVulkan2KHR requirements = {
      XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
  if (!Check(get_requirements2(instance, system, &requirements),
             "xrGetVulkanGraphicsRequirements2KHR")) {
    return false;
  }
  log("  openxr: the runtime takes Vulkan %u.%u to %u.%u",
      static_cast<unsigned>(XR_VERSION_MAJOR(requirements.minApiVersionSupported)),
      static_cast<unsigned>(XR_VERSION_MINOR(requirements.minApiVersionSupported)),
      static_cast<unsigned>(XR_VERSION_MAJOR(requirements.maxApiVersionSupported)),
      static_cast<unsigned>(XR_VERSION_MINOR(requirements.maxApiVersionSupported)));
  if (requirements.minApiVersionSupported > XR_MAKE_VERSION(1, 3, 0)) {
    log("  openxr: the runtime needs a Vulkan newer than 1.3");
    return false;
  }

  if (g_vk.made) {
    // A later Start: the runtime must ask for the GPU of the device.
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    XrVulkanGraphicsDeviceGetInfoKHR get_info = {
        XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    get_info.systemId = system;
    get_info.vulkanInstance = g_vk.instance;
    if (!Check(get_device(instance, &get_info, &physical),
               "xrGetVulkanGraphicsDevice2KHR")) {
      return false;
    }
    if (physical != g_vk.physical) {
      log("  openxr: the runtime asks for another GPU than the Vulkan device "
          "of the first session");
      return false;
    }
    log("  openxr: the Vulkan device of the first session, on %s", g_vk.gpu);
  } else {
    if (g_vk.library == nullptr) g_vk.library = LoadLibraryA("vulkan-1.dll");
    if (g_vk.library == nullptr) {
      log("  openxr: there is no vulkan-1.dll, so a session with Vulkan "
          "cannot start");
      return false;
    }
    vkf.vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetProcAddress(g_vk.library, "vkGetInstanceProcAddr"));
    if (vkf.vkGetInstanceProcAddr == nullptr) {
      log("  openxr: vulkan-1.dll has no vkGetInstanceProcAddr");
      return false;
    }

    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = BWVR_PRODUCT_NAME;
    app.pEngineName = "bwvr";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo instance_info = {
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo = &app;
    // THE WINDOW OF THE GAME: the renderer of the mod can present to it
    // with a swapchain of this instance, when the loader has a surface of
    // Win32.
    const char* surface_extensions[2] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                         "VK_KHR_win32_surface"};
    bool surface = false;
    {
      PFN_vkEnumerateInstanceExtensionProperties enumerate =
          reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
              vkf.vkGetInstanceProcAddr(
                  nullptr, "vkEnumerateInstanceExtensionProperties"));
      uint32_t count = 0;
      if (enumerate != nullptr && enumerate(nullptr, &count, nullptr) >= 0) {
        std::vector<VkExtensionProperties> list(count);
        if (enumerate(nullptr, &count, list.data()) >= 0) {
          int found = 0;
          for (uint32_t i = 0; i < count; ++i) {
            for (const char* name : surface_extensions) {
              if (strcmp(list[i].extensionName, name) == 0) ++found;
            }
          }
          surface = found == 2;
        }
      }
    }
    if (surface) {
      instance_info.enabledExtensionCount = 2;
      instance_info.ppEnabledExtensionNames = surface_extensions;
    }
    XrVulkanInstanceCreateInfoKHR xr_instance_info = {
        XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    xr_instance_info.systemId = system;
    xr_instance_info.pfnGetInstanceProcAddr = vkf.vkGetInstanceProcAddr;
    xr_instance_info.vulkanCreateInfo = &instance_info;
    VkResult vk_result = VK_SUCCESS;
    if (!Check(create_instance(instance, &xr_instance_info, &g_vk.instance,
                               &vk_result),
               "xrCreateVulkanInstanceKHR")) {
      return false;
    }
    if (vk_result != VK_SUCCESS) {
      log("  openxr: vkCreateInstance failed (%d)", static_cast<int>(vk_result));
      return false;
    }
#define BWVR_XR_VK_LOAD_INSTANCE(name)                                     \
  vkf.name = reinterpret_cast<PFN_##name>(                                 \
      vkf.vkGetInstanceProcAddr(g_vk.instance, #name));                    \
  if (vkf.name == nullptr) {                                               \
    log("  openxr: the Vulkan instance has no " #name);                    \
    return false;                                                          \
  }
    BWVR_XR_VK_INSTANCE(BWVR_XR_VK_LOAD_INSTANCE)
#undef BWVR_XR_VK_LOAD_INSTANCE

    XrVulkanGraphicsDeviceGetInfoKHR get_info = {
        XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    get_info.systemId = system;
    get_info.vulkanInstance = g_vk.instance;
    if (!Check(get_device(instance, &get_info, &g_vk.physical),
               "xrGetVulkanGraphicsDevice2KHR")) {
      return false;
    }

    // What the GPU of the runtime has.
    VkPhysicalDeviceIDProperties id = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 properties = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &id;
    vkf.vkGetPhysicalDeviceProperties2(g_vk.physical, &properties);
    strncpy_s(g_vk.gpu, properties.properties.deviceName, _TRUNCATE);
    if (properties.properties.apiVersion < VK_API_VERSION_1_3) {
      log("  openxr: the GPU of the runtime (%s) has no Vulkan 1.3", g_vk.gpu);
      return false;
    }
    if (!id.deviceLUIDValid && d3d11) {
      log("  openxr: the GPU of the runtime (%s) gives no LUID, so Direct3D "
          "11 cannot find it", g_vk.gpu);
      return false;
    }
    memcpy(&g_vk.luid, id.deviceLUID, sizeof(LUID));
    VkPhysicalDeviceVulkan13Features v13 = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan11Features v11 = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    v13.pNext = &v11;
    VkPhysicalDeviceFeatures2 features = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &v13;
    vkf.vkGetPhysicalDeviceFeatures2(g_vk.physical, &features);
    if (!v13.dynamicRendering || !v13.synchronization2 || !v11.multiview) {
      log("  openxr: the GPU of the runtime (%s) has no dynamic rendering, "
          "synchronization2 or multiview", g_vk.gpu);
      return false;
    }
    g_vk.max_anisotropy = features.features.samplerAnisotropy
                              ? properties.properties.limits.maxSamplerAnisotropy
                              : 0.0f;
    bool external = false;
    bool has_swapchain = false;
    uint32_t extension_count = 0;
    vkf.vkEnumerateDeviceExtensionProperties(g_vk.physical, nullptr,
                                             &extension_count, nullptr);
    std::vector<VkExtensionProperties> extension_list(extension_count);
    vkf.vkEnumerateDeviceExtensionProperties(
        g_vk.physical, nullptr, &extension_count, extension_list.data());
    for (uint32_t i = 0; i < extension_count; ++i) {
      if (strcmp(extension_list[i].extensionName,
                 VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME) == 0) {
        external = true;
      }
      if (strcmp(extension_list[i].extensionName,
                 VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
        has_swapchain = true;
      }
    }
    g_vk.window_present = surface && has_swapchain;
    // FOVEATED RENDERING: a shading rate attachment, when the GPU has it.
    bool rate_extension = false;
    for (uint32_t i = 0; i < extension_count; ++i) {
      if (strcmp(extension_list[i].extensionName,
                 VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME) == 0) {
        rate_extension = true;
      }
    }
    VkPhysicalDeviceFragmentShadingRateFeaturesKHR rate_features = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};
    if (rate_extension) {
      VkPhysicalDeviceFeatures2 rate_query = {
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
      rate_query.pNext = &rate_features;
      vkf.vkGetPhysicalDeviceFeatures2(g_vk.physical, &rate_query);
    }
    g_vk.shading_rate =
        rate_extension && rate_features.attachmentFragmentShadingRate;
    if (!external) {
      log("  openxr: the GPU of the runtime (%s) has no %s", g_vk.gpu,
          VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
      return false;
    }
    VkQueueFamilyProperties families[16];
    uint32_t family_count = 16;
    vkf.vkGetPhysicalDeviceQueueFamilyProperties(g_vk.physical, &family_count,
                                                 families);
    g_vk.family = UINT32_MAX;
    for (uint32_t q = 0; q < family_count && q < 16; ++q) {
      if (families[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        g_vk.family = q;
        break;
      }
    }
    if (g_vk.family == UINT32_MAX) {
      log("  openxr: the GPU of the runtime (%s) has no queue for graphics",
          g_vk.gpu);
      return false;
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = g_vk.family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    VkPhysicalDeviceVulkan13Features want13 = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    want13.dynamicRendering = VK_TRUE;
    want13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan11Features want11 = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    want11.multiview = VK_TRUE;
    want13.pNext = &want11;
    VkPhysicalDeviceFeatures2 want = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    want.pNext = &want13;
    // The game asks for an anisotropic filter (D3DTSS_MAXANISOTROPY 16).
    want.features.samplerAnisotropy =
        g_vk.max_anisotropy > 0.0f ? VK_TRUE : VK_FALSE;
    VkPhysicalDeviceFragmentShadingRateFeaturesKHR want_rate = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};
    want_rate.attachmentFragmentShadingRate = VK_TRUE;
    if (g_vk.shading_rate) want11.pNext = &want_rate;
    const char* device_extensions[3] = {
        VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME};
    uint32_t device_extension_count = 1;
    if (g_vk.shading_rate) {
      device_extensions[device_extension_count++] =
          VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME;
    }
    if (g_vk.window_present) {
      device_extensions[device_extension_count++] =
          VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    }
    VkDeviceCreateInfo device_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.pNext = &want;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = device_extension_count;
    device_info.ppEnabledExtensionNames = device_extensions;
    XrVulkanDeviceCreateInfoKHR xr_device_info = {
        XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    xr_device_info.systemId = system;
    xr_device_info.pfnGetInstanceProcAddr = vkf.vkGetInstanceProcAddr;
    xr_device_info.vulkanPhysicalDevice = g_vk.physical;
    xr_device_info.vulkanCreateInfo = &device_info;
    if (!Check(create_device(instance, &xr_device_info, &g_vk.device,
                             &vk_result),
               "xrCreateVulkanDeviceKHR")) {
      return false;
    }
    if (vk_result != VK_SUCCESS) {
      log("  openxr: vkCreateDevice failed (%d)", static_cast<int>(vk_result));
      return false;
    }
#define BWVR_XR_VK_LOAD_DEVICE(name)                                       \
  vkf.name = reinterpret_cast<PFN_##name>(                                 \
      vkf.vkGetDeviceProcAddr(g_vk.device, #name));                        \
  if (vkf.name == nullptr) {                                               \
    log("  openxr: the Vulkan device has no " #name);                      \
    return false;                                                          \
  }
    BWVR_XR_VK_DEVICE(BWVR_XR_VK_LOAD_DEVICE)
#undef BWVR_XR_VK_LOAD_DEVICE
    vkf.vkGetDeviceQueue(g_vk.device, g_vk.family, 0, &g_vk.queue);
    vkf.vkGetPhysicalDeviceMemoryProperties(g_vk.physical, &g_vk.memory);
    g_vk.made = true;
    log("  openxr: Vulkan 1.3 on %s, made by the runtime. The anisotropic "
        "filter: up to %.0f. A shading rate attachment: %s. A swapchain for "
        "the window of the game: %s", g_vk.gpu, g_vk.max_anisotropy,
        g_vk.shading_rate ? "yes" : "no", g_vk.window_present ? "yes" : "no");
  }

  if (d3d11) {
    if (!MakeD3D11(g_vk.luid)) return false;
  } else {
    log("  openxr: the session has no Direct3D 11 device. Each picture goes "
        "into the swapchains with Vulkan");
  }
  if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);

  // The objects of this session: the copies and the wait for Direct3D 11.
  VkCommandPoolCreateInfo pool_info = {
      VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_info.queueFamilyIndex = g_vk.family;
  if (vkf.vkCreateCommandPool(g_vk.device, &pool_info, nullptr, &vk_pool) !=
      VK_SUCCESS) {
    log("  openxr: vkCreateCommandPool failed");
    return false;
  }
  VkCommandBufferAllocateInfo cb_info = {
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  cb_info.commandPool = vk_pool;
  cb_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cb_info.commandBufferCount = kVkSlots;
  if (vkf.vkAllocateCommandBuffers(g_vk.device, &cb_info, vk_cb) !=
      VK_SUCCESS) {
    log("  openxr: vkAllocateCommandBuffers failed");
    return false;
  }
  for (int i = 0; i < kVkSlots; ++i) {
    VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkf.vkCreateFence(g_vk.device, &fence_info, nullptr, &vk_fence[i]) !=
        VK_SUCCESS) {
      log("  openxr: vkCreateFence failed");
      return false;
    }
  }
  vk_slot = 0;
  if (device == nullptr) return true;
  D3D11_QUERY_DESC query = {D3D11_QUERY_EVENT, 0};
  if (FAILED(device->CreateQuery(&query, &d3d11_done))) {
    log("  openxr: the event query of Direct3D 11 could not be made");
    return false;
  }
  return true;
}

// The objects of a session with Vulkan. The instance and the device stay.
void XrScreen::Impl::StopVulkan() {
  if (!vulkan || !g_vk.made) return;
  for (int i = 0; i < kVkChains; ++i) {
    if (vk_chains[i].chain != XR_NULL_HANDLE) DestroyChain(&vk_chains[i].chain);
  }
  for (int i = 0; i < kVkSlots; ++i) {
    if (vk_fence[i] != VK_NULL_HANDLE) {
      vkf.vkWaitForFences(g_vk.device, 1, &vk_fence[i], VK_TRUE, UINT64_MAX);
      vkf.vkDestroyFence(g_vk.device, vk_fence[i], nullptr);
      vk_fence[i] = VK_NULL_HANDLE;
    }
    vk_cb[i] = VK_NULL_HANDLE;
  }
  FreeStages();
  if (vk_pool != VK_NULL_HANDLE) {
    vkf.vkDestroyCommandPool(g_vk.device, vk_pool, nullptr);
    vk_pool = VK_NULL_HANDLE;
  }
  Release(d3d11_done);
}

int64_t XrScreen::Impl::ChainFormat() const {
  if (format_index < 0) return static_cast<int64_t>(format);
  return vulkan ? static_cast<int64_t>(kFormats[format_index].vk)
                : static_cast<int64_t>(kFormats[format_index].dxgi);
}

// A swapchain of Vulkan gets the layout of a color attachment at each
// acquire, and its image must have it again at the release.
XrSwapchainUsageFlags XrScreen::Impl::ChainUsage() const {
  XrSwapchainUsageFlags usage =
      XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
  if (vulkan) usage |= XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
  return usage;
}

XrScreen::Impl::VkChain* XrScreen::Impl::FindChain(XrSwapchain chain) {
  if (chain == XR_NULL_HANDLE) return nullptr;
  for (int i = 0; i < kVkChains; ++i) {
    if (vk_chains[i].chain == chain) return &vk_chains[i];
  }
  return nullptr;
}

ID3D11Texture2D** XrScreen::Impl::ChainImages(XrSwapchain chain,
                                              uint32_t chain_width,
                                              uint32_t chain_height,
                                              uint32_t* count) {
  *count = 0;
  if (XR_FAILED(xrEnumerateSwapchainImages(chain, 0, count, nullptr)) ||
      *count == 0 || *count > 64) {
    log("  openxr: xrEnumerateSwapchainImages gave no images");
    *count = 0;
    return nullptr;
  }
  const uint32_t n = *count;
  ID3D11Texture2D** out = new ID3D11Texture2D*[n];
  memset(out, 0, sizeof(*out) * n);
  if (!vulkan) {
    std::vector<XrSwapchainImageD3D11KHR> raw(
        n, XrSwapchainImageD3D11KHR{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    xrEnumerateSwapchainImages(
        chain, n, count, reinterpret_cast<XrSwapchainImageBaseHeader*>(raw.data()));
    for (uint32_t i = 0; i < *count && i < n; ++i) out[i] = raw[i].texture;
    return out;
  }
  std::vector<XrSwapchainImageVulkan2KHR> raw(
      n, XrSwapchainImageVulkan2KHR{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR});
  xrEnumerateSwapchainImages(
      chain, n, count, reinterpret_cast<XrSwapchainImageBaseHeader*>(raw.data()));
  VkChain* slot = nullptr;
  for (int i = 0; i < kVkChains && slot == nullptr; ++i) {
    if (vk_chains[i].chain == XR_NULL_HANDLE) slot = &vk_chains[i];
  }
  if (slot == nullptr) {
    log("  openxr: no room for one more swapchain of Vulkan");
    delete[] out;
    *count = 0;
    return nullptr;
  }
  VkChain& c = *slot;
  memset(&c, 0, sizeof(c));
  c.chain = chain;
  c.count = *count;
  c.width = chain_width;
  c.height = chain_height;
  c.acquired = UINT32_MAX;
  c.images = new VkImage[n];
  c.relays = new Relay[n];
  memset(c.relays, 0, sizeof(Relay) * n);
  for (uint32_t i = 0; i < c.count; ++i) {
    c.images[i] = raw[i].image;
    // With no Direct3D 11 an image has no relay, and its entry is null.
    if (device == nullptr) continue;
    if (!MakeRelay(chain_width, chain_height, &c.relays[i])) {
      log("  openxr: the relay %u of a swapchain of %u by %u could not be "
          "made", i, chain_width, chain_height);
      for (uint32_t k = 0; k <= i; ++k) FreeRelay(&c.relays[k]);
      delete[] c.images;
      delete[] c.relays;
      memset(&c, 0, sizeof(c));
      delete[] out;
      *count = 0;
      return nullptr;
    }
    out[i] = c.relays[i].texture;
  }
  return out;
}

void XrScreen::Impl::DestroyChain(XrSwapchain* chain) {
  const XrSwapchain handle = *chain;
  if (handle == XR_NULL_HANDLE) return;
  VkChain* c = vulkan ? FindChain(handle) : nullptr;
  if (c != nullptr) {
    // No copy of the GPU may still read a relay or write an image.
    for (int i = 0; i < kVkSlots; ++i) {
      if (vk_fence[i] != VK_NULL_HANDLE) {
        vkf.vkWaitForFences(g_vk.device, 1, &vk_fence[i], VK_TRUE, UINT64_MAX);
      }
    }
    for (uint32_t i = 0; i < c->count; ++i) FreeRelay(&c->relays[i]);
    delete[] c->images;
    delete[] c->relays;
    memset(c, 0, sizeof(*c));
  }
  xrDestroySwapchain(handle);
  *chain = XR_NULL_HANDLE;
}

// A RELAY: a Direct3D 11 texture with an NT handle, and a Vulkan image on
// its memory. The image stays in the layout GENERAL, and the queue of the
// session takes it from the external owner (Direct3D 11) for each copy and
// gives it back after it.
bool XrScreen::Impl::MakeRelay(uint32_t relay_width, uint32_t relay_height,
                               Relay* relay) {
  const FormatChoice& choice = kFormats[format_index < 0 ? 0 : format_index];
  D3D11_TEXTURE2D_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.Width = relay_width;
  desc.Height = relay_height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = choice.dxgi_unorm;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  desc.MiscFlags =
      D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
  if (FAILED(device->CreateTexture2D(&desc, nullptr, &relay->texture))) {
    log("  openxr: the Direct3D 11 texture of a relay could not be made");
    return false;
  }
  IDXGIResource1* resource = nullptr;
  if (FAILED(relay->texture->QueryInterface(
          __uuidof(IDXGIResource1), reinterpret_cast<void**>(&resource)))) {
    log("  openxr: the texture of a relay has no IDXGIResource1");
    return false;
  }
  const HRESULT shared = resource->CreateSharedHandle(
      nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
      &relay->handle);
  resource->Release();
  if (FAILED(shared)) {
    log("  openxr: the NT handle of a relay could not be made (0x%08X)",
        shared);
    relay->handle = nullptr;
    return false;
  }

  VkExternalMemoryImageCreateInfo external = {
      VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
  external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
  VkImageCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  info.pNext = &external;
  info.imageType = VK_IMAGE_TYPE_2D;
  info.format = choice.vk_unorm;
  info.extent = {relay_width, relay_height, 1};
  info.mipLevels = 1;
  info.arrayLayers = 1;
  info.samples = VK_SAMPLE_COUNT_1_BIT;
  info.tiling = VK_IMAGE_TILING_OPTIMAL;
  info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkf.vkCreateImage(g_vk.device, &info, nullptr, &relay->image) !=
      VK_SUCCESS) {
    log("  openxr: the Vulkan image of a relay could not be made");
    return false;
  }
  VkMemoryRequirements req;
  vkf.vkGetImageMemoryRequirements(g_vk.device, relay->image, &req);
  VkMemoryWin32HandlePropertiesKHR handle_properties = {
      VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
  uint32_t bits = req.memoryTypeBits;
  if (vkf.vkGetMemoryWin32HandlePropertiesKHR(
          g_vk.device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT,
          relay->handle, &handle_properties) == VK_SUCCESS &&
      (bits & handle_properties.memoryTypeBits) != 0) {
    bits &= handle_properties.memoryTypeBits;
  }
  uint32_t type = UINT32_MAX;
  for (uint32_t i = 0; i < g_vk.memory.memoryTypeCount && type == UINT32_MAX;
       ++i) {
    if ((bits & (1u << i)) != 0 &&
        (g_vk.memory.memoryTypes[i].propertyFlags &
         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
      type = i;
    }
  }
  for (uint32_t i = 0; i < g_vk.memory.memoryTypeCount && type == UINT32_MAX;
       ++i) {
    if ((bits & (1u << i)) != 0) type = i;
  }
  VkMemoryDedicatedAllocateInfo dedicated = {
      VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
  dedicated.image = relay->image;
  VkImportMemoryWin32HandleInfoKHR import = {
      VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};
  import.pNext = &dedicated;
  import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
  import.handle = relay->handle;
  VkMemoryAllocateInfo allocate = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocate.pNext = &import;
  allocate.allocationSize = req.size;
  allocate.memoryTypeIndex = type;
  if (type == UINT32_MAX ||
      vkf.vkAllocateMemory(g_vk.device, &allocate, nullptr, &relay->memory) !=
          VK_SUCCESS) {
    log("  openxr: the memory of a relay could not be imported into Vulkan");
    relay->memory = VK_NULL_HANDLE;
    return false;
  }
  if (vkf.vkBindImageMemory(g_vk.device, relay->image, relay->memory, 0) !=
      VK_SUCCESS) {
    log("  openxr: the memory of a relay could not be bound");
    return false;
  }
  // The first layout, and the image goes to its external owner.
  VkCommandBuffer cb = BeginCopy();
  if (cb == VK_NULL_HANDLE) return false;
  VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  barrier.srcQueueFamilyIndex = g_vk.family;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
  barrier.image = relay->image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkf.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr,
                           0, nullptr, 1, &barrier);
  return EndCopy(cb);
}

void XrScreen::Impl::FreeRelay(Relay* relay) {
  if (relay->image != VK_NULL_HANDLE) {
    vkf.vkDestroyImage(g_vk.device, relay->image, nullptr);
  }
  if (relay->memory != VK_NULL_HANDLE) {
    vkf.vkFreeMemory(g_vk.device, relay->memory, nullptr);
  }
  if (relay->handle != nullptr) CloseHandle(relay->handle);
  Release(relay->texture);
  memset(relay, 0, sizeof(*relay));
}

XrResult XrScreen::Impl::AcquireImage(XrSwapchain chain, uint32_t* image) {
  XrSwapchainImageAcquireInfo acquire = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  XrResult result;
  {
    QueueGuard guard(vulkan);
    result = xrAcquireSwapchainImage(chain, &acquire, image);
  }
  if (XR_SUCCEEDED(result) && vulkan) {
    VkChain* c = FindChain(chain);
    if (c != nullptr) c->acquired = *image;
  }
  return result;
}

XrResult XrScreen::Impl::ReleaseImage(XrSwapchain chain, bool push) {
  if (vulkan) {
    VkChain* c = FindChain(chain);
    if (c != nullptr && push && device != nullptr &&
        c->acquired < c->count) {
      if (!Push(*c, c->acquired) && !vk_said_push_failed) {
        vk_said_push_failed = true;
        log("  openxr: a copy of a relay into its swapchain image failed");
      }
    }
    if (c != nullptr) c->acquired = UINT32_MAX;
  }
  XrSwapchainImageReleaseInfo release = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  QueueGuard guard(vulkan);
  return xrReleaseSwapchainImage(chain, &release);
}

VkCommandBuffer XrScreen::Impl::BeginCopy() {
  if (vk_pool == VK_NULL_HANDLE) return VK_NULL_HANDLE;
  const int slot = vk_slot;
  vkf.vkWaitForFences(g_vk.device, 1, &vk_fence[slot], VK_TRUE, UINT64_MAX);
  vkf.vkResetFences(g_vk.device, 1, &vk_fence[slot]);
  vkf.vkResetCommandBuffer(vk_cb[slot], 0);
  VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (vkf.vkBeginCommandBuffer(vk_cb[slot], &begin) != VK_SUCCESS) {
    return VK_NULL_HANDLE;
  }
  return vk_cb[slot];
}

// Submits the command buffer of BeginCopy with the fence of its slot. The
// next BeginCopy takes the next slot.
bool XrScreen::Impl::EndCopy(VkCommandBuffer cb) {
  const int slot = vk_slot;
  vk_slot = (vk_slot + 1) % kVkSlots;
  if (vkf.vkEndCommandBuffer(cb) != VK_SUCCESS) {
    // The fence must be signaled again for the next use of the slot.
    VkSubmitInfo empty = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    QueueGuard guard(true);
    vkf.vkQueueSubmit(g_vk.queue, 1, &empty, vk_fence[slot]);
    return false;
  }
  VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cb;
  QueueGuard guard(true);
  return vkf.vkQueueSubmit(g_vk.queue, 1, &submit, vk_fence[slot]) ==
         VK_SUCCESS;
}

// The relay of `image` into the image. Vulkan has no wait for the work of
// Direct3D 11, so the CPU waits until Direct3D 11 has written the relay.
bool XrScreen::Impl::Push(VkChain& chain, uint32_t image) {
  LARGE_INTEGER start, done;
  QueryPerformanceCounter(&start);
  if (d3d11_done == nullptr) return false;
  context->End(d3d11_done);
  context->Flush();
  BOOL finished = FALSE;
  while (context->GetData(d3d11_done, &finished, sizeof(finished), 0) ==
         S_FALSE) {
    SwitchToThread();
  }
  VkCommandBuffer cb = BeginCopy();
  if (cb == VK_NULL_HANDLE) return false;
  const Relay& relay = chain.relays[image];
  VkImageMemoryBarrier before[2] = {{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER},
                                    {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}};
  before[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  before[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
  before[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
  before[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
  before[0].dstQueueFamilyIndex = g_vk.family;
  before[0].image = relay.image;
  before[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  before[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  before[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  before[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  before[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  before[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  before[1].image = chain.images[image];
  before[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkf.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                           nullptr, 2, before);
  VkImageCopy region = {};
  region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.extent = {chain.width, chain.height, 1};
  vkf.vkCmdCopyImage(cb, relay.image, VK_IMAGE_LAYOUT_GENERAL,
                     chain.images[image], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     1, &region);
  VkImageMemoryBarrier after[2] = {{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER},
                                   {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}};
  after[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  after[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
  after[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
  after[0].srcQueueFamilyIndex = g_vk.family;
  after[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
  after[0].image = relay.image;
  after[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  after[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  after[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  after[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  after[1].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  after[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  after[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  after[1].image = chain.images[image];
  after[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkf.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                           nullptr, 2, after);
  const bool ok = EndCopy(cb);
  QueryPerformanceCounter(&done);
  vk_push_ticks += static_cast<uint64_t>(done.QuadPart - start.QuadPart);
  ++vk_pushes;
  return ok;
}

void XrScreen::Impl::FreeStages() {
  for (int i = 0; i < kVkSlots; ++i) {
    Stage& stage = vk_stage[i];
    if (stage.buffer != VK_NULL_HANDLE) {
      vkf.vkDestroyBuffer(g_vk.device, stage.buffer, nullptr);
    }
    // The free of the memory also ends its map.
    if (stage.memory != VK_NULL_HANDLE) {
      vkf.vkFreeMemory(g_vk.device, stage.memory, nullptr);
    }
    memset(&stage, 0, sizeof(stage));
  }
}

// THE PIXELS OF THE CPU. The CPU writes the rect into the buffer of the slot
// of this copy, and the GPU copies the buffer into the image. The image is
// in the layout of a color attachment after the acquire, and it has that
// layout again before the release. The rest of the image keeps its pixels.
bool XrScreen::Impl::UploadVulkan(XrSwapchain chain, uint32_t image,
                                  const uint8_t* pixels, uint32_t pitch,
                                  int32_t x, int32_t y, int32_t w, int32_t h,
                                  bool swap_red_blue) {
  LARGE_INTEGER start, done;
  QueryPerformanceCounter(&start);
  VkChain* c = FindChain(chain);
  if (c == nullptr || image >= c->count || pixels == nullptr || w <= 0 ||
      h <= 0) {
    return false;
  }
  // BeginCopy waits for the fence of this slot, so the GPU does not read
  // its buffer any more.
  const int slot = vk_slot;
  VkCommandBuffer cb = BeginCopy();
  if (cb == VK_NULL_HANDLE) return false;
  Stage& stage = vk_stage[slot];
  const VkDeviceSize bytes = static_cast<VkDeviceSize>(w) * h * 4;
  if (stage.size < bytes) {
    if (stage.buffer != VK_NULL_HANDLE) {
      vkf.vkDestroyBuffer(g_vk.device, stage.buffer, nullptr);
    }
    if (stage.memory != VK_NULL_HANDLE) {
      vkf.vkFreeMemory(g_vk.device, stage.memory, nullptr);
    }
    memset(&stage, 0, sizeof(stage));
    VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = bytes;
    info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bool ok = vkf.vkCreateBuffer(g_vk.device, &info, nullptr, &stage.buffer) ==
              VK_SUCCESS;
    VkMemoryRequirements req = {};
    if (ok) vkf.vkGetBufferMemoryRequirements(g_vk.device, stage.buffer, &req);
    uint32_t type = UINT32_MAX;
    const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; ok && i < g_vk.memory.memoryTypeCount; ++i) {
      if ((req.memoryTypeBits & (1u << i)) != 0 &&
          (g_vk.memory.memoryTypes[i].propertyFlags & want) == want) {
        type = i;
        break;
      }
    }
    VkMemoryAllocateInfo allocate = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = req.size;
    allocate.memoryTypeIndex = type;
    ok = ok && type != UINT32_MAX &&
         vkf.vkAllocateMemory(g_vk.device, &allocate, nullptr,
                              &stage.memory) == VK_SUCCESS &&
         vkf.vkBindBufferMemory(g_vk.device, stage.buffer, stage.memory, 0) ==
             VK_SUCCESS;
    void* mapped = nullptr;
    ok = ok && vkf.vkMapMemory(g_vk.device, stage.memory, 0, VK_WHOLE_SIZE, 0,
                               &mapped) == VK_SUCCESS;
    if (!ok) {
      if (stage.buffer != VK_NULL_HANDLE) {
        vkf.vkDestroyBuffer(g_vk.device, stage.buffer, nullptr);
      }
      if (stage.memory != VK_NULL_HANDLE) {
        vkf.vkFreeMemory(g_vk.device, stage.memory, nullptr);
      }
      memset(&stage, 0, sizeof(stage));
      EndCopy(cb);
      return false;
    }
    stage.mapped = static_cast<uint8_t*>(mapped);
    stage.size = bytes;
  }
  const size_t row_bytes = static_cast<size_t>(w) * 4;
  for (int32_t row = 0; row < h; ++row) {
    const uint8_t* in = pixels + static_cast<size_t>(y + row) * pitch +
                        static_cast<size_t>(x) * 4;
    uint8_t* out = stage.mapped + static_cast<size_t>(row) * row_bytes;
    if (!swap_red_blue) {
      memcpy(out, in, row_bytes);
      continue;
    }
    for (int32_t i = 0; i < w; ++i, in += 4, out += 4) {
      out[0] = in[2];
      out[1] = in[1];
      out[2] = in[0];
      out[3] = in[3];
    }
  }
  VkImageMemoryBarrier before = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  before.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  before.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  before.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  before.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  before.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  before.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  before.image = c->images[image];
  before.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkf.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                           nullptr, 1, &before);
  VkBufferImageCopy region = {};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageOffset = {x, y, 0};
  region.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
  vkf.vkCmdCopyBufferToImage(cb, stage.buffer, c->images[image],
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  VkImageMemoryBarrier after = before;
  after.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  after.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  after.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  after.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  vkf.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                           nullptr, 1, &after);
  const bool ok = EndCopy(cb);
  QueryPerformanceCounter(&done);
  vk_upload_ticks += static_cast<uint64_t>(done.QuadPart - start.QuadPart);
  ++vk_uploads;
  if (!ok && !vk_said_upload_failed) {
    vk_said_upload_failed = true;
    log("  openxr: a copy of the pixels of the CPU into a swapchain image "
        "failed");
  }
  return ok;
}

// One layer of a Vulkan image into the next image of a swapchain: acquire,
// wait, one copy on the GPU, release. The work that drew the image is in the
// queue before this copy, and the barrier waits for it.
XrResult XrScreen::Impl::CopyVulkan(XrSwapchain chain, uint64_t image,
                                    uint32_t layer) {
  VkChain* c = FindChain(chain);
  if (c == nullptr || image == 0) return XR_ERROR_VALIDATION_FAILURE;
  uint32_t index = 0;
  XrResult result = AcquireImage(chain, &index);
  if (XR_FAILED(result)) return result;
  XrSwapchainImageWaitInfo image_wait = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  image_wait.timeout = XR_INFINITE_DURATION;
  result = xrWaitSwapchainImage(chain, &image_wait);
  if (XR_FAILED(result)) return result;
  VkCommandBuffer cb = index < c->count ? BeginCopy() : VK_NULL_HANDLE;
  if (cb != VK_NULL_HANDLE) {
    const VkImage source = (VkImage)image;  // a handle of 64 bits
    VkImageMemoryBarrier before[2] = {{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER},
                                      {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}};
    before[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    before[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    before[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    before[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    before[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before[0].image = source;
    before[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, layer, 1};
    before[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    before[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    before[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    before[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before[1].image = c->images[index];
    before[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkf.vkCmdPipelineBarrier(cb,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 2, before);
    VkImageCopy region = {};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.extent = {c->width, c->height, 1};
    vkf.vkCmdCopyImage(cb, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       c->images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &region);
    const int eye = chain == swapchain[0] ? 0 : chain == swapchain[1] ? 1 : -1;
    const bool capture = capture_left > 0 && eye >= 0;
    VkImageMemoryBarrier after[2] = {{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER},
                                     {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}};
    after[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    after[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    after[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    after[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    after[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    after[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    after[0].image = source;
    after[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, layer, 1};
    after[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    after[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    after[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    after[1].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    after[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    after[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    after[1].image = c->images[index];
    after[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkf.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr,
                             0, nullptr, 2, after);
    const int slot = vk_slot;
    if (EndCopy(cb)) ++vk_copies;
    if (capture) {
      vkf.vkWaitForFences(g_vk.device, 1, &vk_fence[slot], VK_TRUE,
                          UINT64_MAX);
      SaveChainPicture(c, index, eye);
      --capture_left;
    }
  }
  return ReleaseImage(chain, false);
}

// THE TEST AID of CaptureSwapchainNext: the swapchain image `index`, after
// its copy, into a buffer of the CPU and into a BMP file. With a wait for
// the GPU.
void XrScreen::Impl::SaveChainPicture(VkChain* c, uint32_t index, int eye) {
  const VkDeviceSize bytes = static_cast<VkDeviceSize>(c->width) * c->height * 4;
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  info.size = bytes;
  info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkf.vkCreateBuffer(g_vk.device, &info, nullptr, &buffer) != VK_SUCCESS) {
    return;
  }
  VkMemoryRequirements req = {};
  vkf.vkGetBufferMemoryRequirements(g_vk.device, buffer, &req);
  uint32_t type = UINT32_MAX;
  const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  for (uint32_t i = 0; i < g_vk.memory.memoryTypeCount; ++i) {
    if ((req.memoryTypeBits & (1u << i)) != 0 &&
        (g_vk.memory.memoryTypes[i].propertyFlags & want) == want) {
      type = i;
      break;
    }
  }
  VkMemoryAllocateInfo allocate = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocate.allocationSize = req.size;
  allocate.memoryTypeIndex = type;
  void* mapped = nullptr;
  const bool made =
      type != UINT32_MAX &&
      vkf.vkAllocateMemory(g_vk.device, &allocate, nullptr, &memory) ==
          VK_SUCCESS &&
      vkf.vkBindBufferMemory(g_vk.device, buffer, memory, 0) == VK_SUCCESS &&
      vkf.vkMapMemory(g_vk.device, memory, 0, VK_WHOLE_SIZE, 0, &mapped) ==
          VK_SUCCESS;
  VkCommandBuffer cb = made ? BeginCopy() : VK_NULL_HANDLE;
  if (cb != VK_NULL_HANDLE) {
    // The image is in the layout of a color attachment after the copy.
    VkImageMemoryBarrier to_read = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    to_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    to_read.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    to_read.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    to_read.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    to_read.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_read.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_read.image = c->images[index];
    to_read.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkf.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &to_read);
    VkBufferImageCopy copy = {};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {c->width, c->height, 1};
    vkf.vkCmdCopyImageToBuffer(cb, c->images[index],
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1,
                               &copy);
    VkImageMemoryBarrier back = to_read;
    back.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    back.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    back.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    back.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    vkf.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &back);
    const int slot = vk_slot;
    if (EndCopy(cb)) {
      vkf.vkWaitForFences(g_vk.device, 1, &vk_fence[slot], VK_TRUE,
                          UINT64_MAX);
      const VkFormat chain_format =
          kFormats[format_index < 0 ? 0 : format_index].vk_unorm;
      const bool bgra = chain_format == VK_FORMAT_B8G8R8A8_UNORM;
      char path[MAX_PATH];
      GetModuleFileNameA(nullptr, path, MAX_PATH);
      char* slash = strrchr(path, '\\');
      if (slash != nullptr) slash[1] = 0;
      char name[64];
      _snprintf_s(name, sizeof(name), _TRUNCATE, "bwvr-xr-%s-%05llu.bmp",
                  eye == 0 ? "left" : "right", capture_name);
      strcat_s(path, name);
      const uint8_t* src = static_cast<const uint8_t*>(mapped);
      int low = 255, high = 0;
      FILE* out = nullptr;
      if (fopen_s(&out, path, "wb") == 0 && out != nullptr) {
        const uint32_t w = c->width, h = c->height;
        const uint32_t row = (w * 3 + 3) & ~3u;
        const uint32_t data = row * h;
        uint8_t header[54] = {'B', 'M'};
        const uint32_t file = 54 + data, offset = 54, size40 = 40;
        const uint32_t planes_bits = 1 | (24 << 16);
        memcpy(header + 2, &file, 4);
        memcpy(header + 10, &offset, 4);
        memcpy(header + 14, &size40, 4);
        memcpy(header + 18, &w, 4);
        memcpy(header + 22, &h, 4);
        memcpy(header + 26, &planes_bits, 4);
        memcpy(header + 34, &data, 4);
        fwrite(header, 1, 54, out);
        std::vector<uint8_t> line(row, 0);
        for (uint32_t y = 0; y < h; ++y) {
          const uint8_t* p = src + static_cast<size_t>(h - 1 - y) * w * 4;
          for (uint32_t x = 0; x < w; ++x) {
            line[x * 3 + 0] = p[x * 4 + (bgra ? 0 : 2)];
            line[x * 3 + 1] = p[x * 4 + 1];
            line[x * 3 + 2] = p[x * 4 + (bgra ? 2 : 0)];
            const int a = p[x * 4 + 3];
            if (a < low) low = a;
            if (a > high) high = a;
          }
          fwrite(line.data(), 1, row, out);
        }
        fclose(out);
        log("  openxr: the swapchain image of the %s eye is in %s (format %d, "
            "alpha %d to %d)",
            eye == 0 ? "left" : "right", path, static_cast<int>(chain_format), low,
            high);
      }
    }
  }
  if (memory != VK_NULL_HANDLE) vkf.vkFreeMemory(g_vk.device, memory, nullptr);
  if (buffer != VK_NULL_HANDLE) vkf.vkDestroyBuffer(g_vk.device, buffer, nullptr);
}

void XrScreen::UseVulkan(bool on) { impl_->vulkan = on; }

void XrScreen::UseD3D11(bool on) { impl_->d3d11 = on; }

void XrScreen::CaptureSwapchainNext(uint64_t name) {
  impl_->capture_left = eyes_ > 1 ? 2 : 1;
  impl_->capture_name = name;
}

void XrScreen::WantEyeGaze(bool on) { impl_->gaze_wanted = on; }

bool XrScreen::EyeGazeSupported() const { return impl_->gaze_supported; }

bool XrScreen::EyeGaze(float direction[3]) const {
  if (!impl_->gaze_valid) return false;
  for (int i = 0; i < 3; ++i) direction[i] = impl_->gaze_direction[i];
  return true;
}

void XrScreen::SetVulkanUpload(bool on) { impl_->vk_upload = on; }

void XrScreen::SetHoldMs(int ms) {
  InterlockedExchange(&impl_->hold_ms, ms > 0 ? (ms < 1000 ? ms : 1000) : 0);
}

int XrScreen::HoldMs() const { return static_cast<int>(impl_->hold_ms); }

void XrScreen::SetRefreshRate(int hz) {
  impl_->refresh_ask = refresh_rate_math::ClampAsk(hz);
}

double XrScreen::DisplaySeconds() const {
  return static_cast<double>(impl_->display_seconds_ns) * 1e-9;
}

bool XrScreen::vulkan_upload() const {
  return impl_->vk_upload || (impl_->vulkan && impl_->device == nullptr);
}

bool XrScreen::vulkan() const { return impl_->vulkan; }

bool XrScreen::VulkanFailed() const { return impl_->vk_failed; }

bool XrScreen::VulkanInfo(XrVulkan* out) const {
  const Impl& s = *impl_;
  if (!s.vulkan || !g_vk.made || !running_) return false;
  out->get_instance_proc_addr =
      reinterpret_cast<void*>(vkf.vkGetInstanceProcAddr);
  out->instance = g_vk.instance;
  out->physical = g_vk.physical;
  out->device = g_vk.device;
  out->queue_family = g_vk.family;
  out->queue_index = 0;
  out->max_anisotropy = g_vk.max_anisotropy;
  out->eye_format = static_cast<uint32_t>(
      kFormats[s.format_index < 0 ? 0 : s.format_index].vk_unorm);
  out->shading_rate = g_vk.shading_rate;
  out->window_present = g_vk.window_present;
  return true;
}

void XrScreen::LockQueue() { AcquireSRWLockExclusive(&g_queue_lock); }

void XrScreen::UnlockQueue() { ReleaseSRWLockExclusive(&g_queue_lock); }

uint32_t XrScreen::width() const { return impl_->width; }

uint32_t XrScreen::height() const { return impl_->height; }

void XrScreen::SetEyeVulkan(int index, uint64_t image, uint32_t layer) {
  Impl& s = *impl_;
  if (!s.vulkan || !s.frame_open || !should_render_ || image == 0) return;
  const int eye = (index >= 1 && eyes_ > 1) ? 1 : 0;
  if (s.swapchain[eye] == XR_NULL_HANDLE) return;
  if (s.Check(s.CopyVulkan(s.swapchain[eye], image, layer),
              "the copy of a Vulkan eye picture")) {
    s.acquired[eye] = 0;
  }
}

}  // namespace bwvr
