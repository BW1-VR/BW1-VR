# The controls of the plain game, and what the controllers can do

An audit for the package "controls" (2026-10-04). Buck asked: "Check
vanilla game controls too, make sure we can do every action and interaction
with VR controllers, left controller is basically unused. Things like
entering the temple for example, I don't think we can do that."

This file lists each control of the plain game: each bindable action, each
fixed key, each mouse action, and each thing that the player clicks in the
world to reach a function. For each one it says what the game does and how
the player does it with the controllers today. Section 7 is the list of what
is missing. Section 8 has the runs.

Marks: **[V]** read in the disassembly of `runblack.exe` v1.42 in this
session. **[D]** from the docs of the mod (built and run before). **[R]** seen
in a desktop run of this package. **[?]** not proven.

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

| No. | Name in the game | Default | What the game does | With the controllers today |
| --- | --- | --- | --- | --- |
| `0x00` | Help | F1 | `HelpSystem::HelpQuery` `0x005C98E0`: help for the thing under the Hand | **No way** |
| `0x01` | Move | left button | Drags the land. A double click goes to that place | Trigger. In the God view the left grip moves the land and A is the teleport [D] |
| `0x02` | Action | right button | Take, drop, throw, tap, stroke, cast | Grip of the pointing hand [D] |
| `0x03`, `0x04` | Zoom Out, Zoom In | wheel | The camera zooms | Stick up and down [D] |
| `0x05` | Talk | T | Opens the text line of a multiplayer game | No way. It needs a keyboard for the text |
| `0x06` | Zoom On | right Ctrl, held | The mouse and the arrow keys zoom | Not needed: the stick and the two grips zoom |
| `0x07` to `0x0A` | Move Left, Right, Forwards, Backwards | arrow keys | The camera moves | Left grip, and A [D] |
| `0x0B`, `0x0C` | Tilt Up, Tilt Down | A, Q | The camera tilts. In the God view the land moves up or down [D] | Left grip, pull up or down [D] |
| `0x0D`, `0x0E` | Rotate Left, Rotate Right | Z, scan code `0x16` | The camera turns | Stick left and right [D] |
| `0x0F` | Tilt/Rotate On | right Shift, held | The mouse and the arrow keys tilt and turn | Not needed |
| `0x10` | Tilt/Rotate Around Mouse On | middle button (the key 0 with no wheel mouse) | The mouse turns the camera about the Hand | Stick left and right. The mod hides this button from the game [D] |
| `0x11` | Zoom To Temple | Space | `fn_0064A7A0(0)`: `CameraModeNew3::ZoomToCitadel` `0x00457B60`. One press pulls the camera back to a safe view. A second press in 500 ms goes to the Temple. Two more presses go back | **No way** |
| `0x12` | Zoom To Creature | C | A `CameraModeFollow` on the Creature of the player. A second press ends it | **No way** |
| `0x13` | Zoom To Realm | F3 | `fn_0064A7A0(1)`: the camera goes up to see the whole land | **No way** (zoom steps come near) |
| `0x14` | Zoom To Inside Temple | F4 | `GGame::GoInsideCitadel(0)` `0x00553E10` | The ring item `enter`. The door of the Temple is a tap of the grip [R, section 8] |
| `0x15` | Zoom To Creature Room | F5 | `GoInsideCitadel(1)` | **No key** |
| `0x16` | Zoom To Challenge Room | F6 | `GoInsideCitadel(2)` | **No key** |
| `0x17` | Zoom To Save Game Room | F7 | `GoInsideCitadel(5)` | **No key** |
| `0x18` | Zoom To Options Room | F8 | `GoInsideCitadel(4)` | **No key** |
| `0x19` | Zoom To Library | F9 | `GoInsideCitadel(6)` | **No key** |
| `0x1A` | Leash/UnLeash Creature | L | Packet `0x5F`: the leash goes on or off | A short press of the grip on the Creature, the grip with the hand pointed at it, and a shake to take it off [D] |
| `0x1B` | Show Villager Names | N | Changes bit `0x20000` of `g_game + 0x14` | **No way** |
| `0x1C` | Show Villager Details | S | Changes the details of the villager under the Hand, or bit `0x200000` for all | **No way** |
| `0x1D` | Quick Save | Ctrl S | Packet `0x61`, not in a multiplayer game | **No way** |
| `0x1E` | Quick Load | Ctrl L | Packet `0x62` | **No way** |
| `0x1F` | Previous Leash | V | Packet `0x65`: the kind of the leash goes 1, 2, 3, 1 | The ring items of the leash. The gestures of the leash (B and a draw), and a tap of the grip on a post at the Temple [R, section 8] |
| `0x20` | Next Leash | B | The same, 1, 3, 2, 1 | The same |

