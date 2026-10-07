====================================================================
 Black & White VR Mod
 Version 1.1.0 (alpha 1, for testers)
 By Buck
====================================================================


WHAT IT IS
----------

The Black & White VR Mod puts Black & White (Lionhead Studios, 2001)
into a VR headset. You see the world in real stereo 3D, and your head
turns the view. You play with the mouse and the keyboard, as you
always did.

The mod adds its own files beside the game. It changes no game file,
and it does not touch your saved games.


WHAT YOU NEED
-------------

Your own copy of Black & White, with the unofficial v1.42 patch.
  The patch is the work of the community. You can get it from
  bwfiles.com. After you apply it, the game folder holds the file
  readme-patch-v142.txt. The installer looks at runblack.exe. If the
  patch is not there, the installer stops and tells you.

A VR headset with an OpenXR runtime that registers a 32 bit entry.
  Black & White is a 32 bit program. A runtime that registers itself
  for 64 bit only cannot serve it, and the headset stays dark.
  Virtual Desktop registers a 32 bit entry, and SteamVR does it from
  version 2.17. The launcher names the runtime that it found.

Windows. The mod needs nothing else.


HOW TO START
------------

1. Open the "Black & White VR Mod" shortcut. The launcher opens.
2. Set what you want.
3. Press "Play in VR".

The launcher saves your settings, starts the game in the headset,
and closes. "Play, new game" does the same and starts a new game.
"Save" keeps your settings and does not start the game.

The game shows its logos when it starts. To skip them, press a
trigger or A, B, X or Y on a controller. Esc and a mouse click skip
them too, as before.


THE SETTINGS
------------

The launcher shows five groups.

  Picture         How big a picture each eye gets, how sharp the
                  textures are, and how far out models keep their
                  full detail.

  View            The play mode, and how big the world feels. The
                  default play mode is the God view (see below).

  Comfort         The size of the interface and of the hint beside
                  the Hand, and how far in front of you the
                  interface sits.

  Moving around   How the camera moves when you did not move your
                  head: turns in steps, jumps, the zoom step, the
                  story camera and the blink.

  Testing         One box, which turns on keys for tests. The box
                  is off, and a player does not need it.

Every setting has a tooltip. Hold the pointer over a setting, and
the tooltip tells you what the setting does.

If you are not sure, leave the settings as they are. Each default is
a value that the headset tests settled.


THE GOD VIEW
------------

The God view is the default play mode. The land is at your waist, so
your hands can reach it. It works with motion controllers and with
the mouse. It takes the place of the play mode "Immersive". If your
settings had "Immersive", the God view starts in its place.

The view does not go up or down with the hills. Zoom out to see more
of the island. Zoom in until the world is life size. A cut scene of
the game plays on a screen in front of you, and the God view comes
back after it. When you load a saved game, the God view starts again
at the place of that game.

With motion controllers, the Hand is your own hand, at the size of
your hand, at every zoom. Put your hand down on the land to touch it
or to take a villager.

Your two hands look like the Hand of the game. The mod reads the hand
from your own copy of the game. The fingers follow the trigger and
the grip, and the hands change with your alignment, as the Hand of
the game does. Set hand_model=0 in bwvr.ini for plain hands.

The fingers also react to the world. When you put a hand down on the
land, each finger bends until it rests on it. When your finger tip
comes to a thing that the grip can take, the hand takes the form of
the thing: a claw for a villager, an open hand for a tree. In your
hand the fingers close on the thing, and a tree lies with its trunk
on your palm. Set hand_feel=0 in bwvr.ini for fingers that follow
the trigger and the grip only.

  Stick up, down      Zoom in or out one step. Push again for the
                      next step.
  Stick left, right   Turn one step. You stay where you are.
  Left grip           Grab the land and pull it to move. Pull up to
                      raise the land. Pull down to lower it. The land
                      stops when it is too near or too far below you.
                      Hold both grips, then turn or spread your hands
                      to turn and zoom.
  Y (left hand)       Open the game menu. The left menu button does
                      the same.
  X (left hand)       Hold it for the ring: the Temple, your Creature,
                      the leashes and more. See MOTION CONTROLLERS.
  Trigger             Click on things. A drag with the trigger does
                      not move the land. Use the left grip.
  A                   Hold it and an arc shows where you will go. A
                      ring marks the place. Let go to go there. A
                      red arc means you cannot go there. Press B to
                      cancel.

