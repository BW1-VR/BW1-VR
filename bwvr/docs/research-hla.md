# What Half-Life: Alyx does, and what the mod can take from it

Research only. No source file, build, game run or registry value changed
while it was written. Nobody started Half-Life: Alyx, SteamVR or Black &
White for it.

Asked for by Buck on 2026-10-03: "I have HL:A installed, you could do some
research and compare things like throwing physics, hints, teleport system,
surround sound, interactions, raycasting angles, etc. to it, see if you can
pull anything useful from the code that would make our mod more polished and
refined."

The mod takes numbers, rules and design ideas from the game. It takes no code
and no asset. Each extracted file stays in the scratchpad, not in the repo.

Marks used in this file:

- **[G]** read in the game files, or in another file on this PC. The line
  names the file or the function.
- **[P]** from a public source. The line has the link.
- **[I]** inferred. The evidence points that way, but no file says it.
- **[M]** what the mod does, with the doc that says so.

"HLA" is Half-Life: Alyx. "The mod" is the Black & White VR Mod.

## 0. Sources and method

**The install.** Steam app 546560 in
`C:\Program Files (x86)\Steam\steamapps\common\Half-Life Alyx`. The game
files have the date 2026-10-02. The packed files are in `game/hlvr/pak01_*.vpk`
(46,934 files).

**The tools.** All of them are in the scratchpad folder `hla`:

- A small Python reader for the VPK archives (`vpk.py`). It gives the plain
  text files: `scripts/instructor_lessons.txt`, `cfg/*`, the localization
  files.
- The Source 2 Viewer command line tool, version 20.0, from the official
  GitHub releases of ValveResourceFormat. It gives the compiled files as
  text: the models (attachments, bones), the sound stacks, the sound events,
  the panorama layouts and styles, the particle files.
- A Python script with capstone (`cvars.py`) that lists each console variable
  of a DLL with its default. Each variable has a small start function of one
  form: `lea r8, default`, `lea rdx, name`, `lea rcx, object`, `call`. The
  script found 2,585 variables in `server.dll` and 1,763 in `client.dll`. A
  few variables that have a change function are not in the list.
- A Python disassembly helper (`s2re.py`, `fn.py`) that finds the code that
  reads a variable or a string. The DLLs have no symbols. A function has only
  its address as a name in this file.
- A reader for the caption file (`ccd.py`). The words of the developer
  commentary of the game are in
  `game/hlvr/resource/subtitles/closecaption_english.dat`.

**Units.** HLA uses inches. 1 unit is 2.54 cm. The axes of Source 2 are x
forward, y left, z up. An angle has three parts: pitch (positive is down),
yaw (positive is left) and roll.

**The other files on this PC.** The render model files of SteamVR, in
`steamapps\common\SteamVR\resources\rendermodels` and in `drivers`. They give
the poses of each controller against its raw pose.

## 1. The findings that help the mod most

The order is value against cost. Each line names the section with the
evidence and the changes.

1. **Aim 6 degrees lower.** The menu ray of HLA on a Touch controller is the
   aim pose turned 7.1 degrees toward the other hand and 6.0 degrees down.
   The yaw agrees with the pointer of Meta (6.45 degrees). The pitch does
   not: the pointer of Meta is 0.8 degrees up. Buck said "too high". The
   mod already has this ray as `aim_preset=2`, the default (commit
   `c9bb998`). Cost: one headset test. (Section 2)
2. **A cone of 15 degrees with its tip 25 cm behind the hand.** The cone of
   the gravity gloves is twice as wide as the cone of the mod (8 degrees).
   A near object can be 20 degrees from the ray. A new target must also be
   in the view of the head (60 by 70 degrees). The lock holds a target to 60
   degrees. Buck's own log had his object at the edge of the 8 degrees.
   (Section 3)
3. **A low bounce after a throw.** The median material of HLA keeps 10% of
   the speed at a bounce, the default material 25%, and the friction is
   0.8. This is the number for Buck's open item "objects jitter and bounce
   too fast". (Section 4)
4. **A gain of 1.6 on each throw.** The throw law of the mod is already the
   law of HLA: the peak of the last 100 ms with a short mean around it. HLA
   then multiplies each throw by 1.6, and it takes the velocity from the
   runtime, not from positions. (Section 4)
5. **A hint opens and closes on an event, and it counts successes.** No HLA
   hint runs on a clock. Each stops for good after the player did the action
   1 to 3 times. (Section 7)
6. **The highlight is on the object.** HLA shows nothing at the hand before
   a near grab. An item glows when the player looks at it. HLA selects the
   near object by a score: the cosine to the palm direction divided by the
   square of the distance. The reach is 15 cm. (Section 6)
7. **A pulse for the arrival and a quiet ping for the lock.** In the last
   0.125 s of the flight the hand gets a medium pulse each frame. The lock
   plays a ping at volume 0.15, one at a time. (Sections 3 and 8)
8. **A sound for the teleport, and the nearest valid place.** HLA plays
   footsteps after each teleport, and Valve found that players then say "I
   walked". The end of the arc goes to a valid place within 25 cm.
   (Section 5)
9. **Spread a sound that is near the head.** HLA sends a source nearer than
   25 to 50 cm to the two ears. It sends 1,284 sound events through the HRTF
   of Steam Audio, which is the one way to tell the ear that a sound is
   below. (Section 8)
10. **Keep a part of the room in view when the picture moves.** In each ride
    of HLA the cabin fills most of the view. The cut scene window of the mod
    has black walls with nothing to see. (Section 9)

## 2. The aim ray

### 2.1 What HLA does

**The pose that the game reads.** Each bindings file of HLA binds the hand
pose to the raw pose of SteamVR. This is true for each controller type. [G]
`game/hlvr/cfg/bindings_touch.json`:

```
"output" : "/actions/default/in/handposeright",
"path" : "/user/hand/right/pose/raw"
```

The game also has the hand skeleton of SteamVR as an input
(`/actions/default/in/HandSkeletonRight` in `cfg/actions.json`). [G]

**The menu ray.** The menu ray is the tracked pose with one fixed turn and
one fixed offset. [G] `client.dll`, function `0xa2880`, which reads these
variables:

| Variable | Default | Meaning |
| --- | --- | --- |
| `hlvr_client_ui_hand_pointer_rot_x` | 45 | Pitch of the ray in degrees. Positive is down |
| `hlvr_client_ui_hand_pointer_rot_y` | 10 | Yaw of the ray of the right hand. Positive is left |
| `hlvr_client_ui_hand_pointer_rot_lh_y` | -10 | Yaw of the ray of the left hand |
| `hlvr_client_ui_hand_pointer_rot_z` | 0 | Roll |
| `hlvr_client_ui_hand_pointer_pos_x`, `_y`, `_z` | 0, 0, -2.5 | Start of the ray in inches: 6.35 cm below the pose |
| `hlvr_client_ui_hand_pointer_forward_offset` | -1 | The start moves 1 inch back along the ray |

So each hand has a ray that points 45 degrees below the raw pose and 10
degrees toward the other hand. The function makes the turn with the usual
order of Source: yaw first, then pitch.

**The raw pose against the aim pose.** The render model files of SteamVR
give each pose of a controller against its raw pose. [G]
`SteamVR\resources\rendermodels\oculus_quest_plus_controller_right\oculus_quest_plus_controller_right.json`
(the Quest 2 and the Quest Pro files have the same numbers):

```
"openxr_aim"  : origin -0.007, -0.0389, 0.0095   rotate_xyz -39.4, 0, 0
"openxr_grip" : origin -0.007, -0.0018, 0.1019   rotate_xyz  20.6, 0, 0
"tip"         : origin -0.0167, -0.0252, 0.0247  rotate_xyz -37.4, 0, 0
```

The aim pose is 39.4 degrees below the raw pose. The grip pose is 20.6
degrees above it. From these numbers the aim point is at 0, -6.73, -7.35 cm
in the frame of the grip, 9.96 cm from the grip point. The aim pose is 60
degrees below the grip pose. These are the numbers of the Virtual Desktop
runtime in `controllers.md`, section 8.1. So the aim pose of SteamVR is the
aim pose that the mod gets. [I]

