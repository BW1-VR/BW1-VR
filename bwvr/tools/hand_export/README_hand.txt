====================================================================
 The Hand of Black & White, as files for a 3D editor
 Made by bwvr/tools/hand_export/export.py for the Black & White VR Mod
====================================================================


WHAT THIS FOLDER IS
-------------------

These files are the Hand of Black & White (Lionhead Studios, 2001).
The export tool read them from the game folder on this PC. The files
are the art of the game. Do not put them in git, in the installer or
on the internet. A hand that you make from them is also the art of
the game.

The tool reads these files of the game, and it changes none of them:

  Data\CreatureMesh\Hand_Boned_Base2.l3d    the hand of a neutral god
  Data\CreatureMesh\Hand_Boned_Good2.l3d    the hand of a good god
  Data\CreatureMesh\Hand_Boned_Evil2.l3d    the hand of an evil god
  Data\CTR\hh.HBN                           the animations
  Data\hndspec5.txt                         the names of the animations


THE FILES
---------

  bw_hand.blend        The Blender scene (Blender 4.5 or later). The
                       three textures are in the file.
  bw_hand.fbx          The same hand as FBX 7.4 (binary), with the
                       base texture in the file.
  bw_hand.glb          The same hand as glTF binary.
  textures\            hand_base.png, hand_good.png, hand_evil.png.
                       256 by 256, RGBA. The alpha makes the wrist
                       fade, as in the game.
  looks\               The good hand and the evil hand as separate
                       files, each with its own texture and no takes.
  example\             bwvr-hand.glb: an example of an edited hand.
                       Each triangle is cut in four, 676 in all. The
                       tool made it in Blender with the steps of HOW
                       TO EDIT THE HAND.
  preview\             Pictures of the FBX after Blender read it back.
  check_report.txt     The result of the check of each file.
  data\hand_data.json  The numbers of the game that the tool used.

bw_hand.fbx, bw_hand.glb and bw_hand.blend each hold:

  - 1 mesh `hand`: 141 vertices, 169 triangles, 1 texture layer
    `UVMap`, and the normals of the game.
  - 1 skeleton `hand_armature`: 22 bones. The bind pose is the rest
    pose of the game's file, with the fingers straight.
  - The skin: each vertex has 1 bone with the weight 1. That is how
    the game stores the hand.
  - 2 shape keys (blend shapes): `good` and `evil`.
  - 26 takes: each animation that the game has for the hand.


UNITS AND AXES
--------------

Size: the hand has a real size. The distance from the wrist joint to
the end of the middle finger is 19.0 cm. One unit of the game's mesh
is 0.30468 mm.

Origin: the tip of the index finger. This is the point of the Hand in
the game.

In Blender (+Z up): the fingers go to +Z, the palm faces -Y and the
thumb goes to -X.

In the FBX and the GLB (+Y up): the fingers go to +Y, the palm faces
+Z and the thumb goes to -X.

The hand is a LEFT hand. The game stores a left hand and draws its
mirror image, a right hand. The mod does the same: it draws the file
as the left hand and its mirror image as the right hand.

141 vertices are at 88 places. Where the texture has a seam, the game
has two vertices at one place.


THE BONES
---------

The game gives the bones no names. The names here are from the tool.
The number in a name is the number of the bone in the game's file.

  Bone          Parent        Vertices   What it is
  b00_wrist     -                 17     the wrist and the heel
  b01_attach    b00_wrist          0     a held object (takes 60, 63)
  b02_palm      b00_wrist          0     the root of the fingers
  b03_thumb1    b02_palm           3     the thumb, first bone
  b20_thumb2    b03_thumb1        13     the thumb, second bone
  b21_thumb3    b20_thumb2         7     the thumb, last bone
  b04_index0    b02_palm           8     the index finger in the palm
  b17_index1    b04_index0         5     the index finger, first bone
  b18_index2    b17_index1         6     the index finger, second bone
  b19_index3    b18_index2         9     the index finger, last bone
  b07_middle0   b02_palm           6     the middle finger in the palm
  b08_middle1   b07_middle0        5
  b09_middle2   b08_middle1        5
  b10_middle3   b09_middle2        7
  b05_ring0     b02_palm           6     the ring finger in the palm
  b14_ring1     b05_ring0          5
  b15_ring2     b14_ring1          5
  b16_ring3     b15_ring2          7
  b06_little0   b02_palm          10     the little finger in the palm
  b11_little1   b06_little0        5
  b12_little2   b11_little1        5
  b13_little3   b12_little2        7

In Blender a finger bone bends about its own X axis.


THE LOOKS
---------

The game has three meshes with the same triangles, and one texture
for each. It mixes them with the alignment of the player, from -1
(evil) to 1 (good):

  - Below 0 it moves each vertex from the base mesh to the evil mesh,
    and it mixes the base texture with the evil texture.
  - Above 0 it mixes the base texture with the good texture. The good
    mesh has the vertices of the base mesh.

In the files the shape key `evil` is the evil mesh, and the shape key
`good` is the good mesh. To see a look in Blender:

  1. Select the object `hand`.
  2. In the Data properties, set the value of the shape key to 1.
  3. In the Shading workspace, select the image hand_evil or
     hand_good in the Image Texture node.


THE TAKES
---------

