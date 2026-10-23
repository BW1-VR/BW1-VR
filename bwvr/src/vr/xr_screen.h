// A screen in VR, fed one or two images at a time.
//
// With one eye this is the picture the game already draws, on a flat screen
// floating in front of the player. With two eyes it is the same screen, but
// each eye sees the scene rendered from its own camera, so the island has
// real depth.
//
// The frame is explicit, because the mod needs the headset pose before the
// game renders anything:
//
//   XrScreen screen;
//   screen.Start(1280, 720, 2, Log);
//   ...
//   while (screen.BeginFrame()) {        // polls events, waits for the runtime
//     ... render the left eye ...
//     screen.SetEyeImage(0, pixels, pitch);
//     ... render the right eye ...
//     screen.SetEyeImage(1, pixels, pitch);
//     screen.EndFrame();
//   }
//
// BeginFrame blocks until the runtime is ready for the next frame, so the
// game's frame rate becomes the headset's frame rate.

#pragma once

#include <stdint.h>

namespace bwvr {

typedef void (*LogFn)(const char* format, ...);

// What the runtime predicts for one eye at the next display time. The mod
// uses this to place the camera. The interpupillary distance is the
// difference between the two positions.
struct EyeView {
  float position[3];      // meters, in the reference space
  float orientation[4];   // quaternion, x y z w
  float fov_left;         // radians, signed, so the frustum can be asymmetric
  float fov_right;
  float fov_up;
  float fov_down;
  bool valid;
};

// One hand, as the runtime reported it at the display time of the last
// BeginFrame. Poses are in meters, in the same LOCAL space as EyeView.
struct HandInput {
  bool active;               // bound, and the session has input focus
  bool aim_valid;            // position and orientation both valid
  bool grip_valid;
  bool aim_tracked;          // tracked, not inferred
  bool grip_tracked;         // the same for the grip pose
  float aim_position[3];
  float aim_orientation[4];  // x y z w
  float grip_position[3];
  float grip_orientation[4];
  float trigger;             // 0 to 1
  float squeeze;             // 0 to 1
  float stick[2];            // -1 to 1, x right, y forward
  bool stick_click;
  bool primary;              // A or X
  bool secondary;            // B or Y
  bool menu;
  uint64_t serial;           // one more on every sync
};

// One interface panel for this frame: a quad layer in the LOCAL space that
// shows a rect of the panel swapchain. See docs/ui-layer.md.
// The quads of the panel: the menu, the help and the Creature panel.
const int kPanelQuads = 3;

struct PanelQuad {
  bool visible;
  float position[3];     // meters
  float orientation[4];  // x y z w, the front of the quad is its +z side
  float width;           // meters
  float height;
  int32_t rect[4];       // x, y, width, height in panel pixels
};

// THE CARDS: small quad layers in the LOCAL space, each with a picture of its
// own that the mod draws on the CPU. A quad in the room shows once to each
// eye at one place, so what is on a card cannot be double.
//
//   kCardHintPointing  the hints and prompts of the hand that points, beside
//                      its wrist (docs/controller-hints.md, "The hint card")
//   kCardHintOther     the prompts of the other hand, beside its wrist
//   kCardSubtitles     the subtitles and the help text, fixed in the room
//                      (docs/ui-layer.md, "The subtitle card")
//   kCardRing          the ring of the hand that does not point, fixed in the
//                      room while it is open (docs/controllers.md, "The ring")
const int kCardHintPointing = 0;
const int kCardHintOther = 1;
const int kCardSubtitles = 2;
const int kCardRing = 3;
const int kCards = 4;

// The picture of a hint card.
const uint32_t kHintCardWidth = 768;
const uint32_t kHintCardHeight = 320;

struct CardLayer {
  bool visible;
  // texture_width by texture_height pixels, BGRA, top row first, with the
  // color multiplied by the alpha. The pointer must stay valid until
  // EndFrame returns.
  const uint8_t* pixels;
  uint32_t texture_width;   // the swapchain of the card gets this size
  uint32_t texture_height;
  int32_t rect[4];       // the part that shows: x, y, width, height
  float position[3];     // meters, the center
  float orientation[4];  // x y z w, the front of the quad is its +z side
  float width;           // meters
  float height;
};

// THE CUT SCENE WINDOW of one frame (docs/cutscene-window.md). All poses are
// in the LOCAL space, in meters.
//
//   screen  The eye pictures of this frame go out as one quad for each eye
//           at this pose, as the stereoscopic window does, and not as a
//           projection layer. Past the quad the view is black. The subtitle
//           card of the frame goes over the two quads.
//   crop    Each eye picture loses this many columns of pixels: the left
//           eye at its left side and the right eye at its right side. The
//           quad is the opening: `width` is the width of what is left. So
//           the two pictures are `crop` pixels apart on one rectangle.
//   edge    Each eye picture also loses this many columns at each side of
//           what is left after the crop (cutscene_edge_columns). `width` is
//           the width of what is left after the crop and the edge.
//   mask   The frame keeps its projection layer. Four black quads go over
//           all its layers, as walls around an opening. `alpha` is how black
//           they are, 0 to 1.
struct WindowLayer {
  bool screen;
  bool mask;
  int32_t crop;
  int32_t edge;
  float position[3];     // the center of the screen
  float orientation[4];  // x y z w, of the screen and of the walls
  float width;           // the screen
  float height;
  float wall_position[4][3];  // the center of each wall
  float wall_size[4][2];      // its width and height
  float alpha;
};

// THE VULKAN OF A SESSION WITH VULKAN (XrScreen::UseVulkan). The runtime
// makes the instance and the device. They stay until the end of the
// process, because the Vulkan renderer of the mod (vk_core.cpp) draws with
// them. The handles are the ones of vulkan.h, as void* and uint64_t, so this
// header needs no vulkan.h.
struct XrVulkan {
  void* get_instance_proc_addr;  // PFN_vkGetInstanceProcAddr
  void* instance;                // VkInstance
  void* physical;                // VkPhysicalDevice
  void* device;                  // VkDevice
  uint32_t queue_family;
  uint32_t queue_index;
  // The largest anisotropy of a sampler, or 0 when the device has no
  // anisotropic filter.
  float max_anisotropy;
  // The VkFormat of 8 bits UNORM with the order of the bytes of the eye
  // swapchains. An image in this format goes into a swapchain image by a
  // copy of its bytes (SetEyeVulkan).
  uint32_t eye_format;
  // The device has VK_KHR_fragment_shading_rate with a shading rate
  // attachment, for foveated rendering.
  bool shading_rate;
  // The instance has VK_KHR_surface and VK_KHR_win32_surface, and the device
  // VK_KHR_swapchain, so the mod can present to the window of the game.
  bool window_present;
};

// A LATE FRAME: a frame whose eye pictures are in textures that the GPU has
// not finished. See XrScreen::EndFrameLate.
struct LateFrame {
  // As for SetEyeTexture. Null: the eye has its picture, or it gets none.
  void* eye_texture[2];
  // As for SetEyeVulkan, in a session with Vulkan: a VkImage and its layer.
  // 0: the eye takes eye_texture.
  uint64_t eye_vulkan[2];
  uint32_t eye_layer[2];
  // As for SetPanelTexture. Null: the frame has no panel.
  void* panel_texture;
  // As for SetPanelVulkan, in a session with Vulkan. 0: the panel takes
  // panel_texture.
  uint64_t panel_vulkan;
  // Returns when the GPU has finished the textures. It runs on the thread
  // that sends the frame, and it must not write a line of the log. Null: the
  // textures are finished.
  void (*wait)();
};

class RemoteScreen;

class XrScreen {
 public:
  // Select before startup. 0 uses the local runtime, 1 uses the x64 host.
  static void ConfigureBridge(bool enabled);
  XrScreen();
  ~XrScreen();

