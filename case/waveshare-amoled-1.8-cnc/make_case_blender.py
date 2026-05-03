import bpy, math, os, struct
from mathutils import Vector

OUT_DIR = os.path.dirname(os.path.abspath(__file__))
BOARD_STL = "/Users/miniclaw/Downloads/Waveshare_ ESP32-S3-Touch-AMOLED-1.8 (29957).stl"

# Dimensions derived from the downloaded Tinkercad STL bbox:
# source bbox: X 41.2mm, Y 10.9mm thick, Z 34.2mm tall.
BOARD_W = 41.2
BOARD_H = 34.2
BOARD_T = 10.9

# CNC case v0.1 dimensions, mm
OUTER_W = 56.0
OUTER_H = 50.0
BOTTOM_Z = 14.0
TOP_Z = 4.0
FLOOR_Z = 1.8
CAVITY_W = BOARD_W + 1.4     # 0.7mm total clearance per side-ish
CAVITY_H = BOARD_H + 1.4
CAVITY_D = 12.2              # room for board thickness + insulation
CORNER_R = 4.0
BEVEL = 0.8
SCREEN_W = 31.5              # first-pass display/bezel window
SCREEN_H = 36.5
SCREW_X = 23.0
SCREW_Y = 20.5
TAP_D = 2.05                 # M2.5 tap drill-ish
CLEAR_D = 2.8                # M2.5 clearance
CBORE_D = 5.2
CBORE_DPT = 2.0
USB_W = 13.0
USB_H = 5.8
USB_Z = 6.0
USB_SLOT_HEIGHT = 7.0

bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete()
bpy.context.scene.unit_settings.system = 'METRIC'
bpy.context.scene.unit_settings.scale_length = 0.001

# ---------- helpers ----------
def apply(obj):
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.select_set(False)
    return obj

def cube(name, dims, loc):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = dims
    apply(obj)
    return obj

def cyl(name, radius, depth, loc, vertices=64):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth, location=loc)
    obj = bpy.context.object
    obj.name = name
    apply(obj)
    return obj

def bevel_obj(obj, amount=BEVEL, segments=5):
    mod = obj.modifiers.new('small CNC edge breaks', 'BEVEL')
    mod.width = amount
    mod.segments = segments
    mod.affect = 'EDGES'
    mod.profile = 0.5
    mod2 = obj.modifiers.new('weighted normals', 'WEIGHTED_NORMAL')
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.ops.object.modifier_apply(modifier=mod2.name)
    obj.select_set(False)
    return obj

def boolean_diff(base, cutter):
    mod = base.modifiers.new('cut_' + cutter.name[:20], 'BOOLEAN')
    mod.operation = 'DIFFERENCE'
    mod.object = cutter
    bpy.context.view_layer.objects.active = base
    base.select_set(True)
    bpy.ops.object.modifier_apply(modifier=mod.name)
    base.select_set(False)
    cutter.hide_viewport = True
    cutter.hide_render = True
    return base

def material(name, color):
    m = bpy.data.materials.new(name)
    m.diffuse_color = color
    return m

alum = material('6061 aluminum satin', (0.65, 0.68, 0.70, 1))
blue = material('board reference blue', (0.05, 0.25, 0.9, 0.35))
black = material('screen black', (0.0, 0.0, 0.0, 1))

# ---------- bottom tray ----------
bottom = cube('bottom_tray_cnc_aluminum', (OUTER_W, OUTER_H, BOTTOM_Z), (0,0,BOTTOM_Z/2))
bottom.data.materials.append(alum)
# internal pocket from top
pocket = cube('board_cavity_clearance', (CAVITY_W, CAVITY_H, CAVITY_D), (0,0,FLOOR_Z + CAVITY_D/2))
boolean_diff(bottom, pocket)
# top-down USB notch at lower/front edge; intentionally open to front wall and top-pocket area
usb = cube('usb_c_open_edge_notch', (USB_W, 8.0, USB_SLOT_HEIGHT), (0, -OUTER_H/2 + 2.0, USB_Z))
boolean_diff(bottom, usb)
# blind M2.5 tap-drill holes from top side
for sx in (-SCREW_X, SCREW_X):
    for sy in (-SCREW_Y, SCREW_Y):
        h = cyl('m2_5_tap_drill_blind', TAP_D/2, 9.5, (sx, sy, BOTTOM_Z - 9.5/2 + 0.05), 48)
        boolean_diff(bottom, h)
