# Renders the pitch and mod wheel lying on its side from wheel.blend into the skin at every scale, as
# faders/wheel_h: 128 frames stacked top to bottom, 92 x 17 px at 1x, frame 0 with the groove at the left and
# frame 127 at the right, 33 px either side of the centre. The keys view's wheels drag sideways, so right is
# up: pitch bends up to the right.
#   In Blender, with wheel.blend open (blender/scripts on sys.path): import render_wheel; render_wheel.run()
# The wheel is turned a quarter about the view axis inside its scene, under the scene's own lamps, so the
# light still falls from the top of the window.
import math, os, sys
import bpy
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import kit

FRAMES = 128
W, H = 92, 17
TRAVEL_PX = 33
DRUM_PX = 48       # drum radius; must match the Drum mesh in wheel.blend


def run(scales=kit.SCALES):
    sc = bpy.context.scene
    D = bpy.data
    drum = D.objects["Drum"]
    turn = D.objects.get("Turn")
    if turn is None:   # everything but the camera and the lamps turns a quarter clockwise: the wheel's up is right
        turn = D.objects.new("Turn", None)
        sc.collection.objects.link(turn)
        for o in D.objects:
            if o.type == 'MESH' and o.parent is None:
                o.parent = turn
    turn.rotation_euler.z = math.radians(-90)
    sc.render.image_settings.file_format = 'PNG'
    sc.render.image_settings.color_mode = 'RGBA'
    sc.render.filepath = os.path.join(bpy.app.tempdir, "wheel_frame.png")
    cam = sc.camera
    ortho = cam.data.ortho_scale   # frames the wheel's 92 px either way
    theta = math.asin(TRAVEL_PX / DRUM_PX)
    for s in scales:
        rw, rh = kit.lround(W * s), kit.lround(H * s)
        sc.render.resolution_x, sc.render.resolution_y = rw, rh
        cam.data.ortho_scale = max(rw, rh) / s * kit.PX
        tiles = []
        for k in range(FRAMES):
            # the drum turns evenly with the value, so the groove's travel follows a sine, as a real wheel's does
            drum.rotation_euler.x = theta - 2 * theta * k / (FRAMES - 1)
            bpy.ops.render.render(write_still=True)
            img = D.images.load(sc.render.filepath)
            tiles.append(np.array(img.pixels[:], dtype=np.float32).reshape(rh, rw, 4)[::-1].copy())
            D.images.remove(img)
        kit.save(np.concatenate(tiles), "faders/wheel_h", s)
    kit.meta("faders/wheel_h", tiles=FRAMES, surface=False)
    drum.rotation_euler.x = 0
    turn.rotation_euler.z = 0
    cam.data.ortho_scale = ortho


if __name__ == '__main__':
    run()