The kinds of the leash are 1 the Leash of Aggression, 2 the Leash of
Learning and 3 the Leash of Compassion (`LEASH_TYPE`). A kind is in the
cycle only when the Creature has it (`Creature + 0x1108 + 4 * kind` is 1).

The game turns actions off for the tutorial (`fn_0046F750`). With
`ControlMap + 0x652C` at 0, the actions `0x03`, `0x04` and `0x06` to `0x13`
do nothing. With `+0x6530` at 0, the actions `0x11` to `0x13` do nothing.

## 3. The fixed keys [V]

These keys are in the code of `GGame::ProcessKey`. The player cannot change
them. If the player gives one of them to an action of section 2, the action
wins.

| Key | What the game does | With the controllers today |
| --- | --- | --- |
| Esc | Opens the game menu. Ends a cut scene that can end. Closes a dialog box. In the Temple: back to the hall, and from the hall out of the Temple | Y and the left menu button [D] |
| F2 | Asks "Are you sure you want to go to the Gods' Playground?", then goes there (single player) | **No way** |
| P | Pause on and off (`PauseGame` `0x0054AE20`, single player) | **No way** |
| Alt 1, Alt 2 | The game speed: half as fast (0.5 at the least), two times as fast (2.0 at the most). `GGame::SetSpeed` `0x005537F0` | **No way** |
| 1 to 7 | Go to the bookmark of that number (`fn_0043A070`) | **No way** |
| Ctrl with 1 to 7 | Set the bookmark at the place of the Hand, or on the object under it (`fn_00439E50`) | **No way** |
| Shift with 1 to 7 | Follow the object of the bookmark (`fn_0043A390`) | **No way** |
| Ctrl Shift with 1 to 7 | Remove the bookmark | **No way** |
| Tab | Go to the next town of the player (`fn_0043AA60`) | **No way** |
| Ctrl Tab, Shift Tab | Go to the next or the previous bookmark | **No way** |
| Backspace | Go back to the place of before the last move to a bookmark | **No way** |
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

| What the player does | What the game does | With the controllers today |
| --- | --- | --- |
| Hold the left button on the land and move | The land moves | Left grip (the God view), trigger (the other modes) [D] |
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

