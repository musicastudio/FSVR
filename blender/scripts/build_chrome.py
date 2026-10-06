# Builds blender/chrome.blend: the main window's chrome from layout.py. One sheet of the mottled aqua chrome
# with the wells cut into it (the content, the operator column and the keyboard), 45 degree walls, brushed
# floors; the page tabs as raised plates, each with a cutter that sinks it into the content well instead; the
# slots the two wheels lie in.
#   In Blender (blender/scripts on sys.path): import build_chrome; build_chrome.build()
#   or: blender -b -P blender/scripts/build_chrome.py
import os, sys
import bpy
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import kit, layout as L

TAB_PLATE = 4       # how far a raised tab stands off the sheet, px
RIM = 1.5           # the dark rim round a raised tab


def trapezoid(x, y, w, h, slant, r=3, inset=0.0):
    """A tab's outline: its foot (x, y + h) to (x + w, y + h), its top `slant` in from each end; with inset, the
    same shape every edge moved in by that many pixels."""
    if inset:
        cos = h / (h * h + slant * slant) ** 0.5
        dx = slant * inset / h + inset / cos
        x, w, y, h, slant = x + dx, w - 2 * dx, y + inset, h - 2 * inset, slant * (h - 2 * inset) / h
        r = max(r - inset / 2, 0.5)
    return kit.rounded_poly([(x, y + h), (x + slant, y), (x + w - slant, y), (x + w, y + h)], r, 6)


def well(name, rect, material, cutters, radius=L.RADIUS, depth=L.DEPTH):
    x, y, w, h = rect
    top = kit.rounded_rect(x, y, w, h, radius, 8)
    floor = kit.rounded_rect(x + depth, y + depth, w - 2 * depth, h - 2 * depth, max(radius - depth, 0.5), 8)
    return kit.pocket(name, top, floor, depth, material, cutters)


def build(save=True):
    bpy.ops.wm.read_homefile(use_empty=True)
    sc = kit.studio(samples=48)
    sc.name = 'Chrome'
    aqua = kit.aqua()
    kit.aqua('AquaHover', tint=(1.08, 1.08, 1.08)).use_fake_user = True   # a raised tab under the pointer; kept when saved
    floor = kit.brushed('Brushed45', colour=(0.42, 0.50, 0.52), streak=0.14)
    keybed = kit.brushed('Keybed', colour=(0.16, 0.19, 0.20), streak=0.10)
    rim = kit.plain('TabRim', (0.05, 0.07, 0.08), metallic=0.2, roughness=0.5)
    sheet_c = kit.clear('Sheet')
    cut_c = kit.clear('Cutters')
    cut_c.hide_render = True
    cut_c.hide_viewport = True
    tabs_c = kit.clear('Tabs')
    off_c = kit.clear('TabCutters')   # the cutters of tabs that are raised: not cutting
    off_c.hide_render = True
    off_c.hide_viewport = True

    sheet = kit.prism('Sheet', kit.rounded_rect(0, 0, L.W, L.H, 0), -14, 0, aqua, collection=sheet_c)
    kit.bevel(sheet, 1.5, 2)
    well('ContentWell', L.CONTENT, floor, cut_c)
    well('OpWell', L.OPCOL, floor, cut_c)
    well('KeyWell', L.KEYWELL, keybed, cut_c)
    md = sheet.modifiers.new('Wells', 'BOOLEAN')
    md.operation, md.operand_type, md.collection = 'DIFFERENCE', 'COLLECTION', cut_c
    md.solver, md.material_mode = 'EXACT', 'TRANSFER'
    sheet.modifiers.move(len(sheet.modifiers) - 1, 0)   # cut first, then bevel the outer edge

    for i, (name, caption, page) in enumerate(L.TABS):
        x, y, w, h = L.tab_rect(i)
        out = trapezoid(x, y, w, h, L.TAB_SLANT, 4)
        base = kit.prism('TabRim_' + name, out, 0, 1.0, rim, collection=tabs_c)
        plate = kit.prism('Tab_' + name, trapezoid(x, y, w, h, L.TAB_SLANT, 4, RIM), 1.0, 1.0 + TAB_PLATE, aqua,
                          collection=tabs_c, top_outline=trapezoid(x, y, w, h, L.TAB_SLANT, 4, RIM + TAB_PLATE - 1))
        kit.bevel(plate, 0.8, 2)
        # sunk: the same outline down into the content well, deep enough that its floor meets the well's floor
        hd = h + L.DEPTH + 12
        c = kit.prism('TabCut_' + name, trapezoid(x, y, w, hd, L.TAB_SLANT * hd / h, 4, L.DEPTH), -L.DEPTH, 1.0, floor,
                      collection=off_c, top_outline=trapezoid(x, y, w, hd, L.TAB_SLANT * hd / h, 4, -1.0))
        c['tab'] = name
        for o in (base, plate):
            o['tab'] = name

    if save:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(kit.ROOT, 'blender', 'chrome.blend'))
    return sc


def sink(name):
    """Sinks tab `name` into the content well (None: every tab raised)."""
    cut_c, off_c = bpy.data.collections['Cutters'], bpy.data.collections['TabCutters']
    for o in list(cut_c.objects) + list(off_c.objects):
        t = o.get('tab')
        if t is None:
            continue
        want = cut_c if t == name else off_c
        if o.name not in want.objects:
            for c in o.users_collection:
                c.objects.unlink(o)
            want.objects.link(o)
    for o in bpy.data.collections['Tabs'].objects:
        o.hide_render = o.get('tab') == name


def hover(name, on):
    """A raised tab under the pointer: its plate a little lighter."""
    for o in bpy.data.collections['Tabs'].objects:
        if o.get('tab') == name and o.name.startswith('Tab_'):
            o.data.materials[0] = bpy.data.materials['AquaHover' if on else 'Aqua']


if __name__ == '__main__':
    build()
