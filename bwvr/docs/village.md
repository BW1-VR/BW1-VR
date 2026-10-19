# Clicks, scrolls, the village and the Temple door

This doc is about the things of the world that the Hand uses but does not
carry: a click on a thing, the scroll of a challenge, the village (the totem,
the Worship Site, the store, the Workshop, the scaffolds, the villagers), the
door of the Temple and the three leash posts.

Marks: [V] is verified in the disassembly. [I] is inferred. The facts come
from `disasm.py` on `runblack.exe` 1.42, and from `Scripts\info.dat` and
`Data\Gestures.jty`.

Related docs: `miracles.md` (the gestures and the cast), `grab.md` (the grip,
the choice of the target, an object onto a thing), `hand.md` (the Hand),
`hand-model.md` (the halo), `controllers.md` and `ui-layer.md` (the inside of
the Temple).

## Summary

- Signposts, scrolls, the Temple and the buildings go through the same render
  pick. A direct Hand that points the pick through the controller point keeps
  each click working.
- A scroll of a challenge is not active until the first click. The mod finds
  each scroll from the game's draw call and treats a press on it as a click
  for the scripts.
- The totem of the Village Centre follows the real hand one to one. The mod
  gives the game the screen rows that make its own math do that.
- A miracle icon, the store, the Workshop, a scaffold and a villager work with
  the grip. The rules for the grip are in `grab.md`.
- The door of the Temple and the leash posts are targets of a tap. The mod
  adds the posts as candidates, because the game offers them to the pick as
  invisible balls.

## Clicks on things in the world

Signposts, scrolls, the Temple and buildings go through the same render pick
[V for the draw callers: `Abode::Draw`, `MultiMapFixed::DrawBuilding`,
`CitadelHeart::DrawNow`, `Reward::ProcessDrawing`,
`Game3DObject::AddForDrawing`]. Signposts and scrolls are `Game3DObject`s
[I]. The interface actions read the same records (`+0x3C8`, `+0x400`).
`GInterface::CalculateOverObject` `0x005C9FC0` picks the tooltip from them.
Inside the Temple the game draws from another camera (`GGame+0x205A28`), and
the Hand runs the Citadel state, which calls `SetDistanceFromView` and
`ScreenPointToWorldAtDistance` itself [V].

So a direct Hand that points the render pick through the controller point
keeps every click working. The 3D nearest search of the mod takes each object
that a press taps, a scroll and a house too, and the game's pick at the pixel
stays for an object that is not near the Hand's point. The button of a tap is
the Action button, the grip. See `grab.md`, "One point for each interaction".

## The scroll

The code is in `hand_direct.cpp` (`h_ScrollDraw`, `ClickScroll`, `Choose`,
`ScrollClick`, `h_RightPress`, `FarSelect`, the words of `seek_plan`),
`hand_glow.cpp` (`HandGlowPicturePoint`) and `near_math.h` (`ScrollReach`,
`ScrollPress`, `TargetRule`, `PicturePoint`).

### A scroll is not active until the first click [V]

The game taps a scroll only while it is active. The game makes the scroll of
a challenge active at the first click. A mod that takes "a press does not tap
it" as "a press does nothing" has no target at such a scroll: no glow, no
halo, and a press with the pick record empty.

- `ScriptHighlight::InterfaceValidToTap` `0x0070ADD0` gives 1 for a silver
  scroll (type 1) with a script (`+0x78`). For each other type it also needs
  the active flag (`+0x60`).
- `ScriptHighlight::SetActivated` `0x0070A630` sets the flag. Its callers
  are the script word `SET_ACTIVE` (`GScript::SetActive` `0x006FD720`, native
  255), the creation and `Process` (type 1 only), and `InterfaceTap`.
- The script `ChallengeHighlightNotify` (`challenge.chl`, script 6, file
  `ChallengeNotify.txt`) owns the scroll of a challenge. It loops until
  `GAME_THING_CLICKED(Highlight)` or `GAME_THING_CLICKED(Location)` is true.
  Only then it calls `SET_ACTIVE(Highlight, 1)`, and the challenge starts.
  `Location` is an object of the challenge, for example a house.
- `GAME_THING_CLICKED` (`GScript::GameThingClicked` `0x0070AEB0`, native 16)
  compares its object with the clicked object of the interface
  (`GInterface+0x45C`, with the game turn at `+0x468`).
- `fn_005D36D0` writes that record. Only `fn_005D3930` (thiscall on
  `GInterface`, one `Object*`, `ret 4`) calls it, before each test of a tap.
- The game calls `fn_005D3930` from three places: the right press on an
  object that it can tap, hold or leash (`fn_005D1740`, `0x005D1833`), and
  two places of the interface step for a press that ended within 225 ms with
  an object in the pick record (`0x005D531B` and `0x005D5426`).

So in the plain game a short click on a scroll that is not active is a click
for the scripts, with no tap. The script then makes the scroll active and
starts the challenge.

In the loop the script also lets the advisor speak when the camera position
is less than the global `NotifyDistance` from the scroll and the scroll is in
the field of view. That part does not change the active flag. No instruction
of `challenge.chl` writes `NotifyDistance`.

**What the plain game shows at such a scroll.** The interface gives hand mode
`0x12` (something to interact with) for a scroll in the pick record, active
or not: the mode needs only that the object is available. So the plain game
shows the same Hand over the two states, and a click works on the two. Only
the beam of light shows that a scroll is active. The glow and the halo of the
mod show on the two states for the same reason: a press of the grip does
something.

### How the game draws a scroll [V]