**The result for a Touch controller.** [I] From the two facts above (the
calculation is `aimcalc.py` in the scratchpad):

- Against the aim pose, the menu ray of HLA points 7.09 degrees toward the
  other hand and 5.99 degrees down.
- The ray starts 1.0 cm to the outside of the aim point, 1.0 cm below it and
  0.2 cm behind it.

| Ray of a Touch controller | Yaw toward the other hand | Pitch | Source |
| --- | --- | --- | --- |
| The aim pose of the runtime (`aim_preset=0`) | 0 | 0 | `controllers.md` 8.1 |
| The pointer of Meta's Interaction SDK (`aim_preset=1`) | 6.45 | 0.79 up | `controllers.md` 8.1 |
| The menu ray of HLA (`aim_preset=2`) | 7.09 | 5.99 down | this section |
| The `tip` pose of SteamVR for a Touch controller | 0 | 2.0 up | [G] the file above |

**Not proven.** Nobody ran the game for this. The result needs two things
that no file says in words. The first: the engine gives the game the raw
pose with no turn of its own. The start of the ray supports this, because it
comes out 1.4 cm from the aim point. The second: the Virtual Desktop driver
for SteamVR gives the same raw pose as the Oculus driver.

**Other controllers.** SteamVR gives each controller a `tip` pose, "the
position and rotation that are appropriate for pointing (i.e. with a laser
pointer)". [P] <https://github.com/ValveSoftware/openvr/wiki/Input-Profiles>
The `tip` of the Index controller is 40 degrees below its raw pose and 5
degrees toward the other hand. The aim pose of the Steam Frame controller is
40 degrees below its raw pose, with no yaw. [G]
`SteamVR\drivers\indexcontroller\resources\rendermodels\valve_controller_knu_1_0_right\`
and `drivers\frame_controller`. So the pitch of 45 degrees and the yaw of 10
degrees are Valve's own choice for the menu. They are 5 degrees lower than
SteamVR's own pointing poses, and they turn more toward the other hand.

**Controllers with no hand skeleton.** For a Windows Mixed Reality controller
the menu code moves the hand model 4 inches back and pitches it 50 degrees
down. [G] The variables are `hlvr_client_ui_hand_wmr_pos_x` (-4),
`hlvr_client_ui_hand_wmr_rot_x` (50) and
`hlvr_client_ui_hand_use_wmr_offset` ("0 Auto Detect, 1 Always, 2 never").

**The teleport and the gravity gloves do not use the menu ray.** Both aim
along the forward axis of the hand, not of the controller:

- The teleport arc starts at the attachment `vr_hand_origin` of the hand
  model and goes along its +x axis. [G] `server.dll`, function `0x35b6c0`.
  The lookup of the attachment is in `0x34be90`.
- The gravity gloves aim along the +x axis of the attachment
  `vr_controller_grabbity_gun_forward_pull`. [G] `server.dll`, function
  `0xd5d0e0`.
- In `models/hands/alyx_hand_right.vmdl` the two attachments are on the hand
  bone, at the same point, with the same axes. The model has the wrist at
  x = -2.5 inches and the finger tips at x = +4 to +4.7 inches. So +x is the
  line from the wrist to the fingers. [G]
- The files do not give the angle between the hand bone and the raw pose.
  [I] The game takes it from the hand skeleton of SteamVR at run time. The
  menu ray (45 degrees) and the fallback for a controller with no skeleton
  (50 degrees) say where Valve expects the hand. That is 6 to 11 degrees
  below the aim pose of a Touch controller.

**The weapon hand.** When the weapon hand aims the teleport, HLA turns the
arc 10 degrees about the up axis of the hand. The variable is
`vr_teleport_primaryhand_yaw_adjust` (-10), and the left hand has the
opposite sign. [G] `0x35b6c0`. The turn is away from the other hand. [I] The
off hand, which aims the teleport by default, has no such turn.

**The look of the menu ray.** A thin line (radius 0.1 inch) with a dot at its
end (radius 0.5 inch). [G] `particles/ui_laser_pointer.vpcf`. The menu sits
65 inches (1.65 m) from the player and is 40 inches wide and 60 inches high
(`vr_game_menu_distance`, `_width`, `_height`). The ray works to 128 inches
(`vr_game_menu_interact_dist`). [G]

**Small helps for the menu.** [G] The names and the defaults are read. The
meaning of each line is my reading of the name:

- `cl_panel_freeze_time_after_press` 0.5: the pointer does not move for 0.5 s
  after a press. The pull of the trigger then cannot move the pointer off
  the button. The help text of the variable says "time to freeze
  mouse/pointer motion after a mouse button press".
- `vr_worldui_interact_haptic_pulse` 0.13: a pulse when the pointer goes
  onto a control.
- `hlvr_client_ui_hand_point_finger_distance` 10: within 10 inches of the
  menu the hand points with a finger and can touch the menu.
- `hlvr_menu_touch_time` 0.3 and `hlvr_menu_touch_distance_repeat` 1.5: a
  touch must leave the panel by 1.5 inches before it counts again.

### 2.2 What OpenXR and Meta say

The OpenXR specification defines the two poses in words, with no numbers.
[P] <https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html>,
"Standard pose identifiers":

- The grip pose is for an object in the hand. Its -Z axis is "the ray that
  goes through the center of the tube formed by your non-thumb fingers, in
  the direction of little finger to thumb".
- The aim pose for a controller is "the ray that follows platform conventions
  for how the user targets objects in the world with the motion controller,
  with +Y up, +X to the right, and -Z forward".

So the runtime decides the aim ray. An app that draws its own hand is free
to aim along that hand. HLA does that.

Meta's page for the Touch Plus controller lists `.../input/grip/pose` and
`.../input/aim/pose` and gives no angle. [P]
<https://developers.meta.com/horizon/documentation/native/pc/native-touch-plus-controllers/>
The only numbers of Meta are in the Interaction SDK. Its pointer is the
controller pose turned 6.45 degrees toward the other hand and 0.79 degrees
up (`controllers.md`, section 8.1).

### 2.3 What the mod does today

- Until 2026-10-03 the aim ray was the aim pose of the runtime with no turn,
  from the aim point of the runtime. [M] `controllers.md`, section 2.7.
- The menu pointer, the teleport arc, the cone of the grab from a distance
  and the Creature commands all read this ray. A near press does not: it
  acts from the point of the Hand at the tip of the index finger. [M]
  `god-view.md`, section 10, and `hand.md`, "One point for each interaction".
- Since commit `c9bb998` of 2026-10-03 the ray of a Touch controller has
  three presets (`aim_preset`): 0 is the ray of the runtime, 1 is the
  pointer of Meta, and 2 is the menu ray of HLA from this file. The default
  is 2. A preset also moves the start of the ray. `aim_pitch_deg` and
  `aim_yaw_deg` add a trim, and the number pad changes the preset and the
  trim in the headset. [M] `controllers.md`, section 8.
- The mod draws the ray only in a menu: a warm white line of 2 mm radius
  that ends on the cursor. It has no dot. [M] `controllers.md`, section 3.6.
- A one euro filter smooths the aim of the teleport and of the Creature
  command on the land. The ray of the menu and of the grab from a distance
  has no filter. [M] `god-view.md`, section 14.5.
- Buck on 2026-10-03: the ray points "too high and to the right".

### 2.4 Changes, in order of value against cost

1. **Test the three presets in the headset.** Agent aim built the menu ray
   of HLA as `aim_preset=2` while this file was written, from the numbers of
   section 2.1. Numpad 5 goes through the presets. The test must show which
   preset is correct for the menu, and which is correct for the grab from a
   distance. Cost: none.
2. **Freeze the menu pointer after a press.** Keep the pointer pixel for a
   short time (HLA: 0.5 s) after the trigger goes down. The docs of the mod
   do not name such a rule. Cost: small, in the code that makes the pointer
   pixel. Do it only if Buck's clicks miss.
3. **Give the world ray its own trim, if one trim does not fit.** HLA aims
   the menu with one ray and the world with a second ray (the hand). The
   headset test can show that the menu is correct and the grab from a
   distance is not. Then add a second pair of trim keys for the world ray.
   Cost: small.
4. **A dot at the end of the menu ray.** HLA draws a dot of 1.3 cm radius
   where the ray meets the menu. Cost: small. Value: low, because the mod
   has the game cursor at that point.

Not for the mod: the turn of 10 degrees for the weapon hand. The mod has no
weapon in the hand.

## 3. The gravity gloves

### 3.1 What HLA does

All facts are from `server.dll`, unless a line says otherwise. The class is
`CHLVRHandUseController`. Its think function is at `0x3dde40`.

**The design.** The developer commentary: "we used a physical gesture to
activate the pull, instead of a button press, and then required the player
to actually catch the object instead of automatically attaching it to the
hand." And: "We set the object's launch velocity to result in a standard
flight time from pull to catch so the player can develop a rhythm that
eventually becomes automatic." [G] `closecaption_english.dat`.

**The cone.** [G] functions `0x3f33a0` and `0x3f3b40`:

- The axis is the forward axis of the hand (section 2.1).
- The tip of the cone is 10 inches (25 cm) behind the hand
  (`vr_hand_pull_coneoffset` -10). So the cone is already 14 cm wide at the
  hand, and a near object can be far from the axis.
- An object is in the cone when the line from the tip to its center is
  within 15.2 degrees of the axis (`vr_hand_pull_cone` 0.965, a cosine).
- For the useful items (ammunition, resin, a grenade that is not armed, a
  health item) the limit is 0.9 times that cosine: 29.7 degrees.
- The cone is 400 inches long (10.2 m, `vr_hand_pull_conelength`).
- The nearest point of the object must be 12 inches (30 cm) or more from the
  hand (`vr_hand_pull_mindist`). A nearer object is for the hand.
- The object must be light and small. One limit is a mass of 20 and a
  diagonal of 20 inches. A softer limit is a mass of 25 and a diagonal of 50
  inches.

**The other tests.**

- Line of sight: two rays go from the tip of the cone, one to the center of
  the object and one to 90% of its height. One clear ray is sufficient. [G]
  `0x3f3730`.
- The view of the head: the box of the object must touch a view around the
  direction of the head. The view is 60 degrees wide and 70 degrees high
  (`vr_gg_fov_max_for_target`, `vr_gg_fov_max_vert_for_target`) and 500
  inches deep. The object that is the candidate now does not need this test
  again. [G] `0xbe07d0`. So the gloves do not take
  an object that the player does not look at.

**The choice among candidates.** [G] `0x3f3b40`, at `0x3f4749`. The value
`dot` is the cosine of the angle between the axis and the object:

- Objects of the same class: the larger `dot` wins. If two values differ by
  less than 0.005, the nearer object wins.
- A useful item wins against a plain object if its `dot` is not more than
  0.02 lower.
- A plain object wins against a useful item only if its `dot` is 0.02
  higher.
- Weapons and armed grenades never win against a different class.
- The target of the moment has no bonus.

**The timing.** [G] `0x3dde40`:

- The search runs each 0.1 s, not each frame.
- A new result becomes the target only after 0.1 s, and only if the next
  search gives the same result (`vr_gg_target_select_time` 0.1,
  `vr_gg_target_change_time` 0.1). A result of "no object" removes the target
  with the same delay.
- The lock is a press of the trigger or of the grip (the action `GGLock`,
  on at 75%, off at 72%). While the lock is held, the target does not
  change, and no test of range or sight runs again.
- The lock still counts for 0.25 s after the release of the button
  (`vr_gg_lock_release_delay`).
- The lock ends when the object is more than 60 degrees from the axis of the
  hand (`vr_hand_pull_detach_cone` 0.5), measured from the hand.

**The flick.** [G] `0x357b80` and `0xc1d4d0`:

- The hand keeps its last 8 velocity samples.
- The pull starts when the velocity changed by more than 40 inches per
  second (1.0 m/s) between the oldest and the newest sample. The hand must
  also be faster now than before. The variable is `vr_gg_pull_gesture_speed`
  (5), which is the change divided by 8. [I] The samples are linear
  velocities in inches per second.
- The code has no test of the direction of the flick.
- The lock must be held, or released less than 0.25 s before.

**The launch.** [G] `0x3f54c0`:

- The aim point is the hand plus 6 inches (15 cm) up, at the time of the
  launch.
- The time of flight T is 0.6 s for 100 inches (2.5 m) or less. It is 0.8 s
  for 400 inches (10.2 m) or more. Between the two it follows a straight
  line (`vr_hand_pull_traveltime_min`, `_max` and their distances).
- The start velocity is that of a throw that arrives after T under gravity:
  `V0 = distance / T - 0.5 * G * T`.
- The direction of the flick leans the arc to its side. The lean is limited
  to 20 degrees at 100 inches or less and 10 degrees at 250 inches or more
  (`vr_hand_pull_angle_near`, `_far`).
- The object also gets a spin that brings it to the hold orientation at the
  arrival.

**In the air.** [G] `0x3e7690`:

- Each frame the game calculates the acceleration that the object needs to
  arrive at the hold point of the hand on time. The hold point is the place
  where the hand is now, so the object follows a hand that moves.
- The game applies the upward and sideways part of it, limited to 500
  inches per second squared (12.7 m/s2, `vr_hand_pull_acceleration_max`).
- The steer comes in with a ramp in the last 0.6 s
  (`vr_hand_pull_acceleration_start_time`), and stops 0.05 s before the
  arrival (`vr_hand_pull_acceleration_shutoff_time`).

**The catch.** [G] `0x3e7690` and `0xd64560`:

- The player must press again to catch. The press can come at any time
  while the object flies: for this object the reach of the hand is 45 inches
  (1.14 m).
- After the press, the game puts the object in the hand when it is 10
  inches (25 cm) or less from the hold point. It also does this at the
  arrival time.
- With no press, nothing holds the object. 0.1 s after the arrival time the
  pull ends, and the object falls as a normal object.

**The feedback.**

- A candidate gets a scan effect (`particles/weapon_fx/grabbity_gloves_scan`).
  [G] `client.dll`, `0x1ec9c0`.
- The lock plays `Grabbity.HoverPing` and makes the object glow. The end of
  the lock plays `Grabbity.HoverPingEnd`. The ping has half the volume when
  the object is more than 37 degrees from the view direction
  (`hlvr_grab_target_facing_dot` 0.8). [G] `client.dll`.
- While the lock is held, the hand gets a weak pulse each think. [I] 75 Hz,
  amplitude 0.1.
- The pull plays `Grabbity.Grab` and one strong pulse (amplitude 0.75, 11
  ms).
- In the last 0.125 s of the flight, when the object is nearer than 30
  inches (76 cm), the hand gets a medium pulse (0.40) each frame
  (`vr_hand_pull_catch_vibrate_time`). This is the signal to close the
  hand.

**Variables with no reader.** `vr_gg_activate_time`,
`vr_hand_pull_test_segments`, `vr_hand_pull_dynamic_min_gravity_scale` and
`vr_gg_fov_center_vert_offset` (debug draw only) do nothing in this build.

### 3.2 What the mod does today

All from `hand.md`, "The grab from a distance" [M]:

- A new target must be within 8 degrees of the aim ray, less the angular
  radius of the object (`far_grab_cone_deg=8`). A target stays to 14
  degrees.
- The score is that angle plus 1 degree times log2(1 + the distance in
  meters). A rival must be 2 degrees better.
- The range is 2.0 m from the head along the floor (`far_grab_max_m`). The
  grab from a distance is off while a pressable object is within 10 cm of
  the point of the Hand.
- A target that the land hides is out.
- The highlight is a halo behind the object: gold, or red outside the
  influence.
- The grip locks the target and shows a gold tether. After 60 ms, one of
  three things starts the pull: 0.8 m/s toward the head within 55 degrees
  (`far_grab_pull=80`), 12 cm of travel, or an upward flick of 1.0 m/s.
- The game picks the object up at once, and the mod draws a glide. The
  glide takes 0.30 s plus 0.05 s for each meter, 0.50 s at most. It rises
  by 15% of the distance.
- The object ends in the palm with no catch. The closed grip carries it.
- Pulses: 0.15 for 10 ms at a new target, 0.3 for 30 ms at the lock.
- Buck on 2026-10-03: "Distance grab is uncomfortable". His log had the
  object that he pointed at 6.7 to 7.9 degrees from the ray, at the edge of
  the cone (`controllers.md`, section 8.1).

### 3.3 Changes, in order of value against cost

1. **A wider cone with its tip behind the hand.** The cone of the mod is
   half the cone of HLA. Put the tip 25 cm behind the aim point, and set
   `far_grab_cone_deg` to 15 as an A/B against 8. With the tip behind the
   hand, an object 0.7 m away can be 20 degrees from the ray, and an object
   2 m away 17 degrees. The score of the mod already selects the object
   nearest to the ray, so a wider cone mostly adds reach. Cost: small.
2. **Hold the target while the grip is down.** In HLA the lock stops each
   test, and it ends only at 60 degrees. The player can then move the hand
   freely for the flick. Make sure that the lock of the mod does not end
   when the ray leaves the 14 degrees. Cost: small.
3. **The test of the view of the head.** Take a new target only if it is in
   a box of 60 by 70 degrees around the direction of the head. This removes
   the targets that the hand sweeps over while the player looks at a
   different place. Cost: small.
4. **Two rays for the line of sight.** One to the center and one to 90% of
   the height, and one clear ray is sufficient. A villager behind a low rise
   stays a target. Cost: small.
5. **A delay of 0.1 s before the target changes.** The mod has a margin of 2
   degrees. HLA adds time: a new target must win two searches that are 0.1 s
   apart. Use the two together if the halo still jumps between two objects.
   Cost: small.
6. **A pulse for the arrival.** A medium pulse train in the last 0.125 s of
   the glide tells the hand that the object comes. A weak pulse while the
   grip holds the lock tells the hand that the lock is alive. Cost: small.
7. **A quiet sound at the lock.** One short sound of the game at low volume
   when the lock starts, and no second copy while one plays. Cost: small.
8. **A catch as an option.** Valve says that the catch is what makes the
   pull feel like the act of the player. It needs a flight of 0.6 s and a
   wide catch zone (25 cm, and a press at any time in the flight). The mod
   lets the game pick the object up at once, so a real catch is a larger
   change. Ask Buck before any work. Cost: medium to large.

Not for the mod: the limit of 30 cm near the hand, because the mod has its
own rule. Also not the limits of mass and size, and not the classes of
useful items. The score of the mod already has the distance.

## 4. Throws

### 4.1 What HLA does

**What Valve says.** The developer commentary tells the history of the throw
in three games. [G] `closecaption_english.dat`:

- The problem: "A typical VR game running at 90 frames-per-second will read
  the sensor data from the tracked controllers once per frame. [...] when
  throwing, a user can easily move their hand 15 cm or more in that amount
  of time. Additionally, the player may release the object [...] at any
  point during their throwing motion."
- The Lab (2016) made a velocity from the positions of each frame, and took
  the average of some frames before and after the release. "it didn't always
  feel consistent, especially if the game's frame rate ever fell below 90
  frames per second."
- Moondust added a bias toward the high velocities. It also took the
  velocity from the motion sensor of the controller, "which is computed on
  the controller every 2 milliseconds", through SteamVR. "the throwing
  calculations were no longer tied to the rendering frame rate."
- HLA: "takes ten frames that preceded the release of the held object and
  averages the three frames bracketing the peak controller velocity in that
  time window."

**The release.** On a Touch controller the trigger and the grip each hold an
object. The hold starts at 75% and ends at 72%
(`click_activate_threshold` 0.75, `click_deactivate_threshold` 0.72). [G]
`game/hlvr/cfg/bindings_touch.json`. So the object leaves the hand at the
start of the opening of the fingers, not at its end.

**What the code does.** The disassembly agrees with the commentary and gives
the numbers:

- At the release, the client asks SteamVR for the hand pose at 10 times: now,
  and 1 to 9 steps of 0.011 s before now. So the window is 99 ms. [G]
  `client.dll`, function `0x5862f0`.
- Each sample has the linear and the angular velocity that SteamVR reports.
  The game does not make them from positions. [G] the same function.
- The game finds the sample with the highest speed. If it is the first or
  the last sample, the game takes its neighbor. The throw velocity is the
  plain mean of the linear velocity of this sample and of the two samples
  beside it. [G]
- The server multiplies the throw velocity by 1.6
  (`vr_throw_velocity_scale`). It does this for each release. There is no
  speed below which a release is a drop, no test of the object type and no
  speed limit. [G] `server.dll`, function `0xd53120`.
- The velocity is the velocity of the hand pose. The code adds no term for
  the distance from the hand to the center of the object. [G]
- The game does not give the angular velocity of the hand to the object.
  `vr_throw_ang_scale` exists, but each caller drops the result. [G] The
  object keeps the spin that it had in the hand. [I]
- There is no aim assist: no turn of the throw toward a target. [G]

**The second law.** If the controller has no hand skeleton, or if
`vr_throw_velocity_type` is not 0 or 1, the server makes the velocity from
positions. [G] `server.dll`, functions `0xc1d030`, `0xc1d970`, `0xc1de40`:

- It keeps 8 records of the hand (`vr_handinfo_history_size` 5 plus
  `vr_handinfo_history_ignore_size` 3).
- It ignores the 3 newest records.
- The direction is the mean of the directions of the others. The speed is
  the highest speed of the others, each limited to 2000 inches per second.

**Special cases.** [G] `0xd53120` and `0x3ee4d0`:

- Two hands: if the second hand lets go within 0.3 s
  (`vr_two_handed_throw_interval`), the object gets the mean of the two
  velocities.
- A drop that the game forces keeps the velocity of the object. The limit
  is 100 inches per second (2.5 m/s), and there is no factor of 1.6.
- A body that is not one rigid object gets an impulse, with a factor of 0.2
  (`vr_hand_release_impulse_scale`).

**After the object lands.** The physics engine does the rest. The surface
file has 369 materials. [G] `surfaceproperties/surfaceproperties.vsurf`:

- The median elasticity is 0.1. The default material has 0.25. [I] So an
  object keeps about 10 to 25% of its speed at a bounce.
- The median friction is 0.8.
- A body that does not move goes to sleep (`sv_phys_sleep_enable` 1).

### 4.2 What the mod does today

- The release is the opening of the grip below 35%, or a fall of 30% in 3
  frames (`grip_fast_open=1`). A game turn runs at once
  (`release_at_once=1`). [M] `hand.md`, "The release at once".
- The throw velocity is the peak (`throw_law=2`). The velocity of each sample
  is the mean over 24 ms on each side. The largest sample from the 120 ms
  before the opening wins. The samples are positions of the grip point in
  room space. [M] `hand.md`, "The speed of a throw".
- There is no gain. A time factor of 2 (`throw_slow_x10=20`) keeps the real
  arc and the real landing point, at half the speed. [M] "The throw
  physics".
- A release with a mean of 0.18 m/s or less over 80 ms is a set down, unless
  the peak reached 1 m/s. [M]
- There is no aim assist. [M] "The real hand".
- After the landing, the game does its own bounce and roll. Buck: the
  objects jitter and bounce too fast (the evening list, item 1).

### 4.3 Changes, in order of value against cost

The law of the mod is already the law of HLA: the peak in a window of about
100 ms, with a short mean around it. Four things differ.

1. **A low bounce after the landing.** This is Buck's open item. The number
   to aim for: an object keeps 10 to 25% of its speed at a bounce, and it
   comes to rest after one or two contacts. If the mod can find the bounce
   factor of the game for a thrown object, set it in that range for a throw
   from the Hand. Cost: medium, it needs the disassembly of the game.
2. **The gain of 1.6 as an A/B.** Valve multiplies each throw by 1.6, with
   no condition. A throw at the true speed of the hand feels weak to most
   players. Let `throw_gain_pct` act on `throw_law=2`, and give Buck a test
   key that goes between 100 and 160. Cost: small.
3. **The velocity of the runtime as a second source.** OpenXR gives the
   linear velocity of a pose (`XrSpaceVelocity`). It comes from the motion
   sensor and it does not depend on the frame rate. Add it as `throw_law=3`:
   the same peak rule, with the velocity of the runtime in place of the
   position difference. The log must say if the runtime gives no valid
   velocity. Cost: small to medium.
4. **Samples at fixed times, not one for each frame.** HLA asks the runtime
   for the pose at 10 times, 11 ms apart, at the moment of the release.
   OpenXR lets an app locate a space at a time in the near past. With this,
   a slow frame does not make a hole in the window. Cost: medium. Do it
   only if a log shows frames longer than 15 ms at a release.

No change:

- The set down rule. HLA has none, but the game of the mod needs one.
- The angular velocity. HLA does not use it.
- An aim assist. HLA has none.

## 5. The teleport

### 5.1 What HLA does

**The buttons.** [G] `game/hlvr/cfg/bindings_touch.json`, action set
`/actions/move`:

- The stick of the off hand starts the teleport. A push of more than 55% in
  any direction shows the arc. The release of the stick moves the player.
- While the arc shows, the direction of the same stick turns the marker
  (`/actions/move/in/TeleportTurn`, dead zone 5%). So the player selects the
  place and the direction to face with one thumb. `vr_teleport_feet_rotation`
  is 1.
- The stick of the weapon hand turns the player: left and right past 75% are
  a snap turn of 45 degrees (`vr_teleport_quick_turn_angle` 45). A smooth
  turn is an option, at 100 degrees per second
  (`vr_quick_turn_continuous_speed` 100).

**The four ways to move.** [G] The option texts in
`panorama/localization/hlvr_english.txt`:

- Blink: "Teleport to destinations with a brief screen fade. The most
  comfortable movement type." It is the default (`hlvr_movetype_default` 0).
- Shift: "Teleport to destinations with a fast linear movement."
  `vr_teleport_shift_velocity` is 1200 inches per second (30 m/s). [I] That
  is the speed of this move.
- Continuous and Continuous Hand: the stick moves the player.

**The arc.** [G] `server.dll`, function `0x363ee0`:

- The arc starts at the hand (section 2.1) and goes along its forward axis.
- The game limits the pitch: the arc cannot aim more than 18 degrees up
  (`vr_teleport_arc_max_pitch` -18).
- The arc is a chain of straight parts. Each part goes 12 inches along the
  aim (`vr_teleport_arc_forward_increment` 12) and also falls. The fall of
  the first part is 0.4 inch (`vr_teleport_arc_downward_increment`). The fall
  of each next part is 1.25 times the fall before
  (`vr_teleport_arc_downward_increment_scalar`).
- The game traces each part against the world. The arc ends where a part
  hits.
- [I] From these numbers, with the hand 1 m above a flat floor: a level aim
  reaches 4.6 m in 15 parts. The highest aim reaches 5.8 m in 20 parts. So
  the arc is short and steep at its end. It is not a parabola.
- `vr_teleport_arc_magnet_radius` is 10 inches. [I] The end of the arc goes
  to a valid place within 25 cm.

**The filter of the aim.** [G] `server.dll`, function `0x35b6c0`. Each tick,
for each of the three parts of the position and of the angles:

- `d` is the change against the filtered value of the tick before.
- If `d` is larger than the limit, the new value passes with no change.
- If not, the filtered value moves by `limit * Bias(d / limit, 0.2)`.
  `Bias(x, 0.2)` is `x` to the power 2.32.
- The limit is 5 degrees for an angle (`vr_teleport_arc_hand_filter_ang`) and
  1 inch for a position (`vr_teleport_arc_hand_filter_dist`).

[I] The effect: a tremor of 0.3 degrees in one tick moves the arc by 0.007
degrees. A steady turn of 45 degrees per second at 90 ticks per second has a
lag of 1.9 degrees. A fast turn has no lag. The filter has no time constant
to tune.

**A valid place.** Greg Coomer of Valve: "the main goals of which are to
ensure the end player position is a valid place for the player's body and
that the path to get there is viable." [P]
<https://www.uploadvr.com/valve-deep-dive-locomotion/> The variables
`vr_teleport_path_*` control the test of the path. [G] If the place at the
end of the arc is not valid, the game can use the last valid place within 36
inches (`vr_teleport_lastvalidspot_distance_sqr` 1296). [I]

**The marker.** [G] The arc is a thin line (radius 0.04 inch). It is yellow
green (242, 255, 0) for a valid place and gray (65, 65, 65) for a place that
is not valid: `particles/teleport/teleport_arc_valid.vpcf` and
`teleport_arc_invalid.vpcf`. The marker at the end is a pair of feet
(`models/teleport_feet/`), and more feet walk along the path
(`vr_teleport_feet_step_speed` 0.35). The developer commentary says why:
"We swapped the abstract cylindrical indicator used in The Lab for a pair of
holographic feet. The feet help provide a sense of physical presence and
indicated the direction the player would face upon finishing a teleport."
[G] `closecaption_english.dat`.

**The sound.** After each teleport the game plays footsteps and the sound of
body movement. [G] The sound events are `Player.TeleportFootsteps`,
`PlayerFoley.Teleport_movement` and `PlayerFoley.Teleport_movement_short`.
The number of steps follows the distance: the limits are 30, 80 and 150
inches (`hlvr_teleport_footstep_distance_1`, `_2`, `_3`), with 0.35 s
between steps (`hlvr_teleport_footstep_interval`). A place that is not valid
has its own sound, `PlayerTeleport.Fail`. The commentary: "The footstep audio
transformed how players described their movement through the world and
players would often refer back to movement in the environment as walking or
running as opposed to referring to game terminology." [G]

**Not found.** The length of the fade of Blink is not in the variables that
the script lists. `vr_teleport_delay_time` (0.3) and
`vr_teleport_controller_fadein` (0.03) have no known meaning.

### 5.2 What the mod does today

- The player holds A on the pointing hand, and the release of A moves the
  player. B stops the teleport. [M] `god-view.md`, section 14.
- The arc is a parabola from the aim ray: g is 9.8, and the start speed gives
  a flat range of 10 m at 45 degrees (`teleport_reach_m=10`). It has 48
  steps. No arc shows above 60 degrees. [M] section 14.2.
- The marker is a tube and a ring on the land of 11 cm (`teleport_ring_cm`).
  Cyan with a disc is a valid place. Red with a cross is not. [M] 14.5.
- A one euro filter smooths the aim (direction 0.9 Hz plus 6 Hz per rad/s,
  point 1.5 Hz plus 8 Hz per m/s). [M] 14.5.
- A valid place is on the map, above the sea, and more than 30 cm from the
  point under the head. [M] 14.3.
- The move is one cut. The heading and the scale stay. The doc says "short
  black blink", but the default in the code is no blink (`blink=0`). A pulse
  of 0.4 for 60 ms answers the move, and two ticks of 0.2 answer a red
  target. [M] 14.4.
- The right stick makes a snap turn of 45 degrees (`snap_turn=2`). [M]
  `god-view.md`, section 4.
- The mod plays no sound for a teleport.

### 5.3 Changes, in order of value against cost

1. **A sound for the move.** Play one of the game's own sounds when the
   player arrives, and a different one for a red target. HLA shows that the
   sound changes how the player understands the move. Cost: small, if
   `audio.md` has a call that starts a sample.
2. **Go to the nearest valid place.** When the arc ends in the sea or off the
   map, look for a valid place within about 25 cm of room. Also keep the
   last valid place for a short distance. The marker then stays cyan along a
   shore. Cost: small to medium.
3. **Decide the blink.** The doc and the code disagree. Blink is the default
   of HLA and Valve calls it the most comfortable. Give Buck the A/B on a
   test key, then correct the doc or the default. Cost: none to small.
4. **Let the stick turn the marker.** While A is held, the stick of the same
   hand is free. Let it turn an arrow on the ring, and turn the player to
   that direction at the cut. This helps a player who sits. Cost: medium.
5. **The filter of HLA as an A/B.** The one euro filter is a good filter.
   If Buck reports a slow marker or a marker that shakes, the dead zone
   curve of HLA is a second law to test. It is 10 lines of pure math. Cost:
   small.
6. **A calm color for a place that is not valid.** HLA uses gray, not red.
   Value: low.

## 6. Hands

### 6.1 What HLA does

**The hand model.** The hand is a skeleton (`models/hands/alyx_hand_right`)
with a glove on it. Its animation graph has one curl value for each finger
(`FingerCurl_Index` and so on), one target point for each finger
(`IKTarget_Index` and so on) and two grab modes (`GrabMode_IK`,
`GrabMode_FingerCurl`). [G] `animgraphs/alyx_hands.vanmgrph`. So the fingers
follow the fingers of the player, and near a surface each finger goes to a
point on that surface.

**The fingers near a surface.** A sensor of 8 inches (20 cm) around the hand
starts the finger traces (`vr_hand_conform_sensor_radius` 8, with 10 samples
for each finger). It reacts to any surface, not only to an object that the
hand can take. [G] `server.dll`, `0x34b130`.

**The hand does not go through the world.** The glove stays at the surface
(`vr_hand_glove_respects_physics` 1). A second, faint hand shows the real
position when the two are more than 1.5 inches apart
(`vr_error_glove_start_fadein_distance` 1.5). [G] the names and the
defaults. [I] the meaning.

**The object that a press takes.** [G] `server.dll`, `FindUseEntity` at
`0xd64560`. It runs each think:

- The start point is the hand origin.
- The direction is between the palm and the fingers: the palm direction plus
  half of the finger direction. That is 26.6 degrees from the palm toward
  the fingers.
- The reach is 6 inches (15 cm, `vr_hand_use_distance`), measured to the
  nearest point of the object.
- The object must be on the palm side of the hand.
- A sphere of 2 inches radius (5 cm, `vr_hand_use_sweep_radius`) goes from
  the hand to the object and must hit that object first. The radius is 4
  inches when the hand is inside the box of the object.
- The score of a candidate is `dot / d2`: the cosine to the direction,
  divided by the square of the distance. The highest score wins.
- For an object that moves faster than 10 inches per second, the reach is
  its speed times 0.1 s, from 6 to 45 inches
  (`vr_hand_catch_speed_scalar`, `vr_hand_use_distance_fast`). So the hand
  can catch a fast object from far, and the palm test is off for it.

**What shows before the press.** For the object at the hand: nothing. The
server does not send the candidate to the client, and the search function
makes no pulse. [G] and [I]. The feedback is the hand: the fingers go onto
the surface. A different rule lights the items that the player looks at.
An item glows when three things are true: it is inside its glow range, it
is within 32 degrees of the view direction (`glow_use_tolerance` 0.85), and
the line from the eye is clear.
[G] `client.dll`, `0x633e70`.

**The grab.** [G] `0xd52304`, `0xd58110`:

- Four kinds of object go to the hold pose of the hand: a small object (a
  diagonal under 10 inches), a fast object, an object with a grip point, and
  the target of the gravity gloves. The move takes 0.10 s
  (`vr_hand_pickup_pose_slerp_duration`).
- Each other object stays where the hand touched it. The offset then gets
  smaller only while the hand moves: the faster the hand, the faster the
  offset goes. A hand that is still shows no slide of the object.
- The hand that the player sees is locked to the object
  (`vr_hand_pickup_lock_to_object` 1).

**The pulses.** [G] `0xc19090` and its callers:

- Three levels: light 0.05, medium 0.40, strong 0.75. Each pulse is 11 ms,
  one frame.
- Strong: the pull of the gravity gloves. Medium: the catch zone. Light: the
  wrist pocket.
- A near grab and a drop make no pulse.
- The hand that hits the world makes a pulse by the force of the hit, at
  most one in 0.2 s (`vr_hand_collision_haptic_time`).
- An object in the hand that touches the world makes a pulse by the force
  of the contact.
- In the puzzles and in the photo mode: 0.02 each 0.05 s while the hand is
  over a point, and 0.25 to 0.40 when it goes onto one. [G] the
  `vr_hacking_*_haptic_*` and `vr_photo_mode_hover_haptic_*` variables.

**Which hand does what.** One hand is the weapon hand
(`hlvr_left_hand_primary`). Each hand has a gravity glove
(`hl_vr_dual_grabbity_gloves` 1). On a Touch controller the trigger and the
grip do the same grab. [G] A hint sits on the hand that must act
(section 7).

### 6.2 What the mod does today

All from `hand.md` [M]:

- The mod draws its own hands, 19 cm long, 1:1 with the real hand
  (`hand_real_cm=19`). The Hand mesh of the game is hidden.
- The index follows the trigger. The other fingers follow the grip. The
  poses are hold, point, flat, fist and relaxed, with a blend of 0.1 s. An
  object in the Hand sits in the palm.
- A glow disc at the tip of the index shows what a press will do
  (`hand_glow=1`). Gold is an object to take, pale gold is an object to
  activate, and pale blue is nothing. The pick reach is 4 cm
  (`hand_pick_cm=4`).
- Pulses (strength and time): 0.15 for 20 ms over an object, 0.35 for 50 ms
  when a pick up starts, 0.60 for 80 ms when the object is in the Hand,
  0.40 for 70 ms at a release, 0.60 to 1.00 for a slap.
- One hint card for each controller shows which button does what.
- Open from the evening list: a point that is vague (item 8), a fist while
  the Hand strokes (item 9), a glow that always shows (item 11).

### 6.3 Changes, in order of value against cost

1. **Put the highlight on the object, not on the finger.** HLA shows nothing
   at the hand. The glow of HLA is on the item. The mod has a halo for a
   target at a distance. Use the same halo for the object that a near press
   will take, and remove the pale blue disc for "nothing". This answers item
   11 of the evening list. Cost: small.
2. **Choose the near object by a score, from the palm.** The rule of HLA is
   the cosine to the hand direction divided by the square of the distance.
   The reach is 15 cm, and the sight test uses a sphere of 5 cm. The mod
   picks inside 4 cm of one point at the tip of the index. A score lets the
   Hand take the object that it faces when two objects are near. Scale the
   numbers to the size of the land. Cost: medium. It is in the area of
   agent hand.
3. **Short pulses.** HLA uses pulses of 11 ms and three strengths. The mod
   uses 10 to 100 ms. A long pulse at each event makes the controller feel
   busy. Test the pick up and the release at 20 ms. Cost: small.
4. **A pulse for a hit, by its force.** When the Hand or the object in it
   hits the land, give a pulse that grows with the speed of the hit, at most
   one in 0.2 s. Cost: small to medium, the mod must know the contact.
5. **Hide the correction in the motion of the hand.** When a large object
   goes to the palm, move it only while the hand moves. Cost: medium. Value:
   low.

No change: the 1:1 hand, and the fingers that follow the trigger and the
grip.

## 7. Hints and prompts

### 7.1 What HLA does

**One file holds all hints.** `scripts/instructor_lessons.txt` has 110
"lessons". [G] Each lesson has one line of text, the events that open it, the
events that close it, and the place where it shows. All the facts of this
section are from that file, unless a line says otherwise.

**When a hint shows.**

- Game events open and close each lesson. No lesson shows for a fixed time
  only: 89 of 109 lessons have `timeout` 0, and they stay until an event
  closes them.
- `delayed_show` gives a wait before the hint shows. The teleport lesson
  ("Push to move") waits 5 seconds and closes at the first teleport
  (`player_teleport_finish`). So a player who moves at once does not see it.
- `success_limit` is the number of times the player must do the action before
  the lesson stops for good. It is 1 for 43 lessons, 2 for 11 and 3 for 4.
  The count is by success, not by the number of times the hint showed.
- `instance_type` 3 lets all lessons of a type be open, but only one shows at
  a time.

**The example of the gravity gloves.** Each hand has three lessons, and the
state of the glove selects the one that shows:

| Text | Visible when | Hidden when |
| --- | --- | --- |
| "Press to lock target" | a target gets the highlight (`grabbity_glove_highlight_start`) | the highlight stops, or the lock starts |
| "Flick hand back to pull object" | the lock starts (`grabbity_glove_locked_on_start`) | the lock stops, or the pull starts |
| "Catch object in mid-air" | the object flies. This lesson has a `timeout` of 2 s | the catch (`grabbity_glove_catch`) |

So each text is on the screen only while its action is possible.

**Where a hint sits.** `vr_panel_type` gives the place: on the weapon hand
(51 lessons), on the off hand (25), on an object in the world (22) or ahead
of the head (5). The hint is on the hand that must do the action. The offset
from the hand is 8 inches up (`vr_text_handattach_offset` "0 0 8", 20 cm).
[G] `server.dll`.

**Which button.** A lesson names an action, not a button:
`"highlight_controller_action" "/actions/move/in/Teleport"`. The game then
lights the part of the controller that the player bound to this action. The
hint is correct for each controller and for each change of the bindings.

**How a hint looks.** [G] `panorama/layout/world_hint.xml` and
`panorama/styles/world_hint.css`:

- One panel in the world with one label. No icon, no picture.
- The text is short, in upper case, in a semi bold font, yellow to white
  with a soft glow. Examples: "Push to move", "Press and Hold to Grab
  objects", "Store items in wrist pockets".
- The panel is clear yellow, about half transparent, with a soft yellow
  shadow.
- It opens in 0.3 s (from 80% of its size, with a flicker) and closes in
  0.2 s.

**The sound.** A lesson plays `Instructor.StartLesson` when it opens. Two
sounds of the same lesson type are at least 4 seconds apart
(`gameinstructor_start_sound_cooldown` 4.0). [G]

**The rule behind it.** The developer commentary: "we try to teach elements
in ways that feel as natural as possible, entwined within the core flow of
the game so that you don't feel like the game just stopped to teach you
something." [G] `closecaption_english.dat`.

### 7.2 What the mod does today

- One card beside each wrist shows the hints of that hand (`hint_card=1`). It
  is a dark card with white letters of 8 mm, and the word of the button is on
  a badge. [M] `controller-hints.md`, sections 9.2 and 9.9.
- The card is 3 cm clear of the outline of the hand, at the depth of the
  wrist, and it faces the head. It fades in 0.25 s and out in 0.35 s. [M] 9.3.
- The card shows the tooltips of the game with words for the controller, and
  the prompts of the mod: Select, Release to go, Pull to fetch, Open to drop,
  Take, Hold to collect, Stroke or slap, Move the land, Menu, Teleport, Send
  the Creature. [M] 9.4.
- A prompt shows after a dwell of 0.3 s (1.5 s for the land), for 4 to 6 s,
  and 2 to 4 times in a session (`prompts=1`). [M] 9.4.
- The mod has no picture of the controller and no lit button. [M] 9.7.

### 7.3 Changes, in order of value against cost

1. **Close a prompt on the event, not on a clock.** "Pull to fetch" must show
   from the lock to the pull and then go away at once. "Release to go" must
   show while A is down. A prompt that stays for 4 to 6 s after its action is
   done reads as noise. Cost: small. The states exist in the mod.
2. **Count the successes.** Stop a prompt after the player did the action 1
   to 3 times, not after the prompt showed 2 to 4 times. Keep the counts in
   a file beside the ini, so that a prompt does not come back in the next
   session. Cost: small to medium.
3. **Wait before a general prompt.** Show "Teleport" or "Move the land" only
   when the player has not done the action for some time in a place where it
   helps. HLA waits 5 s for the teleport. Cost: small.
4. **One quiet sound when a new prompt opens**, with at least 4 s between two
   sounds. Cost: small. Use a sound of the game.
5. **Light the button on a picture of the controller.** This is step H4 of
   `controller-hints.md`. HLA shows that it is the right goal: the hint names
   the action and the picture follows the bindings. Cost: large.

## 8. Sound

### 8.1 What HLA does

HLA mixes each sound in a "sound stack", a small program of operators. [G]
`soundstacks/soundstacks_hlvr_core.vsndstck`. The stack `hlvr_base_3d` does
the work for a sound in the world. Its defaults, which a sound event can
change:

| What | Default | Meaning |
| --- | --- | --- |
| `volume_falloff_min`, `_max` | 100, 2500 inches | Full volume to 2.5 m, silent at 63 m. Most events set shorter ranges: 50 to 400 for the start and 300 to 1500 for the end |
| `volume_falloff_curve_amount` | 0.7 | The shape of the curve between the two |
| `spread_min`, `spread_max` | 10, 20 inches | A source nearer than 25 to 50 cm goes to the two ears, to a spread of 0.71 |
| `occlusion_interval` | 0.1 s | The game traces from the source to the listener 10 times a second |
| `occlusion_radius` | 60 inches | The size of the source for the traces |
| `occlusion_max` | 0.7 | The largest effect of a wall |
| `occlusion_max_velocity` | 1.0 | The effect changes by at most 1.0 in a second, so a wall fades a sound and does not cut it |
| `reverb_curve_max_distance` | 2500 inches | The reverb part grows with the distance. At the far end the mix is 0.8 reverb and 0.2 direct |
| `use_doppler` | 0 | Off, except for 122 events |

**The HRTF.** A sound event sets `use_hrtf` to send the sound through the
HRTF of Steam Audio. [G] The stack `hlvr_default_3d`, and
`soundstacks/mixgraph.vmix` with 20 processors of the type
`VPROCESSOR_STEAMAUDIO_HRTF`. Counts over all 126 sound event files:

- 1,331 events set `use_hrtf`, and 1,284 of them set it to 1.0.
- The amount of HRTF is `hrtf_mix` times the part of the sound that has no
  spread. So a source very near the head gets less HRTF and more plain
  spread.
- An event that does not set `use_hrtf` plays with plain volume panning
  (`calc_spatialize_vr`).
- 4,320 events set `occlusion_scale`. 3,186 of them use 0.6.

**The reverb.** One Steam Audio reverb processor is on the reverb bus
(`VPROCESSOR_STEAMAUDIO_REVERB`). The maps have baked reverb data (22
`.sareverb` files). The reverb changes at most 30 times a second
(`snd_steamaudio_reverb_update_rate`). [G] The sound paths of Steam Audio are
off (`snd_steamaudio_enable_pathing` 0). One event uses them. [G]

**What Valve says.**

- "The Head Related Transfer Functions available in Steam Audio were
  especially helpful, as they made it possible to accurately spatialize
  Barnacle sounds so that the players could tell that the sounds were coming
  from above." [G] the developer commentary, `closecaption_english.dat`.
- Old sounds with a narrow frequency range do not place well through an
  HRTF. The sound team added frequency content to some of them. [P]
  <https://www.asoundeffect.com/half-life-alyx-vr-sound/>
- Each weapon shot has four layers. The "head" and the "body" go through the
  HRTF. The low frequency layer and the tail do not. [P] the same page.
- The game has almost 2,000 impact sounds for more than 160 objects. It
  changes a sound with the speed of the impact and the hardness of the other
  material. [G] the developer commentary.

**Small sounds of the interface.** The ping of the gravity gloves
(`Grabbity.HoverPing`) plays at volume 0.15, through the HRTF, and a limiter
lets only one play at a time. [G] the sound event files.

### 8.2 What the mod does today

- All sound goes through the QMixer of the game, which does the 3D
  positioning in software. The docs do not say that QMixer has an HRTF,
  reverb or occlusion. [M] `audio.md`, section 2.
- The listener is the head, set each frame (`audio_head=1`). The voices of
  the advisors get a direction (`audio_advisors=1`). [M] section 10.
- Distances are in room meters: 1 m sounds as 40 game units
  (`audio_room=1`). A sound with no object plays at the ears. [M] 11.5.
- Flat effects and single ambient sounds get a place
  (`audio_flat_places=1`). The ambient bed and the music stay flat. [M] 11.6.
- Open: background sounds stop at times (the evening list, item 3).

### 8.3 Changes, in order of value against cost

1. **A floor on the distance, or a spread, for a source near the head.** In
   the God view the Hand and the objects in it are 30 to 60 cm from the
   ears. Plain panning puts such a sound hard in one ear. HLA spreads a
   source inside 25 to 50 cm to the two ears. Cost: small, where the mod
   sets the position of a sound.
2. **Feedback sounds are quiet and single.** For each sound that the mod
   adds (a target, a lock, a prompt): low volume, and one at a time. Cost:
   none, as a rule for new work.
3. **An HRTF for the sounds with a place.** This is the one thing that tells
   the ear that a sound is below. In the God view the land is at waist
   height, so most sounds are below the head, and panning cannot say that.
   Steam Audio is open source under the Apache 2.0 license. [P]
   <https://github.com/ValveSoftware/steam-audio> The mod cannot put it in
   QMixer. It must play the samples with a place through its own mixer, with
   the samples that `LHSamplePlay` gets. Cost: large. It is a track of its
   own, and `audio.md` must say if the mod can get the samples.
4. **Keep the flat layer flat.** HLA does not send low and long layers
   through the HRTF. The mod already keeps the ambient bed and the music
   flat. No change.

Not for the mod: occlusion by walls, because the land of the game is open.
Also not baked reverb, because it needs the maps in a form that the mod does
not have.

## 9. Comfort when the player only watches

### 9.1 What HLA does

HLA has no scene that takes the camera from the player. The developer
commentary gives the reason and the rules. [G] All quotes are from
`closecaption_english.dat`.

**The camera is the head.** "Since a VR game must always be rendered from a
virtual camera that matches the player's own point of view, we have limited
opportunities to control what the player sees."

**Do not move the player by force.** "In previous Half-Life games, we could
always forcefully move the player if we really needed them out of the way.
We made several attempts at finding a similar solution in VR, but found that
it often made players feel deeply uncomfortable or disoriented."

**A ride needs a cabin.** For the van and the train: "the outside world is
only visible through small windows. These outside views are necessary to
provide motion parallax, but the small window size ensures that the player's
field of view is always dominated by the vehicle interior, which is
naturally in the player's own frame of reference. This greatly reduces the
occurrence of motion sickness."

**An elevator is slow and half closed.** "We found that we could make the
elevator rides in Half-Life: Alyx comfortable enough for most players by
moving the elevator slowly and limiting how much the player could see out of
the elevator." The first design was more open. Valve added more cover "so
that the player always had some significant portion of the elevator in their
field of view".

**The player keeps the hands.** In each talk scene the player can move, look
away and handle objects. The characters answer only a small set of actions
while they give story information. [G] the commentary on the laboratory of
Russell.

**Options.** [G] `panorama/localization/hlvr_english.txt` and the variables:

- Blink is the default way to move, with "a brief screen fade".
- The turn is a snap turn of 45 degrees, or a smooth turn, or off.
- "Barnacle Lift" can be off. It is the one place where an enemy lifts the
  player.
- "Light Sensitivity" reduces the strength and the flicker of lights.
- The size, the width and the speed of the subtitles have three steps each.
- The view fades to black when the head goes into a wall
  (`vr_head_bubble_fade_enable` 1).
- The pause menu comes with a fade of 0.2 s
  (`hlvr_pause_postprocess_fadein`).

### 9.2 What the mod does today

- The cinema bars flag of the game starts the cut scene window. [M]
  `cutscene-window.md`, section 1.
- Black walls close around the player in 0.5 s (`cutscene_window_ms=500`). A
  stereo screen 2.6 m wide stands 2 m ahead at head height
  (`screen_width_cm=260`, `screen_distance_cm=200`). It is fixed in the room
  and does not follow the head. [M] sections 3 and 5.
- The camera of the game moves inside the picture. The picture has a view of
  70 degrees, and the bars cover 44% of it. [M] sections 6 and 7.
- Y or the left menu button skips the scene. The trigger is a left click.
  There are no hands, no glow, no hints, no teleport and no grab. [M]
  section 7.
- Since the commits of 2026-10-03: the screen starts ahead of the head
  (`cutscene_face_head=1`), nothing comes in front of the screen
  (`cutscene_depth=1`), and the subtitles are on a card under the screen
  (`cutscene_subtitles=1`). [M] `cutscene-window.md`.

### 9.3 Changes, in order of value against cost

The cut scene window already follows the main rule of HLA: the camera that
moves is in a window, and the player stays in a frame that does not move.
Three things differ.

1. **Give the room something to see.** In HLA the cabin fills most of the
   view, and the cabin is the reference of the player. In the mod the walls
   are black, so the only thing the eye can hold is the picture that moves.
   Draw a thin frame around the screen, and a dim floor or a dim grid in the
   room. Cost: small. Give Buck the A/B.
2. **A smaller window as a comfort setting.** A screen of 2.6 m at 2 m is 66
   degrees wide. HLA says "small windows". The key `screen_width_cm` exists.
   Name a comfort value near 1.8 m (48 degrees) in the README. Cost: none to
   small.
3. **Show the hands in the room.** HLA never takes the hands from the
   player. Hands that only show, with no function, tell the player that the
   body is still there. Cost: small. Give Buck the A/B.

No change: the fade. HLA uses 0.2 s for a change that the player did not
make. The walls of the mod take 0.5 s.

## 10. What the files did not show

- **The angle of the hand against the controller.** The teleport and the
  gravity gloves aim along the hand bone. The files give no angle between
  that bone and the raw pose (section 2.1).
- **The raw pose of Virtual Desktop.** The numbers of section 2.1 are from
  the render model files of SteamVR. The Virtual Desktop driver has its own
  folder, with a file for one Pico controller only. [I] For a Quest
  controller it uses the files of SteamVR.
- **The fade of Blink.** Its length is not in the list of variables.
- **The glow of an item.** The code that draws it (`hlvr_glow_behind` and
  the variables beside it) was not read.
- **The rate of the hand samples.** The hand keeps 8 samples, one for each
  think. The time between two thinks was not found. So the flick window is
  "8 thinks", not a known number of milliseconds.
- **The catch after a release.** The catch needs a new press. The code that
  ends the lock press and starts the catch press was not read in full.
- **The mass unit.** The limits of 20 and 25 are in the unit of the physics
  engine. [I] kilograms.
- **The names of the object classes.** The code uses bits of the object
  flags. The names in section 3.1 are from the list of entity classes that
  set each bit.

## 11. The files of this research

All in the scratchpad folder `hla`. None is in the repo.

| File | What it is |
| --- | --- |
| `vpk.py`, `ccd.py` | Readers for the VPK archives and the caption file |
| `cvars.py`, `all_cvars.tsv` | The script that lists the variables, and its result for `server.dll` and `client.dll` |
| `pe.py`, `s2re.py`, `fn.py`, `callers.py`, `disp.py` | The disassembly helpers |
| `dec/`, `decc/` | The functions of `server.dll` and `client.dll` that this file names |
| `aimcalc.py` | The calculation of section 2.1 |
| `att.py` | The attachments of a model in model space |
| `x_hlvr/`, `vrfout/` | The text files and the decompiled files of the game |
| `cc_english.txt` | The captions, with the developer commentary |
| `re-common.md` | Notes on how to read the DLLs |
