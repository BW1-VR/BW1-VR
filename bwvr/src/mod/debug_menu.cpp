// The debug page of the ring. See debug_menu.h, docs/controllers.md, "The
// debug page", and docs/backend.md, "Keys for v1.0".
//
// WHY. The test keys of the mod (test_keys=1) change one rule each while the
// game runs, for a live A/B. A player in the headset has no keyboard. With
// debug_menu=1 the ring has the item "debug", and its pages hold the same
// switches: the player chooses one with the hand, and the switch changes at
// once.
//
// HOW. kSwitches below is the one table of the switches. A line has the
// group, the word on the badge, the name on the card, the ini key, the
// function that gives the value now, the function that changes it, and the
// words of the values. The change function is the function that the test
// key calls, so the page and the key make the same step, the same pulse and
// the same log line. A switch whose value is kDebugCannot is not on the page
// at that time. The ring (radial_menu.cpp) builds the pages
// (debug_menu_math.h) each frame that it is closed.
//
// THE KEYS of bwvr.ini:
//
//   debug_menu=0     1 puts the item "debug" on the ring. It does not need
//                    test_keys=1. The keys of the keyboard still need
//                    test_keys=1

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "debug_menu.h"
#include "debug_menu_math.h"

extern void ModLog(const char* format, ...);
uint64_t ControllersFrameCount();

// --- the switches, from the files of their areas ----------------------------------

