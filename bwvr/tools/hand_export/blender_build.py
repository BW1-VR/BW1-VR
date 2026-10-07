# Runs in Blender, in the background. export.py starts it.
#
#     blender --background --factory-startup --python blender_build.py --
#         <hand_data.json> <output folder> <main | good | evil>
#
# `main` makes the hand with its bones, its skin, the two shape keys of the
# alignment and each animation of the game as an action. It writes
# bw_hand.blend, bw_hand.fbx and bw_hand.glb. `good` and `evil` make the hand
# of one look, with its texture and no animation, in the folder `looks`.
import json
import os
import sys

import bpy
from mathutils import Matrix, Vector

arguments = sys.argv[sys.argv.index('--') + 1:]
JSON_PATH, OUT_DIR, LOOK = arguments[0], arguments[1], arguments[2]

with open(JSON_PATH, 'r') as handle:
    DATA = json.load(handle)
BONES = DATA['bones']


def action_fcurves(action):
    """The curves of an action, for the Blender versions with and without layers."""
    curves = []
    if getattr(action, 'layers', None):
        for layer in action.layers:
            for strip in layer.strips:
                for bag in strip.channelbags:
                    curves.extend(bag.fcurves)
        return curves
    return list(action.fcurves)


def make_armature(scene):
    armature = bpy.data.armatures.new('hand_bones')
    armature_object = bpy.data.objects.new('hand_armature', armature)
    scene.collection.objects.link(armature_object)
    bpy.context.view_layer.objects.active = armature_object
    bpy.ops.object.mode_set(mode='EDIT')
    made = []
    for bone in BONES:
        edit_bone = armature.edit_bones.new(bone['name'])
        edit_bone.head = bone['head']
        edit_bone.tail = bone['tail']
        edit_bone.align_roll(Vector(bone['z_axis']))
        edit_bone.use_connect = False
        if bone['parent'] >= 0:
            edit_bone.parent = made[bone['parent']]
        made.append(edit_bone)
    bpy.ops.object.mode_set(mode='OBJECT')
    return armature_object


def make_material(look):
    folder = os.path.join(OUT_DIR, 'textures')
    images = {}
    for name in ('base', 'good', 'evil'):
        image = bpy.data.images.load(os.path.join(folder, 'hand_%s.png' % name))
        image.name = 'hand_%s' % name
        image.use_fake_user = True
        images[name] = image
    material = bpy.data.materials.new('hand')
    material.use_nodes = True
    tree = material.node_tree
    shader = tree.nodes['Principled BSDF']
    texture = tree.nodes.new('ShaderNodeTexImage')
    texture.image = images['base' if look == 'main' else look]
    texture.location = (-400, 300)
    tree.links.new(texture.outputs['Color'], shader.inputs['Base Color'])
    # The alpha of the texture makes the wrist fade, as in the game.
    tree.links.new(texture.outputs['Alpha'], shader.inputs['Alpha'])
    shader.inputs['Roughness'].default_value = 1.0
    for name in ('Specular IOR Level', 'Specular'):
        if name in shader.inputs:
            shader.inputs[name].default_value = 0.0
    material.use_backface_culling = True
    if hasattr(material, 'surface_render_method'):
        material.surface_render_method = 'BLENDED'
    if hasattr(material, 'blend_method'):
        material.blend_method = 'BLEND'
    return material


def make_mesh(scene, armature_object, look):
    vertices = DATA['vertices'] if look == 'main' else DATA['shapes'][look]
    normals = DATA['normals'] if look == 'main' else DATA['shape_normals'][look]
    mesh = bpy.data.meshes.new('hand')
    mesh.from_pydata(vertices, [], DATA['triangles'])
    mesh.update()
    layer = mesh.uv_layers.new(name='UVMap')
    for loop in mesh.loops:
        layer.data[loop.index].uv = DATA['uvs'][loop.vertex_index]
    for polygon in mesh.polygons:
        polygon.use_smooth = True
    mesh.normals_split_custom_set_from_vertices(normals)
    mesh.materials.append(make_material(look))

    mesh_object = bpy.data.objects.new('hand', mesh)
    scene.collection.objects.link(mesh_object)
    for bone in BONES:
        mesh_object.vertex_groups.new(name=bone['name'])
    for vertex, bone in enumerate(DATA['vertex_bone']):
        mesh_object.vertex_groups[bone].add([vertex], 1.0, 'REPLACE')
    mesh_object.parent = armature_object
    modifier = mesh_object.modifiers.new('Armature', 'ARMATURE')
    modifier.object = armature_object

    if look == 'main':
        mesh_object.shape_key_add(name='Basis')
        for name in ('good', 'evil'):
            key = mesh_object.shape_key_add(name=name)
            for vertex, place in enumerate(DATA['shapes'][name]):
                key.data[vertex].co = place
            key.value = 0.0
    return mesh_object