With the mouse:

  Drag the land       The land moves with the mouse. The point that
                      you took stays under the Hand.
  Double click        Go to that place on the land.
  Wheel               Zoom in or out one step for each click.
  Tilt keys           Move the ground up or down 5 cm for each press.
  Turn keys           Turn one step. The middle mouse button does the
                      same.

The tutorial of the game turns on the camera controls one at a time.
Until the tutorial turns on a control, that control does nothing in
the God view.

Ctrl F11 turns the God view off and on. Off is the old immersive
view. It is for tests only.


THE SOUND
---------

Use headphones. The mod plays the sounds of the world for two ears.
A sound comes from its place, also from below and from above, and the
sea stays where it is when you turn. A sound far away is dull and
quiet. When you zoom out, the small sounds go away and the loud ones
still carry.

A turn step, a zoom step, a teleport, a grip on the land, and a lock
and an arrival of the grab from a distance have a soft sound. Set
audio_cues=0 in bwvr.ini to turn these off, or audio_cues_pct to
change their level (100 is the level as made).

The advisors and the speech of a story sound as before. Set
audio_mixer=0 in bwvr.ini for the sound of the world as it was.


THE KEYS
--------

  F11   The direction your head faces becomes straight ahead. Press
        it when you sit down, or when the world is not where you
        expect it. This key always works.

The game keys and the mouse work as they always did. In the God
view, the tilt keys move the ground (see THE GOD VIEW).

With "Test keys" on, F12 saves a picture of each eye in the game
folder, Shift F12 moves the subtitles down one step, and Ctrl F12 sets
who points: the mouse, the controllers, or both. Ctrl Shift F12 steps
the sound back toward the sound of the game, one change for each
press, and then to the start again. Shift F11 sets the grab of the
world: as set, one to one, or off. Ctrl Shift F11 turns the interface
panel on and off, when it is set up. Press these keys with the mouse
still.


MOTION CONTROLLERS
------------------

The mod reads tracked controllers, for example the Quest Touch
controllers. The mouse keeps working all the time: the one you move
or press last is the one that points.

The right hand points. In the launcher, "Controllers" and "Hand" set
how it points:

  Direct hand   The Hand is where your hand is, and the size of your
                hand. This is the default.
  Pointer       The controller points at the land, like a mouse.

  Trigger             The left mouse button: drag the land, and click
                      on things.
  Trigger, leash on   Point at an object or at the land and press the
                      trigger: your Creature goes there. Hold the
                      trigger to aim, then let go. Press it two times
                      on an object to tie the leash to the object.
  Grip                Close it on an object to take it. Keep it
                      closed to carry. Open it to let go. Swing and
                      open it to throw. On a tree, keep it closed and
                      pull up.
  Grip, scroll        Put the glow of your Hand on a scroll and close
                      the grip: the scroll starts. The same press
                      knocks on a house.
  Grip, forest        Put your Hand in a forest and keep the grip
                      closed. After a moment a tree is in your Hand.
  Grip, store         On the food or wood of a store, on a field or on
                      a fish farm: keep the grip closed to collect.
                      Open it to stop. The bundle stays in your Hand.
                      Close and open the grip again to put the bundle
                      down.
  Grip, far object    Point at an object that is out of reach. Close
                      the grip, then pull your hand toward your head.
                      The object comes to your Hand. On a far pile of
                      a store or a far field, the closed grip collects
                      with no pull. On a far scroll, the closed grip
                      starts the scroll.
  Stick up, down      Zoom in or out one step.
  Stick left, right   Turn one step.
  A                   Hold it and an arc shows where you will go.
                      Let go to go there. B cancels.
  B                   Hold it and draw a gesture in the air to cast a
                      miracle. With a miracle in your Hand, a short
                      press of B cancels the miracle.
  Left grip           Grab the world and pull it to move. Hold both
                      grips, then turn or spread your hands to turn
                      and zoom. The world stops when you let go. In
                      the God view, pull up or down to move the land
                      up or down.
  Y (left hand)       Open the game menu. The left menu button does
                      the same. With the left hand as the pointing
                      hand, B does it.
  X (left hand)       Hold it, and a ring of items opens at your left
                      hand. Move your hand toward an item, then let go
                      of X. Let go with your hand in the middle to
                      close the ring with no choice. With the left
                      hand as the pointing hand, A opens the ring at
                      your right hand.
  Left stick          Push it toward an item of the same ring, then
                      pull the left trigger to choose. Let the stick
                      rest in the middle to close the ring.