  // eyes is 1 for a mono screen or 2 for stereo. width and height are the size
  // of the images that will be handed to SetEyeImage. Returns false and logs a
  // reason if anything fails, and the caller can carry on without VR.
  //
  // Start does the four parts below in one call, and Stop after a part that
  // fails. A caller can also call the parts one at a time. The first three
  // need no device of the game, so they can run on any thread. OpenXR lets
  // each call come from any thread, when two threads do not use one handle
  // at one time.
  bool Start(uint32_t width, uint32_t height, int eyes, LogFn log);

  // Makes the instance, and sets the log. With an instance, it only sets
  // the log. False: no instance, and the log has the reason.
  bool StartInstance(LogFn log);
  bool has_instance() const;

  // Asks the runtime for the head mounted display. 1: the system is there.
  // 0: the runtime has no head mounted display now
  // (XR_ERROR_FORM_FACTOR_UNAVAILABLE). The instance stays, and a later call
  // can find it. -1: a different fault, and only Stop is left to do.
  int StartSystem();

  // Writes the name and the size of the headset that StartSystem found to
  // the log.
  void LogSystem();

  // The device of the session on the adapter of the runtime (Direct3D 11,
  // or Vulkan with UseVulkan), the session, the spaces, the controllers and
  // the swapchains. After StartSystem gave 1. False: only Stop is left to
  // do, and VulkanFailed says if the Vulkan part failed.
  bool StartSession(uint32_t width, uint32_t height, int eyes);

