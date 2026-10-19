# The controls of the plain game, and what the controllers can do

This file lists each control of the plain game: each bindable action, each
fixed key, each mouse action, and each thing that the player clicks in the
world to reach a function. For each one it says what the game does and how
the player does it with the VR controllers. Section 7 is the list of what has
no way yet, and section 8 shows which ring item gives each function that
needed the keyboard.

The ring and the other inputs of the controllers are in
[controllers.md](controllers.md). The ring writes the key of an item into the
key buffer of the game (section 1), so the game does what it does for the
key.

Marks: **[V]** read in the disassembly of `runblack.exe` v1.42. **[D]** from
the docs of the mod. **[R]** seen in a desktop run. **[?]** not proven.

All addresses are in `runblack.exe` v1.42, image base `0x00400000`.

## 1. How the game reads a key [V]

```
WM_KEYDOWN, WM_SYSKEYDOWN                 GameWindowProc 0x007DBF30
  LHKeyboard::ProcessKeyboard 0x007DCAF0  one byte for each scan code, at 0x00E85374 + code
    GGame::KeyHandler 0x0054FF80          message 0x100 or 0x104, a key that is not 0
      [g_game vtable + 0x2C]()            the game says if it takes keys now
      GKeyBuffer::AddKeyIfDifferentToPrevious 0x005E1BF0   (key, modifiers)
GGame::ProcessGameInputs 0x0054C3D0, and GGame::Loop at 0x0054D27C
  GGame::ProcessBufferedKeys 0x0054C420   for each key of the buffer:
    GGame::ProcessKey 0x0063EF20 (key, modifiers)
```

- `g_game` is the pointer at `0x00D0195C`. The key buffer is the object at
  `g_game + 0x2502A8`: the entries at `+8` (16 bytes each, the key at `+8`
  and the modifiers at `+0xC`), the size at `+0xC` and the count at `+0xE`.
  `GKeyBuffer::AddKey` `0x005E1BB0` is `__thiscall (key, modifiers)`, `ret 8`.
- The modifiers are one word: `0x10` Shift, `0x20` Ctrl, `0x40` Alt.
  `ProcessKey` makes one modifier key from it (`fn_005553A0`): `0x2A`, `0x1D`
  or `0x38`.
- The keys that an action holds down (the camera keys) do not use the
  buffer. `ControlMap::ProcessActionsPerformed` `0x00470AB0` reads the byte
  of the scan code once a frame.

`ProcessKey` tests in this order:

1. Esc and F2 (section 3).
2. A dialog box is up (`0x00C4CCED`): the key goes to the box, and nothing
   else acts.
3. The player is in the Temple (`g_game + 0x205A28` is 1): the key goes to
   the Temple (`fn_00794830`, section 6).
4. `GGame::ProcessBindableKeys` `0x0063F830`: the control map finds the
   action of the key (`fn_00470A00`), and `GGame::DoAction` `0x0063F940`
   does it. `DoAction` does nothing while the help system plays a cut scene
   (`HelpSystem + 0x45E8` and `+0x45EC`).
5. The fixed keys (section 3).

## 2. The actions of the control map [V]

`ControlMap::LoadDefaults` `0x0046F890` sets 33 actions. The player can give
each one a different key in the Options Room. A record is `0x30C` bytes: the
key at `+0x100`, the modifier key at `+0x184` and the mouse code at `+0x208`.
The mouse codes (`BindableAction::IsMouseBeingPressed` `0x0046F500`): 1 the
left button, 2 the middle button, 3 the wheel up, 4 the wheel down, 5 the
right button.

"Ring" means an item of the ring of the hand that does not point. "Ring (ini)"
means an item that is not on a default page: the player puts its word in
`radial_menu_items`, `radial_menu_items2` or `radial_menu_items_temple`.

