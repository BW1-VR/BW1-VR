====================================================================
 Black & White VR Mod
 Version 1.1.0 (alpha, for testers)
 By Buck
====================================================================


WHAT IT IS
----------

The Black & White VR Mod puts Black & White (Lionhead Studios, 2001)
into a VR headset. You see the world in real stereo 3D, and your head
turns the view. You play with motion controllers, or with the mouse
and the keyboard.

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


HOW TO INSTALL
--------------

1. Run the installer.
2. If Windows warns about a file with no signature, select "More
   info", then "Run anyway".
3. If the installer does not find your game folder, select it.
4. To update, run a newer installer over the old one. You do not need
   to remove the old one first.


HOW TO START
------------

1. Start your VR software and connect the headset.
2. Open the "Black & White VR Mod" shortcut. The launcher opens.
3. Set what you want. See THE SETTINGS.
4. Press "Play in VR".

The launcher saves your settings, starts the game in the headset,
and closes. "Play, new game" does the same and starts a new game.
"Save" keeps your settings and does not start the game.

"Play, no VR" starts the game on the monitor, without the headset.
The game is full screen, at the size of your desktop, and the mod
does nothing in it. The launcher closes its window and waits for the
end of that game. Then it sets the game for the headset again.

The game shows its logos when it starts. To skip them, press a
trigger or A, B, X or Y on a controller. Esc and a mouse click skip
them too.

The setting "Intro in stereo" is off at first. With it, the Lionhead
logo is in 3D around you. Your view goes between the orbs on the path
of the game, and then back to the place from which the logo shows.
The view does not turn: you look where your head looks. The menu
button skips the logo. This setting is a test. If the motion is not
comfortable, set intro_motion=2 in bwvr.ini: the orbs are then a model
in front of you, and the room stays still. intro_motion=0 gives a
still view of the logo.


THE SETTINGS
------------

The launcher shows five groups.

  Picture         How big a picture each eye gets, a margin around
                  the view, how sharp the textures are, and how far
                  out models keep their full detail.

  View            The play mode and the intro in stereo. The default
                  play mode is the God view (see below).

  Moving around   Turns in steps, the zoom with the stick, the walk
                  with the left stick, and a blink when you jump or
                  turn.

  Controllers     The glow at the Hand, and the grab of the world.

  Testing         One box, which turns on keys for tests. The box
                  is off, and a player does not need it.

Every setting has a tooltip. Hold the pointer over a setting, and
the tooltip tells you what the setting does.

If you are not sure, leave the settings as they are. Each default is
a tested value.

Some settings are not in the launcher. You can set them in the file
bwvr.ini in the game folder, in the section [bwvr]. The text of this
readme names each one as key=value. The launcher writes bwvr.ini
when you save.


THE GOD VIEW
------------

The God view is the default play mode. The land is at your waist, so
your hands can reach it. It works with motion controllers and with
the mouse.

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
on your palm. A small thing, for example a rock or a mushroom when you
are zoomed out, is held between your thumb and your index finger. Set
hand_pinch=0 in bwvr.ini to hold it in your fist. Set hand_feel=0 for
fingers that follow the trigger and the grip only.

  Stick up, down      Zoom in or out one step. Push again for the
                      next step. The land that you look at stays where
                      it is, and its size changes. In the launcher,
                      "Center of the zoom" chooses a different point
                      (key god_zoom_about), and "Zoom with the stick"
                      gives a zoom while you hold the stick (key
                      god_zoom_smooth).
  Stick left, right   Turn one step. You stay where you are.
  Left grip           Grab the land and pull it to move. Pull up to
                      raise the land. Pull down to lower it. The land
                      stops when it is too near or too far below you.
                      Hold both grips, then turn or spread your hands
                      to turn and zoom.
  Left stick          Push it to move over the land. You go where
                      your head faces, and you stop when you let the
                      stick go. In the launcher, "Move with the left
                      stick" makes forward the direction of the left
                      controller, or turns the walk off (key
                      move_stick). A walk with a stick makes some
                      people ill. If it makes you ill, turn it off,
                      and move with the left grip and with A.
  Left menu button    Open the game menu.
  Y (left hand)       Hold it for the ring: the Temple, your Creature,
                      the leashes and more. See MOTION CONTROLLERS.
  Trigger             Click on things. A drag with the trigger does
                      not move the land. Use the left grip.
  A                   Hold it and an arc shows where you will go. A
                      ring marks the place. The ring has one size in
                      the world: it is larger when you zoom in, and
                      smaller when you zoom out. Let go to go there.
                      A red arc means you cannot go there. Press B
                      to cancel.

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