`ScriptHighlight::Draw` `0x00709C60`, on each call (each eye pass):

1. It does nothing when `+0x5C` is not 0, or with no mesh (`+0x40`). A
   scroll that is not silver is also not drawn while the flag at
   `*(GGame+0x250090)+0x80` is 0.
2. **The place.** The point of the scroll on the map, at the altitude of the
   land plus the height in `+0x1C`. `ScriptHighlight::Process` sets that
   height on each game turn. There is no bob.
3. **The turn.** The angle is the angle of the mesh (`+0x48`) plus 0.00314
   radians for each ms of the frame. Each call adds a step, so the second
   eye pass draws the scroll a step further than the first one. A silver
   scroll that is active faces the camera of the eye pass.
4. **The size.** A scroll that is not silver has its own scale times `d / 30`,
   where `d` is the distance from the game's camera to the scroll, held
   between 10 and 30 units. The camera is the point of `GGame::GetCamera` at
   `+0x118`, `+0x148` and `+0x178`: the game's own camera, and not the camera
   of the eye pass. So the two eye passes get one size. In the God view the
   game's camera is 400 units from the player, so each scroll is at its full
   size at each scale.
5. `LH3DObject::SetPosition` puts the point, the angle and the scale in the
   matrix of the mesh (`0x00709ED2`).
6. The draw takes the middle and the half sizes of the box of the mesh
   through that matrix: the middle in the world, and the length of the half
   diagonal (`0x00709ED7` to `0x00709FC0`).
7. `SingleMapFixed::Draw` draws the mesh. It offers the scroll to the pick
   as each object (`Game3DObject::AddForDrawing`).
8. `GInterface::SendInvisibleDrawCollision` `0x00519960` gets the middle and
   the half diagonal plus 0.5 (`0x0070A2D7`). It offers the scroll only when
   the pick pixel is inside the circle of that ball on the screen.
9. **The glow of the game.** A sprite goes 1.4 times the half diagonal from
   the middle toward the camera of the eye pass, with a size of 2 times the
   half diagonal (`0x0070A2DF` to `0x0070A447`). Each eye has its own sprite
   place, on the line from the middle to that eye.
10. A scroll that is active and not silver has a beam too (`+0x68`).

The type is the index of the info at `+0x28` in the list at `0x00D96390`,
with 272 bytes for each info: 0 bronze, 1 silver, 2 gold, 3 and 4 a
scoreboard, 5 and 6 the cancel scrolls, 7 and 8 the Creature scrolls, 9 the
brotherhood. The scrolls of the challenges of Land 1 are type 2.

### What the mod does with a scroll

**The body of a scroll is its draw.** A hook on `ScriptHighlight::Draw`
records each scroll that the game draws in this frame, after the game's own
code. The body is the ball of step 6: the middle and the half diagonal of the
box of the mesh through the matrix that the draw set. So the body is the
scroll of the picture by construction. The scroll is a candidate of the
choice from that draw. The game's pick does not have to offer it.

**Both eyes have one body.** The place and the size do not use the camera of
the eye pass. The ball of the two passes is the same. The second pass turns
the scroll one step further, which does not move the middle of the box by
more than that.

**The reach.** The pick reaches 10 cm of the room for a scroll
(`scroll_reach_cm`), and 4 cm for each other object. 10 cm is the gap at
which the grab from a distance goes off, so no band is between the two. A
scroll past the reach of the pick is rule 5 of the choice, the last one (see
`grab.md`). So the longer reach takes no tree and no house from the Hand.

**The click.** `near_math::ScrollPress` gives what a press does on a scroll:
a tap when the game's test of a tap says yes, a click when the game draws
the scroll and does not tap it, and else nothing. A scroll of the click is a
target as a scroll of the tap is: the same glow, the same halo and the same
pulse. At the press the mod calls `fn_005D3930` with the scroll. The game's
own handler does not get that press, because it would take it as a press on
the land. The mod checks the first 10 bytes of the function before the call.

**The far touch** takes such a scroll too, and its press is the same click.

**A press with no target** names the nearest object that a press acts on,
its distance and its reach, at each press. It also names the nearest scroll
in view, whether it is active, and which test of the game says no.

### Other objects that float [V]

`GInterface::SendInvisibleDrawCollision` has six callers. Each one offers
its object only when the pick pixel is in the circle of a ball on the
screen:

- `ScriptHighlight::Draw`: the scroll.
- The draw of `SpellSeedGraphic`: the bubble of a miracle. The ball is the
  point plus the middle of the local box, with a radius of 2 times `+0x54`.
  The camera does not scale it.