While a menu or a dialog box is open, the Hand steps aside. The
pointing controller then points like a mouse: a thin line shows where
it points, and the trigger clicks. Close the game menu with "Continue
Game". The Hand comes back when the menu closes.

The ring holds what the plain game has only on the keyboard. An item
acts as its key does in the game. The first page has: go to the
Temple, go to your Creature, go to the next town, the Leash of
Learning, the Leash of Compassion, the Leash of Aggression, go into
the Temple, and More. Keep your hand on More for a moment to get the
second page: set a bookmark at your Hand, go to the next bookmark, go
back, pause, the help of the game, the villager names and quick save.
In the Temple the ring has the rooms and the way out. An item that
cannot act now is dim, and the ring says why. The ring stays where it
opened. It does not follow your hand. A grip does not move the land
while the ring is open. The ring does not open in a cut scene or
while a dialog box is open.

"Go to the Temple" is the Temple key of the game. Choose it again at
the Temple to go back to the place where you were. In the God view,
"Go to the Creature" is one teleport to your Creature. Set
radial_menu_creature=0 in bwvr.ini for the camera of the game that
follows the Creature.

In a room of the Temple, point your hand at a door, a picture or a
pad. A line shows where you point. Pull the trigger or close the grip
to click. The room does not turn when you point: turn your head to
look about, or push the stick to the left or to the right to turn in
steps. The ring of the Temple goes to each room too, and it has the
way out. Set temple_pointer=0 in bwvr.ini for the pointer of before,
which was the place of your hand in the view.

To save in the Save Game Room, click an empty picture two times and
then click OK. The save has no message, because the text line needs
a keyboard. To load, click the picture of the save and then Yes.

At the Temple on the land, a short press of the grip on the door goes
into the Temple, and a short press on one of the three leash posts
chooses that leash.

To change the items, set radial_menu_items, radial_menu_items2 and
radial_menu_items_temple in bwvr.ini: 8 words with commas, from the
top and clockwise. The words are temple, enter, creature, town, land,
leash_learning, leash_compassion, leash_aggression, leash_next,
leash_previous, mark_set, mark_go, mark_back, pause, slower, faster,
help, names, details, save, load, playground, hall, room_creature,
room_challenge, room_save, room_options, room_library, leave, more
and none. Set radial_menu_stick=0 for the ring with X only,
radial_menu_button=0 for the ring with the stick only, or
radial_menu=0 for no ring. Set radial_menu_reach_cm to change how far
your hand moves to pick an item (5), and radial_menu_size_pct for the
size of the ring (100). With "Test keys" on in the launcher, Ctrl with
the 1 key of the number pad goes through the two ways while you play.
The ring is new in this version and was not tested in a headset.

The line of the pointing hand is not the ray of the runtime. For a
Touch controller the mod turns that ray 7 degrees toward your other
hand and 6 degrees down, as the menu pointer of Half-Life: Alyx does.
If the line does not point where you expect, set aim_pitch_deg and
aim_yaw_deg in bwvr.ini, in degrees. A positive pitch turns the line
up. A positive yaw turns it toward your other hand. Set aim_preset=1
for the pointer of Meta's own apps (6.5 degrees toward your other hand
and 0.8 up), or aim_preset=0 for the ray of the runtime. With "Test
keys" on in the launcher and Num Lock on, the keys 8, 2, 4 and 6 of the
number pad turn the line 1 degree up, down, to the left and to the
right while you play. The key 5 goes to the next preset. bwvr.log gives
the values after each press.