Ctrl F11 turns the God view off and on. Off gives the immersive
view, which is for tests only.


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

The advisors and the speech of a story sound as the game made them.
Set audio_mixer=0 in bwvr.ini to let the sound system of the game
(QMixer) pan each sound, with no mixing by the mod.

While your Hand collects food or wood, the game plays a sound that
becomes higher as the take becomes faster. The mod starts that sound
by the distance from your head. Set audio_take_loop=0 in bwvr.ini to
let the camera of the game decide: in the God view the take then has
no sound.

When a sound stops, the sound system of the game holds the picture
for 20 ms. The mod skips that wait for each sound that it plays, so
the picture does not stop. Set audio_stop_wait=1 in bwvr.ini to keep
the wait of the game.


THE KEYS
--------

  F11   The direction your head faces becomes straight ahead. Press
        it when you sit down, or when the world is not where you
        expect it. This key always works.

The game keys and the mouse work as they always do. In the God
view, the tilt keys move the ground (see THE GOD VIEW).

With "Test keys" on, these keys work. Press them with the mouse
still:

  F12                 Save a picture of each eye in the game folder.
  Shift F12           Move the subtitles down one step. After the
                      lowest step they go back up.
  Ctrl F12            Set who points: the mouse, the controllers, or
                      both.
  Ctrl Shift F12      Step the sound back toward the sound of the
                      game, one change for each press, and then to
                      the start again.
  Shift F11           Set the grab of the world: as set, one to one,
                      or off.
  Ctrl Shift F11      Turn the interface panel on and off, when it is
                      set up.
  Ctrl Menu key       Change the layout of the left controller: the
                      menu button for the game menu and Y for the
                      ring, or Y for the game menu and X and the
                      stick for the ring (button_layout). The Menu
                      key is the key beside the right Ctrl key.
  Shift Menu key      Change the walk with the left stick: where your
                      head faces, where the controller points, or off
                      (move_stick).
  Number pad keys     Switch between two ways of one behavior, while
                      you play. See TEST KEYS OF THE NUMBER PAD.


MOTION CONTROLLERS
------------------

The mod reads tracked controllers, for example the Quest Touch
controllers. The mouse keeps working all the time: the one you move
or press last is the one that points.

The right hand points. The Hand is where your hand is, and it has the
size of your hand.

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
  Grip, far Creature  Point at your Creature from a distance. A short
                      grip changes its leash. Keep the grip closed,
                      and your Creature stops and stays until you
                      open the grip.
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
  Left stick          In the God view, push it to move over the land.
                      Forward is where your head faces. You stop when
                      you let the stick go.
  Left menu button    Open the game menu. With the left hand as the
                      pointing hand, the same button does it.
  Y (left hand)       Hold it, and a ring of items opens at your left
                      hand. Move your hand toward an item, then let go
                      of Y. Let go with your hand in the middle to
                      close the ring with no choice. With the left
                      hand as the pointing hand, B opens the ring at
                      your right hand.
  X (left hand)       No job.

To change the grab of the world, set "Grab the world" in the
launcher (key grab_hand). 1 uses the grip of your other hand. 2 also
uses the grip of the pointing hand when the Hand is empty and not
over an object. 0 turns the grab off.

To change the walk with the left stick, set "Move with the left
stick" in the launcher (key move_stick). 1 moves you where your head
faces. 2 moves you where the left controller points. 0 turns the walk
off. A full push moves you 150 cm of your room each second, at each
zoom (key move_stick_speed_cm in bwvr.ini). The stick does not move
you while a grip holds the land, in a menu, in a cut scene and in the
Temple.

Set button_layout=0 in bwvr.ini for a different layout of the left
controller: Y opens the game menu, X opens the ring, and the left
stick points at an item of the ring (the left trigger then chooses).
The left stick does not move you with that layout.

While a menu or a dialog box is open, the Hand steps aside. The
pointing controller then points like a mouse: a thin line shows where
it points, and the trigger clicks. Close the game menu with "Continue
Game". The Hand comes back when the menu closes.