  // THE GRAPHICS API OF THE SESSION. Call it before Start. false (the
  // default): a session with Direct3D 11. true: a session with Vulkan
  // (XR_KHR_vulkan_enable2). Each other call stays the same: a texture of
  // Direct3D 11 and the pixels of the CPU go through a Direct3D 11 texture
  // that Vulkan shares, and SetEyeVulkan copies a Vulkan image straight into
  // the swapchain.
  void UseVulkan(bool on);
  bool vulkan() const;
  // THE DIRECT3D 11 OF A SESSION WITH VULKAN. Call it before Start. true
  // (the default): the session has a Direct3D 11 device on its GPU, and
  // each swapchain image has a relay. false: the session has no Direct3D 11
  // device and no relay. Then each picture goes with Vulkan: SetEyeVulkan,
  // SetPanelVulkan and the pixels of the CPU. SetEyeTexture, SetPanelTexture
  // and SubmitTexture do nothing, D3D11Device is null, and SetVulkanUpload
  // has no effect.
  void UseD3D11(bool on);
  // True after a Start with Vulkan that failed at the Vulkan part, and not
  // at the runtime or the headset. Then a session with Direct3D 11 can
  // still start.
  bool VulkanFailed() const;
  // The Vulkan of the session. False before a Start with Vulkan succeeded.
  bool VulkanInfo(XrVulkan* out) const;
  // EYE TRACKING for foveated rendering (XR_EXT_eye_gaze_interaction).
  // WantEyeGaze(true) before Start asks for the extension when the runtime
  // lists it. EyeGazeSupported is true when the headset has an eye tracker.
  // EyeGaze gives the direction of the gaze in the space of the head (x
  // right, y up, -z forward) at the display time of the frame, false when
  // the eye tracker gave none for this frame.
  void WantEyeGaze(bool on);
  bool EyeGazeSupported() const;
  bool EyeGaze(float direction[3]) const;
  // The queue of the session. The runtime uses it in xrBeginFrame,
  // xrEndFrame and in the acquire and the release of a swapchain image, so
  // each other submit to it must hold this lock.
  static void LockQueue();
  static void UnlockQueue();

  // One eye from a Vulkan image of the session device, in a session with
  // Vulkan: one layer of `image` (a VkImage), of the size the screen was
  // started with, in the format XrVulkan::eye_format and in the layout
  // VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL. The work that draws the image
  // must be in the queue before this call. The image keeps its layout.
  void SetEyeVulkan(int index, uint64_t image, uint32_t layer);

