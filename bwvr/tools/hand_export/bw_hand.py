# Reads the Hand of Black & White from the game's own files: the three L3D
# meshes, the animations of hh.HBN and the animation names of hndspec5.txt.
#
# The format facts are in bwvr/docs/hand-model.md, "The files of the Hand". Each
# function here follows the game's own code, and the comment gives the address.
#
# All matrices are 4 by 4 numpy arrays in the game's convention: a point is a
# row, and it goes through a matrix as `point @ matrix`. So a bone in the
# world is `local @ world of the parent`.
import math
import os
import struct

import numpy as np

MESH_NAMES = ('base', 'good', 'evil')
MESH_FILES = {
    'base': 'Hand_Boned_Base2.l3d',
    'good': 'Hand_Boned_Good2.l3d',
    'evil': 'Hand_Boned_Evil2.l3d',
}

# The game's files give the bones no names. These names are ours. The number
# is the index of the bone in the L3D file, and the mod reads the number.
BONE_NAMES = (
    'b00_wrist', 'b01_attach', 'b02_palm', 'b03_thumb1',
    'b04_index0', 'b05_ring0', 'b06_little0', 'b07_middle0',
    'b08_middle1', 'b09_middle2', 'b10_middle3',
    'b11_little1', 'b12_little2', 'b13_little3',
    'b14_ring1', 'b15_ring2', 'b16_ring3',
    'b17_index1', 'b18_index2', 'b19_index3',
    'b20_thumb2', 'b21_thumb3',
)

# The length of the hand in mesh units: the wrist joint to the end of the
# middle finger (hand.md, "The exact Hand").
HAND_LENGTH_UNITS = 623.6


def _u32s(data, offset, count):
    return struct.unpack_from('<%dI' % count, data, offset)


class Mesh:
    """One L3D file of the Hand."""


def read_l3d(path):
    """Read an L3D file with one submesh and one primitive."""
    with open(path, 'rb') as handle:
        data = handle.read()
    if data[:4] != b'L3D0':
        raise ValueError('not an L3D file: ' + path)
    mesh = Mesh()
    mesh.path = path
    mesh.flags, mesh.data_size, submesh_count, submesh_table = _u32s(data, 4, 4)
    # 32 bytes of zero follow, then the offsets of the other blocks.
    _, skin_count, skin_table, point_count, _, _ = _u32s(data, 52, 6)
    if submesh_count != 1:
        raise ValueError('expected 1 submesh, found %d' % submesh_count)

    # A texture: an id, then 256 by 256 texels of 16 bits, 4 bits each for
    # alpha, red, green and blue.
    mesh.skins = []
    for offset in _u32s(data, skin_table, skin_count):
        skin_id = _u32s(data, offset, 1)[0]
        texels = np.frombuffer(data, dtype='<u2', count=256 * 256,
                               offset=offset + 4).reshape(256, 256).copy()
        mesh.skins.append((skin_id, texels))

    submesh = _u32s(data, submesh_table, 1)[0]
    (mesh.submesh_flags, primitive_count, primitive_table, bone_count,
     bone_offset) = _u32s(data, submesh, 5)
    if primitive_count != 1:
        raise ValueError('expected 1 primitive, found %d' % primitive_count)
    primitive = _u32s(data, primitive_table, 1)[0]
    words = _u32s(data, primitive, 12)
    mesh.material = words[:4]
    (vertex_count, vertex_offset, triangle_count, triangle_offset,
     group_count, group_offset, blend_count, _) = words[4:12]
    mesh.blend_count = blend_count

    # A vertex is 32 bytes: a position, a texture point and a normal. The
    # position and the normal are in the frame of the bone of the vertex.
    raw = np.frombuffer(data, dtype='<f4', count=vertex_count * 8,
                        offset=vertex_offset).reshape(vertex_count, 8)
    raw = raw.astype(np.float64)
    mesh.positions = raw[:, 0:3].copy()
    mesh.uvs = raw[:, 3:5].copy()
    mesh.normals = raw[:, 5:8].copy()
    mesh.triangles = np.frombuffer(
        data, dtype='<u2', count=triangle_count * 3,
        offset=triangle_offset).reshape(triangle_count, 3).astype(int)

    # A bone group is a count of vertices and a bone. The groups cover the
    # vertices in order, so each vertex has one bone and no weight.
    mesh.groups = [struct.unpack_from('<HH', data, group_offset + 4 * index)
                   for index in range(group_count)]
    mesh.vertex_bone = np.zeros(vertex_count, dtype=int)
    cursor = 0
    for count, bone in mesh.groups:
        mesh.vertex_bone[cursor:cursor + count] = bone
        cursor += count
    if cursor != vertex_count:
        raise ValueError('the bone groups do not cover the vertices')

    # A bone is 60 bytes: the parent, the first child and the next bone as
    # three ints, then a matrix of 4 rows of 3 in the frame of the parent.
    mesh.parent, mesh.first_child, mesh.next_bone, mesh.local = [], [], [], []
    for index in range(bone_count):
        offset = bone_offset + 60 * index
        parent, child, sibling = struct.unpack_from('<iii', data, offset)
        values = struct.unpack_from('<12f', data, offset + 12)
        matrix = np.eye(4)
        matrix[0, :3] = values[0:3]
        matrix[1, :3] = values[3:6]
        matrix[2, :3] = values[6:9]
        matrix[3, :3] = values[9:12]
        mesh.parent.append(parent)
        mesh.first_child.append(child)
        mesh.next_bone.append(sibling)
        mesh.local.append(matrix)
    mesh.bone_count = bone_count
    return mesh