THE RING
--------

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

To change the items, set radial_menu_items, radial_menu_items2 and
radial_menu_items_temple in bwvr.ini: 8 words with commas, from the
top and clockwise. The words are temple, enter, creature, town, land,
leash_learning, leash_compassion, leash_aggression, leash_next,
leash_previous, mark_set, mark_go, mark_back, pause, slower, faster,
help, names, details, save, load, playground, hall, room_creature,
room_challenge, room_save, room_options, room_library, leave, more
and none. Set radial_menu=0 for no ring. With button_layout=0 the
ring opens with X and with the left stick: set radial_menu_stick=0
for the ring with X only, or radial_menu_button=0 for the ring with
the stick only. Set radial_menu_reach_cm to change how far your hand
moves to pick an item (5), and radial_menu_size_pct for the size of
the ring (100).


THE TEMPLE
----------

In a room of the Temple, point your hand at a door, a picture or a
pad. A line shows where you point. Pull the trigger or close the grip
to click. The room does not turn when you point: turn your head to
look about, or push the stick to the left or to the right to turn in
steps. The ring of the Temple goes to each room too, and it has the
way out. Set temple_pointer=0 in bwvr.ini to point with the place of
your hand in the view.

When you go into the Temple or into a room, you are at your place in
the room at once. The game makes a flight of its camera to that
place, and the mod ends the flight in one cut, because the flight
turns the room about you. Set temple_camera_flight=1 in bwvr.ini for
the flight of the game.

Your head gives the view in a room, as on the land: turn it, tilt it
and lean. The camera of the game gives only your place and your
heading in the room, so a look down at the pool of the hall does not
move you. When you go into the Temple or into a room, the room comes
out of black in both eyes. Each of these three rules has a key in
bwvr.ini, and the value 0 turns the rule off: temple_view,
temple_camera_screen and temple_fade_eyes.

To save in the Save Game Room, click an empty picture two times and
then click OK. The save has no message, because the text line needs
a keyboard. To load, click the picture of the save and then Yes.

At the Temple on the land, a short press of the grip on the door goes
into the Temple, and a short press on one of the three leash posts
chooses that leash.


THE POINTING LINE
-----------------

The line of the pointing hand is not the ray of the runtime. For a
Touch controller the mod turns that ray 7 degrees toward your other
hand and 6 degrees down, as the menu pointer of Half-Life: Alyx does.
If the line does not point where you expect, set aim_pitch_deg and
aim_yaw_deg in bwvr.ini, in degrees. A positive pitch turns the line
up. A positive yaw turns it toward your other hand. Set aim_preset=1
for the pointer of Meta's own apps (6.5 degrees toward your other hand
and 0.8 up), or aim_preset=0 for the ray of the runtime. bwvr.log
gives the values after each change.


HINTS AND SUBTITLES
-------------------

While a controller points, the hints of the game show on a small card
beside the wrist of your pointing hand, and not in the view. The card
names the controller button: Trigger, Grip or Stick. A card is one
picture in the room, so the hint is never double, and your Hand cannot
hide it. A hint that is not true with the controllers does not show.
For example, in the God view the trigger does not move the land, so the
card does not say "Trigger Move". Set hint_card=0 in bwvr.ini to draw
the hints in the view. Set controller_hints=0 to keep the mouse hints.

Some lines of the game wait for a click. The text stays, and the card
says "Trigger Continue" as its first line. Press the trigger. Until
you do, the quest that said the line waits, and a new quest cannot
start its story. While the card says this, the trigger is that click
only: it sends no Creature command. An example is in the quest of the
sailors. They take a pile of wood, for example from the store. A whole
tree at their boat gives such a line. Set text_click=0 in bwvr.ini to
give this hint the rule of the other trigger hints.

Each controller has its own card. A line for a button of the other
controller shows beside the wrist of that hand: "Grip Move the land",
"Y Ring menu" and "Menu Game menu" show at your left hand. The two
cards can show at the same time.

The cards also show prompts: what a button does now. Examples are "Grip
Take" over an object, "Grip Pull to fetch" for a far object, "Grip Hold
to collect" at a store, "Grip Stroke or slap" on your Creature,
"Trigger Send the Creature" with the leash on, and "A Teleport" over
the land. A prompt for the thing that your hand or your aim is on shows
at once and stays while you are on it. The other prompts show for a few
seconds, and only the first times. Set prompts=2 in bwvr.ini to show
those always, or prompts=0 for none. Set hint_card_size_pct=150 for larger cards.