  // THE PIXELS OF THE CPU in a session with Vulkan (xr_vulkan_upload). On:
  // an eye picture of SetEyeImage or SubmitBgra, a card and the walls of the
  // cut scene window go from the CPU into the swapchain image with a copy of
  // Vulkan, with no relay and no wait for Direct3D 11. Off: they go into the
  // relay with Direct3D 11, and the release copies the relay. The change
  // takes effect at the next write.
  void SetVulkanUpload(bool on);
  // THE HOLD (xr_hold_ms). ms > 0: when the game gives the runtime no frame
  // for ms milliseconds, a second thread sends the layers of the last frame
  // again, with the poses of their pictures, at the rate of the headset,
  // until the game sends a frame. A frame of the game that is open three
  // times as long is ended with those layers, and its pictures go in a new
  // frame. 0: the headset gets no frame while the game gives none.
  void SetHoldMs(int ms);
  int HoldMs() const;
  // THE REFRESH RATE OF THE HEADSET (refresh_rate_hz,
  // XR_FB_display_refresh_rate). Before StartSession. 0: the mod asks for
  // no rate. A number: the mod asks for that rate when the headset offers
  // it, else for the nearest higher one. -1: the highest rate that the
  // headset offers. With each value the log has the rates of the headset
  // and the rate now when the runtime has the extension.
  void SetRefreshRate(int hz);
  // The display time that the runtime predicted for the frame of the last
  // BeginFrame, in seconds. 0 before the first frame. A move of the view by
  // the time between two of these has the rate of the display.
  double DisplaySeconds() const;
  bool vulkan_upload() const;

  // The size of each eye picture, as Start had it.
  uint32_t width() const;
  uint32_t height() const;

  // Waits for the runtime and starts a frame. Returns false when the session
  // has ended and the caller should stop. When it returns true but
  // should_render() is false, the runtime does not want this frame drawn and
  // the caller can skip straight to EndFrame.
  bool BeginFrame();

  bool should_render() const { return should_render_; }

  // Where the runtime wants each eye at the display time this frame. Valid
  // only between BeginFrame and EndFrame.
  const EyeView& eye(int index) const { return views_[index ? 1 : 0]; }

  // Uploads one eye's image. pixels is BGRA or BGRX, top row first, and pitch
  // is the byte distance between rows. With one eye, index is always 0.
  //
  // This is the slow path. It exists because system DirectDraw keeps the
  // finished frame where only the CPU can read it, so the pixels have to
  // travel out of graphics memory and back in again. Prefer SetEyeTexture.
  void SetEyeImage(int index, const void* pixels, int pitch);

  // Uploads one eye from a texture the GPU already holds. texture is an
  // ID3D11Texture2D that the caller opened on the device this class returns
  // from D3D11Device(). Nothing is read or written by the CPU, so the cost
  // does not grow with resolution.
  //
  // The texture must be the size the screen was started with, and its format
  // must match the swapchain format or share its typeless family.
  void SetEyeTexture(int index, void* texture);

  // The Direct3D 11 device the OpenXR session was created with, as an
  // ID3D11Device. A backend bridge opens its shared textures here, so that
  // both sides of the copy live on one device. Null until Start succeeds.
  void* D3D11Device() const;

  // WHAT THE HEADSET ITSELF ASKS FOR, per eye, in pixels.
  //
  // xrEnumerateViewConfigurationViews needs only an instance and a system, so
  // this is known before any swapchain exists. The launcher offers a
  // percentage of it instead of a list of resolutions picked by hand, because
  // a number the headset chose is right for every headset and a list is right
  // for one.
  //
  // False before Start has reached the system.
  bool RecommendedSize(uint32_t* width, uint32_t* height) const;

  // Swap the log sink after Start or between its parts. The retries log to
  // nowhere so a message
  // every three seconds does not bury the file, and without this the attempt
  // that SUCCEEDS keeps the quiet sink for the whole session and loses every
  // openxr line, including the eye angles and the measured separation.
  void SetLog(LogFn log);

