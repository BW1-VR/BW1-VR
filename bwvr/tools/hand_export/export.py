# Exports the Hand of Black & White as FBX and glTF, from the game's own files.
#
#     python bwvr/tools/hand_export/export.py
#
# It reads the three hand meshes, the animations and the textures from the
# game folder. It writes the textures as PNG and a data file as JSON, and it
# starts Blender in the background. The first Blender makes the scene and
# writes the .blend, the FBX and the GLB. The second Blender, a different
# version if there is one, reads the FBX and the GLB back, compares them with
# the data of the game and renders the pictures. It also makes an example of
# an edited hand, which the mod can read as bwvr-hand.glb.
#
# The output is the art of the game. It goes to media\hand, which git does not
# track. Do not commit it and do not ship it.
#
# The format facts are in bwvr/docs/hand-model.md, "The files of the Hand".
import argparse
import glob
import json
import os
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw

import bw_hand

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))

# The frame of the files. The game's mesh frame is left handed: +x to the
# thumb, +y from the wrist to the finger tips, +z out of the palm. Blender is
# right handed with +z up, so a point (x, y, z) of the mesh goes to
# (-x, -z, y) in Blender. Blender's exporters then write +y up, and the files
# hold (-x, y, z): the fingers along +y, the palm to +z, the thumb to -x.
TO_BLENDER = np.array([[-1.0, 0.0, 0.0],
                       [0.0, 0.0, -1.0],
                       [0.0, 1.0, 0.0]])

FPS = 30

# The next bone of each bone along its chain. It gives the length of the bone
# in Blender. A bone that is not here is the last bone of a chain.
MAIN_CHILD = {0: 2, 2: 7, 3: 20, 4: 17, 5: 14, 6: 11, 7: 8, 8: 9, 9: 10,
              11: 12, 12: 13, 14: 15, 15: 16, 17: 18, 18: 19, 20: 21}
# The attach bone has no vertices and no child, so it has no length of its own.
ATTACH_LENGTH_UNITS = 40.0


def find_blender(prefer):
    """The path of a Blender, the preferred version first, then the newest."""
    root = os.path.join(os.environ.get('ProgramFiles', r'C:\Program Files'),
                        'Blender Foundation')
    found = sorted(glob.glob(os.path.join(root, 'Blender *', 'blender.exe')))
    for path in found:
        if os.path.basename(os.path.dirname(path)) == 'Blender ' + prefer:
            return path
    return found[-1] if found else None


def to_blender_points(points, scale):
    return (np.asarray(points) @ TO_BLENDER.T) * scale


def to_blender_vectors(vectors):
    return np.asarray(vectors) @ TO_BLENDER.T


def skin_matrix(rest_world, pose_world, scale):
    """The move of a bone from its rest pose, as a Blender matrix.

    `inverse(rest) @ pose` takes a point of the rest mesh to its posed place,
    in the game's row convention. Blender uses columns, so the transpose goes
    between the change of frame and its inverse.
    """
    row = np.linalg.inv(rest_world) @ pose_world
    column = row.T
    frame = np.eye(4)
    frame[:3, :3] = TO_BLENDER * scale
    return frame @ column @ np.linalg.inv(frame)


def bone_length(hand, bone):
    base = hand.meshes['base']
    if bone in MAIN_CHILD:
        return float(np.linalg.norm(base.local[MAIN_CHILD[bone]][3, :3]))
    own = base.positions[base.vertex_bone == bone]
    if len(own) == 0:
        return ATTACH_LENGTH_UNITS
    return float(own[:, 2].max())


