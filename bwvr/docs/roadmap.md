# Roadmap

This file is the list of the open work and of the plans. It does not hold
the history: when an item is done, remove it, and write what the mod does
now in the doc of the area. The git history is the history.

The doc of each area in `bwvr/docs` ends with the open questions and the
known limits of that area. This file names the work, and the doc has the
detail.

## Where it stands

The mod is at version 1.1.0, an alpha for a small group of testers.

- **The game in VR.** The game draws the scene one time for each eye, the
  head moves the camera, and the picture goes to the headset through
  OpenXR. The main mode is the God view: the land is at the height of the
  waist, and the player zooms, turns, grabs the land and teleports.
- **The hands are the Hand.** With motion controllers the player takes a
  thing with the grip, brings a far thing to the hand, throws, strokes and
  slaps the Creature, commands and leashes it, casts a miracle, and uses
  the village and the Temple. Hint cards say what each button does.
- **The mouse and the keyboard** play as in the plain game.
- **A story** of the game shows in a window in the room, and the sound comes
  from where its source is.
- **The build.** `dev.ps1` builds the mod and puts it in the game folder of
  a contributor. GitHub builds each push and each pull request and keeps an
  installer with the run. A release for the testers is made by hand.

Only one setup is tested by a person: a Quest 3 through Virtual Desktop.
The known faults for a player are in `bwvr/installer/README.txt`.

## To do

The most urgent part is first. An item that says "not run" or "not shown"
is built, and no test has proven it.

### Faults in play

- **B2. A moving object is hard to take.** The pick reaches farther for an
  object that moves (`catch_moving`, `grab.md`): check the defaults (100 ms,
  8 cm) in a headset. The grab from a distance locks and fetches a rock
  that falls and a rock that flies in a desktop run. Still open:
  - A tree that is let go comes to rest in about 1 s now
    (`throw_tree_settle`, `throw.md`, "The landing of a tree"). Not shown:
    a far target that hides and shows by itself (not seen in a desktop
    run), a tree thrown fast across the land, and the look in a headset.
  - A thrown object is no candidate for 9 to 16 frames after the release,
    while the mod draws it on its arc.
- **B4. The card does not know a body in the way of a planted tree.** The
  card says "plant" by the tests of the game that it can read (the height,
  the lean, dry land, fire, `grab.md`, "A tree in the Hand"). The game also
  lets a tree fall when another body is where the tree would stand: the card
  does not see that. No run had a tree that leans more than 0.2 rad.
- **B1b. The roots pile of a far grab.** `far_roots_at_tree=1` puts the pile
  at the roots, as for a tree that the Hand takes. Only a headset shows
  whether that is what the player means by the "pit" at the roots. Not
  run: a tree that comes from a distance and is then planted again. A
  tree that is thrown, or that falls from more than `release_fall_cm`,
  is not planted by the game: it takes the path of a dead tree.