| Thing | What a click does | With the controllers today |
| --- | --- | --- |
| The door of the Temple (`CitadelEntrance::InterfaceTap` `0x00468EF0`) | `GoInsideCitadel(0)`: the player is in the hall of the Temple. In a single player game the tap is valid only after the story turns it on | Grip, short press, with the Hand's point at the door [R, section 8] |
| The three posts of the leash at the Temple (`LeashObj::InterfaceTap` `0x00464490`) | Sets the kind of the leash (packet `0x65`) | Grip, short press, with the Hand's point at a post (`temple_taps=1`) [R, section 8] |
| A gold or silver scroll, a signpost (`ScriptHighlight::InterfaceTap` `0x0070AC70`) | Starts the story, the challenge or the text | Grip on the scroll, and from far [D] |
| A house (`Abode::InterfaceTap`) | Knocks: the villagers come out | Grip, short press [D] |
| A miracle icon of a Worship Site (`SpellIcon::InterfaceTap` `0x00726430`) | The miracle is in the Hand | Grip, short press [D for a bubble]. For the icon: [R], runs vilD to vilG of the package "village" (hand.md, "The village with the real hand") |
| A one-off miracle (a bubble), a reward | Takes it, or uses it | Grip and trigger [D] |
| The Village Store, a field, a fish farm | Take or put food and wood | Grip, held [D]. To put: open the grip with the wood or the food at the store [R, run vilP1 of the package "village"] |
| The Workshop and a scaffold | Put wood in. Take a scaffold, put one on a second one, put it on the land | Grip, as for each object [D]. A short press takes a scaffold of two apart, and a scaffold that the grip lets go at a second one joins it [R, run vilL of the package "village"] |
| The totem of the Village Centre | Pull it up or down: how many villagers worship | Grip, held: the pole follows the real hand one to one [R, run vilC of the package "village"]. The town must have a Worship Site |
| A villager, an animal, a tree, a rock | Take, put down, throw | Grip [D] |
| The Creature | Stroke, slap, the leash | Grip on the Creature [D] |
| A scoreboard of a challenge | Text only | Nothing to do |
| A toolbar | The game has no toolbar on the screen | Nothing to do |

## 6. In the Temple

`GoInsideCitadel` sets `g_game + 0x205A28` to 1, pauses a single player
game, and starts the engine of the Temple with a room. The rooms
(`TempleRoomsEnum`): 0 the hall, 1 the Creature Room, 2 the Challenge Room,
3 the room of the multiplayer game, 4 the Options Room, 5 the Save Game
Room, 6 the Library.

| What the player does | What the game does | With the controllers today |
| --- | --- | --- |
| A click on a door of the hall | `Temple::GoToRoom` `0x00793D90` | Not proven [?] |
| F4 to F9 | `fn_00794900`: goes to that room from each room | **No key** |
| Esc | In a room: back to the hall. In the hall: `GGame::LeaveInsideCitadel` `0x00553A70`, and the pause goes back to what it was | Y [D]. Not proven in a run [?] |
| A click on a picture, a slider, a check box or a line of a room | The save games, the options, the key map, the challenges, the statistics of the Creature | Not proven [?] |
| The keys A and Z, held | The camera of the room moves forward and back (`InnerCamera::UpdateMain` `0x00795CE0`) | No way. Not needed |
| A text line (the name of a save game) | The keyboard | No way. It needs a keyboard |

The game draws a room from a camera of its own, and the God view stands
aside for it [D, god-view.md, "The Temple rooms": not tested]. So this
package must prove in a run that the view and the pointer are right there.

## 7. What a player cannot do with the controllers today

The plain game needs the keyboard for each of these. They are in the order
of how often a player needs them.

1. **Go to the Temple** (Space), and **go into the Temple** (F4, or a tap on
   its door, which is not proven).
2. **Go to a room of the Temple** (F5 to F9): the Creature Room, the
   Challenge Room, the Save Game Room, the Options Room, the Library. Inside
   the Temple, the doors and the things of each room are not proven.
3. **Go to the Creature** and follow it (C).
4. **The kind of the leash** by a key (V and B). The gestures and the posts
   at the Temple are the other ways.
5. **Bookmarks**: set (Ctrl and a number), go (the number), follow (Shift
   and the number), remove, the next and the previous one (Ctrl Tab, Shift
   Tab), and back (Backspace).
6. **Go to the next town** (Tab).
7. **See the whole land** (F3).
8. **Pause** (P) and **the game speed** (Alt 1, Alt 2).
9. **The help of the game** for the thing under the Hand (F1).
10. **The names of the villagers** (N) and **the details of a villager** (S).
11. **Quick save and quick load** (Ctrl S, Ctrl L).
12. **The Gods' Playground** (F2).
13. The keys R and M. The gestures do the same, so nothing is lost.
14. Text: the name of a save game, and the talk line of a multiplayer game.
    These need a keyboard.

