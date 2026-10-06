# Renders the main window's chrome from chrome.blend into the skin, at every scale (kit.SCALES):
#   backgrounds/main      the whole window, every tab raised: main.json's fill
#   backgrounds/keys      the keyboard's strip of it, for the keys view, which moves up under the top bar when
#                         the editor is hidden
#   backgrounds/page      the content well's floor behind the dialogs, as big as the biggest (dialog_audio)
#   buttons/tab_<name>    x4 tiles: raised, sunk into the content well, raised under the pointer, sunk under it
# In Blender (blender/scripts on sys.path): import render_chrome; render_chrome.run()
# or: blender -b blender/chrome.blend -P blender/scripts/render_chrome.py
import os, sys
import bpy
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import kit, layout as L, build_chrome as B


def run(scales=kit.SCALES):
    if 'Cutters' not in bpy.data.collections:
        B.build(save=False)
    kit.studio(samples=48)
    B.sink(None)
    for s in scales:   # the window's own ground: opaque to its edges
        a = kit.render(0, 0, L.W, L.H, s)
        a[..., 3] = 1
        kit.save(a, 'backgrounds/main', s)
        a = kit.render(0, L.KEYWELL[1] - 6, L.W, L.H - L.KEYWELL[1] + 6, s)
        a[..., 3] = 1
        kit.save(a, 'backgrounds/keys', s)
    kit.meta('backgrounds/main', surface=False)
    kit.meta('backgrounds/keys', surface=False)
    kit.tiles('backgrounds/page', (L.CONTENT[0] + 40, L.CONTENT[1] + 8, 560, 521), [None], scales=scales)
    for i, (name, caption, page) in enumerate(L.TABS):
        x, y, w, h = L.tab_rect(i)
        rect = (x - 2, y - 2, w + 4, h + 2 + L.DEPTH + 2)
        kit.tiles('buttons/tab_' + name, rect, [
            lambda: (B.sink(None), B.hover(name, False)),
            lambda: B.sink(name),
            lambda: (B.sink(None), B.hover(name, True)),
            lambda: B.sink(name),
        ], scales=scales)
        B.hover(name, False)
    B.sink(None)


if __name__ == '__main__':
    run()
