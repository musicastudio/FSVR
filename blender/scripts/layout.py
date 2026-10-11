# The main window's layout at 1x, in window pixels (x right, y down): what build_chrome.py cuts into the chrome
# and where plugin/skin/views/main.json puts its embeds and tabs. Move something here, render the chrome again
# and move the widget to match.
W, H = 1514, 952

DEPTH = 6            # every well is cut this deep, with 45 degree walls as wide as it is deep
RADIUS = 10          # the wells' corner radius at the surface

TOPBAR = (0, 0, W, 134)          # the top bar's widgets sit straight on the chrome
OLD_W = 1422                     # the old window's width: the top bar's right-hand group moves by W - OLD_W

# the page tabs across the top of the content well, each a raised plate with square ends until chosen, when it
# is cut into the chrome with the well and opens into it; the first starts at the well's left edge, so sunk, its
# wall runs straight down into the well's (whose corner there is square)
TAB_Y, TAB_H, TAB_W, TAB_SLANT, TAB_GAP, TAB_X = 138, 34, 168, 0, 4, 8
TABS = [   # name, caption, the page it shows
    ('browser', 'Browser', 'page_library'),
    ('parts', 'Parts', 'page_parts'),
    ('operators', 'Operators', 'page_operator'),   # and the six other expert pages, its sub-tabs
    ('performance', 'Performance', 'page_master'),
    ('effects', 'Effects', 'page_fx'),
    ('fseq', 'Fseq', 'page_fseq'),
    ('quick', 'Quick Control', 'page_quick'),
]
CONTENT = (8, TAB_Y + TAB_H, 1235, 566)          # the pages embed, and the well around it
SUBTABS = 30                                      # the Operators tab's sub-tabs over its pages
OPCOL = (1251, TAB_Y, W - 8 - 1251, CONTENT[1] + CONTENT[3] - TAB_Y)   # the operator panel and the expert pages
KEYWELL = (8, CONTENT[1] + CONTENT[3] + 6, W - 16, H - 4 - (CONTENT[1] + CONTENT[3] + 6))
WHEEL_Y, WHEEL_H = KEYWELL[1] + DEPTH + 2, 26     # the pitch and mod wheels, lying across the strip over the keys
KEYS_Y, KEYS_H = WHEEL_Y + WHEEL_H + 3, 161       # the keyboard: 88 keys, A0 to C8


def tab_rect(i):
    return (TAB_X + i * (TAB_W + TAB_GAP), TAB_Y, TAB_W, TAB_H)


assert KEYS_Y + KEYS_H <= KEYWELL[1] + KEYWELL[3], 'the keys run past their well'