The left controller has three inputs with no job: the stick, X and the
trigger.

## 8. Runs, and what the ring gives (2026-10-04, night)

Sections 2 to 7 are the state of before the ring. The ring of the hand that
does not point is in [controllers.md](controllers.md), section 10. It writes
the key of an item into the key buffer of the game (section 1), so the game
does what it does for the key.

The desktop runs ctl1 to ctl6 used the fake controller with the game window
behind. The table says what each one proved. The numbers are in
controllers.md, 10.7.

| No. in section 7 | On the ring | Proof |
| --- | --- | --- |
| 1. The Temple | `temple` (Space two times), `enter` (F4) | [R] ctl1, ctl4, ctl6: the God view goes to the Temple, and the player is in the hall |
| 2. The rooms | The ring of the Temple: `room_creature`, `room_challenge`, `room_save`, `room_options`, `room_library`, `hall`, `leave` | [R] ctl2, ctl3: the Creature Room, the Save Game Room, back to the hall, and out. The Challenge Room, the Options Room and the Library were not chosen in a run. They are the same call with a different room |
| 3. The Creature | `creature`. In the God view it is one teleport to the Creature, because the key of the game starts a follow camera | [R] ctl4, ctl6 |
| 4. The kind of the leash | `leash_learning`, `leash_compassion`, `leash_aggression` | [R] ctl4: kind 2 to 3 to 1 |
| 5. Bookmarks | `mark_set`, `mark_go`, `mark_back` on page 2. Follow and remove are not on the ring | [R] ctl5, ctl6 |
| 6. The next town | `town` | [R] ctl4 |
| 7. The whole land | `land`, by the ini. In the God view the view goes to the middle of the land at the scale of now | [R] ctl5 |
| 8. Pause and speed | `pause` on page 2. `slower` and `faster` by the ini | [R] ctl3, ctl5 |
| 9. The help | `help` on page 2 | [R] ctl5: the key went out. What the help showed was not read |
| 10. Names and details | `names` on page 2. `details` by the ini | [R] ctl5 |
| 11. Quick save and load | `save` on page 2. `load` by the ini | Not run: a test must not write a saved game |
| 12. The Gods' Playground | `playground`, by the ini | [R] ctl5: the dialog box of the game came up |
| 13. R and M | Not on the ring. The gestures do the same | |
| 14. Text | Not possible with no keyboard | |

What the runs showed for the rows of sections 5 and 6 that were not proven:

- **A door of the hall of the Temple** [R, ctl3]. The pointer of the game is
  the place where the player sees the point of the real hand. With that
  point over the door "Save Game Room", the grip went to that room. The
  trigger did not.
- **Esc in the Temple** [R, ctl3]. In a room it goes to the hall, and in the
  hall it leaves the Temple. The pause goes back to what it was, and the God
  view is at its place of before.
- **The first visit of the Temple and of each room** plays a guide of the
  game with the cinema bars. The game takes no key in the Temple then
  (`GGame::ProcessKey`), and the ring does not open.
- **The door of the Temple and the leash posts on the land** [R, runs tland1
  and tland2 of 2026-10-05]. The door was a target of the grip before. A
  post was not, and it is one now (hand.md, "The door of the Temple and the
  leash posts").
- **Proven later** by the package "village" (hand.md, "The village with the
  real hand"): the totem of the Village Centre, the miracle icon of a
  Worship Site, the gestures that ask for a miracle, a power up, the
  power that goes back at a cancel, a tap on a scaffold, and two scaffolds
  that join.
- **The rooms of the Temple** have a ray pointer from the night of
  2026-10-04: controllers.md, section 11. It has what each room holds, and
  the runs of a door, a pad of the pool, a scroll and the way out.