The number in the name of a take is the animation number of the game.
A take of type C is a full animation. A take of type L is a lean: the
game adds it to a full animation when the Hand moves to the side (lr)
or to the front and the back (fb). Here a lean is on the rest hand.

The frame rate is 30. The duration is the number in the game's file.
It is most likely in milliseconds.

  Take                 Type  Group         Duration  Frames in the game
  00_wiggle            C     standard        266       2
  01_wiggle_lr         L     standard        533       3
  02_wiggle_fb         L     standard        533       5
  03_point             C     standard        533       4
  06_hold_above        C     standard        533       4
  09_hold_side         C     standard        533       3
  12_can_pickup        C     standard        533       3
  13_can_pickup_lr     L     standard        533       3
  14_can_pickup_fb     L     standard        533       5
  15_hold_fingers      C     standard        526       2
  16_hold_fingers_lr   L     standard        533       3
  17_hold_fingers_fb   L     standard        533       5
  24_stroke            C     reward          533       2
  25_stroke_lr         L     reward          533       5
  27_tickle            C     reward          533       4
  33_slap              C     punish          533       4
  34_slap_lr           L     punish          533       5
  42_grip              C     camera          533       3
  43_grip_lr           L     camera          533       3
  44_grip_fb           L     camera          533       3
  45_rotate            C     camera          533       3
  48_pitch             C     camera          533       3
  51_zoom              C     camera          533       3
  57_tap_house         C     individual      421       5
  60_horn              C     special_hold    266       2
  63_phile             C     special_hold    266       2

A take that loops ends with its first frame again. The hand of the
mouse player is 00_wiggle. A held object uses 09_hold_side. The land
drag uses 42_grip.

In Blender each take is an action. To see one, select the object
`hand_armature`, open the Dope Sheet, set the mode to Action Editor
and select the action. The old FBX importer of Blender also makes 26
actions with `Key` in the name. They hold the shape keys at 0, and
you can delete them.


HOW TO EDIT THE HAND
--------------------

 1. Open bw_hand.blend in Blender 4.5 or later.
 2. Select the object `hand` and go to Edit Mode.
 3. To close the seams before you add polygons, select all and use
    Mesh > Clean Up > Merge by Distance. Blender keeps the texture
    points.
 4. Edit the mesh. Add polygons as you like. Blender gives each new
    vertex the bones and the shape keys of the vertices around it.
 5. Do not move, rename, add or delete a bone. The mod finds each
    bone by its name.
 6. Keep the shape keys `good` and `evil`. After an edit, set each
    one to 1 and look at the hand.
 7. A vertex can have up to 4 bones. To make a joint bend smoothly,
    paint the weights of the vertices near the joint.
 8. Play some takes and look at the joints.

The texture points must stay on the texture of the game, because the
mod draws your mesh with the textures of the game.

Blender cannot apply a modifier to a mesh with shape keys. So add
polygons with the tools of Edit Mode (for example Edge > Subdivide),
not with a Subdivision Surface modifier.


THE EDITED HAND IN THE MOD
--------------------------

The mod reads one file: bwvr-hand.glb in the game folder, beside
runblack.exe, ddraw.dll and bwvr.ini. When the file is there and it
fits, both hands have its mesh. The hands keep the bones, the
textures and the light of the game.

 1. In Blender, use File > Export > glTF 2.0 (.glb/.gltf).
 2. Set Format to glTF Binary (.glb). Keep each other setting as it
    is.
 3. Give the file the name bwvr-hand.glb.
 4. Put the file in the game folder.
 5. Start the game in VR.
 6. Open bwvr.log in the game folder and find the lines that start
    with `hand model`. One line says that bwvr-hand.glb is the mesh
    of the hands, or it gives the reason why the file is not used.

What the mod reads from the file:

  - The first mesh with a skin: the positions, the normals and the
    texture points of each vertex, and the triangles.
  - The bones of each vertex, up to 4, and their weights. The mod
    finds a bone by its name, b00 to b21.
  - The shape keys with the names `evil` and `good`. Without the
    shape key `evil`, an evil hand has the form of the base hand.

What the mod does not read: the textures, the materials and the
takes. The pose of the fingers comes from the controllers.

The rules for the file:

  - Each of the 22 bones must be in the file, with its name, and at
    its place. The limit is 4 mm on a hand of 19 cm.
  - Each vertex must have a bone of the hand.
  - The mesh can have 20000 vertices and 20000 triangles at most.
  - The size and the axes of the file do not matter. The mod finds
    them from the places of the bones.

To see the mod read a file, copy example\bwvr-hand.glb to the game
folder.

To compare the hands in the headset, set test_keys=1 in bwvr.ini.
Ctrl and the minus key of the number pad then go through your mesh,
the mesh of the game and the plain hands. Shift and the minus key of
the number pad step the look: your alignment, evil, neutral, good.

To go back to the mesh of the game, remove bwvr-hand.glb or set
hand_model_file=0 in bwvr.ini.

A mesh that you make from these files is still the art of the game.
Do not put bwvr-hand.glb in the installer or on the internet.


HOW TO MAKE THESE FILES AGAIN
-----------------------------

In the folder of the repository:

    python bwvr/tools/hand_export/export.py

The tool needs Python with numpy and Pillow, and Blender. It writes
this folder again and it runs the check. The format of the game's
files is in bwvr/docs/hand-model.md, "The files of the Hand".
