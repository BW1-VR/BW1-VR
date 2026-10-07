# Runs in Blender, in the background. export.py starts it.
#
#     blender --background --factory-startup --python blender_check.py --
#         <hand_data.json> <output folder>
#
# It reads bw_hand.fbx and bw_hand.glb back with Blender's importers and
# compares what it finds with the data of the game: the counts, the place of
# each vertex and each bone, the bone of each vertex, the texture points, the
# shape keys, and the posed mesh at each key of each animation. It writes
# check_report.txt, and it renders the pictures of the folder `preview` from
# the FBX that it read.
import json
import math
import os
import sys

import bpy
from mathutils import Vector

arguments = sys.argv[sys.argv.index('--') + 1:]
JSON_PATH, OUT_DIR = arguments[0], arguments[1]

with open(JSON_PATH, 'r') as handle:
    DATA = json.load(handle)
BONES = DATA['bones']
BONE_NAMES = [bone['name'] for bone in BONES]
MM = 1000.0

report = []
failures = []


def say(text):
    report.append(text)
    print(text)


def check(ok, text):
    say(('ok    ' if ok else 'FAIL  ') + text)
    if not ok:
        failures.append(text)


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    # A glTF file gives its keys in seconds, so the scene must have the frame
    # rate of the export for a key to be on its frame again.
    bpy.context.scene.render.fps = DATA['fps']


def distance(a, b):
    return (Vector(a) - Vector(b)).length


def world_vertices(mesh_object, evaluated=False):
    if evaluated:
        graph = bpy.context.evaluated_depsgraph_get()
        source = mesh_object.evaluated_get(graph)
        mesh = source.to_mesh()
        out = [source.matrix_world @ vertex.co for vertex in mesh.vertices]
        source.to_mesh_clear()
        return out
    return [mesh_object.matrix_world @ vertex.co for vertex in mesh_object.data.vertices]


def skinned_meshes():
    """The meshes with bones. The glTF importer also adds a mesh for the shape of a bone."""
    return [item for item in bpy.context.scene.objects
            if item.type == 'MESH' and len(item.vertex_groups) > 0]


def nearest_map(found, expected):
    """For each found vertex, the index of the nearest expected vertex."""
    out = []
    for place in found:
        best = min(range(len(expected)), key=lambda index: distance(place, expected[index]))
        out.append(best)
    return out


def use_action(armature_object, action):
    animation = armature_object.animation_data or armature_object.animation_data_create()
    animation.action = action
    if action is not None and hasattr(animation, 'action_slot') and animation.action_slot is None:
        for slot in getattr(action, 'slots', []):
            try:
                animation.action_slot = slot
                break
            except Exception:
                continue


def find_action(name):
    """The action of a take. The importers add the name of the armature."""
    exact = [action for action in bpy.data.actions if action.name == name]
    if exact:
        return exact[0]
    for action in bpy.data.actions:
        parts = action.name.replace('|', ' ').split()
        if name in parts or action.name.endswith(name) or action.name.startswith(name):
            return action
    return None