While a controller points, the hints of the game show on a small card
beside the wrist of your pointing hand, and not in the view. The card
names the controller button: Trigger, Grip or Stick. A card is one
picture in the room, so the hint is never double, and your Hand cannot
hide it. A hint that is not true with the controllers does not show.
For example, in the God view the trigger does not move the land, so the
card does not say "Trigger Move". Set hint_card=0 in bwvr.ini to draw
the hints in the view, as before. Set controller_hints=0 to keep the
mouse hints.

Each controller has its own card. A line for a button of the other
controller shows beside the wrist of that hand: "Grip Move the land",
"X Ring menu" and "Y Game menu" show at your left hand. The two cards
can show at the same time.

The cards also show prompts: what a button does now. Examples are "Grip
Take" over an object, "Grip Pull to fetch" for a far object, "Grip Hold
to collect" at a store, "Grip Stroke or slap" on your Creature,
"Trigger Send the Creature" with the leash on, and "A Teleport" over
the land. A prompt shows for a few seconds, and only the first times.
Set prompts=2 in bwvr.ini to show the prompts always, or prompts=0 for
none. Set hint_card_size_pct=150 for larger cards.

The game shows its hints, the amount that you take and the subtitles
only when its own help options are on. If you see none of them, open
the game options, then "Help Options". Set "Intelligent ToolTips",
"All Text" and "Medium Help" or more.

In the headset, the subtitles and the help text of the game show on one
card that is fixed in your room, and not in the view. The card is
straight ahead of the place where you sat when you pressed F11, a
little below eye level and 1.6 m away. It does not follow your head, so
the text is never double. If you turn far away from it for more than
one second, it fades out and comes back ahead of you. Set
subtitle_card=0 in bwvr.ini to draw the subtitles in the view, as
before. Set subtitle_card_distance_cm, subtitle_card_down_deg and
subtitle_card_width_cm in bwvr.ini to change the place and the size of
the text. The defaults are 160, 17 and 100.

With the direct hand, the Hand acts from one point: the tip of your
index finger. Put that point on an object, or at most 4 cm from it. A
gold halo then shows around the object, and a glow shows at your
finger tip. Close the grip, and the Hand acts on the object with the
halo, and on no other object. With no halo and no glow, a press of the
grip does nothing. An object at your palm or at your wrist is not
taken. Set hand_pick_cm in bwvr.ini to change the distance.

When two objects are near the point, the object that the point
touches wins, and of two such objects the smaller one. A scroll wins
over a house, a tree or a forest that the point is in. A red halo
shows an object outside your influence.

A scroll is a target from 10 cm. At more than 4 cm it is the target
only when no other object is in reach. The scroll of a challenge
starts at the first press of the grip on it. Set scroll_reach_cm in
bwvr.ini to change the 10 cm. Set scroll_click=0 and scroll_draw=0
for the scroll of the versions before.

In the God view the quests of the game see where you are. When a
quest waits for you to come near a thing, your head or a hand must be
near it on the land. When a quest waits for you to look at a thing,
your head must face it. In a cut scene the quest sees the camera of
the cut scene, as in the game. Set script_distance=0 and script_view=0
in bwvr.ini for the versions before, where a quest saw only the camera
of the game, far behind you. With "Test keys" on in the launcher, Ctrl
with the 7 key of the number pad goes between the two while you play.
The controller gives a long pulse for the new rule and a short pulse
for the old one.

The color of the glow is the color of the grip on the hint cards.
It is gold when the grip can pick something up or take from a store
or a field. It is a paler gold when the grip can start a scroll,
knock on a house or touch your Creature. There is no glow when a
press does nothing, and none while the Hand holds an object. To turn
the glow off, clear "Glow at the Hand's point" in the launcher. Set
hand_glow_idle=1 in bwvr.ini for a glow that shows always.

To take an object that is out of reach, point your hand at it. A gold
halo shows around the object, and the controller gives a short tick.
Close the grip and a thin line shows from your Hand to the object. Then
pull your hand toward your head. The object glides to your Hand, and
the grip carries it. Open the grip before you pull to cancel. The halo
is red when the object is outside your influence, and then the Hand
cannot take it. A hill in front of an object hides it, so you cannot
pick it. The sea does not hide an object. An object that rolls, flies
or floats on the sea is a target too: the line follows it, and the pull
brings it from the place where it is then. This works in the God view
and in the Tabletop play mode, and not in a multiplayer game. Set
far_grab=0 in bwvr.ini to turn it off.