- `SpellDispenser`, `MagicTeleport` and `LandscapeVortex`.
- `fn_00466730`, which `CitadelHeart::DrawNow` calls for three objects with
  a radius of 1.0. These are the three leash posts at the Temple (see "The
  door of the Temple and the leash posts").
- A reward draws with the draw of the bubble (`Reward::ProcessDrawing`) and
  `Game3DObject::AddForDrawing`.

Only the scroll takes its size from the distance of a camera. For each other
one the body of the mod is the box of the mesh at the place of the draw, as
for each object. The mod has not been tested with a bubble, a dispenser, a
teleport or a reward at the Hand. The signpost is not examined.

### Keys of the scroll

| Key | Default | What it does |
| --- | --- | --- |
| `scroll_draw` | 1 | Each scroll that the game draws is a candidate, with the ball of its draw. 0: only what the game's pick offers. Ctrl and the point key of the number pad change it in play |
| `scroll_reach_cm` | 10 | The reach of the pick for a scroll, 0 to 30 |
| `scroll_click` | 1 | A press on a scroll that is not active is a click for the scripts. 0: such a scroll is no target |

Test words of `seek_plan`, for the desktop runs only:

- `go`: the God view teleports to the next place of `seek_go="x,z;x,z"`.
- `scrolls`: one log line for each scroll of the frame.
- `picture`: the next entry of `seek_picture="lx,ly,rx,ry;..."` is the pixel
  of one thing in the picture of the left eye and of the right eye. The mod
  finds the place of the world that shows at the two pixels
  (`near_math::PicturePoint`), and the fake hand goes there.

### The log of the scroll

Each line that names a scroll gives its state in the same plain words
(`ScrollWords`): "The scroll is active: a press taps it", "The scroll is not
active: the game makes it active at the first click", or "The game says that
a press does not tap it" with the test of the game that says no.

- Tag `hand scroll`: `scroll_click` and `scroll_draw` at the start. One line
  for each change of the scroll that is nearest to the Hand's point within
  30 cm of the room, with what a press does. One line for each scroll that
  changes between active and not active, with the distance of the game's
  camera, of the camera of the eye and of the Hand's point.
- Tag `hand pick`: the line of the target says "the grip clicks it (the
  scroll is not active: the game makes it active at the first click)".
- Tag `hand press`: "the grip pressed on the target P (ScriptHighlight): the
  scroll is not active: the game makes it active at the first click. This
  press is that click (fn_005D3930 of the game), so the script that waits for
  it goes on". For the far touch the line adds ", the target of the far
  touch".
- Tag `hand press`, a press with no target: "The nearest object that a press
  acts on is P (class, kind), N cm of the room from the point, and its reach
  is R cm. The nearest scroll in view is P (type, script N), M cm of the room
  from the point, and its reach is 10 cm.", and then the state of that scroll
  in the plain words.
- Each report window: the frames with scrolls in view, the scrolls that the
  game's pick did not offer, the largest difference of a ball between the two
  eye passes, and the count of clicks.

### Tests and limits of the scroll

The math test covers `ScrollReach`, `ScrollPress`, rule 5 of the choice and
`PicturePoint`.

A desktop run can test the near click and the far touch with the scroll of
the challenge "Creature saving people" of Land 1 (script 36, at 1755.47
13.27 2523.73), with the start "Keep your old Creature". The word `picture`
puts the fake hand at a pixel pair of the two eye pictures. At a scale of
29.6 units a meter, one pixel of disparity is about 17 cm at 3.4 m of the
room, so the picture limits that test and not the body.

**Only the headset can show:**

- If 10 cm is a good reach for a real hand at each scale.
- If the glow and the halo show at each scroll that a player could not start
  before.

**Not proven, and suspects:**

- The scoreboard and the Creature scrolls had no run. A silver scroll with a
  script is a tap from the start.
- The start value of `NotifyDistance` was not read in a run. If it is more
  than 0, the advisor speaks of a scroll only with the game's camera near
  it. In the God view that camera is 400 units from the player.

## The village with the real hand

The plain game is played around the village. The player sets how many
villagers worship with the totem of the Village Centre. The worshippers make
the chants (the prayer power) of the Worship Site. The player takes a miracle
there, makes it stronger and casts it. The player helps the villagers build
with scaffolds.

The code is in `src/mod/village.cpp`, with the pure math in `village_math.h`.
The cast and the cancel of a miracle are in `miracles.md`. The pick and the
grip are in `hand_direct.cpp`.

### The totem of the Village Centre [V]

The totem is a `TotemStatue` (vtable `0x008E029C`). The town has it at
`Town+0x9A4` (the `TownCentre`) and then `+0xCC`. Its value is at `+0x80`,
from 0 to 1.

- `TotemStatue::ValidForLockedSelectProcess` `0x00738500` says yes for the
  player who owns the totem, when the totem is complete, the heart of the
  Temple is complete and the town has a complete Worship Site
  (`Town+0x98C`). A press of the Action button then starts the locked select
  of the game, the same action as a take from a store (interface action 4).
  So the hand mode at the totem is `0xD`, as at a store.
- `NetworkUnfriendlyStartLockedSelect` `0x007385E0` copies the value to
  `+0x88`, starts Immersion effect 15 and the sound of the totem. While the
  action runs, the hand mode is `0xE` (`fn_005D80C0`: an object in the pick
  record that is no Creature), and the Hand is in state 5, Totem.
- `HandStateTotem::Enter` `0x005B7690` puts the Hand at the map point of the
  totem, at the height of its mesh plus 0.7 of its height. It pins the mouse
  picture there (`LHMouse::PinImage`).
- `HandStateTotem::Update` `0x005B77A0` reads the object of the pick record
  of now (`GInterface+0x3C8`). It takes the screen rows that the pointer went
  up since the last frame (`CHand+0x4868` less `+0x4860`) and makes a move
  from them: rows times the height of the totem times 3, divided by the
  height of the screen. A move down stops at the land. The move goes to
  `TotemStatue::NetworkUnfriendlyLockedSelect` `0x007386A0`, which adds 0.1
  of it to `+0x88` and keeps the result in 0 to 1.
- `TotemStatue::Draw` `0x00738960` puts the pole 8 units higher for each 1.0
  of the value. While the Hand moves the totem it shows a tooltip (text
  `0xEEB`) with the value times 100: "People Worshipping: 22%".