def build_data(hand, hand_cm):
    """All that Blender needs, in Blender's frame and at the real size."""
    base = hand.meshes['base']
    scale = (hand_cm / 100.0) / bw_hand.HAND_LENGTH_UNITS
    data = {
        'scale': scale,
        'hand_cm': hand_cm,
        'hand_length_units': bw_hand.HAND_LENGTH_UNITS,
        'fps': FPS,
    }

    bones = []
    for index, name in enumerate(bw_hand.BONE_NAMES):
        world = hand.rest_world[index]
        head = to_blender_points(world[3, :3], scale)
        # The stored bone has its length along its own z, and a finger bends
        # about its own x. A Blender bone has its length along y. The stored
        # frame is mirrored, so -x, z and y of the stored bone make a right
        # handed frame with the bend about x.
        x_axis = -to_blender_vectors(world[0, :3])
        y_axis = to_blender_vectors(world[2, :3])
        z_axis = to_blender_vectors(world[1, :3])
        length = bone_length(hand, index)
        bones.append({
            'name': name,
            'index': index,
            'parent': hand.parent[index],
            'head': head.tolist(),
            'tail': (head + y_axis * length * scale).tolist(),
            'x_axis': x_axis.tolist(),
            'z_axis': z_axis.tolist(),
            'length_units': length,
            'vertex_count': int((base.vertex_bone == index).sum()),
        })
    data['bones'] = bones

    data['vertices'] = to_blender_points(bw_hand.rest_positions(base), scale).tolist()
    data['normals'] = to_blender_vectors(bw_hand.rest_normals(base)).tolist()
    # The stored texture point has v down. Blender has v up.
    data['uvs'] = [[float(u), 1.0 - float(v)] for u, v in base.uvs]
    # The change of frame is a mirror, so the order of each triangle turns.
    data['triangles'] = [[int(a), int(c), int(b)] for a, b, c in base.triangles]
    data['vertex_bone'] = [int(bone) for bone in base.vertex_bone]

    data['shapes'] = {}
    data['shape_normals'] = {}
    for name in ('good', 'evil'):
        other = hand.meshes[name]
        data['shapes'][name] = to_blender_points(
            bw_hand.rest_positions(base, other.positions), scale).tolist()
        data['shape_normals'][name] = to_blender_vectors(
            bw_hand.rest_normals(base, other.normals)).tolist()

    homogeneous = np.hstack([base.positions, np.ones((len(base.positions), 1))])
    actions = []
    for index in sorted(hand.anims):
        anim = hand.anims[index]
        times = anim.key_times()
        frames_done = []
        keys = []
        order = list(range(anim.frame_count))
        if anim.loop:
            # The last frame of a loop blends back to the first, so the take
            # ends with the first frame again, at the full duration.
            order.append(0)
            times = times + [float(anim.duration)]
        for step, frame in enumerate(order):
            world = bw_hand.pose_world(hand, anim, frame)
            number = 1 + int(round(times[step] * FPS / 1000.0))
            if frames_done and number <= frames_done[-1]:
                number = frames_done[-1] + 1
            frames_done.append(number)
            posed = np.zeros((len(base.positions), 3))
            for vertex in range(len(base.positions)):
                posed[vertex] = (homogeneous[vertex]
                                 @ world[base.vertex_bone[vertex]])[:3]
            keys.append({
                'frame': number,
                'time': times[step],
                'game_frame': frame,
                'skin': [skin_matrix(hand.rest_world[bone], world[bone], scale).tolist()
                         for bone in range(len(world))],
                'expected': to_blender_points(posed, scale).tolist(),
            })
        actions.append({
            'name': '%02d_%s' % (index, anim.name),
            'index': index,
            'category': anim.category,
            'type': anim.type,
            'loop': bool(anim.loop),
            'duration': anim.duration,
            'game_frames': anim.frame_count,
            'turn_bones': anim.turn_bones,
            'move_bones': anim.move_bones,
            'keys': keys,
        })
    data['actions'] = actions

    # The data as the game stores it, for a reader that wants the source.
    data['game'] = {
        'hbn': {key: hand.hbn[key] for key in
                ('hand_version', 'data_version', 'morph_version', 'mesh_names')},
        'vertex_count': len(base.positions),
        'triangle_count': len(base.triangles),
        'bone_count': base.bone_count,
        'bone_groups': [[int(count), int(bone)] for count, bone in base.groups],
        'bones': [{'parent': hand.parent[index],
                   'local': base.local[index][:, :3].tolist(),
                   'rest': hand.rest_world[index][:, :3].tolist()}
                  for index in range(base.bone_count)],
        'animations': [{'index': index,
                        'category': hand.spec[index][0],
                        'name': hand.spec[index][1],
                        'type': hand.spec[index][2],
                        'in_file': index in hand.anims}
                       for index in range(len(hand.spec))],
    }
    return data


def write_textures(hand, folder):
    os.makedirs(folder, exist_ok=True)
    for name in bw_hand.MESH_NAMES:
        _, texels = hand.meshes[name].skins[0]
        Image.fromarray(bw_hand.texels_to_rgba(texels)).save(
            os.path.join(folder, 'hand_%s.png' % name))