- **B6. A soft lock in the quest of the sailors.** The sailors ask for
  wood. After a tree was dropped on their boat, the boat took no wood, the
  subtitle "Trees won't work, they need prepared wood" stayed, and the game
  froze when a different quest started. Not known: a fault of the mod (for
  example the object that a release acts on, or the place that the scripts
  see) or a fault of the plain game. Try the same steps in the plain game.
  The logs of a session with this fault are on the owner's PC, in
  `local\logs\2026-10-07\`.
- **B7. The zoom seems to move the view forward and back.** A zoom keeps
  the land point that the player looks at in its place, so only the size of
  that land changes (`god_zoom_about=1`, `god-view.md`, section 2). Desktop
  runs show it. Check in a headset whether a change of the zoom still looks
  like a move forward or back. The test key (Ctrl, Shift and Delete) gives
  the zoom about the land under the head (0) and about the Hand (2) for a
  comparison. Then choose the default and the limit of the look
  (`god_zoom_about_far_cm`). The line of the look is the forward direction
  of the head, and the eyes can look lower than that.
- **B8. The size of an advisor at a place of the world: check it in the
  headset.** A script can send an advisor to a place of the world, for
  example to the scroll of a Creature lesson. The game makes him 4 times as
  large there, which is 73 cm in the room. With `spirit_world_size=1` he has
  the angle of size of his usual place (`tabletop.md`, "A spirit at a place
  of the world"). A desktop run shows the two sizes with a test that sends
  the advisor to the place. Not run: a script that sends him there by itself,
  and the table. Check in a headset at a scroll of a Creature lesson: the
  size, and where the advisor stands at the scroll (the distance to the side
  stays in units of the world). The test key (Shift and Home) gives the size
  of the game for a comparison.
- **B9. A crash at each exit of the game.** Windows records two crashes at
  the time the game closes: the faulting modules are
  `VirtualDesktop.LibOVRRT32_1.dll` (exception `0xc0000409`) and
  `nvd3dum.dll_unloaded` (`0xc0000005`). The player sees nothing, because
  the game is closing. Find the order in which the mod, the OpenXR session
  and the Direct3D devices shut down, and whether the crash can cut the
  last lines of the log or a write of the profile.

B1b, B2 and B4 are one package: a thing that is held, thrown, or that
moves while the Hand takes it.

### Changes to the controls

- **P1. The choice of a far target: test it in the headset.** The choice by
  the body, the size term and the bias for living things (`far_grab_choice=1`,
  `grab.md`) are proven by the math checks and by log lines of a desktop run
  with a rock and a tree. No run had a villager beside a tree. In the quest to
  save the children, check that the halo goes to the character and not to the
  tree (Ctrl Delete with `test_keys=1` changes the choice). Open numbers: the
  bias of 3 degrees, the least size of 2 degrees, and the range of 10 m for a
  Creature command. The screen test offers an object to the pick by its base
  point (12 degrees), so a tall tree that the ray crosses only at its crown,
  far from its base, is a candidate only when the draw offers it by the pixel.
- **P2. A held grip on the far Creature makes it stay.** When the Creature
  is the far target and the grip is held, the Creature stops and stays
  where it is for as long as the grip is held. A short grip on the far
  Creature changes the leash at the close of the grip (`far_leash=1`), so
  the short press and the hold need a rule that keeps both. Find how the
  plain game makes the Creature stop and attend to the Hand, so that the
  mod uses the game's own way.
- **P4. A choice of control scheme, the default of the zoom, and a two
  hand grab as in Deisim.**
  - A choice of control scheme: more options, so that each player chooses
    how the controls work. For example: the zoom in steps or smooth, the
    turn in steps or smooth, and the grab of the world with one hand or
    with two. Many of these are ini keys, which a player does not see. Find
    where the player chooses them (the launcher, or a menu in the game),
    and keep the scheme of now as the default.
  - The default of the zoom: the stick zooms in steps of 1.5 times, and
    `god_zoom_smooth=1` gives a smooth zoom while the stick is held
    (`god-view.md`, section 2). Only an ini key chooses it. Compare the two
    in a headset with the test key (Ctrl, Shift and Insert), set the top
    rate (`god_zoom_smooth_ms`), and choose the default. Then give the
    player the choice in the place of the first point. This goes with B7:
    the point of the zoom is in one function (`GodZoomPoint`).
  - A two hand grab as in Deisim: the player holds both grips, and the
    hands make the world smaller or larger and turn it. The mod has a
    version of this (`god-view.md`): both grips turn the land about up and
    scale it with the spread of the hands, about the land point under the
    middle of the hands. Before the work starts, compare the controls of
    the mod with the controls of Deisim. Points to compare: the turn is
    only about up here, the height of the land stays, and the grip of the
    Hand also takes objects.
- **P5. Hand tracking.** The player plays with the hands and no
  controllers: the Hand follows the real hand and its fingers, one to one,
  as it follows a controller. First find what the headset gives to the
  game: whether Virtual Desktop (VDXR) gives a 32 bit OpenXR program the
  extension `XR_EXT_hand_tracking`, and which gestures it gives. Then
  choose a gesture for each control: the grip, the trigger, the stick, and
  A, B, X and Y. The rules of the Hand stay: the real hand, one to one, and
  no motion that the player did not make.
- **P6. Hints that count successes.** An option behind the hint setting:
  with all hints on, each hint shows always. With a different setting, a
  hint goes away after the player did its action a few times.
- **P7. A view for videos.** A 16:9 view in the game window with the cards
  and the subtitles in it and a smoothed head, for a capture on the PC.
- **P8. Pictures in the hints.** A flat picture of each button, then a
  picture of the controller with the button lit, then a 3D model of the
  controller (`controller-hints.md`).
- **P9. The pointer on the interface panel**, and the screens with no
  scene (menus, loading, videos) on a panel in the room (`ui-layer.md`).
- **P10. The tabletop mode** (play mode 3) is a test mode. Not built: a
  game camera over the table, the mouse, the wheel, the stick and the
  double click for the table, the ring and the depth key for the key color,
  and the place of the table in a real room (`tabletop.md`).
- **P11. The intro in stereo** (`intro_vr`, off by default) is not finished
  (`intro.md`). In a headset:
  - Some things show double, here and there. Find which draws get a wrong
    depth or go to one eye only.
  - Nothing moves. The mod holds the camera still (`intro_still=1`), and
    the player sees the formed logo from the start. In the intro of the
    game the orbs stay where they are. The game moves its camera between
    them, and then away to the place from which the logo shows.
  - Wanted: motion that shows the depth of the scene and does not make the
    player sick. An idea: the camera moves along the path of the game and
    does not turn. This is motion that the player did not make, so decide
    first whether the intro is an exception to that design rule. A way that
    keeps the rule: the orbs are a small model in front of the player, the
    path of the game moves the model, and the room stays still.
  - A push of the particles in 3D with a controller.
- **P12. Ideas from Half-Life: Alyx** that are not built are in
  `research-hla.md`, for the far grab, the teleport, the hands, the hints,
  the sound and comfort.

### Built, and not proven or not finished

- **N1. The frame time.** Headset sessions gave means between 73 and 80
  fps, and the scenes were not the same. Measure one scene with a fixed
  recipe before and after a change. The cause of the worst frames (31 to
  52 ms) is not known. Ideas for the render are in `backend.md`: one walk
  of the scene for both eyes, a projection off the centre for each eye,
  and the game turn off the render thread.
- **N2. The hands that react.** The part for bodies that the hand passes
  is built and off (`hand_feel_bodies`): no picture proves it. Not shown: a
  tree taken with the palm to the side. The idle wave is off
  (`hand_feel_idle`).
- **N3. The hand model.** The game's finger animations are not played (a
  pose is a curl for each finger). The shadow of the Hand on the land has
  the game's fingers. The textures of an edited hand file are not read.
- **N4. The Temple.** Not done: the Challenge Room, the Library, a value of
  the options box, a drag of a scroll. Suspects: the picture of a new save
  is flat magenta, the waist point is 8.4 units higher after a load, the
  ray ends short of the pointer in the left eye in a dialog box inside the
  Temple, and the leash posts are 1.4 cm from each other at the scale of
  the start. In the ring menu: a thumb on the left stick can open it, and
  a jump to the town or to a bookmark takes the heading of the game camera.
- **N5. The village.** Not run: food, a tree, an animal or crops set down
  at a store, the altar, a gift to the Creature, the red halo outside the
  influence, a Workshop that gives scaffolds, villagers that make chants,
  a miracle of a Worship Site that is not Water.
- **N6. Miracles.** Not run: food, wood, lightning, forest, teleport,
  explosion, flocks, the other Creature miracles, and each reason of a
  refusal but "needs a Creature".
- **N7. The commands of the Creature.** Not run: the walk of the Creature
  to an object that the leash is tied to.
- **N8. The scripts and the engine camera.** The scripts get the place of
  the player. Not changed: the 114 markers at "camera position",
  `GetFacingCameraPosition`, and the other readers of the engine camera.
  Not run: the scoreboard and the Creature scrolls.
- **N9. The sound.** `audio_loops_keep` is built and not proven. The open
  points of the sound are in `audio.md`.
- **N10. The cut scene window.** Limits: the plane stays near for some
  seconds after a near thing left, a thing smaller than one grid cell can
  be in front, and the advisors are in the measure.
- **N11. The God view.** No desktop run: the mouse pan, the events that a
  step of the tutorial waits for, the load of a saved game, and a pick up,
  a set down and a throw in the God view. The teleport has only its math
  test. Some cameras of the game still glide: the follow of the Creature
  after a double click, the pull back past 3500 units, the arrow keys and
  the automatic tilt (`backend.md`).
- **N12. The far grab.** No desktop run: the fetch of a thing that floats,
  a villager in the sea, a Field or a FishFarm as a target, a take while
  the Hand is outside the influence, and a second object in the Hand
  (`grab.md`).
- **N13. The exact Hand.** No desktop run in the Tug, Holding, Creature,
  Grain and PlayAnim states. The Totem and the Citadel states are not read
  (`hand.md`).
- **N14. The throw.** Not tested: a tree that flies, a villager, a throw
  into a pile and into the sea, and a scale other than 100 units for each
  meter (`throw.md`).
- **N15. The interface panel** (`ui_panel=1`). No run, on the desktop or in
  a headset (`ui-layer.md`).
- **N16. A PC with no DirectX 9 runtime.** No test has run the mod on a PC
  that never had the DirectX 9 runtime installed.
- **N17. Only one headset.** No person has run the mod with a headset or a
  runtime other than a Quest 3 through Virtual Desktop.

### The repo, the release and the tools

- **R1. The installer and a person.** No person has run the installer on a
  real game folder and told the result: the install, an update over an
  older build, and the uninstall.
- **R2. The run scripts of one area** (`village_run.ps1`, `temple_run.ps1`,
  `miracle_run.ps1` in `bwvr/tools/desk/`) have not run from this repo, and
  `hand_run.ps1` has run only with `-Kind one`. `-Box` of `hand_run.ps1` is
  a place of the start dialog at one render size.
- **R3. The game window is in front in a desktop run.** Windows gives the
  front to the game window of a run on a PC that nobody uses, with no call
  of the game process (`bwvr/tools/desk/README.md`, and `backend.md`, "Who
  moves the cursor of Windows"). The window stays in front until the run
  ends. The real cursor stays where it is. Not tried: a start of the game
  that keeps its window behind.
- **R4. The names of the symbol file.** `bwvr/tools/check_names.py --all`
  lists about 70 decorated names whose parameter classes are not those of
  the Mac symbol, with each class by name (for example an `int` for an
  enum). Nobody has read them one by one. About 100 names with a class as
  a back-reference digit have no Mac symbol to compare with.
- **R5. A file of names of the project.** When contributors start to name
  functions that the symbol file does not have, put those names in
  `bwvr/ghidra/name-corrections.txt` (the tools read it).
- **R6. Look at REA again from 2026-10-15.** REA (`github.com/morluto/rea`,
  the npm package `rea-agents`) is an MCP server that gives an agent the
  pseudocode, the strings and the references of a binary through Ghidra.
  At version 6.0.0 it does not fit: on Windows it is experimental and read
  only, and each session imports the exe into a new temporary project, so
  it has none of the names that `bwvr/tools/re.ps1` gives. It fits when it
  can open a project that is there, or take a symbol file, on Windows.
