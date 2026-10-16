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

- **B2. A catch at once: check it in a headset.** A press of the grip on
  an object that moves (it falls, flies or rolls) puts the object in the
  hand from the first frame after the press. The game sends its pick up at
  once, the body of the object stays where it is, and the mod draws the
  object in the Hand (`catch_at_once=1`, `grab.md`, "A catch at once"). No
  such press is a click, so no rock splits. Desktop runs show it at the frame
  rate of the rig and at 90 frames a second, with a rock in its fall, a rock
  that the hand threw up, a grip of 50 ms, a throw and a drop after the
  catch, and a fetch from a distance of a tree that still moves.

  Check in a headset, with a real hand:
  - that a rock in its fall, a rock that you threw up and a rock that rolls
    on the land is in the hand at the press: it does not move on, and it
    does not jump,
  - that its way to its place in the hand (0.12 s) looks right when it was
    15 to 23 cm from the finger tip at the press,
  - that it leaves the hand as each held object does: a throw with the speed
    of the hand, or a drop,
  - that an object that still moves comes from a distance with the glide
    from the press, and does not wait at its place first,
  - that no grip on an object that moves breaks a rock,
  - that the guard of a throw does not stop a catch (`catch_moving=3`: the
    guard ends when the grip is fully open).

  The test key (Ctrl and Shift with Backspace) gives the pick up of the game
  for a comparison: the object moves on for 0.3 s and then jumps into the
  hand. Still open:
  - Not shown: a grip on an object that rolls for more than 1 s. The recipes
    `rollnear`, `rollfar`, `droproll` and `treenear` do not reach that state
    on the desktop (`bwvr/tools/desk/README.md`, "Known limits of the
    recipes").
  - Not shown in a run: the guard of a throw. The scripted grip opens in one
    frame, so it is fully open at once. The math checks have the rule.
  - A press with no target when a ball rolls 23 to 32 cm from the Hand's
    point: the object is past its reach of the pick, and the aim is not on
    it, so the grip does nothing.
  - A tree that is let go comes to rest in about 1 s now
    (`throw_tree_settle`, `throw.md`, "The landing of a tree"). Not shown:
    a far target that hides and shows by itself (not seen in a desktop
    run), a tree thrown fast across the land, and the look in a headset.
    A tree that does not move for 0.5 s is at rest, also while its torque
    is over the limit of the rest test of the game (`throw_land_rest`,
    `throw.md`, "The rest after a landing"). A tree thrown at 3 m/s left
    the physics 2.6 and 4.3 s after the release (`throw_queue.ps1 -Runs
    tree`).
- **B4. The card does not know a body in the way of a planted tree.** The
  card says "plant" by the tests of the game that it can read (the height,
  the lean, dry land, fire, `grab.md`, "A tree in the Hand"). The game also
  lets a tree fall when it raised the tree over a body at the release, and
  the card does not see that. A desktop run shows the fault: a tree set down
  upright on dry land with a rock 0.8 units from its place has the card
  "Plant", and the game does not plant it. The log has the 6 objects of the
  game's physics that are near the tree, and the flag of the tree that the
  release leaves out when it raised the tree (`throw_queue.ps1 -Runs
  treeset -NewGame` in `bwvr/tools/desk`).
  The test of the game puts each near object into its physics and compares
  two physics bodies, and the tree has a physics body only from the release
  on (`grab.md`, "The test of a body in the way"). So no call of the game
  gives the answer before the release with no change of state. A tree that
  stands and a forest are never in the way. A rock, a scaffold, a Creature,
  a dead tree and a building can be. Not built: the same test with physics
  bodies that the mod makes for the tree and for each near object.
  The lean is shown in a desktop run: a hand that turns 30 degrees makes the
  tree lean 0.26 and -0.46 rad, the card says "Hold upright to plant", and the
  tree has the same lean in the physics of the game (`-Runs treelean
  -NewGame`). That
  run is at the place with the rock, so it does not show that the lean alone
  stops the planting. Not run: a tree that leans at a place where the game
  plants an upright tree.
- **B1b. The roots pile of a far grab.** `far_roots_at_tree=1` puts the pile
  at the roots, as for a tree that the Hand takes. Only a headset shows
  whether that is what the player means by the "pit" at the roots. Not
  run: a tree that comes from a distance and is then planted again. A
  tree that is thrown, or that falls from more than `release_fall_cm`,
  is not planted by the game: it takes the path of a dead tree.
- **B6. A script that waits for the dialogue has the camera of the game.**
  A script that has the camera and waits for the dialogue shows the camera
  of the game with a held shot and no cinema bars, and not the God view
  (`cutscene-window.md`, section 2). It can show when a quest starts while a
  text of a different script waits for a click (`text_click`,
  `controller-hints.md`, section 10). No run and no headset session has
  shown it.

- **B10. A thrown object that touches another object: check the picture in
  a headset.** In a substep where another object touches the thrown object,
  the game runs one step, and the mod gives that substep 5 ms of the picture
  (`throw_draw=1`, `throw.md`, "The picture between the steps"). So the
  object moves through the touch at the speed of the game's own time, and
  the picture does not stand and jump. A desktop run with a thrown villager
  shows it, and the stand and jump with `throw_draw=0` (`village_run.ps1
  -Kind throw` in `bwvr/tools/desk`). Only a headset shows whether the slower
  move in the touch looks right. The test key (Ctrl and Scroll Lock) gives
  the rule 0 for a comparison. Not run: a picture that stays at the place of
  a guess (the math checks cover it). A run shows the clock that falls back
  after a wait.
- **B11. A tree that comes to rest while the Hand takes it: check it in a
  headset.** The game makes a dead tree of a tree that fell when the tree
  comes to rest, and it deletes the tree. The Hand has an object from the
  press, and the game puts it in the Hand 2 or 3 game turns later. With the
  rule of the game, a tree that comes to rest in those turns leaves the Hand
  at once, and the Hand is empty with the grip closed. With
  `catch_rest_guard=1` the tree does not come to rest in those turns: it
  stays a tree, and it is in the Hand until the grip opens (`grab.md`, "An
  object that comes to rest while the Hand takes it"). The take starts at
  the lock of a far grab. With `catch_at_once=1` the body of the tree does
  not move after the press, so the rest can come only in the lock and the
  pull. Desktop runs show both ends: with the rest of the
  game itself in a turn of the take (one run with each value), and with a
  test key that makes the rest in the first turn in which the Hand has the
  tree (`throw_queue.ps1 -Runs treedropend -More 'catch_at_once=0'` in
  `bwvr/tools/desk`, and the same with `catch_rest_guard=0`). Check in a headset that a tree
  which fell comes to the hand each time. The test key (Ctrl, Shift and
  Scroll Lock) gives the rule of the game for a comparison.
- **B13. The Temple in a headset: check the view, and find the black
  frames of the guide.** In a room the head owns the view: each eye pass
  draws from the head pose of the frame, with its roll and the place of its
  eye (`temple_view=1`). The head does not turn the camera of the room
  (`temple_camera_screen=1`), and the fade of the Temple shows in both eyes
  (`temple_fade_eyes=1`). `controllers.md`, 11.4, has the three rules. The
  math checks cover them. Still open:
  - No desktop run has shown the three rules. The recipes are in
    `controllers.md`, 11.9. Make those runs first.
  - Check in a headset: the room stays still and level when the head turns,
    tilts and leans, and it has depth. The left eye is not dark at the entry
    of the Temple, at a change of the room and at the way out. A look down at
    the pool of the hall does not move the player. The test keys (Shift and
    the slash key of the number pad, Ctrl, Shift and the plus key) give the other
    ways for a comparison.
  - Single black frames showed at random times in the guide of the first
    visit of the hall. The cause is not known. In that session the camera of
    the hall was at its view of the pool for the whole guide, and the mod now
    keeps it at its rest pose. If the black frames stay, a session with
    `eye_test_luma=1` names each black frame of the render of the game in the
    log (`backend.md`, "Testing without a headset").
  - Not built: the depth of a guide in the cut scene window, and a ray of the
    room that starts at the eye (`controllers.md`, 11.9).
- **B14. In a pass the second eye does not have zero as its frame time.**
  After the two eye passes the game has the real time of the frame as its
  frame time (`frame_time_real=1`, `backend.md`, "Time, bars and the second
  eye pass"). So the cameras of the game move at the speed of the game, also
  in a game from a saved game.
  Not run: a story camera with no
  cinema bars, the camera of a Creature fight, the pull back past 3500 units
  and the moves of the game camera by the keys. They read the same number.
  Not known: why the Creature is one step further in its plan 1 s after the
  start of play with the real time (the first stay of `creature_recipe.ps1
  -Kind stay`, `backend.md`). Later in a run the plan of the Creature does not
  depend on the key.
  Still open: in the scene walk `LH3DRender::StartFrame` writes the frame
  time at the start of each pass, so the second eye pass has the time of the
  first pass there, and not zero. The readers in the walk count in both
  passes (the hand states, `HelpSystem::Draw3D`, `Temple::Update`), and
  their sum was 4 to 9 percent more than the real time in desktop runs. Not
  built: a hook on `StartFrame` for the scene walk, as the intro has for its
  call (`intro.md`). First find each reader that divides by the number: the
  game never has less than 1 there.
- **B16. The left eye showed pictures of frames before: check it in a
  headset.** In a headset session the left eye started to show faint
  pictures that came late, and the right eye did not. In play the game
  does not clean its render target: the sky dome is the clean of a frame
  (`LH3DRender::g_mode_cleaning` is 0). The two eye passes share that
  target. So a pixel that an eye pass does not cover keeps the picture of
  the other eye: one frame old for the left eye, and of the same moment for
  the right eye. Each eye pass now starts from its own frame before
  (`eye_clean=1`, `stereo-artifacts.md`, "The frame before of each eye").
  No desktop run has shown it: that section has the two runs that are made
  for it. Still open:
  - Not known: which thing of the game left such pixels in that session.
    The log of the session cannot place it.
  - Check in a headset that no eye shows an old picture. Shift and Page
    Down (`test_keys=1`) give the shared render target for a comparison.
    If the fault shows again, the log line `eye clean : an eye pass drew no
    sky` names the scene of that frame.
  - Not run: the video of a falling spell, the one scene of play that draws
    no sky. The first frame of such a scene is drawn as before.
- **B17. The size of a speech bubble, and the name of a villager in a
  headset.** The speech bubble of a signpost is one flat card at the depth
  of the sign (`bubble_card=1`, `ui-layer.md`, section 18), and a headset
  session read it well. The name of a villager is such a card at the depth
  of the villager (`name_card=1`). Desktop runs show both cards (section
  18.5 has the recipes). Still open:
  - The size of a bubble comes from the depth of the sign in units, with no
    change for the scale of the God view.
  - Check in a headset that the name of a villager reads as well as a
    bubble. The ring item `details` with the Hand on a villager shows it.
- **B18. A flash of lightning in the left eye only: check it in a
  headset.** In a headset session the white flashes of the lightning at the
  end of the quest of the singing stones showed in the left eye only. A
  lightning of a storm makes the land white with a light on the land, and
  the game keeps the lights on the land in a list that each scene walk
  empties. The second eye pass gets each light that it does not add itself
  (`land_light_eyes=1`, `backend.md`, "The lights on the land in the second
  eye pass"). A desktop run shows it with the test light
  (`land_light_test=1`), which is the light of a lightning of a storm. Still
  open:
  - No run had a real lightning. Check in a headset that the land is white
    in both eyes at a lightning of a storm and at the Lightning Bolt
    miracle. Ctrl, Shift and the Menu key (`test_keys=1`) give the left eye
    only for a comparison.
  - Not known: whether the flash of that session was a light on the land.
    If a flash still shows in one eye, the lines `land light` in the log of
    that session have the count of the lights of each group at that time. A
    flash with no such line is not a light on the land. `backend.md`, "The
    color over the picture at the end of a frame", has the two full screen
    draws of the end of a frame. The listing shows both in each eye pass on
    the land.
- **B19. A take at a distance from a pile of a miracle.** When the Hand
  takes all of such a pile at a distance, the game deletes the pile, and the
  grip must carry the load (`far_take_carry`, `grab.md`, "A bulk source at a
  distance"). The desktop runs make the case with a test key and a pile of
  the store. Open: a headset session with the pile of a wood or a food
  miracle.
- **B20. The loop of a bulk take.** In the God view the ears decide the
  start of the loop that the game plays while the Hand takes wood or grain
  (`audio_take_loop`, `audio.md`, 6.4), and a desktop run has the loop in the
  stream. Open: only a person can hear the loop and say how loud it is
  beside the other sounds. Each other sound that the game asks for at the
  Hand with no object keeps the test of the game camera (`audio.md`,
  section 12).
- **B21. A failed start of the headset session can stop the game.** With no
  headset, each try to start a session ends in `XrScreen::Stop`. One time,
  in a full screen game, the frame thread did not come back from the first
  try: the log stopped after the line "Connect the headset in Virtual
  Desktop, then start the game.". The cause is not known, and no run has
  repeated it. A full screen game makes no try (`vr_full_screen=0`), and a
  game with `vr=0` makes none. A game in a window with no headset makes a
  try each 3 seconds for 2 minutes.
- **B22. The glow at the Hand's point with one walk: check it in a
  headset.** In one walk the mod draws the glow for each eye by itself, and
  the right eye has it at the eye difference of the Hand's point
  (`one_walk_marks=1`, `backend.md`, "The cards and the marks of the mod").
  A desktop run shows the glow of the right eye at its place in each pose of
  a hand that turns. The glow is off by default (`hand_glow=0`). Still open:
  - Check in a headset that the glow stays at one place on the hand when the
    hand turns. Ctrl, Shift and the right square bracket key (`test_keys=1`)
    give the right eye the glow by the rhw of its disc for a comparison.
- **B23. The card of a signpost with one walk: check it in a headset.** In
  one walk the mod draws the card of a speech bubble for each eye by itself,
  and the right eye has the box and the text at the eye difference of the
  sign (`one_walk_marks=1`, `ui-layer.md`, section 18). A desktop run shows
  the card of the right eye at its place in its own walk. The hint cards and
  the subtitle card are quad layers, and one walk does not draw them. Still
  open:
  - Check in a headset that the text of a signpost is in its box and easy to
    read with one walk. Ctrl, Shift and the right square bracket key
    (`test_keys=1`) give the right eye the card by the rule of each other
    batch for a comparison.
- **B24. After a cut scene the controllers do nothing until a press.** The
  headset turns off a controller that the player holds still in a long cut
  scene, and a press of a button turns it on again (`controllers.md`,
  section 6, "A controller that is still turns off"). The mod cannot turn a
  controller on. Still open: check in a headset whether a still controller
  stays on when the setting of the headset that switches between the
  controllers and the hands is off.
- **B26. The tutorial after its first step.** The aim: a player can play the
  whole tutorial in VR, also where the words of the game do not fit the
  controllers. The first step is shown in a desktop run: the walk to the
  village with the teleport and with the stick (`god-view.md`, section 14.9).
  No run and no headset session has the later steps: the way to the Temple,
  the three lessons of the camera, the guide at the mountain. `god-view.md`,
  section 7, "The interface levels", has the camera controls that the game
  allows in each step. Wanted: a headset session through the whole tutorial,
  and a correction for each step that stops the player.
- **B27. The walk with the stick is not smooth in the headset.** The walk
  itself is even: each frame moves the view at the speed of the push, and no
  limit, hill or switch stops it on some frames (`god-view.md`, section 5.7,
  "An even walk"). The likely cause is the frame rate: about 97 frames each
  second on a display of 120 Hz with Space Warp off, about one refresh in
  five has no new frame. The headset then shows the frame before again, so a
  move of the view stops for a refresh and jumps. Wanted: a headset session with a walk. The line of the end of
  each walk counts the frames that came more than one refresh late. Compare
  a walk at 120 Hz with a walk at 90 Hz in Virtual Desktop, or with Space
  Warp on. If the walk is even at 90 Hz, the fault is the frame rate, and
  this item goes to the performance work.
- **B29. The advisors and the cut scene window.** In a cut scene the mod
  draws the advisors over the window and outside it at times, and their
  depth is the depth of the inside of the window. Wanted: the cut scene
  plays inside the window, and the advisors have a depth between the
  player and the window. What is known (`cutscene-window.md`, section 11):
  - The advisors are meshes of the scene. `HelpDude::Draw` `0x005C0700`
    draws them from `HelpSystem::Draw3D` `0x005C59A0` (called by
    `GGame::Process3dEngine` at `0x0054E2ED`), at the screen points x =
    +0.66 and -0.66, 7.5 to 11 units from the camera. They are in the
    measure of the nearest thing, so with the advisors in view the screen
    plane is at about 6 to 9 units, and they show a little behind the
    screen.
  - Out of the measure, the plane goes back to the scene. With the plane at
    15 units an advisor 7.5 to 11 units away is seen 1 to 1.5 m away, in
    front of the screen. In a close shot the plane is nearer than the
    advisors, and they go behind the screen again.
  - So a fixed place in front of the screen needs a fixed difference
    between the eyes for the advisor batches, in the two walks and in one
    walk (there the copy for the right eye moves each vertex by its rhw).
  - The work needs a mark of each advisor batch at the draw. The draw calls
    of `HelpDude::Draw` can go through the sort list of the engine, so a
    flag for the time of that call is not proven to mark them.
  - The edge of the opening must not cut an advisor that is in front of
    the screen. At x = 0.66 the middle of an advisor is 17 percent of the
    width from the edge.
  - A desktop run does not show it: the first story of a new game has no
    advisor in it.
- **B30. The gift of an object to the Creature: check it in a headset.**
  An object in the Hand that the player lets go at the Creature is a gift,
  with the leash on and with no leash. The gift goes on while the hand is at
  the Creature (`creature_gift=1`), and a Creature that is the target of
  the object stays the target to 6 cm (`hold_creature_stay=1`,
  `creature.md`, section 2.9). Desktop runs show a pig that the Creature
  takes with the leash on, and the game that ends the same gift with
  `creature_gift=0` (`creature_recipe.ps1 -Kind gift` in
  `bwvr/tools/desk`). Check in a headset, with a real hand:
  - The Creature takes a pig at the first opening of the grip, with the
    leash on and with no leash.
  - A move of the hand away from the Creature keeps the object in the Hand.
  - 20 cm (`creature_gift_cm`) is a good reach, and the halo on the
    Creature is steady while the hand is at it.
- **B31. With the leash on, a grip at a distance takes nothing from some
  objects.** The grab at a distance has no test of the leash: with the
  leash on it takes each object in its range. Three rules stop a grip
  there, and the halo does not tell them apart:
  - The target of a Creature command has its halo to 10 m of the room from
    the head (`creature_command_max_m`), and the grab at a distance takes an
    object to 2 m (`far_grab_max_m`). So a tree at 2.2 m has the halo of a
    command, and a grip on it takes nothing.
  - The game does not let the Hand take the object that holds a tied
    leash. A press on that object is a press for the leash (hand mode
    `0x20`), at the Hand and from a distance.
  - Near objects win (`grab.md`): an object that a press acts on near the
    Hand's point turns the grab at a distance off. A Creature on the leash
    comes to the Hand, so this happens more often with the leash on.

  To decide: a halo or a line of the card that shows which objects the
  grip takes, a longer range of the grab while the leash is on, or no
  change.
- **B32. A fight of Creatures: test the grip and the marks in the
  headset.** The grip with the Hand at a Creature is the attack or the
  block of the game, at the place of the body that is nearest to the Hand
  (`fight_grip=1`). The marks of the fight are at their place on the body
  (`fight_mark_depth=1`). No desktop run reaches a fight, so only the math
  checks and the code show it (`creature.md`, section 6). To see in the
  headset: the attack and the block act at the first grip, and the stars
  sit on the body in both eyes.

B1b, B2, B4 and B11 are one package: a thing that is held, thrown, or
that moves while the Hand takes it.

### Changes to the controls

- **P1. The choice of a far target, and the walk mode of a command: test
  them in the headset.** A living thing in its cone comes before each thing
  that is not living, and a living thing that has the halo keeps it past its
  cone (`far_grab_choice=2`, `far_grab_stay=1`, `grab.md`, "The cone and the
  choice"). A quest character is a living thing for this choice. A press of
  the trigger on an object becomes a walk when the aim leaves the object
  (`creature_command_rule=4`, `creature.md`, section 4.1). The math checks
  and desktop runs show the rules, with a villager beside a tree and a rock.
  No run had a quest character beside a tree. Check in a headset:
  - the quest in which a man leads the children away: the halo goes to the
    man with trees around him, and it stays on him while the hand shakes,
  - the children behind the man: the aim on one of them takes the halo from
    the next one when it is 1 degree nearer to the ray,
  - the cone of a living thing of 4 degrees (`far_grab_living_deg`) and the
    stay margin of 3 degrees (`far_grab_stay_deg`): a tree, a rock and a pile
    of a store beside a villager must still be easy to take,
  - the other values, by Ctrl and Delete (4 balls with ranks, 3 balls, 1 the
    bias, 0 one point) and by Ctrl and Backspace (`far_grab_stay`),
  - the walk mode with a real trigger: the 100 ms before a press on an object
    becomes a walk, the tick at that time, and rule 3 by Ctrl and Numpad 5.

  Open numbers: the least size of 2 degrees, and the range of 10 m for a
  Creature command. The screen test offers an object to the pick by its base
  point (12 degrees), so a tall tree that the ray crosses only at its crown,
  far from its base, is a candidate only when the draw offers it by the pixel.

  The Creature, a miracle and a signpost in the aim (`far_grab_creature`,
  `far_grab_miracle`, `far_grab_sign`, `grab.md`, "The cone and the
  choice"): the aim measures your Creature by its body, a miracle that the
  Hand can take is in the first rank, and a signpost is in the last rank.
  The math checks and desktop runs show the rules, with a miracle beside a
  signpost and with a thing of no rank beside the Creature. No run had a
  tree or a signpost beside the Creature, a large Creature, or a miracle of
  a quest. Check in a headset:
  - the aim on the body of your Creature gives it the halo with a tree or a
    signpost beside it, and a tree beside it is still easy to take,
  - a miracle beside a signpost takes the halo, and it keeps it,
  - a signpost is still easy to choose with the aim on it.

  A signpost and the Creature at the Hand (`hand_sign_last`,
  `hand_wide_creature`, `grab.md`, "The rule of the choice" and "Near
  objects win"): a signpost that the Hand's point is not in comes last, it
  does not turn the grab from a distance off, and the Creature has the wide
  reach of 15 cm. The math checks and desktop runs show the rules. No run
  had a miracle in the wide reach beside a signpost, or a grip that stays
  closed on the Creature in the wide reach. Check in a headset:
  - a miracle or the Creature beside a signpost is the target of the grip,
    and the aim keeps its target with the hand beside a signpost,
  - a signpost is still easy to tap: with the Hand's point in it, or with
    no other target in reach and no target of the aim,
  - the grip on the Creature from 4 to 15 cm: the leash click, and the start
    of the Creature state with the hand that far from the body.

  A tree, a building or a pile at the Hand (`far_grab_beside_loose=1`,
  `grab.md`, "Near objects win"): one that the Hand's point is not in does
  not turn the far grab off, and it gives way to a far target. The math
  checks and desktop runs at the Village Store show the rule. Check in a
  headset:
  - the aim past a tree, a house or a pile with the hand beside it, at the
    Creature too,
  - a tree, a house or a pile is still easy to take or to tap: with the
    Hand's point in it, or with the aim on no far target.
- **P2. The stay of the Creature: test it in the headset.** A grip from a
  distance that stays closed on the Creature makes it stop and stay until
  the grip opens. A short grip changes the leash when it opens
  (`creature_hold_stay=1`, `creature.md`, section 5.3). The stay is the
  locked select of the game, with no stroke, no lesson and no camera.
  Desktop runs show it with a Creature that stands and with a Creature that
  walks. Check in a headset:
  - the hold time of 350 ms with a real grip (`creature_hold_stay_ms`),
  - where the Creature turns to, and how its head follows the real hand
    (`creature_stay_look`),
  - whether the halo, the pulse and the card show well that the stay is on.

  The leash click, the stay and a command act on the Creature of the local
  player only: the code compares each Creature with the Creature of that
  player, and a Creature that is not the player's own is no far target
  (`creature.md`, section 5.1, "Only your own Creature"). The math checks
  have the rule. Not run: the aim and the grip at a Creature that is not
  the player's (`aim_queue.ps1 -Runs other` in `bwvr/tools/desk`), and a
  game with the Creature of a different god (a skirmish or a multiplayer
  game).

  The test key (Shift and End) gives the leash click at the close of the
  grip for a comparison. Not run: a start that the game refuses, and a grip
  that closes again before the end of a stay went to the game.
- **P4. A choice of control scheme, the default of the zoom, and a two
  hand grab as in Deisim.**
  - A choice of control scheme: more options, so that each player chooses
    how the controls work. The launcher offers the turn in steps, the zoom
    in steps or smooth, the center of the zoom, the walk with the stick of
    the other hand (along the direction of the head, along the direction of
    the controller, or off), and the grip that grabs the world
    (`launcher.md`, "Moving around" and "Controllers"). The layout of the
    buttons of the other hand has an ini key and no row (`button_layout`,
    `controllers.md`, 5.1). Not built: a smooth turn with the stick, and a
    choice in the game (the launcher runs before the game). Keep the scheme
    of now as the default.
  - The default of the zoom: the stick zooms in steps of 1.5 times, and
    `god_zoom_smooth=1` gives a smooth zoom while the stick is held
    (`god-view.md`, section 2). Compare the two in a headset with the test
    key (Ctrl, Shift and Insert), set the top rate (`god_zoom_smooth_ms`),
    and choose the default.
  - A two hand grab as in Deisim: the player holds both grips, and the
    hands make the world smaller or larger and turn it. The mod has a
    version of this (`god-view.md`): both grips turn the land about up and
    scale it with the spread of the hands, about the land point under the
    middle of the hands. Before the work starts, compare the controls of
    the mod with the controls of Deisim. Points to compare: the turn is
    only about up here, the height of the land stays, and the grip of the
    Hand also takes objects.
- **P13. The walk with the stick and the layout of the left controller:
  check them in a headset.** The stick of the hand that does not point moves
  the player over the land of the God view while it is pushed
  (`move_stick=1`, `god-view.md`, section 5.7). The menu button of the left
  controller opens the game menu, Y opens the ring, and X has no job
  (`button_layout=1`, `controllers.md`, 5.1). The math checks and desktop
  runs show the rules. Check in a headset, with `test_keys=1`:
  - The comfort of the walk, with the head as the direction and with the
    controller as the direction. Shift and the Menu key of the keyboard
    change `move_stick` from 1 to 2, to 0 and back.
  - The comfort of the follow of the land (`move_stick_follow_land=1`, the
    launcher row "Walk follows the land"): whether the view goes up and down
    calmly on the hills, and whether the land stays near enough to the hands
    on a steep slope. The filter is 50 cm of the walk behind the land.
  - The top speed of 150 cm of the room for each second
    (`move_stick_speed_cm`), near the land and far out, and the dead zone of
    the stick.
  - Whether Y is easy to hold while the hand moves to an item of the ring.
    Ctrl and the Menu key change `button_layout` between 1 and 0.
  - That the left menu button always comes to the game. Virtual Desktop can
    keep that button for its own menu (`controllers.md`, section 6), and
    with `button_layout=1` no other button opens the game menu.

  Not run: a small push of the stick, the edge of the map, a push with the
  switch for the drag off, an Index controller, and a controller with no
  face buttons.
- **P14. The stream of a take at a distance: check it in a headset.** In a
  take at a distance the pieces of wood or grain go on a curve from the top
  of the source to the Hand (`take_stream`, `grab.md`, "The stream of a take
  at a distance"). Desktop runs show the curve from a pile of wood of the
  Village Store, with the Hand 0.27 m to the side of the pile and 0.5 m over
  it. Open:
  - Only a headset can show whether the curve reads well in depth, and how
    it looks with a hand that moves. The test key is Ctrl, Shift and the
    minus key.
  - A piece lives 1 s for each length of the way, and 8 pieces start in a
    second. So on a way of 2 m the pieces are 25 cm apart, and they go 2 m
    in a second. If that does not read well, the rule has both values
    (`RaiseTime` at `+0x2C`, `EmitRate` at `+0x30`), and the hook can change
    them for a take at a distance.
  - No run has shown the stream from a pile of food, a field, a fish farm or
    the pile of a miracle, a way that is long along the floor, and a pile
    that goes down in the take.
- **P15. The glow on the mesh of a target: check it in a headset, and the
  targets that keep the halo.** The mesh of a target of the Hand glows in
  the color of its halo, with a pulse and a band of light that goes up the
  mesh (`target_glow=1`, `hand-model.md`, "The glow on the mesh of a
  target"). Desktop runs show it in both eyes on a rock, a villager, a bush,
  a pile and the Village Store. Open:
  - Only a headset shows whether the strength is right by day and by night
    (`target_glow_pct`, 45), whether the pulse and the band of light are calm
    enough at the size of the God view, and whether a small target needs
    more than the light on its mesh. The test key steps `target_glow`
    through the glow, the halo and both.
  - The Creature (`LH3DCreature::DrawNow`, `0x0048E260`) and a building in
    work (`0x00517F90`) do not draw through `Game3DObject::AddForDrawing`.
    They keep the halo. Wanted: the glow on their mesh.
  - An object that the game draws as a picture far away keeps the halo.
  - No run has shown a mesh of the sorted list as a target, a mesh that
    comes through the clip of the engine (`LH3DRender::DrawAndClip`), or a
    target by day.
  - The light is even on the mesh. A glow that follows the bright parts of
    the texture needs a second draw of the triangles with additive blend.
    Not built.
- **P16. The margin around the view: check it in a headset.** "Margin around
  the view" in the launcher (`fov_margin_pct`, 0 to 30, default 0) makes the
  render go past the view of the headset on each side (`backend.md`, "A
  margin around the view"). No run has shown it: a desktop run has no
  headset, and only a session with a headset reads the key. Check in a
  headset:
  - That the world keeps its place and its scale with a margin, and that the
    Hand and the pointer are where the controller is.
  - That a fast turn of the head shows less black at the edge, and which
    margin is enough at 90 Hz and at 120 Hz.
  - The cost in frame time (`profile=1`).
  - The interface in the eye pictures (the Creature panel, a tooltip): with a
    margin it is larger and farther from the middle. Not known: whether that
    needs a correction.
- **P17. A cut scene with no window: check it in a headset.** The launcher
  row "Cut scenes", "Around you" (`cutscene_immersive=1`) plays a cut scene
  around the player, with no window and with the camera of the game and all
  its motion (`script_camera=0`). The window stays the default. No run has
  shown it: a desktop run has no head to feel the motion.
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
- **P11. The intro in stereo** (`intro_vr`, off by default) needs a headset
  check (`intro.md`, "What only a headset can show"). Each eye has its own
  trails, the mark beside the logo has the depth of the logo, and the view
  goes along the camera path of the game with no turn (`intro_motion=1`).
  With `test_keys=1`, while the logo plays:
  - Is the doubling gone for the eye? Shift and Page Down switch
    `intro_eye_trails`, and Shift and Delete switch `intro_text_place`.
  - Is the motion comfortable? Shift and Page Up step `intro_motion`: 2 is
    the orbs as a model in a room that stays still (it keeps the design
    rule "no motion that the player did not make"), 3 is the camera of the
    game with its turns, 0 is the still camera. `intro_speed_pct` makes the
    path slower. Then choose the default, and decide if `intro_vr` goes on.
  - Not built: a push of the orbs with a controller, and a push in 3D. A
    controller does not push the orbs now (`intro.md`, "Open work").
- **P12. Ideas from Half-Life: Alyx** that are not built are in
  `research-hla.md`, for the far grab, the teleport, the hands, the hints,
  the sound and comfort.

### Built, and not proven or not finished

- **N1. The frame time.** `bench_run.ps1` measures six fixed scenes on the
  desktop, and `profile=1` gives the same numbers for a session in a headset
  (`backend.md`, "Frame cost"). Still open:
  - **A session in a headset with `profile=1`.** No such session has run. A
    desktop run has no wait for the runtime, no copy to the bridge, no
    compositor and no encoder. The lines `frame cost   : window` of the log
    say what these cost. They also say what a slow frame is made of when it
    is not a game turn. Set the key after the last save of the launcher.
  - **The wait at the stop of a sound.** The mod skips the 20 ms that
    the audio DLL waits on the frame thread at the stop of a sound
    (`audio_stop_wait=0`, `audio.md`, 7.6). In a desktop run the worst frame
    at the village, less the copy to the monitor, is 10 to 12 ms with the key
    at 0, and 32 to 72 ms with the key at 1. A headset session showed no fault of
    the sounds. With `test_keys=1`, Ctrl and Pause change the
    key while the game runs. A sound that QMixer plays (an advisor, the
    speech of a story) keeps the wait.
  - **Slow frames that stay.** The Creature reads an animation from the disk
    at its first use, and the game turn then takes 4.5 to 9.5 ms. The first
    use of a texture or of an effect file makes an eye pass 8 to 15 ms
    longer, when Windows does not have the file in memory. One time a minute
    the game writes the alignment of the player and the mind of the Creature.
    Its file calls take 2 ms most times, and they took 28 ms one time. A load
    of these files before the game needs them is not built.
  - **The texture of 32 by 32 that the land draw writes.** With two writes
    in one scene walk, its two locks take 0.6 ms for each eye pass. That is
    1.2 to 1.5 ms of a frame of 8.7 to 10 ms at the door of the Temple. With
    one write the lock takes 0.005 ms. The cause is not known: a wait for
    the GPU in the backend is the first candidate. First step: read what
    `Lock` of dxwrapper does for a surface that a copy used in the same
    frame. Then name the thing that the second write is for (the list at
    `0x00FAA7E0` of `LH3DIsland::Draw`).
  - **One walk of the scene for both eyes.** It is on by default
    (`one_walk`, `backend.md`, "One walk for both eyes"). The batches
    of eye pass 0 go to a wide target, and a hook draws each one again for
    the right eye, moved by the disparity of its `rhw`. On the desktop the
    scene time of `verify_build.ps1` went down by 35 to 40 percent. Not shown:
    a session in a headset with a person in it, and its feel.
  - **The read ahead of the subtitle card, and `frame_send=2`.** With
    `frame_send` 1 and 2 the frame thread does not wait for the GPU, but the
    read of the copy of the subtitle card did (`ui-layer.md`, 17.2). The
    read ahead takes that wait away (`subtitle_read_ahead=1`). Not shown: a
    headset session with `frame_send=2` and the card on, and its frame
    rate against `frame_send=0`. The default 0 came from headset sessions
    in which the card read had that wait. A desktop run cannot show it:
    with no headset the mod makes no wait for the GPU, and no thread has a
    frame to send, so `bench_run.ps1` gives the same numbers for each value
    of `frame_send`. The comparison needs two sessions in a headset with
    `profile=1`, at one place with a still head.
  - **The merge of draw calls in a headset.** The mod gives a row of draw
    calls with one state to Direct3D as one draw call (`draw_merge=1`,
    `backend.md`, "One draw call for a row of draw calls"). Desktop runs of
    the six scenes show the same triangles and 0.4 to 0.8 ms less in a frame.
    Not run: a headset session, and the check of the triangles in the Temple,
    in a story, in a fight of Creatures and with the effect of a miracle
    (`draw_merge_test=1` in the ini for one session).
  - **The cost of the lock of the Direct3D 9 device.** A device for two
    threads takes a lock on each call. A sampling profile of the frame
    thread had 3.8 percent of its samples in critical sections (`backend.md`,
    "The clock of the game and the hooks of dxwrapper"). The device of
    dxwrapper is a device for two threads in each case: the log has the
    behavior flags 0x84 with `test_keys` 0 and 1, so the request of the mod
    (`test_keys=1` or `frame_send=2`) changes nothing, and `bench_run.ps1`
    gives the same numbers for both values. To do: find whether dxwrapper
    calls the device from a second thread. If it does not, a test that takes
    `D3DCREATE_MULTITHREADED` off at the creation of the device when
    `frame_send` is not 2, and a benchmark of it.
  - **A render thread for the Direct3D calls.** To do. The Direct3D draw
    calls are 3.3 to 4.8 ms of a frame in scenes 1 to 5, and with one walk
    the game makes 1580 to 2610 state calls in a frame. In the sampling profile with one
    walk, the driver, `d3d9.dll` and the calls into the kernel under them
    were about 45 percent of the frame thread, and the code of the game 23
    percent. A thread that gets a record of these calls and gives them to
    Direct3D takes that time off the frame thread. This is the largest gain
    that is left. Not built, and not planned in detail. The hard parts: a texture that the game writes in the
    middle of a scene walk, a state that the game reads back, the draws of
    the mod itself, and the order of each call. With the merge of draw calls
    the thread has less work.
  - **The other render ideas have a small gain.** A projection off the centre
    for each eye: a view of 70 to 98 degrees changes the walk by 4 percent
    (`backend.md`, "A wide view is nearly free"). So a narrower cull saves
    0.4 ms at the most. The gain for the GPU is not measured. The game turn
    off the render thread: a game turn is 0.4 ms in one frame of six, which
    is 0.05 to 0.11 ms as a mean. So the mean does not change. It can only
    take the turns of 4.5 to 9.5 ms out of the frame. A cache that drops a
    state call for the value that the state has: the game makes no such
    call, because it keeps the values itself (`backend.md`, "Draw calls with
    one state"). With one walk, each of its 1580 to 2610 state calls in a
    frame changes a value. The texture filter of the mod adds about 4 calls
    in a frame.
- **N2. The hands that react.** The part for bodies that the hand passes
  is built and off (`hand_feel_bodies`): no picture proves it. Not shown: a
  tree taken with the palm to the side. The idle wave is off
  (`hand_feel_idle`).
- **N3. The hand model.** The game's finger animations are not played (a
  pose is a curl for each finger). The shadow of the Hand on the land has
  the game's fingers. The textures of an edited hand file are not read.
- **N4. The Temple.** Not done: a click on a frame of the Challenge Room,
  a text of the Library, a value of the options box, a drag of a scroll.
  Suspects: the picture of a new save is flat magenta, the waist point is
  8.4 units higher after a load, the ray ends short of the pointer in the
  left eye in a dialog box inside the Temple, and the leash posts are 1.4
  cm from each other at the scale of the start. In the ring menu: with
  `button_layout=0` a thumb on the left stick can open it, and a jump to the
  town or to a bookmark takes the heading of the game camera.
- **N5. The village.** Not run: food, an animal or crops set down at a
  store, the altar, a gift to the Creature, the red halo outside the
  influence, a Workshop that gives scaffolds, villagers that make chants,
  and Heal from its icon at the Worship Site.
- **N6. Miracles.** Not run: teleport, the Creature miracles other than
  Strong and Freeze, and each reason of a refusal but "needs a Creature"
  and "needs a circle on the land".
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
  step of the tutorial waits for, and the load of a saved game in play (a
  run with a test save shows the load at the start of the game). Of the
  teleport, not run: the red arc on the sea, the aim at the sky, the cancel
  with B and the mouse double click (`god-view.md`, 14.9). Some cameras of
  the game still glide: the follow of the Creature after a double click, the
  pull back past 3500 units, the arrow keys and the automatic tilt
  (`backend.md`).
- **N12. The far grab.** No desktop run: a villager in the sea, a Field or
  a FishFarm as a target, a take while the Hand is outside the influence,
  and a second object in the Hand (`grab.md`).
- **N13. The exact Hand.** No desktop run in the PlayAnim state. The Totem
  and the Citadel states are not read (`hand.md`).
- **N14. The throw.** Not tested: a throw into a pile, and whether a thrown
  villager lives (`throw.md`).
- **N15. The interface panel** (`ui_panel=1`). No run, on the desktop or in
  a headset (`ui-layer.md`).
- **N16. A PC with no DirectX 9 runtime.** No test has run the mod on a PC
  that never had the DirectX 9 runtime installed.
- **N17. Only one headset.** No person has run the mod with a headset or a
  runtime other than a Quest 3 through Virtual Desktop.

### The repo, the release and the tools

- **R1. The installer and a person.** No person has run the installer on a
  real game folder and told the result: the install, an update over a
  build that is installed, and the uninstall.
- **R2. No desktop run chooses a Creature at the Creature select.** A
  Creature in a pen has no player. A grip on it starts no Creature state, a
  press of the trigger there starts a drag of the land, and the double click
  of A makes the jump of the game (`bwvr/tools/desk/README.md`, "Known
  limits of the recipes"). Find how the plain game chooses a Creature
  there, and make a recipe for it: no run shows that choice with the
  controllers. The recipes with the Creature state start in the test save
  `temple`.
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
- **R7. The word `aimfar` chooses a different object in the test save
  `pen`.** The head is 1 cm from its place in a new game, and some objects
  are at the least distance of the word. So `aim_queue.ps1 -Runs
  ring1,ring0` and `throw_queue.ps1 -Runs treeset,treelean` show their proof
  only with `-NewGame` (`bwvr/tools/desk/README.md`, "Known limits of the
  recipes"). Give these recipes an aim that does not depend on the least
  distance, or the start of a new game by default.
- **R8. The wait for the lock of a run can stop a caller.** `desk_run.ps1`
  reads the age of the lock after a failed open. When the other run removes
  the lock between those two lines, PowerShell gives the error "Cannot find
  an overload for op_Subtraction". A caller that has the call in a `try`
  block stops there, and no run starts. With no `try` block the wait goes
  on. It happened one time, with two queues of runs that waited for each
  other.
- **R9. No recipe shows a stay that stops a walk.** `creature_recipe.ps1
  -Kind stay` makes three stays of a Creature that stands (`creature.md`,
  6.2). A grip from a distance in the walk of `-Kind tie` can show it.
- **R10. Two test keys on one key.** Ctrl and the 9 key of the number pad is
  the test key of `creature_palm_point` (`hand_direct.cpp`) and of the take
  from a forest (`forest_take.cpp`). The keys of the keyboard that no code
  of the game or of the mod reads are few: a new test key needs a search
  first (`backend.md`, the hotkey table).
