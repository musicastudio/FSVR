// check_gui: the FSVR editor end to end without a window. The real skin and the real processor, one Hollow
// Gui over them, driven as a mouse and a keyboard would drive it: widgets found by name, clicks, drags,
// right-clicks and typing, then the params, text data, menus and modals checked. The top bar's menus and
// toggles, the keyboard's lit keys, every Navigator page, the skin's keyboard shortcuts and the tooltips that name them,
// the browser and its right-click menus, every dialog, the modal's veil,
// Escape and close X, the close prompt, the LCD's scale menu and the About box; then a sweep of every page's
// dials, faders, dropdowns and toggles. Exits non-zero on any failure.
//   build/<dir>/Release/check_gui      (ctest runs it as "gui")
#include <hollow/hollow.h>
#include "core/core.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

using namespace hollow;
namespace fs = std::filesystem;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("check_gui: FAIL line %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); ++fails; } } while (0)

struct Ui {   // one instance and its editor, run a block and a GUI tick at a time
    std::shared_ptr<Skin> skin = loadSkin();
    std::unique_ptr<State> st = std::make_unique<State>(skinParams(*skin));
    std::unique_ptr<Processor> proc = createProcessor(*st);
    Gui gui{skin.get(), *st, nullptr};
    std::vector<float> l = std::vector<float>(512), r = std::vector<float>(512);
    Ui() { proc->prepare(48000, 512); }

    void run(int n = 1) {
        float* out[2] = {l.data(), r.data()};
        for (int k = 0; k < n; ++k) {
            proc->process(nullptr, out, 512);
            gui.tick();
        }
        gui.pixels();   // paints what changed, so a paint that breaks shows up here
    }
    template <class F> bool until(F done, int ms = 10000) {
        for (auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms); std::chrono::steady_clock::now() < end;) {
            run();
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }
    void settle() { until([] { return false; }, 200); }   // the worker's ~30 Hz tick reaches what was just done
    // Opens the Save modal from a menu or button and waits for the processor to fill it in: the name and bank
    // name cleared first, so a value left from an earlier save never passes for the new one.
    bool openSave(const std::function<void()>& open) {
        st->setData("save.name", "");
        st->setData("save.bank_name", "");
        open();
        const bool ok = until([&] { return gui.modal() == "dialog_save" && !data("save.name").empty() && !data("save.bank_name").empty(); });
        settle();
        return ok;
    }

    int at(const std::string& id) const { return st->indexOf(id); }
    double get(const std::string& id) const { return at(id) >= 0 ? st->get((size_t)at(id)) : -1e9; }
    void set(const std::string& id, double v) { if (at(id) >= 0) st->set((size_t)at(id), v); }
    std::string data(const std::string& k) const { return st->data(k); }
    int lines(const std::string& k) const { std::string d = st->data(k); return (int)std::count(d.begin(), d.end(), '\n'); }
    bool shows(const std::string& path) { Rect r; return gui.widgetRect(path, r); }
    std::string ui() const { return gui.uiJson(); }

    bool rect(const std::string& path, Rect& r) {
        if (gui.widgetRect(path, r)) return true;
        std::printf("check_gui: FAIL: no visible widget %s\n", path.c_str());
        ++fails;
        return false;
    }
    void clickAt(int x, int y, bool dbl = false) {
        gui.mouseMove(x, y, false);
        gui.mouseDown(x, y, false, dbl, false);
        gui.mouseUp(x, y, false);
        run(2);
    }
    bool click(const std::string& path) {
        Rect r;
        if (!rect(path, r)) return false;
        clickAt(r.x + r.w / 2, r.y + r.h / 2);
        return true;
    }
    void rclickAt(int x, int y) {
        gui.mouseMove(x, y, false);
        gui.mouseDown(x, y, true, false, false);
        gui.rightUp(x, y, false);
        run(2);
    }
    // A list's row n (browser lists: 23 px rows, 2 px apart), counted from the list's top as it is scrolled.
    bool row(const std::string& list, int n, bool right = false) {
        Rect r;
        if (!rect(list, r)) return false;
        const int x = r.x + 60, y = r.y + n * 25 + 11;
        if (right) rclickAt(x, y);
        else clickAt(x, y);
        return true;
    }
    bool lit(const std::string& list, int n) {   // row n drawn in the browser's lit green (its selectFill)
        Rect r;
        if (!gui.widgetRect(list, r)) return false;
        const auto& px = gui.pixels();
        const int x = r.x + r.w - 20, y = r.y + n * 25 + 11;
        return (px[(size_t)y * gui.width() + x] & 0xffffff) == 0x82ca9c;
    }
    void drag(int x, int y, int dx, int dy) {
        gui.mouseMove(x, y, false);
        gui.mouseDown(x, y, false, false, false);
        for (int k = 1; k <= 8; ++k) gui.mouseMove(x + dx * k / 8, y + dy * k / 8, false);
        gui.mouseUp(x + dx, y + dy, false);
        run(2);
    }
    void type(const std::string& text) {   // into the field being edited: all of it replaced, then Enter
        gui.keyDown(KeySelectAll, false, true);
        for (char c : text) gui.keyChar((unsigned char)c);
        gui.keyDown(KeyEnter, false, false);
        run(2);
    }
    // One chord on a letter or a digit, as a platform would deliver it. True when the editor used the key;
    // false means it stays the host's.
    bool chord(char ch, bool shift, bool alt) {
        const bool used = gui.keyDown(KeyNone, shift, false, alt, (unsigned char)ch);
        run(2);
        return used;
    }
    bool altKey(char ch) { return chord(ch, false, true); }
    bool shiftKey(char ch) { return chord(ch, true, false); }
    // One press of an Alt chord delivered twice, as a host that both forwards through its plug-in API and
    // lets the key reach the window sends it. Back to back with nothing in between, so the window the second
    // copy has to fall inside is not a race against however long a paint takes. True when both were ours.
    bool altKeyTwice(char ch) {
        const bool a = gui.keyDown(KeyNone, false, false, true, (unsigned char)ch, false);
        const bool b = gui.keyDown(KeyNone, false, false, true, (unsigned char)ch, true);
        run(2);
        return a && b;
    }
    bool menuIs(const std::vector<std::string>& want, const char* what) {
        const auto got = gui.menuLabels();
        std::string g;
        for (auto& s : got) g += "[" + s + "]";
        CHECK(got == want, "%s: the menu reads %s", what, g.c_str());
        return got == want;
    }
    bool choose(const std::string& label) {
        const bool ok = gui.chooseMenu(label);
        CHECK(ok, "no menu item \"%s\" to choose", label.c_str());
        run(2);
        return ok;
    }
    int lineOf(const std::string& key, const std::string& first) const {   // 1 + the line whose first field is first
        std::string d = st->data(key);
        int n = 1;
        for (size_t p = 0; p < d.size(); ++n) {
            size_t e = d.find('\n', p), t = d.find('\t', p);
            if (d.substr(p, (t < e ? t : e) - p) == first) return n;
            p = e + 1;
        }
        return 0;
    }
};

static const char* const kModal = "hollow_modal/";

int main() {
    const fs::path tmp = fs::temp_directory_path() / ("fsvr_check_gui_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));   // runs side by side
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp / "Library");
#ifdef _WIN32
    _putenv_s("FSVR_LIBRARY", (tmp / "Library").u8string().c_str());
#else
    setenv("FSVR_LIBRARY", (tmp / "Library").u8string().c_str(), 1);
#endif
    {
        Ui u;
        const std::string M = kModal;
        CHECK(u.until([&] { return u.data("perf.name") == "Zap !"; }), "A001 did not load (\"%s\")", u.data("perf.name").c_str());

        // ---- the top bar ------------------------------------------------------------------------------
        if (u.click("topbar/save_menu"))
            u.menuIs({"Save Current Preset...", "-", "Export Current Preset...", "Export to FS1R SysEx..."}, "Save");
        u.gui.keyDown(KeyEscape, false, false);
        CHECK(u.gui.menuLabels().empty(), "Escape left the Save menu open");
        if (u.click("topbar/import_menu")) u.menuIs({"Import FSVR Preset...", "Import FS1R SysEx to New Bank..."}, "Import");
        u.gui.keyDown(KeyEscape, false, false);
        CHECK(!u.shows("topbar/audio_settings"), "MIDI/Aud shows outside the standalone");

        const int tall = u.gui.height();
        u.click("topbar/show_editor");
        CHECK(u.get("gui.hide_editor") == 1 && u.gui.height() < tall, "Editor did not hide the editor (%d px)", u.gui.height());
        u.click("topbar/show_editor");
        CHECK(u.get("gui.hide_editor") == 0 && u.gui.height() == tall, "Editor did not bring it back (%d px)", u.gui.height());
        u.click("topbar/show_keys");
        CHECK(u.get("gui.hide_keys") == 1 && u.gui.height() < tall, "Keys did not hide the keyboard");
        u.click("topbar/show_keys");
        CHECK(u.gui.height() == tall, "Keys did not bring it back");

        // The keyboard lights a key from its note on to its note off and follows no sustain pedal.
        {
            const uint8_t pedalDown[3] = {0xb0, 64, 127}, on[3] = {0x90, 60, 100}, off[3] = {0x80, 60, 0}, pedalUp[3] = {0xb0, 64, 0};
            u.st->midiIn(pedalDown, 3);
            u.st->midiIn(on, 3);
            CHECK(u.st->noteHeld(60), "a note on did not light its key");
            u.st->midiIn(off, 3);
            CHECK(!u.st->noteHeld(60), "a key stayed lit past its note off while the sustain pedal was down");
            u.st->midiIn(pedalUp, 3);
        }

        u.click("topbar/part_3");
        CHECK(u.ui().find("\"part\":\"p3\"") != std::string::npos, "Part 3 did not select part 3");
        u.click("topbar/part_1");
        u.click("topbar/knobs_kn");
        CHECK(u.shows("topbar/kn1") && !u.shows("topbar/attack"), "KN did not show KN1 to KN4");
        u.click("topbar/knobs_tone");
        CHECK(u.shows("topbar/attack") && !u.shows("topbar/kn1"), "Tone did not show the part's knobs");

        if (u.click("topbar/lcd_scale")) u.menuIs({"1x", "2x", "3x", "4x"}, "the LCD's scale");
        u.choose("2x");
        CHECK(u.gui.scale() == 2, "the scale is %dx, not 2x", u.gui.scale());
        u.click("topbar/lcd_scale");
        u.choose("1x");

        // Right-clicking empty space opens nothing: the scale is the LCD's.
        u.rclickAt(3, 3);
        CHECK(u.gui.menuLabels().empty(), "a right-click on empty space opened a menu");

        u.click("topbar/logo");
        CHECK(u.get("gui.about_open") == 1 && u.shows("about_box/close_x"), "the logo did not open About");
        u.click("about_box/close_x");
        CHECK(u.get("gui.about_open") == 0, "About's X did not close it");

        // ---- every Navigator page -------------------------------------------------------------------
        const std::pair<const char*, const char*> pages[] = {
            {"nav_tags", "page_parts"}, {"nav_master", "page_master"}, {"nav_fx", "page_fx"}, {"nav_arp", "page_fseq"},
            {"nav_quick", "page_quick"}, {"nav_expert", "page_all_ops"}, {"nav_all_envs", "page_all_envs"},
            {"nav_mod_matrix", "page_mod_matrix"}, {"nav_key_scaling", "page_key_scaling"}, {"nav_spectrum", "page_filter"},
            {"nav_pitch", "page_pitch"}, {"nav_op_1", "page_operator"}, {"nav_op_8", "page_operator"}, {"nav_library", "page_library"}};
        for (auto& p : pages) {
            u.click(std::string("sidebar/") + p.first);
            CHECK(u.ui().find(std::string("\"view\":\"") + p.second + "\"") != std::string::npos || (std::string(p.second) == "page_library" && u.shows("pages/library")),
                  "%s did not open %s", p.first, p.second);
        }

        // ---- the keyboard shortcuts (skin.json "keys", issue #11) -------------------------------------
        {
            auto opened = [&](const char* view) {
                return u.ui().find(std::string("\"view\":\"") + view + "\"") != std::string::npos ||
                       (std::string(view) == "page_library" && u.shows("pages/library"));
            };
            const std::pair<char, const char*> chords[] = {
                {'E', "page_all_envs"},   {'O', "page_all_ops"}, {'M', "page_mod_matrix"}, {'K', "page_key_scaling"},
                {'F', "page_filter"},     {'P', "page_pitch"},   {'X', "page_fx"},         {'S', "page_fseq"},
                {'Z', "page_quick"},      {'T', "page_parts"},   {'R', "page_master"},     {'B', "page_library"}};
            for (auto& c : chords) {
                CHECK(u.altKey(c.first), "Alt+%c was not used", c.first);
                CHECK(opened(c.second), "Alt+%c did not open %s", c.first, c.second);
            }
            for (char n = '1'; n <= '8'; ++n) {   // the operator pages
                CHECK(u.altKey(n), "Alt+%c was not used", n);
                CHECK(opened("page_operator") && u.ui().find(std::string("\"op\":\"") + n + "\"") != std::string::npos,
                      "Alt+%c did not open operator %c's page", n, n);
            }
            // Alt+N steps the voiced/unvoiced var and wraps back on the second press, N being the unit's own
            // mark for an unvoiced operator.
            CHECK(u.altKey('N') && u.ui().find("\"layer\":\"u\"") != std::string::npos, "Alt+N did not show the unvoiced operators");
            CHECK(u.altKey('N') && u.ui().find("\"layer\":\"v\"") != std::string::npos, "Alt+N twice did not go back to the voiced ones");
            for (char n = '1'; n <= '4'; ++n) {   // Shift+1..4 pick the part, whatever page is up
                CHECK(u.shiftKey(n), "Shift+%c was not used", n);
                CHECK(u.ui().find(std::string("\"part\":\"p") + n + "\"") != std::string::npos, "Shift+%c did not select part %c", n, n);
            }
            u.shiftKey('1');
            // A host that forwards a chord through its plug-in API and also lets it reach the window delivers
            // one press twice (seen with Shift+digit). Alt+N cycles, so acting twice would land back
            // where it started and look like nothing happened: the second copy is ours but must not act.
            CHECK(u.altKeyTwice('N'), "a copy of Alt+N was handed back to the host");
            CHECK(u.ui().find("\"layer\":\"u\"") != std::string::npos, "one press of Alt+N delivered twice cycled twice");
            CHECK(u.altKey('N') && u.ui().find("\"layer\":\"v\"") != std::string::npos,
                  "the press after a doubled one did not act, so the guard swallowed a real press");

            // What the skin does not bind stays the host's, so a DAW keeps its own keys.
            CHECK(!u.altKey('U'), "Alt+U was taken though the voiced/unvoiced chord is Alt+N");
            CHECK(!u.altKey('Q'), "Alt+Q was taken though nothing binds it");
            CHECK(!u.chord('F', false, false), "a bare F was taken for Alt+F");
            CHECK(!u.chord('F', true, false), "Shift+F was taken for Alt+F");
            CHECK(!u.chord('5', true, false), "Shift+5 was taken though only Shift+1 to 4 are bound");

            // Every shortcut names itself in the tooltip of the control that does the same thing, so the chord
            // is never written out twice and never hides. The wording of a tip is the skin's to change, so
            // only the chord it ends with is checked here, and that it kept the words it had.
            u.click("sidebar/nav_quick");
            auto tipEnds = [&](const char* path, const char* chord) {
                const std::string got = u.gui.tipOf(path), want = std::string(" (") + chord + ")";
                CHECK(got.size() > want.size() && got.compare(got.size() - want.size(), want.size(), want) == 0,
                      "%s reads \"%s\", not something ending \"%s\"", path, got.c_str(), want.c_str());
            };
            tipEnds("sidebar/nav_spectrum", "Alt+F");
            tipEnds("sidebar/nav_fx", "Alt+X");
            tipEnds("sidebar/nav_library", "Alt+B");
            tipEnds("sidebar/nav_all_ops", "Alt+O");
            tipEnds("sidebar/nav_all_envs", "Alt+E");
            tipEnds("sidebar/nav_mod_matrix", "Alt+M");
            tipEnds("sidebar/nav_key_scaling", "Alt+K");
            tipEnds("sidebar/nav_pitch", "Alt+P");
            tipEnds("sidebar/nav_arp", "Alt+S");
            tipEnds("sidebar/nav_tags", "Alt+T");
            tipEnds("sidebar/nav_master", "Alt+R");
            tipEnds("sidebar/nav_quick", "Alt+Z");
            tipEnds("sidebar/nav_op_3", "Alt+3");
            tipEnds("topbar/part_2", "Shift+2");
            // Alt+N cycles the var, so both buttons that set one of its values own the chord.
            u.click("sidebar/nav_all_ops");
            tipEnds("pages/voiced", "Alt+N");
            tipEnds("pages/unvoiced", "Alt+N");
        }

        // ---- the operator page's EG plot -------------------------------------
        {
            u.click("sidebar/nav_op_1");
            const char* ids[] = {"eg_hold", "eg_t1", "eg_l1", "eg_t2", "eg_l2", "eg_t3", "eg_l3", "eg_t4", "eg_l4", "eg_time_scale"};
            const double vals[] = {40, 30, 99, 40, 50, 40, 0, 40, 0, 0};
            auto id = [](const char* p) { return std::string("op.1.v.") + p + ".p1"; };
            for (size_t i = 0; i < std::size(ids); ++i) u.set(id(ids[i]), vals[i]);
            for (auto p : {"part.attack.p1", "part.decay.p1", "part.release.p1"}) u.set(p, 0);
            u.set("gui.eg_db", 1);
            u.set("gui.eg_overlay", 0);   // so only this operator sets the time axis
            u.run(3);
            Rect env;
            auto handles = [&]() {   // centres of the red handle frames, left to right
                std::vector<Rect> boxes;
                const auto& px = u.gui.pixels();
                for (int y = env.y; y < env.y + env.h; ++y)
                    for (int x = env.x; x < env.x + env.w; ++x) {
                        if ((px[(size_t)y * u.gui.width() + x] & 0xffffff) != 0xc80800) continue;
                        Rect* in = nullptr;
                        for (auto& b : boxes)
                            if (x >= b.x - 2 && x <= b.x + b.w + 1 && y >= b.y - 2 && y <= b.y + b.h + 1) in = &b;
                        if (!in) boxes.push_back({x, y, 1, 1});
                        else *in = *in | Rect{x, y, 1, 1};
                    }
                std::vector<std::pair<int, int>> c;
                for (auto& b : boxes) c.push_back({b.x + b.w / 2, b.y + b.h / 2});
                std::sort(c.begin(), c.end());
                return c;
            };
            auto picture = [&]() {
                const auto& px = u.gui.pixels();
                uint64_t h = 1469598103934665603ull;
                for (int y = env.y; y < env.y + env.h; ++y)
                    for (int x = env.x; x < env.x + env.w; ++x) h = (h ^ px[(size_t)y * u.gui.width() + x]) * 1099511628211ull;
                return h;
            };
            if (u.rect("pages/envelope", env)) {
                auto hs = handles();
                CHECK(hs.size() == 5, "the amplitude EG plot shows %zu handles, not 5", hs.size());
                if (hs.size() == 5) {
                    const auto p = hs[2];   // level 2
                    // Mid-drag, the handle is under the pointer.
                    const int tx = p.first + 17, ty = p.second + 11;
                    u.gui.mouseMove(p.first, p.second, false);
                    u.gui.mouseDown(p.first, p.second, false, false, false);
                    for (int k = 1; k <= 8; ++k) u.gui.mouseMove(p.first + 17 * k / 8, p.second + 11 * k / 8, false);
                    u.run(1);
                    CHECK((u.gui.pixels()[(size_t)ty * u.gui.width() + tx] & 0xffffff) == 0xc80800, "the held handle is not under the pointer");
                    u.gui.mouseMove(p.first, p.second + 12, false);
                    u.gui.mouseUp(p.first, p.second + 12, false);
                    u.run(2);
                    // Released 12 px lower: the level went down.
                    auto after = handles();
                    CHECK(u.get(id("eg_l2")) < 50 && after.size() == 5, "a point dragged down left L2 at %g", u.get(id("eg_l2")));
                    if (after.size() == 5) {
                        const double l2 = u.get(id("eg_l2")), t2 = u.get(id("eg_t2"));
                        u.drag(after[2].first, after[2].second, 40, 0);
                        CHECK(u.get(id("eg_t2")) > t2 && std::abs(u.get(id("eg_l2")) - l2) <= 1, "a point dragged right moved T2 to %g and L2 to %g", u.get(id("eg_t2")), u.get(id("eg_l2")));
                    }
                    if ((after = handles()).size() == 5) {
                        const auto q = after[2];
                        u.gui.mouseMove(q.first, q.second, false);
                        u.gui.mouseDown(q.first, q.second, false, false, false);
                        int off = 0;
                        for (int k = 1; k <= 10; ++k) {
                            u.gui.mouseMove(q.first, q.second + 3 * k, false);
                            u.run(1);
                            auto now = handles();
                            int best = 1 << 20;
                            for (auto& n : now) best = std::min(best, std::max(std::abs(n.first - q.first), std::abs(n.second - q.second - 3 * k)));
                            off = std::max(off, best);
                        }
                        u.gui.mouseUp(q.first, q.second + 30, false);
                        u.run(2);
                        CHECK(off <= 1, "a point dragged straight down strayed %d px from the pointer", off);
                    }
                    // The wheel zooms, a drag on empty plot pans, a double-click fits again. The wheel can't zoom out past the fit.
                    const int ex = env.x + env.w - 30, ey = env.y + 12;   // empty plot, away from the handles
                    u.clickAt(ex, ey, true);   // start from the fit
                    auto fit = handles();
                    if (fit.size() == 5) {
                        u.gui.wheel(fit[1].first, fit[1].second, 2, false);
                        u.run(2);
                        auto zoom = handles();
                        CHECK(zoom.size() >= 3 && zoom[2].first - zoom[1].first > fit[2].first - fit[1].first + 4, "the wheel did not zoom the plot in");
                        u.drag(ex, ey, -20, 0);
                        auto panned = handles();
                        bool shifted = panned.size() == zoom.size() && !panned.empty() && panned[0].first < zoom[0].first;
                        for (size_t k = 0; shifted && k < panned.size(); ++k) shifted = std::abs((panned[k].first - zoom[k].first) - (panned[0].first - zoom[0].first)) <= 1;
                        CHECK(shifted, "a drag on empty plot did not pan every handle alike");
                        u.clickAt(ex, ey, true);
                        CHECK(handles() == fit, "a double-click did not fit the plot again");
                        u.gui.wheel(ex, ey, -20, false);
                        u.run(2);
                        CHECK(handles() == fit, "the wheel zoomed out past the fit");
                    }
                    // A very long envelope still fits, and switching operators refits.
                    u.set(id("eg_l3"), 90);
                    u.set(id("eg_t4"), 99);   // minutes of release
                    u.run(2);
                    auto slow = handles();
                    CHECK(!slow.empty() && slow.back().first > env.x + env.w * 3 / 4, "a long release ends off the plot");
                    u.gui.wheel(env.x + 40, ey, 4, false);
                    u.run(2);
                    CHECK(handles() != slow, "the wheel did not zoom the long envelope in");
                    u.click("sidebar/nav_op_2");
                    u.click("sidebar/nav_op_1");
                    u.run(2);
                    CHECK(handles() == slow, "coming back to an operator did not show its envelope fitted");
                }
                u.set("gui.eg_overlay", 1);
                u.run(2);
                const uint64_t db = picture();
                u.click("pages/env_db");
                CHECK(u.get("gui.eg_db") == 0 && picture() != db, "the dB toggle did not redraw the plot as linear amplitude");
                u.click("pages/env_db");
                std::vector<double> before;
                for (auto i : ids) before.push_back(u.get(id(i)));
                const uint64_t all = picture();
                u.click("pages/env_all");
                std::vector<double> now;
                for (auto i : ids) now.push_back(u.get(id(i)));
                CHECK(u.get("gui.eg_overlay") == 0 && picture() != all && now == before, "the overlay toggle did not redraw the plot alone");
                u.click("pages/env_all");
            }
            // Overview page: with Sync on, the eight plots share one time axis.
            u.click("sidebar/nav_all_envs");
            Rect one;
            if (u.rect("pages/env_1", one) && u.rect("pages/env_2", env)) {   // picture() is operator 2's plot now
                const int ox = one.x + one.w / 2, oy = one.y + one.h / 2;
                u.set("gui.eg_sync", 1);
                u.set(id("eg_t4"), 40);
                u.run(2);
                uint64_t was = picture();
                u.set(id("eg_t4"), 99);
                u.run(2);
                CHECK(picture() != was, "synced, operator 1's long release did not rescale operator 2's plot");
                was = picture();
                u.gui.wheel(ox, oy, 3, false);
                u.run(2);
                CHECK(picture() != was, "synced, the wheel over operator 1's plot did not zoom operator 2's");
                u.click("pages/envs_sync");
                was = picture();
                u.set(id("eg_t4"), 40);
                u.gui.wheel(ox, oy, 3, false);
                u.run(2);
                CHECK(u.get("gui.eg_sync") == 0 && picture() == was, "not synced, operator 2's plot still followed operator 1's");
                u.click("pages/envs_sync");
            }
        }

        // ---- sliders: the volume knob and Easy's attack fader drag, and a double-click resets ----------
        {
            Rect r;
            const double v0 = u.get("perf.volume");
            if (u.rect("topbar/volume", r)) u.drag(r.x + r.w / 2, r.y + r.h / 2, 0, 40);
            CHECK(u.get("perf.volume") < v0, "dragging Volume down left it at %g", u.get("perf.volume"));
            CHECK(u.until([&] { return u.get("gui.edited") == 1; }), "an edit did not mark the performance edited");
            if (u.rect("topbar/volume", r)) u.clickAt(r.x + r.w / 2, r.y + r.h / 2, true);
            CHECK(u.get("perf.volume") == u.st->def((size_t)u.at("perf.volume")).def, "a double-click did not reset Volume (%g)", u.get("perf.volume"));
            u.click("sidebar/nav_quick");
            if (u.rect("pages/attack", r)) u.drag(r.x + r.w / 2, r.y + r.h / 2, 0, -300);
            CHECK(u.get("part.attack.p1") == 63, "Easy's Attack fader reached %g, not 63", u.get("part.attack.p1"));
        }

        // ---- the close prompt, with the performance edited -------------------------------------------
        CHECK(!u.gui.closeRequested() && u.gui.modal() == "dialog_quit", "closing an edited performance did not ask (modal \"%s\")", u.gui.modal().c_str());
        if (u.gui.modal() == "dialog_quit") u.menuIs({}, "no menu over the modal");
        // The veil: a click on the top bar behind the modal does nothing.
        {
            Rect r;
            const double hide = u.get("gui.hide_editor");
            if (u.rect("topbar/show_editor", r)) u.clickAt(r.x + r.w / 2, r.y + r.h / 2);
            CHECK(u.get("gui.hide_editor") == hide && u.gui.modal() == "dialog_quit", "a click went through the modal's veil");
        }
        u.click(M + "cancel");
        CHECK(u.gui.modal().empty(), "Cancel did not close the close prompt");
        CHECK(!u.gui.closeRequested(), "closing asked only once");
        u.gui.keyDown(KeyEscape, false, false);
        u.run(2);
        CHECK(u.gui.modal().empty(), "Escape did not close the close prompt");
        u.gui.closeRequested();
        u.click(M + "close_x");
        CHECK(u.gui.modal().empty(), "the close X did not close the close prompt");
        u.gui.closeRequested();
        CHECK(u.openSave([&] { u.click(M + "save"); }) && u.data("save.name") == "Zap !", "Save... did not open the Save modal on the name (\"%s\", \"%s\")",
              u.gui.modal().c_str(), u.data("save.name").c_str());   // Save... goes on to Save Current Preset
        u.click(M + "cancel");

        // ---- Save Current Preset into a new bank, typed into the modal -------------------------------
        u.click("sidebar/nav_library");
        CHECK(u.openSave([&] { u.click("topbar/save_menu"); u.choose("Save Current Preset..."); }) && u.data("save.name") == "Zap !",
              "Save Current Preset did not open the Save modal (\"%s\")", u.data("save.name").c_str());
        u.row(M + "banks", 0);
        CHECK(u.get("save.bank") == 0 && u.shows(M + "bank_name"), "<New Bank> did not ask for the new bank's name");
        // The shortcuts are inert while a modal is up, and while a text field has the keyboard: the dialog
        // takes every key, space included, so Alt+F types nothing and moves no page behind it.
        CHECK(u.altKey('F'), "a modal let Alt+F through to the host");
        CHECK(u.ui().find("\"view\":\"page_filter\"") == std::string::npos, "Alt+F changed the page behind a modal");
        u.click(M + "name");
        CHECK(u.altKey('F'), "a text field being edited let Alt+F through to the host");
        CHECK(u.ui().find("\"view\":\"page_filter\"") == std::string::npos, "Alt+F changed the page while a text field was being edited");
        u.type("Zap Edit");
        u.click(M + "bank_name");
        u.type("GUI Bank");
        CHECK(u.data("save.name") == "Zap Edit" && u.data("save.bank_name") == "GUI Bank", "typing gave \"%s\" and \"%s\"", u.data("save.name").c_str(),
              u.data("save.bank_name").c_str());
        u.click(M + "save");
        CHECK(u.gui.modal().empty(), "Save did not close the modal");
        CHECK(u.until([&] { return u.lineOf("bank.list", "GUI Bank") > 0 && u.data("perf.user.name") == "Zap Edit" && u.get("gui.edited") == 0; }),
              "the save made no \"GUI Bank\" (%s)", u.data("fsvr.message").c_str());
        CHECK(u.gui.closeRequested(), "closing asked though the performance is saved");

        // The browser shows the new bank, the saved row lit; clicking off it and back keeps its rows.
        const int bankRow = u.lineOf("bank.list", "GUI Bank");
        CHECK(u.until([&] { return u.get("browse.bank") == bankRow && u.lines("browse.perf.list") == 1 && u.get("browse.perf") == 1; }),
              "the browser is on bank %g, %d rows, row %g lit", u.get("browse.bank"), u.lines("browse.perf.list"), u.get("browse.perf"));
        u.row("pages/library/category_list", 5);   // Guitar: a filter left from elsewhere
        u.row("pages/library/banks", 0);
        u.settle();
        u.row("pages/library/banks", bankRow);
        CHECK(u.until([&] { return u.get("browse.category") == 0 && u.lines("browse.perf.list") == 1; }), "back on the bank: category %g, %d rows",
              u.get("browse.category"), u.lines("browse.perf.list"));

        // ---- a factory preset's right-click menu: read only, its attributes copy ----------------------
        u.row("pages/library/banks", 0);
        u.settle();
        u.row("pages/library/perfs_all", 1, true);   // A002 Shaman
        CHECK(u.lit("pages/library/perfs_all", 1) && !u.lit("pages/library/perfs_all", 0), "the right-clicked row is not the lit one");
        u.menuIs({"Performance: Shaman", "-", "Rename", "Delete", "-", "Edit Attributes", "Copy Attributes", "Paste Attributes"}, "a factory preset's");
        u.choose("Delete");
        CHECK(u.until([&] { return u.gui.modal() == "dialog_delete" && u.get("item.readonly") == 1; }) && !u.shows(M + "delete"),
              "a factory preset's Delete offered to delete it");
        u.click(M + "cancel");
        u.row("pages/library/perfs_all", 1, true);
        u.choose("Edit Attributes");
        CHECK(u.until([&] { return u.gui.modal() == "dialog_attributes" && u.data("item.name") == "Shaman"; }) && !u.shows(M + "ok"),
              "a factory preset's attributes opened writable (\"%s\")", u.data("item.name").c_str());
        u.click(M + "copy");
        CHECK(u.until([&] { return u.data("fsvr.message").rfind("Copied", 0) == 0; }), "Copy Attributes said \"%s\"", u.data("fsvr.message").c_str());
        u.click(M + "cancel");
        CHECK(u.gui.modal().empty(), "Cancel did not close Attributes");
        u.row("pages/library/perfs_all", 2);   // a left-click after all that still loads, and lights its row alone
        CHECK(u.until([&] { return u.data("perf.name") == "Nightmare"; }), "a left-click after the right-click menus loaded \"%s\"", u.data("perf.name").c_str());
        CHECK(u.lit("pages/library/perfs_all", 2) && !u.lit("pages/library/perfs_all", 1), "after the right-click menus the lit row is not the loaded one");

        // ---- a user preset's right-click menu: Rename, Edit Attributes, Paste Attributes, Delete --------
        u.row("pages/library/banks", bankRow);
        u.settle();
        u.row("pages/library/perfs_bank", 0, true);
        u.menuIs({"Performance: Zap Edit", "-", "Rename", "Delete", "-", "Edit Attributes", "Copy Attributes", "Paste Attributes"}, "a user preset's");
        u.choose("Rename");
        CHECK(u.until([&] { return u.gui.modal() == "dialog_rename" && u.data("item.new_name") == "Zap Edit"; }), "Rename opened on \"%s\"", u.data("item.new_name").c_str());
        u.settle();
        u.click(M + "name");
        u.type("Zap Two");
        u.click(M + "rename");
        CHECK(u.until([&] { return u.data("browse.perf.list").find("\tZap Two\t") != std::string::npos; }), "Rename did not rename (%s)", u.data("fsvr.message").c_str());
        u.row("pages/library/perfs_bank", 0, true);
        u.choose("Paste Attributes");
        CHECK(u.until([&] { return u.data("browse.perf.list").find("Zap Two\tVocal") != std::string::npos && u.get("item.category") == 19; }),
              "Paste Attributes did not paste Shaman's Vocal");
        u.settle();
        u.row("pages/library/perfs_bank", 0, true);
        u.choose("Edit Attributes");
        CHECK(u.until([&] { return u.gui.modal() == "dialog_attributes" && u.data("item.name") == "Zap Two"; }) && u.shows(M + "ok"), "a user preset's attributes are not writable");
        u.settle();
        u.click(M + "category");
        u.choose("Bass");
        u.click(M + "ok");
        CHECK(u.until([&] { return u.data("browse.perf.list").find("Zap Two\tBass") != std::string::npos; }), "Edit Attributes did not set Bass");
        u.row("pages/library/perfs_bank", 0);   // loads it (the rename moved nothing, but it is its own row now)
        u.settle();
        u.row("pages/library/perfs_bank", 0, true);
        u.choose("Delete");
        CHECK(u.until([&] { return u.gui.modal() == "dialog_delete" && u.data("item.what") == "performance \"Zap Two\""; }) && u.shows(M + "delete"),
              "Delete asked about \"%s\"", u.data("item.what").c_str());
        u.click(M + "delete");
        CHECK(u.until([&] { return u.lineOf("bank.list", "GUI Bank") == 0; }), "deleting the bank's only preset left the bank");

        // ---- a bank's right-click menu: Rename, Delete -----------------------------------------------
        u.openSave([&] { u.click("topbar/save_menu"); u.choose("Save Current Preset..."); });
        CHECK(u.data("save.bank_name") == "My Presets", "the new bank offered is \"%s\"", u.data("save.bank_name").c_str());
        u.row(M + "banks", 0);
        u.click(M + "save");   // into the offered new bank, "My Presets"
        CHECK(u.until([&] { return u.lineOf("bank.list", "My Presets") > 0; }), "Save did not make \"My Presets\"");
        u.row("pages/library/banks", 0, true);
        u.menuIs({"Bank: Yamaha FS1R", "-", "Rename", "Delete"}, "the factory bank's");
        u.choose("Delete");
        CHECK(u.until([&] { return u.gui.modal() == "dialog_bank_delete" && u.get("bank.readonly") == 1; }) && !u.shows(M + "delete"), "the factory bank offered deleting");
        u.click(M + "cancel");
        u.row("pages/library/banks", u.lineOf("bank.list", "My Presets"), true);
        u.menuIs({"Bank: My Presets", "-", "Rename", "Delete"}, "a user bank's");
        u.choose("Rename");
        CHECK(u.until([&] { return u.gui.modal() == "dialog_bank_rename" && u.data("bank.name") == "My Presets" && u.data("bank.new_name") == "My Presets"; }),
              "Rename Bank has no name (\"%s\")", u.data("bank.name").c_str());
        u.settle();
        u.click(M + "name");
        u.type("Renamed Bank");
        u.click(M + "rename");
        CHECK(u.until([&] { return u.lineOf("bank.list", "Renamed Bank") > 0 && u.lineOf("bank.list", "My Presets") == 0; }), "Rename Bank did not rename");
        u.row("pages/library/banks", u.lineOf("bank.list", "Renamed Bank"), true);
        u.choose("Delete");
        CHECK(u.until([&] { return u.gui.modal() == "dialog_bank_delete" && u.data("bank.name") == "Renamed Bank"; }), "Delete Bank asked about \"%s\"", u.data("bank.name").c_str());
        u.click(M + "delete");
        CHECK(u.until([&] { return u.lineOf("bank.list", "Renamed Bank") == 0; }) && fs::exists(tmp / "Library" / "Deleted" / "Renamed Bank.syx"),
              "Delete Bank did not move the bank to Deleted");

        // ---- every page's controls: each dial and fader drags, each dropdown opens a menu, each toggle flips
        int dials = 0, drops = 0, toggles = 0;
        for (auto& p : pages) {
            u.click(std::string("sidebar/") + p.first);
            for (const auto& w : u.gui.probes()) {
                const Widget& x = *w.w;
                const bool here = w.path.rfind("pages/", 0) == 0 || w.path.rfind("topbar/", 0) == 0;
                if (!here || w.path.find("hollow_modal") != std::string::npos) continue;
                const int cx = w.r.x + w.r.w / 2, cy = w.r.y + w.r.h / 2;
                if (x.kind == Kind::Dial && w.param >= 0 && x.midi < 0) {
                    const double before = u.st->get((size_t)w.param);
                    const int d = x.vertical ? (before >= u.st->def((size_t)w.param).max ? 50 : -50) : 0, h = x.vertical ? 0 : (before >= u.st->def((size_t)w.param).max ? -50 : 50);
                    u.drag(cx, cy, h, d);
                    CHECK(u.st->get((size_t)w.param) != before, "%s: dragging %s left it at %g", p.second, w.path.c_str(), before);
                    ++dials;
                } else if (x.kind == Kind::Dropdown && x.menuStyle != "native") {
                    u.clickAt(cx, cy);
                    CHECK(!u.gui.menuLabels().empty(), "%s: %s opened no menu", p.second, w.path.c_str());
                    u.gui.keyDown(KeyEscape, false, false);
                    ++drops;
                } else if (x.kind == Kind::Button && x.toggle && w.param >= 0 && x.action.type == Action::None && w.path.find("midi_learn") == std::string::npos &&
                           w.path.find("show_") == std::string::npos) {
                    const double before = u.st->get((size_t)w.param);
                    u.clickAt(cx, cy);
                    CHECK(u.st->get((size_t)w.param) != before, "%s: %s did not toggle", p.second, w.path.c_str());
                    u.clickAt(cx, cy);
                    ++toggles;
                }
                CHECK(u.gui.modal().empty(), "%s: %s opened the modal \"%s\"", p.second, w.path.c_str(), u.gui.modal().c_str());
                if (!u.gui.modal().empty()) u.gui.keyDown(KeyEscape, false, false);
            }
        }
        CHECK(dials > 100 && drops > 30 && toggles > 10, "the sweep found only %d dials, %d dropdowns, %d toggles", dials, drops, toggles);
        std::printf("check_gui: swept %d dials and faders, %d dropdowns and %d toggles on %zu pages\n", dials, drops, toggles, sizeof pages / sizeof pages[0]);
        u.proc.reset();
    }
    fs::remove_all(tmp, ec);
    if (!fails) std::printf("check_gui: the top bar, its menus and toggles, every page, the keyboard shortcuts and their tooltips, the browser's right-click menus, every dialog, the modal's veil, Escape and close X, the close prompt, the scale menu and About all pass\n");
    return fails ? 1 : 0;
}