  // The swapchain's DXGI format. A bridge checks this before copying into it,
  // because CopyResource only works between formats in one typeless family.
  uint32_t SwapchainFormat() const;

  // THE INTERFACE PANEL. Call EnablePanel before Start, and Start makes a
  // third swapchain of the render size for the interface. Each frame,
  // SetPanelTexture copies the panel like SetEyeTexture copies an eye, and
  // SetPanelQuads says where its two quads go. EndFrame puts them over the
  // projection layer, with the source alpha. They go out only in a frame
  // that has both, and only with the projection layer.
  void EnablePanel(bool on);
  bool panel_ready() const;
  void SetPanelTexture(void* texture);
  // The panel from a Vulkan image of the session device, in a session with
  // Vulkan: as SetEyeVulkan, an image of one layer of the render size.
  void SetPanelVulkan(uint64_t image);
  void SetPanelQuads(const PanelQuad quads[2]);
  // The third quad, the Creature panel (docs/ui-layer.md, "The Creature
  // panel"). Set it before SetPanelQuads in the same frame, and
  // SetPanelQuads takes it. A frame that does not set it shows no Creature
  // panel. Static, so the mod can set it with no XrScreen at hand.
  static void StagePanelQuad(const PanelQuad& quad);

  // THE CARDS. Stage a card before EndFrame, and EndFrame puts it over the
  // projection layer and the panels, in that frame only. The first layer of
  // a card makes its swapchain. Static, so the mod can stage with no
  // XrScreen at hand. CardFailed is true after the runtime refused the
  // swapchain of that card or a frame with it. Then what the card holds must
  // go back to the eye pictures.
  static void StageCard(int card, const CardLayer& layer);
  static bool CardFailed(int card);

  // THE CUT SCENE WINDOW. Stage it before EndFrame, and EndFrame uses it in
  // that frame only. Static, so the mod can stage with no XrScreen at hand.
  // When the runtime gives no swapchain for the walls, the frame goes out
  // with no walls.
  static void StageWindow(const WindowLayer& layer);

  // Presents whatever was uploaded.
  void EndFrame();

  // EndFrame for a late frame. The call makes the layers first. Then come
  // frame.wait, the copy of each texture to its swapchain image, and
  // xrEndFrame. With on_thread a second thread does those three, and the
  // call returns at once. The next BeginFrame calls xrWaitFrame, and then it
  // waits for the second thread: the runtime must have a frame before the
  // next one begins. The textures must stay valid until Join returns.
  void EndFrameLate(const LateFrame& frame, bool on_thread);

  // Returns when the second thread has no frame. Call it before a change of
  // a texture that the frame of EndFrameLate reads.
  void Join();

  // True while the second thread has a frame.
  bool sending() const;

  // How long the last EndFrameLate waited for the GPU on the thread of the
  // caller, in ticks of QueryPerformanceCounter. 0 when the second thread
  // has the frame.
  uint64_t LateWaitTicks() const;

  // The display time of the last frame that BeginFrame started, and the
  // display period of the runtime, in nanoseconds. False before the first
  // frame and after a failed wait.
  bool LastDisplayTime(int64_t* time, int64_t* period) const;

  // Mono convenience: one call does a whole frame.
  bool SubmitBgra(const void* pixels, int pitch);
  // The same for a Vulkan image of the session device: layer `layer` of
  // `image` (a VkImage in the layout of a color attachment) for both eyes.
  bool SubmitVulkan(uint64_t image, uint32_t layer);
  // A TEST AID: the next copy of a Vulkan eye picture into each eye
  // swapchain is read back from the swapchain image and saved as
  // bwvr-xr-left-N.bmp and bwvr-xr-right-N.bmp beside the game, with the
  // lowest and the highest alpha in the log. N is `name`.
  void CaptureSwapchainNext(uint64_t name);