| No. | Name in the game | Default | What the game does | With the controllers |
| --- | --- | --- | --- | --- |
| `0x00` | Help | F1 | `HelpSystem::HelpQuery` `0x005C98E0`: help for the thing under the Hand | Ring, `help`, page 2 [R] |
| `0x01` | Move | left button | Drags the land. A double click goes to that place | Trigger. In the God view the grip of the other hand moves the land and A is the teleport [D] |
| `0x02` | Action | right button | Take, drop, throw, tap, stroke, cast | Grip of the pointing hand [D] |
| `0x03`, `0x04` | Zoom Out, Zoom In | wheel | The camera zooms | Stick up and down [D] |
| `0x05` | Talk | T | Opens the text line of a multiplayer game | No way. It needs a keyboard for the text |
| `0x06` | Zoom On | right Ctrl, held | The mouse and the arrow keys zoom | Not needed: the stick and the two grips zoom |
| `0x07` to `0x0A` | Move Left, Right, Forwards, Backwards | arrow keys | The camera moves | The stick of the other hand in the God view (`move_stick`), the grip of the other hand, and A [D] |
| `0x0B`, `0x0C` | Tilt Up, Tilt Down | A, Q | The camera tilts. In the God view the land moves up or down [D] | The grip of the other hand, pull up or down [D] |
| `0x0D`, `0x0E` | Rotate Left, Rotate Right | Z, scan code `0x16` | The camera turns | Stick left and right [D] |
| `0x0F` | Tilt/Rotate On | right Shift, held | The mouse and the arrow keys tilt and turn | Not needed |
| `0x10` | Tilt/Rotate Around Mouse On | middle button (the key 0 with no wheel mouse) | The mouse turns the camera about the Hand | Stick left and right. The mod hides this button from the game [D] |
| `0x11` | Zoom To Temple | Space | `fn_0064A7A0(0)`: `CameraModeNew3::ZoomToCitadel` `0x00457B60`. One press pulls the camera back to a safe view. A second press in 500 ms goes to the Temple. Two more presses go back | Ring, `temple`. It writes the key two times in one frame [R] |
| `0x12` | Zoom To Creature | C | A `CameraModeFollow` on the Creature of the player. A second press ends it | Ring, `creature`. In the God view it is one teleport to the Creature, because the key of the game starts a follow camera [R] |
| `0x13` | Zoom To Realm | F3 | `fn_0064A7A0(1)`: the camera goes up to see the whole land | Ring (ini), `land`. In the God view the view goes to the middle of the land at the scale of now [R] |
| `0x14` | Zoom To Inside Temple | F4 | `GGame::GoInsideCitadel(0)` `0x00553E10` | Ring, `enter`. Or a tap of the grip on the door of the Temple [R] |
| `0x15` | Zoom To Creature Room | F5 | `GoInsideCitadel(1)` | Ring, `room_creature`, on the ring of the Temple [R] |
| `0x16` | Zoom To Challenge Room | F6 | `GoInsideCitadel(2)` | Ring, `room_challenge`, on the ring of the Temple [R] |
| `0x17` | Zoom To Save Game Room | F7 | `GoInsideCitadel(5)` | Ring, `room_save`, on the ring of the Temple [R] |
| `0x18` | Zoom To Options Room | F8 | `GoInsideCitadel(4)` | Ring, `room_options`, on the ring of the Temple [R] |
| `0x19` | Zoom To Library | F9 | `GoInsideCitadel(6)` | Ring, `room_library`, on the ring of the Temple [R] |
| `0x1A` | Leash/UnLeash Creature | L | Packet `0x5F`: the leash goes on or off | A short press of the grip on the Creature, the grip with the hand pointed at it, and a shake to take it off [D] |
| `0x1B` | Show Villager Names | N | Changes bit `0x20000` of `g_game + 0x14` | Ring, `names`, page 2 [R] |
| `0x1C` | Show Villager Details | S | Changes the details of the villager under the Hand, or bit `0x200000` for all | Ring (ini), `details` [R] |
| `0x1D` | Quick Save | Ctrl S | Packet `0x61`, not in a multiplayer game | Ring, `save`, page 2. Not run: a test must not write a saved game |
| `0x1E` | Quick Load | Ctrl L | Packet `0x62` | Ring (ini), `load`. Not run, for the same reason |
| `0x1F` | Previous Leash | V | Packet `0x65`: the kind of the leash goes 1, 2, 3, 1 | The ring items of the leash (`leash_learning`, `leash_compassion`, `leash_aggression`, `leash_previous`) [R]. The gestures of the leash (B and a draw), and a tap of the grip on a post at the Temple [D, R] |
| `0x20` | Next Leash | B | The same, 1, 3, 2, 1 | The same (`leash_next`) |