def compose(local, parent, root=None):
    """Each bone becomes `bone @ parent`, and bone 0 takes the root.

    This is fn_00839F10 and LH3DAnim::SetTransform 0x0083A1D0.
    """
    world = [None] * len(local)
    for index, matrix in enumerate(local):
        if parent[index] < 0:
            world[index] = matrix.copy() if root is None else matrix @ root
        else:
            if parent[index] >= index:
                raise ValueError('a bone comes before its parent')
            world[index] = matrix @ world[parent[index]]
    return world


def rest_positions(mesh, positions=None, world=None):
    """The vertices in the frame of the mesh, with the bones at rest."""
    if positions is None:
        positions = mesh.positions
    if world is None:
        world = compose(mesh.local, mesh.parent)
    out = np.zeros((len(positions), 3))
    for index, point in enumerate(positions):
        out[index] = (np.append(point, 1.0) @ world[mesh.vertex_bone[index]])[:3]
    return out


def rest_normals(mesh, normals=None, world=None):
    """The normals in the frame of the mesh, with the bones at rest."""
    if normals is None:
        normals = mesh.normals
    if world is None:
        world = compose(mesh.local, mesh.parent)
    out = np.zeros((len(normals), 3))
    for index, normal in enumerate(normals):
        out[index] = normal @ world[mesh.vertex_bone[index]][:3, :3]
    return out


def yxz_matrix(first, second, third):
    """LHMatrix::SetYXZMatrixOnly 0x007FAC10, with the rows as the game has them."""
    c1, s1 = math.cos(first), math.sin(first)
    c2, s2 = math.cos(second), math.sin(second)
    c3, s3 = math.cos(third), math.sin(third)
    return np.array([
        [c3 * c1 - s3 * s2 * s1, -s3 * c2, c3 * s1 + s3 * s2 * c1],
        [s3 * c1 + c3 * s2 * s1, c3 * c2, s3 * s1 - c3 * s2 * c1],
        [-c2 * s1, s2, c2 * c1],
    ])


def frame_rotation(angles):
    """The turn of one bone in one frame. The game passes (y, x, z)."""
    return yxz_matrix(angles[1], angles[0], angles[2])