// The ring (radial_menu.cpp).
int RadialMenuWayValue();
void RadialMenuWayChange(const char* by);
// The controllers (controllers.cpp).
int ControllersOwnerModeValue();
void ControllersCycleOwnerMode();
int ControllersButtonLayoutValue();
void ControllersButtonLayoutToggle(const char* by);
int ControllersPressGuardValue();
void ControllersPressGuardChange(const char* by);
int ControllersPressHoldValue();
void ControllersPressHoldChange(const char* by);
// The God view and the table (tabletop.cpp), the grab of the world
// (world_grab.cpp).
int TabletopValue();
void TabletopChange(const char* by);
int TabletopGodSpawnValue();
void TabletopGodSpawnToggle();
int TabletopGodCameraAtValue();
void TabletopGodCameraAtToggle();
int TabletopGodZoomSmoothValue();
void TabletopGodZoomSmoothToggle();
int TabletopGodZoomAboutValue();
void TabletopGodZoomAboutCycle();
int TabletopGodMoveStickValue();
void TabletopGodMoveStickCycle();
int WorldGrabTestValue();
void WorldGrabCycleTest();
// The teleport (teleport.cpp).
int TeleportRingValue();
void TeleportRingChange(const char* by);
int TeleportSwitchValue();
void TeleportSwitchToggle(const char* by);
// The interface (ui_panel.cpp, draw_hooks.cpp, bubble_card.cpp,
// note_depth.cpp, hint_card.cpp, text_click.cpp, spirit_anchor.cpp).
int UiPanelValue();
void UiPanelToggle();
int UiPanelSubtitleReadAheadValue();
void UiPanelSubtitleReadAheadToggle(const char* by);
int UiDepthSubtitleDegreesValue();
void UiDepthCycleSubtitleDegrees();
int UiDepthHintHandDepthValue();
void UiDepthToggleHintHandDepth(const char* by);
int BubbleCardValue();
void BubbleCardToggle(const char* by);
int NoteDepthValue();
void NoteDepthToggle(const char* by);
int HintCardTimingValue();
void HintCardTimingChange(const char* by);
int HintCardWordsValue();
void HintCardWordsChange(const char* by);
int HintCardMovesValue();
void HintCardMovesToggle(const char* by);
int TextClickValue();
void TextClickChange(const char* by);
int SpiritAnchorWorldSizeValue();
void SpiritAnchorWorldSizeToggle();
// The picture (cutscene_window.cpp, eye_clean.cpp, one_walk.cpp,
// land_light.cpp, vk_scene.cpp).
int CutsceneWindowValue();
void CutsceneWindowToggle();
int CutsceneWindowDepthValue();
void CutsceneWindowDepthToggle();
int EyeCleanValue();
void EyeCleanToggle(const char* by);
int OneWalkMarksValue();
void OneWalkMarksToggle(const char* by);
int LandLightEyesValue();
void LandLightEyesChange(const char* by);
int VkSceneEyesValue();
void VkSceneEyesToggle(const char* by);
int VkSceneKeepPictureValue();
void VkSceneKeepPictureToggle(const char* by);
int VkSceneStage1Value();
void VkSceneStage1Toggle(const char* by);
int VkSceneUntexturedValue();
void VkSceneUntexturedToggle(const char* by);
// The render (one_walk.cpp, frame_time.cpp, frame_send.cpp, render_thread.cpp,
// fast_transform.cpp, d3d7_thread.cpp, thread_wait.cpp, vk_scene.cpp,
// vk_window.cpp, mod_main.cpp, file_read_ahead.cpp).
int OneWalkValue();
void OneWalkToggle();
int OneWalkMergeValue();
void OneWalkMergeToggle(const char* by);
int FrameTimeValue();
void FrameTimeToggle(const char* by);
int FrameSendMode();
void FrameSendCycle(const char* by);
int RenderThreadValue();
void RenderThreadToggle(const char* by);
int RenderThreadLockFreeValue();
void RenderThreadLockFreeToggle(const char* by);
int RenderThreadFilterValue();
void RenderThreadFilterToggle(const char* by);
int RenderThreadBuffersValue();
void RenderThreadBuffersToggle(const char* by);
int RenderThreadFlushValue();
void RenderThreadFlushToggle(const char* by);
int RenderThreadRightEyeValue();
void RenderThreadRightEyeToggle(const char* by);
int RenderThreadLateEndValue();
void RenderThreadLateEndToggle(const char* by);
int FastTransformValue();
void FastTransformToggle(const char* by);
int D3D7ThreadValue();
void D3D7ThreadToggle(const char* by);
int D3D7ThreadMergeValue();
void D3D7ThreadMergeToggle(const char* by);
int ThreadWaitMode();
void ThreadWaitToggle(const char* by);
int VkSceneFollowValue();
void VkSceneFollowToggle(const char* by);
int VkScenePanelValue();
void VkScenePanelToggle(const char* by);
int VkSceneOnlyValue();
void VkSceneOnlyToggle(const char* by);
int VkSceneUploadsValue();
void VkSceneUploadsToggle(const char* by);
int VkSceneFoveationValue();
void VkSceneFoveationToggle(const char* by);
int VkSceneFoveaResValue();
void VkSceneFoveaResToggle(const char* by);
int VkWindowValue();
void VkWindowToggle(const char* by);
int XrVulkanUploadValue();
void XrVulkanUploadToggle(const char* by);
int XrHoldValue();
void XrHoldToggle(const char* by);
int FileReadAheadValue();
void FileReadAheadToggle(const char* by);
// The Hand (hand_direct.cpp, hand_exact.cpp, hand_feel.cpp, hand_model.cpp,
// target_glow.cpp, take_stream.cpp, forest_take.cpp).
int HandDirectReleaseAtOnceValue();
void HandDirectReleaseAtOnceChange(const char* by);
int HandDirectCatchMovingValue();
void HandDirectCatchMovingChange(const char* by);
int HandDirectWidePickValue();
void HandDirectWidePickChange(const char* by);
int HandDirectFarChoiceValue();
void HandDirectFarChoiceChange(const char* by);
int HandDirectFarStayValue();
void HandDirectFarStayChange(const char* by);
int HandDirectTakeCarryValue();
void HandDirectTakeCarryChange(const char* by);
int HandDirectNearRuleValue();
void HandDirectNearRuleChange(const char* by);
int HandDirectScrollDrawValue();
void HandDirectScrollDrawChange(const char* by);
int HandDirectScriptPlayerValue();
void HandDirectScriptPlayerChange(const char* by);
int HandDirectPalmPointValue();
void HandDirectPalmPointChange(const char* by);
int HandDirectStrokeLimitsValue();
void HandDirectStrokeLimitsChange(const char* by);
int HandDirectSlapValue();
void HandDirectSlapChange(const char* by);
int HandDirectStrokeHandValue();
void HandDirectStrokeHandChange(const char* by);
int HandDirectGlowIdleValue();
void HandDirectGlowIdleChange(const char* by);
int HandDirectRootsValue();
void HandDirectRootsToggle(const char* by);
int HandDirectTreeHintValue();
void HandDirectTreeHintToggle(const char* by);
int HandExactHideValue();
void HandExactHideChange(const char* by);
int HandFeelValue();
void HandFeelChange(const char* by);
int HandFeelPinchValue();
void HandFeelPinchChange(const char* by);
int HandModelSwapValue();
void HandModelSwapChange(const char* by);
int HandModelLookValue();
void HandModelLookChange(const char* by);
int TargetGlowValue();
void TargetGlowChange(const char* by);
int TakeStreamValue();
void TakeStreamChange(const char* by);
int ForestTakeValue();
void ForestTakeChange(const char* by);
// The throw (throw_physics.cpp).
int ThrowLawValue();
void ThrowLawChange(const char* by);
int ThrowDrawValue();
void ThrowDrawChange(const char* by);
int ThrowFallValue();
void ThrowFallChange(const char* by);
int ThrowLandValue();
void ThrowLandChange(const char* by);
int ThrowSettleValue();
void ThrowSettleChange(const char* by);
int ThrowRestValue();
void ThrowRestChange(const char* by);
int ThrowTakeValue();
void ThrowTakeChange(const char* by);
int ThrowCatchValue();
void ThrowCatchChange(const char* by);
int ThrowSlowValue();
void ThrowSlowerChange(const char* by);
void ThrowFasterChange(const char* by);
int ThrowLandRateValue();
void ThrowLandSlowerChange(const char* by);
void ThrowLandFasterChange(const char* by);
// The Creature, the villagers and the miracles (creature_command.cpp,
// creature_stay.cpp, miracle.cpp, village.cpp).
int CreatureCommandRuleValue();
void CreatureCommandRuleChange(const char* by);
int CreatureStayModeValue();
void CreatureStayModeChange(const char* by);
int MiracleHandValue();
void MiracleHandChange(const char* by);
int VillageTotemValue();
void VillageTotemChange(const char* by);
int VillageHoldValue();
void VillageHoldChange(const char* by);
// The sound (audio_listener.cpp, audio_voices.cpp, audio_range.cpp).
int AudioModeValue();
void AudioCycleMode();
int AudioStopWaitValue();
void AudioStopWaitToggle();
int AudioTakeLoopValue();
void AudioTakeLoopChange(const char* by);
// The Temple (temple.cpp).
int TemplePointerValue();
void TemplePointerChange(const char* by);
int TempleFlightValue();
void TempleFlightChange(const char* by);
int TempleViewValue();
void TempleViewChange(const char* by);
int TempleFadeValue();
void TempleFadeChange(const char* by);