def make_sheets(out_dir, action_names, game_picture):
    """Put the renders of the check on three sheets."""
    folder = os.path.join(out_dir, 'preview')

    def tile(name, size):
        return Image.open(os.path.join(folder, name)).convert('RGB').resize((size, size))

    views = ('palm', 'back', 'thumb_side', 'three_quarter')
    sheet = Image.new('RGB', (384 * len(views), 384 * 3))
    for row, look in enumerate(bw_hand.MESH_NAMES):
        for column, view in enumerate(views):
            sheet.paste(tile('rest_%s_%s.png' % (look, view), 384), (384 * column, 384 * row))
    sheet.save(os.path.join(folder, 'sheet_looks.png'))

    columns = 7
    rows = (len(action_names) + columns - 1) // columns
    sheet = Image.new('RGB', (240 * columns, 240 * rows), (154, 163, 176))
    draw = ImageDraw.Draw(sheet)
    for index, name in enumerate(action_names):
        place = (240 * (index % columns), 240 * (index // columns))
        sheet.paste(tile('pose_%s.png' % name, 240), place)
        draw.text((place[0] + 6, place[1] + 6), name, fill=(255, 255, 255))
    sheet.save(os.path.join(folder, 'sheet_animations.png'))

    if game_picture:
        # The game draws the stored mesh mirrored, as a right hand. So the
        # render of the file is mirrored here to compare it.
        game = Image.open(game_picture).convert('RGB')
        game.thumbnail((512, 512))
        render = tile('pose_00_wiggle_from_wrist.png', 512).transpose(Image.FLIP_LEFT_RIGHT)
        sheet = Image.new('RGB', (1024 + 16, 512 + 28), (20, 20, 20))
        sheet.paste(game, ((512 - game.width) // 2, 28 + (512 - game.height) // 2))
        sheet.paste(render, (512 + 16, 28))
        draw = ImageDraw.Draw(sheet)
        draw.text((8, 8), 'The Hand in the game (Normal state)', fill=(255, 255, 255))
        draw.text((512 + 24, 8), 'bw_hand.fbx read back, take 00_wiggle, mirrored', fill=(255, 255, 255))
        sheet.save(os.path.join(folder, 'compare_game.png'))


def write_readme(out_dir):
    """The README of the output folder: ASCII, CRLF, 71 columns."""
    with open(os.path.join(HERE, 'README_hand.txt'), 'r', encoding='ascii') as handle:
        text = handle.read()
    with open(os.path.join(out_dir, 'README.txt'), 'w', encoding='ascii',
              newline='\r\n') as handle:
        handle.write(text)


def run_blender(blender, script, arguments, log_path):
    command = [blender, '--background', '--factory-startup', '--python-exit-code', '3',
               '--python', os.path.join(HERE, script), '--'] + arguments
    with open(log_path, 'w', encoding='utf-8', errors='replace') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode != 0:
        raise SystemExit('%s failed with code %d. See %s'
                         % (script, result.returncode, log_path))


def default_game():
    """Return the game folder that dev.ps1 setup wrote down, or None."""
    path = os.path.join(REPO, 'local', 'game-path.txt')
    if not os.path.exists(path):
        return None
    with open(path, 'r', encoding='utf-8-sig') as handle:
        return handle.readline().strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', default=default_game(),
                        help='the Black & White folder (default: the folder '
                             'that dev.ps1 setup wrote down)')
    parser.add_argument('--out', default=os.path.join(REPO, 'media', 'hand'),
                        help='the folder for the files')
    parser.add_argument('--hand-cm', type=float, default=19.0,
                        help='the wrist joint to the end of the middle finger, in cm')
    parser.add_argument('--blender', default=None,
                        help='the Blender that writes the files (default: 4.5)')
    parser.add_argument('--check-blender', default=None,
                        help='the Blender that reads the files back (default: the newest)')
    parser.add_argument('--game-picture', default=None,
                        help='a picture of the Hand in the game, for the comparison sheet')
    parser.add_argument('--no-blender', action='store_true',
                        help='write only the textures and the data file')
    args = parser.parse_args()
    if not args.game:
        raise SystemExit('No game folder is set. Run this first, at the root of '
                         'the repo: .\\dev.ps1 setup -Game "<folder>". Or give '
                         '--game.')

    hand = bw_hand.load(args.game)
    os.makedirs(args.out, exist_ok=True)
    data_dir = os.path.join(args.out, 'data')
    os.makedirs(data_dir, exist_ok=True)
    write_textures(hand, os.path.join(args.out, 'textures'))
    data = build_data(hand, args.hand_cm)
    write_readme(args.out)
    json_path = os.path.join(data_dir, 'hand_data.json')
    with open(json_path, 'w') as handle:
        json.dump(data, handle)
    print('read %d vertices, %d triangles, %d bones, %d animations'
          % (len(data['vertices']), len(data['triangles']), len(data['bones']),
             len(data['actions'])))
    if args.no_blender:
        return

    blender = args.blender or find_blender('4.5')
    checker = args.check_blender or find_blender('')
    if blender is None:
        raise SystemExit('Blender was not found. Give its path with --blender.')
    for look in ('main', 'good', 'evil'):
        run_blender(blender, 'blender_build.py', [json_path, args.out, look],
                    os.path.join(data_dir, 'build_%s.log' % look))
        print('built the %s files with %s' % (look, blender))
    run_blender(checker, 'blender_check.py', [json_path, args.out],
                os.path.join(data_dir, 'check.log'))
    print('checked the files with %s' % checker)
    example_dir = os.path.join(args.out, 'example')
    os.makedirs(example_dir, exist_ok=True)
    run_blender(checker, 'blender_example.py',
                [os.path.join(args.out, 'bw_hand.blend'),
                 os.path.join(example_dir, 'bwvr-hand.glb')],
                os.path.join(data_dir, 'example.log'))
    print('made the example of an edited hand with %s' % checker)
    make_sheets(args.out, [entry['name'] for entry in data['actions']], args.game_picture)
    with open(os.path.join(args.out, 'check_report.txt'), 'r') as handle:
        sys.stdout.write(handle.read())


if __name__ == '__main__':
    main()