With a tree in your Hand, the card says "Grip Plant" only when opening
the grip plants the tree. If your Hand is more than 15 cm over the land,
or the tree leans, or the land is not dry, it says "Lower to plant. Open
to drop" (or a line for the other cause), and the tree falls. Set
tree_plant_hint=0 in bwvr.ini for the text of the game. A tree that comes
from a distance leaves its roots pile at the roots, on the land. Set
far_roots_at_tree=0 to leave the pile where the game puts it. A tree that
you drop or throw turns down fast when it lies on the land, so it comes to
rest in about 1 s. Set throw_tree_settle=0 in bwvr.ini for the turn of the
game.

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
subtitle_card=0 in bwvr.ini to draw the subtitles in the view. Set
subtitle_card_distance_cm, subtitle_card_down_deg and
subtitle_card_width_cm in bwvr.ini to change the place and the size of
the text. The defaults are 160, 17 and 100.


THE HAND AND THE OBJECTS
------------------------

With the direct hand, the Hand acts from one point: the tip of your
index finger. Put that point on an object, or at most 4 cm from it. A
gold halo then shows around the object, and a glow shows at your
finger tip. Close the grip, and the Hand acts on the object with the
halo, and on no other object. With no halo and no glow, a press of the
grip does nothing. An object at your palm or at your wrist is not
taken. Set hand_pick_cm in bwvr.ini to change the distance.

An object that you can hold, for example a rock, is also taken from
farther: up to 15 cm from your finger tip, when no other object that a
press acts on is nearer. The halo shows which object it is. Set
hand_wide_pick_cm in bwvr.ini to change the distance (0 turns it off).

An object that moves, for example a rock that falls, can be taken from
farther: the faster it moves, the longer the reach, up to 8 cm more.
An object that you threw does not have the longer reach until your
hand is fully open, so a throw does not come back in the same grip.
After that you can catch it with a new grip. Set catch_moving=0 in
bwvr.ini to turn the longer reach off, and catch_cap_cm to change the
most. With catch_moving=2 an object that you threw does not have the
longer reach for 0.6 s. With catch_moving=1 an object that moves is
taken from 15 cm at most.

You can catch an object in the air, and you can take an object on the
land that still rolls. Close the grip on it: the object is in your
hand at once. It stops its own motion, and it goes to its place in
your hand in about a tenth of a second. The game itself takes an
object that moves a quarter of a second after the press or later, and
it breaks a rock when the button comes up sooner. So the mod makes
the game take the object at the press. It also keeps the button of
the game down until the game has the object, also when you open the
grip sooner: the object then comes into your Hand and falls out of
the open Hand. A short grip on a rock that lies still breaks the
rock, as a click does in the game. Set catch_at_once=0 in bwvr.ini
for the pick up of the game: the object then moves on for a moment
before it is in your hand. Set grip_press_hold=0 to let the button of
the game come up with the grip.

A tree that fell becomes a dead tree when it comes to rest. If you take
the tree at that moment, it stays a tree and comes to your Hand. Set
catch_rest_guard=0 in bwvr.ini for the game's own rule: the Hand is
then empty, and the dead tree lies on the land.

When two objects are near the point, the object that the point
touches wins, and of two such objects the smaller one. A scroll wins
over a house, a tree or a forest that the point is in. A red halo
shows an object outside your influence.

A scroll is a target from 10 cm. At more than 4 cm it is the target
only when no other object is in reach. The scroll of a challenge
starts at the first press of the grip on it. Set scroll_reach_cm in
bwvr.ini to change the 10 cm. Set scroll_click=0 and scroll_draw=0
to turn off the press on a scroll and the drawing of its halo.

The color of the glow is the color of the grip on the hint cards.
It is gold when the grip can pick something up or take from a store
or a field. It is a paler gold when the grip can start a scroll,
knock on a house or touch your Creature. There is no glow when a
press does nothing, and none while the Hand holds an object. To turn
the glow off, clear "Glow at the Hand's point" in the launcher. Set
hand_glow_idle=1 in bwvr.ini for a glow that shows always.

