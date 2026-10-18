#include "mod_plugins.h"

/*
 * Syphon Filter's culling and UI hooks remain framework configuration
 * ([widescreen] in game.toml). These trusted activation callbacks let the
 * game-owned mod package choose how the wide view is presented before
 * renderer startup.
 */
static void sf_widescreen_16_9_activate(void) {
    (void)psx_mod_set_fixed_display_aspect(16u, 9u);
}

static void sf_widescreen_21_9_activate(void) {
    (void)psx_mod_set_fixed_display_aspect(21u, 9u);
}

static void sf_widescreen_adaptive_activate(void) {
    (void)psx_mod_set_fixed_display_aspect(16u, 9u);
    (void)psx_mod_set_adaptive_display_aspect(21u, 9u);
}

PSX_MOD_CONSTRUCTOR(sf_register_widescreen_plugins) {
    (void)psx_mod_register_activation_plugin(
        "sf.widescreen.16-9", sf_widescreen_16_9_activate);
    (void)psx_mod_register_activation_plugin(
        "sf.widescreen.21-9", sf_widescreen_21_9_activate);
    (void)psx_mod_register_activation_plugin(
        "sf.widescreen.adaptive", sf_widescreen_adaptive_activate);
}