- **The ghost.** The totem has two meshes, at `+0x8C` and at `+0x40` (2.73
  units higher). The pole shows the value at `+0x9C`, which goes to the value
  of the town in `|change| * 5.2` s (`TotemStatue::SetWorshipPercentage`
  `0x00738270`). While the Hand moves the totem, `Draw` puts the pole at the
  value of the Hand (`+0x88`). When the two values are not the same, `Draw`
  first draws both meshes at the other value with an alpha of `0x80` (the
  byte `+0x4F` of the 3D object, the calls of `LH3DObject::SetPosition` at
  `0x00738B9C` and `0x00738C13`). So while the Hand moves the totem, the
  ghost is at the old value. After the release, the pole goes up or down to
  the new value, and the ghost is at the new value. The ghost is the same
  mesh at a different height, so each vertical face of the ghost is in the
  plane of a face of the pole. Where the two overlap, the depth test gives
  noise: the faces fight. This comes from the draw of the game, so it is the
  same with each renderer.
- The start of the locked select (`fn_005DA950`) writes the totem to
  `GInterfaceStatus+0x3C`, as it writes a pile of a store. The totem says no
  to `IsLockedInInteract` (slot `0x6A0`, `0x00402790`), and its
  `ProcessInInteract` (slot `0x808`, `0x00639B10`) always says go on. So the
  game ends the action only at the release, and only when the pick record
  holds the totem then ("How a take ends in the game", `grab.md`).
- The release (`NetworkUnfriendlyEndLockedSelect` `0x00738630`) sends packet
  `0x29` with the value. The town then works to that value (`Town+0x5C0`).
  `Town::GetWorshipersNeeded` `0x0073C860` multiplies it with the count of
  the villagers that can worship (`Town+0x618` and `+0x61C`) and rounds. A
  value above 0 sends one villager at the least.