In the God view the quests of the game see where you are. When a
quest waits for you to come near a thing, your head or a hand must be
near it on the land. When a quest waits for you to look at a thing,
your head must face it. In a cut scene the quest sees the camera of
the cut scene, as in the game. Set script_distance=0 and script_view=0
in bwvr.ini if a quest must see only the camera of the game, far
behind you.


THE FAR GRAB
------------

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

When two objects are near the line of your hand, the choice follows
the body of each object and not one point of it. You can point at the
crown of a tree. A living thing comes first: a villager, a character
of a quest, an animal or your Creature takes the halo when you point
within about 6 degrees of it, also when a tree or a rock is on the
same line. Of two living things, the one nearest to the line of your
hand wins. A living thing that has the halo keeps it until you point
about 9 degrees away from it. So a tree beside it does not take the
halo back when your hand shakes. With no living thing near the line,
a small object comes before a large one that stands behind it.

Set far_grab_stay=0 in bwvr.ini if a living thing must not keep the
halo past its own 6 degrees. Set far_grab_choice=1 for a choice with
no ranks, in which a villager only scores better than a tree. Set
far_grab_choice=0 to choose by one point of the object. The values 3
and 4 give each object a ball of one size for its class.

A far object must be near you: at most 2 m of your room from your
head, along the floor. Set far_grab_max_m in bwvr.ini to change this
distance. The value 0 gives no limit.

A pile of food or wood of a store, a field and a fish farm are far
targets too. Point at one and keep the grip closed to collect from a
distance. Open the grip to stop. When the pile is used up while the
grip is closed, the grip carries the bundle, and the next opening of
the grip puts it down. With far_take_carry=0 in bwvr.ini the Hand
lets the bundle go when such a pile is used up.

While you collect from a distance, the pieces of food or wood go up
out of the pile and along a curve into your Hand. With take_stream=0
in bwvr.ini they come out of the land under your Hand.

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
more than 15 cm from each such object. Set far_grab_near_cm and
far_grab_near_out_cm in bwvr.ini to change the two distances.


HOLD, RELEASE AND THROW
-----------------------

The rope of the leash ends in the palm of your hand. Set
leash_at_palm=0 in bwvr.ini to end it at the wrist of the game's
Hand.

The Hand lets go when you open the grip. The grip opens when your
hand starts to open fast, or when it is almost open. In the first
moment after you close the grip on an object, only an almost open grip
lets go (set grip_press_guard=0 in bwvr.ini to turn this off), so a
fast grab does not break a rock. If the Hand lets
go when you do not want it to, set grip_fast_open=0 in bwvr.ini. The
game then drops the object at once, and does not wait for its next
game turn. Set release_at_once=0 in bwvr.ini to make the game wait
for its next game turn.

In the God view and in the Tabletop play mode, an object that you
throw flies as a real object flies in your room: as fast as your hand
moved, on the same arc, for the same time. Swing your hand and open
the grip in the swing. A hard throw can kill a villager, as in the
game. Open the grip with a still hand to put an object down. If your
hand is more than 15 cm over the land, the object falls and does not
go to the land at once. Set release_fall_cm in bwvr.ini to change this
height, or release_fall_cm=0 to put the object down at each height.
A miracle flies as the game makes it fly. Set throw_law=1 in
bwvr.ini for the slower flight of the physics of the game, or
throw_law=0 for a throw that scales your hand speed by a fixed gain.

After a thrown object touches the land, it slows to the pace of the
game, bounces low and comes to rest soon. Set throw_land=0 in bwvr.ini
if you want the landing of the physics of the game at the speed of
the flight. An object that lies on the land does not turn in place,
and on a steep slope it goes down to flatter land. Set
throw_land_rest=0 in bwvr.ini for a comparison.

The mod draws a thrown object between the game turns. While the
object touches a different object, it moves at the pace of the game,
and its picture does not stop and jump. Set throw_draw=0 in bwvr.ini
for a comparison.


MIRACLES
--------

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
to turn off the miracle in your Hand.


YOUR CREATURE
-------------