namespace {

enum Group {
  kGroupView,
  kGroupPicture,
  kGroupRender,
  kGroupInterface,
  kGroupHand,
  kGroupThrow,
  kGroupCreature,
  kGroupSound,
  kGroupTemple,
  kGroups
};

const wchar_t* const kGroupName[kGroups] = {
    L"View",  L"Picture",  L"Render", L"Interface", L"Hand",
    L"Throw", L"Creature", L"Sound",  L"Temple"};

struct Switch {
  int group;
  const wchar_t* label;   // the badge, at most 14 letters
  const wchar_t* name;    // the card in the middle
  const char* key;        // the ini key, or "" for a state of a test only
  int (*value)();
  void (*change)(const char* by);
  const wchar_t* words;   // the words of the values from 0, with "|"
};

// THE TABLE. One line for each test key that is a switch. A new test key is
// one more line here, with its two functions in the file of its area.
const Switch kSwitches[] = {
    // View
    {kGroupView, L"Table", L"Table or God view", "", TabletopValue,
     TabletopChange, L"off|on"},
    {kGroupView, L"Follow camera", L"God view follows the game camera",
     "god_spawn", TabletopGodSpawnValue,
     [](const char*) { TabletopGodSpawnToggle(); }, L"off|on"},
    {kGroupView, L"Camera place", L"Game camera over the head",
     "god_camera_at", TabletopGodCameraAtValue,
     [](const char*) { TabletopGodCameraAtToggle(); }, L"recenter|head"},
    {kGroupView, L"Smooth zoom", L"Stick zooms while it is held",
     "god_zoom_smooth", TabletopGodZoomSmoothValue,
     [](const char*) { TabletopGodZoomSmoothToggle(); }, L"steps|smooth"},
    {kGroupView, L"Zoom point", L"Point that a zoom keeps", "god_zoom_about",
     TabletopGodZoomAboutValue,
     [](const char*) { TabletopGodZoomAboutCycle(); },
     L"under head|look|Hand"},
    {kGroupView, L"Stick walk", L"Direction of the walk stick", "move_stick",
     TabletopGodMoveStickValue,
     [](const char*) { TabletopGodMoveStickCycle(); },
     L"off|head|controller"},
    {kGroupView, L"Grab test", L"Grab of the world: test step", "",
     WorldGrabTestValue, [](const char*) { WorldGrabCycleTest(); },
     L"as set|one to one|off"},
    {kGroupView, L"Ring size", L"Size rule of the teleport ring",
     "teleport_ring_mode", TeleportRingValue, TeleportRingChange,
     L"room|world"},
    {kGroupView, L"Jump switch", L"Game switch the teleport keeps to",
     "teleport_switch", TeleportSwitchValue, TeleportSwitchToggle,
     L"jump|drag"},
    {kGroupView, L"Button layout", L"Layout of the buttons", "button_layout",
     ControllersButtonLayoutValue, ControllersButtonLayoutToggle,
     L"ring on X|ring on Y"},
    {kGroupView, L"Ring ways", L"The ways that open the ring", "",
     RadialMenuWayValue, RadialMenuWayChange, L"both|button|stick"},
    {kGroupView, L"Pointer owner", L"Owner of the pointer", "",
     ControllersOwnerModeValue,
     [](const char*) { ControllersCycleOwnerMode(); },
     L"automatic|controller|mouse"},
    {kGroupView, L"Script player", L"Scripts see the player",
     "script_distance", HandDirectScriptPlayerValue,
     HandDirectScriptPlayerChange, L"camera|player"},

    // Picture
    {kGroupPicture, L"Scene window", L"Cut scene on a screen in the room",
     "cutscene_window", CutsceneWindowValue,
     [](const char*) { CutsceneWindowToggle(); }, L"off|on"},
    {kGroupPicture, L"Scene depth", L"Depth limit of the cut scene",
     "cutscene_depth", CutsceneWindowDepthValue,
     [](const char*) { CutsceneWindowDepthToggle(); }, L"off|nearest|fixed"},
    {kGroupPicture, L"Eye clean", L"Own frame before for each eye",
     "eye_clean", EyeCleanValue, EyeCleanToggle, L"shared|own"},
    {kGroupPicture, L"Walk marks", L"Cards and marks for each eye",
     "one_walk_marks", OneWalkMarksValue, OneWalkMarksToggle, L"off|on"},
    {kGroupPicture, L"Land light", L"Lights of the land in both eyes",
     "land_light_eyes", LandLightEyesValue, LandLightEyesChange,
     L"left eye|both eyes"},
    {kGroupPicture, L"Advisor size", L"Advisor size at a world place",
     "spirit_world_size", SpiritAnchorWorldSizeValue,
     [](const char*) { SpiritAnchorWorldSizeToggle(); },
     L"game|near size"},
    {kGroupPicture, L"Vulkan eyes", L"Eye pictures from Vulkan", "vk_eyes",
     VkSceneEyesValue, VkSceneEyesToggle, L"D3D9|Vulkan"},
    {kGroupPicture, L"Keep picture", L"Walk starts from last picture",
     "vk_keep_picture", VkSceneKeepPictureValue, VkSceneKeepPictureToggle,
     L"black|keep"},
    {kGroupPicture, L"Stage 1", L"Second texture stage in Vulkan",
     "vk_stage1", VkSceneStage1Value, VkSceneStage1Toggle, L"off|on"},
    {kGroupPicture, L"No texture", L"Stage 0 with no texture",
     "vk_untextured", VkSceneUntexturedValue, VkSceneUntexturedToggle,
     L"white|diffuse"},

    // Render
    {kGroupRender, L"One walk", L"One walk for both eyes", "one_walk",
     OneWalkValue, [](const char*) { OneWalkToggle(); }, L"off|on"},
    {kGroupRender, L"Foveation", L"Foveated rate of the eye pictures",
     "foveation", VkSceneFoveationValue, VkSceneFoveationToggle,
     L"off|on|marks"},
    {kGroupRender, L"Fovea images", L"Outer and centre eye images",
     "fovea_res", VkSceneFoveaResValue, VkSceneFoveaResToggle, L"off|on"},
    {kGroupRender, L"Frame send", L"Wait for the GPU in a frame",
     "frame_send", FrameSendMode, FrameSendCycle,
     L"each eye|one wait|2nd thread"},
    {kGroupRender, L"Frame hold", L"Hold of the frame, ms", "xr_hold_ms",
     XrHoldValue, XrHoldToggle, L""},
    {kGroupRender, L"Real time", L"Real frame time after the eyes",
     "frame_time_real", FrameTimeValue, FrameTimeToggle, L"kept|real"},
    {kGroupRender, L"SSE loops", L"Vertex loops with SSE", "fast_transform",
     FastTransformValue, FastTransformToggle, L"x87|SSE"},
    {kGroupRender, L"Thread wait", L"Threads spin, then sleep",
     "thread_wait", ThreadWaitMode, ThreadWaitToggle, L"spin|sleep"},
    {kGroupRender, L"Read ahead", L"Read ahead of the game files",
     "file_read_ahead", FileReadAheadValue, FileReadAheadToggle, L"off|on"},
    {kGroupRender, L"Follow target", L"Vulkan follows the render target",
     "vk_scene_follow", VkSceneFollowValue, VkSceneFollowToggle, L"off|on"},
    {kGroupRender, L"Upload queue", L"Texture uploads in the submit",
     "vk_upload_queue", VkSceneUploadsValue, VkSceneUploadsToggle,
     L"wait|queue"},
    {kGroupRender, L"Vulkan window", L"Window picture by a swapchain",
     "vk_window", VkWindowValue, VkWindowToggle, L"GDI|Vulkan"},
    {kGroupRender, L"Vulkan only", L"Walk with no Direct3D 9", "vk_only",
     VkSceneOnlyValue, VkSceneOnlyToggle, L"off|on"},
    {kGroupRender, L"Vulkan upload", L"CPU pixels to swapchains by Vulkan",
     "xr_vulkan_upload", XrVulkanUploadValue, XrVulkanUploadToggle,
     L"D3D11|Vulkan"},
    {kGroupRender, L"Render thread", L"Calls of a walk to render thread",
     "render_thread", RenderThreadValue, RenderThreadToggle, L"off|on|kept"},
    {kGroupRender, L"No lock", L"Texture answers with no lock",
     "render_thread_lock_free", RenderThreadLockFreeValue,
     RenderThreadLockFreeToggle, L"off|on"},
    {kGroupRender, L"Call filter", L"Drop calls that change nothing",
     "render_thread_filter", RenderThreadFilterValue,
     RenderThreadFilterToggle, L"off|on"},
    {kGroupRender, L"Own buffers", L"Render thread has own buffers",
     "render_thread_buffers", RenderThreadBuffersValue,
     RenderThreadBuffersToggle, L"off|on"},
    {kGroupRender, L"Merge eyes", L"One draw for both eyes",
     "one_walk_merge", OneWalkMergeValue, OneWalkMergeToggle, L"off|on"},
    {kGroupRender, L"GPU flush", L"Calls to the GPU each N draws",
     "gpu_flush", RenderThreadFlushValue, RenderThreadFlushToggle, L""},
    {kGroupRender, L"Right eye copy", L"Right eye copy on render thread",
     "render_thread_right_eye", RenderThreadRightEyeValue,
     RenderThreadRightEyeToggle, L"frame|render"},
    {kGroupRender, L"D3D7 thread", L"Direct3D 7 calls on own thread",
     "d3d7_thread", D3D7ThreadValue, D3D7ThreadToggle, L"off|on"},
    {kGroupRender, L"D3D7 merge", L"D3D7 thread merges draws",
     "d3d7_thread_merge", D3D7ThreadMergeValue, D3D7ThreadMergeToggle,
     L"off|on"},
    {kGroupRender, L"Late end", L"End of walk does not wait",
     "render_thread_late_end", RenderThreadLateEndValue,
     RenderThreadLateEndToggle, L"wait|no wait"},

    // Interface
    {kGroupInterface, L"UI layer", L"Interface on its own layer", "ui_panel",
     UiPanelValue, [](const char*) { UiPanelToggle(); },
     L"in picture|own layer|test"},
    {kGroupInterface, L"Subtitle angle", L"Subtitles below the view, degrees",
     "subtitle_deg", UiDepthSubtitleDegreesValue,
     [](const char*) { UiDepthCycleSubtitleDegrees(); }, L""},
    {kGroupInterface, L"Subtitle copy", L"Read the subtitle copy later",
     "subtitle_read_ahead", UiPanelSubtitleReadAheadValue,
     UiPanelSubtitleReadAheadToggle, L"wait|no wait"},
    {kGroupInterface, L"Bubble card", L"Speech bubble as one card",
     "bubble_card", BubbleCardValue, BubbleCardToggle, L"off|on"},
    {kGroupInterface, L"Note depth", L"Note of the game at its depth",
     "note_depth", NoteDepthValue, NoteDepthToggle, L"off|on"},
    {kGroupInterface, L"Hint depth", L"Hint at the Hand at Hand depth",
     "hint_hand_depth", UiDepthHintHandDepthValue,
     UiDepthToggleHintHandDepth, L"off|on"},
    {kGroupInterface, L"Vulkan panel", L"Panel of the Vulkan renderer",
     "vk_panel", VkScenePanelValue, VkScenePanelToggle, L"off|on"},
    {kGroupInterface, L"Prompt timing", L"A prompt shows at once",
     "prompt_timing", HintCardTimingValue, HintCardTimingChange, L"off|on"},
    {kGroupInterface, L"Hint words", L"Prompts and hints of the cards",
     "hint_words", HintCardWordsValue, HintCardWordsChange, L""},
    {kGroupInterface, L"Prompt moves", L"Lines show only when they move",
     "prompt_moves", HintCardMovesValue, HintCardMovesToggle, L"off|on"},
    {kGroupInterface, L"Tree card", L"Card of a tree in the Hand",
     "tree_plant_hint", HandDirectTreeHintValue, HandDirectTreeHintToggle,
     L"off|on"},
    {kGroupInterface, L"Text click", L"Trigger clicks a text of the game",
     "text_click", TextClickValue, TextClickChange, L"off|on"},

    // Hand
    {kGroupHand, L"Catch moving", L"Reach for an object that moves",
     "catch_moving", HandDirectCatchMovingValue, HandDirectCatchMovingChange,
     L"off|pick|rest|open"},
    {kGroupHand, L"Wide pick", L"Wide reach of the pick, in cm",
     "hand_wide_pick_cm", HandDirectWidePickValue, HandDirectWidePickChange,
     L""},
    {kGroupHand, L"Near rule", L"Rule of the pick at the Hand", "near_rule",
     HandDirectNearRuleValue, HandDirectNearRuleChange,
     L"old|nearest|one target"},
    {kGroupHand, L"Far choice", L"Choice of a far target", "far_grab_choice",
     HandDirectFarChoiceValue, HandDirectFarChoiceChange,
     L"point|bias|ranks|balls|ball ranks"},
    {kGroupHand, L"Far stay", L"Far target that stays", "far_grab_stay",
     HandDirectFarStayValue, HandDirectFarStayChange, L"off|on"},
    {kGroupHand, L"Take carry", L"Grip keeps a take with no source",
     "far_take_carry", HandDirectTakeCarryValue, HandDirectTakeCarryChange,
     L"off|on"},
    {kGroupHand, L"Roots pile", L"Roots pile of a far tree",
     "far_roots_at_tree", HandDirectRootsValue, HandDirectRootsToggle,
     L"glide|roots"},
    {kGroupHand, L"Take stream", L"Curve of a far take stream",
     "take_stream", TakeStreamValue, TakeStreamChange, L"line|curve"},
    {kGroupHand, L"Forest take", L"Bundle from a forest", "forest_take",
     ForestTakeValue, ForestTakeChange, L"off|on"},
    {kGroupHand, L"Target glow", L"Glow of a far target", "target_glow",
     TargetGlowValue, TargetGlowChange, L"halo|mesh|both"},
    {kGroupHand, L"Idle glow", L"Glow with no target", "hand_glow_idle",
     HandDirectGlowIdleValue, HandDirectGlowIdleChange, L"off|on"},
    {kGroupHand, L"Scroll draw", L"Each drawn scroll is a target",
     "scroll_draw", HandDirectScrollDrawValue, HandDirectScrollDrawChange,
     L"off|on"},
    {kGroupHand, L"Press guard", L"Guard of the first 300 ms",
     "grip_press_guard", ControllersPressGuardValue,
     ControllersPressGuardChange, L"off|on"},
    {kGroupHand, L"Press hold", L"Game button held for a pick",
     "grip_press_hold", ControllersPressHoldValue, ControllersPressHoldChange,
     L"off|on"},
    {kGroupHand, L"Take guard", L"Hold back the rest during a take",
     "catch_rest_guard", ThrowTakeValue, ThrowTakeChange, L"off|on"},
    {kGroupHand, L"Catch at once", L"Catch a moving object at once",
     "catch_at_once", ThrowCatchValue, ThrowCatchChange, L"off|on"},
    {kGroupHand, L"Pinch", L"Pinch for a small object", "hand_pinch",
     HandFeelPinchValue, HandFeelPinchChange, L"fist|pinch"},
    {kGroupHand, L"Finger feel", L"Fingers react to the world", "hand_feel",
     HandFeelValue, HandFeelChange, L"off|on"},
    {kGroupHand, L"Game Hand", L"Mesh of the game's Hand", "hand_hide_game",
     HandExactHideValue, HandExactHideChange, L"shown|hidden"},
    {kGroupHand, L"Hand model", L"Model of the hands", "hand_model",
     HandModelSwapValue, HandModelSwapChange, L"low poly|game|file"},
    {kGroupHand, L"Hand look", L"Look of the hands", "", HandModelLookValue,
     HandModelLookChange, L"player|evil|neutral|good"},

    // Throw
    {kGroupThrow, L"Fast release", L"Release at once", "release_at_once",
     HandDirectReleaseAtOnceValue, HandDirectReleaseAtOnceChange, L"off|on"},
    {kGroupThrow, L"Throw law", L"Throw: root law or real flight",
     "throw_law", ThrowLawValue, ThrowLawChange, L"gain|root|real"},
    {kGroupThrow, L"Throw picture", L"Picture between the physics steps",
     "throw_draw", ThrowDrawValue, ThrowDrawChange, L"flight|real"},
    {kGroupThrow, L"Fall rule", L"Still hand lets a high object fall",
     "release_fall_cm", ThrowFallValue, ThrowFallChange, L"off|on"},
    {kGroupThrow, L"Landing", L"Landing of a thrown object", "throw_land",
     ThrowLandValue, ThrowLandChange, L"game|land|flight"},
    {kGroupThrow, L"Tree settle", L"A tree that falls settles",
     "throw_tree_settle", ThrowSettleValue, ThrowSettleChange, L"off|on"},
    {kGroupThrow, L"Land rest", L"Rest after a landing", "throw_land_rest",
     ThrowRestValue, ThrowRestChange, L"off|on|spin|steep|still"},
    {kGroupThrow, L"Throw slower", L"Time factor of a throw x10",
     "throw_slow_x10", ThrowSlowValue, ThrowSlowerChange, L""},
    {kGroupThrow, L"Throw faster", L"Time factor of a throw x10",
     "throw_slow_x10", ThrowSlowValue, ThrowFasterChange, L""},
    {kGroupThrow, L"Land slower", L"Rate of the land x10",
     "throw_land_rate_x10", ThrowLandRateValue, ThrowLandSlowerChange, L""},
    {kGroupThrow, L"Land faster", L"Rate of the land x10",
     "throw_land_rate_x10", ThrowLandRateValue, ThrowLandFasterChange, L""},

    // Creature
    {kGroupCreature, L"Command rule", L"Rule of the Creature command",
     "creature_command_rule", CreatureCommandRuleValue,
     CreatureCommandRuleChange, L""},
    {kGroupCreature, L"Hold stay", L"Long grip makes the Creature stay",
     "creature_hold_stay", CreatureStayModeValue, CreatureStayModeChange,
     L"off|on"},
    {kGroupCreature, L"Stroke hand", L"Hand of a stroke",
     "creature_stroke_hand", HandDirectStrokeHandValue,
     HandDirectStrokeHandChange, L"by size|flat|finger|fist"},
    {kGroupCreature, L"Stroke limits", L"Limits of the hand of a stroke",
     "creature_finger_below_pct", HandDirectStrokeLimitsValue,
     HandDirectStrokeLimitsChange, L"ini|fixed"},
    {kGroupCreature, L"Palm point", L"Stroke point at the palm",
     "creature_palm_point", HandDirectPalmPointValue,
     HandDirectPalmPointChange, L"finger|palm"},
    {kGroupCreature, L"Slap speed", L"Speed of a slap", "creature_slap_mps",
     HandDirectSlapValue, HandDirectSlapChange,
     L"ini|0.7 m/s|1.0 m/s|1.3 m/s|1.6 m/s"},
    {kGroupCreature, L"Miracle hand", L"A miracle is in the real hand",
     "miracle_hand", MiracleHandValue, MiracleHandChange, L"off|on"},
    {kGroupCreature, L"Totem hand", L"The totem follows the real hand",
     "totem_hand", VillageTotemValue, VillageTotemChange, L"off|on"},
    {kGroupCreature, L"Hold target", L"Halo on the thing at the Hand",
     "hold_target", VillageHoldValue, VillageHoldChange, L"off|on"},

    // Sound
    {kGroupSound, L"Sound step", L"Sound of the mod, step by step", "",
     AudioModeValue, [](const char*) { AudioCycleMode(); },
     L"as set|same dist|QMixer pan|game picks|game ears"},
    {kGroupSound, L"Stop wait", L"Wait of 20 ms at a sound stop",
     "audio_stop_wait", AudioStopWaitValue,
     [](const char*) { AudioStopWaitToggle(); }, L"skip|wait"},
    {kGroupSound, L"Take loop", L"Ears start the loop of a take",
     "audio_take_loop", AudioTakeLoopValue, AudioTakeLoopChange, L"off|on"},

    // Temple
    {kGroupTemple, L"Temple pointer", L"Pointer and click in the Temple",
     "temple_pointer", TemplePointerValue, TemplePointerChange,
     L"ray click|ray plain|hand"},
    {kGroupTemple, L"Room flight", L"Camera flight into a room",
     "temple_camera_flight", TempleFlightValue, TempleFlightChange,
     L"off|on"},
    {kGroupTemple, L"Room view", L"View of a room of the Temple",
     "temple_view", TempleViewValue, TempleViewChange,
     L"screen|head view|game cam"},
    {kGroupTemple, L"Temple fade", L"Fade of the Temple in both eyes",
     "temple_fade_eyes", TempleFadeValue, TempleFadeChange,
     L"left eye|both eyes"},
};

const int kCount = static_cast<int>(sizeof(kSwitches) / sizeof(kSwitches[0]));
static_assert(kCount <= radial_math::kMaxSwitches,
              "the ring has no item code for so many switches");

bool IniPath(char path[MAX_PATH]) {
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return false;
  char* slash = strrchr(path, '\\');
  if (slash == nullptr) return false;
  slash[1] = 0;
  if (strlen(path) + 9 >= MAX_PATH) return false;
  strcat_s(path, MAX_PATH, "bwvr.ini");
  return true;
}

// The two keys are read one time. TestSwitchesOn runs in DllMain too, before
// the other keys of the mod are read, so it reads the file itself.
int g_test_keys = -1;
int g_debug_menu = -1;

void ReadKeys() {
  if (g_debug_menu >= 0) return;
  char path[MAX_PATH];
  if (!IniPath(path)) {
    g_test_keys = 0;
    g_debug_menu = 0;
    return;
  }
  g_test_keys = GetPrivateProfileIntA("bwvr", "test_keys", 0, path) != 0;
  g_debug_menu = GetPrivateProfileIntA("bwvr", "debug_menu", 0, path) != 0;
}

void Narrow(const wchar_t* in, char* out, size_t size) {
  size_t n = 0;
  for (; n + 1 < size && in[n] != 0; ++n) {
    out[n] = (in[n] >= 32 && in[n] < 127) ? static_cast<char>(in[n]) : '?';
  }
  out[n] = 0;
}

}  // namespace