  // The same, from a texture the GPU already holds. Menus, loading screens
  // and videos never render the scene twice, so they come through here, and
  // without this they would still pay for a readback. On a modern backend
  // that readback is far more expensive: 18.98 ms per frame at 2560x1440,
  // which makes a menu unusable.
  bool SubmitTexture(void* texture);

  // Ends each thing that Start made. For a screen with no session that
  // started, it can run on any thread.
  void Stop();

  bool running() const { return running_; }
  int eyes() const { return eyes_; }

  // How far in front of the player the screen sits, in meters, and how wide
  // it is. Tunable while running.
  void SetPlacement(float distance_meters, float width_meters);

  // Submit the world instead of a screen.
  //
  // With projection on, EndFrame submits an XrCompositionLayerProjection built
  // from the poses and frustums that BeginFrame reported, which are the ones
  // the caller must have rendered with. With it off, the two quad layers of
  // a flat screen are submitted instead, which is the way back if the world shears.
  void SetProjection(bool on) { projection_ = on; }
  bool projection() const { return projection_; }

  // The half angle the caller actually rendered, in radians, horizontally.
  // The game renders a symmetric frustum, so the layer has to report a
  // symmetric frustum: a projection layer that lies about what was rendered
  // is worse than a screen. Zero means report the runtime's own angles, which
  // is only right if the caller matched them exactly.
  void SetRenderHalfAngle(float radians) { render_half_angle_ = radians; }

  // The pose the caller actually rendered this eye from.
  //
  // The engine builds its camera earlier in the frame than this class begins
  // one, so the scene is drawn from the pose reported one frame ago. A
  // projection layer must declare the pose it was rendered from, not the
  // newest one known, or the runtime warps the image to close a gap that is
  // not there. Declaring the older pose is correct and costs nothing: the
  // runtime reprojects it to wherever the head is at display time, which is
  // what reprojection is for.
  void SetRenderPose(int index, const float position[3],
                     const float orientation[4]);

  // True once after the headset recentered its own space, for example from
  // the Quest menu, when the poses of this frame are in the new space. The
  // mod keeps its own origin, so it has to recenter too. See head_pose.cpp.
  bool TakeRecenter() {
    const bool due = recenter_due_;
    recenter_due_ = false;
    return due;
  }

  // The two hands, 0 left and 1 right. Valid after BeginFrame. A hand that
  // is not bound, or a session with no input focus, reads as inactive with
  // every button released.
  const HandInput& hand(int index) const { return hands_[index ? 1 : 0]; }

  // The interaction profile the runtime chose for a hand, for example
  // "oculus/touch_controller". Empty when nothing is bound.
  const char* HandProfile(int index) const;

  // True while the session has input focus. The headset menu takes it away.
  bool InputFocused() const;

  // A short pulse on one hand. amplitude is 0 to 1. Does nothing when the
  // hand cannot act.
  void Haptic(int index, float amplitude, float seconds);

  uint64_t presented() const { return presented_; }
  uint64_t skipped() const { return skipped_; }

  // Logs what the upload costs, split into the three things SetEyeImage does:
  // waiting for the runtime to hand back a swapchain image, copying the
  // pixels into an upload texture, and telling the GPU to copy that into the
  // swapchain. Only the middle one is work we could remove. Resets the
  // counters, so each report covers the frames since the last one.
  void ReportUpload(uint64_t frames);

 private:
  RemoteScreen* remote_ = nullptr;
  struct Impl;
  Impl* impl_;
  bool BuildFrame();   // the layers of the frame
  void FinishFrame();  // after xrEndFrame, on the frame thread
  void FinishLate();   // after the copies of a late frame, on the frame thread
  bool running_;
  bool should_render_;
  int eyes_;
  EyeView views_[2];
  uint64_t presented_;
  uint64_t skipped_;
  bool projection_;
  uint32_t recommended_w_;
  uint32_t recommended_h_;
  float render_half_angle_;
  EyeView render_views_[2];
  bool have_render_pose_;
  bool recenter_due_ = false;
  HandInput hands_[2];
};

}  // namespace bwvr