To stroke or slap your Creature, put your hand on it and close the
grip. The view does not move, and your hand stays open: the index
finger for a small Creature, the flat hand for a large one. The flat
hand shows when the Creature is three quarters as tall as your hand
is long, or more. The hand opens half a second after you close the
grip. Move your hand slowly over the Creature to stroke it. Hit its
body with a fast move, about 1 m/s, to slap it. A swing of the arm
and a flick of the wrist count the same, forehand and backhand. Set
creature_slap_mps in bwvr.ini to change the speed. Set
creature_stroke_hand=3 in bwvr.ini for a fist, and a slap of the arm
at 1.5 m/s. Set creature_finger_below_pct and creature_flat_above_pct
(60 and 90) to change the size at which the hand changes, in percent
of the length of your hand. With the flat hand, the glow and the
touch are at your palm. Set creature_palm_point=0 in bwvr.ini to keep
them at the tip of your index finger.
The glow is pale gold while your hand touches the Creature. Open the
grip to stop. A short press of the grip on your Creature changes its
leash, as a short click with the mouse does. When the interface panel
is set up, the panel of the Creature shows near the Creature.

To change the leash from a distance, point your hand at your Creature,
close the grip and open it again at once. The leash changes when the
grip opens. The card on your hand says what the grip does: put the
leash on, take it off, or take it back from an object. This does not
start a stroke. For a stroke or a slap, put your hand on the Creature.
Set far_leash=0 in bwvr.ini to turn this off.

To make your Creature stay, point your hand at it from a distance,
close the grip and keep it closed. After a third of a second the
controller gives a pulse. Your Creature stops, turns to you, looks at
your hand and stays where it is for as long as the grip is closed. You
can move your hand in that time. Open the grip, and your Creature goes
on by itself. A stay does not change the leash. It is not a stroke and
not a slap, so your Creature learns nothing from it. The card says
"Grip hold Make it stay". Set creature_hold_stay=0 in bwvr.ini to turn
the stay off: each grip then changes the leash when it closes. Set
creature_hold_stay_ms in bwvr.ini to change the time of the hold, in
milliseconds (350). Set creature_stay_look=0 if your Creature must not
look at your hand.

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
hand on the Creature. Set creature_command=0 to turn the commands of
the trigger off. The grip on the land then sends the Creature.

A press of the trigger is one of two things. A quick press on an
object with a halo makes your Creature act on that object. So does a
longer press, if you still point at that object when you let go. If
you hold the trigger and move your hand away from the object, the
press becomes a walk: the halo goes, an arc and a green ring show a
point on the land, and the controller gives a tick. The press then
stays a walk until you let go. No object takes the halo, also not the
first object when you point at it again. Let go, and your Creature
goes to the ring.

A press that starts on the land is a walk from its start. You move the
ring over the land, and no tree, rock or villager takes the halo, also
when you point at one. To send your Creature to an object, point at
the object first and then press the trigger. Set
creature_command_rule=3 if a held press on an object must go to each
object that you point at. Set creature_command_rule=2 if a held press
on the land must also change to an object that you point at.

An object takes the aim of the trigger only when you point at it:
within 3 degrees, at most 10 m from your head, and not when you point at
the land in front of it. A villager, a character of a quest or an
animal in that aim comes before a tree or a building. If objects take
the aim too often or not often enough, set creature_command_cone_deg
and creature_command_max_m in bwvr.ini. Set creature_command_rule=1 if
the press must choose its target, and an object can take the aim from
far away.

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
value 0 turns the shake off.


HAPTICS
-------

The controllers give short pulses for what you do. A pulse comes for
a new target, a lock on a far object, and a fetched object that
arrives. A pulse also comes for a pick up, a release, and a touch, a
stroke or a slap of your Creature. A change of the leash, a command,
the teleport and a gesture that the game knows give one too. A hand
that is still gives no pulse. Set haptics_pct in bwvr.ini to change
the strength: 100 is the default, 50 is half, and 0 turns the pulses
off.


TABLETOP
--------

In the play mode "Tabletop (test)", the island is a model on a
table. The Hand is where your hand is on the model, with no long
reach. The left grip moves the land on the table: pull to slide it.
Hold both grips, then turn your hands to turn the land, or spread
them to zoom in. The table stays where it is, and the land stops
when you let go.


TEST KEYS OF THE NUMBER PAD
---------------------------