bool TestSwitchesOn() {
  ReadKeys();
  return g_test_keys != 0 || g_debug_menu != 0;
}

bool DebugMenuOn() {
  ReadKeys();
  return g_debug_menu != 0;
}

int DebugGroupCount() { return kGroups; }

const wchar_t* DebugGroupName(int group) {
  return group >= 0 && group < kGroups ? kGroupName[group] : L"";
}

int DebugSwitchCount() { return kCount; }

int DebugSwitchValue(int i) {
  if (i < 0 || i >= kCount) return kDebugCannot;
  return kSwitches[i].value();
}

int DebugSwitchGroup(int i) {
  if (i < 0 || i >= kCount) return -1;
  return kSwitches[i].group;
}

const wchar_t* DebugSwitchLabel(int i) {
  return i >= 0 && i < kCount ? kSwitches[i].label : L"";
}

const wchar_t* DebugSwitchName(int i) {
  return i >= 0 && i < kCount ? kSwitches[i].name : L"";
}

const char* DebugSwitchKey(int i) {
  return i >= 0 && i < kCount ? kSwitches[i].key : "";
}

void DebugSwitchValueText(int i, int value, wchar_t* out, int size) {
  if (size <= 0) return;
  out[0] = 0;
  if (i < 0 || i >= kCount) return;
  if (value == kDebugCannot) {
    swprintf_s(out, size, L"-");
    return;
  }
  if (debug_math::ValueWord(kSwitches[i].words, value, out, size) > 0) return;
  swprintf_s(out, size, L"%d", value);
}