def check_file(label, vertex_exact):
    """Compare the scene that an importer made with the data of the game."""
    meshes = skinned_meshes()
    armatures = [item for item in bpy.context.scene.objects if item.type == 'ARMATURE']
    check(len(meshes) == 1 and len(armatures) == 1,
          '%s: 1 mesh and 1 armature (found %d and %d)' % (label, len(meshes), len(armatures)))
    if len(meshes) != 1 or len(armatures) != 1:
        return None, None
    mesh_object, armature_object = meshes[0], armatures[0]
    mesh = mesh_object.data

    # The counts.
    triangles = sum(len(polygon.vertices) - 2 for polygon in mesh.polygons)
    expected_vertices = DATA['vertices']
    if vertex_exact or len(mesh.vertices) == len(expected_vertices):
        check(len(mesh.vertices) == len(expected_vertices),
              '%s: %d vertices (the game: %d)' % (label, len(mesh.vertices), len(expected_vertices)))
    else:
        # A glTF file has one vertex for each pair of a place and a texture
        # point, so its count can be larger.
        say('note  %s: %d vertices (the game: %d)' % (label, len(mesh.vertices), len(expected_vertices)))
    check(triangles == len(DATA['triangles']),
          '%s: %d triangles (the game: %d)' % (label, triangles, len(DATA['triangles'])))
    bones = armature_object.data.bones
    names = sorted(bone.name for bone in bones)
    check(names == sorted(BONE_NAMES), '%s: %d bones with the 22 names' % (label, len(bones)))

    # The place of each vertex, with the bones at rest.
    found = world_vertices(mesh_object)
    same_order = len(found) == len(expected_vertices)
    if same_order:
        index_of = list(range(len(found)))
        worst = max(distance(found[i], expected_vertices[i]) for i in range(len(found)))
        if worst * MM > 0.01:
            same_order = False
    if not same_order:
        index_of = nearest_map(found, expected_vertices)
    worst = max(distance(found[i], expected_vertices[index_of[i]]) for i in range(len(found)))
    covered = len(set(tuple(round(c, 6) for c in expected_vertices[i]) for i in index_of))
    unique = len(set(tuple(round(c, 6) for c in place) for place in expected_vertices))
    check(worst * MM < 0.01 and covered == unique,
          '%s: each vertex is at its place, the worst is %.5f mm (%s order, %d of %d places used)'
          % (label, worst * MM, 'the same' if same_order else 'another', covered, unique))

    # The place and the parent of each bone.
    worst = 0.0
    parents_ok = True
    for bone in BONES:
        if bone['name'] not in bones:
            continue
        found_bone = bones[bone['name']]
        head = armature_object.matrix_world @ found_bone.head_local
        worst = max(worst, distance(head, bone['head']))
        parent = found_bone.parent.name if found_bone.parent else None
        expected_parent = BONES[bone['parent']]['name'] if bone['parent'] >= 0 else None
        parents_ok = parents_ok and parent == expected_parent
    check(worst * MM < 0.01, '%s: each bone joint is at its place, the worst is %.5f mm' % (label, worst * MM))
    check(parents_ok, '%s: each bone has its parent' % label)

    # The bone of each vertex.
    group_names = {group.index: group.name for group in mesh_object.vertex_groups}
    wrong = 0
    for index, vertex in enumerate(mesh.vertices):
        weights = [(group_names[item.group], item.weight) for item in vertex.groups if item.weight > 1e-6]
        expected_bone = BONE_NAMES[DATA['vertex_bone'][index_of[index]]]
        if len(weights) != 1 or weights[0][0] != expected_bone or abs(weights[0][1] - 1.0) > 1e-4:
            wrong += 1
    check(wrong == 0, '%s: each vertex has one bone with the weight 1, and it is the bone of the game (%d wrong)'
          % (label, wrong))

    # The texture points and the normals.
    layer = mesh.uv_layers.active
    worst_uv = 0.0
    worst_normal = 0.0
    corner_normals = getattr(mesh, 'corner_normals', None)
    for loop in mesh.loops:
        expected_index = index_of[loop.vertex_index]
        uv = layer.data[loop.index].uv
        expected_uv = DATA['uvs'][expected_index]
        worst_uv = max(worst_uv, abs(uv[0] - expected_uv[0]), abs(uv[1] - expected_uv[1]))
        normal = corner_normals[loop.index].vector if corner_normals else loop.normal
        normal = (mesh_object.matrix_world.to_3x3() @ Vector(normal)).normalized()
        cosine = max(-1.0, min(1.0, normal.dot(Vector(DATA['normals'][expected_index]))))
        worst_normal = max(worst_normal, math.degrees(math.acos(cosine)))
    if same_order:
        check(worst_uv < 1e-4, '%s: each texture point is that of the game, the worst is %.6f' % (label, worst_uv))
    else:
        say('note  %s: the texture points are not compared, because two vertices of the game can '
            'share one place' % label)
    if same_order:
        check(worst_normal < 0.5, '%s: each normal is that of the game, the worst is %.3f degrees'
              % (label, worst_normal))

    # The shape keys of the alignment.
    keys = mesh.shape_keys.key_blocks if mesh.shape_keys else []
    key_names = [key.name for key in keys]
    check('good' in key_names and 'evil' in key_names, '%s: the shape keys %s' % (label, key_names))
    for name in ('good', 'evil'):
        if name not in key_names:
            continue
        key = keys[name]
        expected_shape = DATA['shapes'][name]
        worst = 0.0
        largest = 0.0
        for index in range(len(mesh.vertices)):
            place = mesh_object.matrix_world @ key.data[index].co
            worst = max(worst, distance(place, expected_shape[index_of[index]]))
            largest = max(largest, distance(place, found[index]))
        check(worst * MM < 0.01, '%s: the shape key `%s` is the %s mesh of the game, the worst is %.5f mm '
              '(it moves a vertex by up to %.2f mm)' % (label, name, name, worst * MM, largest * MM))

    # The material and its texture.
    images = []
    for material in mesh.materials:
        if material and material.node_tree:
            for node in material.node_tree.nodes:
                if node.type == 'TEX_IMAGE' and node.image:
                    images.append(node.image)
    check(len(images) >= 1 and tuple(images[0].size) == (256, 256),
          '%s: the material has a texture of %s' % (label, tuple(images[0].size) if images else 'none'))

    # The posed mesh at each key of each animation.
    found_actions = 0
    worst_all = 0.0
    worst_name = ''
    for entry in DATA['actions']:
        action = find_action(entry['name'])
        if action is None:
            continue
        found_actions += 1
        use_action(armature_object, action)
        first = entry['keys'][0]['frame']
        start = action.frame_range[0]
        for key in entry['keys']:
            bpy.context.scene.frame_set(int(round(start + key['frame'] - first)))
            posed = world_vertices(mesh_object, evaluated=True)
            worst = max(distance(posed[i], key['expected'][index_of[i]]) for i in range(len(posed)))
            if worst > worst_all:
                worst_all = worst
                worst_name = '%s, frame %d' % (entry['name'], key['game_frame'])
    use_action(armature_object, None)
    check(found_actions == len(DATA['actions']),
          '%s: %d of the %d animations of the game are in the file' % (label, found_actions, len(DATA['actions'])))
    extra = len(bpy.data.actions) - found_actions
    if extra:
        say('note  %s: this importer also makes %d actions for the shape keys, one for each take. '
            'They hold the value 0.' % (label, extra))
    keys_total = sum(len(entry['keys']) for entry in DATA['actions'])
    check(worst_all * MM < 0.05,
          '%s: the skinned mesh at each of the %d keys is the mesh that the game makes, the worst is '
          '%.5f mm (%s)' % (label, keys_total, worst_all * MM, worst_name))
    return mesh_object, armature_object