In state 5 the Hand of the game is on the top of the pole. The mesh of the
game's Hand is hidden (`hand_hide_game=1`), and the mod draws its hands at the
real hands. With `totem_take=1` the totem stays in the pick record while the
grip is closed, so the game keeps the totem while the hand goes away from it,
and the opening of the grip ends the action (see "The mod: the totem is the
source of a take").

### The Worship Site and its chants [V]

Each town of the player has a Worship Site at the Temple (`Town+0x98C`, a
`WorshipSite`). A town gets one only when the game permits it: on Land 1 a
script of the story does that (`SET_CAN_BUILD_WORSHIPSITE`, `0x006FEC40`,
which clears `Town+0x5F0` and calls `Citadel::FindOrCreateWorshipSite`). The
start with no story has no Worship Site. Then the totem does nothing and no
miracle icon is there.

The site keeps a pool of chants at `+0xF0`. Its step of a game turn (it ends
at `0x0077B894`) adds what the worshippers made in that turn and puts the sum
at `+0xF8`. The chants that the turn used are at `+0xFC`. With `+0x108` set
the site has 1,000,000 chants (`WorshipSite::GetTotalChantsAvailable`
`0x0077CC30`).

**A miracle icon.** The miracles of a town come from
`Town::AddMagicTypesHeld` `0x0073D380`: the setup of a land and a reward call
it. It turns the magic type on for the player (`GPlayer::SetMagicTypeEnabled`)
and gives the Village Centre the icon (`TownCentre::AddSpell`) or the power
up. The Worship Site has a `WorshipSpellIcon` (vtable `0x0099D878`) for each
miracle of its towns: the list starts at `WorshipSite+0xE0` and goes on at
`+0x110` of each icon. The icon has its `GSpellSeedInfo` at `+0x80`, the byte
"charges" at `+0x120`, the power up that it charges for at `+0x124`, the
player it charges for at `+0x128` and its chants at `+0x134`.

A tap on the icon (`SpellIcon::InterfaceTap` `0x00726430`,
`WorshipSpellIcon::ActualInterfaceTap` `0x0077F880`) does the first of these
that fits:

1. The icon has the chants that the miracle needs, and the Hand is empty:
   the miracle goes into the Hand (`0x0077F8F0`).
2. The icon charges for this player: the charge stops, and the chants go
   back to the pool (`CancelCharge` `0x0077F9A0`).
3. The Hand is empty and the player has the magic type: the icon starts to
   charge (`fn_0077FA00`).

`WorshipSite::ProcessSpellIcons` `0x0077B4D0` gives the chants of the turn to
the icons that charge, once a game turn. `WorshipSpellIcon::Process`
`0x0077F390` puts the `SpellSeed` in the Hand when the icon has enough. With
chants in the pool that takes 3 or 4 frames. The chants go into the seed
(`SpellSeed+0x74`). Water needs 5000 chants, Heal 6000 and Food 7000.

**The gestures.** `GSpellSeedInfo+0x100`, `+0x104` and `+0x108` are the
gestures of the chain that asks for a miracle (see `miracles.md`). The first
is the spiral (1) for each miracle of the god and the inverse spiral (2) for
each Creature miracle. The second gesture is:

| Miracle | Second gesture | Power up 1, power up 2 |
| --- | --- | --- |
| Storm | fork down | inverse spiral, spiral |
| Forest | Cyrillic L | none |
| Fireball | vertical | inverse spiral, spiral |
| Food | S shape | spiral |
| Spiritual Shield | fork right | none |
| Physical Shield | fork left | none |
| Lightning | fork up | inverse spiral, spiral |
| Heal | heart | spiral |
| Wood | three | none |
| Water | W shape | spiral |
| Flock of birds | square spiral | none |
| Flock of animals | inverse square spiral | none |
| Teleport | house | none |
| Explosion | star | inverse spiral, spiral |

No miracle has a third gesture. The game looks for the start gesture only
while a Worship Site of the player has an icon that the player can ask for
(`WorshipSpellIcon::ValidForRequestSpell` `0x0077FBA0`): the magic type is
on, the icon does not charge, and the site has chants.

**A power up.** The magic type of a miracle is at `GSpellSeedInfo+0x124`, the
magic types of its power ups at `+0x128` to `+0x130` and their gestures at
`+0x134` to `+0x13C`. `fn_005CEE80` lists the gestures of the power ups that
the player has. The game looks for them only while `fn_005CEF50` says yes:
the seed is ready, it is from a Worship Site, and `SpellSeed+0x98` is 0. That
field is not 0 after the first cast. So a miracle gets stronger only before
its first cast. A match sends packet `0x6A`: the icon charges for the power
up, and `SpellSeed::SetPowerUp` `0x00729B30` runs when the seed has the
chants (the first power up of Water costs 2000 more).

**The way back.** A miracle from a Worship Site that leaves the Hand with the
shake gives the chants that it still has back to the pool
(`SpellSeed::ApplyToWorshipSite` `0x00729A80`). A press with the miracle on
its icon, or on the totem of the Worship Site, does the same
(`IsSpellSeedReturnPoint`). A pour takes chants out of the seed.

### The Village Store, the Workshop and the scaffolds [V unless marked]

**The Village Store.** The piles of food and of wood are bulk sources
(`PileResource::ValidForLockedSelectProcess` `0x0066E4F0`, "The bulk take").
Food or wood that the Hand lets go over the store goes into it
(`StoragePit::AddResource` `0x00732F60`). The flags of desire
(`TownDesireFlags`) have a draw and a help text only. The player reads them.
No press acts on them.

**The Workshop and a scaffold.** The Workshop is a store for wood
(`Workshop::AddResource` `0x00779E00`). It makes a `Scaffold` with
`fn_00779910` [V], when it has the wood [I].

- The Hand can take a scaffold that is not planted, or one whose building
  site has no work done (`Scaffold::ValidForPlaceInHand` `0x006E96A0`).
- While the Hand carries it, `Scaffold::ProcessInHand` `0x006E8570` shows the
  building that it would make at the place of the Hand.
- A scaffold that the Hand lets go over a second scaffold joins it
  (`Scaffold::ValidToApplyThisToObject` `0x006E9900`, `Combine`
  `0x006E9B60`). The count of the scaffolds sets the kind of the building
  [I].
- A scaffold that the Hand lets go over the land makes the building site
  there (`TryToBuildPlannedBuilding` `0x006E9080`), when the game has a place
  for the building. `Scaffold::ProcessInHand` looks for the place each game
  turn: the land must be good for a fixed object
  (`MapCoords::IsSuitableForFixed`), a town must be there for the building
  (`Scaffold::GetTownForBuilding`), and the place must be away from the
  Workshops (`Town::IsScaffoldAwayFromWorkshops`). The hint of the game for
  the grip is then the name of the building, and else "Throw". When the
  scaffold has more scaffolds than the building needs, the rest stays in the
  Hand (`InterfaceSetOutMagicHand` `0x006E9770`).
- A tap on a scaffold of two or more takes it apart
  (`Scaffold::InterfaceValidToTap` `0x006E9D80`, `InterfaceTap`
  `0x006E9DD0`).

**A villager and a job.** While the Hand carries a villager,
`Villager::InHand` `0x0076ADE0` looks for the objects near the villager each
game turn (`FindCloseObjectsForInteract`, from the place of the villager).
The first object that gives a job (`GetDiscipleStateIfInteractedWith`, slot
`0x644`: a field, a forest, a store, the Workshop, the Worship Site, a house)
sets it (`Villager::SetVillagerDisciple` `0x00756000`), and the game shows
the icon of the job. The villager keeps the job when the Hand lets him go.
The job comes from the place of the villager, so it follows the Hand. It does
not use the pick record.

### What the real hand can do

| Thing | With the real hand |
| --- | --- |
| The totem | The grip takes it, at the Hand or from a distance, and the pole follows the hand one to one until the grip opens (`totem_hand=1`, `totem_take=1`) |
| A miracle icon | A short press of the grip takes the miracle |
| The gestures of a miracle | B and a draw: the spiral, then the gesture of the miracle |
| A power up | Before the first cast. B and a draw of its gesture |
| The cast | By its kind (`miracles.md`) |
| The cancel, and the power that goes back | The shake or a tap of B |
| The Village Store | Hold the grip to collect. With `hold_target=1` the opening of the grip over the store puts the wood or the food in ("The bulk take" and "An object in the Hand that goes onto a thing" in `grab.md`) |
| The flags of desire | Nothing to do |
| A scaffold: take, set down, take apart | The grip takes it and sets it down, and a short press takes a scaffold of two apart |
| A scaffold: a building site | The grip opens where the game has a place for the building |
| The Workshop | Wood: the grip opens with the wood at the Workshop. A scaffold from it is not seen |
| Two scaffolds: join | With `hold_target=1` |
| A villager and a job | The job comes from the place of the villager in the Hand, and the card says the job |

### The mod: the pole follows the real hand (`totem_hand=1`)

A hook before `HandStateTotem::Update` gives the game the screen rows that
make its own math put the pole where the hand is (`village_math::TotemValue`,
`TotemRows`). The game alone reads the screen rows of the pixel of the
Hand's point. That moves the pole by only a part of the way of the hand, and
the part changes with the distance of the head.

The mod keeps the value of the totem and the height of the point of the real
hand from the frame in which the game starts to move the totem. The value
that it wants is that value plus the way of the hand in world units, divided
by 8. The rows are the rest between that value and the value of the game, so
no error adds up. A hand that goes above the top or below the bottom of the
travel does not move the pole. The pole moves again when the hand comes back
to the place where the pole stopped. The whole travel is 8 units: 18 cm of
the room at the default scale of the God view.

The mod changes only the rows. The game does the rest: the start, the stop
at the land, the tooltip, the sound, the packet.

### The mod: the totem is the source of a take (`totem_take=1`)

The totem in the locked select of the game is the source of a take for the
mod, as a pile of a store is (`VillageTotemInSelect`, `SelectSource` in
`hand_direct.cpp`). Two things then follow:

- `KeepTakeSource` puts the totem in the next pick record before each
  interface step and after each eye pass. So the release step of the game
  finds the totem and ends the action when the grip opens, also when the real
  hand is far from the pole. With the mouse the game holds the pointer on the
  totem, so its pick record always holds it.
- **The totem from a distance.** The totem is a bulk target of the grab from
  a distance (`TakeValid`, see `grab.md`, "A bulk source at a distance"). The
  grip that closes on it goes to the game as a press. The far grab then waits
  for the source of a take. With `totem_take=1` that is the totem, so the far
  grab goes to its Take state, and the grip stays the button of the game until
  it opens. The pole follows the height of the hand from where the grip
  closed. The tether and the halo stay until the game ends the action.

With `totem_take=0` the totem is no source: the far grab finds no take,
cancels the press after 700 ms (`near_math::kFetchMs`) and holds the grip
back. The game then sees the button up and lets the totem go, with the grip
still closed. At the Hand, the pick record has the object at the Hand's
point.

### The mod: the ghost is inside the pole (`totem_ghost=1`)

A hook on `LH3DObject::SetPosition` (`0x00423140`) acts only on the two calls
of the ghost in `TotemStatue::Draw` (by their return addresses `0x00738BA1`
and `0x00738C18`). It gives the ghost 97 percent of the scale of the game
(`village_math::GhostScale`), and then writes the scale of the game to the y
row of the matrix (`+0x24`). So the ghost has the height of the pole, at the
same place, and each side of the ghost is 3 percent of its distance from the
line of the pole inside the pole. The pole hides the ghost where the two
overlap, and the ghost shows only where it is above or below the pole. The
fix is in the geometry of the game, so it is the same with each renderer.
With `totem_ghost=0` the ghost has the size of the pole.

### The mod: a scaffold joins the scaffold at the Hand

This is one case of the rule for each object in the Hand with the key
`hold_target`: see `grab.md`, "An object in the Hand that goes onto a thing".
The game joins two scaffolds when the Hand lets one go while the pick record
holds the other one. With an object in the Hand, the pick record would hold
the game's pick at the pixel of the Hand's point, and that pixel can be on
an object behind the scaffolds. So `VillageSelect` runs at the end of the
first eye pass while the Hand carries a scaffold. It looks at each object
that the draw offered and that the game lets the scaffold go on
(`ValidToApplyThisToObject`). The target is the object whose body is nearest
to the Hand's point or to the scaffold in the Hand, within `hold_target_cm`
(6 cm of the room, `village_math::JoinGap`). `VillageKeepRecord` puts the
target in the next pick record after each eye pass, before each interface
step and before each press. So the opening of the grip joins the two. With
no target the record is the game's own.

### The card

The card has the context "the totem or a miracle icon". Its lines stay while
the Hand's point is there, and one line of the game's own text stays with
them.

| State | Lines |
| --- | --- |
| The Hand's point is at the totem | "[Grip] Hold, then raise or lower", "Worship: 22%, 10 of 44 villagers" |
| The Hand moves the totem | "[Grip] Raise or lower, open to set", and the same line of the worship |
| The Hand's point is at a miracle icon | "[Grip] Tap to take the miracle" |
| The icon charges for the player | "[Grip] Tap to stop", "The miracle charges: 60%" |
| The site and the icon have no chants | "No prayer power. Villagers must worship" |
| A scaffold in the Hand is at a second scaffold | "[Grip] Open to join the scaffolds" |
| A scaffold in the Hand is at an other object that takes it | "[Grip] Open to put the scaffold here" |

The count of the villagers is the count of the game
(`village_math::Worshippers`). With a miracle in the Hand the lines of
`miracles.md` show. At its icon the line is "[Grip] Give the miracle back".

### Keys of the village

| Key | Default | What it does |
| --- | --- | --- |
| `totem_hand` | 1 | 0: the game moves the totem with the screen rows of the pixel of the Hand's point |
| `totem_take` | 1 | 0: the totem is no source of a take. The grab from a distance lets it go 700 ms after the press, and the pick record has the object at the Hand's point |
| `totem_ghost` | 1 | 0: the ghost of the pole has the size of the pole, and where the two overlap their faces fight |
| `hold_target`, `hold_target_cm` | 1, 6 | See `grab.md`, "An object in the Hand that goes onto a thing". The code also reads the old names `scaffold_join` and `scaffold_join_cm` as the defaults |

The keys are read from `bwvr.ini`. They are not in the launcher.

**The A/B key**, with `test_keys=1`: Ctrl with the 4 key of the number pad
changes `totem_hand`. The pointing controller gives a long pulse for 1 and a
short pulse for 0.

Test keys for the desktop runs only (the launcher removes them):

- `village_test_site`: a frame. At that frame the first town of the player
  gets its Worship Site, complete, from the function that the setup of a
  land uses (`CitadelHeart::CreateBuiltWorshipSite` `0x00465110`).
- `village_test_miracles`: a list of magic types. Before the site is made,
  the town knows each of them (`Town::AddMagicTypesHeld`). 22 is Water and
  23 its power up, 14 Food and 15 its power up, 10 Heal.
- `village_test_chants`: a list `amount@frame`. At that frame the pool of
  each Worship Site of the player has that many chants.
- `village_test_census`: a list of frames. At each one the log gets the
  towns, the totems, the Worship Sites, the miracle icons and the miracle in
  the Hand.
- `village_test_icon`: the seed type of the icon for the place `icon`.
- `village_test_scaffolds`: `count@frame`. At that frame the game makes that
  many scaffolds on the land under the Hand, 12 units apart, with the
  function that a reward uses (`fn_006E8460`). The place `scaffold` of the
  word `seek` gives each scaffold in its turn.
- `village_test_workshop`: read by `village.cpp`.
- `seek_plan` has the places `gosite` and `gototem` (the God view teleports
  to 60 cm of the room before the Worship Site or the totem), `icon` and
  `totem` (the Hand's point goes there), and `up` and `down` (the Hand's
  point goes 4 cm of the room up or down, `seek_step_cm`).

The word `trace` takes its templates from `fake_controller_gestures`. The
numbers of that list must have spaces between them, not commas.

### The log of the village

The tag `village` has one line for each change:

- The key at the start, and each press of the A/B key.
- "the Hand holds the totem ... of the Village Centre", with the value, the
  count of the villagers, the rule, the Hand state and the hand mode.
- "in the Totem state the Hand of the game is at ... and the point of the
  real hand is at ...", 20 frames after the state starts.
- "the totem is at ...", for each 10 percent of change, with the way of the
  real hand, the value that the mod wanted and the rows that it gave.
- "the Hand let the totem ... go", with the frames and the value.
- "the game draws the ghost of the totem at the other value" and "the ghost
  of the totem is gone after ... frames".
- With the tag `far grab`: "the game moves the totem ... at a distance".
- "the town ... now works to a value of worship of ...".
- "the miracle icon ... starts to charge" and "the charge of the miracle
  icon ... ended", with the chants of the icon and of the site.
- "the pool of chants of the Worship Site ... went from ... to ...", for a
  change of 100 chants or more in one frame.
- "the scaffold ... in the Hand can go on the ...", with the gap and the
  reach, and "... has no object to go on now".
- One line each report window: the frames in the Totem state, the frames on
  which the mod gave the rows, the totems taken and the charges. One more
  line with the frames that had a ghost of a totem.

The lines of a miracle in the Hand have the tag `miracle`, and they say
"from a Worship Site". A press with the miracle at its icon gives "the game
takes ... back at the ... of its Worship Site".

### Tests and limits of the village

The math test (`village_checks.h`) covers the value of the totem for a hand
that goes up, down, above the top and back, the scale of the ghost and how
far its faces are inside the pole, the rows against the game's own
math on a screen of 2880 and of 600 rows, the count of the worshippers, the
percent, each state of a miracle icon, the percent of its charge, and the gap
and the choice of a join.

Desktop runs with the start "Keep your old Creature" (44 villagers can
worship) have shown the totem (three steps of 4.0 cm up give the values 0.22,
0.44 and 0.67), the miracle icon (a tap, a charge, a cancel of the charge,
and the miracle given back), the whole chain of gestures with a power up, the
cast and the cancel with the power that goes back, and the join of two
scaffolds. The icon of Food gives the same chain as the icon of Water: a tap,
a power up with the spiral, a pour and a cancel
(`village_run.ps1 -Kind miracle -Icon 3`). The start with no story has no
Worship Site, so the totem is no target there.

The totem from a distance (`village_run.ps1 -Kind fartotem`): the grip
closes on the totem 0.56 m of the room from the Hand and stays closed for 400
frames. With `totem_take=1` the far grab goes to its Take state 5 frames
after the press, the pole follows the hand to 0.67 and back to 0.44, and the
Hand lets the totem go 3 frames after the grip opens. With `totem_take=0`
the far grab cancels the press after 42 frames (700 ms), and the Hand lets
the totem go with the grip still closed. The eye pictures of the same run
show the ghost: with `totem_ghost=0` the faces of the pole have lines of
noise where the ghost overlaps them, with the Vulkan renderer and with
dxwrapper. With `totem_ghost=1` the faces are clean with both.

**Not run on the desktop:**

- Heal from its icon, and the second power up.
- Worshippers that make the chants: the runs set the pool with a test key.
- A scaffold of three or more.
- A left handed player, and a multiplayer game.

**Needs the headset:**

- Whether the pole feels held: the mod's hand stays where the grip closed on
  the pole, and the pole moves with it.
- Whether the totem from a distance is easy to set: the pole follows the
  height of the hand, and the hand is not at the pole.
- Whether 18 cm for the whole travel is easy to set to a wanted count.
- Whether a real draw of the spiral and of the second gesture fits the
  templates of the game.
- Whether a tap of the grip on an icon is easy to hit: the icons of one site
  are 4 to 8 cm of the room apart.

**Limits.** The pole follows the height of the hand only: the hand can move
away sideways while the grip is closed. The mod does not change the game's
rule that a Worship Site must exist. A player who starts with no story has no
Worship Site until the game makes one.

## The door of the Temple and the leash posts

The inside of the Temple (the ray in a room, the Save Game Room, the turn
with the stick) is in `controllers.md`.

The code is `temple.cpp` (the hooks, the log, the card lines, the test
places) and `hand_direct.cpp` (`HandDirectOffer`, `BallOf`, the words `door`
and `post` of `seek_plan`).

### What the game does [V]

- **The door** is a `CitadelEntrance`, at `CitadelHeart + 0xDC`. It has a
  mesh for the collision only (`LH3DObject::Create`, `0x004676CD`), and the
  draw of the Temple offers it to the pick as each object
  (`CitadelHeart::DrawNow` `0x004670D0`). Its body is the cylinder of the
  game: a radius of 3.25 and a height of 6.41 units.
  `CitadelEntrance::InterfaceValidToTap` `0x00468F50` says yes in a
  multiplayer game. In a single player game it says yes when the story has
  the door on (`*(g_game + 0x250090) + 0xA0`, set by
  `GScript::SetCitadelInteract` `0x0070B9A0`).
  `CitadelEntrance::InterfaceTap` `0x00468EF0` calls `GoInsideCitadel(0)`
  when the Temple belongs to the local player.
- **The three posts** are `LeashObj` objects with no mesh. `TempleLeash` at
  `CitadelHeart + 0xE4` holds them at `+4`, `+8` and `+0xC`
  (`CitadelHeart::CreateLeashes` `0x00464950`). `fn_00466730`, from
  `CitadelHeart::DrawNow`, gives each one to the pick as a ball of 1 unit
  (`GInterface::SendInvisibleDrawCollision` `0x00519960`, cdecl: the object,
  the point, the radius). It does that only when the player of the Temple
  has `+0xA4C` set and the Temple is at its full size. The posts never come
  through `GInterface::SendObjectDrawCollision`.
- `LeashObj::InterfaceValidToTap` `0x00464450` says yes for the posts of the
  local player. `LeashObj::InterfaceTap` `0x00464490`: the number of the
  post is at `+0x54`, and `+0x58` points at the number of the chosen post. A
  tap on the chosen post sets -1 and sends nothing. A tap on a different
  post plays a sound, makes it the chosen one and sends packet `0x65` with
  the kind 1, 2 or 3 (the Leash of Aggression, of Learning, of Compassion).

At the scale of the start of a desktop run (about 250 units a meter) the door
is 2.6 cm wide and the posts are 1.4 cm from each other, 8 cm from the door.
A player who zooms in gets them larger.

### What the mod does (`temple_taps=1`)

A hook on `fn_00466730` knows the Temple of the local player while its draw
runs, and a hook on `GInterface::SendInvisibleDrawCollision` sees each post
with its point and its radius. `HandDirectOffer` makes the post a candidate
of the choice, and its body is that ball about that point (`BallOf`, in
`BodyOf` and in `Measure`). The rest is the path of each object that a
press taps: the nearest body in the reach of 4 cm is the target, it has the
halo, the pick record holds it, and the game taps it with its own tests and
its own `InterfaceTap`. The door needs no candidate of the mod, because the
draw offers it. `temple_taps=0` gives no target at a post.

**The card** has the context "the door of the Temple or a leash post". The
game has its own hint for the grip at these things, and a hint of the game
for a button wins over a line of the mod for the same button
(`controller-hints.md`). So with the hints of the game on, the card has the
line of the game. The lines below show when the game gives no hint for the
grip there:

| The Hand's point is at | Line |
| --- | --- |
| The door of the Temple | "[Grip] Tap to go into the Temple" |
| A post | "[Grip] Tap for the Leash of Learning" (or Aggression, Compassion) |
| The chosen post | "The Leash of Learning is chosen" |

### Keys of the Temple

| Key | Default | What it does |
| --- | --- | --- |
| `temple_taps` | 1 | 0: a post is no target of the real hand |
| `temple_test_taps_frame` | 0 | Test only. `temple_taps` stays 0 before this frame, so one run shows both |

The `seek_plan` words `door` and `post` put the Hand's point at the door and
at the next post, for the desktop runs.

### The log of the Temple

```
temple land  : the Temple of the player is in the draw. The door is at 1915.05 29.48 2508.89. The story has the door on. The leash posts are at 1905.02 34.83 2525.41, 1902.88 34.86 2528.11 and 1899.45 34.75 2530.72, each with a ball of 1.0 units. The chosen post is -1. temple_taps is on, frame 604
temple land  : the Hand's point is at the leash post 1, the Leash of Learning (1902.88 34.86 2528.11). The game says that a press taps it. The chosen post is -1, frame 1043
temple land  : the chosen leash post is 1 now (the Leash of Learning). It was -1, frame 1105
temple land  : the Hand's point is at the door of the Temple (1915.05 29.48 2508.89): the grip taps it, frame 1473
temple land  : the story turns the door of the Temple on: a tap goes into the Temple, frame 5120
```

The first line also says "The story has the door off: a tap does nothing
yet" when that is so. The lines of each press are the lines of `hand press`.

### Tests and limits of the Temple

Desktop runs (the God view, `fake_controller=2`, Land 1, the choice "Keep
your old Creature", `temple_run.ps1 -NoEnter`) have shown a tap at each post
(the chosen post changes and the packet kind follows) and at the door (the
player is in the hall 3 frames later).

**Not tested, and what needs the headset:**

1. A real hand at the posts: they are small at the scale of the start, and
   the nearest one wins. Whether a player hits the one that he wants.
2. A tap on the chosen post (the game sets -1 and sends nothing).
3. The door before the story turns it on: the card says "Tap to go into the
   Temple" then too. The log has the state of the door.
4. The grab from a distance on a post or on the door.
5. The lines of the card of the mod: no run had the hints of the game off.