The kinds of the leash are 1 the Leash of Aggression, 2 the Leash of
Learning and 3 the Leash of Compassion (`LEASH_TYPE`). A kind is in the
cycle only when the Creature has it (`Creature + 0x1108 + 4 * kind` is 1).

The game turns actions off for the tutorial (`fn_0046F750`). With
`ControlMap + 0x652C` at 0, the actions `0x03`, `0x04` and `0x06` to `0x13`
do nothing. With `+0x6530` at 0, the actions `0x11` to `0x13` do nothing. A
ring item for an action that is off is dim.

## 3. The fixed keys [V]

These keys are in the code of `GGame::ProcessKey`. The player cannot change
them. If the player gives one of them to an action of section 2, the action
wins.

| Key | What the game does | With the controllers |
| --- | --- | --- |
| Esc | Opens the game menu. Ends a cut scene that can end. Closes a dialog box. In the Temple: back to the hall, and from the hall out of the Temple | The left menu button, and Y of the left controller with `button_layout=0` [D]. In the Temple also the ring item `leave` [R] |
| F2 | Asks "Are you sure you want to go to the Gods' Playground?", then goes there (single player) | Ring (ini), `playground` [R] |
| P | Pause on and off (`PauseGame` `0x0054AE20`, single player) | Ring, `pause`, page 2 [R] |
| Alt 1, Alt 2 | The game speed: half as fast (0.5 at the least), two times as fast (2.0 at the most). `GGame::SetSpeed` `0x005537F0` | Ring (ini), `slower`, `faster` [R] |
| 1 to 7 | Go to the bookmark of that number (`fn_0043A070`) | Ring, `mark_go`: the next bookmark (Ctrl Tab). No way to a bookmark by its number |
| Ctrl with 1 to 7 | Set the bookmark at the place of the Hand, or on the object under it (`fn_00439E50`) | Ring, `mark_set`, page 2: the first bookmark that is not set [R] |
| Shift with 1 to 7 | Follow the object of the bookmark (`fn_0043A390`) | No way |
| Ctrl Shift with 1 to 7 | Remove the bookmark | No way |
| Tab | Go to the next town of the player (`fn_0043AA60`) | Ring, `town` [R] |
| Ctrl Tab, Shift Tab | Go to the next or the previous bookmark | Ring, `mark_go`, for the next one [R]. No way to the previous one |
| Backspace | Go back to the place of before the last move to a bookmark | Ring, `mark_back`, page 2 [R] |
| R | Do the last miracle again (`fn_005D0410`). The readme: "same as using the repeat Gesture" | B and the gesture [D]. No key |
| M | Start the choice of a miracle (`fn_005D03D0`). The readme: "same as using the Miracle Gesture" | B and the gesture [D]. No key |
| Ctrl Alt T | Saves a snapshot of the Creature | No way. A small thing |
| Enter of the number pad | The help text goes away, as with a click (`HelpSystem::ProcessInterface` `0x005C69B0`) | A click does the same |
| Ctrl Shift, held | The camera goes near the Hand (the readme) | Not needed |

The bookmarks act only while `0x009CD384` is not 0 and the two flags of the
control map are on. A bookmark does nothing in the Temple.

No other code reads a key by its number. A scan of the code for the address
of each byte of the key table found only: the modifier keys in
`BindableAction::IsKeyDown` `0x0046F3C0`, the keys A and Z in
`InnerCamera::UpdateMain` (section 6), the keys 1 to 4 in
`FallingSpell::Draw` (a test), Space in a recorder (`fn_005DB4D0`) and the
Enter key above.

## 4. The mouse