With "Test keys" on in the launcher, some keys of the number pad
switch between two ways of one behavior while you play. The
controller gives a long pulse for the way that is the default, and a
short pulse for the other way. These keys are for testers. A player
does not need them.

  Ctrl 1    With button_layout=0: the two ways of the ring (X,
            the stick, or both).
  Ctrl 5    The four rules of the aim of the trigger
            (creature_command_rule). One short pulse for each
            unit of the value.
  Ctrl 7    The two rules for what a quest sees
            (script_distance and script_view).
  Ctrl 8    The two ways to cast a miracle (miracle_hand).
  .         The rule "near objects win" and the pick of the Hand
            (far_grab_near_cm).
  *         The two ways of release (release_at_once).
  Shift /   In the Temple: the three ways of the view of a room
            (temple_view and temple_camera_screen).
  Ctrl Shift +
            The fade of the Temple in both eyes, or in the left
            eye only (temple_fade_eyes).
  Shift +   A catch of an object that moves keeps the button of the
            game down, or lets it go after 167 ms (grip_press_hold).
  8, 2      Turn the pointing line 1 degree up or down.
  4, 6      Turn the pointing line 1 degree to the left or right.
  5         Go to the next preset of the pointing line.


CUT SCENES
----------

A cut scene plays on a screen. While the game shows its black bars,
the view closes to a screen 2 m in front of you, in the direction
that you look when the cut scene starts. The game moves its camera
there as it does on a monitor. The screen is a window: the scene is
behind it, and the subtitles are below the scene. The view opens
again when the cut scene ends. The screen does not follow your head.
If you do not see it, press F11, and it comes in front of you. The
left menu button is Esc. The Hand does not follow your hands until
the view is open again. To see cut scenes in the full view with held
shots, set cutscene_window=0 in bwvr.ini.

A story camera with no black bars can put a shot in an odd place.
The game can also move the camera for you with no bars, for example
to follow the Creature. There the mod cuts to each shot instead of
gliding to it, because a glide that fills the view is uncomfortable.
The shot itself is the shot that the game chose, and some shots sit
too near or point away from the action.


KNOWN ISSUES
------------

This is an alpha build for testers. Expect faults. Save the game
before you start a quest. These faults are known:

  - A catch of an object in the air, and a grip on an object that
    still rolls, put the object in your hand at once. This is tested
    on the desktop only, and not in a headset.
  - In the Temple, black frames can show in the guide of the hall.
    In the hall the frame rate is low while the pool is in your
    view.
  - One eye can show faint pictures of the frames before.
  - The aim at a distance puts a villager, a character of a quest
    and an animal before a tree or a rock. This rule is not tested
    in a headset. Of a rock and a tree near the same line, the aim
    can still select the tree.
  - With the leash on, a press of the trigger with the halo on a
    tree sends your Creature to the tree. Hold the trigger and
    move your hand off the tree, and the press becomes a walk to
    the land. This is not tested in a headset.
  - With a tree in your Hand, the card can say "Grip Plant" when a
    rock or a different object is at that place. The tree then
    falls and is not planted.
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

The edge of the view can show black when you turn your head fast. Set
"Margin around the view" in the launcher (key fov_margin_pct). The
game then draws more on each side. A margin costs frame time, so use
the smallest one that helps.


SUPPORT
-------

The mod writes a log, bwvr.log, beside runblack.exe in the game
folder. It starts a new log each time the game runs, and it keeps
the logs of the 20 runs before as bwvr-old-<date>-<time>.log. The
first line of the log gives the build of the mod.

To report a fault:

1. Open the Issues page of the project on GitHub.
2. Select "New issue", and fill in the form.
3. Attach bwvr.log and each bwvr-old file of the same day.
4. Write what you did, what you expected and what happened.

The log holds:

  - the settings that the mod read,
  - the hooks that went in, and the hooks that failed,
  - the frame rate reports, and a count of the frames.


HOW TO REMOVE IT
----------------

1. Open Windows Settings, then Apps.
2. Find "Black & White VR Mod" in the list.
3. Remove it.

The uninstaller takes away only the files that the mod put in the
game folder, and the log. If the game folder held a ddraw.dll from
another program before you installed the mod, the installer kept
that file, and the uninstaller puts it back. Your saved games, your
creature and the game files are never touched.

The uninstaller asks if you want to keep your settings (bwvr.ini). A
later install of the mod starts where you left off if you keep them.


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