A far object must be near you: at most 2 m of your room from your
head, along the floor. Set far_grab_max_m in bwvr.ini to change this
distance. The value 0 gives no limit.

A pile of food or wood of a store, a field and a fish farm are far
targets too. Point at one and keep the grip closed to collect from a
distance. Open the grip to stop.

A forest is a far target too. Point at it, close the grip and pull:
one tree comes to your Hand. You can also put your Hand in the forest
and keep the grip closed. The game gives one tree for each press.

The mod can also take many trees with one grip. This is off, because
the plain game gives one tree for each grip. To turn it on, set
forest_take=1 in bwvr.ini. Then keep the grip closed on a tree, with a
second tree 10 cm or less from the tip of your index finger. After a
moment the two trees become one bundle of wood in your Hand, and one
more tree goes in at a time. The card shows the amount of wood. Open
the grip to stop. The bundle stays in your Hand. Close and open the
grip again to put the bundle down.

A scroll is a far target too. Point at it and close the grip, with no
pull. The scroll starts, as when you close the grip with the glow on
it. Set far_touch=0 in bwvr.ini to turn this off.

Near objects win. While your Hand is 10 cm or less from an object
that a press acts on, there is no far target and no halo, and the grip
of the pointing hand does not grab the world. So the grip acts only on
the object at your Hand. The far targets come back when your Hand is
more than 15 cm from each such object. Set
far_grab_near_cm and far_grab_near_out_cm in bwvr.ini to change the
two distances. With "Test keys" on in the launcher, the point key (.)
of the number pad goes between this rule and the old pick while you
play. The controller gives a long pulse for the new rule and a short
pulse for the old pick.

The rope of the leash ends in the palm of your hand. Set
leash_at_palm=0 in bwvr.ini to end it at the wrist of the game's
Hand, as before.

The Hand lets go when you open the grip. The grip opens when your
hand starts to open fast, or when it is almost open. If the Hand lets
go when you do not want it to, set grip_fast_open=0 in bwvr.ini. The
game then drops the object at once, and does not wait for its next
game turn. Set release_at_once=0 in bwvr.ini for the old wait. With
"Test keys" on in the launcher, the star key (*) of the number pad
goes between the old release and the new one while you play. The
controller gives a long pulse for the new release and a short pulse
for the old one.

In the God view and in the Tabletop play mode, an object that you
throw flies as a real object flies in your room: as fast as your hand
moved, on the same arc, for the same time. Swing your hand and open
the grip in the swing. A hard throw can kill a villager, as in the
game. Open the grip with a still hand to put an object down. A tree
and a miracle fly as the game makes them fly. Set throw_law=1 in
bwvr.ini for the slower flight of the game's physics, or throw_law=0
for the old throw.

After a thrown object touches the land, it slows to the pace of the
game, bounces low and comes to rest soon. Set throw_land=0 in bwvr.ini
if you want the landing of the game's physics at the speed of the
flight.

To cast a miracle, hold B and draw its gesture in the air: the same
shape that you draw with the mouse. Draw it in front of you, as you
see it, about 30 cm across. Moves of the Hand without B do not draw.

A miracle from a reward or a dispenser is a bubble on the land. Close
the grip on the bubble to carry it. Then press the trigger to use it.
A short press of the grip on the bubble uses it at once. The card on
your hand shows these controls.

A miracle in your Hand is at your real hand. The card on your hand
says what the grip does with it:
  A Creature miracle  Touch a Creature with your Hand, or point at
                      it. A gold halo shows around it. Then close the
                      grip.
  Heal, forest        Put your Hand over the place and close the grip.
  Food, wood, water   Keep the grip closed over the place to pour.
                      Open the grip to stop.
  A fireball          Close the grip, swing your hand and open the
                      grip in the swing.
  A shield, a storm   Close the grip, draw a circle with your hand
                      and open the grip.
When the game refuses a cast, the card says why.

To cancel a miracle, shake your hand or press B for a short time. As
in the game, a miracle from a bubble is then lost, and a miracle from
a Worship Site gives its power back. Set miracle_hand=0 in bwvr.ini
for the miracle of the versions before. With "Test keys" on in the
launcher, Ctrl with the 8 key of the number pad goes between the two
while you play.

