# Roadmap

The plan after v1.0, and where each part stands. Update this file after each
milestone, so the work can continue from here at any time.

## Where it stands

v1.0 is built: the mod, the launcher and the installer. It plays with the
mouse and the keyboard, in stereo, with head tracking. The installer is not
tested by a person yet.

## This repo, from 2026-10-07

The mod moved to this repo (`github.com/Buckzor/BW1-VR`, private, by
invitation) so that testers and contributors can take part. The history
starts new at version 1.1.0. The text of the old history is in this file,
with the project name of the owner.

- **A clone builds with no more steps.** `third_party/` holds MinHook, the
  OpenXR headers and the pinned 32 bit loader, dxwrapper and cgltf.
- **`dev.ps1`** at the root: `setup -Game <folder>` one time, then
  `.\dev.ps1` builds, runs the math tests and puts the mod in the game
  folder of the contributor. `uninstall` puts the folder back.
- **No tags by hand.** `.github/workflows/build.yml` builds each push and
  each pull request and keeps an installer with the run. A release is made
  only when the owner starts `release.yml` on GitHub ("Run workflow"): it
  builds `main` and names the release from the version, the date and the
  commit. The first line of `bwvr.log` names the build ("build <commit>").
- **The desktop test runs** are in `bwvr/tools/desk/` (they were in a
  working folder of one machine). `verify_build.ps1` is the check before a
  merge.
- **`bwvr/tools/check_repo.ps1`** and a git hook stop a commit that holds
  a file of the game.
- License: MIT for the code and the documents of the mod.

Checked on the day of the move: a new clone builds and passes the 2748 math
checks with no more steps, and `dev.ps1 install`, `status` and `uninstall`
work on a test game folder (9 files in, the folder as before after it).

`verify_build.ps1` passed from this repo at 16:30 (runs aim2n2, tlandn2 and
vcn2, 40 to 54 fps): the far fetch, the teleport 0.000 units off, a throw
that landed, and the leash click on the Creature, with the profile put back
after each run. It could not pass before the fix of item 9 of the list
below, because the game could not start on that PC.

Open on the day of the move: the first build on GitHub has not run yet, so
the two workflows are not proven. Collaborators of a private repo of a personal
account all have write access, and GitHub does not protect `main` there on
the free plan. An organization gives read access for testers.

## Alpha 1 for testers, 2026-10-07

Buck wants to give the mod to a small group of testers. It is not a
public release. The game folder must not go out: it holds the whole game,
his profile and saved games with his name, his logs and his test settings,
and the game needs its registry values on a different PC.

- **Version 1.1.0** (`294814d`). `depth24` and `overlay` are 1 by default
  now. Those were the only two settings in which a new install was
  different from the setup of all headset tests. The README says "alpha 1,
  for testers" and lists the known faults of the list below.
- **The installer**: `bwvr\out\Black & White VR Mod 1.1.0 alpha 1 Setup.exe`
  (6.0 MB, SHA-256 `7B837AFE...DE32D35`), made by `bwvr\tools\package.ps1
  -TestBuild` after a clean build (2748 checks). The script checked the
  three third party binaries against their SHA-256 and found none of the
  author's names in the 9 files. The version must be three numbers, so the
  patch number counts the alpha builds (the next one is 1.1.1).
- **Checked**: the test variant installed into an empty folder as the
  current user (exit code 0, the 9 files, the mod DLL the same as the
  build). The test variant writes no registry value, but it makes a Start
  Menu shortcut and a desktop shortcut: remove both after such a test.
- **Not checked**: the real installer on a game folder (it needs the
  rights of an administrator and writes the uninstall values), a PC or a
  headset other than Buck's, the uninstall.
- A tester needs: Black & White with the unofficial patch v1.42 (the
  installer tests the exe), a PC VR headset with an OpenXR runtime (only
  the Quest 3 through Virtual Desktop is tested), and Windows will warn
  about a file with no signature.

## To do, from the session of the video, 2026-10-07