def make_actions(scene, armature_object):
    armature = armature_object.data
    rest = [armature.bones[bone['name']].matrix_local.copy() for bone in BONES]
    pose_bones = [armature_object.pose.bones[bone['name']] for bone in BONES]
    for pose_bone in pose_bones:
        pose_bone.rotation_mode = 'QUATERNION'
    animation = armature_object.animation_data_create()
    last_frame = 1
    for entry in DATA['actions']:
        action = bpy.data.actions.new(entry['name'])
        action.use_fake_user = True
        animation.action = action
        before = {}
        for key in entry['keys']:
            # The pose of each bone in the frame of the armature.
            pose = [Matrix(key['skin'][index]) @ rest[index]
                    for index in range(len(BONES))]
            for index, bone in enumerate(BONES):
                parent = bone['parent']
                if parent < 0:
                    basis = rest[index].inverted() @ pose[index]
                else:
                    basis = (pose[parent] @ rest[parent].inverted()
                             @ rest[index]).inverted() @ pose[index]
                location, rotation, _ = basis.decompose()
                # Keep each quaternion on the side of the one before it, so
                # the blend between two keys takes the short way.
                if index in before and before[index].dot(rotation) < 0.0:
                    rotation.negate()
                before[index] = rotation.copy()
                pose_bone = pose_bones[index]
                pose_bone.location = location
                pose_bone.rotation_quaternion = rotation
                pose_bone.keyframe_insert('location', frame=key['frame'],
                                          group=bone['name'])
                pose_bone.keyframe_insert('rotation_quaternion', frame=key['frame'],
                                          group=bone['name'])
            last_frame = max(last_frame, key['frame'])
        for curve in action_fcurves(action):
            for point in curve.keyframe_points:
                point.interpolation = 'LINEAR'
        action.use_frame_range = True
        action.frame_start = entry['keys'][0]['frame']
        action.frame_end = entry['keys'][-1]['frame']
        action.use_cyclic = entry['loop']
    # The file opens with the hand at rest and no action in use.
    animation.action = None
    for pose_bone in pose_bones:
        pose_bone.location = (0.0, 0.0, 0.0)
        pose_bone.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
    scene.frame_start = 1
    scene.frame_end = last_frame
    scene.frame_set(1)


def known(operator, options):
    """Only the options that this Blender version has."""
    names = operator.get_rna_type().properties.keys()
    return {name: value for name, value in options.items() if name in names}


def export(look):
    if look == 'main':
        folder = OUT_DIR
        stem = 'bw_hand'
    else:
        folder = os.path.join(OUT_DIR, 'looks')
        stem = 'bw_hand_%s' % look
    os.makedirs(folder, exist_ok=True)
    animated = look == 'main'

    bpy.ops.export_scene.fbx(**known(bpy.ops.export_scene.fbx, {
        'filepath': os.path.join(folder, stem + '.fbx'),
        'object_types': {'ARMATURE', 'MESH'},
        'use_mesh_modifiers': False,
        'mesh_smooth_type': 'OFF',
        'add_leaf_bones': False,
        'use_armature_deform_only': False,
        'bake_anim': animated,
        'bake_anim_use_all_bones': True,
        'bake_anim_use_nla_strips': False,
        'bake_anim_use_all_actions': True,
        # A take of the game can hold one pose, with no motion. The start and
        # end keys and the simplify factor of 0 keep such a take in the file.
        # They also keep one curve for each shape key in each take.
        'bake_anim_force_startend_keying': True,
        'bake_anim_step': 1.0,
        'bake_anim_simplify_factor': 0.0,
        'path_mode': 'COPY',
        'embed_textures': True,
    }))
    bpy.ops.export_scene.gltf(**known(bpy.ops.export_scene.gltf, {
        'filepath': os.path.join(folder, stem + '.glb'),
        'export_format': 'GLB',
        'export_yup': True,
        'export_apply': False,
        'export_normals': True,
        'export_texcoords': True,
        'export_materials': 'EXPORT',
        'export_skins': True,
        'export_def_bones': False,
        'export_morph': True,
        'export_morph_normal': True,
        'export_animations': animated,
        'export_animation_mode': 'ACTIONS',
        'export_force_sampling': True,
        'export_frame_range': False,
        'export_anim_slide_to_zero': True,
        'export_optimize_animation_size': False,
        'export_reset_pose_bones': True,
    }))
    if look == 'main':
        bpy.ops.file.pack_all()
        # No backup copy (.blend1) when the tool writes the file again.
        bpy.context.preferences.filepaths.save_version = 0
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, stem + '.blend'))


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.fps = DATA['fps']
    scene.unit_settings.system = 'METRIC'
    scene.unit_settings.scale_length = 1.0
    armature_object = make_armature(scene)
    make_mesh(scene, armature_object, LOOK)
    if LOOK == 'main':
        make_actions(scene, armature_object)
    export(LOOK)
    print('BUILD DONE', LOOK, bpy.app.version_string)


main()