To stroke or slap your Creature, put your hand on it and close the
grip. The view does not move, and your hand stays open: the index
finger for a small Creature, the flat hand for a large one. The flat
hand shows when the Creature is three quarters as tall as your hand
is long, or more. The hand opens half a second after you close the
grip. Move your hand slowly over the Creature to stroke it. Hit its
body with a fast move, about 1 m/s, to slap it. A swing of the arm
and a flick of the wrist count the same, forehand and backhand. Set
creature_slap_mps in bwvr.ini to change the speed. Set
creature_stroke_hand=3 in bwvr.ini for the stroke of the build
before: a fist, and a slap of the arm at 1.5 m/s. Set
creature_finger_below_pct and creature_flat_above_pct (60 and 90) to
change the size at which the hand changes, in percent of the length
of your hand. With the flat hand, the glow and the touch are at your
palm. Set creature_palm_point=0 in bwvr.ini to keep them at the tip of
your index finger.
The glow is pale gold while your hand touches the Creature. Open the
grip to stop. A short press of the grip on your Creature changes its
leash, as a short click with the mouse does. When the interface panel
is set up, the panel of the Creature shows near the Creature.

To change the leash from a distance, point your hand at your Creature
and close the grip. The card on your hand says what the grip does: put
the leash on, take it off, or take it back from an object. This does
not start a stroke. For a stroke or a slap, put your hand on the
Creature. Set far_leash=0 in bwvr.ini to turn this off.

While the leash is on your Creature, the trigger commands it. Point
your hand at an object: a green halo shows around it. Press the
trigger, and your Creature goes to the object and acts on it. An object
that your Hand touches is a target too: put the glow on it and press
the trigger. A far object outside your influence has a red halo with no
leash, because the Hand cannot take it. With the leash on, its halo is
green: the trigger can send your Creature to it, on the land and in the
sea. To send your Creature to a place, point at the land with no halo
in view and press the trigger. A quick press sends your Creature to the
place that you pointed at when you pressed. A green ring shows that
place for a moment. The sea is not a place for this: the ring is red
there. The grip does not send the Creature. To stroke or slap, put your
hand on the Creature as before. Set creature_command=0 in bwvr.ini for
the old controls.

To aim with care, hold the trigger. The target follows your hand while
you hold it. When you point at an object, its halo shows. When you
point at no object, an arc and a green ring show a point on the land.
Let go, and your Creature goes to what shows then: the object with the
halo, or the place of the ring. So if an object has the halo and you
want the land, keep the trigger down and point away from the object.

An object takes the aim of the trigger only when you point at it:
within 3 degrees, at most 4 m from your head, and not when you point at
the land in front of it. If objects take the aim too often or not often
enough, set creature_command_cone_deg and creature_command_max_m in
bwvr.ini. Set creature_command_rule=1 for the rule of the versions
before: the press chose its target, and an object took the aim from far
away. With "Test keys" on in the launcher, Ctrl with the 5 key of the
number pad goes between the two rules while you play. The controller
gives a long pulse for the new rule and a short pulse for the old one.

To tie the leash to an object, point at the object and press the
trigger two times, one soon after the other. The first press sends your
Creature to the object, and the second press ties the leash to it. The
hint card says "Trigger x2 Tie the leash" when the leash can go on the
object. While the leash is tied, the trigger sends no command. To take
the leash back, point at that object and press the trigger two times.
You can also point at your Creature and press the trigger one time, or
close the grip. Set creature_command_double_ms=0 in bwvr.ini to turn
the double press off. The default is 500, the most time between the two
presses in ms.

To take the leash off, shake your hand: four fast moves from side to
side, in about half a second. The controller gives a pulse when the
leash comes off. A short press of the trigger with your hand
pointed at your Creature takes the leash off too. If the leash comes
off when you do not want it to, set leash_shake_mps=1.0 in bwvr.ini.
The default is 0.6, the speed of each move in meters a second. The
value 0 turns the shake off. These controls are new and were not
tested in a headset before this version.

