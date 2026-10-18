# Syphon Filter widescreen

Enabled from the launcher: Mods -> Syphon Filter Widescreen (experimental,
off by default) with a 16:9 / 21:9 / Adaptive choice. Rendering is
native-wide: the wider field of view is rendered into a wider target, not
squashed and stretched. FMVs, briefings, the pause map and the title menu keep
their 4:3 pillarbox.

## Culling (`[widescreen.cull]` in game.toml)

Syphon Filter keeps screen X centred (GTE OFX = 0; the GPU draw offset
recentres), so its visibility tests compare `|SX|` against a bound.

| Site | Test | Hook |
|---|---|---|
| 0x800CF0A8, 0x80077BC0, 0x800D18D4, 0x800D699C | object centre `\|SX\| < a1` (a1 = 0x8C / 0x190 / 0x400) | `a1_sites` (load-delay nop) |
| 0x800D4230 | model bounding box `\|SX\| <= a1`, bound computed in place | `a1_sites` (`mflo a1`, fork codegen) |
| 0x800D1FD0, 0x800D272C | near terrain polygon vertex 0 `\|SX\| < 0xC3` | `slti_sites` |
| 0x800D5988 / 0x800D5990 | model polygon guard band +-0xC0 | `bias_lower_sites` / `bias_sites` |

The world mesh renderer (0x800CF0E4) has no per-polygon screen test.

Verification: during the attract demo the cull margin was toggled live
(margin 0 vs the 16:9 margin) frame by frame; no object popped at the edges.
The usual object bound (400 px from centre) already exceeds the 16:9 (213)
and 21:9 (280) half-widths, so the hooks mostly matter for near terrain
polygons and rooms using the 140 px bound.

## HUD (`src/mods/sf_widescreen_plugin.c`)

The plugin anchors HUD packets explicitly (`psx_mod_anchor_hud_primitive`)
instead of the generic `nw_hud_corners` heuristic, which split the ammo
counter and dragged the full-screen fog tint. Producers hooked through
`[recompiler] mod_instruction_sites`:

- 0x800E8918 GsSortSprite packet complete (HUD glyphs and icons),
- 0x800E81A8 shared AddPrim (only the HUD loop's scaled sprites),
- 0x800C8704 HUD sprite loop exit (groups glyphs into strings/icons),
- 0x800C8988 2D primitive list (static HUD records 0x8011B000..0x8011C000).

Anchors are renewed every VBlank while the packet's command words are
unchanged, because the game sorts at 30 fps and anchors expire after two
GPU frames. `SF_HUD_DIAG=<file>` logs every grouping decision.

## Open items for review

- **Conservative choices**: elements are classified by position (left
  column < -60 px from centre -> left edge, right column > +60 -> right edge,
  straddling the centre -> stays). Only verified in the Georgia Street /
  park demo and the first mission. Other missions may draw HUD pieces
  (e.g. boss bars, mission timers, special weapon UIs) that need checking.
- The clock sits at the very left edge in wide mode (same inset it has from
  the 4:3 edge); it may look tight on screens that crop.
- Some in-engine cutscenes show dark areas at the far edges (top-left,
  far right). With the culling A/B clean, these are most likely night sky /
  the end of the authored geometry, not culling; worth a look in daylight
  levels.
- Gameplay past the first mission is untested with widescreen.
- The cull sites and the `mflo a1` codegen support depend on the
  `danilopaulinodasilva/psxrecomp` fork (branch `syphon-filter`).
