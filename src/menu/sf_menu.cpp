// sf_menu.cpp -- Syphon Filter's rows in the in-game menu (F10 / Ctrl+L,
// Select + L3 on a controller). recomp-ui owns the menu and its standard rows
// (fullscreen, filters, internal resolution, resume, quit); this file adds the
// title's mod features through recomp_ui_psx_menu_set_title. Values are read
// from the runtime's mod catalogue and written back through PsxMenuHost
// mod_set, so they land in mods/state.toml exactly as the launcher would.
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

const RecompUiPsxMenuTitle kTitle = {
    "Syphon Filter", kItems, sizeof kItems / sizeof kItems[0], nullptr,
    get_value, set_value, is_enabled, visibility_changed,
};

// Registered before main(): the runtime picks the provider when the game
// window opens.
struct Register {
    Register() { recomp_ui_psx_menu_set_title(&kTitle); }
} g_register;

}  // namespace
#endif  // RECOMP_UI_PSX_MENU_HAS_TITLE_ITEMS
#endif  // SF_MENU_AVAILABLE