| What the player does | What the game does | With the controllers |
| --- | --- | --- |
| Hold the left button on the land and move | The land moves | The grip of the other hand (the God view), the trigger (the other modes) [D] |
| Double click the left button | The camera flies to that place or object | A, the teleport [D] |
| The right button on an object | Take, drop, throw, collect, stroke, slap | Grip [D] |
| A short right click (a tap) | A scroll starts, a house is knocked on, a miracle icon gives its miracle | Grip, short press [D] |
| Move the mouse in the shape of a gesture, with no button held | A gesture: a miracle, the kind of the leash | B, held, and a draw in the air [D] |
| The wheel | Zoom | Stick up and down [D] |
| The middle button, held | Turn and tilt about the Hand | Stick left and right, in steps [D] |
| The pointer at the side of the screen | Turn, tilt | Not needed |
| Both buttons, held | Zoom and turn with the mouse | Both grips [D] |
| Double click the right button on an object, with the leash on | The leash is tied to the object | Trigger two times [D] |
| A click on a button of a menu or a dialog box | The button | The ray of the pointing hand and the trigger [D] |

## 5. What the player clicks in the world

A tap is a short Action click. The game calls `InterfaceTap` of the object.

| Thing | What a click does | With the controllers |
| --- | --- | --- |
| The door of the Temple (`CitadelEntrance::InterfaceTap` `0x00468EF0`) | `GoInsideCitadel(0)`: the player is in the hall of the Temple. In a single player game the tap is valid only after the story turns it on | Grip, short press, with the point of the Hand at the door [R] (`village.md`) |
| The three posts of the leash at the Temple (`LeashObj::InterfaceTap` `0x00464490`) | Sets the kind of the leash (packet `0x65`) | Grip, short press, with the point of the Hand at a post (`temple_taps=1`) [R] (`village.md`) |
| A gold or silver scroll, a signpost (`ScriptHighlight::InterfaceTap` `0x0070AC70`) | Starts the story, the challenge or the text | Grip on the scroll, and from far [D] |
| A house (`Abode::InterfaceTap`) | Knocks: the villagers come out | Grip, short press [D] |
| A miracle icon of a Worship Site (`SpellIcon::InterfaceTap` `0x00726430`) | The miracle is in the Hand | Grip, short press [R] (`village.md`, `miracles.md`) |
| A one-off miracle (a bubble), a reward | Takes it, or uses it | Grip and trigger [D] |
| The Village Store, a field, a fish farm | Take or put food and wood | Grip, held [D]. To put: open the grip with the wood or the food at the store [R] (`village.md`) |
| The Workshop and a scaffold | Put wood in. Take a scaffold, put one on a second one, put it on the land | Grip, as for each object [D]. A short press takes a scaffold of two apart, and a scaffold that the grip lets go at a second one joins it [R] (`village.md`) |
| The totem of the Village Centre | Pull it up or down: how many villagers worship | Grip, held: the pole follows the real hand one to one, at the Hand or from a distance, until the grip opens [R]. The town must have a Worship Site (`village.md`) |
| A villager, an animal, a tree, a rock | Take, put down, throw | Grip [D] |
| The Creature | Stroke, slap, the leash | Grip on the Creature [D] (`creature.md`) |
| A scoreboard of a challenge | Text only | Nothing to do |
| A toolbar | The game has no toolbar on the screen | Nothing to do |

## 6. In the Temple

`GoInsideCitadel` sets `g_game + 0x205A28` to 1, pauses a single player game,
and starts the engine of the Temple with a room. The rooms
(`TempleRoomsEnum`): 0 the hall, 1 the Creature Room, 2 the Challenge Room,
3 the room of the multiplayer game, 4 the Options Room, 5 the Save Game Room,
6 the Library.

In a room the pointing hand points with a ray, and the trigger and the grip
both click (controllers.md, section 11).

