# The skin's glyphs as strokes on a 24 x 24 grid, x right and y down: each a list of polylines (closed when the
# last point repeats the first) and filled dots, drawn by sprites.py as rounded tubes so they light and shade
# like everything else. Width is the stroke's in grid units.
import math


def circle(cx, cy, r, n=24, a0=0, a1=360):
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)), cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n)))
            for i in range(n + 1)]


def wave(x0, x1, cy, amp, cycles, n=40):
    return [(x0 + (x1 - x0) * i / n, cy - amp * math.sin(2 * math.pi * cycles * i / n)) for i in range(n + 1)]


def keyboard(held=(), sounds=(), arrow=0):
    """Five white keys, a dot on each key held and the keys that sound filled; arrow 1 or -1 runs over the keys
    the way time does (the Last and First priorities)."""
    sounds = sounds if isinstance(sounds, (list, tuple)) else [sounds]
    k = {'width': 1.2, 'lines': [[(2, 8), (22, 8), (22, 20), (2, 20), (2, 8)]] + [[(2 + 4 * i, 8), (2 + 4 * i, 20)] for i in range(1, 5)],
         'fills': [[(2.9 + 4 * i, 12), (5.1 + 4 * i, 12), (5.1 + 4 * i, 19.1), (2.9 + 4 * i, 19.1)] for i in sounds],
         'dots': [(4 + 4 * i, 16.5, 1.1) for i in held if i not in sounds]}
    if arrow:
        tip, tail = (20, 4) if arrow > 0 else (4, 20)
        k['lines'] += [[(tail, 4), (tip, 4)], [(tip - 2.5 * arrow, 2), (tip, 4), (tip - 2.5 * arrow, 6)]]
    return k


ICONS = {
    # the top bar's strip buttons
    'save': {'width': 1.7, 'lines': [
        [(4, 3), (16, 3), (20, 7), (20, 21), (4, 21), (4, 3)],
        [(8, 3), (8, 9), (15, 9), (15, 3)],
        [(8, 21), (8, 14), (16, 14), (16, 21)]]},
    'folder': {'width': 1.7, 'lines': [
        [(3, 6), (9, 6), (11, 8), (21, 8), (21, 19), (3, 19), (3, 6)],
        [(3, 11), (21, 11)]]},
    'editor': {'width': 1.7, 'lines': [
        [(3, 4), (21, 4), (21, 20), (3, 20), (3, 4)],
        [(3, 8), (21, 8)],
        [(6, 12), (12, 12)], [(6, 16), (16, 16)]]},
    'keys': {'width': 1.6, 'lines': [
        [(3, 4), (21, 4), (21, 20), (3, 20), (3, 4)],
        [(9, 20), (9, 13)], [(15, 20), (15, 13)],
        [(7, 4), (7, 13), (11, 13), (11, 4)], [(13, 4), (13, 13), (17, 13), (17, 4)]]},
    # the bezel's two buttons: MIDI learn (a plug on its lead) and panic
    'learn': {'width': 1.7, 'lines': [
        [(5, 21), (5, 12)] + circle(9, 12, 4, 12, 180, 360)[1:] + [(13, 21)],
        [(16, 3), (16, 9)], [(20, 3), (20, 9)],
        [(14, 9), (22, 9), (22, 12), (18, 15), (14, 12), (14, 9)], [(18, 15), (18, 20)]]},
    'panic': {'width': 2.6, 'lines': [[(12, 4), (12, 14)]], 'dots': [(12, 19.5, 1.7)]},
    'up': {'width': 0, 'fill': [(12, 7), (19, 16), (5, 16)]},
    'down': {'width': 0, 'fill': [(5, 8), (19, 8), (12, 17)]},
    'chevron': {'width': 2.4, 'lines': [[(6, 9), (12, 15), (18, 9)]]},   # a header's menu
    # the expert pages
    'page_all_ops': {'width': 1.5, 'lines': [
        [(3, 3), (10, 3), (10, 9), (3, 9), (3, 3)], [(14, 3), (21, 3), (21, 9), (14, 9), (14, 3)],
        [(8.5, 15), (15.5, 15), (15.5, 21), (8.5, 21), (8.5, 15)],
        [(6.5, 9), (6.5, 12), (12, 12), (12, 15)], [(17.5, 9), (17.5, 12), (12, 12)]]},
    'page_all_envs': {'width': 1.6, 'lines': [
        [(2, 20), (7, 4), (11, 11), (17, 11), (22, 20)],
        [(2, 21.5), (22, 21.5)]]},
    'page_mod': {'width': 1.6, 'lines': [
        wave(2, 22, 9, 5, 1.5),
        [(4, 19), (20, 19)], [(16, 16), (20, 19), (16, 22)]]},
    'page_keysc': {'width': 1.4, 'lines': [
        [(2, 13), (22, 13), (22, 22), (2, 22), (2, 13)],
        [(7, 13), (7, 22)], [(12, 13), (12, 22)], [(17, 13), (17, 22)],
        [(2, 9), (12, 5), (22, 2)]]},
    'page_filter': {'width': 1.6, 'lines': [
        [(2, 9), (11, 9)] + [(11 + 3 * math.sin(math.radians(a)), 9 - 4 * math.sin(math.radians(2 * a)) * (a < 90))
                             for a in range(0, 91, 15)] + [(15, 13), (18, 21)],
        [(2, 21.5), (22, 21.5)]]},
    'page_pitch': {'width': 1.6, 'lines': [
        circle(9, 18, 3.5, 16), [(12.5, 18), (12.5, 3), (18, 6), (18, 9)],
        [(17, 15), (21, 12), (21, 19)]]},
    'lock': {'width': 1.6, 'lines': [
        circle(12, 10, 4, 12, 180, 360) + [(16, 13)], [(8, 13), (8, 10)]],
        'fill': [(5, 13), (19, 13), (19, 22), (5, 22)]},
    'check': {'width': 2.2, 'lines': [[(5, 12.5), (10, 17.5), (19, 6)]]},
    'arrow': {'width': 0, 'fill': [(8, 5), (17, 12), (8, 19)]},
    'note': {'width': 1.8, 'lines': [[(13, 18), (13, 4), (18, 7)]], 'dots': [(10, 18, 3)]},
    'edit': {'width': 0, 'fill': [(6, 4), (16, 4), (16, 20), (6, 20)]},
    # the Parts page's Poly and Mono, and the mono priorities: which held key sounds
    'kbd_poly': keyboard(sounds=[0, 2, 4]),
    'kbd_mono': keyboard(sounds=2),
    'prio_last': keyboard((0, 2, 4), 2, 1),
    'prio_first': keyboard((0, 2, 4), 2, -1),
    'prio_top': keyboard((0, 2, 4), 4),
    'prio_bottom': keyboard((0, 2, 4), 0),
}