bevel_obj(bottom, 0.65, 4)

# ---------- top bezel ----------
top = cube('top_bezel_cnc_aluminum', (OUTER_W, OUTER_H, TOP_Z), (0,0,BOTTOM_Z + TOP_Z/2))
top.data.materials.append(alum)
window = cube('display_window_cutout', (SCREEN_W, SCREEN_H, TOP_Z+2), (0,0,BOTTOM_Z + TOP_Z/2))
boolean_diff(top, window)
# screw through holes + counterbores from top
for sx in (-SCREW_X, SCREW_X):
    for sy in (-SCREW_Y, SCREW_Y):
        h = cyl('m2_5_clearance_through', CLEAR_D/2, TOP_Z+2, (sx, sy, BOTTOM_Z + TOP_Z/2), 48)
        boolean_diff(top, h)
        cb = cyl('m2_5_counterbore', CBORE_D/2, CBORE_DPT+0.1, (sx, sy, BOTTOM_Z + TOP_Z - CBORE_DPT/2 + 0.05), 64)
        boolean_diff(top, cb)
bevel_obj(top, 0.45, 4)

# ---------- board reference import ----------
if os.path.exists(BOARD_STL):
    bpy.ops.wm.stl_import(filepath=BOARD_STL)
    board = bpy.context.object
    board.name = 'waveshare_board_reference_do_not_machine'
    board.data.materials.append(blue)
    # Source axes: X=width, Z=height, Y=thickness. Rotate so source Z -> case Y, source Y -> case Z.
    # new = (oldX, oldZ, oldY). This rotation around X by -90 maps oldY -> newZ, oldZ -> -newY;
    # then mirror Y by scale -1 to keep orientation readable.
    board.rotation_euler[0] = math.radians(-90)
    board.scale[1] = -1
    apply(board)
    # center it in X/Y and put bottom at floor + 0.35mm insulation clearance
    mins = [min((board.matrix_world @ v.co)[i] for v in board.data.vertices) for i in range(3)]
    maxs = [max((board.matrix_world @ v.co)[i] for v in board.data.vertices) for i in range(3)]
    center = [(mins[i]+maxs[i])/2 for i in range(3)]
    board.location.x -= center[0]
    board.location.y -= center[1]
    # place bottom at floor + insulation allowance
    mins2 = [min((board.matrix_world @ v.co)[i] for v in board.data.vertices) for i in range(3)]
    board.location.z += (FLOOR_Z + 0.35 - mins2[2])

# Add simple black screen reference in top window
screen = cube('screen_opening_reference', (SCREEN_W-1.5, SCREEN_H-1.5, 0.4), (0,0,BOTTOM_Z+TOP_Z+0.25))
screen.data.materials.append(black)

# ---------- export ----------
def export_stl(obj, name):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.wm.stl_export(filepath=os.path.join(OUT_DIR, name), export_selected_objects=True)

export_stl(bottom, 'waveshare_amoled_1p8_cnc_bottom_tray_v0_1.stl')
export_stl(top, 'waveshare_amoled_1p8_cnc_top_bezel_v0_1.stl')
# assembly preview
bpy.ops.object.select_all(action='DESELECT')
for obj in bpy.context.scene.objects:
    if not obj.hide_viewport and obj.type == 'MESH':
        obj.select_set(True)
bpy.ops.wm.stl_export(filepath=os.path.join(OUT_DIR, 'waveshare_amoled_1p8_cnc_assembly_preview_v0_1.stl'), export_selected_objects=True)

# save editable blend
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT_DIR, 'waveshare_amoled_1p8_cnc_case_v0_1.blend'))
print('Exported CNC case files to', OUT_DIR)
