# Changelog

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow SemVer.

## [Unreleased]

## [0.1.0] - 2026-10-19

First release, alpha: source only. Build it with your own Syphon Filter (USA) disc (README).

### Added
- Syphon Filter (USA, `SCUS-94240`) on psxrecomp: boot on the bundled OpenBIOS, intro FMV, title menu, briefings and the first mission, Georgia Street, on recompiled code.
- Widescreen (`sf.enhancement.widescreen`, off by default): 16:9, 21:9 or adaptive, rendered native-wide. The visibility tests and the world renderer's vertex clip limit are widened, and the HUD is anchored to the wide frame's edges by `src/mods/sf_widescreen_plugin.c`. FMVs, briefings and menus keep their 4:3 pillarbox. Notes in `docs/WIDESCREEN.md`.
- In-game menu rows (`src/menu/sf_menu.cpp`): widescreen, HD textures, internal resolution; a Controls page with the keyboard binds and pad buttons, and a Shortcuts page. Esc opens it too, and it uses the title screen's green.
- README, screenshots, LICENSE (MIT), CONTRIBUTING.md.

### Fixed
- Black screen at boot: the FMV task waits for a plain seek status after SeekL. The psxrecomp fork adds `[runtime] cd_seek_sets_read`, off for this game.

### Changed
- `generated/` is no longer committed: it is made from the player's disc at build time. Releases are source only, and the release workflow no longer builds the game.

## [0.0.1]
- Initial scaffolding (New Project Layout).