Buck played the build of `0b53680` and recorded a video of it (the Quest's
own recorder, through Virtual Desktop). No work is started on this list.
His logs are saved as `buck-2026-10-07\` in the session scratchpad: 22
files, with seven sessions of that morning. The long ones ended at 11:14
(42578 frames, 120 lines about wood), 11:29 (67782 frames) and 11:44 (66904
frames). No session ran on 2026-10-05 or 2026-10-06.

1. **A soft lock in the quest of the sailors.** The sailors asked for wood,
   and he dropped a tree on their boat. After that the boat took no wood,
   the subtitle "Trees won't work, they need prepared wood" stayed, and
   when he tried a different quest the game froze: it could not go on. Not
   known: a fault of the mod (an interaction that is broken, for example
   the object that a release acts on, the take from a forest, or the place
   that the scripts see) or a fault of the plain game. Find the session in
   the saved logs first, then try the same steps in the plain game.
2. **The cone of the far target does not fit the objects.** It is too large
   for some and too small for others, and he "was really struggling with
   some quests". In the quest to save the children the highlight went to
   trees, not to the characters that he wanted. His guess: the cone uses
   the collision model.
3. **A held trigger on the land only sends the Creature to walk.** When the
   aim is on the ground and the trigger is held, the command must stay a
   walk command and never change to an object. He could not send the
   Creature to a place beside trees, because the trees took the highlight.
   This changes the live target of 2026-10-04 (`creature_command_rule=2`).
4. **The zoom moves the view forward and back.** A change of the zoom must
   keep the player over the same place of the land. Today the player
   drifts while he zooms.
5. **The advisors are very large at times.** For example the good advisor
   is very large while he points at a quest scroll, and then he goes back
   to the normal size. His guess: the game uses a size in the world when
   the advisor is at a place in the world and a size on the screen when he
   floats in front of the player, and the size in the world is too large
   in the God view.
6. **The mark of the teleport** must scale with the zoom, or have one size
   in the world.
7. **The cone of the far grab and of the highlight.** Maybe larger, as in
   Half-Life: Alyx: he did struggle to select the right object. (On
   2026-10-03 he kept the small cone, because the game has many more
   objects than Half-Life: Alyx.) His ideas: a better order of priority
   for the targets. Or a small cone to get a target and a larger cone to
   keep it, so that a lock is not lost quickly. This goes with item 2.

8. **A held grip on the far Creature makes it stay** (added 13:27). When
   the Creature is the far target (the highlight is on it) and the grip is
   held, the Creature stops and stays where it is, for as long as the grip
   is held. It was hard to interact with the Creature while it ran to
   some place. To decide in the design: a short grip on the far Creature
   changes the leash today, at the close of the grip (`far_leash=1`), and
   a held grip does nothing more. So the short press and the hold need a
   rule that keeps both. And find how the plain game makes the Creature
   stop and attend to the Hand, so that the mod uses the game's own way.

9. **The game stops after the logos** (found and solved on 2026-10-07).
   Each start of the game stopped when the logo screens were over: the
   desktop runs from 14:00, and the owner's start in the headset at 15:43.
   The cause is the plain game and one service of Windows: the game asks
   for an internet connection (`LHCheckForInternetConnection` calls
   `RasEnumConnectionsA`), and the Remote Access Connection Manager of that
   PC did not answer, so the main thread waited forever. `rasdial.exe`
   waited too. The installer, the new repo and the mod were not the cause.
   The mod now gives the answer itself, "no connection" (`net_check=0`,
   `net_check.cpp`, `docs/intro.md` section 12), so a PC in that state can
   start the game.

10. **A crash at each exit of the game** (found 2026-10-07 in the event log
    of Windows, not looked at yet). Each session of that morning ended with
    two crash records at the time the game closed: the faulting modules are
    `VirtualDesktop.LibOVRRT32_1.dll` (exception `0xc0000409`) and
    `nvd3dum.dll_unloaded` (`0xc0000005`). The player sees nothing, because
    the game is closing. Find the order in which the mod, the OpenXR
    session and the Direct3D devices shut down, and whether the crash can
    cut the last lines of the log or a write of the profile.

Items 2, 3 and 7 are one package (the choice of a far target). Items 4 and
6 are one package (the zoom of the God view). Items 1, 5, 8 and 10 each
need their own research.

## Headset test, 2026-10-04, evening

Buck played the build of `be11418`. "Everything you worked on feels much
better", and "the game is almost playable now". He wanted to record a video
and stopped because of one fault. His log is `buck-2026-10-04bwvr.log`
in the session scratchpad (72607 frames).

1. **A miracle in the Hand cannot be cast or cancelled.** A quest gave the
   Creature strength miracle. He picked it up and did not find how to make
   it active. After random presses it was active and stuck to the Hand: the
   potion moved on the land where he pointed, with a red trail. The grip
   and the trigger did nothing, also on the Creature: only a yellow X and
   the sound of a refusal, with no reason on the card. No button and no
   shake cancelled it. His log: the grip took `OneOffSpellSeed` near frame
   28500 and the card said "Open to drop". From frame 63137 to 71501 each
   press was "with SpellSeed in the Hand. The pick record holds nothing",
   in Hand state 8.

**A miracle in the Hand** (`68432bd`, `3c17504`, `4505b7c`, `4e6a1ff`.
`docs/hand.md`, "A miracle in the Hand". New `miracle.cpp`,
`miracle_math.h`).

- **The causes.** (1) A press shorter than 225 ms taps a one-off seed and
  makes it active, a longer press picks it up as an object, and nothing
  told the player (his press that worked was 155 ms). (2) A miracle is
  Hand state 8, and the hooks of the mod acted only in the states 1, 4
  and 7, so the game put the Hand on the land under the pointer. (3) With
  an object in the Hand the mod left the pick record to the game's pick at
  the pixel: at his 116 presses it held nothing 100 times, a tree 15 times
  and never the Creature, so the game refused the cast (`fn_005D18F0`).
  (4) The game's shake is the scribble gesture, and the mod drops the
  samples of the pointer while the controller owns it.
- **A crash, found on the way** (`68432bd`): the first cancel of a miracle
  ended the game. The sound code of 2026-10-04 called vtable slot `0x10`
  as `Base::Get3DSoundPos` on the object of each sound, and the sound of a
  cancelled miracle has a `PHandFX`, which is no `Base`. An RTTI test in
  `audio_object.h` corrects it. Windows also logged a crash at the end of
  his session (18:17:57, `0xC0000005` in an unknown module). Not examined.
- **Now** (`miracle_hand=1`, Ctrl + Numpad 8 for the A/B): the Hand with a
  miracle is at the real hand. The target is the body within 3 cm of the
  Hand's point, else the body within 6 degrees of the aim and 4 m, with
  the gold halo, and it is in the pick record when the game reads it. The
  grip casts by kind: a Creature miracle on the target. Heal, forest,
  teleport and explosion at the land under the Hand. Food, wood, water and
  lightning pour while the grip is held. Fireball and flocks: close, swing,
  open. Storm and the shields: close, draw a circle, open. A shake of the
  real hand or a short press of B takes the miracle out of the Hand
  (`miracle_shake_mps`, `miracle_cancel_ms`). A seed that the Hand carries
  becomes active with the trigger. The card says what the grip does, how to
  cancel, and the reason of a refusal for 4 s.
- **Runs** (mirA to mirQ of the agent, mirV1 and mirV2 on the build of
  main): the fault with the key off (the Hand 22.2 cm off, each press
  refused, no cancel). With the key on: Creature Strong cast on the
  Creature at the Hand and in the aim, the cancel by shake and by B, a
  Fireball thrown, Water poured, Heal, a shield and a storm from the
  circle. Not run: a miracle of a Worship Site (the test game has none),
  food, wood, lightning, forest, teleport, explosion, flocks, the other
  Creature miracles, each refusal but "needs a Creature".
- A clean build of `4e6a1ff` (2310 checks) passed the two miracle runs and
  the three runs of `verify_build.ps1`. It is in the game folder since
  19:54.

**Buck's list of 20:55** (he plays again on 2026-10-05, and said: work
through the night, and do not think about the quota).

1. The VR controllers did nothing in the game until he moved the mouse of
   the desktop onto the game window. His log of 18:00 has 15 lines "the
   hand cannot act as a mouse now: game not in front 1".
2. The next task: the game's own hand model on the hands, "a huge part of
   the charm of the game", and the model as an FBX file, because he wants
   to add polygons.
3. The hands must react to the world as in Half-Life: Alyx: the fingers
   move over objects, bend on surfaces, and start to grasp a thing when
   the hand comes near.
4. A forest gives only one tree at a time. The bulk take from a forest
   must work.
5. Each action of the plain game must be possible with the controllers.
   The left controller has almost no job. He does not think that he can
   enter the Temple. The few keys of the plain game need a control: maybe
   a radial menu.
6. The game did not crash at the end of his session of 18:17: he closed it.

**In work from 21:00** (the briefs `brief-input.md`, `brief-handmodel.md`
and `brief-controls.md` in the scratchpad): the agent input with items 1
and 4, the agent handmodel with item 2 (the export first), the agent
controls with item 5 (an audit document, then a radial menu on X of the
left controller). Item 3 starts when the hand model has landed.

**The hand export** (`0d935fa`, the tool in `bwvr/tools/hand_export/`). The
files are in `media\hand\` (not tracked, the game's assets): `bw_hand.fbx`,
`.glb` and `.blend`, the three textures, the good and the evil look as
their own files, picture sheets and a README. One mesh of 141 vertices and
169 triangles, 22 bones, 1 bone for each vertex, the blend shapes `good`
and `evil`, and 26 takes (each hand animation of `hh.HBN`). It is a left
hand, 19.0 cm from the wrist to the end of the middle finger. The game
makes the look with `Morphable::MorphVertices` and `MorphTexture`: toward
evil it moves the vertices and mixes the textures, toward good it mixes
the textures only. A read back in Blender 5.1 passed 53 of 53 checks.

**The controls of the plain game and the ring** (item 5. `2387300`,
`5f8db49`, `0a1a599`, `e5c28cb`. `docs/controls-audit.md`,
`docs/controllers.md` section 10, `radial_menu.cpp`).

- **The audit**: the 33 actions of the control map, the fixed keys, the
  mouse actions and the world clicks. With no controller way until now:
  the Temple (Space two times, F4 to enter, F5 to F9 for the rooms), the
  Creature (C), the kind of the leash (V and B), the bookmarks, the next
  town (Tab), the whole land (F3), pause and speed, the help (F1), the
  names and details of villagers, quick save and load, the Gods'
  Playground.
- **The ring** (`radial_menu=1`): hold X of the left controller, move the
  hand 5 cm toward an item and let go. Or push the left stick toward an
  item and pull the left trigger. The ring stays where it opened. Page 1:
  Temple, Creature, town, the three leashes, enter the Temple, more. Page
  2: bookmark set, go and back, pause, help, names, save. In the Temple:
  the rooms and the way out. An item writes its key in the key buffer of
  the game (`GKeyBuffer::AddKey` `0x005E1BB0`), so each gate of the game
  holds and no window focus is needed. The Creature item is a teleport to
  the Creature. An item that cannot act is dim and says why. One quad
  layer of 32 cm. The items and the button are ini keys
  (`radial_menu_items`, `radial_menu_button`). Ctrl + Numpad 1 steps
  through the two ways.
- **Runs** ctl1 to ctl7: the ring, each item of the table in the agent's
  report (Temple, enter, two rooms, leave, Creature, the leash kinds, town,
  bookmarks, land, pause, speed, names, the Playground). In the Temple the
  God view stands aside and the room fills the view. The grip on a door of
  the hall went to that room.
- **Not done**: a ray pointer in the Temple rooms (the pointer there is the
  pixel under the real hand, with no line), the pictures, sliders and
  check boxes of the rooms, save and load in a run. Not proven: a tap on
  the Temple door on the land, the leash posts, the totem, a scaffold, the
  miracle icon of a Worship Site. Suspects: a thumb on the left stick can
  open the ring, and a jump to the town, a bookmark or the land takes the
  heading of the game camera, so the world can turn.

**The game's hand on both real hands** (item 2. `e492d03`, `bf8f9d5`.
`docs/hand.md`: "The model of the Hand", "The game's hand on both real
hands", "A hand that the player edited").

- The mod reads the mesh, the bones and the textures of the Hand from the
  game folder of the player at run time (`hand_model=1`, 0 is the low poly
  hands). The left hand is the mirror. The poses and the analog trigger
  and grip give a curl for each finger, and a curl bends each bone by the
  game's own angles. The index tip of the mesh is the Hand's point.
- The look is the game's own mix by the alignment, and the light is the
  game's vertex light with its live light place and Hand color (it changes
  at sunset). The color agrees with the game's own Hand within 6 of 255.
- Cost: 0.097 ms a frame, less than the low poly hands (0.127 ms).
- **An edited hand**: if `bwvr-hand.glb` is in the game folder, the mod
  uses its mesh, with up to 4 bones for a vertex, found by the bone names
  `b00` to `b21`, with the shape keys `evil` and `good`. The mod finds the
  scale, the axes and the mirror from the joints, and each joint must be
  within 4 mm of its place. A file that does not fit is refused with the
  reason in the log. The reader is cgltf (MIT), cloned to
  `third_party\cgltf`, with a new section in `installer/Notices.txt`. A
  mesh made from the game's mesh is the game's art: the installer does not
  ship it unless Buck decides so.
- Runs hm1 to hm9: both hands in each pose and each look, a rock in the
  palm, the Creature state with the finger and the flat hand, a mesh of
  447 and of 922 vertices from the export, a file with a moved bone
  refused. Test keys: Ctrl + numpad minus goes through the file mesh, the
  game mesh and the low poly hands, and Shift + numpad minus steps the
  look.
- Limits: the game's finger animations are not played (a pose is a curl
  for each finger). The shadow on the land has the game's fingers. Only
  the headset can show how the mesh sits on the real hand
  (`hand_mesh_yaw_deg`, `_pitch_deg`, `_roll_deg` turn it).

**The controllers with the game window behind** (item 1. `291eb0a`.
`docs/controllers.md` section 9). The mod asked for the game window in the
real front in three places: `Usable()`, the drop of a press in
`h_SetButtons`, and the pointer, which needs the message `0x8009` of the
game's mouse thread (that thread stops when `gWindowActive` is 0). His log
has two episodes: the start to frame 1123 (the game window not in front,
the cursor on the taskbar), and about frame 65900 (the headset session went
to VISIBLE, one frame took 69.8 s, and the game thread had no active window
for 440 frames). Now (`controller_window_behind=1`) the logical focus is
the real focus or a headset session with the focus of the runtime. All
input in process uses it, the mod posts `0x8009` itself, and Esc goes to
the game window with `SendMessageA`. Each call that touches the cursor of
Windows keeps the real focus. Nothing takes the front by force. Run inA:
with another window in front the clicks, the land drag, the both button
mode and Esc worked, and the real cursor did not move. Run inB with the key
at 0: no press reached the game. Limits: a minimized game window stops the
draw, and F11, F12 and the keyboard test keys still need the real front.

**Many trees from a forest** (item 4. `d3a358e`, `b667371`). A fact first:
the plain game v1.42 has no carry of many trees. `GMagicHand` holds one
object, a `Tree` and a `BigForest` are no bulk source, and
`HandStateMultiPickUp` is dead code (the game's "multi pickup" is the take
of a pile). So this is new behavior of the mod (`forest_take=1`, 0 is one
tree as in the plain game): a grip closed on a tree for 600 ms with a
second tree within 10 cm makes one bundle of wood (the game's `PileWood`),
and one more tree goes in each 400 ms, also at a distance in the cone of
the aim. The opening stops the take and keeps the bundle, and the next
close and opening let it go. Runs: 3 trees gave 1126 wood, a `BigForest`
gave 24 trees and 8400 wood. **For Buck to decide:** the take changes
trees into wood, which the plain game does not let the Hand do, and a
`BigForest` gives 350 wood each step with no cap. Not proven: a throw of
the bundle, a let go over a store. hand.md now lists the 11 Hand states:
the Totem state (5) has no handling.

**The village with the real hand** (`d5d87c3`, `2f66f40`, `441bfd5`,
`d92f1b1`. `docs/hand.md`, "The village with the real hand").

- **A miracle of a Worship Site**: no code fault. The chain worked with
  `miracle.cpp`. It lacked a Worship Site in the test game (the "keep"
  start has none, a story script permits it). With test keys for a site,
  miracles and chants: a press of 3 frames on the Water icon put Water in
  the Hand at the real hand (the pool went from 20000 to 15000), B and a
  drawn spiral gave a power up, the pour sent 14 casts, and a tap of B
  gave back what the seed still held. A gesture with an empty Hand (B, a
  spiral, then a W) put Water in the Hand. An icon with too few chants
  charges, and a second tap stops it.
- **The totem** (Hand state 5, `totem_hand=1`, Ctrl + Numpad 4): the game
  moved the pole from the screen rows of the pixel, so a lift of 4 cm
  raised it 1.35 cm. Now the pole is where the real hand is (18 cm of the
  room for the full range). Three steps of 4 cm gave 22%, 44% and 67% of
  the villagers. The card shows "Worship: 22%, 10 of 44 villagers".
- **Scaffolds** (`scaffold_join=1`): two scaffolds did not join, because
  with an object in the Hand the pick record held what was behind the
  pixel. Now the scaffold at the Hand goes in the record. Take, join,
  split and set down ran.
- Not run: the Workshop (the test town has none), a building site from a
  scaffold, a villager dropped at a job, worshippers that make chants,
  miracles other than Water. Suspect: other held objects that go onto an
  object (food onto a store) still use the object behind the pixel.

**A ray pointer in the rooms of the Temple** (`3861340`. `docs/controllers.md`
section 11, `temple.cpp`). In a room the direct hand stands aside and the
pointing hand points with the ray of a dialog box (`temple_pointer=1`). The
trigger and the grip are both the Action button (`temple_click`). Two
motions that the player did not make are stopped: the camera of the hall
turned 15 to 30 degrees down with a low pointer (`temple_camera_follow=0`),
and the Move button on empty space turned the camera with the hand
(`temple_camera_drag=0`). Runs tpl1 to tpl6: a door, a check box on and
off, a scroll, the way out. Facts: the hall is the World Room, the Options
Room opens the options box, and a guide of a first visit has no skip. Not
done: the taps on the land (the Temple door, the leash posts), a save and
a load, the Challenge Room and the Library, a turn of the view in a room.
**A clash:** Ctrl + Numpad 4 is the test key of `totem_hand` and of the
Temple pointer.

**The fingers react to the world** (item 3. `2a52b4f`, `208e7a7`.
`docs/hand.md`, "Hands that react to the world", `hand_feel.cpp`). One
rule: a finger that is in the world takes the nearest curl at which it is
not. The world only adds bend, the trigger and the grip have no lag, and
the place of the hand never changes. On by default: the land
(`hand_feel_land`), with a faint pulse at the first contact. Run hf2: the
curls of the four fingers go from 12 12 12 12 at 7.8 cm over the land to
94 79 47 23 at 1.8 cm. Cost 0.011 ms a frame. Built and off, because no
picture proves them yet: the approach to a target with the game's
`can_pickup` pose (`hand_feel_approach`), the hold by the game's hold type
(`hand_feel_hold`), and bodies that the hand passes (`hand_feel_bodies`).
The idle wave is `hand_feel_idle=0`: the two frames of the game's `wiggle`
have the same finger bends, so a wave would be the mod's own, and Buck
decides. The game's rule for a hold pose is `Object::GetHoldType` (slot
`+0x58C`) and one moment of the animation by `GetHoldRadius`.

**An object in the Hand that goes onto a thing** (`2b6bbf2`, `6cdc389`,
`03c947d`, `26e4910`. `docs/hand.md`, the section of that name). The game's
rule: a press with an object in the Hand acts on the Creature that the
Holding state latched, or on the object of the pick record, when the held
object's own class test accepts that thing (`ValidToApplyThisToObject`,
slot `0x71C`). Else it sets the object down. The fault: the record held
what was behind the pixel (368 wood over the Village Store went on the
land, because the record held a villager). Now (`hold_target=1`, Ctrl +
Numpad 2, it replaces `scaffold_join`): the thing at the Hand that passes
the game's tests goes in the record, gets the gold halo, and the card
names the result ("Open to store the wood", "Open to give the wood to the
builders", "Open to make a forester"). Runs: wood at the Village Store, at
the Workshop, at a building site and at a planted scaffold, a scaffold on
a scaffold, a villager made a forester at the base of a tree, a building
site from a scaffold on open land. Not run: food, a tree, an animal or a
crop at a store, the altar, a gift to the Creature, the red halo outside
the influence. A Workshop made by a test key took 2306 wood and gave no
scaffold in the run.

A fault of the night: the commit `917005c` of the agent handfeel removed
two sections of hand.md that other agents wrote (431 lines). The agent
village put its section back, and the agent temple puts back "The door of
the Temple and the leash posts". The code was not lost.

**The approach and the hold of hand feel are on** (`917005c`, `24cc336`).
The approach: each finger goes to 0.55 of the curl at which it holds a
ball of the size of the target, and to the game's `can_pickup` at least. A
small target gives a claw with the thumb at the index finger, a tree an
open hand with a little bend. A held tree now lies with its trunk on the
palm (`hand_hold_trunk=1`: the game's own hold radius in place of the
radius of the crown). Pictures of run hf8 and hf9 (curls, thumb to little
finger): at a rock 35 12 36 31 33 against 0 12 12 12 12 with hand feel
off. A tree in the Hand 12 70 83 63 80 with the thumb out, against the fist
78 75 73 63 100. A villager stands on the palm with no finger in him. The
palm down hand lies flat on the land (hf11). The bodies part stays off:
the Creature of the profile is 2 cm across, the box of a house is not at
the point of the object, and the bend at a tree is too small to see in a
picture. Not shown: a tree taken with the palm to the side.

**The Temple, second round** (`47d0e55`, `2ea0981`, `a0061d1`).

- The test key of the Temple pointer is Ctrl + Numpad 6 (Ctrl + Numpad 4
  stays with `totem_hand`).
- **The leash posts** (`temple_taps=1`): the game gives a post to its pick
  as a ball with no mesh (`SendInvisibleDrawCollision`), and the choice of
  the real hand took candidates only from `SendObjectDrawCollision`. Now
  the three posts of the player's Temple are candidates. A press at each
  post changed the kind of the leash. The door of the Temple had no fault:
  it was a target, and a press put the player in the hall. At the scale of
  the start the posts are 1.4 cm from each other.
- **The Save Game Room** with the ray and the trigger (run tsave4): the
  door, the picture two times, OK, then the picture and Yes: a save in
  slot 0 and a load of it. Buck's slots 15, 16, 17 and 20 were the same
  before and after. A save has no name with no keyboard.
- **A turn in a room** (`temple_turn=1`): the stick of the pointing hand
  turns the player by the snap turn angle, and the sum goes back on the
  way out. A small cross marks the end of the ray.
- Not done: the Challenge Room, the Library, a value of the options box, a
  drag of a scroll. Suspects: the picture of a new save is flat magenta,
  the waist point is 8.4 units higher after a load, and in a dialog box
  inside the Temple the ray ended short of the pointer in the left eye.

**State at 03:40 on 2026-10-05.** Each agent has stopped. The weekly quota
is at 75%. The build in the game folder has all of the night.
`desk_run.ps1` now also removes an empty folder that a run made in the
profile (a new save slot).

**For Buck's test, in this order:** the hands (the game's mesh, the
fingers on the land, the form at a rock, a villager and a tree, the hold),
the ring (hold X of the left controller), the Temple (the ring item, the
ray in a room, the Save Game Room), a forest (`forest_take`: his decision),
a miracle of a Worship Site (the icon, a power up with B, the cast, the
cancel), the totem, wood at the store and at a building site, a villager
at a job, and the controllers with a different window in front.

**Open, not started:** a pass on the frame time, the bodies part of hand
feel, the Challenge Room and the Library, food and other objects at a
store, the readers of the engine camera that are not scripts, the hints
that count successes.

## Headset test, 2026-10-04, morning

Buck played the build of `e951f87`. "Most of the changes work really well
and everything is looking much more polished." His log is
`buck-2026-10-04awvr.log` in the session scratchpad (56119 frames, 266
presses of the grip, mostly at 29.6 units per meter). His list:

1. The finger strokes a Creature that is about as large as his hand. Move
   the change to the flat hand by about 25%.
2. The biggest: a scroll is still not reliable. The glow does not light at
   it at all. His log: at each press near the scroll, the mod measured its
   body 7.8 to 11.3 cm of the room from the Hand's point, outside the reach
   of 4 cm. So the body of the scroll in the mod is not where the scroll is
   in the picture, at his scales.
3. There is no way to tie the leash to a thing. A double press of the
   trigger must do it.
4. In the command mode, a held trigger that started on an object does not
   go back to the land target when the hand moves away, and the aim locks
   on to distant objects too easily. The land target must show as soon as
   the aim is on no object.

**The live target of a command, and the leash on an object** (items 4 and
3. `39dbf87`, `cdda8a7`, `8dca798`, `183d994`, `44a146f`. `docs/hand.md`,
`docs/controllers.md`).

- **Item 4, three causes.** The press fixed its kind (3 of his 7 presses on
  an object ended with "the aim left the target", and no arc could show).
  Objects took the aim too easily: of 114 object targets, 84 were more than
  3 m from the Hand, 57 were more than 3 degrees off the ray, and 67 were
  trees. And a press on the land had no land point in the headset (12 of 12
  in his log): the button pump runs after the draw, where the room of the
  God view is not known, so the press got the map of the immersive view.
- **Now** (`creature_command_rule=2`): while the trigger is held, the
  target is what the hand points at now. The land shows at once, an object
  after 100 ms in the aim. A tap acts on what showed at the press, a hold
  on what shows at the release. An object needs the aim within 3 degrees of
  its upright body (5 to stay), at most 4 m from the head, and it is out
  when the ray meets the land well in front of it
  (`creature_command_cone_deg`, `_keep_deg`, `_max_m`, `_behind_cm`). The
  aim is made one time a frame, inside the draw. Ctrl + Numpad 5 is the
  A/B.
- **Item 3.** The plain game ties the leash with a double click: the first
  click is a normal click (`0x5D`), and the double click sends packet
  `0x5C` with the object. It unties with `0x5C` and no object, or with the
  leash click `0x5F`. The mod: a second press of the trigger on the same
  object within 500 ms (`creature_command_double_ms`) sends `0x5C`. The
  first tap still sends the Creature at once. The card shows "Trigger x2
  Tie the leash" only on an object that can take the leash.
- Desktop runs at 2880 by 2880 (lv2a, lv1b, old1b, tie1a): the arc showed 2
  frames after the aim left a tree, and the halo 6 frames after it came
  back. Eight turns across the edge of the cone gave one change. The old
  rule sent the Creature to a tree with the aim 10 degrees away. Three
  double presses tied the leash (the picture shows the rope from the tree
  to the Creature), and the three ways back worked. Not run: the test key,
  the walk of the Creature to a tied object. Limits: the range of 4 m is
  in room meters, so a far zoom has almost no object target. The halo is
  one or two frames late at a change.

A clean build of `44a146f` (2154 checks) passed the three runs of
`verify_build.ps1` at 2880 by 2880 (aim2u, tlandu, vcu, 39 to 55 fps on the
desktop at that size). It is in the game folder since 13:00.

**Frame time in the headset, from his three logs** (the mean of the report
lines): 13.3 ms on 2026-10-03 morning (79.6 fps), 13.0 ms in the afternoon
(79.6 fps), 14.8 ms on 2026-10-04 (73.3 fps: wait 4.8, scene for two eyes
6.9, grab 1.7). The scenes differ (he played at 29.6 units per meter), so
this is no proof of a loss, but a pass on the frame time is due.

**The scroll, and the hand of a stroke** (items 2 and 1. `64bc355`,
`0d93b56`, `180cb80`, `e4eab89`, `5c6d85a`. `docs/hand.md`).

- **Item 2, the cause.** The scroll of a challenge is not active until the
  player clicks it. The game taps a scroll only while it is active
  (`ScriptHighlight::InterfaceValidToTap` `0x0070ADD0`). The script
  `ChallengeHighlightNotify` waits for `GAME_THING_CLICKED` on the scroll
  or on the Location of the challenge, and only then calls `SET_ACTIVE`.
  The plain game records the click in `fn_005D3930` before each tap test.
  The rule "one target" of 2026-10-03 kept a scroll that a press does not
  tap out of the pick record, so no click reached the script: no glow, no
  halo, no quest. His one success came 12 frames after he tapped a Creche,
  the Location of that challenge. The guesses before (the body, the trees,
  the script camera) were not the cause.
- **Now**: each scroll that the game draws is a target, with the ball that
  its own draw made in that frame (`scroll_draw`), a reach of 10 cm
  (`scroll_reach_cm`), and the glow, the halo and the pulse while it is
  not active too. The press calls `fn_005D3930` (`scroll_click`), and the
  far touch does the same. A press with no target logs the nearest acting
  object and the nearest scroll with its state in plain words.
- Desktop runs at 2880 by 2880 with the hand at the place of the scroll in
  the picture (hq3, hq4, hr1, hr2): at 100 and at 29.6 units per meter the
  scroll was active 5 frames after the press and the cut scene of the
  challenge started. The installed build of before (hq5, hq6) at the same
  points: "the grip pressed with no target". Not run: the scoreboard and
  the Creature scrolls.
- **Item 1.** His log has one stroke, at a ratio of 1.45, with the flat
  hand for all of it, and the flat pose is a flat hand in the captures. So
  the limit was not what he saw. The suspect: with the flat hand the glow
  and the ray of the stroke stay at the tip of the index finger, 12 cm
  from the palm that makes the contact. Landed: the limits moved by 25%
  as he asked (finger below 0.60 of the hand's length, flat above 0.90, a
  stroke starts flat from 0.75: `creature_finger_below_pct`,
  `creature_flat_above_pct`), and the hand opens 450 ms after a press on
  the Creature (`creature_open_wait`), where it was a fist until the state
  started. Ctrl + Numpad 3 is the A/B of the limits.
- **The point of the flat hand at the palm** (`7f9874e`, `be11418`,
  `creature_palm_point`, Ctrl + Numpad 9). The first build failed its run
  because of feedback: the palm comes from the mod's hand, the mod's hand
  stood on the target, and each frame added the offset of 11.4 cm again.
  Now the mod's hand stands on the tip and the offset is taken from it.
  Runs sc-p3, sc-p4, sc-p2: the glow is in the middle of the flat hand, at
  the tips with the key at 0, and at the fingertip for the finger. Not
  known: whether a slow real stroke with the palm stays a stroke.

**The player that a script sees** (`0e99285`, `3528044`, `7319977`.
`docs/god-view.md` section 16, `script_camera.cpp`). Buck: "that sounds
like it could be potentially game-breaking and should be addressed".

- **The fault.** The scripts of the challenges ask about the camera, and in
  the God view the game's own camera is about 400 units behind the player
  (257 on the map). 111 tests of the distance from the camera to a thing
  (limits 1 to 250, on the map) were false for each thing at the player.
  107 tests of "the camera sees a thing" used the matrix of that camera,
  so they followed neither the head nor the zoom.
- **Now** (`script_distance=1`, `script_view=1`, Ctrl + Numpad 7 for both):
  the distance is the least of the head and each tracked hand on the map,
  and the view tests use the place, the axes and the view angle of the
  head. The player is the answer only while the God view owns the frame,
  a camera mode of the player is on top, the cinema bars are down and the
  game does not move its own camera to a new place. Else the script gets
  the game's own answer. The "stale place" of the first build was the true
  place: the view was still at the shore while the game camera went to
  the Temple.
- **Runs.** `PointOutScroll` (limit 100) with the player 80 units away: the
  advisor comes out and speaks with the key at 1 (sc-s1), and nothing
  happens with the key at 0 (sc-s0). A gold scroll 80 units behind the
  head: with the key at 0 the advisors speak at once, with the key at 1
  they speak only when the head has turned to 45 degrees from it (sc-x0,
  sc-x1). A story to the pen: 1332 asks got the story camera (sc-d2).
- The log has one start line and one count in the report line of the God
  view, as Buck asked.
- Not changed: the 114 markers at "camera position",
  `GetFacingCameraPosition`, and the other readers of the engine camera.
  A side finding: `ChallengeHighlightNotify` compares with a
  `NotifyDistance` that no script writes, so those tests look dead in the
  plain game too.

Buck permitted test runs with no idle wait for this package ("Just start
tests as needed and I will go hands-off until the game is closed"). A
clean build of `be11418` (2233 checks) passed `verify_build.ps1` and is in
the game folder since 16:53. All of the list of the morning is in it.

A clean build of `180cb80` passed `verify_build.ps1` and went in the game
folder at 15:37, and the build of `5c6d85a` (2186 checks, with the limits
of the stroke hand) at 15:46. Buck asked for the runs with no idle wait ("Just run the
tests"), so `idle_seconds.ps1` and `desk_run.ps1` now honor a time in
`test-now-until.txt` (each other guard stays).

**The render is 2880 by 2880 again** after a headset session. `spawn_run.ps1`,
`aim_run.ps1` and `throw_queue.ps1` now read the size and choose the dialog
points.

## Headset test, 2026-10-03, evening

Buck played the build of `3918f87`. The log of his long session is lost: he
started the game again before the report, and each start made a new
`bwvr.log`. `buck-2026-10-03c.log` in the session scratchpad holds only the
last 4 minutes (the comparison of the menu ray with the ray of the Quest
menu). From `0b5088c` the mod keeps the logs of the 20 runs before as
`bwvr-old-<date>-<time>.log` beside the game. His list:

1. Throws feel a lot more natural (the time factor 2, the default). After
   the landing the objects jitter, and they bounce faster than feels natural.
2. The grab from a distance is not comfortable: the ray points too high and
   to the right, so he must point the wrist down. He compared the menu ray
   with the ray of the Quest menu. Match the native ray.
3. Background sounds, such as the large portal, stop at times. They come
   back after some teleports.
4. A moving object cannot be fetched from a distance: a ball that drifted in
   the sea, and he thinks on land too. The real hand can take it.
5. The trigger could not send the Creature to some sailors in the water,
   although they showed the highlight. A reach with the real hand worked.
6. The cut scene window is much better than the held shots.
   - a. Put the screen where the player looks when the cut scene starts.
   - b. Put the subtitles below the window. The depth clash inside the
     picture is hard on the eyes.
   - c. The sides of the window show at different places in the two eyes.
   - d. Close shots come out of the screen and make the eyes cross. Nothing
     must come in front of the screen.
   - e. Keep the transition in and out as it is.
7. Half-Life: Alyx is installed on this PC. Compare its throws, hints,
   teleport, sound, interactions and ray angles, and take what helps.
8. The point of interaction is still vague. A grip at a scroll took trees
   below it, and only many presses later the scroll.
9. The hand stays a fist while it strokes the Creature. The plain game
   showed a hand that tickles. Use a finger when the Creature is small in
   the view and the whole hand when it is large. A slap needs too hard a
   swing, and a backhand does not slap at all.
10. A press of the trigger or of a button must skip the intro of the game
    start (the logos and the intro movie). Not the cut scenes: he said so
    after the first reading.
11. The glow at the fingertip must show only when the hand is in place to
    act on something.

**In work.** Each agent has its own git worktree in the scratchpad
(`wt-<name>`, branch `agent/<name>`) and lands on main by a fast forward.
Two or three agents run at a time.

| Agent | Items | State |
| --- | --- | --- |
| cutwin | 6, 10 | landed 21:09, `6c443e3` |
| aim | 2, 4, 5 | landed 21:20, `94b87b2` |
| hand | 8, 9, 11 | landed 21:35 and 21:57, `e760c6a` |
| throw | 1 | landed 22:26, `926a91c` |
| audio | 3 | landed 22:30, `d5c7c22` |
| hla (research only) | 7 | landed 21:22, `519262f` |

**The cut scene window, second round, and the intro skip** (items 6 and 10.
`bcadd5c`, `97afde7`, `15e2526`, `7066028`, `6c443e3`.
`docs/cutscene-window.md`, `docs/intro.md` section 11).

- **6a.** When a cut scene starts, the screen goes 2 m ahead of the head of
  that frame: its yaw, level, at the height of the recenter pose. Then it
  stays in the room. F11 puts it at the recenter pose (`cutscene_face_head`).
- **6c and 6d had one cause.** The two eye pictures were at one place on the
  screen. So only infinity was on the screen plane, and each other thing was
  in front of it. An old capture of `3918f87`: 268 of 268 matched blocks in
  front, the nearest at 0.74 m, and the two pictures 0.5 to 10.7 cm apart at
  the edges. The walls were not the cause.
- **6d.** The two pictures are now the eye distance apart (the far limit is
  parallel eyes). The eye cameras are as far apart as the nearest thing
  permits: the screen plane is at 85% of the nearest depth, and life size is
  the cap. A cut snaps. A nearer thing takes the plane at once. A thing that
  left is held 2 s, then the plane goes back slowly. The nearest depth comes
  from the `DrawTriangle` batches of both eye passes (a grid of 16 by 16
  between the bars, the second nearest cell). The scripted camera and its
  view angle do not change (`cutscene_depth`, numpad 1 is the live A/B).
- **6c.** The two eye quads have one pose and one size, and each eye picture
  loses a crop on its outer side. So the opening is the same for both eyes.
- **6b.** The subtitle card takes the help text in the window too. It hangs
  in the plane of the screen, 4 cm under the scene, and the text is not in
  the picture then (`cutscene_subtitles`). `cutscene_subtitle_place=1` puts
  it under the whole opening.
- **6e.** No change. Proven again: the God view is built before the walls
  open, and the waist point does not move after that.
- **10.** In the intro of the game start, a new press of a trigger or of A,
  B, X or Y writes Esc to the game's last key field. One press skips all
  that is left (`intro_skip`). The grip does not skip. A cut scene has no
  skip.

Desktop runs: cw2b, cw9 and cw10 for the window, cw6 to cw8 for the intro.
A head that looked 30 degrees left got a screen turned 30 degrees. In 12
captures no block was in front of the screen. A close shot of two villagers
had its nearest block at 2.40 m (the rule gives 2.35 m), and the same story
with the limit off had 565 of 566 blocks in front, the nearest at 0.80 m.
The fake button cut the logo screens from about 800 frames to 36 and the
Lionhead logo from about 2930 frames to 2. Runs cw2b and cw4b showed a fault
(the measure gave the near plane in almost each frame, from vertices that no
triangle uses), and `7066028` corrected it.

The build of `6c443e3` (1676 checks, 78 to 91 fps in cw10, no failed hook)
is in the game folder since 21:20. Not seen: the cropped eye quads and the
card as layers in the headset, a real story subtitle, a real controller in
the intro, the comfort of a close shot. Limits: the plane stays near for
some seconds after a near thing left. A thing smaller than one grid cell
(4.4 degrees) can be in front. The advisors are in the measure, so the
scene behind them gets less depth.

**The aim ray and the far targets** (items 2, 4 and 5. `175ef85`, `005d9ae`,
`6b8e0a8`, `933dbcd`, `c9bb998`, `94b87b2`. `docs/controllers.md`,
`docs/hand.md`).

- **Item 2, the cause.** The mod had no turn of its own, and the menu ray
  was the real aim ray. The aim pose that Virtual Desktop gives is the
  Oculus controller pose (60 degrees down from the grip pose, 9.96 cm from
  the grip point, and Buck's log gives 9.96 cm). The pointers that a Quest
  player knows are turned against that pose:

  | Pointer | Toward the other hand | Pitch | Source |
  | --- | --- | --- | --- |
  | Meta Interaction SDK | 6.45 | 0.79 up | `OVRPointerPoseSelector` |
  | Half-Life: Alyx menu pointer | 7.09 | 5.99 down | the cvars of `client.dll` and the SteamVR render model files |
  | SteamVR `tip` | 0 | 2.0 up | the render model files |

  No document gives the ray of the Quest system menu. Buck said "too high
  and to the right", and only the Alyx pointer is lower. So the default is
  the Alyx pointer with its origin (1.0 cm outside, 1.0 cm lower, 0.2 cm
  behind): `aim_preset=2` (0 is the runtime, 1 is Meta), for the Touch
  profile only, mirrored for the left hand. `aim_pitch_deg` and
  `aim_yaw_deg` are a trim on top. Test keys with Num Lock on: numpad 5
  goes to the next preset, 8 and 2 change the pitch, 4 and 6 the yaw. A new
  log line gives the aim pose as seen from the grip pose for each hand.
- **Item 4.** The first cause was the aim. Three more faults: the test of
  sight counted the sea as land that hides an object under the sea plane.
  The lock did not follow an object that left the cone. The lock kept a
  bare pointer (it now tests the first word of the object, and ends when
  the game deleted the object).
- **Item 5.** The game refuses neither action over water. A command had no
  target less than 35 cm from the Hand's point, while the far grab showed
  its halo down to 15 cm. The press then went to the land point of the
  arc, and the sea has none, so nothing was sent. Now a command has no
  least distance, the object at the Hand is a trigger target, the target
  list holds the best 128, and a red far target that the Creature can go
  to shows the green halo.

Desktop runs at 2560 by 1440: aim1 (the far grab reads the trimmed ray),
aim1b (the Alyx preset moved the menu pixel 76 px left and 78 px down, for
81 and 68 expected), move1b (a still rock fetched, a rock in flight locked,
followed for 14.8 units and taken, a rock over the sea locked, and "the
target is gone" when the game deleted it under the sea), cmd1b ("the Hand
is on the Rock", and two presses "would send packet 0x5D"). The math test
has 1769 checks. Not proven: numpad 5 through the presets, a far fetch or
a teleport with a preset on a Touch hand, a fetch over the sea, an object
that floats, a command target 15 to 35 cm from the Hand, the green halo on
a red target. Only the headset can show which preset is the Quest ray.

Two runs of the build of `9172fdb` with the whole evening in it (aim2m and
move2m, 62 to 87 fps, no failed hook): the first story in the cut scene
window, then with the Alyx preset on a Touch hand a far fetch, a teleport
0.000 units off, numpad 5 through the three presets (the runtime, Meta at
0.29 up and 6.70 left with the trim, Alyx at 6.49 down and 7.34 left) and
the trim keys. A rock in flight toward the sea was locked, moved 30.1 units
under the lock, and the Hand took it. This build is in the game folder
since 21:31.

**One target at the Hand's point** (items 8 and 11. `a98a0e2`, `18c0f3b`,
`abb5626`. `docs/hand.md`).

- **Item 8, the cause.** Three paths of the game gave a press an object that
  was not at the Hand's point. The main one is the game's search at the
  palm, `fn_005D39E0`: the right press calls it when the pick record holds
  no object, and it takes the nearest object within 5 units of the land
  behind the middle of the Hand's mesh. With the real hand over the land
  that is a tree below the hand. Also, `UpdateInterfaceCollide`
  `0x005D5A70` took the mod's choice out of the record, and the game's pick
  at the pixel stayed for each object that the Hand cannot hold. The pick
  of 4 cm made the search at the palm more frequent.
- **Now** (`near_rule=2`): the pick record holds the mod's choice and no
  other object, and the search at the palm gives a press no object. The
  choice is made before the halo of the frame is drawn. The target has the
  halo of a far target (gold, or red outside the influence). The rule: a
  scroll in the reach wins, then a body that the point touches (1 cm,
  `hand_touch_cm`), the smaller of two, then the nearest. A tree is a trunk
  and a crown, a scroll is a ball, and the Creature is a choice. One log
  line for each press (`hand press`).
- **Item 11.** The glow was drawn on each frame ("900 of 900 eye passes over
  nothing" in his log). Now there is no glow with no target and none while
  the Hand holds an object (`hand_glow_idle=1` is the old glow).
- Test keys: the numpad point key changes `near_rule` between 2 and 1, and
  numpad 7 changes the glow.

Desktop runs: hs6 (the point 11.1 cm over open land: with the old rule the
press took a tree 31.9 cm away, with the new rule the Hand stayed empty),
hs3 against hs4 and hs5 (a tree at its crown: the old build picked up
nothing, the new one took the tree with the halo on it), hs2 (a scroll
tapped with both rules). Not run: a scroll with trees under it, a run at
his scale of 66 to 100 units a meter, the red halo, the Creature as a
choice. Paths that still act from the pixel: a bubble and a leash, a drop
while the Hand holds an object, and the trigger grab of the land.

A clean build of `abb5626` (1811 checks) passed the two runs again (aim2n
and move2n, 66 to 107 fps): each grip press reached the game with the
right record, the search at the palm ran 0 times, and the far fetch, the
teleport and the presets were as before. This build is in the game folder
since 21:45. The builds of 21:20 and 21:31 were not clean builds: MSBuild
does not track header changes in a build folder under Temp, so build with
`--clean-first`.

**The open hand on the Creature** (item 9. `6dd9001`, `0c64293`, `0cea636`,
`e760c6a`. `docs/hand.md`). The grip holds the Creature state, and the pose
code gave a fist for a closed grip. Now the hand is open: the index finger
when the body is less than 0.8 of the hand's length, the flat hand above
1.2, with a band between (`creature_stroke_hand`). The contact is at the tip
or at the palm. The fingers move with the path of the contact point, so a
still hand has still fingers. A slap: the game's test has no direction, and
the mod read the speed of the grip, which stays slow in a slap of the
wrist. The speed is now the larger of the grip's and the Hand's point's,
and `creature_slap_mps` is 1.0 (it was 1.5). `creature_stroke_hand=3` is
the stroke of `3918f87`. Numpad 3 goes through the four strokes, numpad 9
through the slap speeds. The Creature is no far target now (it was a "bulk
source", so a grip from a distance started its state or changed the
leash). **None of this ran with a Creature**: two desktop attempts (hc1,
hc2) did not reach one, because the touch needs a Creature that has a
player. Open risk: with `near_rule=2` the Creature is a target only when
the draw offers it to the mod's list. The numpad point key is the way back.

**The landing of a thrown object** (item 1. `a56329c`, `926a91c`.
`docs/hand.md`, "The landing of a thrown object"). The cause of the jitter:
after the first contact the object kept the rate of the flight (5 steps a
substep at 100 units a meter), and the game made its contact for 1. The
rock turned at 7.5 turns a second and its center changed vertical direction
11 to 15 times a second. Now (`throw_land=1`), from the first contact: the
rate goes to the game's own time while the object touches (time constant
0.10 s), a bounce keeps 50% of its speed (`throw_bounce_pct`), and the land
brakes the object at 60% of its own friction (`throw_land_brake_pct`). A
rock at 2 m/s on a steep slope: 0.15 to 0.19 m across the land (it was
0.68 m), 1 to 2 hops of 0.4 cm (4 to 5 of 2.2 cm), at rest after 2.5 to
3.0 s (2.1 to 2.5 s). The flight is the same: the first contact at 0.810 s,
0.7 cm apart. A throw right after a far fetch started at the glide point,
0.80 m from the hand, and now starts in the Hand. Test keys: Pause goes
through the landings 1, 2, 0, and Ctrl with Numpad Plus or Divide changes
the rate. Not tested: a villager, a pile of objects, the sea, a scale other
than 100.

**The head decides which sounds play** (item 3. `3c2d620`, `5c1a70d`,
`d5c7c22`. `docs/audio.md` sections 12 and 13). The cause: the game starts
and stops a positional sound by its distance from the game camera
(`GAudio::PlaySoundEffect` `0x00429E30`, and `LHSampleUpdate3DChannels` on
each game turn), and the ears are the head. A zoom step or a teleport moves
the game camera, so a loop came and went. Now (`audio_range=1`) the
distance from the ears decides the start and the stop. Runs with a sound
tag 0.60 m under the head: with the old rule the sound stopped at the
first zoom step and stayed off after a teleport (1 start), with the new
rule it played on (580 starts). The log tag `audio range` names each loop
that starts, stops or is refused. Ctrl Shift F12 has a third step for the
A/B. `audio_loops_keep=1` (a loop out of range keeps its slot, silent) is
built and not run, so it is off. Not known: which sound his "large portal"
is. Section 13 of audio.md is a plan of eight items for sounds that do not
seem to come from the world (first: no height cue, and a near sound hard
in one ear).

**The build of the evening.** A clean build of `d5c7c22` (1901 checks)
passed two runs (aim2r and tlandr, 70 to 108 fps): the three audio hooks
went in, the head decided 22 starts (1 that the game camera refuses), the
far fetch, the teleport 0.000 units off, and a throw that landed with
`throw_land=1`. It is in the game folder since 22:38. All six packages of
the evening list are in it.

**For the next headset test, in this order:** the Creature (a grip with the
hand on it must start the stroke, else the numpad point key), the aim ray
against the Quest menu (numpad 5, 8, 2, 4, 6), a scroll near trees, the
cut scene window (close shots, the sides, the subtitles under the scene),
the intro skip, a throw and its landing (Pause), the far fetch of the ball
in the sea, a sailor in the water, the portal sound after a zoom and a
teleport.

**Buck's decisions of 22:58.** He does not test more that night.

- The far grab keeps its cone of 8 degrees. The game has many more objects
  to act on than Half-Life: Alyx.
- Hints that go away after some successes: not now. Keep it as an option
  for later, behind the hint setting: with all hints on, each hint shows
  always. With another setting, a hint goes away after the player did its
  action a few times.
- The low bounce (built, `throw_bounce_pct`) and haptics: yes.
- The short grip on the Creature from a distance changed the leash, and he
  used it. Bring it back, and keep the Creature state away from a grip at
  a distance.
- The sound of the world: after the quota reset of 01:30, a night of work.
  Height, the character of distance, loud events that carry when zoomed
  out, an ambience that stays fixed in the world when the head turns, soft
  sounds for the player's own actions, and each more improvement that
  fits. "It's something we can do and forget about."

**Item 12, from Buck at 23:25.** In VR a new game starts at the edge of
the island, and a quest does not put him at the right place either. The
plain game puts the camera at the door of the temple with the Creature in
front. His log has the waist point at altitude 0.0, the sea. The guess: the
God view puts the player under the game camera, not at the place that the
camera looks at. He also gave the recipe for a Creature on the desktop: a
new game with the choice of the Creature of before.

**In work from 01:30** (the briefs `brief-audio2.md`, `brief-hand2.md` and
`brief-spawn.md` in the scratchpad): the agent audio with the sound
program, the agent hand with the leash from a distance, haptics, and a
desktop recipe that reaches a Creature (the proof of item 9 that is still
open), and the agent spawn with item 12.

**The night of 2026-10-04.**

- **A game that a background process starts does not get the front** when
  the taskbar has it, and the mod then drops the fake clicks. The agent
  spawn added `fake_controller_front=1` (`c7854d0`, only read with
  `fake_controller` on): a logical focus for the owner of the pointer and
  the game's pointer, the real focus for each call that touches the cursor
  of Windows. Run sp3 closed the start dialog with the taskbar in front,
  and the real cursor did not move.
- **Test runs wrote Buck's profile.** Each `-NEWGAME` run wrote save slot
  20, the save pictures and helpstats.dat (so before this night too), and
  the run mx4 of the agent audio wrote `creature.lhp` at 02:14:36. His own
  slots 15, 16 and 17 were not touched. A backup of the profile as it was
  at 02:40 is in `backup\Profile-<profile>-2026-10-04-0240`.
  `desk_run.ps1` now copies the profile before a run and puts each file
  back after it ("profile put back: 7 files restored, 0 new files removed"
  in the runs sp13 to sp17).
- **Item 12, the start place** (`8757f73`, `4d50c16`. `docs/god-view.md`
  section 15). The guess was wrong: `BuildGod` already put the focus of
  the game camera under the player. The cause: the view was built one
  time, at frame 1, while the start dialog is up and the game camera is at
  the shore. After "Keep your old Creature" the game plays no story. It
  makes a new `CameraModeNew3` object 55 frames after OK and brings its
  camera to the Temple, and nothing built the view again. A quest always
  has a story camera, and the view was built again at its end, but the
  focus went under the feet, and a script focus under the land gave a
  place 22 units off. Now a watch in `comfort_camera.cpp` counts a move as
  the game's own (a set from outside the update, a new mode object, a
  jump) and never a move of the player, and gives one cut when the camera
  is at rest. The look point of the camera goes 50 cm in front of the head
  (`god_spawn_ahead_cm`), and the head gets the camera heading. The scale
  stays, because it is the player's choice. `god_spawn=0` is the old
  place, and Ctrl + Numpad 0 is the live A/B. Runs sp3 to sp17: with "Keep
  your old Creature" the waist point is 1858.8 29.5 2528.7 at the Temple
  door and the Creature is 46 cm in front of the head. Teleports, a snap
  turn, zoom steps and a grab gave 0 new places. Not proven: a load, the
  Temple rooms, the real Temple key, a grab that moves the land. The
  recipe for a Creature on the desktop is `spawn_run.ps1 -Choice keep`.

- **The Creature on the desktop, the leash from a distance, haptics**
  (`637e0cf`, `aa5b262`, `84a86af`, `8a15cd1`. `docs/hand.md`, the last
  four sections). The recipe `creature_recipe.ps1` (`-Kind state` or
  `-Kind leash`) reaches the Creature at the door of the Temple.
  - **A fault that the recipe found.** The mod read the body bone of the
    Creature only when `LH3DCreature+0x526C` was not 0. That flag means
    "ready for the Hand": the game sets it after a press and clears it at
    the end of the press. So before a press the Creature was a plain
    cylinder at its map place and no target of a trigger command. Now the
    bone is read always (`creature_bone=0` is the way back).
  - **Item 9 is proven now** (runs hkT, hkD, hkA, hkC): the choice at the
    capsule, the press and the state, the index finger for a body of 8.2 cm
    and the flat hand for a large one, a stroke of 0.58 m/s, a backhand
    (grip 0.00 m/s, the Hand's point 1.31 m/s), a hard slap, and the way
    back `creature_stroke_hand=3`.
  - **The leash from a distance** (`far_leash`): the Creature in the aim is
    a far target of its own kind with a halo and a card ("Put the leash
    on", "Take the leash off", "Take the leash back"). The grip sends
    packet `0x5F` and no press, so no Creature state starts for a short or
    a long grip (runs hkU, hkT).
  - **Haptics** (`haptics_pct`, `haptics_math.h`): 21 kinds of pulse, none
    above 0.75 or longer than 40 ms, each at an action of the player.
    Seen in runs: target, lock, arrive, take, let go, leash on, leash off,
    command.
  - **Changes to know.** With the leash on, a short trigger press aimed at
    the Creature takes the leash off (before, it sent the Creature to the
    land under itself). A body that the point touches wins over a scroll
    in the reach, unless the body is a house, a tree or a forest.
  - **Open.** One run (hk6) had a slap from a step of 4 cm and a second
    slap with a still hand, not seen again in four runs. A zoom in the
    first second after the start dialog drops the move to the Temple. Not
    run: a leash tied to an object, the pulses teleport, refused, gesture.
- **The build of 03:25.** A clean build of `8a15cd1` (2098 checks) passed
  three runs through `verify_build.ps1` (aim2s, tlands and vcs, 55 to 114
  fps): the far fetch, the teleport, the presets, a throw that lands, and
  the leash click on the Creature at a distance with its pulse. It is in
  the game folder.

- **The sound of the world** (18 commits to `e951f87`. `docs/audio.md`
  sections 14 to 21, `tools/audio_measure.py`). Each item has an ini key
  for the old behavior, and Ctrl Shift F12 now has five steps that take
  one change away at a time.
  - **A capture** (`audio_capture`): what QMixer mixes for each ear goes to
    a file (22050 Hz, 16 bit, stereo), and a script gives the level, the
    spectrum by band, the difference and the delay between the ears. So a
    desktop run can measure a sound.
  - **Height** (`audio_mixer`, `audio_height`, `audio_height_pct`): the mod
    plays each sound that has a place itself, through a model of the head
    (Brown and Duda 1998, no data set of a third party). In the 8 kHz band
    a test voice is -7.4 dB at 60 degrees below, +1.4 dB level and +6.6 dB
    at 60 degrees above. Almost each sound is below the head in the God
    view, so the world is darker than before: only ears can set the amount.
  - **A near sound** (`audio_near`): a source at the side is 7.0 dB and
    794 us apart at 1 m, and 3.2 dB and 221 us at 20 cm.
  - **Distance** (`audio_far`): a low pass by the part of the sound's own
    range (12 kHz near, 5 kHz half way, 1.2 kHz at the end), and up to 25%
    with no direction. Off in a story.
  - **Loud events carry** (`audio_zoom_pct`): the distance factor grows
    with the square root of the scale. At 4000 units a meter a sound with
    a range of 300 units carries 1.2 m and one of 2000 units 7.9 m.
  - **The ambience** (`audio_bed`): the bed of the game is stereo, and it
    turns against the head yaw. In a test the low noise went from the left
    ear to the right ear over four turn steps. Single sounds of the
    atmosphere go toward the land of their kind.
  - **Soft sounds for the player's actions** (`audio_cues`,
    `audio_cues_pct`): a turn, a zoom step, a teleport, a grab, a lock and
    an arrival, made in code, each in the frame of its pulse.
  - **Loops** (`audio_loops_keep=1` now, proven with a loop of the game):
    a loop out of range keeps its slot, silent, and comes back. Effects of
    animations, physics and particles start by the head's distance.
  - **Balance**: the mixer has the power of QMixer for a source at the
    side. Advisors, story speech and music stay with QMixer.
  - Cost: 55 us for a block of 16 ms on QMixer's thread. Also a soft
    limiter, and `audio_latency_ms` (the stream is 116 ms ahead of the
    device, 25 gives 66 ms, the default is 0 until the headset shows no
    gap).
  - Not built: slots by loudness, two delayed start paths. Not run: the
    game menu, a load, a long session. The name of his "large portal" sound
    is still not known.
  - The run mx4 did not end by itself: the game wrote `creature.lhp` about
    74 s after its start, while it ran. So the game saves the Creature of
    the profile in play (not verified), and each test run of that length
    wrote it before the guard.
- **The build of 03:50.** A clean build of `e951f87` (2099 checks) passed
  the three runs of `verify_build.ps1` (aim2t, tlandt, vct, 57 to 108 fps,
  the mixer on). It is in the game folder. All of the evening and of the
  night is in it.

**The headset test of this build, in this order.**

1. Start with "Keep your old Creature": the view goes to the door of the
   Temple about 1 s after OK (Ctrl + Numpad 0 is the old place).
2. The Creature: the grip with the hand on it (open hand or finger, a
   slap, a backhand). The grip from a distance changes the leash.
3. The sound, 10 minutes (`docs/audio.md` section 21): the Creature from
   below and ahead, a bird from ear height, the head 20 cm from the
   Creature, the sea under four turn steps, voices at a far zoom against
   a rock or a miracle, a loop after a teleport away and back, the cues,
   the advisors as before. Listen for clicks, a wrong side, a lag at a
   fast head turn. Too dull: `audio_height_pct=60`.
4. The aim ray against the Quest menu (numpad 5, 8, 2, 4, 6).
5. A scroll near trees, and the glow.
6. The cut scene window and the intro skip.
7. A throw and its landing (Pause), the ball in the sea, a sailor.
8. The pulses of the controllers (`haptics_pct`).

**Not built:** hints that count successes (see above), a recipe that takes
its dialog points from the render size.

**The research on Half-Life: Alyx** (item 7. `9172fdb`, `519262f`.
`docs/research-hla.md`, 1240 lines, read from the game files on this PC and
from public sources. No code and no asset was taken). What the mod can use,
by value against cost:

1. The aim 6 degrees lower (built, see above).
2. The cone of the far grab: 15.2 degrees with its tip 25 cm behind the
   hand, a new target only inside 60 by 70 degrees of the head's view, a
   lock that holds until the target is 60 degrees off, and a change of
   target only after two searches 0.1 s apart. Ours is 8 degrees.
3. A low bounce after a throw: the median elasticity of 369 materials is
   0.1 and the median friction is 0.8 (given to the agent throw).
4. A throw gain of 1.6, with no cap and no aim assist. The peak law is
   like ours.
5. Hints open and close on game events and count successes (1 to 3), and
   most have no timeout.
6. Nothing shows at the hand before a near grab. Items glow when the head
   looks at them. A near grab has a reach of 15 cm and a score by the palm
   direction.
7. A pulse of the controller when a fetched object arrives, and a quiet
   ping at a lock.
8. The fetch is a ballistic flight of a fixed time (0.6 to 0.8 s) that
   steers to the hand, and the catch needs a new press.
9. The teleport: the aim filter is a dead zone (5 degrees, 1 inch), the arc
   end snaps to a valid place, and footsteps play after the move.
10. Sound: a source nearer than 25 to 50 cm goes to both ears, and the HRTF
    is the only cue for "below". Comfort: in each ride the cabin fills most
    of the view, and our cut scene window has black walls only.

**The desktop renders 2560 by 1440 now**, not 2880 by 2880, so the dialog
points of `wait_and_run.ps1` miss. `cutwin_next.ps1` in the scratchpad
computes the points from the render size. `intro_skip_test_frame=5` saves
about 50 s of logos in each run. A run needs the game window in front: the
mod drops the fake clicks when another window has the focus, so "test now"
does not work while Buck uses the PC.

## Headset test, 2026-10-03, afternoon

Buck played the build of `f194c59`. "Very good improvements all over." His
log is `buck-2026-10-03b.log` in the session scratchpad (54 throws, 137
Creature command lines). The hint card went out as a quad layer in his
headset. His list:

1. The hints are great now. But "Trigger: Move" shows, and the trigger no
   longer moves the land. The "Y Menu" hint showed at the wrong hand: each
   controller must show its own hints, as in HL:A.
2. The trigger commands the Creature much better. But a quick point and
   click does not send it: the trigger must be held a few seconds.
3. A quest scroll was hard to activate: the grab from a distance took trees
   instead. The interaction seems to be at the palm, not at the glow of the
   index finger. Turn the grab from a distance off when the hand is near an
   object.
4. The subtitles are sometimes too far apart in the two eyes to read.
5. Throws of rocks and people now feel too fast or too heavy, as if the
   physics ran twice as fast. Trees are still slow. Find a happy medium.
6. A forest gives no wood at all now. The bulk take at a distance works.
7. The shake takes the leash off well.
8. The leash is drawn below the hand.
9. The game's Hand still shows in the cut scenes that teach an action. Keep
   it for now.

**Built the same afternoon.** `fc1784b` builds clean, and the math test
passes 1497 of 1497. Run all4 at 17:30 (God view, 120 s, 56 fps, no failed
hook): the far fetch of a rock with the new near rule, the cards "[Grip]
Pull to fetch", "[Grip] Pick Up" and "[Grip] Drop" at the right wrist, a
throw at 2.61 m/s with the time factor 2.00 that met the land after 1.052 s
(twice the real 0.52 s), 0.2 cm of the room from the arc, and the teleport.
This build is in the game folder. Not run: the subtitle card and two hint
cards as quad layers (headset only), a scroll, a forest, the leash, the
Creature tap, a tree throw. Item 12 of the morning list (cut
scenes in the stereoscopic window) is in work.

**The cut scene window** (item 12 of the morning list, `d3649a5`,
`72e6ca2`, `9339256`, `3918f87`, `docs/cutscene-window.md`). The signal is
the game's own cinema state (`HelpSystem+0x45E8`). Bars up: four black walls
close in 0.5 s around a screen 2.6 m wide, 2 m ahead of the recenter pose,
and the frame is a play mode 1 frame: the game camera moves as the game made
it, the hands and cards stand aside, Y still sends Esc. At the end the God
view is built first, then the walls open. `cutscene_window=0` is the old
behaviour, and numpad 0 changes it live. Desktop runs: the opening story and
the story to the pen in the window, the live change both ways. Run all5 of
the whole build (`3918f87`, 1558 checks, 65 fps): the window from frame 609
to 2975, then the fetch, the throw with the factor 2 and the teleport as
before. This build is in the game folder. Not seen: the walls and the screen
as layers in the headset, subtitles and advisors in the window. The screen
is at the recenter pose: a player who turned must press F11.

| Commits | Items | What |
| --- | --- | --- |
| `44feb2f` | 5 | His throws flew in real time (first contact within 0.02 s of the real arc, nothing stepped twice). A real fall looks too fast in a small world. A time factor k: the same arc and landing point, k times slower than real (`throw_slow_x10`, default 20). Numpad Plus and Numpad Divide change k by 0.25 live. Trees take the same flight (`throw_trees`). Left alone: while a thrown object touches another physics object, the game runs one step, so it moves slower there |
| `e61be0a`, `9bf122b`, `27b9bd0`, `33a35ba`, `1584aac`, `fc1784b` | 1, 4, 2 | The game hints that are false with controllers do not show ("Trigger Move", "Grip Focus Creature"). Each controller has its own card. The Creature prompt names the grip (the grip strokes and slaps, the trigger there is the land drag). The subtitles go out as one quad layer fixed in the room, 1.6 m ahead and 17 degrees below eye level (`subtitle_card`), and move only after the head was more than 50 degrees away for 1 s. The cause of the split: flat text at 1.4 to 4.1 m over hands and land at 0.4 to 1 m. A tap of the trigger sends the leashed Creature (16 of his 26 presses were shorter than the old 250 ms hold) |
| `e608fbb`, `af17a90`, `59fc73b`, `6a6137b`, `920a62c`, `5f15d11`, `4c6cbe9` | 3, 6, 8 | All presses already acted from the glow at the index tip, but the pick reached 11.4 cm around it. It is now 4 cm (`hand_pick_cm`). Near objects win: within 10 cm of an object that a press acts on, the grab from a distance is off (`far_grab_near_cm`). A scroll in the aim is a far target that the grip activates with no pull. The grip activates a scroll, so the touch look of the glow is now pale gold, not green. A forest: hold the grip in it for a tree (the mod took the forest out of the pick record during the game's wait), and grip and pull at a distance for one tree. The leash starts at the palm of the mod's hand (`GetLeashStartPoint` `0x005D8EA0`). Numpad `.` is the test key for the old pick |

## Headset test, 2026-10-03

Buck played the build of `a785a15` in the God view. His log is kept in the
session scratchpad as `buck-2026-10-03.log` (71 throws, real hand speeds up
to 4.84 m/s, scales 11.2 to 100). His list:

1. The grab from a distance works and feels natural. Remove the gold ring
   under the target: the glow is enough.
2. The menu ray does not meet the cursor. The cursor is down and to the
   right of the ray's end.
3. Limit the grab from a distance to about 2 m in front of the player.
4. Shaking the hand does not take the Creature's leash off.
5. Throws are much worse: very slow, however hard the throw. The release
   timing feels wrong: the grip opens, but the object does not leave.
6. The grab from a distance on a bulk source (a store, a forest, a fish
   farm) takes one object or one tree. Make it start the bulk take at a
   distance, or remove it there.
7. The bulk take gets stuck: it goes on after the grip opens, up to the
   limit of 20000. Wanted: hold the grip to collect (also at a distance),
   open it to stop and keep the bundle, close and open again to drop.
8. The game's Hand still turns at odd angles in some grabs. He wants to hide
   the game's Hand and use the mod's hands only, with more poses for the
   actions and later the game's own mesh (good and evil) on them. Seeing
   both hands at once breaks immersion.
9. A smaller teleport ring, and a smoothed target (hand tremor shakes it).
10. Directing the leashed Creature is hard, and the grip conflicts with the
    grab from a distance. Use the trigger when leashed: point (with the
    glow of the grab from a distance), press to make the Creature act on
    the object, hold to show the game's footprint marker at a place and
    let go to send it there.
11. The hints are STILL doubled. Fix it for good: one hint, beside the
    hand, a little above the wrist so the hand does not hide it.
12. Cut scenes: bring back a sign that the game holds the camera. Best:
    switch to the stereoscopic window during a cut scene (the view shrinks
    to the 2 m screen, the shot plays as the game made it, with camera
    moves), then grow back to the God view. A simpler choice: soft
    gradient cinema bars.
13. HL:A style controller prompts. "The game is very nearly playable",
    and good hints are a large part of the polish.

**Built on 2026-10-03.** None of it has run in the game yet: the PC was in
use, so every desktop run was refused. The math test passes 1195 of 1195 at
`6927da6`. Next: items 8, 10 and 4, 12.

| Commits | Items | What | Runs to do |
| --- | --- | --- | --- |
| `aeea377`, `3761044` | 9, 2 | The teleport ring is 11 cm, and a one euro filter steadies the aim. The menu ray and the cursor both use the point where the aim ray meets the menu at the depth the eyes see it | pol0, pol1 (`pol_both.ps1`) |
| `8fbebc3`, `05e2a1d`, `07990dd`, `9e289b2` | 1, 3, 5, 6, 7 | No ring under a far target. The grab from a distance reaches 2 m from the head (`far_grab_max_m`). A pile, a field or a fish farm in reach starts a bulk take at a distance on a closed grip. A forest is not a far target. The stuck take: the game ends a take only if the next pick record holds the source, so the mod keeps the source there, and ends a take that runs 18 frames after the button went up. The release: the next game turn runs at once (`release_at_once`), and a fast fall of the grip value opens the grip (`grip_fast_open`). Numpad `*` is the test key | gt1 to gt6 (`gt_run.ps1`) |
| `c29b71e`, `52deab3`, `10c52ae`, `b24c929`, `59748d0` | 5 | The throw (`throw_law=2`): the top speed of the swing, and a real flight. The thrown object runs the game's physics step sqrt(s) times as often, with no cap and no drag until the first contact, and the mod draws it on each frame. Scroll Lock is the test key | tp1 to tp4 (`throw_run.ps1`) |
| `c6e920e`, `4966844`, `7d76eb9` | 11, 13 | The hint card: while a controller owns the pointer, no game hint draws in an eye picture. One card beside the wrist goes out as one quad layer, with the game's hint or a controller prompt (`hint_card`, `prompts`). The cause of the double hint in his log: 8.8 percent of the hint draws used the old flat rule (a land drag, a Tug, a Miracle, the hand out of view) | hc0 to hc2 (`hints_runs.ps1`). The quad layer needs the headset |

**Built after the combined runs** (`f194c59` builds clean, the math test
passes 1297 of 1297). Run all3 at 13:30, God view, 120 s, 56 fps, no failed
hook: the game's Hand mesh was hidden on every draw and only the mod's two
hands show in the eye captures, the wrist stayed 2.5 cm from the overlay's
wrist in the Holding state, the rock sat in the palm on 52 frames, the
throw met the land 0.1 cm of the room from the real arc, and the teleport
landed 0.000 units off. Not run: the Creature commands and the shake (they
need a leashed Creature), the tint, the poses with a real controller. This
build is in the game folder.

| Commits | Items | What |
| --- | --- | --- |
| `544a70a`, `f194c59` | 8 | One pair of hands (`hand_hide_game=1`): the game's Hand mesh does not draw while a controller places it (a hook on the mesh draw `fn_00813BA0`, only for the two calls of `CHand::Draw`), and the overlay draws both hands. The flip: in the Holding state the animation moves the wrist bone, so the wrist was 14 cm off and the hand turned 79 degrees. A wrist lock keeps the wrist bone at the real wrist. The held object sits in the palm and turns with the hand. Poses: hold, point, flat, fist, relaxed, with the fingers and the thumb from the trigger and the grip. A tint from the alignment (`CHand+0x9C`). The plan for the full skin is in `hand.md` (141 vertices, 22 bones, one 256 by 256 texture). Numpad minus is the test key |
| `fb892f4` | 10, 4 | The trigger commands the leashed Creature (`creature_command=1`): a press on an object with the green halo sends packet `0x5D` (act on it), a hold of 250 ms on the land shows an arc and a ring, and the release sends packet `0x5E` (go there). No land drag while leashed. A grip on bare land no longer sends the Creature. The shake: the game's test is the scribble gesture on the pointer ring, which the mod does not feed, so the mod reads the shake from the real hand (4 strokes of 6 cm at 0.6 m/s within 0.9 s, `leash_shake_mps`) and sends packet `0x5F`. The game has no footprint marker that was found, so the mod draws its own ring |

**Combined runs, 12:15 (Buck said "test now").** Runs all1 and all2 of the
build of `6927da6`, God view, 115 s each, 60 fps, no failed hook or draw.
This build is in the game folder.

- The start dialog closed through the new menu ray: the pixel was 1298.7,
  1420.5 for the check box at 1301, 1420.
- The hint card: one card, "[Trigger] Move", then the land prompt once, and
  no game hint in an eye picture.
- The range of 2 m rejected 9003 candidates. With the range off for the
  test, a rock came to the Hand, the grip carried it, and the grip opening
  gave the release 2 frames later.
- The throw: a top speed of 2.51 m/s (the old mean gave 1.23). The rock flew
  at 251 units/s under 981 units/s^2, drawn on the arc from the first frame.
  The first contact came after 0.492 s (the real arc: 0.50 s), 0.1 cm of the
  room from the real arc. Then the game's own roll.
- Not run: the bulk take (gt1 to gt6), the square root law A and B, other
  scales, the quad layer of the hint card (headset only).

**The dependencies.** At 11:52 a recursive delete of a temporary worktree
followed a directory link and emptied `third_party`. It is restored (minhook,
OpenXR-SDK 1.1.43 with the pinned loader, bw1-decomp, the dxwrapper files).
The build now takes the folder as `BWVR_THIRD_PARTY` (`6927da6`), so a
worktree needs no link.

## Headset test, 2026-10-02, evening

Buck played the build of `91fff4c` in the God view. "The game is much
better now." His list, in his order:

1. **The teleport works well.**
2. **Grab from a distance**, as the gravity gloves of Half-Life: Alyx. Some
   quests stop the player from moving, so things out of reach cannot be
   taken. Point at an object: it glows. Close the grip and pull toward you:
   the object comes to the hand. Later, maybe: new throw physics, because the
   game's throws are floaty and were made for a mouse.
3. **One hand.** The mod's hands work well, but the game's Hand follows the
   right hand only roughly. Use the game's own Hand model and rig (it
   changes when the player is evil) for the mod's hands, with the game's
   animations for each action, and do not draw the game's Hand.
4. **Hints for the controllers.** He saw no hints and no subtitles. It is
   hard to know which button does what.
5. **The Creature.** The stroke and the slap feel natural. A hint must show
   how to start them.
6. **The left grip moves the ground up and down too.** The left stick for
   the ground height is then not needed.
7. **A button for the menu (Esc).** Maybe Y on the left controller.
8. **Bulk actions do not work.** Taking grain or wood from a store or a
   forest: the animation got stuck, no sound played, and no counter showed.
9. **Polish.** Park the tabletop mode until the God view is right. The
   advisors sound good in the room, but most other sounds do not seem to
   come from the world.

**Built in the night of 2026-10-02.** The designs are in
`docs/design-far-grab-one-hand.md`. In the game folder: the build of
`a785a15`.

| Commit | Item | What | Desktop runs |
| --- | --- | --- | --- |
| `e79c3d8` | 6, 7 | Y (left) sends Esc, and so does the left menu button. While a dialog box is up, the pointing controller points like a mouse, with a thin ray, and the trigger clicks. The left grip moves the land up and down one to one, inside the ground limits. The left stick has no job now | menu6, grab2, grab3: the menu opens on Y, the pointer is on "Options" at the aim pixel, a 20 cm rise raised the ground 20.00 cm, a 70 cm rise stopped at the limit, a still hand moved nothing |
| `4f265a8` | 9 | Sound in the room: the distance settings of each sound scale with the zoom (1 m of the room sounds the same at every scale, `audio_units_per_meter` 40), sounds at the game camera play at the ears, birds and waves get a fixed place in the room, the ears are the head | The Creature's steps at 0.6 m went from 0.2 to full level. Not heard by a person |
| `312da08` | 8, 4 | The bulk take: while a take runs, the grip is the game's button, and opening it ends the take with the bundle in the Hand. The God view pick counts store piles. The hint at the Hand is drawn at the hand's depth in each eye, 2.5 cm tall, beside the hand | bk7: wood grew from 33 to 1088 with "Amount" at the Hand. **Buck's profile has the help off** ("No ToolTips", "No Text", "No Help"), which is why he saw no hints and no subtitles. Turn it on in the game's Options, Help Options |
| `fa6dc1f` | 3 | The exact Hand (`hand_exact=1`): the game's own Hand turns with the controller's full rotation, its index tip is the real index tip and the Hand's point, and its size is right (the old Hand was 12% too big). The overlay draws only the other hand, or both with `overlay=1`. Not built: the game's mesh for the left hand | ex1 to ex4: the point 0.0 cm from the overlay's index tip at five turns, the wrist 2.5 cm off. Only the Normal and Camera states were seen |
| `9e3cddf` to `a785a15` | 2 | The grab from a distance (`far_grab=1`): point at an object out of reach for a gold halo and a ring (red outside the influence), close the grip to lock (a thin line), pull toward the head, and the game takes it with its own pick up. The object glides to the Hand over 0.3 to 0.5 s. The throw law (`throw_law=1`): a throw sends sqrt(s) times the real speed, so it lands where a real throw lands, times the scale | far13, far15: a tree came to the Hand, Tug then Holding, and the grip carried it. far14: an open grip before the pull ends the lock. far19: 1.08 m/s sent 10.8 units/s, not the cap of 124 |

**Combined build.** `a785a15` builds clean, and the math test passes 962
of 962. Run smoke1 (God view, all of the above together, 150 s): the 24 bit
Z buffer, the start dialog clicked by the menu ray, the story stand aside
and return, a teleport 0.000 units off, the grab from a distance finding
targets, no failed hook or draw, 65 fps on the desktop. This build is in the
game folder.

**Review of `91fff4c..a785a15`.** No fault confirmed, no change made. Three
suspects to watch in the headset:

1. The grab from a distance keeps only the object pointer while the grip
   holds a lock. If the object dies during a long lock (a villager killed or
   eaten), the pull can put a dead object in the game's pick record, which
   can crash the game. Watch: hold a far lock on a villager for several
   seconds near the Creature or a fight, then pull.
2. The sound keeps the QMixer session handle for good. If the game opens a
   new sound session (the sound options, perhaps a load), the rolloff calls
   go to a dead handle. Watch: change the sound options and load a game,
   then listen.
3. A throw at once after a far fetch may start from the glide point. A look
   fault only.

Also check: one Y press opens the menu and it stays open. Open the menu
while carrying an object, close it, and the object is still in the Hand.
After a bulk take, close and open the grip again: the bundle drops.

## Headset test, 2026-10-02

Buck played in play mode 0 (immersive) with the direct hand, `depth24=1`,
`overlay=1` and `hand_glow=1`. The overlay hands draw. The direct hand is
better than on 2026-09-30, but not playable yet. His list:

1. **The near plane is too far.** He sees his hands and the Hand only with
   his arms out. The near plane of play mode 0 is 47 cm of the room.
   `depth24=1` did not work: the backend still asked for a 16 bit buffer.
   Work: make depth24 work, then a near plane of about 5 cm in play mode 0.
2. **The zoom must scale the world, not move the head.** The head keeps its
   place in the island and its height over the ground, with the ground at
   the waist (0.8 to 1 m below the head). This is the God view (play mode
   4), which he has not tried yet.
3. **A teleport with a visible arc.** Hold to show the path and the target,
   release to go.
4. **The grip holds.** Close the grip to take, open it to let go or throw.
   Today a second press lets go, so a throw does not work well.
5. **The Creature without camera moves.** The camera jumps around when the
   Hand touches the Creature. He wants to stroke and slap it where it
   stands, with the Creature panel fixed in the room.

Work started on items 1 and 4, and a design for items 3 and 5.

Later the same day Buck tried the God view. It is much better, and it
replaces the immersive mode: there is no need for both. Play mode 0 stays
only as the view of a story and of the stereoscopic window. One fault:

6. **The Hand cannot get down to the ground** in the God view, so a pick up
   and a touch are difficult. The glow also stays above the land. This is
   now a part of the grip work.

The design for the Creature by touch, the teleport arc and the God view as
the only main mode is in `docs/design-creature-teleport.md`.

**Built the same day.** All in the game folder (build of `91fff4c`), not
yet tried in a headset:

| Commit | What | Desktop runs (Buck allowed runs at once, 2026-10-02) |
| --- | --- | --- |
| `ca2c8a9` | The 24 bit Z buffer works (the old hook read the wrong field of the format), and the near plane is 5 cm of the room (10 cm with 16 bit), in `near_plane.cpp` | gr1: "The Z buffer is D24X8, 24 bit", the near plane at most 5 cm. `depth24` stays 0 by default until the headset confirms it |
| `025c35e` | The grip holds: close to take, open to let go or throw, the throw speed latched at the opening. The Hand's point now stops 5 mm over the land (it was held 9.5 cm up) | gr1c: "the point is 0.50 cm of the room over the land". The pick, carry and throw did not run: no object was in reach of the fake hand |
| `51319f9` | The Creature by touch: no camera move, the Hand stays at the controller, a body capsule, slaps in real m/s (`creature_slap_mps` 1.5), the Creature panel near the Creature with `ui_panel=1` | Not run: a run needs a game with a Creature |
| `cb38679` | The God view is the main mode: "Immersive" is out of the launcher, `play_mode=0` gives the God view, a mouse land drag pans it, the wheel zooms, the tilt keys move the ground, the camera switches and the tutorial events, a load builds the view again | gm1: all of these work. The pan kept the land within 0.000 cm of the mouse ray. Not seen: the drag event 0x308, from the pan or from the game's own drag, so a tutorial drag step needs a check |
| `91fff4c` | The teleport: hold A for an arc and a ring (cyan valid, red not), let go to go there in one cut, B cancels. The God view, the table, play mode 0, and the mouse double click in the God view. `teleport=0` gives the old A jump | tp1, God view: two moves landed 0.000 units off the target, the land 0.600 m under the head before and after, the scale and the heading kept, event 0x305 sent. An aim at the sea was red, and B cancelled with no gesture. The tube and the ring show in both eyes. Not run: play mode 0, the table and the mouse double click |

## Test in the headset next

Everything below was built on 2026-09-29 and 2026-09-30 and tested only on
the desktop, with a fake head and a fake controller. In this order:

1. **Mouse play, nothing broken.** Play as before with the controllers on
   the desk. The pointer must stay with the mouse. The land drag, the
   subtitles and the menus must be as before. `controllers=0` in bwvr.ini
   turns all of the controller code off, if something is wrong.
2. **The direct hand.** Pick up a controller: the Hand goes to it. Grip
   picks up, open to drop, swing and open to throw. Trigger drags the land.
   A jumps. The stick zooms and turns. Lean with the hand still: nothing
   may move.
3. **Grab the world.** The left grip pulls the world. Both grips turn and
   zoom. The world stops dead at the release.
4. **Miracles.** At a Worship Site with a miracle ready, hold B and draw
   the gesture in the air.
5. **The Creature.** Stroke and slap it with the hand.
6. **The hints** say "Trigger" and "Grip" while a controller points.
7. **Sound.** The ears follow the head, and the advisors speak from where
   they float. With test keys on, Ctrl Shift F12 switches the ears back to
   the game camera, for an A and B.
8. **Recenter** from the Quest menu.
9. **The tabletop** (play mode 3, "Tabletop (test)"): the size and the
   height of the table, the hands on the table, the frame rate. For the
   passthrough mask set `table_key_color=FF00FF` and pick magenta in
   Virtual Desktop's passthrough settings.

10. **The Lionhead intro in VR.** Tick "Intro in stereo" in the
    launcher's View group and press Play: the particle logo forms around
    you in stereo. It is off by default until it is seen in the headset, because
    it runs before anything else. The menu button skips it.
11. **The interface in the room.** Set `ui_panel=1` and `test_keys=1` in
    bwvr.ini. In play, the Esc menu and the subtitles must be sharp, fixed
    in the room, in front of the world, and not in the eye pictures. The
    menu panel is ahead and a little low, the help panel lower. F11 moves
    both in front of you. Ctrl Shift F11 puts the interface back in the eye
    pictures, for an A and B. Known limit until U3: the pointer does not
    follow a ray to the menu panel. See `docs/ui-layer.md`, section 12.

11. **The menus on a panel in the room.** Set `ui_panel=1` in bwvr.ini:
    menus hang 140 cm ahead and help text lower down, sharp and fixed in
    the room. With test keys on, Ctrl Shift F11 turns it on and off. The
    pointer on the panel is not built yet, so a click may land beside
    what you aim at. Leave it at 0 for normal play.
12. **The God view** (play mode 4, "God view" in the launcher). The land
    is at your waist, 60 cm below your head at the last recenter. Right
    stick forward and back: one zoom step a push. Left stick forward and
    back: the ground up and down 5 cm a push. Left grip: pull the land.
    Check the ground height, the zoom step, the Hand at the life size scale
    near your eyes, picking up and throwing when zoomed out, and the frame
    rate. See `docs/god-view.md`, section 13.
13. **The throw on the table and in the God view.** Pick up a villager, press
    the grip, swing, and open. It must fly on the first release, also when
    the swing ends past the edge of your influence. A still hand must set it
    down gently. A still hand outside your influence keeps the villager, as
    the mouse does. See `docs/tabletop.md`, section 10.11.
14. **The glow at the Hand's point.** With the direct hand a small disc
    marks the point of the Hand that picks up and clicks. It is pale blue
    over the land, gold when the grip can pick something up, and green when a
    press acts on something that the Hand cannot take. Check that it is
    easy to see on the land and on the Hand, that both eyes put it at the
    same depth, and that it does not hide what you aim at. `hand_glow=0`
    turns it off. See `docs/hand.md`, "The glow at the Hand's point".

15. **The 24 bit depth buffer.** Add `depth24=1` to bwvr.ini. In the God
    view zoomed out and on the table, distant houses must not flicker into
    the land. Nothing else may change: check the sun glare, the sea and the
    interface. `depth24=0` (the default) is the old 16 bit buffer. The log
    line `depth24      :` says what the game asked for and what the
    adapter gave. See `docs/backend.md`, "The depth buffer".

16. **The overlay hand.** Add `overlay=1` to bwvr.ini. The mod draws a
    plain skin colored hand at each controller, the length of your real
    hand. The index finger closes with the trigger, and the other three
    fingers close with the grip. Check that it sits on your real hand at the
    same depth in both eyes, that land in front hides it, and the frame
    rate. The game's own Hand still draws beside it. `overlay=0` (the
    default) turns it off. See `docs/hand.md`, "Overlay renderer".

The values to tune are ini keys: `hand_real_cm` (the length of your hand,
19 by default), `hand_scale_pct`, `throw_gain_pct`,
`creature_speed_gain_pct`, the `grab_` keys, `gesture_size_cm`, the
`table_` keys and the `god_` keys. Each doc names its defaults and why.

Known faults: the game's Hand turns with the controller and sits on the
real hand since `fa6dc1f` (`hand_exact`, `docs/hand.md` "The exact Hand"),
but the Tug, Holding, Creature, Grain and PlayAnim states were not seen on
the desktop. In the immersive mode (play mode 0) the game's near plane is 3.5 units, 47 cm
of the room, so it likely cuts away a real hand held nearer to the eyes
than that. The God view lowers it to 5 cm (`docs/god-view.md`, section 8).

## Track 1: motion controllers

The goal is the Hand as your own hand: reach into the island with a tracked
controller, pick up a villager, throw a rock, stroke or slap the Creature, and
draw a miracle in the air. The mouse keeps working all the time.

| Step | What it gives | State |
| --- | --- | --- |
| M1 | OpenXR input: an action set, both hand poses, every button, haptics. A fake controller for desktop tests. Nothing in the game changes yet. | Built. Math test 37 of 37. Desktop run: a fake hand at the head is 0.00 units from the eye and at the middle pixel. Not tried with real controllers yet. |
| M2 | The controller as a pointer: its ray replaces the mouse ray. Trigger and grip act as the mouse buttons. The thumbstick turns in steps and zooms. | Built. Desktop runs with the fake controller: presses, releases, the double click, the A button jump, the snap turn and the wheel reach the game. Not tested: the land drag with a controller, and real controllers. |
| M3 | Direct hand: the Hand sits at the controller in 3D, not on the land under a ray. It picks the nearest object it can hold. | Built, see `docs/hand.md`, "M3 as built". Desktop runs: the target, `hand+0x78` and `0x00C5E680` agree to 0.0000 units in the Normal and Holding states, near, far and on the land. The grip picks up the object the 3D pick chose. Not tried with real controllers yet. |
| M4 | Throwing with the speed of your real hand. Haptics from the game's own Immersion effects. | Built, see `docs/hand.md`, "M4 as built". The throw goes in the direction of the Hand at the speed of the real hand times `throw_gain_pct` (150), cut at the game's cap of 124. Desktop runs: a step of 12 cm sent 20.3 units/s, no motion sent zero. Each game effect gives a pulse on the pointing hand. Not tried with real controllers. |
| M5 | The Creature: stroke and slap by the motion of your hand. | Built, not run. The hooks install and the math is tested. No desktop run reached the Creature state. Needs a headset or a run that presses on the Creature. See `docs/hand.md`, "M5 as built". |
| M6 | Miracles: draw the gesture in the air. The path goes to the game's 2D gesture reader on a plane. | Built, see `docs/hand.md`, "M6 as built". Hold B and draw. The grip goes on a plane that faced the head at the press, 30 cm of hand to a third of the view, and into the game's own sampler, with sharp corners kept. Desktop runs: 13 traced templates of `Gestures.jty` (13 of the 23 gesture types) fit their own type with the game's own test, after two fixes, and steps of the head during a stroke changed nothing. The game asked for no gesture in the pen, so no miracle was cast. Not tried with real controllers. |
| M7 | Move by grabbing the world: hold the grip and pull the land. | Built: stages G0 to G3 and a part of G4, see `docs/grab-world.md`, section 9. The other hand's grip pulls the world, and both grips turn it about up and zoom. The camera is solved from the press, and it stops at the release. Desktop runs: a 10 cm step moved the camera 0.7499 units one to one and 2.35 units at the glued gain of 3.14, with an anchor error of 0.0000. A head lean and a head turn with still hands moved nothing. Not tried with real controllers. G5 (the tabletop) is not started. |
| M8 | Controller hints: with controllers in use, the game's mouse hints (the icons beside the Hand, drawn by `CameraHelp::DrawKeyOrMouse` `0x00447EA0`) show the controller button instead. Best: a model of the player's own controller with the button lit, as modern VR games do. At least: a hint image per controller profile (Touch first, then Index, Vive, Windows Mixed Reality). Asked for by Buck, 2026-09-29. | Designed in `docs/controller-hints.md`, steps H0 to H5. H0, H1 and H2 are built (section 8 there): the census log, key caps with the controller word, and the help text words. Desktop runs with the fake controller and a test key show one cap in each eye. Not run with a real hint or a headset. H3 to H5 (own art, a controller picture, a 3D model) are next |
| M9 | A glow at the Hand's point: a small disc at the point that the Hand picks up and clicks from, in the color of what a press does there. Asked for by Buck after the first headset test, 2026-09-30. | Built, see `docs/hand.md`, "The glow at the Hand's point". A disc in the color of the look and a core that adds light, drawn on the game's device after the sun glare in each eye pass, with every state put back. Gold over something to take, green over something to touch, pale blue over the land. Math test 498 of 498 (29 new). Desktop runs glow1 to glow3: both eyes put it on the tip of the index finger, 67.4 px apart at 1 m and 149.7 px at 45 cm, as the math gives, and it turned gold over a villager. Not seen: the look while holding, and a disc hidden by land in front. Not tried in a headset. |

## Track 2: tabletop mode

The island as a model on a table in front of you. You walk around it and lean
over it. The advisors float in the real room, and the menus hang in the room
at fixed places.

| Step | What it gives | State |
| --- | --- | --- |
| T1 | Research: the world scale, where the camera goes, how to cut the world at the edge of the table, and what the engine culls. | Done, `docs/tabletop.md` |
| T2 | A fixed camera over a point of the island at a large world scale, with head tracking. | Built, see `docs/tabletop.md`, section 10. Play mode 3, "Tabletop (test)" in the launcher. Ctrl F11 turns the table off and on. Land 1 fits a 1.5 m table at 1973 units per meter. Desktop runs: the whole island on a round sea with no fog, both eyes 131.8 units apart. Both scene walks take 9.1 ms at 3093 draw calls, against 4.3 ms without the table, and 11.2 ms leaned in. No object list was full. Second build (`docs/tabletop.md`, 10.6 to 10.10): the controller's Hand on the table with no reach curve, 10 cm long (`table_hand_cm`). Desktop runs: the pick up and the set down work, and a lean moves nothing. The throw sends its speed at the table scale, cut at 124. A throw that ends outside the player's influence goes from the last point of the swing inside it (`docs/tabletop.md`, 10.11). The ears go down to 400 units over the land under the head. The spirits float beside the table at the table scale, not seen yet. Not built: the lock of the mouse's Hand when the head leans. Not tried in a headset. |
| T3 | The edge of the table: nothing drawn past a radius. | Built without the ring. Land blocks past the edge are cut, the plain sea is a disc of the table's radius, and clouds past the edge are not drawn. Desktop run: a 1 m table cut 6 of 100 blocks in each pass. The ring needs a draw of the mod's own, see `docs/tabletop.md`, 10.5. |
| T4 | Moving the table: grab and turn it, and zoom. | Built for the grips, see `docs/tabletop.md`, 10.7. The other hand's grip slides the land on the table, and both grips turn it and zoom it between 40 units per meter and the fit. The table top stays in the room. The table is solved from the press, so a head that moves with still hands moves nothing and nothing moves after the release. Not built: the moves by the mouse, the stick and the jump, and the game camera over the table. |
| T5 | The table in your real room: everything past the table drawn in one flat key color, for the Virtual Desktop passthrough mask. | The key color is built: `table_key_color=FF00FF`. The sky still draws behind the translucent sea, and a clear of the key color covers every pixel outside the sea disc. Desktop runs: magenta past the edge, the sea in its own color. Not built: the depth key, the launcher text for the mask, the table on a real desk. Not tried with the mask. |

**Mixed reality, found 2026-09-29.** Virtual Desktop gives an app no control
of passthrough, but it has a passthrough mask that the player turns on: every
pixel in a chosen color shows the headset cameras instead of the game. It
came in Virtual Desktop 1.29 and needs a headset with color cameras, such as
the Quest 3. So the tabletop mode can draw the sky, the sea past the table
and the room around the table in one flat key color, and with the mask on,
the island stands on the player's real desk. The mod cannot turn the mask on,
so the launcher has to say how.

## Track 7: the God view

Asked for by Buck, 2026-09-30, after the first headset test of the direct
hand. The land is at the player's waist, as in "How to God", so the real
hands reach it. The world scales about the player, and the Hand is always
the real hand. It is the camera of the tabletop mode with a different room.
Designed and built in `docs/god-view.md`.

| Step | What it gives | State |
| --- | --- | --- |
| G1 | Play mode 4: the ground at the waist, the zoom about the ground under the head from 7.5 to 4000 units per meter, the ground height by the other hand's stick, the snap turn about the head, the grab of the table, the Hand at the real size, the fog and the detail that follow the scale, the near plane at 5 cm of the room, the game camera over the player's place, and a stand aside for stories. | Built. Math test 460 of 460 (104 new). Desktop runs gv1 to gv7: the zoom, the ground steps, the limits, the turn, a one hand pan with an anchor error of 0.0000 units, and the story stand aside at the Creature select. Not tried in a headset. Not tested: a pick up and a throw in the God view |
| G2 | What the headset test asks for: the default height, the zoom step, a smooth feel. Possible: a jump that moves the God view, and a follow of the story's end place. | Not started |

## Track 3: the interface in the room

The menus, the help text and the Creature panel on a flat panel that stays in
the room, drawn as its own OpenXR layer. This is sharper than text in the 3D
picture, and it is what the tabletop mode needs. Designed in
`docs/ui-layer.md`.

| Step | What it gives | State |
| --- | --- | --- |
| U1 | Desktop only: the interface also drawn to its own surface in the first eye pass, saved as a BMP with alpha. Nothing changes in the headset. | Built, `ui_panel=2`. Two desktop runs with menus: the composite check passes, and the switch leaves the eye picture unchanged. Not yet seen: the subtitles, the help text and the Creature panel on the panel. See `ui-layer.md`, section 12. |
| U2 | The surface shared to OpenXR as quad layers in the room: a menu panel ahead and a help panel lower down. | Built, `ui_panel=1`, off by default. Not run yet, not even on the desktop. Next: a headless run with `ui_panel=1`, then the headset. Ctrl Shift F11 (test keys) is the A/B. |
| U3 | The pointer on the panel, for the mouse and the controllers, while a menu is open. | Not started |
| U4 | Screens with no scene (menus, loading, videos) on a quad in the room, not stuck to the last head pose. | Not started |

## Track 5: sound in the room

Asked for by Buck, 2026-09-29. Today the advisors speak at the same volume
in both ears, wherever they float. The goal is spatial stereo: every sound
comes from where its source is, relative to the player's real head, so an
advisor on the left is heard on the left.

| Step | What it gives | State |
| --- | --- | --- |
| A1 | Research: the game's sound path (`LHaudiodllR.dll`, `QMixer.dll`), where it places the listener (the game camera, not the head), which sounds are positional, and how the advisors' voices are played. | Done, `docs/audio.md` |
| A2 | The listener follows the head: position and orientation from the head pose, every frame. | Built, checked with the fake head (T2). The ear check needs Buck. |
| A3 | The advisors' voices come from where the advisors float in the room. | Built with a polar angle per frame. Not run on a real advisor line. |
| A4 | Better stereo where the game plays a sound flat, for example a pan and a level from the direction of the source. | Built for the God view (`docs/audio.md` 11.6) |

## Track 4: smaller items

- Done: a recenter from the headset's own menu reaches the mod
  (`XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING`). The mod recenters at
  the first frame whose poses are in the new space. Not tested in a headset.
- The frame rate in busy scenes.
- The story camera scenes that still frame badly, when Buck names them.
- A test on a PC that never had the DirectX 9 runtime installed.

## Track 6: the Lionhead intro in VR

Asked for by Buck, 2026-09-29, and last in the order. The intro where black
and white particles form the Lionhead logo is a 3D scene, and the player can
push the particles around with the mouse. Today the mod shows it flat. In VR
the camera would hold still at the intro's final angle, the logo would form
around and in front of the player, and a controller could push the particles
as the mouse does.

| Step | What it gives | State |
| --- | --- | --- |
| L1 | Research: how the intro draws (the engine's own 3D path or its own code), why the mod shows it flat today, and how the mouse moves the particles. | Done, `docs/intro.md` |
| L2 | The intro in stereo, with head tracking and a still camera at the final angle. | Built, `intro_vr=1` (off by default). Desktop runs show both eyes and the menu after. Headset test open, `docs/intro.md` section 10 |
| L3 | Push the particles with a controller. | L3a built with L2: the aim moves the pointer, the trigger cannot end the intro, the menu button skips it. L3b (3D push) not started |

## Status log

- 2026-09-29: roadmap written. Work on Track 1 starts with research: one
  agent on OpenXR input and the game's mouse path (`docs/controllers.md`),
  one on the Hand (`docs/hand.md`).
- 2026-09-29: the headset menu recenter reaches the mod.
- 2026-09-29: `docs/controllers.md` done. The pointer comes only from window
  messages, every button goes through `LHMouse::SetButtons` `0x007E4B80`,
  and the double click is the Windows one, so the mod makes its own. The pump
  runs from `AnyWindowMessages` `0x007DB910`. M1 is being built.
- 2026-09-29: `docs/hand.md` done. Every Hand state builds the Hand from a
  screen ray and a depth, then commits the pose in `fn_0046E160`. The pick is
  a render test, one frame late, in `GInterface+0x3C8`. The throw velocity is
  `CHand+0x48C8` in world units per second, capped at 124. A slap needs a
  horizontal speed above 75 times the Creature's size. Two symbol names in
  bw1-decomp were swapped, and `tools/disasm.py` now corrects them.
- 2026-09-29: M1 built. The action set covers Touch, simple, Index, Vive and
  Windows Mixed Reality controllers. `test/controller_math` checks the room
  transform against the real `head_pose.cpp`. A desktop run with the fake
  controller put the hand 0.00 units from the eye, at the middle pixel.
- 2026-09-29: M2 built. With no controller button held, the real mouse goes
  to the game exactly as before, every event. Desktop runs showed the
  presses, the double click, the snap turn and the wheel. The land drag did
  not start in any `-NEWGAME` run, with the controller or otherwise, so it is
  not tested.
- 2026-09-29: `docs/tabletop.md` done. A land is 1400 to 3300 units across,
  so a 1.5 m table is about 2000 units per meter. The camera is the head
  camera formula with a table origin. Fog, LOD and culling each need one
  value changed. The key color is magenta, from a clear in place of the sky.
  The biggest risk is the frame cost, 7 to 18 ms for both eyes.
- 2026-09-29: desktop runs now wait until the PC has had no input for 10
  minutes, after an early run took the screen while Buck was using it.
- 2026-09-29: M3 built. The Hand goes to the grip point through three
  hooks (the ray, the depth, the pose builder) and a land test. A reach curve
  takes a 65 cm arm to 150 units. The pick takes the nearest holdable object
  from the first eye pass. The launcher has a Controllers group with the
  choice of a direct hand or a pointer.
- 2026-09-29: `docs/audio.md` done. All sound goes through `LHaudiodllR.dll`
  into QMixer, which positions it in software on a 20 ms timer thread. The
  game sets the listener once per game turn in `GAudio::ProcessAudioGameTurn`
  `0x00427080`, from the game camera. The advisors play flat: `HelpDude::Say`
  sets the 3D flag to 0. A2 hooks `LHListenerUpdate` and feeds it the head,
  every frame.
- 2026-09-29: `docs/ui-layer.md` done. The screen interface draws near the
  end of `LH3DRender::FinishFrame`, once per eye pass. The mod can switch
  the Direct3D 9 render target around those draws in the first pass and
  skip them in the second. dxwrapper caches the target, the viewport and the
  states and never checks them, so the switch must restore all of it.
- 2026-09-29: `docs/controller-hints.md` done. Every mouse hint is a cell of
  one 256 by 256 atlas, `mousehelp.raw`, and `fn_005C78F0` turns each action
  into its button. So the mod can point the atlas at its own art, and show
  words ("Trigger", "Grip") on a key cap first. The help text says "Action
  Button" and never "left mouse", so only about 20 lines need new words.
- 2026-09-30: M3 built. The Hand sits at the controller in the Normal and
  Holding states, with a Go-Go reach: one to one up to 35 cm from the head,
  then a gain, so a stretched arm (65 cm) reaches 150 units. The grip picks
  up the nearest holdable object. The launcher has a "Controllers" group
  with "Hand": Direct hand or Pointer.
- 2026-09-30: `docs/intro.md` done. The particle logo is real time:
  `RunAlexFX` `0x005F89F0` draws 4096 particles as 3D sprites with its own
  StartFrame, camera, draw and Flip, and never calls `Process3dEngine`, so
  the mod shows it flat. L2 runs it twice a frame with the time frozen on the
  second call, and puts each eye's camera in from the `UpdateCamera` hook.
  The mouse pulls the particles within a tenth of the width of the pointer,
  so a controller on the pointer pushes them almost for free.
- 2026-09-30: A2 and A3 built. The listener follows the rendered head every
  frame, through a hook on `LHListenerUpdate`. The advisors' channel gets a
  polar angle from where each spirit floats, relative to the head. With test
  keys on, Ctrl Shift F12 switches the ears between the head and the game
  camera. A desktop run with a 90 degree fake head turn gave QMixer the head's
  forward exactly. Not tested: an advisor line, and the ear check.
- 2026-09-30: `docs/grab-world.md` done. The game's land drag is not a 1:1
  grab: 10 cm of hand moves the land 8 to 100 units. The design moves the
  game camera through its six Zoomers, as the snap turn does, absolute from
  the press. The off hand grip grabs, both grips turn and zoom, and nothing
  glides after the release.
- 2026-09-30: an Opus review of M1 to M3 found eight faults that a still
  fake controller could not show, and all eight are fixed. The worst: the
  reach was measured from the eye, so a head sway moved the Hand and a held
  object, and a camera turn could hand the pointer to a controller lying on
  the desk. The reach now starts at a fixed point in the room (the recenter
  head, 20 cm lower), and the math test proves that a 1 or 5 cm head move
  with a still hand moves nothing (97 checks).
- 2026-09-30: M4 and M5 built. The throw takes its direction from the Hand and its speed from the real hand, because the Go-Go reach made every Hand speed hit the game's cap of 124. Effect indices 4, 5, 6, 44 and 46 of the game's haptics were seen in play. The Creature state was not reached in a desktop run.
- 2026-09-30: M7 built, stages G0 to G3. The grab writes the game camera
  through its six Zoomers, as the snap turn does, and only the grip points
  in the room go into the solve. So a head that moves with still hands moves
  nothing, and the camera stops dead at the release. The trigger, the stick
  and the wheel are held back while a grab holds. The G0 probe found that the
  game's own land drag no longer moves the land with a head lean, since the
  review fix of M3.
- 2026-09-30: M6 built. The game has no gesture button: every pointer move
  goes into a ring of 80 samples, the game matches it every frame against the
  gestures that fit the moment, and a miracle is a chain of two or three
  gestures. The mod feeds the ring while B is held, from a plane that faced
  the head at the press. Two fixes came from the desktop runs: sharp corners
  are kept, and a stroke over the sky gets the Hand's point as its land. The
  game asked for no gesture in the first minutes of Land 1, so the cast
  itself needs a later save or the headset.
- 2026-09-30: T2, T3 without the ring, and the key color of T5 built. The
  table takes the main camera call, and the game keeps its own camera
  outside `GGame::ProcessGraphicsEngine`. The first run found that
  `frame_timers.cpp` already hooks that function, so the render bracket ends
  there. Land 1 on the table costs 4.7 ms more for both eyes than the life
  size view. The Z buffer is 16 bit. The key color first made the sea
  purple, because the sea is not opaque, so the sky now stays behind the
  sea and the key covers only the screen outside the sea disc.
- 2026-09-30: the controllers on the table, and T4 by the grips. The hand
  goes on the table with no reach curve, so a real hand on the table top
  touches the land under it, and the Hand is 10 cm long. The render pick
  offers only the objects whose circle on the screen holds the pick pixel,
  which on the table is almost none. A hook on the screen test now offers
  the objects near the Hand, and the influence test gets the land under the
  Hand, so the grip picks up and sets down on the table. The throw goes at
  the table scale and is cut at 124, but the game keeps the object in the
  Hand, and the cause is open. The grips move the land on the table, solved
  from the press. The ears go down to 400 units over the land under the
  head, and the spirits float beside the table at the table scale.
- 2026-09-30, the first headset test of the direct hand: unplayable. The
  Hand was the size of a couch and moved more than the real hand, because
  of the Go-Go reach and a size that grew with it. Buck's rule: the Hand
  is the real hand, one to one in place and size, and reach comes from the
  world, like "How to God". The reach gain is now 0 by default, and the
  Hand's size comes from `hand_real_cm` (19) at the world scale and never
  grows. Next: the God view, with the ground at waist height.
- 2026-09-30: the God view built (play mode 4, `docs/god-view.md`). The
  table camera with the land 60 cm under the recenter pose and no edge. A
  zoom step of 1.5 times turns about the land under the head, from 7.5 to
  4000 units per meter, and the ground goes up and down 5 cm a push. The
  Hand is 19 cm at every scale. The distances of the fog, the detail and
  the reach follow the scale, and the near plane goes down to 5 cm of the
  room so a real hand near the eyes stays in view. The game camera moves
  over the player's place only when the player moves the view, and the God
  view stands aside while a story holds the camera. Desktop run gv7: the
  turn, the pan and the steps work at the Creature select. The desktop test
  script now writes its keys under `[bwvr]`, because the launcher adds a
  `[game_before]` section at the end of bwvr.ini.
- 2026-09-30: the cause of the throw that stayed in the Hand on the table.
  The game lets go only when the land point of the release is inside the
  player's influence (`GInterface+0x48`). On the table a step of 2 cm carried
  the Hand 39.5 units, past the edge at the pen, so the release action and
  the drop refused the throw. A throw on the table or in the God view that
  ends outside the influence now goes from the last point of the swing
  inside it, in the last 0.5 s. Set downs and miracles keep the game's rule.
  Math test 469 of 469. No desktop run yet (`docs/tabletop.md`, 10.11).
- 2026-09-30: a glow at the Hand's point (M9, `docs/hand.md`, "The glow at
  the Hand's point"). Asked for by Buck after the first headset test. A
  small disc sits at the point that the render pick looks through and that
  the pick of holdable objects measures from. It is gold when the game's
  hand mode is 9 (the grip picks up), green when a press acts on something
  that the Hand cannot take, and pale over the land. It is the first
  geometry that the mod draws in the 3D scene: after the sun glare in
  `FinishFrame`, on the game's Direct3D 7 device, with every state that it
  changes read first and put back after. `hand_glow` (1) and a launcher
  row. Math test 498 of 498 (29 new). Desktop runs: the disc sits on the
  tip of the index finger in both eyes, with the disparity the math gives,
  and turns gold over a villager. One layer read as a gray patch on the
  hills, so a core that adds light was added.
