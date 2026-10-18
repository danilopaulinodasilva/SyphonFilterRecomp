// sf_menu.cpp -- Syphon Filter's rows in the in-game menu (Esc, F10 / Ctrl+L,
// Select + L3 on a controller). recomp-ui owns the menu and its standard rows
// (fullscreen, filters, internal resolution, resume, quit); this file adds the
// title's mod features through recomp_ui_psx_menu_set_title. Values are read
// from the runtime's mod catalogue and written back through PsxMenuHost
// mod_set, so they land in mods/state.toml exactly as the launcher would.
// It also gives the menu the title screen's look (fullscreen, the logo's
// green) and a Controls page charting the game's actions to the player's
// current keyboard binds and the pad.
#if defined(__has_include)
#if __has_include("recomp_ui_psx_menu.h") && __has_include("psx_menu_host.h") && \
    __has_include("recomp_launcher.h")
#define SF_MENU_AVAILABLE 1
#endif
#endif

#if defined(SF_MENU_AVAILABLE)
#include "psx_menu_host.h"
#include "recomp_launcher.h"
#include "recomp_runtime_ui.h"
#include "recomp_ui_psx_menu.h"

#include "host_keymap.h"
#include "imgui.h"
#include "psx_keybinds.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#if defined(RECOMP_UI_PSX_MENU_HAS_TITLE_ITEMS)
namespace {

constexpr const char *kWsPackage = "sf.enhancement.widescreen";
constexpr const char *kWsFeature = "widescreen";
constexpr const char *kWsAspect = "aspect";
constexpr const char *kHdPackage = "psx.enhancement.hd-textures";
constexpr const char *kHdFeature = "hd-textures";

// Widescreen row: index 0 is off, the rest are the package's aspect values.
const char *const kWsLabels[] = {"Off", "16:9", "21:9", "Adaptive"};
const char *const kWsAspects[] = {nullptr, "16:9", "21:9", "adaptive"};
constexpr int kWsCount = 4;
const char *const kOnOff[] = {"Off", "On"};

const RecompRuntimeUiItem kItems[] = {
    {"sf.widescreen", "Display", "Widescreen",
     "Wider view, HUD at the edges. Restarts the game.",
     RECOMP_RUNTIME_UI_CHOICE, 0, kWsCount - 1, 1, kWsLabels, kWsCount, nullptr},
    {"sf.hd_textures", "Graphics", "HD textures",
     "Texture pack replacements. Restarts the game.",
     RECOMP_RUNTIME_UI_CHOICE, 0, 1, 1, kOnOff, 2, nullptr},
    // List the Controls and Shortcuts sections; draw_controls draws their pages.
    {"sf.controls", "Controls", "Controls", nullptr,
     RECOMP_RUNTIME_UI_ACTION, 0, 0, 0, nullptr, 0, nullptr},
    {"sf.shortcuts", "Shortcuts", "Shortcuts", nullptr,
     RECOMP_RUNTIME_UI_ACTION, 0, 0, 0, nullptr, 0, nullptr},
};

// What the menu shows: read when it opens, then the player's edits.
int g_ws = 0;
int g_hd = 0;
bool g_have_ws = false;
bool g_have_hd = false;

bool is(const RecompRuntimeUiItem *item, const char *key) {
    return item && item->key && !std::strcmp(item->key, key);
}

const RecompLauncherCModProvider *mods(const PsxMenuHost *host) {
    if (!host || !host->launcher_game_info) return nullptr;
    RecompLauncherCGameInfo gi{};
    host->launcher_game_info(&gi);
    return gi.mods;
}

// Reads one feature's enabled flag and, optionally, one option value.
bool read_feature(const RecompLauncherCModProvider *m, const char *package, const char *feature,
                  int *enabled, const char *option, char *value, size_t value_size) {
    if (!m || !m->feature_count || !m->feature_get) return false;
    const int n = m->feature_count(m->ctx);
    for (int i = 0; i < n; ++i) {
        RecompLauncherCModFeature f{};
        if (!m->feature_get(m->ctx, i, &f)) continue;
        if (std::strcmp(f.package_id, package) || std::strcmp(f.id, feature)) continue;
        *enabled = f.enabled;
        if (option && value && m->feature_option_get) {
            value[0] = 0;
            for (int o = 0; o < f.option_count; ++o) {
                RecompLauncherCModOption opt{};
                if (m->feature_option_get(m->ctx, package, feature, o, &opt) && !std::strcmp(opt.id, option)) {
                    std::strncpy(value, opt.value, value_size - 1);
                    value[value_size - 1] = 0;
                    break;
                }
            }
        }
        return true;
    }
    return false;
}

void refresh(const PsxMenuHost *host) {
    const RecompLauncherCModProvider *m = mods(host);
    int on = 0;
    char aspect[RECOMP_LAUNCHER_MOD_VALUE_MAX] = {};
    g_have_ws = read_feature(m, kWsPackage, kWsFeature, &on, kWsAspect, aspect, sizeof aspect);
    g_ws = 0;
    if (g_have_ws && on) {
        g_ws = 1;  // an unknown aspect value shows as the package default
        for (int i = 1; i < kWsCount; ++i)
            if (!std::strcmp(aspect, kWsAspects[i])) g_ws = i;
    }
    on = 0;
    g_have_hd = read_feature(m, kHdPackage, kHdFeature, &on, nullptr, nullptr, 0);
    g_hd = g_have_hd && on ? 1 : 0;
}

int get_value(void *, const PsxMenuHost *, const RecompRuntimeUiItem *item, int *out) {
    if (is(item, "sf.widescreen")) *out = g_ws;
    else if (is(item, "sf.hd_textures")) *out = g_hd;
    else return 0;
    return 1;
}

int set_value(void *, const PsxMenuHost *host, const RecompRuntimeUiItem *item, int v) {
    if (!host || !host->mod_set) return 0;
    if (is(item, "sf.widescreen")) {
        if (v < 0 || v >= kWsCount || !g_have_ws) return 0;
        if (v > 0 && !host->mod_set(kWsPackage, kWsFeature, kWsAspect, kWsAspects[v]) && host->netplay_active &&
            host->netplay_active())
            return 0;
        if (!host->mod_set(kWsPackage, kWsFeature, nullptr, v > 0 ? "true" : "false") && host->netplay_active &&
            host->netplay_active())
            return 0;
        g_ws = v;
    } else if (is(item, "sf.hd_textures")) {
        if (v < 0 || v > 1 || !g_have_hd) return 0;
        if (!host->mod_set(kHdPackage, kHdFeature, nullptr, v ? "true" : "false") && host->netplay_active &&
            host->netplay_active())
            return 0;
        g_hd = v;
    } else {
        return 0;
    }
    // Feature switches only take effect at the next start.
    return RECOMP_UI_PSX_TITLE_APPLIED | RECOMP_UI_PSX_TITLE_RESTART;
}

int is_enabled(void *, const PsxMenuHost *host, const RecompRuntimeUiItem *item) {
    if (host && host->netplay_active && host->netplay_active()) return 0;
    if (is(item, "sf.widescreen")) return g_have_ws;
    if (is(item, "sf.hd_textures")) return g_have_hd;
    return 1;
}

void visibility_changed(void *, const PsxMenuHost *host, int open) {
    if (open) refresh(host);
}

// ---- Controls page ----------------------------------------------------------

#if defined(RECOMP_UI_PSX_MENU_HAS_TITLE_STYLE)
// The title screen: black, the logo's green, and a darker green for frames.
const RecompRuntimeUiPalette kPalette = {
    0x030603,  // background
    0x081108,  // panel
    0x123012,  // panel_hovered
    0x0C1A0C,  // control
    0x173D17,  // control_hovered
    0x1E4A1E,  // border
    0x18CE08,  // accent: the logo
    0x0F7A06,  // accent_dim
    0x4DF040,  // accent2: headings, selected value
    0xDDF5D8,  // text
    0x7FA67A,  // text_muted
};

enum Pad { kNone, kCross, kCircle, kSquare, kTriangle, kText };

// One action of the game's default controller setup (the manual's
// "Configuration A"). Keys come from the player's keybinds at draw time.
struct Action {
    const char *what;
    int buttons[4];     // PsxKeybindButton, -1 ends
    Pad pad;
    const char *pad_text;
};

const Action kGameActions[] = {
    {"Move", {PSX_KB_UP, PSX_KB_DOWN, PSX_KB_LEFT, PSX_KB_RIGHT}, kText, "D-pad, L-stick"},
    {"Fire, throw a grenade", {PSX_KB_SQUARE, -1}, kSquare, nullptr},
    {"Crouch, take cover", {PSX_KB_CROSS, -1}, kCross, nullptr},
    {"Roll", {PSX_KB_CIRCLE, -1}, kCircle, nullptr},
    {"Use, climb, reload, radio", {PSX_KB_TRIANGLE, -1}, kTriangle, nullptr},
    {"Lock on / next target", {PSX_KB_R1, -1}, kText, "R1"},
    {"Manual aim", {PSX_KB_L1, -1}, kText, "L1"},
    {"Strafe / lean left", {PSX_KB_L2, -1}, kText, "L2"},
    {"Strafe / lean right", {PSX_KB_R2, -1}, kText, "R2"},
    {"Weapons: hold, L2 / R2 to pick", {PSX_KB_SELECT, -1}, kText, "Select"},
    {"Pause, map, objectives", {PSX_KB_START, -1}, kText, "Start"},
};

ImU32 rgb(uint32_t v, float a = 1.0f) {
    return IM_COL32((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF, (int)(a * 255.0f));
}

void key_name(SDL_Scancode sc, char *out, size_t n) {
    if (sc == SDL_SCANCODE_UNKNOWN) { std::snprintf(out, n, "-"); return; }
    if (PSXKB_IS_MOUSE_SC(sc)) {
        std::snprintf(out, n, "Mouse %d", (int)sc - PSXKB_MOUSE_SC_BASE);
        return;
    }
    const char *name = SDL_GetScancodeName(sc);
    std::snprintf(out, n, "%s", name && name[0] ? name : "?");
}

// A key drawn as a keycap; returns its width.
float keycap(ImDrawList *dl, ImVec2 at, const char *label, float scale, float row_h) {
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const float pad = 9.0f * scale;
    const float h = std::min(row_h - 4.0f * scale, ts.y + 8.0f * scale);
    const float w = std::max(ts.x + pad * 2.0f, h);
    const ImVec2 min(at.x, at.y + (row_h - h) * 0.5f);
    const ImVec2 max(min.x + w, min.y + h);
    dl->AddRectFilled(min, max, rgb(kPalette.control), 5.0f * scale);
    dl->AddRect(min, max, rgb(kPalette.accent_dim), 5.0f * scale, 0, 1.5f * scale);
    dl->AddText(ImVec2(min.x + (w - ts.x) * 0.5f, min.y + (h - ts.y) * 0.5f), rgb(kPalette.text), label);
    return w;
}

// The PlayStation face-button symbols in their own colours.
float pad_symbol(ImDrawList *dl, ImVec2 at, Pad pad, float scale, float row_h) {
    const float r = std::min(row_h * 0.36f, 13.0f * scale);
    const ImVec2 c(at.x + r + 2.0f * scale, at.y + row_h * 0.5f);
    const float t = 2.2f * scale;
    dl->AddCircleFilled(c, r + 3.0f * scale, rgb(0x101010));
    switch (pad) {
    case kCross: {
        const ImU32 k = rgb(0x7CB2E8);
        const float d = r * 0.55f;
        dl->AddLine(ImVec2(c.x - d, c.y - d), ImVec2(c.x + d, c.y + d), k, t);
        dl->AddLine(ImVec2(c.x - d, c.y + d), ImVec2(c.x + d, c.y - d), k, t);
        break;
    }
    case kCircle: dl->AddCircle(c, r * 0.6f, rgb(0xFF6666), 0, t); break;
    case kSquare: {
        const float d = r * 0.5f;
        dl->AddRect(ImVec2(c.x - d, c.y - d), ImVec2(c.x + d, c.y + d), rgb(0xD88ACB), 0.0f, 0, t);
        break;
    }
    case kTriangle: {
        const float d = r * 0.62f;
        dl->AddTriangle(ImVec2(c.x, c.y - d), ImVec2(c.x + d * 0.95f, c.y + d * 0.6f),
                        ImVec2(c.x - d * 0.95f, c.y + d * 0.6f), rgb(0x3DDB9C), t);
        break;
    }
    default: break;
    }
    return (r + 3.0f * scale) * 2.0f;
}

const char *pad_name(Pad pad) {
    switch (pad) {
    case kCross: return "Cross";
    case kCircle: return "Circle";
    case kSquare: return "Square";
    case kTriangle: return "Triangle";
    default: return "";
    }
}

// One table row: the action, then cells drawn by the callers.
template <typename Keys, typename PadCell>
void row(const char *what, float scale, float row_h, Keys keys, PadCell pad) {
    ImGui::TableNextRow(0, row_h);
    ImGui::TableSetColumnIndex(0);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(ImVec2(p.x, p.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f),
                                        rgb(kPalette.text), what);
    ImGui::TableSetColumnIndex(1);
    keys(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos());
    ImGui::TableSetColumnIndex(2);
    pad(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos());
    (void)scale;
}

bool begin_table(const char *id, float scale, const char *c1, const char *c2, const char *c3) {
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                  ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingStretchProp;
    ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImGui::ColorConvertU32ToFloat4(rgb(kPalette.panel, 0.0f)));
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImGui::ColorConvertU32ToFloat4(rgb(kPalette.control, 0.55f)));
    ImGui::PushStyleColor(ImGuiCol_TableBorderLight, ImGui::ColorConvertU32ToFloat4(rgb(kPalette.border, 0.6f)));
    if (!ImGui::BeginTable(id, 3, flags)) { ImGui::PopStyleColor(3); return false; }
    ImGui::TableSetupColumn(c1, ImGuiTableColumnFlags_WidthStretch, 1.25f);
    ImGui::TableSetupColumn(c2, ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn(c3, ImGuiTableColumnFlags_WidthStretch, 0.75f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(rgb(kPalette.text_muted)));
    ImGui::TableHeadersRow();
    ImGui::PopStyleColor();
    (void)scale;
    return true;
}

void end_table() {
    ImGui::EndTable();
    ImGui::PopStyleColor(3);
}

bool all_arrows(const Action &a) {
    return psx_keybinds_get_button(1, PSX_KB_UP) == SDL_SCANCODE_UP &&
           psx_keybinds_get_button(1, PSX_KB_DOWN) == SDL_SCANCODE_DOWN &&
           psx_keybinds_get_button(1, PSX_KB_LEFT) == SDL_SCANCODE_LEFT &&
           psx_keybinds_get_button(1, PSX_KB_RIGHT) == SDL_SCANCODE_RIGHT && a.buttons[0] == PSX_KB_UP;
}

void game_table(float scale, float row_h, float gap) {
    if (!begin_table("##game", scale, "Action", "Keyboard", "Controller")) return;
    for (const Action &a : kGameActions) {
        row(a.what, scale, row_h,
            [&](ImDrawList *dl, ImVec2 at) {
                if (all_arrows(a)) {  // the default: one cap, not four
                    keycap(dl, at, "Arrow keys", scale, row_h);
                    return;
                }
                for (int i = 0; i < 4 && a.buttons[i] >= 0; ++i) {
                    char k[48], alt[48];
                    key_name(psx_keybinds_get_button(1, a.buttons[i]), k, sizeof k);
                    at.x += keycap(dl, at, k, scale, row_h) + gap;
                    const SDL_Scancode sc2 = psx_keybinds_get_button_alt(1, a.buttons[i]);
                    if (sc2 != SDL_SCANCODE_UNKNOWN) {
                        key_name(sc2, alt, sizeof alt);
                        at.x += keycap(dl, at, alt, scale, row_h) + gap;
                    }
                }
            },
            [&](ImDrawList *dl, ImVec2 at) {
                const char *label = a.pad_text;
                if (a.pad != kText && a.pad != kNone) {
                    at.x += pad_symbol(dl, at, a.pad, scale, row_h) + gap;
                    label = pad_name(a.pad);
                }
                if (label)
                    dl->AddText(ImVec2(at.x, at.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f),
                                rgb(kPalette.text), label);
            });
    }
    end_table();
}

void hotkey_table(float scale, float row_h, float gap) {
    struct Hot { const char *what; const char *keys[2]; const char *pad; };
    char fs[64] = "", perf[64] = "", ff[64] = "", saves[64] = "";
    host_keymap_label(HOST_KEYMAP_FULLSCREEN, fs, sizeof fs);
    host_keymap_label(HOST_KEYMAP_DISPLAY_PERF, perf, sizeof perf);
    host_keymap_label(HOST_KEYMAP_TURBO, ff, sizeof ff);
    host_keymap_label(HOST_KEYMAP_SAVE_STATE_MENU, saves, sizeof saves);
    const Hot hot[] = {
        {"This menu", {"Esc", "F10"}, "Select + L3"},
        {"Save states", {saves, nullptr}, "Select + R1"},
        {"Fast forward (hold)", {ff, nullptr}, ""},
        {"Fullscreen", {fs, nullptr}, ""},
        {"FPS counter", {perf, nullptr}, ""},
    };
    if (!begin_table("##hot", scale, "Shortcut", "Keyboard", "Controller")) return;
    for (const Hot &h : hot) {
        row(h.what, scale, row_h,
            [&](ImDrawList *dl, ImVec2 at) {
                for (const char *k : h.keys)
                    if (k && k[0]) at.x += keycap(dl, at, k, scale, row_h) + gap;
            },
            [&](ImDrawList *dl, ImVec2 at) {
                dl->AddText(ImVec2(at.x, at.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f),
                            rgb(kPalette.text), h.pad);
            });
    }
    end_table();
}

int draw_controls(void *, const PsxMenuHost *, const char *section, float scale) {
    if (!section) return 0;
    const bool controls = !std::strcmp(section, "Controls");
    if (!controls && std::strcmp(section, "Shortcuts")) return 0;
    const float row_h = std::max(ImGui::GetTextLineHeight() * 1.5f, 26.0f * scale);
    const float gap = 6.0f * scale;
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(rgb(kPalette.text_muted)));
    ImGui::TextWrapped(controls
        ? "Your keyboard binds (keybinds.ini, editable in the launcher) and the PlayStation pad."
        : "The recomp's own keys, on top of the game's.");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));
    if (controls) game_table(scale, row_h, gap);
    else hotkey_table(scale, row_h, gap);
    // A small window may not fit the page, and the menu feeds ImGui no keys:
    // scroll with the arrows straight from SDL.
    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    const float step = row_h * 0.5f;
    if (keys && keys[SDL_SCANCODE_DOWN]) ImGui::SetScrollY(ImGui::GetScrollY() + step);
    if (keys && keys[SDL_SCANCODE_UP]) ImGui::SetScrollY(ImGui::GetScrollY() - step);
    return 1;
}
#endif  // RECOMP_UI_PSX_MENU_HAS_TITLE_STYLE

const RecompUiPsxMenuTitle kTitle = {
    "SYPHON FILTER", kItems, sizeof kItems / sizeof kItems[0], nullptr,
    get_value, set_value, is_enabled, visibility_changed,
#if defined(RECOMP_UI_PSX_MENU_HAS_TITLE_STYLE)
    RECOMP_UI_PSX_TITLE_FULLSCREEN | RECOMP_UI_PSX_TITLE_ESCAPE_OPENS, &kPalette,
    "assets/fonts/LatoLatin-Bold.ttf", draw_controls,
#endif
};

// Registered before main(): the runtime picks the provider when the game
// window opens.
struct Register {
    Register() { recomp_ui_psx_menu_set_title(&kTitle); }
} g_register;

}  // namespace
#endif  // RECOMP_UI_PSX_MENU_HAS_TITLE_ITEMS
#endif  // SF_MENU_AVAILABLE