# --- The pictures -------------------------------------------------------

VIEWS = {
    # name: the direction from the hand to the camera, and the up direction
    'palm': ((0.0, -1.0, 0.0), (0.0, 0.0, 1.0)),
    'back': ((0.0, 1.0, 0.0), (0.0, 0.0, 1.0)),
    'thumb_side': ((-1.0, 0.0, 0.0), (0.0, 0.0, 1.0)),
    'three_quarter': ((-0.55, -0.75, 0.35), (0.0, 0.0, 1.0)),
    # From behind the wrist, at the back of the hand: the view of the mouse player.
    'from_wrist': ((0.12, 0.72, -0.68), (0.0, 0.0, 1.0)),
}


def setup_render(scene, size):
    for engine in ('BLENDER_EEVEE_NEXT', 'BLENDER_EEVEE', 'BLENDER_WORKBENCH'):
        try:
            scene.render.engine = engine
            break
        except TypeError:
            continue
    scene.render.resolution_x = size
    scene.render.resolution_y = size
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.film_transparent = False
    scene.view_settings.view_transform = 'Standard'
    world = bpy.data.worlds.new('world')
    world.use_nodes = True
    background = world.node_tree.nodes['Background']
    background.inputs['Color'].default_value = (0.32, 0.36, 0.42, 1.0)
    background.inputs['Strength'].default_value = 1.0
    scene.world = world
    sun_data = bpy.data.lights.new('sun', 'SUN')
    sun_data.energy = 2.5
    sun = bpy.data.objects.new('sun', sun_data)
    scene.collection.objects.link(sun)
    camera_data = bpy.data.cameras.new('camera')
    camera_data.type = 'ORTHO'
    camera = bpy.data.objects.new('camera', camera_data)
    scene.collection.objects.link(camera)
    scene.camera = camera
    return camera, sun