class Anim:
    """One animation of the HBN file (CAnim::ReadBinary 0x00860860)."""

    def key_times(self):
        """The time of each frame, and the time of the full animation.

        fn_00860E00: an animation that loops has its frames at equal steps
        through its duration, and the last frame blends back to the first.
        An animation that does not loop has its last frame at its duration.
        """
        if self.loop:
            step = self.duration / self.frame_count
        else:
            step = self.duration / max(1, self.frame_count - 1)
        return [step * index for index in range(self.frame_count)]


def read_spec(path):
    """The animation names of hndspec5.txt, in the order of the HBN table.

    A line that starts with `=` names a category. Each other line is a type
    letter and a name. C is a full animation. L is a lean, which the game
    adds to the animation before it in the list.
    """
    names = []
    category = ''
    with open(path, 'r') as handle:
        for word in handle.read().split():
            if word[0] == '=':
                category = word[1:]
            elif word[0] in 'CLNWD-' and len(word) > 1:
                names.append((category, word[1:], word[0]))
    return names


def read_hbn(path, spec):
    """Read hh.HBN: the names of the meshes and the animations of the base mesh.

    CHand::LoadBinary 0x0046D2C0 reads two version words. Then
    Morphable::ReadBinary 0x00617AE0 reads its version, the name of the base
    mesh, six more names (the first is the evil mesh, the second the good
    mesh), and one table of offsets for each mesh that has a name
    (LH3DAnimSet::ReadBinaryNew 0x00619B80).
    """
    with open(path, 'rb') as handle:
        data = handle.read()
    if data[:8] != b'LiOnHeAd' or data[8:12] != b'Hand':
        raise ValueError('not a hand file: ' + path)
    # The segment `Hand` starts after the 8 bytes of the magic, the 32 bytes
    # of the segment name and the 4 bytes of the segment size.
    base = 0x2C
    hand_version, data_version, morph_version = _u32s(data, base, 3)
    cursor = base + 12
    names = []
    for index in range(7):
        raw = data[cursor + 32 * index:cursor + 32 * index + 32]
        names.append(raw.split(b'\0')[0].decode('ascii'))
    cursor += 7 * 32
    table = _u32s(data, cursor, len(spec))

    anims = {}
    for index, offset in enumerate(table):
        if offset == 0:
            continue
        anim = Anim()
        anim.index = index
        anim.category, anim.name, anim.type = spec[index]
        cursor = base + offset
        anim.duration, loop = _u32s(data, cursor, 2)
        anim.loop = loop & 1
        anim.extra = struct.unpack_from('<5f', data, cursor + 8)
        (anim.frame_count, anim.bone_count, turn_count,
         move_count) = _u32s(data, cursor + 0x1C, 4)
        cursor += 0x2C
        # Two lists of bones: the bones with a turn in each frame, then the
        # bones with a place in each frame.
        anim.turn_bones = list(_u32s(data, cursor, turn_count))
        cursor += 4 * turn_count
        anim.move_bones = list(_u32s(data, cursor, move_count))
        cursor += 4 * move_count
        # A frame: three angles for each bone of the first list, then three
        # floats for each bone of the second list (CFrame::ReadBinary).
        anim.turns = np.zeros((anim.frame_count, turn_count, 3))
        anim.moves = np.zeros((anim.frame_count, move_count, 3))
        for frame in range(anim.frame_count):
            anim.turns[frame] = np.frombuffer(
                data, dtype='<f4', count=turn_count * 3,
                offset=cursor).reshape(turn_count, 3)
            cursor += 12 * turn_count
            anim.moves[frame] = np.frombuffer(
                data, dtype='<f4', count=move_count * 3,
                offset=cursor).reshape(move_count, 3)
            cursor += 12 * move_count
        anims[index] = anim
    return {
        'hand_version': hand_version,
        'data_version': data_version,
        'morph_version': morph_version,
        'mesh_names': names,
        'anims': anims,
    }


