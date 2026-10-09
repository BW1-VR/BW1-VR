# Runs in Blender, in the background. export.py starts it.
#
#     blender --background --factory-startup --python blender_example.py --
#         <bw_hand.blend> <output .glb>
#
# It makes an example of an edited hand, with the steps that the README gives
# to the person who edits the hand: open bw_hand.blend, merge the seams of the texture, cut each
# triangle in four, and export a glTF binary with the settings of the menu.
# The mod reads the result as bwvr-hand.glb.
import sys

import bpy

arguments = sys.argv[sys.argv.index('--') + 1:]
BLEND, OUT = arguments[0], arguments[1]

bpy.ops.wm.open_mainfile(filepath=BLEND)
hand = bpy.data.objects['hand']
bpy.context.view_layer.objects.active = hand
for item in bpy.context.scene.objects:
    item.select_set(item == hand)
before = (len(hand.data.vertices), len(hand.data.polygons))
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.mesh.remove_doubles(threshold=0.00001)
bpy.ops.mesh.subdivide(number_cuts=1)
bpy.ops.object.mode_set(mode='OBJECT')
after = (len(hand.data.vertices), len(hand.data.polygons))
shared = sum(1 for vertex in hand.data.vertices
             if sum(1 for group in vertex.groups if group.weight > 0.001) > 1)
bpy.ops.export_scene.gltf(filepath=OUT, export_format='GLB')
print('EXAMPLE: %d vertices and %d faces before, %d vertices and %d faces '
      'after, %d vertices with 2 bones or more, Blender %s'
      % (before + after + (shared, bpy.app.version_string)))