def aim(item, place, target, up):
    direction = (Vector(target) - Vector(place)).normalized()
    item.location = place
    item.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()


def render_view(scene, camera, sun, mesh_object, view, path, fit=None):
    points = fit or world_vertices(mesh_object, evaluated=True)
    low = Vector((min(p[0] for p in points), min(p[1] for p in points), min(p[2] for p in points)))
    high = Vector((max(p[0] for p in points), max(p[1] for p in points), max(p[2] for p in points)))
    middle = (low + high) / 2.0
    span = (high - low).length
    direction = Vector(VIEWS[view][0]).normalized()
    aim(camera, middle + direction * span * 3.0, middle, VIEWS[view][1])
    camera.data.ortho_scale = span * 1.05
    camera.data.clip_start = span * 0.1
    camera.data.clip_end = span * 10.0
    # The light comes from above the camera, a little to one side.
    aim(sun, middle + (direction + Vector((0.35, 0.0, 0.6))) * span * 3.0, middle, (0, 0, 1))
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


def set_look(mesh_object, look):
    keys = mesh_object.data.shape_keys.key_blocks
    keys['good'].value = 1.0 if look == 'good' else 0.0
    keys['evil'].value = 1.0 if look == 'evil' else 0.0
    image = bpy.data.images.load(os.path.join(OUT_DIR, 'textures', 'hand_%s.png' % look),
                                 check_existing=True)
    for material in mesh_object.data.materials:
        for node in material.node_tree.nodes:
            if node.type == 'TEX_IMAGE':
                node.image = image


def render_all(mesh_object, armature_object):
    folder = os.path.join(OUT_DIR, 'preview')
    os.makedirs(folder, exist_ok=True)
    scene = bpy.context.scene
    camera, sun = setup_render(scene, 512)
    use_action(armature_object, None)
    for bone in armature_object.pose.bones:
        bone.location = (0, 0, 0)
        bone.rotation_quaternion = (1, 0, 0, 0)
        bone.rotation_euler = (0, 0, 0)
    armature_object.hide_render = True
    for look in ('base', 'good', 'evil'):
        set_look(mesh_object, look)
        for view in ('palm', 'back', 'thumb_side', 'three_quarter'):
            render_view(scene, camera, sun, mesh_object, view,
                        os.path.join(folder, 'rest_%s_%s.png' % (look, view)))
    set_look(mesh_object, 'base')
    scene.render.resolution_x = 320
    scene.render.resolution_y = 320
    for entry in DATA['actions']:
        action = find_action(entry['name'])
        if action is None:
            continue
        use_action(armature_object, action)
        # The key that is the farthest from the rest pose shows the animation best.
        start = action.frame_range[0]
        first = entry['keys'][0]['frame']
        rest = DATA['vertices']
        best = max(entry['keys'], key=lambda key: sum(
            distance(key['expected'][i], rest[i]) for i in range(0, len(rest), 7)))
        scene.frame_set(int(round(start + best['frame'] - first)))
        render_view(scene, camera, sun, mesh_object, 'three_quarter',
                    os.path.join(folder, 'pose_%s.png' % entry['name']))
    # The hand of the Normal state, as the mouse player sees it.
    action = find_action(DATA['actions'][0]['name'])
    if action is not None:
        use_action(armature_object, action)
        scene.frame_set(int(round(action.frame_range[0])))
        scene.render.resolution_x = 512
        scene.render.resolution_y = 512
        render_view(scene, camera, sun, mesh_object, 'from_wrist',
                    os.path.join(folder, 'pose_00_wiggle_from_wrist.png'))
    use_action(armature_object, None)