bool DebugSwitchChoose(int i) {
  if (i < 0 || i >= kCount) return false;
  const Switch& s = kSwitches[i];
  const int before = s.value();
  char label[48];
  Narrow(s.label, label, sizeof(label));
  if (before == kDebugCannot) {
    ModLog("debug page   : the player chose \"%s\", and it cannot change now, "
           "frame %llu", label, ControllersFrameCount());
    return false;
  }
  s.change(kDebugBy);
  const int after = s.value();
  wchar_t word[32];
  char text[32];
  DebugSwitchValueText(i, after, word, 32);
  Narrow(word, text, sizeof(text));
  if (s.key[0] != 0) {
    ModLog("debug page   : the player chose \"%s\": %s is %d now (%s), it was "
           "%d, frame %llu", label, s.key, after, text, before,
           ControllersFrameCount());
  } else {
    ModLog("debug page   : the player chose \"%s\": the value is %d now (%s), "
           "it was %d, frame %llu", label, after, text, before,
           ControllersFrameCount());
  }
  return true;
}

void DebugMenuLogStart() {
  if (!DebugMenuOn()) return;
  int can = 0;
  for (int i = 0; i < kCount; ++i) {
    if (kSwitches[i].value() != kDebugCannot) ++can;
  }
  ModLog("debug page   : debug_menu=1. The ring has the item \"debug\" with "
         "%d switches in %d groups, %d of them can change now. test_keys=%d",
         kCount, static_cast<int>(kGroups), can, g_test_keys);
}