def sample_local(anim, stand, rest_world, rest_inverse, parent, frame):
    """The matrix of each bone in the frame of its parent, at one frame.

    fn_00860E00 with the stand frame as the fill. The angles of a frame turn
    the bone from its rest pose, about the axes of the mesh:

        turn part = rest of the bone @ turn @ inverse rest of the parent

    The place of a bone is in the frame of its parent. A bone with no entry
    in the animation takes the turn and the place of the stand frame, which
    is frame 0 of animation 0.
    """
    out = []
    for bone in range(len(parent)):
        if bone in anim.turn_bones:
            angles = anim.turns[frame, anim.turn_bones.index(bone)]
        else:
            angles = stand.turns[0, stand.turn_bones.index(bone)]
        turn = rest_world[bone][:3, :3] @ frame_rotation(angles)
        if parent[bone] >= 0:
            turn = turn @ rest_inverse[parent[bone]][:3, :3]
        matrix = np.eye(4)
        matrix[:3, :3] = turn
        if bone in anim.move_bones:
            matrix[3, :3] = anim.moves[frame, anim.move_bones.index(bone)]
        else:
            matrix[3, :3] = stand.moves[0, stand.move_bones.index(bone)]
        out.append(matrix)
    return out


def texels_to_rgba(texels):
    """16 bit texels (4 bits each for A, R, G and B) to 8 bit RGBA."""
    alpha = ((texels >> 12) & 15) * 17
    red = ((texels >> 8) & 15) * 17
    green = ((texels >> 4) & 15) * 17
    blue = (texels & 15) * 17
    return np.dstack([red, green, blue, alpha]).astype(np.uint8)


def mix_texels(base, other, weight):
    """fn_00870640: each 4 bit channel is `(base * (255 - w) + other * w) / 255`."""
    weight = max(0, min(255, int(weight)))
    out = np.zeros_like(base)
    for shift in (0, 4, 8, 12):
        a = (base >> shift) & 15
        b = (other >> shift) & 15
        mixed = (a.astype(np.uint32) * (255 - weight) + b.astype(np.uint32) * weight) // 255
        out |= (mixed.astype(np.uint16) & 15) << shift
    return out


class Hand:
    """The three meshes, the animations and the rest pose of the Hand."""


def load(game_dir):
    """Read the Hand from a Black & White folder."""
    hand = Hand()
    mesh_dir = os.path.join(game_dir, 'Data', 'CreatureMesh')
    hand.meshes = {name: read_l3d(os.path.join(mesh_dir, MESH_FILES[name]))
                   for name in MESH_NAMES}
    base = hand.meshes['base']
    for name in ('good', 'evil'):
        other = hand.meshes[name]
        if (other.bone_count != base.bone_count
                or len(other.positions) != len(base.positions)
                or not (other.triangles == base.triangles).all()
                or other.groups != base.groups):
            raise ValueError('the %s mesh does not fit the base mesh' % name)
    if base.bone_count != len(BONE_NAMES):
        raise ValueError('expected %d bones, found %d'
                         % (len(BONE_NAMES), base.bone_count))
    hand.spec = read_spec(os.path.join(game_dir, 'Data', 'hndspec5.txt'))
    hand.hbn = read_hbn(os.path.join(game_dir, 'Data', 'CTR', 'hh.HBN'), hand.spec)
    hand.anims = hand.hbn['anims']
    hand.stand = hand.anims[0]
    hand.parent = base.parent
    hand.rest_world = compose(base.local, base.parent)
    hand.rest_inverse = [np.linalg.inv(matrix) for matrix in hand.rest_world]
    return hand


def pose_world(hand, anim, frame):
    """The matrix of each bone in the frame of the mesh, at one frame."""
    local = sample_local(anim, hand.stand, hand.rest_world, hand.rest_inverse,
                         hand.parent, frame)
    return compose(local, hand.parent)