| What the player does | What the game does | With the controllers |
| --- | --- | --- |
| A click on a door of the hall | The camera of the hall goes to the door, and `Temple::Update` `0x00793EE0` changes the room when the camera is there (controllers.md, 11.1) | Ray on the door and a click: the player is in that room [R] |
| F4 to F9 | `fn_00794900`: goes to that room from each room (`Temple::GoToRoom` `0x00793D90`) | The ring of the Temple (section 2, section 8) [R] |
| Esc | In a room: back to the hall. In the hall: `GGame::LeaveInsideCitadel` `0x00553A70`, and the pause goes back to what it was | The left menu button, or the ring item `leave` [R] |
| A click on a picture, a slider, a check box or a line of a room | The save games, the options, the key map, the challenges, the statistics of the Creature | Ray and a click. Seen: a pad of the pool, a scroll, a picture and the boxes of the Save Game Room. The Challenge Room, the Library and the controls of the Options Room are not proven [?] |
| The keys A and Z, held | The camera of the room moves forward and back (`InnerCamera::UpdateMain` `0x00795CE0`) | No way. Not needed. The stick of the pointing hand turns the player in the room by one step |
| A text line (the name of a save game) | The keyboard | No way. It needs a keyboard |

The game draws a room from a camera of its own, and the God view stands aside
for it. The first visit of the Temple and of each room plays a guide of the
game with the cinema bars. The game takes no key in the Temple then
(`GGame::ProcessKey`), and the ring does not open.

## 7. What a player cannot do with the controllers

These functions have no way with the controllers. A player needs the keyboard
for each of them.

1. **A bookmark by its number**, **follow a bookmark** (Shift and the
   number), **remove a bookmark** (Ctrl Shift and the number), and **go to
   the previous bookmark** (Shift Tab). The ring sets a bookmark, goes to the
   next one and goes back.
2. **Text:** the name of a save game, and the talk line of a multiplayer game.
   These need a keyboard. With the controllers a save has an empty message.
3. **The snapshot of the Creature** (Ctrl Alt T).
4. **The keys R and M.** The gestures do the same, so nothing is lost.
5. **The Challenge Room, the Library and the Options Room** are open, but the
   ray in them is not proven.

The functions below have a ring item, but the item is not on a default page.
The player puts the word in a page with `radial_menu_items`,
`radial_menu_items2` or `radial_menu_items_temple`: `land` (F3), `slower` and
`faster` (Alt 1, Alt 2), `details` (S), `load` (Ctrl L), `playground` (F2),
`leash_next` and `leash_previous`.

The left controller has these jobs: the grip moves the land, Y opens the
ring, the stick moves the player in the God view, and the menu button sends
Esc. X and the trigger have no job. `button_layout=0` gives the layout with X
and the stick for the ring and Y for Esc (`controllers.md`, 5.1).

## 8. What the ring gives

| Function that needed the keyboard | On the ring |
| --- | --- |
| The Temple | `temple` (Space two times), `enter` (F4) |
| The rooms | The ring of the Temple: `room_creature`, `room_challenge`, `room_save`, `room_options`, `room_library`, `hall`, `leave`. The Creature Room, the Challenge Room, the Save Game Room and the Library were chosen in runs [R]. The Options Room is the same call with a different room |
| The Creature | `creature`. In the God view it is one teleport to the Creature, because the key of the game starts a follow camera |
| The kind of the leash | `leash_learning`, `leash_compassion`, `leash_aggression` |
| Bookmarks | `mark_set`, `mark_go`, `mark_back` on page 2. Follow and remove are not on the ring |
| The next town | `town` |
| The whole land | `land`, by the ini. In the God view the view goes to the middle of the land at the scale of now |
| Pause and speed | `pause` on page 2. `slower` and `faster` by the ini |
| The help | `help` on page 2 [R]: the key went out. What the help showed was not read |
| Names and details | `names` on page 2. `details` by the ini |
| Quick save and load | `save` on page 2. `load` by the ini. Not run: a test must not write a saved game |
| The Gods' Playground | `playground`, by the ini [R]: the dialog box of the game came up |
| R and M | Not on the ring. The gestures do the same |
| Text | Not possible with no keyboard |

In the Temple:

- **A door of the hall.** The pointer of the game is the place where the
  ray of the pointing hand meets the room. With the ray over the door "Save
  Game Room", a click goes to that room [R].
- **Esc in the Temple.** In a room it goes to the hall, and in the hall it
  leaves the Temple. The pause goes back to what it was, and the God view is
  at its place of before [R].
- **The door of the Temple and the leash posts on the land.** Both are
  targets of the grip (`village.md`).
- **The rooms** have a ray pointer, with the buttons and the camera rules in
  controllers.md, section 11.
