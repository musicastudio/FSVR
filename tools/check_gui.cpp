// check_gui: the FSVR editor end to end without a window. The real skin and the real processor, one Hollow
// Gui over them, driven as a mouse and a keyboard would drive it: widgets found by name, clicks, drags,
// right-clicks and typing, then the params, text data, menus and modals checked. The top bar's menus and
// toggles, the keyboard's lit keys, every page by its tab, the Operators tab's sub-tabs or the operator column's buttons, the operator panel,
// the waveform the engine draws for it, its Form header and its frequency box, the skin's keyboard shortcuts and the tooltips that name them,
// the Parts page's steppers, keyboard, velocity curve and Poly/Mono radios, copy and paste of an operator, an envelope and a part, the
// Fseq speed's steppers across the values it skips, the browser and its right-click menus, every dialog, the modal's veil,
// Escape and close X, the close prompt, the Options page (the scales, the keyboard's two layouts, Modern EG Labels, the Init
// Patch, Turn off all Effects, Init Current Patch, Init All and Quality / Speed) and the About box; then a sweep of every page's
// dials, faders, dropdowns and toggles. Exits non-zero on any failure.
//   build/<dir>/Release/check_gui      (ctest runs it as "gui")
// With FSVR_GUI_SHOTS set to a folder it also saves the window as a PNG at each u.shot("name"), to look at.
#include <hollow/hollow.h>
#include "core/core.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "stb/stb_image_write.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

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
    void shot(const char* name) {   // the window as a PNG in FSVR_GUI_SHOTS, when it is set
        const char* dir = std::getenv("FSVR_GUI_SHOTS");
        if (!dir) return;
        const auto& px = gui.pixels();
        std::vector<uint8_t> rgba;
        for (uint32_t p : px) rgba.insert(rgba.end(), {uint8_t(p >> 16), uint8_t(p >> 8), uint8_t(p), 255});
        stbi_write_png((fs::path(dir) / (std::string(name) + ".png")).string().c_str(), gui.width(), gui.height(), 4, rgba.data(), gui.width() * 4);
    }
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
    bool tabLit(const std::string& tab) {   // a page tab sunk into the well (its grey floor), not raised (the bright aqua)
        Rect r;
        if (!gui.widgetRect(tab, r)) return false;
        const uint32_t p = gui.pixels()[(size_t)(r.y + r.h / 3) * gui.width() + r.x + 12];
        return (77 * (p >> 16 & 255) + 150 * (p >> 8 & 255) + 29 * (p & 255)) >> 8 < 180;
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
    // Into the field being edited: all of it replaced, then Enter. A character is delivered the way a
    // platform delivers it, the key first and then the character it typed, which only arrives because the
    // key left it to come (charPending): a key the editor spends takes its character with it. With a stored
    // key, after each character the text data under it must already hold what was typed so far, as a field
    // that commits "typing" does.
    void typeKeys(const std::string& text, const char* stored = nullptr) {
        gui.keyDown(KeySelectAll, false, true);
        for (size_t k = 0; k < text.size(); ++k) {
            const char c = text[k];
            const unsigned ch = c >= 'a' && c <= 'z' ? (unsigned)(c - 'a' + 'A')   // the key, as a platform reports it
                              : (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ? (unsigned)c : 0;
            gui.keyDown(KeyNone, false, false, false, ch);
            CHECK(gui.charPending(), "typing \"%s\": the key for '%c' took its own character", text.c_str(), c);
            if (gui.charPending()) gui.keyChar((unsigned char)c);
            if (stored) CHECK(data(stored) == text.substr(0, k + 1), "typing \"%s\": %s holds \"%s\" after '%c'", text.c_str(), stored, data(stored).c_str(), c);
        }
    }
    void type(const std::string& text) {
        typeKeys(text);
        gui.keyDown(KeyEnter, false, false);
        run(2);
    }
    // How many of a browser list's rows hold text, counted from the list's top as it is scrolled, the last
    // one cut by the list's bottom left out: the rows past the data are drawn too, in the same fill, but empty.
    int shownRows(const std::string& list) {
        Rect r;
        if (!gui.widgetRect(list, r)) return -1;
        const auto& px = gui.pixels();
        int n = 0;
        for (int k = 0; k * 25 + 23 <= r.h; ++k) {
            bool ink = false;
            for (int y = r.y + k * 25; y < r.y + k * 25 + 23 && !ink; ++y)
                for (int x = r.x; x < r.x + 252 && !ink; ++x) {
                    const uint32_t p = px[(size_t)y * gui.width() + x] & 0xffffff;
                    ink = p != 0x434f50 && p != 0x82ca9c && p != 0x3a4546;
                }
            n += ink;
        }
        return n;
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
        CHECK(u.until([&] { return u.data("live.lcd.perf") == "A001 Zap !"; }), "the LCD reads \"%s\" for A001", u.data("live.lcd.perf").c_str());
        {   // a performance name from the host's sysex reaches the LCD, not just the parts' voices
            const uint8_t x[10] = {0xF0, 0x43, 0x10, 0x5E, 0x10, 0x00, 0x00, 0x00, 'X', 0xF7}, z[10] = {0xF0, 0x43, 0x10, 0x5E, 0x10, 0x00, 0x00, 0x00, 'Z', 0xF7};
            u.proc->midi(0, x, 10);
            CHECK(u.until([&] { return u.data("live.lcd.perf") == "Xap !"; }), "a sysex name left the LCD at \"%s\"", u.data("live.lcd.perf").c_str());
            u.proc->midi(0, z, 10);
            CHECK(u.until([&] { return u.data("live.lcd.perf") == "A001 Zap !"; }), "the LCD did not come back to A001 (\"%s\")", u.data("live.lcd.perf").c_str());
        }
        {   // a restored session publishes the LCD again: State::load clears the live data it was cached against
            u.st->load(u.st->save());
            CHECK(u.until([&] { return u.data("live.lcd.perf") == "A001 Zap !"; }), "a restored session left the LCD at \"%s\"", u.data("live.lcd.perf").c_str());
        }
        {   // a note played on the editor's own keyboard lights the MIDI lamp, as a host's note does
            const unsigned was = u.st->midiInCount();
            const uint8_t on[3] = {0x90, 60, 1}, off[3] = {0x80, 60, 0};
            u.st->pushMidi(on, 3);
            u.st->pushMidi(off, 3);
            CHECK(u.st->midiInCount() == was + 2, "a note from the editor did not count as MIDI in");
        }

        // ---- the top bar ------------------------------------------------------------------------------
        if (u.click("topbar/save_menu"))
            u.menuIs({"Save Current Preset...", "-", "Export Current Preset...", "Export to FS1R SysEx..."}, "Save");
        u.gui.keyDown(KeyEscape, false, false);
        CHECK(u.gui.menuLabels().empty(), "Escape left the Save menu open");
        if (u.click("topbar/import_menu")) u.menuIs({"Import FSVR Preset...", "Import FS1R SysEx to New Bank..."}, "Import");
        u.gui.keyDown(KeyEscape, false, false);
        // With nothing of ours open, Escape is nobody's: the platform forwards exactly what keyDown refuses,
        // and a host closing the plug-in's window on Escape is what that forwarding is for.
        CHECK(!u.gui.keyDown(KeyEscape, false, false), "Escape with nothing open was taken from the host");

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

        // The scales are the skin's, each drawn at its own size rather than the 1x canvas magnified, picked on the
        // Options page, where the window's own is lit.
        const int wide = u.gui.width();
        u.click("tab_options");
        u.click("pages/list/display/scale_200");
        CHECK(u.gui.zoom() == 2 && u.gui.scale() == 1 && u.gui.width() == 2 * wide, "200%% drew %d px wide at %gx (%d repeats), not %d",
              u.gui.width(), u.gui.zoom(), u.gui.scale(), 2 * wide);
        CHECK(u.ui().find("\"scale\":2,") != std::string::npos, "the session does not keep 200%%");
        u.click("pages/list/display/scale_75");
        CHECK(u.gui.zoom() == 0.75 && u.shows("tab_parts"), "75%% did not draw the window");
        u.click("pages/list/display/scale_100");
        CHECK(u.gui.zoom() == 1 && u.gui.width() == wide, "100%% came back %d px wide, not %d", u.gui.width(), wide);
        // The standalone on a 150% display: 100% is drawn at 1.5x and is still 100%, saved and shown.
        u.gui.setDisplayScale(1.5);
        CHECK(u.gui.zoom() == 1.5 && u.gui.picked() == 1 && u.ui().find("\"scale\":1,") != std::string::npos,
              "100%% on a 150%% display drew at %gx and kept %g", u.gui.zoom(), u.gui.picked());
        u.gui.setDisplayScale(1);
        CHECK(u.gui.zoom() == 1 && u.gui.width() == wide, "back on a 100%% display, 100%% drew at %gx", u.gui.zoom());
        {
            // Ctrl with minus or plus steps through the skin's scales, wrapping at either end, and Ctrl with
            // period steps up too. Ctrl+Shift+= is the plus key as it comes when Shift types its +.
            auto ctrlKey = [&](char ch, bool shift) {
                const bool used = u.gui.keyDown(KeyNone, shift, true, false, (unsigned char)ch);
                u.run(2);
                return used;
            };
            for (double s : {1.5, 2.0, 0.5, 0.75, 1.0})
                CHECK(ctrlKey('=', false) && u.gui.picked() == s, "Ctrl+= went to %gx, not %gx", u.gui.picked(), s);
            CHECK(ctrlKey('-', false) && u.gui.picked() == 0.75, "Ctrl+- went from 1x to %gx, not 0.75x", u.gui.picked());
            CHECK(ctrlKey('=', true) && u.gui.picked() == 1, "Ctrl+Shift+= went from 0.75x to %gx, not 1x", u.gui.picked());
            CHECK(ctrlKey('.', false) && u.gui.picked() == 1.5 && u.gui.zoom() == 1.5, "Ctrl+. went from 1x to %gx, not 1.5x",
                  u.gui.picked());
            CHECK(u.ui().find("\"scale\":1.5,") != std::string::npos, "the session does not keep the scale Ctrl+. chose");
            CHECK(!u.chord('-', false, false) && !u.chord('.', false, false), "a bare - or . was taken from the host");
            u.click("pages/list/display/scale_100");
            CHECK(u.gui.picked() == 1 && u.gui.width() == wide, "100%% came back %d px wide after the shortcuts", u.gui.width());
        }
        {
            Rect a, b;   // the radio pair's lit tile is darker than its raised one, at the same point of each
            auto lum = [&](const Rect& r) { const uint32_t p = u.gui.pixels()[(size_t)(r.y + r.h / 2) * u.gui.width() + r.x + 4];
                                             return (77 * (p >> 16 & 255) + 150 * (p >> 8 & 255) + 29 * (p & 255)) >> 8; };
            CHECK(u.rect("pages/list/display/scale_100", a) && u.rect("pages/list/display/scale_50", b) && lum(a) != lum(b), "the window's scale is not lit apart from the others");
        }

        // Right-clicking empty space opens nothing: the scales are the Options page's.
        u.rclickAt(3, 3);
        CHECK(u.gui.menuLabels().empty(), "a right-click on empty space opened a menu");

        u.click("topbar/logo");   // About is a modal: in the window here, or in a window of its own where this one is a bar
        CHECK(u.gui.modal() == "dialog_about" && u.shows(M + "close_x"), "the logo did not open About (modal \"%s\")", u.gui.modal().c_str());
        u.click(M + "close_x");
        CHECK(u.gui.modal().empty(), "About's X did not close it");

        // ---- every page: the tabs, and the operator column's buttons ----------------------------------
        const std::pair<const char*, const char*> pages[] = {
            {"tab_parts", "page_parts"}, {"tab_performance", "page_master"}, {"tab_effects", "page_fx"}, {"tab_fseq", "page_fseq"},
            {"tab_quick", "page_quick"}, {"tab_operators", "page_operator"}, {"pages/subtabs/tab_all_ops", "page_all_ops"},
            {"pages/subtabs/tab_all_envs", "page_all_envs"}, {"pages/subtabs/tab_mod_matrix", "page_mod_matrix"},
            {"pages/subtabs/tab_key_scaling", "page_key_scaling"}, {"pages/subtabs/tab_filter", "page_filter"}, {"pages/subtabs/tab_pitch", "page_pitch"},
            {"pages/subtabs/tab_operator", "page_operator"}, {"opcol/page_all_ops", "page_all_ops"}, {"opcol/page_all_envs", "page_all_envs"},
            {"opcol/page_mod", "page_mod_matrix"}, {"opcol/page_keysc", "page_key_scaling"}, {"opcol/page_filter", "page_filter"},
            {"opcol/page_pitch", "page_pitch"}, {"opcol/edit", "page_operator"}, {"tab_options", "page_options"}, {"tab_browser", "page_library"}};
        for (auto& p : pages) {
            u.click(p.first);
            CHECK(u.ui().find(std::string("\"view\":\"") + p.second + "\"") != std::string::npos || (std::string(p.second) == "page_library" && u.shows("pages/library")),
                  "%s did not open %s", p.first, p.second);
            // The Operators tab stands for all seven of its pages, and sinks into the well for any of them.
            const bool expert = std::string(p.second) == "page_operator" || u.shows("pages/subtabs");
            CHECK(u.tabLit("tab_operators") == expert, "%s: the Operators tab is %s", p.second, expert ? "not lit" : "lit");
        }

        // ---- the Options page --------------------------------------------------------------------------
        {
            const fs::path dir = tmp;   // the library's parent, where the options live
            auto has = [&](const char* text) {
                std::ifstream f(dir / "Settings.json");
                return std::string(std::istreambuf_iterator<char>(f), {}).find(text) != std::string::npos;
            };
            u.click("tab_options");
            CHECK(u.shows("pages/list/display") && u.shows("pages/list/special") && !u.shows("pages/list/audio"), "Audio and MIDI shows outside the standalone");
            const int compact = u.gui.height();
            CHECK(u.shows("keys") && !u.shows("keys_top") && compact == 923, "the compact keyboard is not the default (%d px)", compact);
            u.click("pages/list/display/keys_top");
            CHECK(u.get("gui.keys_top") == 1 && u.gui.height() == 952 && u.shows("keys_top") && !u.shows("keys"),
                  "More Keys did not put the wheels over 88 keys (%d px)", u.gui.height());
            CHECK(u.until([&] { return has("\"keysTop\": 1"); }), "Settings.json did not follow More Keys");
            u.click("pages/list/display/keys_top");
            CHECK(u.gui.height() == compact && u.shows("keys") && !u.shows("keys_top"), "the compact keyboard did not come back (%d px)", u.gui.height());
            // Modern EG Labels: the column names and the Time and Level rows, in place of the T1 and L1 tags
            u.click("opcol/edit");
            CHECK(u.shows("pages/env_eg_t1_label") && !u.shows("pages/env_col_2"), "the EG boxes are not tagged T1 and L1 by default");
            u.click("tab_options");
            u.click("pages/list/display/eg_modern");
            u.click("opcol/edit");
            CHECK(u.shows("pages/env_col_2") && u.shows("pages/env_time_label") && !u.shows("pages/env_eg_t1_label"), "Modern EG Labels did not name the columns");
            u.click("opcol/page_pitch");
            CHECK(u.shows("pages/env_col_1") && !u.shows("pages/peg_l0_label"), "Modern EG Labels did not reach the Pitch page");
            u.click("tab_options");
            u.click("pages/list/display/eg_modern");
            CHECK(u.until([&] { return has("\"egModern\": 0"); }), "Settings.json did not follow Modern EG Labels");
            // Turn off all Effects
            u.set("fx.rev.type", 1), u.set("fx.var.type", 2), u.set("fx.ins.type", 3), u.set("fx.eq.low_gain", 6);
            u.click("pages/list/special/fx_off");
            CHECK(u.until([&] { return u.get("fx.rev.type") == 0 && u.get("fx.var.type") == 0 && u.get("fx.ins.type") == 0 && u.get("fx.eq.low_gain") == 0; }),
                  "Turn off all Effects left reverb %g, variation %g, insertion %g, low EQ %g", u.get("fx.rev.type"), u.get("fx.var.type"),
                  u.get("fx.ins.type"), u.get("fx.eq.low_gain"));
            // The Init Patch: the performance playing, then the factory's again
            CHECK(u.data("opt.init_name") == "A001 Zap ! (factory)", "the Init Patch reads \"%s\"", u.data("opt.init_name").c_str());
            u.set("perf.program", 4);
            CHECK(u.until([&] { return u.data("perf.name") == "Snow Decay"; }), "A005 did not load");
            u.click("pages/list/startup/init_set");
            CHECK(u.until([&] { return fs::exists(dir / "Init.fsvr") && u.data("opt.init_name") == "Snow Decay"; }),
                  "Use Current did not make Snow Decay the Init Patch (\"%s\")", u.data("opt.init_name").c_str());
            u.click("pages/list/startup/init_clear");
            CHECK(u.until([&] { return !fs::exists(dir / "Init.fsvr") && u.data("opt.init_name") == "A001 Zap ! (factory)"; }), "Use Factory left the Init Patch");
            // Init Current Patch asks first, then loads the Init Patch and leaves the system settings alone
            u.set("sys.pgm_mode", 1);
            u.set("perf.program", 2);
            CHECK(u.until([&] { return u.data("perf.name") == "Nightmare"; }), "A003 did not load");
            u.click("pages/list/special/init_patch");
            CHECK(u.gui.modal() == "dialog_init_patch", "Init Current Patch did not ask (modal \"%s\")", u.gui.modal().c_str());
            u.click(M + "init");
            CHECK(u.until([&] { return u.gui.modal().empty() && u.data("perf.name") == "Zap !"; }) && u.get("sys.pgm_mode") == 1,
                  "Init Current Patch left %s, Multi %g", u.data("perf.name").c_str(), u.get("sys.pgm_mode"));
            // Init All asks first, then resets the system settings and loads the Init Patch
            u.set("perf.program", 2);
            CHECK(u.until([&] { return u.data("perf.name") == "Nightmare"; }), "A003 did not load");
            u.click("pages/list/special/init_all");
            CHECK(u.gui.modal() == "dialog_init", "Init All did not ask (modal \"%s\")", u.gui.modal().c_str());
            u.click(M + "init");
            CHECK(u.until([&] { return u.gui.modal().empty() && u.get("sys.pgm_mode") == 0 && u.get("perf.program") == 0 && u.data("perf.name") == "Zap !"; }),
                  "Init All left Multi %g and %s", u.get("sys.pgm_mode"), u.data("perf.name").c_str());
            // Quality / Speed reaches the engine, which still plays at its cheapest
            u.set("opt.speed", 3);
            const uint8_t on[3] = {0x90, 60, 100}, off[3] = {0x80, 60, 0};
            u.proc->midi(0, on, 3);
            float peak = 0;
            for (int k = 0; k < 40; ++k) {
                u.run();
                for (float v : u.l) peak = std::max(peak, std::fabs(v));
            }
            u.proc->midi(0, off, 3);
            CHECK(peak > 1e-4f, "at Fastest the engine made no sound (peak %g)", peak);
            CHECK(u.until([&] { return has("\"speed\": 3"); }), "Settings.json did not follow Quality / Speed");
            u.set("opt.speed", 0);
            // 32 Filtered Voices is on by default and its toggle reaches Settings.json
            CHECK(u.get("opt.filter32") == 1, "32 Filtered Voices was not on by default");
            u.click("pages/list/special/filter32");
            CHECK(u.until([&] { return u.get("opt.filter32") == 0 && has("\"filter32\": 0"); }), "Settings.json did not follow 32 Filtered Voices");
            u.click("pages/list/special/filter32");
            // Polyphony's slider reaches Settings.json; check_plugin plays the notes past 32
            u.set("opt.poly", 3);
            CHECK(u.until([&] { return has("\"poly\": 3"); }), "Settings.json did not follow Polyphony");
            u.set("opt.poly", 0);
            u.shot("options");
        }

        // ---- the operator panel: it picks the operator the operator page shows, and draws the engine's wave
        u.click("opcol/op_6");
        CHECK(u.ui().find("\"op\":\"6\"") != std::string::npos, "the panel's 6 did not pick operator 6");
        u.click("opcol/edit");
        CHECK(u.ui().find("\"view\":\"page_operator\"") != std::string::npos && u.shows("pages/fm_matrix/op_6"),
              "Edit Operator did not open the operator page");
        {   // the selected box's ring is whole: its right side is inside the box, which clips it
            Rect r;
            if (u.rect("pages/fm_matrix/op_6", r)) {
                const uint32_t p = u.gui.pixels()[(size_t)(r.y + r.h / 2) * u.gui.width() + r.x + r.w - 1] & 0xffffff;
                CHECK(p == 0xe8f3ef, "the selected matrix box's ring has no right side (%06x)", p);
            }
        }
        {   // FSVR#72: a double-click on an amount in the matrix sets it to 0, Op1's (default 99) too
            const double alg = u.get("voice.alg.p1"), lvl = u.get("op.1.v.level.p1");
            u.set("voice.alg.p1", 1);
            u.run(2);
            Rect r;
            if (u.rect("pages/fm_matrix/1_to_out", r)) u.clickAt(r.x + r.w / 2, r.y + r.h / 2, true);
            CHECK(u.get("op.1.v.level.p1") == 0, "a double-click left Op1's output amount at %g", u.get("op.1.v.level.p1"));
            u.set("op.1.v.level.p1", lvl);
            u.set("voice.alg.p1", alg);
            u.run(2);
        }
        {   // OP Att reads the unit's dB under Voiced, its value on Level's row; Level's fader stands under Noise
            const std::string db = u.st->text((size_t)u.at("op.6.v.carrier_corr.p1"), 15);
            CHECK(db == "-22.5", "OP Att's last step reads \"%s\", not -22.5 dB", db.c_str());
            Rect att, lvl, fader, vb, nb;
            if (u.rect("pages/amp_v_value", att) && u.rect("pages/level_value", lvl) && u.rect("pages/level", fader) &&
                u.rect("pages/voiced", vb) && u.rect("pages/unvoiced", nb)) {
                auto mid2 = [](const Rect& r) { return 2 * r.x + r.w; };   // twice the centre, so a half pixel counts
                CHECK(att.y == lvl.y && att.h == lvl.h, "OP Att's value and Level's are not on one row");
                CHECK(std::abs(mid2(att) - mid2(vb)) <= 2, "OP Att is not under Voiced");
                CHECK(std::abs(mid2(fader) - mid2(nb)) <= 2, "Level's fader is not under Noise");
            }
        }
        {   // An operator the Fseq drives takes its level from the frame, not from Level, Amp Vel or the level key
            // scaling (FUN_00012A32 voiced, FUN_00012FCC unvoiced), so those controls take no input then; with the
            // operator's switch off, or the Fseq on another part, they still do
            const double fseqPart = u.get("fseq.part"), vsw = u.get("op.6.v.fseq_sw.p1"), usw = u.get("op.6.u.fseq_sw.p1");
            auto moves = [&](const std::string& path, const std::string& id) {   // a drag, up or down, changes the param
                Rect r;
                if (!u.rect(path, r)) return false;
                const double was = u.get(id);
                bool changed = false;
                for (int dy : {30, -30}) {
                    u.drag(r.x + r.w / 2, r.y + r.h / 2, 0, dy);
                    changed = changed || u.get(id) != was;
                    u.set(id, was);
                }
                u.run(2);
                return changed;
            };
            struct Case { const char* what; int sw, part; bool live; };
            const Case cases[] = {{"on, the Fseq on part 1", 1, 1, false}, {"on, the Fseq on part 2", 1, 2, true}, {"off, the Fseq on part 1", 0, 1, true}};
            struct Control { const char* path; const char* param; };
            const Control voiced[] = {{"pages/level", "level"}, {"pages/level_value", "level"}, {"pages/amp_vel", "amp_vel"},
                                      {"pages/amp_vel_value", "amp_vel"}, {"opcol/level", "level"}, {"opcol/level_value", "level"}};
            const Control unvoiced[] = {{"pages/level", "level"}, {"pages/level_value", "level"}, {"pages/amp_vel", "amp_vel"},
                                        {"pages/amp_vel_value", "amp_vel"}, {"pages/amp_u_value", "level_key_scale"}};
            for (const char* layer : {"v", "u"}) {
                if (layer[0] == 'u') u.click("pages/unvoiced");
                for (const Case& c : cases) {
                    u.set(std::string("op.6.") + layer + ".fseq_sw.p1", c.sw);
                    u.set("fseq.part", c.part);
                    u.run(2);
                    const Control* first = layer[0] == 'v' ? voiced : unvoiced;
                    for (size_t k = 0; k < (layer[0] == 'v' ? std::size(voiced) : std::size(unvoiced)); ++k)
                        CHECK(moves(first[k].path, std::string("op.6.") + layer + "." + first[k].param + ".p1") == c.live,
                              "%s of operator 6 %s with its Fseq switch %s", first[k].path, c.live ? "does not move" : "moves", c.what);
                }
                u.set(std::string("op.6.") + layer + ".fseq_sw.p1", layer[0] == 'v' ? vsw : usw);
            }
            u.click("pages/voiced");
            if (u.click("pages/subtabs/tab_all_ops")) {   // the all-operators page's Level dial follows the same rule
                for (const Case& c : cases) {
                    u.set("op.6.v.fseq_sw.p1", c.sw);
                    u.set("fseq.part", c.part);
                    u.run(2);
                    CHECK(moves("pages/level_6", "op.6.v.level.p1") == c.live, "pages/level_6 %s with operator 6's Fseq switch %s", c.live ? "does not move" : "moves", c.what);
                }
                u.click("opcol/edit");
            }
            u.set("op.6.v.fseq_sw.p1", vsw);
            u.set("fseq.part", fseqPart);
            u.run(2);
        }
        CHECK(u.until([&] { return u.data("live.wave.p1.6").size() > 128; }), "the engine drew no wave for operator 6 (\"%s\")",
              u.data("live.wave.p1.6").substr(0, 40).c_str());
        CHECK(u.st->save().find("live.wave") == std::string::npos, "the panel's wave went into the saved session");
        u.click("opcol/op_1");
        {   // Form is the screen's header, a menu; the frequency a box under Ratio and Fixed whose steppers move Coarse
            const double form = u.get("op.1.v.form.p1"), mode = u.get("op.1.v.mode.p1"), coarse = u.get("op.1.v.coarse.p1");
            if (u.click("opcol/form")) u.menuIs({"Sine", "All 1", "All 2", "Odd 1", "Odd 2", "Res 1", "Res 2", "Formant"}, "the Form header's");
            u.choose("Formant");
            CHECK(u.get("op.1.v.form.p1") == 7, "Formant from the header left the form at %g", u.get("op.1.v.form.p1"));
            u.run();
            CHECK(u.shows("opcol/transpose") && u.shows("opcol/detune"), "Formant hid Detune or Transpose");
            CHECK(!u.shows("opcol/mode_ratio") && !u.shows("opcol/mode_fixed") && u.shows("opcol/fixed_mark"),
                  "Formant did not replace Ratio and Fixed with the Fixed mark");
            u.set("op.1.v.transpose.p1", -12);
            CHECK(u.get("op.1.v.transpose.p1") == -12, "Transpose did not take -12");
            CHECK(u.shows("opcol/wave_fixed") && !u.shows("opcol/wave_ratio") && u.shows("opcol/freq_hz_frmt"),
                  "Formant did not show the frequency in Hz");
            u.set("op.1.v.coarse.p1", 1);
            Rect r;
            if (u.rect("opcol/wave_fixed", r)) u.clickAt(r.x + r.w - 3, r.y + r.h / 2);
            CHECK(u.get("op.1.v.coarse.p1") == 2, "the frequency's up stepper left Coarse at %g, from 1", u.get("op.1.v.coarse.p1"));
            if (u.rect("opcol/wave_fixed", r)) u.clickAt(r.x + 3, r.y + r.h / 2);
            CHECK(u.get("op.1.v.coarse.p1") == 1, "the frequency's down stepper left Coarse at %g, from 2", u.get("op.1.v.coarse.p1"));
            u.set("op.1.v.form.p1", 0);
            u.set("op.1.v.mode.p1", 0);
            u.run();
            CHECK(u.shows("opcol/wave_ratio") && !u.shows("opcol/wave_fixed"), "Ratio did not show the frequency ratio");
            u.set("op.1.v.coarse.p1", 1);
            if (u.rect("opcol/wave_ratio", r)) u.clickAt(r.x + r.w - 3, r.y + r.h / 2);
            CHECK(u.get("op.1.v.coarse.p1") == 2, "the frequency's up stepper left Coarse at %g, from 1", u.get("op.1.v.coarse.p1"));
            if (u.rect("opcol/wave_ratio", r)) u.clickAt(r.x + 3, r.y + r.h / 2);
            CHECK(u.get("op.1.v.coarse.p1") == 1, "the frequency's down stepper left Coarse at %g, from 2", u.get("op.1.v.coarse.p1"));
            if (u.rect("opcol/wave_ratio", r)) {   // the ratio is read off Coarse and Fine, so Fine alone repaints it
                auto box = [&] {
                    std::vector<uint32_t> px;
                    for (int y = r.y; y < r.y + r.h; ++y)
                        px.insert(px.end(), u.gui.pixels().begin() + (size_t)y * u.gui.width() + r.x, u.gui.pixels().begin() + (size_t)y * u.gui.width() + r.x + r.w);
                    return px;
                };
                const double fine = u.get("op.1.v.fine.p1");
                u.set("op.1.v.fine.p1", 0);
                u.run();
                const auto before = box();
                u.set("op.1.v.fine.p1", 50);
                u.run();
                CHECK(box() != before, "Fine moved from 0 to 50 and the frequency ratio did not repaint");
                u.set("op.1.v.fine.p1", fine);
            }
            u.click("opcol/mode_fixed");
            CHECK(u.shows("opcol/wave_fixed") && !u.shows("opcol/wave_ratio") && u.shows("opcol/freq_hz_label"), "Fixed did not show the frequency in Hz");
            u.set("op.1.v.form.p1", form), u.set("op.1.v.mode.p1", mode), u.set("op.1.v.coarse.p1", coarse);
            u.run();
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
            u.click("tab_quick");
            auto tipEnds = [&](const char* path, const char* chord) {
                const std::string got = u.gui.tipOf(path), want = std::string(" (") + chord + ")";
                CHECK(got.size() > want.size() && got.compare(got.size() - want.size(), want.size(), want) == 0,
                      "%s reads \"%s\", not something ending \"%s\"", path, got.c_str(), want.c_str());
            };
            tipEnds("opcol/page_filter", "Alt+F");
            tipEnds("tab_effects", "Alt+X");
            tipEnds("tab_browser", "Alt+B");
            tipEnds("opcol/page_all_ops", "Alt+O");
            tipEnds("opcol/page_all_envs", "Alt+E");
            tipEnds("opcol/page_mod", "Alt+M");
            tipEnds("opcol/page_keysc", "Alt+K");
            tipEnds("opcol/page_pitch", "Alt+P");
            tipEnds("tab_fseq", "Alt+S");
            tipEnds("tab_parts", "Alt+T");
            tipEnds("tab_performance", "Alt+R");
            tipEnds("tab_quick", "Alt+Z");
            tipEnds("topbar/part_2", "Shift+2");
            u.click("opcol/page_all_ops");
            tipEnds("pages/subtabs/tab_filter", "Alt+F");   // the Operators tab's sub-tabs name the chords too
            tipEnds("pages/fm_matrix/op_3", "Alt+3");   // the matrix's box opens the operator's page, as Alt+3 does
            // Alt+N cycles the var, so both buttons that set one of its values own the chord.
            tipEnds("pages/voiced", "Alt+N");
            tipEnds("pages/unvoiced", "Alt+N");
        }

        // ---- the operator page's EG plot -------------------------------------
        {
            u.click("opcol/op_1");
            u.click("opcol/edit");
            const char* ids[] = {"eg_hold", "eg_t1", "eg_l1", "eg_t2", "eg_l2", "eg_t3", "eg_l3", "eg_t4", "eg_l4", "eg_time_scale"};
            const double vals[] = {40, 30, 99, 40, 50, 40, 0, 40, 0, 0};
            auto id = [](const char* p) { return std::string("op.1.v.") + p + ".p1"; };
            for (size_t i = 0; i < std::size(ids); ++i) u.set(id(ids[i]), vals[i]);
            for (auto p : {"part.attack.p1", "part.decay.p1", "part.release.p1"}) u.set(p, 0);
            u.set("gui.eg_db", 1);
            u.set("gui.eg_overlay", 0);   // so only this operator sets the time axis
            u.run(3);
            u.shot("eg_operator");
            Rect env;
            const uint32_t dot = 0xeffff8;   // the handles' inside, the page's "handle" colour
            auto handles = [&]() {   // centres of the handle dots, left to right
                std::vector<Rect> boxes;
                const auto& px = u.gui.pixels();
                for (int y = env.y; y < env.y + env.h; ++y)
                    for (int x = env.x; x < env.x + env.w; ++x) {
                        if ((px[(size_t)y * u.gui.width() + x] & 0xffffff) != dot) continue;
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
                    CHECK((u.gui.pixels()[(size_t)ty * u.gui.width() + tx] & 0xffffff) == dot, "the held handle is not under the pointer");
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
                    u.click("opcol/op_2");
                    u.click("opcol/op_1");
                    u.run(2);
                    CHECK(handles() == slow, "coming back to an operator did not show its envelope fitted");
                }
                u.set("gui.eg_overlay", 1);
                u.run(2);
                u.shot("eg_overlay");   // All: the other operators' envelopes, each in its own colour
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
            u.click("opcol/page_all_envs");
            u.run(2);
            u.shot("eg_all");
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
            // The side pane's plot is the same model plot: the wheel zooms, a double-click fits again.
            if (u.rect("opcol/envelope", env)) {
                u.set(id("eg_t4"), 40);
                u.run(2);
                const int ex = env.x + env.w / 3, ey = env.y + env.h / 2;
                u.clickAt(env.x + env.w - 10, env.y + 6, true);
                const uint64_t fit = picture();
                u.gui.wheel(ex, ey, 3, false);
                u.run(2);
                CHECK(picture() != fit, "the wheel did not zoom the side pane's EG plot");
                u.clickAt(env.x + env.w - 10, env.y + 6, true);
                CHECK(picture() == fit, "a double-click did not fit the side pane's EG plot again");
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
            u.click("tab_quick");
            if (u.rect("pages/attack", r)) u.drag(r.x + r.w / 2, r.y + r.h / 2, 0, -300);
            CHECK(u.get("part.attack.p1") == 63, "Easy's Attack fader reached %g, not 63", u.get("part.attack.p1"));
        }

        // ---- the Parts page: the voice by name, the steppers, the keyboard, velocity and Poly/Mono ------
        {
            u.click("tab_parts");
            CHECK(u.until([&] { return !u.data("part.voice.p1").empty() && u.data("part.voice.p1") != "off"; }) && u.data("part.voice.p2") == "off",
                  "the parts' voices read \"%s\" and \"%s\"", u.data("part.voice.p1").c_str(), u.data("part.voice.p2").c_str());
            Rect r;
            const double rsv = u.get("part.note_reserve.p1");
            if (u.rect("pages/reserve_1", r)) u.clickAt(r.x + r.w - 3, r.y + r.h / 2);
            CHECK(u.get("part.note_reserve.p1") == rsv + 1, "the reserve's up stepper left it at %g, from %g", u.get("part.note_reserve.p1"), rsv);
            if (u.rect("pages/reserve_1", r)) u.clickAt(r.x + 3, r.y + r.h / 2);
            CHECK(u.get("part.note_reserve.p1") == rsv, "the reserve's down stepper left it at %g", u.get("part.note_reserve.p1"));
            const double ch = u.get("part.rcv_ch.p1");   // the MIDI channel is a menu: 1 to 16, the performance channel and off
            u.click("pages/rcv_1");
            u.choose("5");
            CHECK(u.get("part.rcv_ch.p1") == 4, "the MIDI channel menu's 5 gave %g", u.get("part.rcv_ch.p1"));
            u.click("pages/rcv_1");
            u.choose("Pfm");
            CHECK(u.get("part.rcv_ch.p1") == 16, "the MIDI channel menu's Pfm gave %g", u.get("part.rcv_ch.p1"));
            u.set("part.rcv_ch.p1", ch);
            u.settle();
            // the keyboard: a key three quarters up moves the range's top down to it; the slider over the keys shifts
            if (u.rect("pages/keys_1", r)) u.clickAt(r.x + r.w * 3 / 4, r.y + r.h - 3);
            CHECK(u.get("part.note_high.p1") < 127 && u.get("part.note_high.p1") > 64, "a key three quarters up made the top %g", u.get("part.note_high.p1"));
            if (u.rect("pages/keys_1", r)) u.clickAt(r.x + (int)std::lround(48.5 * r.w / 128), r.y + 4);   // over C2: middle C an octave down
            CHECK(u.get("part.note_shift.p1") == -12, "a press on the shift's rail over C2 made the shift %g", u.get("part.note_shift.p1"));
            if (u.rect("pages/keys_1", r)) u.clickAt(r.x + 20, r.y + 4);   // on its caption, left of the rail
            CHECK(u.get("part.note_shift.p1") == -12, "a press on the Note Shift caption moved the shift to %g", u.get("part.note_shift.p1"));
            if (u.rect("pages/keys_1", r)) u.drag(r.x + r.w / 2, r.y + 4, 30, 0);
            CHECK(u.get("part.note_shift.p1") > 0, "dragging the shift's triangle right left the shift at %g", u.get("part.note_shift.p1"));
            if (u.rect("pages/keys_1", r)) u.clickAt(r.x + r.w / 2, r.y + 4, true);
            CHECK(u.get("part.note_shift.p1") == 0, "a double-click on the strip left the shift at %g", u.get("part.note_shift.p1"));
            // velocity: the right handle, dragged down, lowers the depth
            const double depth = u.get("part.vel_depth.p1");
            if (u.rect("pages/velocity_1", r)) u.drag(r.x + r.w - 7, r.y + 7, 0, 20);
            CHECK(u.get("part.vel_depth.p1") < depth, "dragging the velocity curve's right end down left the depth at %g", u.get("part.vel_depth.p1"));
            // either end dragged past the other inverts the range: all but the gap between them plays, and is lit
            u.set("part.note_low.p1", 40);
            u.set("part.note_high.p1", 80);
            if (u.rect("pages/keys_1", r)) {
                auto keyX = [&](int n) {   // white key n's middle, along the 75 white keys
                    int i = 0;
                    for (int m = 0; m < n; ++m) i += !(0x54a >> (m % 12) & 1);   // 0x54a: the black keys in an octave
                    return r.x + (int)((i + 0.4) * r.w / 75);
                };
                u.drag(keyX(40), r.y + r.h - 3, keyX(100) - keyX(40), 0);
                auto keyLit = [&](int n) { return (u.gui.pixels()[(size_t)(r.y + r.h - 3) * u.gui.width() + keyX(n)] & 0xffffff) == 0x82ca9c; };
                CHECK(u.get("part.note_low.p1") > 90 && u.get("part.note_high.p1") == 80, "dragging the low key past the high one gave %g to %g",
                      u.get("part.note_low.p1"), u.get("part.note_high.p1"));
                CHECK(keyLit(21) && keyLit(77) && !keyLit(84) && keyLit(120), "an inverted range lit keys 21 %d, 77 %d, 84 (in the gap) %d, 120 %d",
                      keyLit(21), keyLit(77), keyLit(84), keyLit(120));
            }
            u.set("part.vel_high.p1", 64);
            if (u.rect("pages/velocity_1", r)) u.drag(r.x + 7, r.y + r.h - 7, r.w - 14, 0);
            CHECK(u.get("part.vel_low.p1") > 100 && u.get("part.vel_high.p1") == 64, "dragging the velocity's low end past its high one gave %g to %g",
                  u.get("part.vel_low.p1"), u.get("part.vel_high.p1"));
            u.shot("parts_inverted");
            u.set("part.note_low.p1", 0);
            u.set("part.note_high.p1", 127);
            u.set("part.vel_low.p1", 1);
            u.set("part.vel_high.p1", 127);
            // Poly and Mono are radio buttons; the priorities show with Mono only
            u.click("pages/poly_1");
            CHECK(u.get("part.poly.p1") == 1 && !u.shows("pages/prio_top_1"), "Poly left the part at %g, its priorities showing", u.get("part.poly.p1"));
            u.click("pages/mono_1");
            CHECK(u.get("part.poly.p1") == 0 && u.shows("pages/prio_top_1"), "Mono did not make the part mono and show its priorities");
            u.click("pages/prio_top_1");
            CHECK(u.get("part.priority.p1") == 1, "Top set the priority to %g", u.get("part.priority.p1"));
            u.click("pages/poly_1");
            CHECK(u.get("part.poly.p1") == 1 && !u.shows("pages/prio_top_1"), "Poly did not bring the part back");
            // the lamp turns the part off, its voice bank off, and on again with the bank it had
            const double bank = u.get("part.bank.p1");
            u.click("pages/part_1_select");
            CHECK(u.until([&] { return u.get("part.bank.p1") == 0 && u.data("part.voice.p1") == "off"; }) && u.get("part.on.p1") == 0,
                  "the lamp left the part's bank at %g, the lamp at %g", u.get("part.bank.p1"), u.get("part.on.p1"));
            u.click("pages/part_1_select");
            CHECK(u.until([&] { return u.get("part.bank.p1") == bank && u.data("part.voice.p1") != "off"; }) && u.get("part.on.p1") == 1,
                  "the lamp brought the bank back as %g, not %g", u.get("part.bank.p1"), bank);
            // the voice opens the Browser's voices on it (B021, Sound FX): the category the browser had stays while the
            // voice is in it, and gives way to All when it is not
            for (const double cat : {18.0, 3.0}) {
                u.set("browse.category", cat);
                u.settle();
                u.click("pages/voice_1");
                CHECK(u.shows("pages/library") && u.ui().find("\"browse\":\"voice\"") != std::string::npos, "the voice did not open the Browser's voices");
                CHECK(u.until([&] { return u.get("browse.focus") == 0 && u.get("browse.bank") == 0 && u.get("browse.category") == (cat == 18 ? 18 : 0); }),
                      "from category %g the voice opened the browser on bank %g, category %g", cat, u.get("browse.bank"), u.get("browse.category"));
                u.click("tab_parts");
            }
            u.click("tab_browser");
            u.click("pages/library/tab_perf");   // back to the performances, where the browser's checks start
        }

        // ---- copy and paste from the right-click menus: an operator, an envelope from V to N, a part --------
        {
            Rect r;
            auto menu = [&](const std::string& path) { if (u.rect(path, r)) u.rclickAt(r.x + r.w / 2, r.y + r.h / 2); };
            u.click("opcol/layer_v");
            u.set("op.1.v.coarse.p1", 5), u.set("op.1.v.eg_l1.p1", 40), u.set("op.2.v.coarse.p1", 1), u.set("op.2.v.eg_l1.p1", 99);
            u.settle();
            menu("opcol/op_1");
            u.menuIs({"Operator 1", "-", "Copy Operator", "Paste Operator"}, "operator 1's");
            u.choose("Copy Operator");
            u.settle();   // the worker takes one request a tick
            menu("opcol/op_2");
            u.choose("Paste Operator");
            CHECK(u.until([&] { return u.get("op.2.v.coarse.p1") == 5 && u.get("op.2.v.eg_l1.p1") == 40; }),
                  "Paste Operator left operator 2 at Coarse %g, L1 %g", u.get("op.2.v.coarse.p1"), u.get("op.2.v.eg_l1.p1"));
            u.click("opcol/op_1");
            menu("opcol/envelope");
            u.menuIs({"Operator 1 Envelope", "-", "Copy Envelope", "Paste Envelope"}, "the panel's envelope");
            u.choose("Copy Envelope");
            u.settle();
            u.click("opcol/layer_u");
            u.click("opcol/op_3");
            menu("opcol/envelope");
            u.choose("Paste Envelope");
            CHECK(u.until([&] { return u.get("op.3.u.eg_l1.p1") == 40; }), "an envelope pasted from V to N left L1 at %g", u.get("op.3.u.eg_l1.p1"));
            u.click("opcol/layer_v");
            u.click("opcol/op_1");
            const double ch = u.get("part.rcv_ch.p2");   // a part takes the voice and the settings, and keeps its channel
            u.set("part.volume.p1", 77);
            u.settle();
            menu("topbar/part_1");
            u.menuIs({"Part 1", "-", "Copy Part", "Paste Part"}, "part 1's");
            u.choose("Copy Part");
            u.settle();
            menu("topbar/part_2");
            u.choose("Paste Part");
            CHECK(u.until([&] { return u.data("part.voice.p2") == u.data("part.voice.p1") && u.get("part.volume.p2") == 77; }) && u.get("part.rcv_ch.p2") == ch,
                  "Paste Part gave part 2 \"%s\" at volume %g on channel %g, from \"%s\" on %g", u.data("part.voice.p2").c_str(), u.get("part.volume.p2"),
                  u.get("part.rcv_ch.p2"), u.data("part.voice.p1").c_str(), ch);
        }

        // ---- the Fseq speed skips 0.5 to 9.9, where nothing plays: the steppers step across, a value there lands on its nearer end
        {
            u.click("tab_fseq");
            const double speed = u.get("fseq.speed");
            u.set("fseq.speed", 10);
            u.run();
            Rect r;
            if (u.rect("pages/speed", r)) u.clickAt(r.x + 3, r.y + r.h / 2);
            const std::string t = u.st->text((size_t)u.at("fseq.speed"), u.get("fseq.speed"));
            CHECK(u.get("fseq.speed") == 0.4 && t == "MIDI 4/1", "the speed's down stepper from 10.0 gave %g (\"%s\")", u.get("fseq.speed"), t.c_str());
            if (u.rect("pages/speed", r)) u.clickAt(r.x + r.w - 3, r.y + r.h / 2);
            CHECK(u.get("fseq.speed") == 10, "the speed's up stepper from MIDI 4/1 gave %g", u.get("fseq.speed"));
            u.set("fseq.speed", 5);
            CHECK(u.get("fseq.speed") == 0.4, "a speed of 5.0 became %g, not MIDI 4/1", u.get("fseq.speed"));
            u.set("fseq.speed", 6);
            CHECK(u.get("fseq.speed") == 10, "a speed of 6.0 became %g, not 10.0", u.get("fseq.speed"));
            u.set("fseq.speed", speed);
            u.click("tab_browser");
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
        u.click("tab_browser");
        CHECK(u.openSave([&] { u.click("topbar/save_menu"); u.choose("Save Current Preset..."); }) && u.data("save.name") == "Zap !",
              "Save Current Preset did not open the Save modal (\"%s\")", u.data("save.name").c_str());
        u.row(M + "banks", 0);
        CHECK(u.get("save.bank") == 0 && u.shows(M + "bank_name"), "<New Bank> did not ask for the new bank's name");
        // The shortcuts are inert while a modal is up, and while a text field has the keyboard: the dialog
        // takes every key, space included, so Alt+F types nothing and moves no page behind it.
        CHECK(u.altKey('F'), "a modal let Alt+F through to the host");
        CHECK(u.ui().find("\"view\":\"page_filter\"") == std::string::npos, "Alt+F changed the page behind a modal");
        const std::string was = u.data("save.name");
        u.click(M + "name");
        CHECK(u.altKey('F'), "a text field being edited let Alt+F through to the host");
        CHECK(u.ui().find("\"view\":\"page_filter\"") == std::string::npos, "Alt+F changed the page while a text field was being edited");
        u.gui.keyDown(KeyEnter, false, false);   // commits, so what the chord did or did not type shows up
        u.run(2);
        CHECK(u.data("save.name") == was, "Alt+F typed into the field being edited: \"%s\"", u.data("save.name").c_str());
        // Escape abandons the edit, and is the editor's: a host that closes the plug-in's window on Escape
        // must not also see this one. The next Escape, with no edit to cancel, is the one that reaches it.
        u.click(M + "name");
        u.gui.keyDown(KeyNone, false, false, false, 'X');
        u.gui.keyChar('X');
        CHECK(u.gui.keyDown(KeyEscape, false, false), "Escape in a text field being edited was left to the host");
        u.run(2);
        CHECK(u.data("save.name") == was, "Escape did not abandon the edit: \"%s\"", u.data("save.name").c_str());
        u.click(M + "name");
        u.type("Zap Edit");
        u.click(M + "bank_name");
        u.type("GUI Bank");
        CHECK(u.data("save.name") == "Zap Edit" && u.data("save.bank_name") == "GUI Bank", "typing gave \"%s\" and \"%s\"", u.data("save.name").c_str(),
              u.data("save.bank_name").c_str());
        u.click(M + "save");
        CHECK(u.gui.modal().empty(), "Save did not close the modal");
        CHECK(u.until([&] { return u.lineOf("bank.list", "GUI Bank") > 0 && u.data("perf.user.name") == "Zap Edit" && u.get("gui.edited") == 0; }),
              "the save made no \"GUI Bank\" (%s)", u.data("live.message").c_str());
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
        // A selected row keeps the keys that act on a selection and no others: Space and the letters are still
        // the host's transport and computer keyboard, and the chords still work.
        CHECK(u.gui.wantsKeys(), "a selected browser row did not take the keyboard");
        CHECK(!u.gui.keyDown(KeyNone, false, false), "a selected browser row took Space from the host");
        CHECK(!u.chord('A', false, false), "a selected browser row took A from the host's computer keyboard");
        CHECK(u.gui.keyDown(KeyDown, false, false), "a selected browser row left Down to the host");
        CHECK(u.gui.keyDown(KeyEscape, false, false) && !u.gui.wantsKeys(), "Escape did not let go of the selected browser row");
        u.row("pages/library/banks", bankRow);
        CHECK(u.altKey('B') && u.shows("pages/library"), "Alt+B did nothing while a browser row was selected");

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
        CHECK(u.until([&] { return u.data("live.message").rfind("Copied", 0) == 0; }), "Copy Attributes said \"%s\"", u.data("live.message").c_str());
        u.click(M + "cancel");
        CHECK(u.gui.modal().empty(), "Cancel did not close Attributes");
        u.row("pages/library/perfs_all", 2);   // a left-click after all that still loads, and lights its row alone
        CHECK(u.until([&] { return u.data("perf.name") == "Nightmare"; }), "a left-click after the right-click menus loaded \"%s\"", u.data("perf.name").c_str());
        CHECK(u.lit("pages/library/perfs_all", 2) && !u.lit("pages/library/perfs_all", 1), "after the right-click menus the lit row is not the loaded one");

        // ---- the filter box in the browser's header: the rows that hold its text, the clicks and the lit row kept ----
        {
            const std::string L = "pages/library/", list = L + "perfs_all", box = L + "filter", kFilter = "live.browse.filter";
            const Widget* all = nullptr;   // the factory performances' rows, to say what the box should leave
            if (const View* lib = u.skin->view("library_browser"))
                for (auto& w : lib->widgets)
                    if (w.name == "perfs_all") all = &w;
            auto low = [](std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; };
            auto has = [&](const std::vector<std::string>& row, const std::string& needle) {
                for (auto& cell : row)
                    if (low(cell).find(low(needle)) != std::string::npos) return true;
                return false;
            };
            auto expect = [&](const std::string& needle, const std::string& name, int& count, int& place) {   // rows kept, and where the one named stands among them
                count = 0;
                place = -1;
                for (auto& row : all->rows)
                    if (has(row, needle)) {
                        if (row[1] == name) place = count;
                        ++count;
                    }
            };
            auto filter = [&](const std::string& text) {   // click into the box and type, no Enter
                u.click(box);
                u.typeKeys(text, kFilter.c_str());
                u.run(3);
            };
            Rect fr;
            CHECK(all && u.rect(box, fr), "the browser has no filter box");
            if (all && u.rect(box, fr)) {
                int count = 0, place = 0;
                CHECK(u.shownRows(list) == 16, "the browser shows %d rows with nothing typed", u.shownRows(list));
                // Nightmare is the one loaded and lit: a filter that leaves it out lights nothing, and the row left loads what it names
                filter("shaman");
                expect("shaman", "Shaman", count, place);
                CHECK(count == 1 && place == 0 && u.shownRows(list) == 1, "\"shaman\" shows %d rows, %d expected", u.shownRows(list), count);
                CHECK(!u.lit(list, 0), "the row left by the filter is lit though Nightmare is the one loaded");
                u.row(list, 0);
                CHECK(u.until([&] { return u.data("perf.name") == "Shaman"; }), "the only row left loaded \"%s\", not Shaman", u.data("perf.name").c_str());
                CHECK(u.lit(list, 0) && u.shownRows(list) == 1, "Shaman is not lit in the filtered list");
                // in any case, in any column, and the loaded row stays lit wherever the filter puts it
                for (const char* text : {"VOCAL", "b028", "zap", "xyzzy"}) {
                    filter(text);
                    expect(text, "Shaman", count, place);
                    CHECK(u.shownRows(list) == std::min(count, 16), "\"%s\" shows %d rows, %d expected", text, u.shownRows(list), count);
                    if (place >= 0 && place < 16) CHECK(u.lit(list, place), "\"%s\" does not light Shaman, row %d", text, place);
                    else CHECK(!u.lit(list, 0) && !u.lit(list, 1), "\"%s\" lights a row though Shaman is not among them", text);
                    if (std::string(text) == "VOCAL") u.shot("library_filter");
                }
                u.row(list, 0);   // no row where the filter left none
                u.settle();
                CHECK(u.data("perf.name") == "Shaman", "a click where the filter left no row loaded \"%s\"", u.data("perf.name").c_str());
                // Backspace stores as well, and Escape ends the entry with the text kept
                u.click(box);
                u.typeKeys("zapx", kFilter.c_str());
                u.gui.keyDown(KeyBackspace, false, false);
                CHECK(u.data(kFilter) == "zap", "Backspace left \"%s\" in the filter", u.data(kFilter).c_str());
                u.gui.keyDown(KeyEscape, false, false);
                u.run(2);
                CHECK(u.data(kFilter) == "zap" && !u.gui.wantsKeys(), "Escape left \"%s\" in the filter, keys wanted %d", u.data(kFilter).c_str(), u.gui.wantsKeys());
                // the cross at the box's right end empties it and leaves the box open for typing; the lit row is still Shaman's
                u.clickAt(fr.x + fr.w - fr.h / 2, fr.y + fr.h / 2);
                CHECK(u.data(kFilter).empty() && u.shownRows(list) == 16 && u.gui.wantsKeys(), "the cross left \"%s\", %d rows, keys wanted %d", u.data(kFilter).c_str(),
                      u.shownRows(list), u.gui.wantsKeys());
                CHECK(u.lit(list, 1) && !u.lit(list, 2), "after the cross the lit row is not Shaman's");
                u.typeKeys("nightmare", kFilter.c_str());
                u.run(3);
                expect("nightmare", "Nightmare", count, place);
                CHECK(count == 1 && u.shownRows(list) == 1 && !u.lit(list, 0), "\"nightmare\" shows %d rows, %d expected", u.shownRows(list), count);
                // another bank opens with the box empty
                u.row(L + "banks", bankRow);
                CHECK(u.until([&] { return u.get("browse.bank") == bankRow && u.data(kFilter).empty(); }), "the bank opened with \"%s\" in the filter", u.data(kFilter).c_str());

                // a user bank's rows are the processor's text, and a right-click names its row by the place it has without the filter
                u.settle();
                u.st->setData("browse.perf.list", "U1\tAlpha One\tPiano\tA001\t1\nU2\tBravo Two\tBass\tA002\t2\nU3\tCharlie\tBass\tA003\t3\nU4\tDelta Four\tOrgan\tA004\t4\n");
                u.run(3);
                const std::string bank = L + "perfs_bank";
                CHECK(u.shownRows(bank) == 4, "the bank's list shows %d rows, not the 4 given", u.shownRows(bank));
                filter("bass");
                CHECK(u.shownRows(bank) == 2, "\"bass\" shows %d of the bank's rows, not 2", u.shownRows(bank));
                u.row(bank, 1, true);
                CHECK(u.data("item.context").rfind("2\tU3\tCharlie", 0) == 0, "the right-click on the second row left shown named \"%s\"", u.data("item.context").c_str());
                u.menuIs({"Performance: Charlie", "-", "Rename", "Delete", "-", "Edit Attributes", "Copy Attributes", "Paste Attributes"}, "a filtered user preset's");
                u.gui.keyDown(KeyEscape, false, false);
                u.run(2);
                filter("bass");   // leaving the bank for another empties the box again, and coming back lists the processor's own rows
                u.row(L + "banks", 0);
                CHECK(u.until([&] { return u.data(kFilter).empty() && u.get("browse.bank") == 0; }), "the factory bank opened with \"%s\" in the filter", u.data(kFilter).c_str());
                u.row(L + "banks", bankRow);
                CHECK(u.until([&] { return u.get("browse.bank") == bankRow && u.lines("browse.perf.list") == 1; }), "back on the bank: %d rows", u.lines("browse.perf.list"));
            }
        }

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
        CHECK(u.until([&] { return u.data("browse.perf.list").find("\tZap Two\t") != std::string::npos; }), "Rename did not rename (%s)", u.data("live.message").c_str());
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

        // An .fs1, as other editors save a performance (its bulk and its four voices'), dropped on the window
        // imports as a bank of its own and plays; one put in the library folder is a bank too. Several dropped
        // at once share a bank, each performance with its own voices, though a loose voice came between them.
        {
            const fs::path unit = tmp / "unit.syx", fs1 = tmp / "Dropped.fs1", second = tmp / "Second.fs1", loose = tmp / "Loose.syx", wav = tmp / "Dropped.wav";
            u.st->setData("sysex.export", unit.u8string());
            CHECK(u.until([&] { return fs::exists(unit) && fs::file_size(unit) > 0; }), "Export SysEx wrote nothing");
            u.settle();
            std::ifstream in(unit, std::ios::binary);
            const std::vector<uint8_t> dump((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            auto named = [](std::vector<uint8_t> m, const std::string& name) {   // a bulk's name, its checksum redone
                std::copy(name.begin(), name.end(), m.begin() + 9);
                int sum = 0;
                for (size_t k = 4; k + 2 < m.size(); ++k) sum += m[k];
                m[m.size() - 2] = (uint8_t)(-sum & 0x7F);
                return m;
            };
            // The unit's performance and part voices, the performance and part 1's voice renamed so that what
            // loads shows; names padded to their length (12, 10).
            int bulks = 0;
            auto fs1Of = [&](const std::string& perf, const std::string& voice, std::vector<uint8_t>* first = nullptr) {
                std::vector<uint8_t> out;
                bulks = 0;
                for (size_t i = 0; i < dump.size();) {
                    const size_t j = std::find(dump.begin() + (long)i, dump.end(), (uint8_t)0xF7) - dump.begin();
                    if (j >= dump.size()) break;
                    const uint8_t ah = j - i > 9 ? dump[i + 6] : 0;
                    if (dump[i + 3] == 0x5E && (ah == 0x10 || (ah >= 0x40 && ah <= 0x43))) {
                        std::vector<uint8_t> m(dump.begin() + (long)i, dump.begin() + (long)j + 1);
                        if (ah == 0x10) m = named(m, perf);
                        if (ah == 0x40) m = named(m, voice);
                        if (ah == 0x40 && first) *first = m;
                        out.insert(out.end(), m.begin(), m.end());
                        ++bulks;
                    }
                    i = j + 1;
                }
                return out;
            };
            auto put = [](const fs::path& f, const std::vector<uint8_t>& b) { std::ofstream(f, std::ios::binary).write((const char*)b.data(), (std::streamsize)b.size()); };
            std::vector<uint8_t> looseVoice;
            put(fs1, fs1Of("Dropped Perf", "DROPVOX   "));
            CHECK(bulks == 5, "the unit's dump has %d performance and part voice bulks, not 5", bulks);
            put(second, fs1Of("Second Perf ", "SECONDVOX "));
            fs1Of("Unused Perf ", "LOOSEVOX  ", &looseVoice);
            put(loose, looseVoice);   // one voice for part 1, after no performance of its own
            CHECK(u.gui.dropAccepts(fs1.u8string()) && u.gui.dropAccepts((tmp / "X.SYX").u8string()) && !u.gui.dropAccepts(wav.u8string()),
                  "the window takes the wrong files");
            u.st->setData("live.browse.filter", "zz");   // the bank the import opens shows its rows, not the last bank's filter
            CHECK(u.gui.drop({wav.u8string(), fs1.u8string()}), "the window refused the .fs1");
            CHECK(u.until([&] { return u.lineOf("bank.list", "Dropped") > 0 && u.data("perf.name") == "Dropped Perf"; }),
                  "the dropped .fs1 did not import and play (\"%s\")", u.data("perf.name").c_str());
            CHECK(u.data("live.browse.filter").empty(), "the dropped .fs1's bank opened with \"%s\" in the filter", u.data("live.browse.filter").c_str());
            fs::copy_file(fs1, tmp / "Library" / "Copied.fs1", ec);
            CHECK(u.until([&] { return u.lineOf("bank.list", "Copied") > 0; }), "an .fs1 in the library folder is not a bank");

            const std::string shared = tmp.filename().u8string();   // the folder the files came from
            CHECK(u.gui.drop({fs1.u8string(), loose.u8string(), second.u8string()}), "the window refused three files");
            CHECK(u.until([&] { return u.lineOf("bank.list", shared) > 0 && u.get("browse.bank") == u.lineOf("bank.list", shared); }),
                  "the three files made no bank \"%s\" (%s)", shared.c_str(), u.data("live.message").c_str());
            CHECK(u.until([&] { return u.lines("browse.perf.list") == 2 && u.lines("browse.voice.list") == 9; }), "the shared bank has %d performances and %d voices, not 2 and 9",
                  u.lines("browse.perf.list"), u.lines("browse.voice.list"));
            const char* const perfs[2] = {"Dropped Perf", "Second Perf"}, *const voices[2] = {"DROPVOX", "SECONDVOX"};
            for (int k = 0; k < 2; ++k) {
                u.set("browse.perf", 0);
                u.run(2);
                u.set("browse.perf", k + 1);
                CHECK(u.until([&] { return u.data("perf.name") == perfs[k] && u.data("part.voice.p1") == voices[k]; }),
                      "%s plays \"%s\" on part 1, not its own %s (performance \"%s\")", perfs[k], u.data("part.voice.p1").c_str(), voices[k], u.data("perf.name").c_str());
            }
        }

        // ---- every page's controls: each dial and fader drags, each dropdown opens a menu, each toggle flips
        int dials = 0, drops = 0, toggles = 0;
        for (auto& p : pages) {
            u.click(p.first);
            for (const auto& w : u.gui.probes()) {
                const Widget& x = *w.w;
                const bool here = w.path.rfind("pages/", 0) == 0 || w.path.rfind("topbar/", 0) == 0 || w.path.rfind("opcol/", 0) == 0;
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
    if (!fails) std::printf("check_gui: the top bar, its menus and toggles, every page and the Operators tab's sub-tabs, the keyboard shortcuts and their tooltips, the Parts page's steppers, keyboard, velocity curve and radios, copy and paste, the Fseq speed, the browser's right-click menus, every dialog, the modal's veil, Escape and close X, the close prompt, the Options page and About all pass\n");
    return fails ? 1 : 0;
}