The controllers give short pulses for what you do. A pulse comes for
a new target, a lock on a far object, and a fetched object that
arrives. A pulse also comes for a pick up, a release, and a touch, a
stroke or a slap of your Creature. A change of the leash, a command,
the teleport and a gesture that the game knows give one too. A hand
that is still gives no pulse. Set haptics_pct in bwvr.ini to change
the strength: 100 is the default, 50 is half, and 0 turns the pulses
off. Motion controllers are new in this version.

In the play mode "Tabletop (test)", the island is a model on a
table. The Hand is where your hand is on the model, with no long
reach. The left grip moves the land on the table: pull to slide it.
Hold both grips, then turn your hands to turn the land, or spread
them to zoom in. The table stays where it is, and the land stops
when you let go.

For the controls of the God view, the default play mode, see THE GOD
VIEW above.


KNOWN ISSUES
------------

This is an alpha build for testers. Expect faults. Save the game
before you start a quest. These faults are known:

  - In the quest of the sailors, do not drop a whole tree on the
    boat. The quest can stop then, and the game cannot go on.
  - A change of the zoom moves you forward or back a little.
  - The aim at a distance can select a tree when you want a
    villager or a different object near it.
  - With the leash on, a held trigger near trees can select a
    tree when you want the Creature to walk to the land.
  - An advisor can be very large for a short time.
  - The motion controllers are tested with a Quest 3 through
    Virtual Desktop only.

Send bwvr.log and the bwvr-old files with each report. See SUPPORT.

The island is below you. The mod holds the horizon straight ahead
and level, whatever the camera of the game does. This keeps the
world still when you tilt your head. The cost is that you look down
at the land, as you would from a real place above an island.

The interface stays in front of your view. Menus, subtitles, help
text and tooltips follow your head. They do not hang in the world.

Smoke and other particles shimmer. The game asks for new random
numbers for each eye, so each eye gets a different jitter.

The clouds and the sun move a little as you move your head.

A hill in the distance can look different in each eye. The game
picks how much detail an island gets from its distance. An island at
the edge of a step can get a different shape in the two eyes, and
the two pictures then do not join.

The frame rate drops in busy scenes. If it gets too low, lower the
resolution scale, or bring the model detail distance in.

A cut scene plays on a screen. While the game shows its black bars,
the view closes to a screen 2 m in front of you, in the direction
that you look when the cut scene starts. The game moves its camera
there as it does on a monitor. The screen is a window: the scene is
behind it, and the subtitles are below the scene. The view opens
again when the cut scene ends. The screen does not follow your head.
If you do not see it, press F11, and it comes in front of you. Y on
the left controller is still Esc. The Hand does not follow your
hands until the view is open again. To see cut scenes in the full
view, as in earlier versions, set cutscene_window=0 in bwvr.ini.

A story camera with no black bars can put a shot in an odd place.
The game can also move the camera for you with no bars, for example
to follow the Creature. There the mod cuts to each shot instead of
gliding to it, because a glide that fills the view is uncomfortable.
The shot itself is still the shot that the game chose, and some
shots sit too near or point away from the action.


SUPPORT
-------

The mod writes a log, bwvr.log, beside runblack.exe in the game
folder. It starts a new log each time the game runs, and it keeps
the logs of the 20 runs before as bwvr-old-<date>-<time>.log. Send
the log of the run that had the problem. The log holds:

  - the settings that the mod read,
  - the hooks that went in, and the hooks that failed,
  - the frame rate reports, and a count of the frames.


HOW TO REMOVE IT
----------------

Open Windows Settings, then Apps. Find "Black & White VR Mod" in the
list, and remove it.

The uninstaller takes away only the files that the mod put in the
game folder, and the log. If the game folder held a ddraw.dll from
another program before you installed the mod, the installer kept
that file, and the uninstaller puts it back. Your saved games, your
creature and the game files are never touched.


THANK YOU
---------

Thank you for playing. If you like the mod, you can support the work
here:

  https://ko-fi.com/buckzor


LEGAL
-----

This is an unofficial mod, made by a fan. Electronic Arts and
Lionhead Studios did not make it, do not endorse it, and have no
connection with it. Black & White is a trademark of its owner. The
mod contains no game files. You must have your own copy of the game.

Copyright (C) 2026 Buck.
