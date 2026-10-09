# The look and the feel of the hands

This doc is about how the hands look and feel. It has these parts:

- The files, the poses and the look of the game's own Hand ("The model of the
  Hand").
- The mod's hands: the game's Hand mesh on both real hands ("The game's hand
  on both real hands"), and a mesh that the player edited ("A hand that the
  player edited").
- Fingers that react to the world ("Hands that react to the world").
- The glow at the Hand's point ("The glow at the Hand's point").
- The renderer that draws the mod's own meshes in the game's frame ("Overlay
  renderer").
- The pulses of the controllers ("Haptics").

The game's Hand itself (states, the point, the exact place) is in
[hand.md](hand.md). The grip that holds is in [grab.md](grab.md). The Creature
is in [creature.md](creature.md). Clicks on things are in
[village.md](village.md). [research-hla.md](research-hla.md) has the facts
about Half-Life: Alyx that this doc uses.

| Subject | Source files in `bwvr/src/mod` |
| --- | --- |
| The mod's hands | `hand_model.cpp`, `hand_model_math.h`, `hand_model_file.cpp` |
| Poses, curls, the overlay | `overlay.cpp`, `overlay_math.h` |
| Fingers and the world | `hand_feel.cpp`, `hand_feel_math.h` |
| The glow | `hand_glow.cpp` |
| Pulses | `haptics.cpp`, `haptics_math.h` |
| The export tool | `bwvr/tools/hand_export/` |

The rule for all of it: the Hand is the real hand, 1:1 in size and place. The
mod changes only how the fingers bend. It never moves or turns a hand, and it
makes no motion that the player did not make.

## The model of the Hand

This section has the files of the game's Hand, how the game poses the Hand and
changes its look, and the export tool. The reader of the files is
`tools/hand_export/bw_hand.py`. A fact with [V] is from the disassembly or
from the bytes of the files. The pose math agrees with the bones that the
mod measured in the game (hand.md, "The exact Hand").

### The files of the Hand [V]

| File | What it holds |
| --- | --- |
| `Data\CreatureMesh\Hand_Boned_Base2.l3d` | The base mesh, the bones and the base texture |
| `Data\CreatureMesh\Hand_Boned_Good2.l3d` | The good mesh and the good texture |
| `Data\CreatureMesh\Hand_Boned_Evil2.l3d` | The evil mesh and the evil texture |
| `Data\CTR\hh.HBN` | The names of the three meshes and the 26 animations |
| `Data\hndspec5.txt` | The 69 animation names of the table in `hh.HBN` |

`Data\HandDemo\*.hnd` (16 files) are not part of the model. Nobody has read
them.

**The L3D file** (138,158 bytes, the three have the same layout):

| Offset | What |
| --- | --- |
| `0x00` | `L3D0`, the flags `0x00022316`, 7,082 bytes of mesh data, 1 submesh, the submesh table at `0x4C` |
| `0x34` | After 32 bytes of 0: `0xFFFFFFFF`, 1 texture, the texture table at `0x1BA6`, 0 points |
| `0x50` | The submesh: the flags `0xE0000801`, 1 primitive, the primitive table at `0x58C`, 22 bones at `0x64` |
| `0x64` | 22 bones of 60 bytes |
| `0x590` | The primitive: 12 words. The material `4` and `0x5F650400` (`0x5F450400` in the good and the evil file), the id of the texture, one more word, then 141 vertices at `0x5C0`, 169 triangles at `0x1760`, 20 bone groups at `0x1B56` and 0 vertex blends |
| `0x5C0` | 141 vertices of 32 bytes |
| `0x1760` | 169 triangles of three 16 bit indices |
| `0x1B56` | 20 bone groups of 4 bytes |
| `0x1BAA` | The texture: its id, then 256 by 256 texels of 16 bits |

- **A vertex** is a position, a texture point and a normal (8 floats). The
  position and the normal are in the frame of the bone of the vertex.
- **A bone** is the parent, the first child and the next bone (three ints,
  -1 for none), then a matrix of 4 rows of 3 in the frame of the parent. A
  point is a row: `point * matrix`. A bone in the frame of the mesh is
  `bone * parent`. The wrist matrix holds a mirror, so each bone in the frame
  of the mesh has the determinant -1. The length of a bone is along its own
  z, and a finger bends about its own x.
- **A bone group** is a count of vertices and a bone (two 16 bit words). The
  groups cover the vertices in order. So each vertex has one bone, and there
  are no weights.
- **A texel** has 4 bits each for alpha, red, green and blue. The alpha is 1
  on the hand and goes to 0 at the wrist (9,758 texels are below 1), so the
  wrist fades.
- 141 vertices are at 88 places: a seam of the texture has two vertices at
  one place. Two vertices at one place always have the same bone.
- Each triangle has the same turn: `(v1 - v0) x (v2 - v0)` is along the
  normals in the stored frame.
- The texture points are the same in the three files. The bones are the
  same. The good mesh has the vertices and the normals of the base mesh. The
  evil mesh moves a vertex by up to 47.9 units (14.6 mm on a hand of 19 cm):
  longer fingers with a point.

**The bones.** The names are ours: the game has none.

| Bone | Parent | Vertices | Bone | Parent | Vertices |
| --- | --- | --- | --- | --- | --- |
| 0 wrist | | 17 | 11 little 1 | 6 | 5 |
| 1 attach | 0 | 0 | 12 little 2 | 11 | 5 |
| 2 palm | 0 | 0 | 13 little 3 | 12 | 7 |
| 3 thumb 1 | 2 | 3 | 14 ring 1 | 5 | 5 |
| 4 index 0 | 2 | 8 | 15 ring 2 | 14 | 5 |
| 5 ring 0 | 2 | 6 | 16 ring 3 | 15 | 7 |
| 6 little 0 | 2 | 10 | 17 index 1 | 4 | 5 |
| 7 middle 0 | 2 | 6 | 18 index 2 | 17 | 6 |
| 8 middle 1 | 7 | 5 | 19 index 3 | 18 | 9 |
| 9 middle 2 | 8 | 5 | 20 thumb 2 | 3 | 13 |
| 10 middle 3 | 9 | 7 | 21 thumb 3 | 20 | 7 |

Bone 0 of a finger is in the palm, from the root of the fingers to the
knuckle. Bone 1 (attach) has no vertices. Only the animations `horn` and
`phile` move it, so it is the place of an object that those poses hold.

**The HBN file** (21,680 bytes). `LiOnHeAd`, the segment name `Hand` in 32
bytes and the segment size, then the data at `0x2C`. Each offset in the data
counts from `0x2C`.

1. `CHand::LoadBinary` `0x0046D2C0` reads the hand version (0) and the data
   version (5). The data version names the spec file, `Data\hndspec5.txt`.
2. `Morphable::ReadBinary` `0x00617AE0` reads its version (6), the name of
   the base mesh in 32 bytes, and six more names of 32 bytes. The first is
   the evil mesh (mesh 2) and the second the good mesh (mesh 3). The other
   four are empty: only a Creature has them.
3. For each mesh with a name, `LH3DAnimSet::ReadBinaryNew` `0x00619B80`
   reads one offset for each of the 69 animations of the spec file (0 for
   none), and one more offset: the end of the animations of the set. The
   base set has 26 animations. The evil set and the good set have none, and
   the game then copies those of the base set (`Morphable::GetSetAnim`
   `0x00619690`).
4. One word for `+0x4830`, the count of hair groups (0), and the events of
   the animations (`Morphable::ReadExtraDataBinary` `0x00617EE0`).

`Morphable::LoadBase` `0x00618360` makes the base mesh two times: mesh 0 is
the mesh that the game draws and changes, and mesh 1 keeps the base. It also
makes three arrays of one matrix for each bone: the pose `+0x47F0`, the rest
pose in the frame of the mesh `+0x47F4`, and the inverse of the rest pose
`+0x47F8`.

**The spec file.** A line that starts with `=` names a group. Each other
line is a type letter and a name. `C` is a full animation. `L` is a lean.
`E` ends the file.

**An animation** (`CAnim::ReadBinary` `0x00860860`, `CFrame::ReadBinary`
`0x00860C30`):

| What | Type |
| --- | --- |
| The duration | 1 word. 266, 421, 526 or 533, most likely in ms [I] |
| The flags | 1 word. Bit 0: the animation loops |
| Five floats | 0 in each full animation. Eight of the ten leans have values there. Their use is not known |
| The count of frames | 1 word, 2 to 5 |
| The count of bones | 1 word, 22 |
| The count of bones with a turn, and with a place | 2 words |
| The bones with a turn, then the bones with a place | 1 word each |
| Each frame | 3 angles for each bone with a turn, then 3 floats for each bone with a place |

### How the game poses the Hand [V]

`fn_00860E00` (thiscall on the animation) samples an animation into the pose
array. Its arguments are the pose array, the mesh, the rest pose `+0x47F4`,
the inverse rest pose `+0x47F8`, the time, the stand frame, a map of bones
for the mirror, and a flag for the fill.

- **The frame.** `frame = count * time / total`. For an animation that loops,
  `total` is the duration, and the last frame blends back to frame 0. For
  the others `total = duration * count / (count - 1)`, so the last frame is
  at the duration.
- **The turn.** The three angles `(x, y, z)` of a frame make a matrix with
  `LHMatrix::SetYXZMatrixOnly` `0x007FAC10`, called with `(y, x, z)`. The
  game blends the matrices of two frames element by element and makes each
  row one long again (`fn_007FB5C0`). The turn is a turn of the bone from
  its rest pose, about the axes of the mesh:
  `local turn = rest of the bone * turn * inverse rest of the parent`.
- **The place.** The three floats of a frame are the place of the bone in
  the frame of its parent.
- **The fill.** A bone with no entry in the animation takes the turn and the
  place of the stand frame. The stand frame is frame 0 of animation 0. It
  has each bone, and its places are the places of the L3D bones. It is not
  the rest pose: the middle, ring and little fingers bend in it.
- **The lean.** `fn_00861EE0` adds a lean to a pose that is there:
  `local turn = old turn * rest of the parent * inverse(turn of the stand
  frame) * turn of the lean * inverse rest of the parent`, and the place
  moves by the place of the lean minus that of the stand frame. The inverse
  is `fn_007FACC0` with the angles negated [I].

Then `fn_00839F10` composes the bones with the root (hand.md, "How the game
draws the Hand"), and each vertex goes through the matrix of its bone.

**The animations of the Hand.** The number is the index in the table, and
the mod's logs give it (`Morphable::GetAnim`).

| Number | Name | Type | Number | Name | Type |
| --- | --- | --- | --- | --- | --- |
| 0 | wiggle | C | 27 | tickle | C |
| 1 | wiggle_lr | L | 33 | slap | C |
| 2 | wiggle_fb | L | 34 | slap_lr | L |
| 3 | point | C | 42 | grip | C |
| 6 | hold_above | C | 43 | grip_lr | L |
| 9 | hold_side | C | 44 | grip_fb | L |
| 12 | can_pickup | C | 45 | rotate | C |
| 13 | can_pickup_lr | L | 48 | pitch | C |
| 14 | can_pickup_fb | L | 51 | zoom | C |
| 15 | hold_fingers | C | 57 | tap_house | C |
| 16 | hold_fingers_lr | L | 60 | horn | C |
| 17 | hold_fingers_fb | L | 63 | phile | C |
| 24 | stroke | C | | | |
| 25 | stroke_lr | L | | | |

The other 43 names of the spec file have no animation in the file. The pose
math agrees with bones measured in the game. Frame 0 of `wiggle` puts the
last joint of the middle finger at `-124.7 -28.4 21.4` (measured in the
Normal state: `-124.8 -28.5 21.7`). `grip` puts the wrist at
`-193.7 -487.8 -136.0` (measured in the land drag: `-193.7 -487.6 -136.0`).
`hold_side` puts the wrist at `68.9 -272.7 177.1` in frame 0 and at
`85.0 -477.6 197.1` in frame 1 (measured in the Holding state:
`69.9 -285.4 178.4`).

### How the game changes the look [V]

`CHand::PrepareForDrawing` writes the player's alignment to `+0x9C`, from -1
(evil) to 1 (good). `Morphable::UpdateMorphing` `0x00618C40` acts when it
moved by 0.03 or more since the last time:

- **The vertices** (`Morphable::MorphVertices` `0x00618D10`). Below 0 the
  other mesh is mesh 2 (evil) and the weight is minus the alignment. From 0
  it is mesh 3 (good) and the weight is the alignment. Each position and
  each normal of mesh 0 becomes `base + (other - base) * weight`, and the
  normal is made one long again. The good mesh has the vertices of the base
  mesh, so only an evil hand changes its form.
- **The texture** (`Morphable::MorphTexture` `0x00619500`). The same choice
  of the other mesh. The weight is the alignment times 256 as an integer, at
  most 255. `fn_00870640` mixes each 4 bit channel of each texel, the alpha
  too: `(base * (255 - w) + other * w) / 255`. `CHand::MorphTexture`
  `0x0046BF80` then calls an empty function.
- **The bones** (`Morphable::MorphAnims` `0x00619100`). The rest pose is
  mixed in the same way. The three files have the same bones, so nothing
  changes for the Hand.

There is no tint and no bone scale. The look is the two textures and the
form of the evil mesh.

### The hold pose of the game [V]

`HandStateHolding::Update` `0x005B3C70` has a switch on `CHand+0x490C`, the
hold type of the object in the Hand. The case of the type 1 starts at
`0x005B4EA5`, of the type 3 at `0x005B513C` and of the types 5, 6 and 7 at
`0x005B533D`. Each object gives three values:

| Virtual function of `Object` | Slot | What it gives |
| --- | --- | --- |
| `GetHoldType` | `+0x58C` | 1 for `Object` and `Poo`. 5 for `Tree` and `DeadTree`. 6 for `MobileObject` and `FieldCrop`. 7 for `Villager` and `Animal`. A `MobileStatic` gives 6 for some of its kinds (the info numbers `0x0E`, `0x0F` and `0x31` to `0x34`, and two more tests) and 1 for the others. A `SpellSeed` gives 2, or the value of its info at `+0x158` |
| `GetHoldRadius` | `+0x590` | `Object`: 0.75 times the height for the hold type 1, and the 2D radius for each other type. `Tree` and `DeadTree`: 0.2 times the 2D radius, which is the trunk. `SpellSeed`: a value of its info |
| `GetHoldLoweringMultiplier` | `+0x594` | The share of its height that the object hangs under the Hand: 0 for `Object`, 0.1 for a tree, 0.65 for a villager and an animal, 0.7 or -0.3 for a `MobileObject`, 0.7, 0.4 or 0 for a `MobileStatic` |

The state then takes one moment of one animation. `r` is the hold radius
over the length of the Hand (3.2 times `CHand+0x4834`, `fn_0046C040` gives
the 3.2), and 1 at most:

| Hold type | Animation | The time in the animation |
| --- | --- | --- |
| 1 | `hold_above` (6) | `duration / 2 * (1 - r)`, with `r = radius / (1.2 * length)` |
| 2 | `wiggle` (0) | `duration / 2`, and the object does not hang |
| 3 | `horn` (60) | `duration / 2 * r`, with `r = radius / length` |
| 5, 6, 7 | `hold_side` (9) | `duration / 2 * r`, with `r = radius / length` |

So the game does not play a hold animation. The animation is a scale from a
closed hand to an open hand, and the size of the object chooses the place on
it. `hold_above` has 4 frames and `hold_side` has 3, and both loop, so the
frame is `2 * (1 - r)` and `1.5 * r`. In the Holding state the game composes
the animation 6 or the animation 9, and no other one.

**The game's poses as curls.** The hand model bends a finger with one curl.
This table has the curl that is nearest to the bends of each frame (least
squares over the three bones of a finger), from the thumb to the little
finger.

| Animation and frame | Thumb | Index | Middle | Ring | Little | What it is |
| --- | --- | --- | --- | --- | --- | --- |
| `hold_above`, frame 0 | 0.05 | 0.01 | 0.01 | 0.03 | 0.03 | The open hand, for a large object |
| `hold_above`, frame 1 | 0.21 | 0.01 | 0.01 | 0.18 | 0.16 | |
| `hold_above`, frames 2 and 3 | 0.84 | 0.66 | 0.58 | 0.65 | 0.64 | The claw, for a small object |
| `hold_side`, frame 0 | 0.13 | 0.99 | 0.90 | 0.83 | 0.84 | The fist with the thumb out, for a thin object |
| `hold_side`, frames 1 and 2 | 0.00 | 0.00 | 0.03 | 0.11 | 0.11 | The open hand |
| `can_pickup`, frame 0 | 0.00 | 0.00 | 0.00 | 0.10 | 0.08 | The open hand |
| `can_pickup`, frame 1 | 0.06 | 0.00 | 0.12 | 0.19 | 0.21 | |
| `can_pickup`, frame 2 | 0.62 | 0.00 | 0.24 | 0.20 | 0.10 | The thumb at the index finger, the middle finger a hook |
| `wiggle`, both frames | 0.13 | 0.00 | 0.13 | 0.15 | 0.21 | The hand of the mouse player |

The thumb of frame 0 of `hold_above` and of frames 1 and 2 of `hold_side`
bends back from its rest pose, and the thumb of `can_pickup` turns at its
base by up to 32 degrees. The hand model has no such bend, so those thumbs
are the open thumb. The index finger of `can_pickup` is straight in each
frame: it points at the thing.

### The export (`tools/hand_export`)

```
python bwvr/tools/hand_export/export.py
```

The tool reads the files of the game and writes `media\hand`, which git does
not track. The output is the art of the game: it is never committed and
never in the installer. `README_hand.txt` in the tool folder is the text of
the README of the output.

| File of the tool | What it does |
| --- | --- |
| `bw_hand.py` | Reads the L3D files, the HBN file and the spec file, and makes the pose of each frame as the game does |
| `export.py` | Writes the textures as PNG and the data as JSON, starts Blender for each step below and makes the picture sheets |
| `blender_build.py` | In Blender 4.5: makes the mesh, the bones, the skin, the shape keys and one action for each animation, and writes the `.blend`, the FBX and the GLB |
| `blender_check.py` | In the newest Blender: reads the FBX and the GLB back, compares them with the data of the game and renders the pictures |
| `blender_example.py` | In the newest Blender: makes `example\bwvr-hand.glb`, an edited hand with each triangle cut in four, with the steps of "A hand that the player edited" |

**The frame of the files.** The stored mesh is left handed, and a 3D editor
is right handed. The tool mirrors x, so the hand stays the left hand that
the game stores: `(x, y, z)` of the mesh is `(-x, y, z)` in the FBX and the
GLB (the fingers to +y, the palm to +z, the thumb to -x) and `(-x, -z, y)` in
Blender. The mirror turns the order of each triangle. The origin is the tip
of the index finger. The size is real: 623.6 mesh units are 19 cm.

**The bones in the files.** A Blender bone has its length along its own y,
and the stored bone has a mirror in it. The tool gives each bone the place
of its joint and the axes `-x, z, y` of the stored bone, so a finger bends
about the x of its bone. The names are `b00_wrist` to `b21_thumb3`: the
number is the bone of the game. For each key of an animation the tool gives
Blender the move of each bone from its rest pose in the frame of the mesh
(`inverse(rest) * pose`), so the result does not depend on the axes of a
bone.

**What a file holds.** One mesh of 141 vertices and 169 triangles with the
texture points and the normals of the game. 22 bones. One bone with the
weight 1 for each vertex. The shape keys `good` and `evil`. 26 takes, each
animation of the file, at 30 frames a second. A take that loops ends with
its first frame again. A lean is a take on the stand frame. The textures are
PNG files of 256 by 256 with alpha. `looks\` has the good and the evil hand
as separate files.

**The check** (`check_report.txt`). Blender 4.5.4 writes the files. Blender
5.1.0 reads the FBX with each of its two importers, and the GLB. The check
compares the counts (141 vertices, 169 triangles, 22 bones, 26 takes), the
place of each vertex and bone joint (the worst is under 0.0001 mm), the bone
and weight of each vertex, the texture points, the normals (the worst is
0.04 degrees), the shape key `evil` against the evil mesh, and the skinned
mesh at the 105 keys of the takes against the pose math above (0.0002 mm).
`preview\compare_game.png` puts a capture of the Hand of the mouse player
beside the take `00_wiggle` of the FBX, mirrored.

**Limits.** A key of a take is on a whole frame of 30 a second, so it can be
17 ms from the time of the game. The old FBX importer of Blender also makes
one action for the shape keys in each take. The tool does not write the
five floats of a lean.

## The game's hand on both real hands

The mod's hands are the game's own hand mesh, with the game's textures and
the game's light. The mod chooses a pose, and the trigger and the grip move
the fingers in it by how far they are pressed. The code is in
`src/mod/hand_model.cpp` (the files, the look, the light and the draw),
`src/mod/hand_model_math.h` (the pure math) and `src/mod/overlay.cpp` (the
poses, and which hands draw). The format of the files and the game's own code
are in "The model of the Hand". The poses and the rule of one pair of hands
are in hand.md, "One pair of hands".

### What the mod does

**Nothing of the game is in the mod.** At the first frame with a controller
hand, the mod reads `Hand_Boned_Base2.l3d`, `Hand_Boned_Good2.l3d` and
`Hand_Boned_Evil2.l3d` from `Data\CreatureMesh` of the game folder of the
player. It checks each offset and each count. A file that is not there or
not as the mod knows it gives one log line with the reason, and the low poly
hands stay.

**The pose.** `overlay.cpp` gives each hand a curl for each finger, from the
poses (hold, point, flat, fist, relaxed), with a blend of 0.1 s between
poses, and from the trigger and the grip. A curl bends the bones of a finger
about their own x, as each animation of the game does. The bend of each bone
at a curl of 1 is the game's own tightest pose, measured in `hh.HBN`:

| Finger | Bones | The bend at a curl of 1, in degrees | From |
| --- | --- | --- | --- |
| Thumb | 20, 21 | 44.9, 90.3 | `point`, frame 1 |
| Index | 17, 18, 19 | 73.8, 66.6, 66.6 | `tap_house` |
| Middle | 8, 9, 10 | 89.4, 83.5, 53.9 | `point`, frame 1 |
| Ring | 14, 15, 16 | 113.8, 94.3, 64.7 | `point`, frame 1 |
| Little | 11, 12, 13 | 123.7, 70.9, 59.3 | `point`, frame 1 |

A curl of 0 is the rest pose of the file. The bones in the palm and the
wrist do not move. A fist of the mesh with these angles is the hand of the
game's `point` animation, within 13.5 mesh units at each bone joint (4 mm on
a hand of 19 cm). The hand of the mouse player (`wiggle`) has the middle,
ring and little fingers at about 0.11 to 0.24 of these bends, which is the
rest curl of 0.12 of the mod.

The hold pose closes each finger on the ball of the held object with the
joints of the mesh (`MeshHoldRange`), not with the joints of the low poly
hand.

**The place.** The mesh has the turn of the exact root (the rest turn of
`hand_mesh_yaw_deg`, `hand_mesh_pitch_deg` and `hand_mesh_roll_deg`, then
the fingers along the finger axis of the grip and the palm away from the
back of the hand) and the size of the real hand: 623.6 mesh units are one
hand length. It is then moved until the tip of its own index finger, at the
rest curl of 0.12, is the point of the Hand (`MakePlacement`). So the glow,
the pick, the touch of the land and each other interaction are at the tip of
the index finger of the hand that the player sees. The trigger bends the
index finger and does not move the point.

With this place the wrist joint of the mesh is at `0.096 0.474 -0.030` of
the hand frame (the low poly hand: `0.130 0.490 0.000`), 0.9 cm away on a
hand of 19 cm. The palm of the mesh is 2.9 mm in front of the palm that the
code uses (`HandExactPalm`, `overlay_math::HoldCenter`), so a held object and
the leash are on the palm of the mesh.

**The two hands.** The right hand is the mirror image of the stored mesh,
as in the game. The left hand is the stored mesh.

**The look.** The mod mixes the base mesh and texture with the evil or the
good ones by the player's alignment (`CHand+0x9C`), with the game's own
formulas ("How the game changes the look"): the vertices and the normals in
a straight line, and each 4 bit channel of each texel as `fn_00870640` does.
The look changes when the alignment moves by 0.03, as in the game.

**The light.** The game lights a vertex of a boned mesh with
`min(1, 0.4 + 1.3 * max(0, n.l))` times the color of the object, where `l`
goes from the bone to the light position `0x00EA9E90` [V `fn_0084C3C0`,
`0x0084C626` to `0x0084C6B6`]. The color of the object is `+0x4C` of the
mesh object (`CHand+0x482C`) [V `fn_00813BA0`]. The mod reads the light
position and that color in each frame and uses the same formula, with the
direction from the grip of each hand. So the hands have the light of the game
at each time of day.

**The draw.** As the overlay ("Overlay renderer"): the game's Direct3D 7
device, vertices of the format `0x1C4` projected on the CPU with the camera of
the eye, one `DrawPrimitive` for both hands in each eye, and each state put
back after it. The differences:

- The texture is a surface of the mod on the game's DirectDraw, 256 by 256
  with 8 bits a channel (4 bits when the device gives no other). The mod
  fills it again when the look changes. The game keeps its own copy of the
  texture of each stage (`0x00EC81D0`), so the texture that was on stage 0
  goes back after the draw.
- Stage 0 multiplies the texture by the lit color, and the alpha of the
  texture by the alpha of the color. The blend is the plain alpha blend, so
  the wrist fades as in the game. A texel with an alpha below 8 of 255
  writes nothing.
- A triangle that faces away from the eye is not drawn. The mod finds that
  on the CPU, with the sign of each mirror on the way to the camera
  (`FacesCamera`).
- The hands draw after the arc, the ray and the tether of the overlay,
  because the wrist is not opaque.

**When the low poly hands draw.** With `hand_model=0`. When a hand file of
the game is not readable. When the device gives no texture. After the test
key (below). Each case has a log line.

### The fingers from outside

The hand model takes one curl and one spread for each finger
(`hand_model::FingerPose`), and `overlay.cpp` has three functions for a
caller that wants to bend a finger by what is near it. A caller declares
them in its own file, as the mod does it for `OverlayHandPoints`. No file of
the mod calls `OverlaySetFinger` now.

```
void OverlaySetFinger(int hand, int finger, float curl, float spread,
                      float weight);
bool OverlayPoseCurls(int hand, float curls[5]);
bool OverlayFingerJoints(int hand, int finger, float curl, float spread,
                         float joints[4][3], float* radius);
```

- `hand` is 0 for the left hand and 1 for the right hand. `finger` is 0 for
  the thumb and 1 to 4 for the index finger to the little finger.
- `curl` is 0 for an open finger (the rest pose of the mesh) and 1 for a
  finger of a fist. `spread` is in radians at the knuckle, plus toward the
  thumb side. For the thumb it turns the base of the thumb.
- `OverlaySetFinger` gives one finger its curl and its spread. `weight` (0
  to 1) is the share of this curl against the curl of the pose, so a caller
  can fade its request in and out. Call it in each frame. The request ends
  one frame after the last call.
- `OverlayPoseCurls` gives the curls that the pose gave in the last frame,
  before a request. So a caller can make its curl larger or smaller than
  the pose.
- `OverlayFingerJoints` gives the knuckle, the two joints and the tip of a
  finger in the world for a curl and a spread, at the place where the hand
  was drawn last, and the radius of the finger. A caller can try some curls
  and take the largest one at which the finger is not in the land or in an
  object.

The three functions work with the mesh and with the low poly hands. Only
the mesh shows a spread.

Hand feel ("Hands that react to the world") does not use them. It needs
the curls of the pose in the same frame, so `overlay.cpp` gives them to
`HandFeelCurls` and takes the new curls back. A request of
`OverlaySetFinger` then acts on the curls that hand feel made.

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `hand_model` | 1 | 1 draws both hands with the game's own hand mesh. 0 draws the low poly hands |
| `hand_model_file` | 1 | 1 uses `bwvr-hand.glb` when it is beside the mod ("A hand that the player edited") |
| `hand_model_test_look` | | Test key. Frame numbers. At each one the look steps, as Shift and the minus key do |
| `hand_model_test_swap` | | Test key. Frame numbers. At each one the hands change, as Ctrl and the minus key do |
| `hand_model_file_path` | | Test key. The full path of a file to read in place of `bwvr-hand.glb` |

`hand_mesh_yaw_deg`, `hand_mesh_pitch_deg` and `hand_mesh_roll_deg` (hand.md,
"The exact Hand") turn the mesh of the mod's hands too. `hand_tint` colors
only the low poly hands. The keys are read from `bwvr.ini` at the start. They
are not in the launcher.

With `test_keys=1`:

- **Ctrl and the minus key of the number pad** go through the hands: the
  mesh of the file of the player (when there is one), the mesh of the game,
  the low poly hands. The pointing controller gives a long pulse for a mesh
  and a short pulse for the low poly hands.
- **Shift and the minus key of the number pad** step the look: the
  alignment of the player, evil, neutral, good.
- The minus key alone shows and hides the mesh of the game's own Hand (hand.md,
  "One pair of hands").

### The log

The tag `hand model`. At the first frame with a hand: the counts of the
mesh that was read, or the reason why the low poly hands draw. Whether a
file of the player is in use. Where the wrist joint, the knuckle of the
index finger and the palm of the mesh are against the low poly hand. The
light position and the color of the game's Hand, and again when the color
is far from the last one in the log. Each change of the look (40 at most),
with the alignment and the two weights. The kind of the texture, and the
first draw. Each press of a test key. Once a report window: the eye passes
drawn, the triangles a pass, the triangles that face away, and the time on
the CPU to skin and to draw.

The tag `overlay`: once, the index tip of the mesh against the point of the
Hand.

### Tests

The math test (`hand_model_checks.h` in `bwvr/test/controller_math/`) has no
game files. It makes a hand with the layout of the game's bones: the same
parents, the length of a bone along its z, the bend about its x, a mirror in
the wrist and the index tip at the origin.

- A curl of 0 is the rest pose, and its skin matrices change nothing. A
  curl of 1 bends each bone by its angle of the table, keeps each length,
  goes to the palm side and moves no other finger. Half the curl is half
  the bend. The spread turns the knuckle to the thumb side and does not
  bend the finger.
- The place: the index tip at the rest curl is the point of the Hand, with
  and without a rest turn. The fingers go along -y of the hand frame, the
  palm faces -x, and the wrist joint to the end of the middle finger is 1
  hand length. A full trigger takes the tip toward the palm.
- One matrix for each bone gives the same place as the long way (the skin,
  the place on the hand, the place in the world), for both hands. The right
  hand has a mirror and the left hand has none. A triangle with its outside
  away from the camera is not drawn, and a mirror turns that.
- The hold: the joints of each finger keep their distances at each curl, a
  larger ball opens each finger more, and at the contact no joint is in the
  ball.
- The light of the game at four angles. The mix of two texels at the
  weights 0, 128 and 255, and the weight from the alignment. The look from
  the alignment.
- A hand from a file ("A hand that the player edited").

On a desktop run the index tip of the mesh is at the point of the Hand, and
the mean color of the hand pixels agrees with the game's own Hand within 6
of 255. The overlay costs about 0.1 ms a frame on the CPU for both hands.

### Limits

- The bones of the game's own Hand, which the mod still places for a spell
  and for an effect on a bone, are on the hand with straight fingers. The
  mesh that shows has its index tip at the point with the rest curl. So a
  spell on a bone can be up to 1.9 cm from the same bone of the hand that
  shows.
- The shadow of the game's Hand on the land has the fingers of the game's
  animation.
- The game's poses of the fingers are not played as animations: each pose
  of the mod is a curl for each finger. The poses `hold_side`, `horn` and
  `phile` of the game also turn the wrist and the bones in the palm, and
  the mod does not.
- The thumb has one curl (its two last bones) and one spread (its base).

**Open: only a headset shows it.**

1. Whether each hand sits right on the controller, with the right size and
   a wrist that fades. `hand_mesh_yaw_deg`, `hand_mesh_pitch_deg` and
   `hand_mesh_roll_deg` turn it.
2. Whether the fingers follow the trigger and the grip at each point of the
   way and look right.
3. Whether a finger goes into a rock, a villager or a tree, or stays far
   from it.
4. How the point, the flat hand and the fist of the mesh look at the
   teleport aim, at a stroke of the Creature and at a drag of the land.

## A hand that the player edited

The mod can draw the hands with a mesh that the player edited (for example
with more polygons). The reader is `src/mod/hand_model_file.cpp`, and the fit
is in `hand_model_math.h`.

**The art.** A mesh that is made from the game's mesh is still the art of
the game. `bwvr-hand.glb` is not in the repository and not in the installer.

### The workflow

1. Run `python bwvr/tools/hand_export/export.py`. It writes `media\hand`
   ("The export").
2. Open `media\hand\bw_hand.blend` in Blender 4.5 or later.
3. Edit the mesh of the object `hand`. To close the seams of the texture
   first, use Mesh > Clean Up > Merge by Distance. Blender gives each new
   vertex the bones and the shape keys of the vertices around it.
4. Keep the 22 bones: do not move, rename, add or delete a bone. Keep the
   shape keys `good` and `evil`. Keep the texture points on the texture of
   the game.
5. Use File > Export > glTF 2.0, with the format glTF Binary (.glb) and each
   other setting as it is.
6. Give the file the name `bwvr-hand.glb` and put it in the game folder,
   beside `runblack.exe`, `ddraw.dll` and `bwvr.ini`.
7. Start the game. A line of the tag `hand model` in `bwvr.log` says that
   the file is the mesh of the hands, or why it is not.

`media\hand\README.txt` has the same steps (`tools/hand_export/README_hand.txt`
is its source). `blender_example.py` makes an example file.

### What the mod reads

The format is glTF binary, because Blender writes it and a small reader
with a permissive license reads it: cgltf 1.14 by Johannes Kuhlmann (MIT),
one header, with the JSON reader jsmn by Serge Zaitsev (MIT) in it. The
build takes it from `third_party/cgltf` (a clone, as MinHook is). A build
with no clone has no reader, and the log of the mod says so.
`installer/Notices.txt` has the notice.

- The first mesh with a skin, and each of its triangle primitives: the
  positions, the normals, the first texture points, the first four bones
  and weights of each vertex, and the indices. A file with no normals gets
  normals from its triangles.
- The bones by their names. `b07_middle0` is bone 7 of the game: the mod
  reads `b`, two digits, and then `_` or the end. A prefix that ends with
  `:` or `|` is ignored. The file must have each of the 22 bones.
- The shape keys `evil` and `good`, by their names. A shape key can be a
  sparse accessor, as Blender writes it. A file with no shape key `evil`
  gives an evil hand with the form of the base hand.
- Not read: the textures, the materials and the takes. The hands keep the
  textures and the light of the game, and the pose comes from the
  controllers.

A vertex can have up to 4 bones. The game's own mesh has one bone for each
vertex, so a mesh with more bones at a joint bends more smoothly than the
game's.

### The fit

The mod does not trust the units and the axes of the file. It takes the
place of each bone joint in the file (the world matrix of the joint node)
and in the game (the rest pose of the L3D file), and it finds the one map
that puts the joints of the file on the joints of the game (`FitJoints`):
a turn, one size for all axes, and a mirror when the file needs one. The
map comes from three joints that are far apart, the wrist and the knuckles
of the index finger and the little finger. Then each of the 22 joints must
be within 2 % of the length of the hand of its place: 4 mm on a hand of 19
cm. So a bone that was moved gives a file that is not used, and the log
names the bone.

Each vertex goes through the mix of the rest matrices of its bones
(`joint world * inverse bind`), and then through the map. A shape key is a
step, so it goes through the map without the offset. The file of the export
has a mirror in its map (the stored mesh is left handed and glTF is right
handed), so the reader turns the order of each triangle back.

### When the file is not used

Each case gives one log line with the reason, and the hands have the mesh
of the game:

| The reason in the log | The cause |
| --- | --- |
| The file is not a glTF file that the mod can read | Another format, or a damaged file |
| The data of the file is not complete | A `.gltf` with its data in other files |
| The file has no mesh with a skin | The mesh was exported with no armature |
| The file has no bone with a name that starts with b07 | A bone was deleted or renamed |
| The bones of the file are not at the places of the bones of the game | A bone was moved |
| A vertex of the file has a bone that is not a bone of the game | Weights on a bone that the player added |
| A vertex of the file has no bone | A vertex with no weight |
| A part of the mesh has no texture points (or no bones and weights) | An export setting was turned off |
| The mesh of the file has ... vertices | More than 20000 vertices or 20000 triangles. The mod skins each vertex on the CPU in each frame, so the limit keeps that time small |
| This build of the mod has no reader for the file | The build had no `third_party/cgltf` |

With `hand_model_file=0` the mod does not look for the file.

### Tests

The math test (part of `hand_model_checks.h`): the fit finds the turn, the
size and the mirror of a file with and without a mirror, a vertex and a
normal of the file go to their places, a bone that is moved by 30 units is
30 units from its place and the fit names it, joints on one line give no
fit, and the bone number of six names.

A reader test with no game links `hand_model_file.cpp` with the bones of the
game's L3D file. It shows these results:

- The export as it is, and the export with the look files, are used. Each
  vertex is within 0.0003 mesh units of the vertex of the game, with the same
  bone, and the shape key `evil` is the evil mesh.
- The export after Merge by Distance and Subdivide in Blender 5.1.0 is used,
  also with 2 cuts (922 vertices and 1521 triangles).
- A file with a moved bone, a renamed bone, a mesh with no armature, a vertex
  on an added bone, a PNG file, the FBX file, or a path with no file is not
  used. The log gives the reason of the table above.

On the desktop, the test key `hand_model_file_path` reads a file from outside
the game folder. A mesh with 922 vertices costs 0.22 ms a frame on the CPU
for both hands, against 0.1 ms for the mesh of the game.

**Open.** A hand that the player edited with his own Blender, a file from
another 3D editor, and how the joints of a mesh with more bones look in the
headset.

## Hands that react to the world

The fingers of the mod's hands react to what is near them: the land, a target
of the grip, the object in the Hand, and (off by default) the body of an
object that the hand passes. The code is in `src/mod/hand_feel.cpp` (the
game side), `src/mod/hand_feel_math.h` (the pure math) and
`src/mod/overlay.cpp` (the call, after the curls of the pose).

### The design

[research-hla.md](research-hla.md), section 6, has the facts from the
Half-Life: Alyx files. The ones that count here:

- The hand has one curl value and one target point for each finger. Near a
  surface each finger goes to a point on that surface.
- A sensor of 20 cm around the hand starts the finger tests, with 10 samples
  for each finger. It reacts to each surface, not only to an object that the
  hand can take.
- An object that the hand takes goes to a hold pose in 0.10 s.
- A hand that hits the world gives a pulse by the force of the hit, at most
  one in 0.2 s.

In Half-Life: Alyx the hand stops at a surface. The mod cannot do that: the
Hand is the real hand, 1:1, and its place and its turn never change. So in
the mod only the fingers react. A hand that the player pushes into the land
goes into the land, and the land hides it. The one exception is the land test
of the Hand's point, which lifts the pointing hand so that the tip of its
index finger stays 0.5 cm over the land (hand.md, "The point touches the
land").

**What the game gives cheaply.**

| Data | From | Cost |
| --- | --- | --- |
| The height of the land at a point | `LH3DIsland::GetAltitude` `0x00803090`, as the land test of the Hand's point uses it | One call for each point |
| The objects near a hand | The candidates of the pick: each object that the game drew in the first eye pass (`hand_direct.cpp`) | A list that the mod has already |
| The body of an object | The body that the choice of the grip uses (`near_math.h`): a cylinder from `Get2DRadius` and `GetHeight`, a trunk and a crown for a tree, a capsule for a Creature | Two virtual calls for each object near a hand |
| The object in the Hand and its place | `CHand+0x4904`, and the matrix that `hand_exact.cpp` writes for it | Read only |
| The game's own hand poses | The 26 animations of `hh.HBN` ("The model of the Hand") | Measured once, with `tools/hand_export` |
| The game's own rule for a hold pose | `HandStateHolding::Update` and three virtual functions of `Object` ("The hold pose of the game") | Two virtual calls for each new object in the Hand |

### What the mod does

`overlay.cpp` makes the curl of each finger from the pose of the hand, the
trigger and the grip. Then `hand_feel.cpp` changes the curls. It has four
parts, and each one has its own key.

**The rules.**

- The place and the turn of a hand never change. Only a curl changes.
- The player's own finger wins. The world can add bend to a finger. It
  opens a finger only while the trigger or the grip of that finger is at
  0.15 or less.
- What the world adds goes through a filter with a time constant of 0.05 s.
  The curl of the pose has no filter, so the trigger and the grip show with
  no lag.
- A finger moves only when the hand moved or the world near it changed. A
  hand that is still over land that is still has still fingers.
- The parts work only while the God view or the table owns the view. In a
  menu, in a Temple room and in a cut scene the fingers have the curls of
  the pose.

**The finger tables.** For each finger the mod keeps the knuckle, the two
joints and the tip at 17 curls, in the hand frame. They come from the hand
model in use (`HandModelFingerJoints`), or from the low poly hand. A curl
between two rows is the mix of the rows, which is within 0.2 % of the hand
length of the true place. For the hand of a frame the tables go to the
world one time. Each test of a finger uses six points: the middle of each
of its three bones, the two joints and the tip. A point has 0.8 of the
radius of the finger. The tip has 0.5 cm of the room, which is the clearance
that the Hand's point keeps over the land.

**The free curl** (`FreeCurl`). This is the one rule for the land and for a
body. When the finger is in the world at the curl of its pose, the mod looks
for the nearest curl at which it is not:

1. The first free curl above the pose on the grid of the table, and with an
   open grip the first one below it. The nearer one wins. The side of the
   last frame wins when the other side is nearer by less than 0.10, so a
   finger between two answers does not go back and forth.
2. Five steps of a search between that curl and the last curl that is not
   free. So the finger rests on the surface.
3. When no curl is free, the curl with the least depth, if it is less deep
   than the pose by 10 % or more. If not, the curl of the pose stays.

**The land** (`hand_feel_land`). The land is `LH3DIsland::GetAltitude`, with
the sea at 0, as the land test of the Hand's point has it. For each hand the
mod reads five heights under the hand in each frame. When the lowest point
that a finger can reach is less than a quarter of a hand length over the
highest of them, it reads a grid of 8 by 8 heights under the hand (the land
patch), and each finger test takes its height from that grid.

- Fingers that go down into the land bend more, each one by its own length,
  until the tip rests on the land. On a slope each finger finds its own
  height.
- A hand with the palm down has fingers that open as it comes to the land,
  until the flat hand lies on it.
- A hand that is under the land has no free curl. The land hides it.
- The index finger of the pointing hand bends on the land too. Its tip is
  the Hand's point, and the land test keeps that point 0.5 cm over the
  land, so the glow and the tip of the bent finger are both on the land.

**The approach** (`hand_feel_approach`). While the grip has a target at the
Hand's point (the object with the halo, grab.md, "One target"), and a press
picks it up or takes from it, the pointing hand takes the form of the target.
The weight is 0 at the reach of the pick (`hand_pick_cm`) and 1 when the point
is 1 cm from the body, with a smooth step between.

- The form. Each finger goes to 0.55 of the curl at which it holds a ball
  of the size of the target at the palm. So a small target (a villager)
  gives a claw with the thumb at the index finger, and a large one (a tree)
  an open hand with a little bend. The size is the larger of the radius and
  half the height of the body, and 0.10 hand lengths at least. A real hand
  that reaches for a thing takes the form of the thing before it closes.
- The game's own pose `can_pickup` is the least curl of each finger. The
  size of the target chooses its frame: a target of 0.30 hand lengths or
  more gets the open hand of frame 0, and a small one goes toward frame 2.
- The index finger keeps its curl, because its tip is the Hand's point.
- A grip that closes takes each finger from the form to the curl of the
  grip: the share of the approach is 1 with the grip open and 0 from a grip
  of 0.5 on.
- A target that a press does not act on (the red halo) and an object that a
  press taps do not change the hand.

**The hold** (`hand_feel_hold`). The game's rule gives the kind of the hold
("The hold pose of the game"), and the contact gives the curl.

- For an object that the game holds from the side (the hold types 5, 6 and
  7: a tree, a villager, an animal, a mobile object), the body is a capsule
  on the axis of the object, at the place where `hand_exact.cpp` put the
  object in the frame before, with the game's hold radius. For a tree that
  is the trunk. A finger closes from the open hand until it touches the
  capsule.
- A finger that cannot touch the capsule, and each finger for each other
  object, stops on the ball that `hand_exact.cpp` puts at the palm.
- A finger that can touch neither takes the curl of the game's own hold for
  the hold type and the hold radius.
- From the side the thumb does not close more than the thumb of the game's
  `hold_side`: the fist of the game with the thumb out.
- A body is never thinner than 0.03 hand lengths. A capsule that is short
  too (a villager) counts as 0.07, so the hand is a loose cup and the player
  sees what is in it.

The pose is made again only when the body moved in the hand frame.

**A tree at the palm** (`hand_hold_trunk`, in `hand_exact.cpp`). The place
of an object in the hand comes from a ball that touches the palm (hand.md,
"The exact Hand"). A tree (the hold type 5) has the game's own hold radius for
that ball, 0.2 times the 2D radius, which is the trunk
[V `Tree::GetHoldRadius` `0x0074B610`]. So the trunk lies on the palm. With
the 2D radius of the crown, the trunk would be several centimeters in front
of the palm. Each other object keeps its 2D radius: a rock that is wide and
flat must not go into the palm.

**The bodies** (`hand_feel_bodies`, off). At the end of the world of the first
eye pass, the mod takes the bodies near each hand from the candidates of the
pick. The fingers of the next frame use them.

| Object | Body |
| --- | --- |
| A tree | The trunk, a cylinder of a quarter of the radius to half the height, and the crown, a spheroid in the upper half |
| A Creature | The capsule of the choice of the grip |
| An object with a mesh box (a house, a rock) | The box of its mesh, with the turn of the object |
| Each other object | The cylinder of the choice of the grip |

These do not count: the object in the Hand, a scroll (the hand goes into it
to tap it), a forest (one object with many trees), a cylinder or a box with
a radius of more than 0.8 hand lengths (the Temple, a store), and a body
that the palm of the hand is in. While the pointing hand strokes the
Creature, it does not bend against a body. The index finger of the pointing
hand does not bend against a body at any time: its tip is the Hand's point,
and that point goes into a body to choose it. When the hand is more than 60
units long (the table), a finger is wider than a house, and no body counts.

**The pulse** (`hand_feel_pulse`). One pulse of the kind `surface` (0.06 for
8 ms, the faintest of the set) when a finger of a hand is in the land or in
a body and none was for 0.2 s. Half-Life: Alyx allows one pulse of a hit in
0.2 s too.

**The idle wave** (`hand_feel_idle`, off). A relaxed hand moves its fingers
a little: a wave of 4 s that adds 0.05 of curl at most. The game's Hand has
no such wave in its file (the two frames of `wiggle` have the same finger
bends), so this one is the mod's own. It is motion that the player did not
make, so it is off.

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `hand_feel` | 1 | 0 gives the curls of the pose only |
| `hand_feel_land` | 1 | The fingers bend on the land |
| `hand_feel_approach` | 1 | The pointing hand opens toward a target of the grip |
| `hand_feel_hold` | 1 | The fingers close on the body of the object in the Hand. 0 stops them on the ball at the palm |
| `hand_feel_bodies` | 0 | The fingers bend against a body near the hand |
| `hand_feel_pulse` | 1 | A faint pulse when a finger first meets the land or a body |
| `hand_hold_trunk` | 1 | A tree in the Hand has its trunk at the palm. 0 puts it at the radius of its crown |
| `hand_feel_idle` | 0 | The idle wave of a relaxed hand |
| `hand_feel_test_toggle` | | Test key. Frame numbers. At each one hand feel goes off or on, as the test key of the keyboard does |
| `hand_feel_log` | 0 | Test key. A log line for each hand each so many frames |

The keys are read from `bwvr.ini` at the start. They are not in the
launcher. With `test_keys=1`, **Ctrl, Shift and the minus key of the number
pad** turn hand feel off and on while the game runs. The pointing controller
gives a long pulse for on and a short pulse for off.

### The log

The tag `hand feel`.

- At the start: which parts are on.
- One line when the fingers of a hand go into the land or into a body, and
  one when they are free again: the curls of the pose, the curls that the
  world asks for, the height of the hand over the land, the count of bodies
  near, the trigger and the grip. At most one line for each hand in 45
  frames, and 60 lines in all.
- One line when the pointing hand starts to take the form of a target: the
  size of the target, the frame of `can_pickup`, the curls of the form, the
  gap and the weight.
- One line for each new object in the Hand, and one more when its body
  becomes the capsule: its class, the hold type of the game, the hold
  radius, the game's own curls, the body in the hand, the hold curls and
  what each finger stops on.
- Each press of the test key.
- Once a report window: the frames, how often the land and a body were near
  a hand, the hand frames with a finger in the land and in a body, the
  frames with no free curl, the frames of the approach, the hold poses, the
  pulses, the time on the CPU and the land heights read in a frame.

### Tests

The math test (`feel_checks.h`) has no game: the hand is the low poly hand,
19 units long, and the land is a function.

- The tables: a curl on a row gives the points of the hand model, a curl
  between two rows is within 0.2 % of the hand length, and a point of the
  world goes back to its place in the hand frame, for both hands. The land
  patch gives the height of a slope between its points.
- The free curl: a free finger keeps its curl. A finger in the world opens
  or bends to the nearer free curl, and bends only when the grip is
  pressed. The side of the last frame holds. With no free curl the finger
  takes the curl with the least depth, or keeps the curl of its pose.
- The land: palm down on flat land the finger opens, and its tip rests 0.5
  cm over the land. With the grip pressed it keeps its curl. A hand that
  lies on the land has flat fingers. Fingers down into the land bend more,
  a hand 1 cm lower bends them more, the little finger bends less than the
  middle finger, and on a slope the finger on the high side bends more.
- The bodies: the gap to a cylinder, a capsule, a ball, a tree and the box
  of a house. A finger bends against the wall of a house and rests on it. A
  house 20 cm away does not move it. The Temple does not count.
- The filter, the pulse and the idle wave. The approach: the weight by the
  gap, the frame by the size, and a grip that closes. The hold: the game's
  curls for each hold type and size, the least radius of a body, the ball
  at the palm against the contact curl, a trunk across the palm, a trunk
  that no finger can reach, and a finger that is in the body.

On the desktop, hand feel shows these results (the fake controller, 100
units a meter):

- Over the land the fingers go from rest to a fist as the hand goes down,
  the longer fingers first, and each hand on its own slope has its own
  curls. A hand on its side bends only the fingers that reach the land. A
  hand with the palm down opens its fingers and lies flat. A hand with the
  back down bends its fingers up, out of the land.
- At a rock, a villager and a tree under the tip of the index finger, the
  hand takes a claw for a small thing (the thumb comes to the index finger)
  and an open hand with a little bend for a tree. Over an object that a press
  taps, the hand keeps the curls of its pose.
- In the Holding state a rock, a tree, a pig, a mobile object and a villager
  each get their hold pose. The tree lies with its trunk on the palm and
  its thumb out.
- The pulse comes once at the first contact of a finger, and not while a hand
  stays on the land.
- The cost is under 0.03 ms a frame on the CPU, less than 0.1 % of a frame.

### Limits

- The hand does not stop at the land or at a body. A hand that the player
  pushes into the land goes into it, with its fingers bent, and the land
  hides it.
- A body is a cheap form. The box of a house has no roof slope, the crown of
  a tree is a spheroid, and a Creature is a capsule. A finger can stop a
  little before the thing that the player sees, or go a little into it.
- A forest, a scroll, and a building that is larger than the hand (the
  Temple, a store) do not bend a finger.
- The land patch has a point each 3.3 units for a hand of 19 units. The
  land of the game has a point each 10 units, so the patch is fine enough
  at 100 units a meter. On the table the hand is much longer than a land
  cell, and a finger can go through a thin ridge.
- The bodies are those of the frame before, so a finger is one frame late
  against a thing that moves.
- The thumb has one curl. It bends on the land and against a body as a
  finger does, but it does not go round an object.

**Open: `hand_feel_bodies` is off** until a desktop run shows these cases:

1. The Creature. The Creature of the profile is 2 cm across, so no finger
   beside the index finger is in its capsule. A run needs a grown Creature.
2. A house. The box of the mesh of a house is not where the point of the
   object is (3.3 units from the middle of the hand in one run). Nothing
   shows where the box is against the house that the player sees.
3. A tree. The crown is a spheroid, and no picture shows one finger at 30
   percent against it.

Also not shown on the desktop: a real press of the test key, the approach at
weights between 0 and 1 at a small target (a villager walks away), and what
the pulse feels like.

**Open: only a headset shows it.**

1. Put a hand down on the land with the fingers first, slowly. The fingers
   must bend as they meet the land, the longer ones first, with no jump. A
   finger must not shake when the hand is still.
2. Lay a flat hand on the land, palm down, on a slope and across a ridge.
   The fingers must open and lie on it.
3. Press the trigger and the grip with the hand on the land. The player's
   own fingers must still show.
4. Is the pulse at the first touch too weak, too strong or too often
   (`hand_feel_pulse=0` turns it off, and `haptics_pct` scales it)?
5. Bring the tip of the index finger to a rock, a villager and a tree. Does
   the form look right, and does it come too late or too early
   (`hand_feel_approach=0` turns it off)?
6. Take a rock, a villager and a tree. Does a finger go into the object or
   stay far from it, and does the thumb that stays out for a villager and a
   tree look right (`hand_feel_hold=0`)?
7. Take a tree with the palm to the side, as a pole. The trunk must lie in
   the hand (`hand_hold_trunk=0`).
8. With `hand_feel_bodies=1`: does a finger bend in the air beside a house, a
   tree or the Creature?
9. With `hand_feel_idle=1`: is the slow wave of the fingers wanted? It is
   motion that the player did not make.

## The glow at the Hand's point

The Hand interacts from one point (hand.md). The render pick looks through
the pixel of that point (scrolls, signposts, buildings), and the pick of
holdable objects measures its grab radius from it. The Hand's mesh is drawn
around the point, so the player cannot see where it is. A glow sits on it. It says what a press of
the grip does there now.

By default the disc shows only while a press of the grip at the point acts on
something (`hand_glow_idle=0`). The disc over nothing and the disc while the
Hand holds an object show only with `hand_glow_idle=1`.

**What it shows.** The look comes from the game's interface hand mode
(`GInterface+0x3AC`), which is what the next press does. The modes are the
returns of the hand mode chooser `fn_005D7F20` [V], and `0x13` is the
Creature. `hand_direct.cpp` turns them and the object of the pick record into
a kind (`HandDirectPressKind`, `near_math::Kind`).

| Hand mode and kind | Look | Color | Radius in the room |
| --- | --- | --- | --- |
| 9: the grip picks it up. `0xD`: the grip takes from a pile, a field or a fish farm (grab.md, "The bulk take") | Take | gold (255, 190, 40), bright | 1.7 cm |
| `0xF`, `0x10`, `0x13`, `0x1C`, `0x20`, and `0x12` over the Creature or over an object that the game taps: the grip acts on something that the Hand cannot take | Activate | a paler gold (255, 224, 120) | 1.5 cm |
| any other, for example 3 (the land) | Nothing (only with `hand_glow_idle=1`) | pale blue (120, 200, 255) | 1.0 cm |
| the Hand holds an object | Holding (only with `hand_glow_idle=1`) | pale blue, faint | 0.8 cm |

The color is the button: the grip is gold on the hint card, and the grip
activates a scroll (the right press `FUN_005D1330` sends the tap) and starts
the touch of the Creature [V]. The trigger acts on nothing at the Hand's
point, so the glow is never green. Green is the color of the trigger.

The chooser gives `0xD` when the object's vtable slot `+0x6CC` returns 1,
`0xF` or `0x10` when `+0x72C` returns 1, `0x1C` for a bubble (`+0x408`),
`0x20` for a leash (`+0x410`), and `0x12` for any other interactable object.
Which objects give `0xF` and `0x10` is not known [I]. The piles of a store, a
field and a fish farm give `0xD` [V]. The mod's pick puts its choice in the
game's record, so the mode is 9 one frame after the Hand reaches a holdable
object. Outside the player's influence the game says `0x12`, and the grip
does not pick up there.

**The halos.** Other discs use the same draw: the halo around the target of
the grip at the Hand's point, a far target, the target of a Creature command
(green), the target of a miracle and the thing that takes the object in the
Hand. The halo is gold, or red when the influence stops the press. The halo
is as large as the body of its target. For a tall body it is an ellipse along
the image of the upright axis. grab.md ("One target" and "The grab from a
distance"), creature.md and miracles.md say when each halo shows. The halo
of the target at the Hand draws also with `hand_glow=0`.

**How it is drawn** (`src/mod/hand_glow.cpp`):

1. `sky_detail.cpp` calls `HandGlowDraw` after the sun glare, at
   `0x0082F4C9` in `LH3DRender::FinishFrame`. There the world and the sorted
   list are drawn, the depth buffer holds them, and the interface is not
   drawn yet ([tabletop.md](tabletop.md), 4.4). The call runs in every eye
   pass, for each value of `sun_glare`.
2. `HandDirectGlowFacts` (`hand_direct.cpp`) gives the point, the Hand's
   length, the hand mode and whether the Hand holds an object. The first ask
   of a frame latches them, so both eyes get the same values. It gives
   nothing unless the controller places the Hand in the Normal or the Holding
   state and the hooks placed it in this frame.
3. An item of the sorted list can leave the camera without the eye offset
   (`sky_detail.cpp`). So the draw calls `StereoReapplyEye`, reads
   `0x00EA9E40`, and puts the four camera matrices back as they were
   (`OverlayEyeMatrix`).
4. `GlowProject` (`controller_math.h`) projects the point as `ProjectPoint`
   does. The radius in pixels comes from the lengths of the matrix columns,
   so the disc is round. Its size is in the room: centimeters times the units
   per meter of the scale now (`TabletopViewScale` on the table and in the
   God view, `HeadPoseScale` at the life size scale).
5. Two layers, each a disc of 20 segments with a middle, a ring at 45% of
   its radius and an edge with an alpha of 0 (60 triangles). First the disc
   in the color of the look, with the game's usual alpha blend, so it shows
   on bright and on dark ground. Then a core of half the radius, in that
   color half way to white, that adds its light (source alpha and one), so
   the middle glows. Each layer is one `DrawPrimitive` on the game's
   Direct3D 7 device, with the game's vertex format `0x1C4`, so dxwrapper
   sees it as it sees the game's own draws. The core is needed: with the disc
   alone, the glow on brown hills reads as a gray patch and not as a light.
6. The draw reads each state before it sets it, and puts back only the states
   that it changed, in the reverse order. The game keeps its own copy of some
   states and skips a call when its copy agrees, so a state left changed would
   stay wrong. Render states: `ZENABLE` (only when it is off), `ZWRITEENABLE`
   off, `ZFUNC` less or equal, the alpha blend with source alpha and inverse
   source alpha, no alpha test, no fog, no cull, Gouraud, solid, no stencil,
   no color key. Stage 0 takes the diffuse color and alpha, and stage 1 is
   off. The texture stays bound. The core then sets the destination blend
   to one. A state that the device does not give is not changed, and the log
   says so once.
7. The depth: `rhw = near / depth` and `z = 1 - rhw`, as the game writes them.
   The depth test uses a point half the Hand's length nearer to the eye, 9.5
   cm of the room for a hand of 19 cm. So the Hand's own mesh does not hide
   the glow, and land or an object further in front does. The disc writes no
   depth. Its place on the screen stays at the true point, so the two eyes
   fuse it at the Hand's point.

The glow is not drawn when the point is not in front of the near plane, when
the disc is wholly outside the render, or when the disc would be wider than
half the render (a hand at the face).

**Keys.** `hand_glow` (1, in the launcher, Controllers group, "Glow at the
Hand's point"): 0 turns the disc off. `hand_glow_idle` (0): 1 draws the glow
on each frame, also over nothing and while the Hand holds an object. With the
mouse, with `controller_hand=0` or with `hand_glow=0` the disc draws nothing
and changes no state.

**The log.** The tag is `hand glow`. One line at the first draw. One line for
each change of the look and every 900 frames, 40 lines at most, with the
center in each eye, the distance between the two, the radius and the depth.
Each report window counts the draws of each look and the passes with no draw.

**Tests.** The math test (`GlowChecks`): the middle and a point off the
middle agree with `ProjectPoint`, the disc is round on a 16:9 render, rhw and
z are the game's, the pull moves only the depth test and stops at the near
plane, a point behind the near plane or outside the view gives no draw, a
turned camera gives the same numbers, and two eyes 6.68 units apart put the
two discs `fx * e / d * hx` pixels apart, level, each on its own eye's line to
the point. On the desktop both eyes put the disc on the tip of the index
finger, and a villager under the point turns the disc gold and larger.

**Open: only a headset shows it.** The sizes and the colors. Whether the glow
is easy to see on bright land, on the sea and on the Hand. Whether the depth
pull of half a Hand is right: the disc must not vanish into the finger tip,
and a hill in front must still hide it. Whether a one frame change of the look
(9, `0x12`, 3 when the Hand leaves an object) shows as a flicker. Which
objects give the touch look: scrolls, signposts and buildings. On the
desktop, a press at the edge of the view can start off the render while one
eye shows gold.

## Overlay renderer

The overlay renderer draws the mod's own meshes inside the game's frame, at
the right depth in both eyes: the hands, the arc and the ring of the
teleport, the ray and the tether. Later users can be the controller models
of the hints and the rim of the table. The code is in `src/mod/overlay.cpp`
and `src/mod/overlay_math.h`.

**The low poly hand** (`overlay_math.h`, `BuildHand`). A palm, four fingers
of three segments and a thumb of three segments, 330 triangles. Each segment
is a prism with six sides that gets thinner at each joint, and each finger
ends in a round tip. The hand uses the frame of the grip pose after the flip
of z in `head_pose.cpp`:

- +x is out of the back of the right hand, so the palm faces -x.
- -y is along the open fingers, from the wrist to the finger tips.
- +z is across the knuckles, from the little finger to the index finger.

The grip point is the middle of the controller's handle, and a closed hand
wraps around it. The left hand is the mirror image in x. The wrist to the
tip of the middle finger is the real hand's length in the room
(`hand_real_cm` times `hand_scale_pct`, 19 cm by default) at the scale now:
the units per meter of `head_pose.cpp` times the gain of the table or the
God view. So the hand is one to one in place and in size, also on the table,
where the game's Hand has `table_hand_cm`.

The index finger closes with the trigger. The middle, ring and little
fingers close with the grip. The curl is 0.12 at a value of 0, so a hand at
rest is a little bent, and 1 at a value of 1. A full curl turns the three
joints by 75, 95 and 55 degrees about +z, toward the palm side. The poses
and the thumb are in hand.md, "One pair of hands". With `hand_model=1` (the
default) the mesh of the game stands for the low poly hand
("The game's hand on both real hands").

**How it is drawn** (`src/mod/overlay.cpp`):

1. `ControllersOverlayHands` (`controllers.cpp`) gives the grip pose of each
   hand in the world, with its trigger and its squeeze. It uses the head
   sample of the frame and the steps of the direct hand, on the table and
   off it. With no reach gain, the pointing grip is the point where the
   direct hand puts the game's Hand.
2. Once a frame, after `HandDirectFrame`, `OverlayFrame` builds the meshes,
   places them in the world and lights each corner: a fixed light from
   straight above and an ambient of 0.35, on a plain skin color. Both eye
   passes draw this one list.
3. `sky_detail.cpp` calls `OverlayDraw` after the sun glare and before the
   glow, at `0x0082F4C9` in `LH3DRender::FinishFrame`. There the world and
   its depth are drawn, and the interface is not. Later a screen quad resets
   the depth before the interface draws ([tabletop.md](tabletop.md), 4.4),
   so the depth of the hand does not reach the interface. The glow comes
   after the hand, so it also shows on the hand.
4. `OverlayEyeMatrix` gives the world to camera matrix of this eye and puts
   the four camera matrices back. The glow uses it too.
5. Each corner goes into camera space on the CPU. Each triangle is cut at
   the near plane, and a cut point takes the color at the same share of the
   edge. Then each corner is projected as `ProjectPoint` does, with
   `rhw = near / depth` and `z = 1 - rhw`. The vertices have the game's
   format `0x1C4`, so the draw needs no shader and no transform state. A
   triangle with all its corners past one edge of the render is not sent.
6. One `DrawPrimitive` for each eye, on the game's Direct3D 7 device. As the
   glow does, the draw reads each state before it changes it, and puts back
   only the states that it changed, in the reverse order. Render states:
   `ZENABLE` (only when it is off), `ZWRITEENABLE` on, `ZFUNC` less or
   equal, no alpha blend, no alpha test, no fog, no cull, Gouraud, solid, no
   stencil, no color key. Stage 0 takes the diffuse color and alpha, and
   stage 1 is off. The texture stays bound. The hand writes depth, so its
   fingers hide its palm, and land in front hides the hand. (The mesh of the
   game uses the alpha blend, see above.)

The overlay draws only while the controller owns the pointer, and each hand
only while its pose is tracked. The skin takes the color of the player's
alignment (`CHand+0x9C`): pale for good, dark and red for evil. The teleport
arc and ring (`teleport.cpp`) are a second source with their own switch: they
draw while the player aims, with `overlay=0` too. They are a tube of 6 sides,
6 mm of the room across, and a ring on the land. Cyan means a valid target
and red means a target that is not valid. They are opaque, with the depth test
and the depth write on.

**Keys.** `overlay` (the code default is 1): 1 draws the hands of the
overlay. With 0 the overlay draws nothing for the hands and changes no state.
With `hand_exact=1` and `overlay=0` the overlay draws only the hand that the
game's Hand does not cover (hand.md, "The exact Hand"). With
`hand_hide_game=1` (the default) the overlay draws both hands with `overlay=0`
too (hand.md, "One pair of hands"). There is no launcher row.

**The log.** The tag is `overlay`. One line at the first draw, and one at
each change of the owner of the pointer. Then, on the first draw, on a
change of the pointing hand's trigger or grip, when its index tip moves on
the screen, and every 900 frames, 40 times at most: for each hand the
trigger, the grip and the index tip in each eye. For the pointing hand also
the grip point, the index tip and the game's Hand position (`CHand+0x78`),
with the distance between the two in cm of the room. After 300 frames with
a draw in both eyes, one line gives the triangles a frame and the time on the
CPU, with the build and the draws apart. Each report window counts the draws,
the triangles, the triangles that crossed the near plane and the triangles
off the screen.

**Tests.** The math test (`OverlayChecks`):

- The curl from the controller values, and 330 triangles.
- The index tip at half curl against an independent chain of bends, and as
  a corner of the mesh.
- The tip at a turned grip pose, for the right hand and for the mirrored left
  hand, and the back of each hand.
- A length of 19 cm and a width of 6 to 12 cm across the knuckles, at 7.5, 100
  and 2000 units per meter.
- The trigger closes only the index, and the grip closes only the other
  three.
- The light from above and the ambient.
- The cut at the near plane: one triangle in front, nothing behind, two
  triangles for one corner behind (the cut on the near plane, the color at
  the same share), and one triangle for two corners behind. The test for a
  triangle off the screen.
- Two eyes put a corner `fx * e / d * hx` pixels apart, level, at the same
  depth.

An offline check sent the fake controller's pose for both hands through the
real `head_pose.cpp` and the overlay math. Both hands showed in both eyes,
and the left hand was the mirror image of the right.

**Open: only a headset shows it.** Whether the hand sits on the real hand:
each runtime and controller puts the grip pose at its own place on the
handle. The size and the color. The near plane: in play mode 0 it is 3.5
units, 47 cm of the room, so a hand nearer to the eye than that is cut away.
The God view brings it in to 5 cm. The frame cost at the headset's render
size.

## Haptics

One table gives each kind of pulse (`haptics_math.h`), and one function sends
it (`HapticsPulse` in `haptics.cpp`). The levels follow Half-Life: Alyx:
short ticks of about 10 ms, and 0.40 for an object that arrives.

The rule: **no pulse for a thing that the player did not do.**

### The kinds

| Kind | Amplitude | ms | When |
| --- | --- | --- | --- |
| target | 0.08 | 8 | A new target at the Hand's point or in the aim, at most one each 80 ms |
| lock | 0.20 | 10 | The grip locks a far target |
| arrive | 0.40 | 11 | Each frame of the last 0.125 s of the glide of a fetched object |
| take | 0.30 | 15 | The grip takes an object at the Hand's point, or a bulk source at a distance |
| let go | 0.20 | 10 | The Hand lets an object go |
| activate | 0.25 | 12 | The grip acts on a target that it does not take: a scroll, a house, the Creature |
| no target | 0.06 | 8 | A press of the grip with no target |
| touch | 0.12 | 10 | In the Creature state, the contact goes into the body |
| stroke | 0.07 | 6 | In the body, one tick for each 1.5 cm of the path of the contact |
| slap | 0.41 to 0.75 | 25 | A slap, by its speed: 0.41 at the speed of a slap, 0.75 at 1.8 times that speed |
| leash on | 0.30 | 20 | The leash goes on, within 1.5 s of a click of the player |
| leash off | 0.20 | 12 | The leash comes off, within 1.5 s of a click or a shake of the player |
| command | 0.25 | 15 | The trigger sends a command to the Creature |
| teleport | 0.30 | 20 | The teleport goes |
| refused | 0.35 | 30 | The teleport has no place to go to |
| tick | 0.15 | 10 | A small step: the draw of a gesture starts, a shake counts |
| gesture | 0.40 | 30 | The game knows a gesture (effect 3) |
| power | 0.30 | 40 | A miracle powers up (effects 8 and 9) |
| cast | 0.45 | 30 | The game casts a miracle (effects 18 to 21 and 23 to 28) |
| menu | 0.15 | 10 | A click in a menu (effect 44) |
| world | 0.15 | 10 | The two grips take or let go the world |
| surface | 0.06 | 8 | A finger first meets the land or a body ("Hands that react to the world") |
| other | 0.20 | 15 | Each other effect of the game |

No pulse is stronger than 0.75 or longer than 40 ms.

### The effects of the game

`GInterface::StartImmersion` `0x005D8A10` starts the game's own effects, and
`HapticsGameEffect` turns each one into a kind.

- Effect 0 (the Hand touches the Creature) gives no pulse. The game starts it
  again and again with a hand that is still. The mod gives one `touch` tick
  when the contact goes into the body, and `stroke` ticks for the path. A
  hand that is still gives none.
- Effects 1 and 2 (a slap) give the `slap` pulse with the speed of the real
  hand.
- Effects 4, 5, 6 and 46 (a pick up starts, the object is in the Hand, the
  Hand is over an object, a release) give no pulse. The mod gives `take`,
  `let go` and `target` at the action of the player.
- Effect 7 (the leash goes on) gives no pulse. The game starts it one frame
  before the leash is on. The pulse `leash on` comes only within 1.5 s of a
  click or a shake of the player (`CreatureCommandLeashAsked`), so a leash
  that the game changes by itself gives none.
- Effect 14 (a seed pops with its miracle in the Hand) gives `activate`.
- A fetched object pulses with `arrive` on each frame of the last 0.125 s of
  its glide (`haptics_math::ArriveNow`).

### Keys and the log

`haptics_pct` (100): the strength of each pulse in percent, 0 to 200. 0 turns
the pulses off. A value above 100 does not pass 0.75.

The first 6 pulses of each kind go to the log, and the report counts each
kind:

```
haptics      : take, 0.30 for 15 ms (strength 1.00, the pointing hand), frame 872
haptics      : 11 pulses this window (haptics_pct=100): 9 arrive, 1 let go, 1 no target
```

### Tests

The math test (`HapticsChecks` in `haptics_checks.h`): no pulse is stronger
than 0.75 or longer than 40 ms. `haptics_pct` scales the amplitude and not
the time. A slap that just counts has 0.41. A hand that is still for 300
frames gives no stroke tick, a stroke of 30 cm gives 20, and a jump of the
tracking gives one. A glide of 0.4 s pulses on 11 or 12 frames at 90 frames a
second.

The desktop shows only that a pulse goes out at the right frame: `target`,
`lock`, `arrive`, `take`, `let go` and `no target` in a fetch and a take,
`activate`, `touch`, `stroke` and `slap` at the Creature, and `leash on`,
`command` and `leash off`. The call sites of `teleport`, `refused`,
`gesture`, `power` and `world` have no desktop proof.

**Open: only a headset shows it.** How each pulse feels. Set `haptics_pct=50`
or `200` and say which kinds are too faint or too strong.