def import_fbx(path):
    reset()
    if hasattr(bpy.ops.wm, 'fbx_import'):
        bpy.ops.wm.fbx_import(filepath=path)
        return 'wm.fbx_import'
    bpy.ops.import_scene.fbx(filepath=path)
    return 'import_scene.fbx'


def main():
    say('The check of the hand files, with Blender %s' % bpy.app.version_string)
    say('The data of the game: %d vertices, %d triangles, %d bones, %d animations'
        % (len(DATA['vertices']), len(DATA['triangles']), len(BONES), len(DATA['actions'])))
    say('')

    fbx_path = os.path.join(OUT_DIR, 'bw_hand.fbx')
    if hasattr(bpy.ops.import_scene, 'fbx') and hasattr(bpy.ops.wm, 'fbx_import'):
        # This Blender has two FBX importers. Check the file with both.
        reset()
        bpy.ops.import_scene.fbx(filepath=fbx_path)
        check_file('bw_hand.fbx (import_scene.fbx)', True)
        say('')
    importer = import_fbx(fbx_path)
    mesh_object, armature_object = check_file('bw_hand.fbx (%s)' % importer, True)
    say('')
    if mesh_object is not None:
        render_all(mesh_object, armature_object)

    reset()
    bpy.ops.import_scene.gltf(filepath=os.path.join(OUT_DIR, 'bw_hand.glb'))
    check_file('bw_hand.glb', False)
    say('')

    for look in ('good', 'evil'):
        for extension in ('fbx', 'glb'):
            path = os.path.join(OUT_DIR, 'looks', 'bw_hand_%s.%s' % (look, extension))
            if extension == 'fbx':
                import_fbx(path)
            else:
                reset()
                bpy.ops.import_scene.gltf(filepath=path)
            meshes = skinned_meshes()
            expected = DATA['shapes'][look]
            if len(meshes) != 1:
                check(False, 'looks/bw_hand_%s.%s: 1 mesh' % (look, extension))
                continue
            found = world_vertices(meshes[0])
            worst = max(min(distance(place, other) for other in expected) for place in found)
            check(worst * MM < 0.01, 'looks/bw_hand_%s.%s: %d vertices, each at its place of the %s mesh, '
                  'the worst is %.5f mm' % (look, extension, len(found), look, worst * MM))

    # The Blender file must open, with the same parts.
    bpy.ops.wm.open_mainfile(filepath=os.path.join(OUT_DIR, 'bw_hand.blend'))
    meshes = skinned_meshes()
    packed = [image.name for image in bpy.data.images if image.packed_file is not None]
    check(len(meshes) == 1 and len(meshes[0].data.vertices) == len(DATA['vertices'])
          and len(bpy.data.actions) == len(DATA['actions']) and len(packed) == 3,
          'bw_hand.blend: it opens, with %d vertices, %d actions and %d textures in the file'
          % (len(meshes[0].data.vertices) if meshes else 0, len(bpy.data.actions), len(packed)))

    say('')
    say('RESULT: %s' % ('all checks passed' if not failures else '%d checks FAILED' % len(failures)))
    with open(os.path.join(OUT_DIR, 'check_report.txt'), 'w') as handle:
        handle.write('\n'.join(report) + '\n')
    if failures:
        sys.exit(3)


main()
