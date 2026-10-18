#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_state.h"
#include "mod_plugins.h"

/*
 * Syphon Filter widescreen. The culling hooks remain framework configuration
 * ([widescreen] in game.toml). This trusted plugin selects the presented
 * aspect from the package option and anchors the HUD to the wide-frame edges.
 *
 * HUD producers (all inside the frame's 2D pass, 0x800C84F4):
 *  - HUD sprites (text glyphs, icons): GsSortSprite called from the sprite
 *    loop; one GsSPRITE per glyph, so a centred message is many packets.
 *  - Scaled HUD sprites: GsSortSpriteScaled -> shared AddPrim.
 *  - The HUD's static primitive records (radar frame, bars) at
 *    0x8011B000..0x8011C000, copied and linked by the 2D primitive list.
 * Rain, lock-on brackets and other per-frame records live elsewhere and are
 * never touched; neither are full-screen overlays (not produced here).
 */
#define PKG "sf.enhancement.widescreen"
#define FEATURE "widescreen"

#define SF_SORT_SPRITE_DONE 0x800E8918u /* addiu a3,a3,0x18 (a3 = packet) */
#define SF_ADD_PRIM_DONE    0x800E81A8u /* sb zero,3(v1) (s0 = packet) */
#define SF_SPRITE_LOOP_EXIT 0x800C8704u /* lhu v0,6(s2) */
#define SF_PRIM_LIST_DONE   0x800C8988u /* sll v0,s3,2 (s2 = packet, s1 = record) */
#define SF_GS_SPRITE_SCALED_RA 0x800C86E4u /* return from the HUD loop's call */

#define SF_HUD_RECORDS_LO 0x8011B000u
#define SF_HUD_RECORDS_HI 0x8011C000u

/* Classification thresholds in centred screen pixels (display 320 wide). The
 * HUD's left column ends near x = -100 and its right column starts near
 * x = +88; centred strings straddle x = 0. */
#define SF_EDGE_CENTRE 60
#define SF_EDGE_INNER  30

typedef struct { uint32_t packet; int x0, x1, y0, y1; } SfHudBox;

#define SF_MAX_SPRITES 512
static SfHudBox sf_sprites[SF_MAX_SPRITES];
static int sf_sprite_count;
static int sf_enabled;
static FILE* sf_hud_diag;

static int s16(uint32_t v) { return (int)(int16_t)(v & 0xFFFFu); }

static int sf_edge_for(int x0, int x1) {
    int cx2 = x0 + x1;
    if (cx2 < -2 * SF_EDGE_CENTRE && x1 < -SF_EDGE_INNER) return -1;
    if (cx2 > 2 * SF_EDGE_CENTRE && x0 > SF_EDGE_INNER) return 1;
    return 0;
}

/* Index of the GP0 draw command in a linked packet (after the tag and any
 * E1..E6 environment words), or -1. */
static int sf_command_index(uint32_t packet, int words) {
    for (int i = 1; i <= words && i < 4; i++) {
        uint32_t op = psx_mod_read_word(packet + 4u * (uint32_t)i) >> 24;
        if (op >= 0xE1 && op <= 0xE6) continue;
        return (op >= 0x20 && op <= 0x7F) ? i : -1;
    }
    return -1;
}

/* Horizontal extent of a polygon, line or rectangle packet. */
static int sf_packet_extent(uint32_t packet, int cmd_index, int* x0, int* x1) {
    uint32_t base = packet + 4u * (uint32_t)cmd_index;
    uint32_t cmd = psx_mod_read_word(base);
    uint32_t op = cmd >> 24;
    int lo = 0x7FFF, hi = -0x8000, n = 0;
    if (op < 0x40) {                       /* polygon */
        int verts = (op & 0x08) ? 4 : 3;
        int shaded = (op & 0x10) != 0, textured = (op & 0x04) != 0;
        uint32_t w = 1;
        for (int v = 0; v < verts; v++) {
            if (v > 0 && shaded) w++;          /* per-vertex colour */
            int x = s16(psx_mod_read_word(base + 4u * w++));
            if (textured) w++;                 /* uv / clut / tpage */
            if (x < lo) lo = x;
            if (x > hi) hi = x;
            n++;
        }
    } else if (op < 0x60) {                /* line (first segment) */
        int shaded = (op & 0x10) != 0;
        int xa = s16(psx_mod_read_word(base + 4u));
        int xb = s16(psx_mod_read_word(base + (shaded ? 12u : 8u)));
        lo = xa < xb ? xa : xb; hi = xa < xb ? xb : xa; n = 2;
    } else {                               /* rectangle */
        int x = s16(psx_mod_read_word(base + 4u));
        int size = (op >> 3) & 3, w;
        if (size == 0) w = s16(psx_mod_read_word(base + ((op & 0x04) ? 12u : 8u)));
        else w = size == 1 ? 1 : size == 2 ? 8 : 16;
        lo = x; hi = x + w; n = 1;
    }
    if (!n) return 0;
    *x0 = lo; *x1 = hi;
    return 1;
}

/* Anchor tags stay fresh for only two GPU frames, but the game sorts its HUD
 * at 30 fps and the ordering table can be drawn later than that. Keep the
 * last passes' anchors with a copy of each packet's command words and renew
 * them every VBlank while the packet is unchanged. */
#define SF_ANCHOR_WORDS 6
#define SF_MAX_ANCHORS 2048
typedef struct {
    uint32_t addr;                     /* word before the GP0 colour command */
    uint32_t words[SF_ANCHOR_WORDS];   /* command words at tag time */
    int count;                         /* command words in this packet */
    int edge;
} SfAnchor;
static SfAnchor sf_anchors[SF_MAX_ANCHORS];
static int sf_anchor_next;

static int sf_anchor_unchanged(const SfAnchor* a) {
    for (int i = 0; i < a->count; i++)
        if (psx_mod_read_word(a->addr + 4u * (uint32_t)(i + 1)) != a->words[i]) return 0;
    return 1;
}

static void sf_anchor(uint32_t packet, int cmd_index, int edge) {
    SfAnchor* a = &sf_anchors[sf_anchor_next];
    sf_anchor_next = (sf_anchor_next + 1) % SF_MAX_ANCHORS;
    a->addr = packet + 4u * (uint32_t)(cmd_index - 1);
    /* Compare only this packet's own command words: the tag's length byte
     * counts the words after the tag, and what follows the last packet of a
     * pass is overwritten later in the frame. */
    int count = (int)(psx_mod_read_word(packet) >> 24) - (cmd_index - 1);
    a->count = count < 1 ? 1 : count > SF_ANCHOR_WORDS ? SF_ANCHOR_WORDS : count;
    for (int i = 0; i < a->count; i++)
        a->words[i] = psx_mod_read_word(a->addr + 4u * (uint32_t)(i + 1));
    a->edge = edge;
    psx_mod_anchor_hud_primitive(a->addr, edge);
}

static void sf_widescreen_vblank(void) {
    if (!sf_enabled) return;
    for (int i = 0; i < SF_MAX_ANCHORS; i++) {
        SfAnchor* a = &sf_anchors[i];
        if (!a->addr) continue;
        if (sf_anchor_unchanged(a)) psx_mod_anchor_hud_primitive(a->addr, a->edge);
        else a->addr = 0;              /* recycled: never re-tag it */
    }
}

static void sf_record_sprite(uint32_t packet) {
    if (sf_sprite_count >= SF_MAX_SPRITES) return;
    int ci = sf_command_index(packet, 3);
    if (ci < 0) return;
    int x0, x1;
    if (!sf_packet_extent(packet, ci, &x0, &x1)) return;
    int y = (int)(int16_t)(psx_mod_read_word(packet + 4u * (uint32_t)(ci + 1)) >> 16);
    SfHudBox* b = &sf_sprites[sf_sprite_count++];
    b->packet = packet; b->x0 = x0; b->x1 = x1; b->y0 = y; b->y1 = y;
}

static void sf_sort_sprite_done(struct CPUState* cpu, uint32_t address) {
    (void)address;
    if (sf_enabled) sf_record_sprite(cpu->gpr[7]);
}

static void sf_add_prim_done(struct CPUState* cpu, uint32_t address) {
    (void)address;
    /* Only the HUD loop's scaled sprites; AddPrim is shared by other code. */
    uint32_t helper_frame = cpu->gpr[29] + 0x28u;
    if (sf_enabled && psx_mod_read_word(helper_frame + 0xACu) == SF_GS_SPRITE_SCALED_RA)
        sf_record_sprite(cpu->gpr[16]);
}

static int sf_box_cmp(const void* a, const void* b) {
    const SfHudBox* p = a; const SfHudBox* q = b;
    if (p->y0 != q->y0) return p->y0 - q->y0;
    return p->x0 - q->x0;
}

/* The sprite loop has emitted every HUD sprite of this pass: group glyphs into
 * rows of touching pieces so a string or icon moves as one element. */
static void sf_sprite_loop_exit(struct CPUState* cpu, uint32_t address) {
    (void)cpu; (void)address;
    if (!sf_enabled || !sf_sprite_count) { sf_sprite_count = 0; return; }
    qsort(sf_sprites, (size_t)sf_sprite_count, sizeof sf_sprites[0], sf_box_cmp);
    int start = 0;
    while (start < sf_sprite_count) {
        int end = start + 1, x0 = sf_sprites[start].x0, x1 = sf_sprites[start].x1;
        while (end < sf_sprite_count &&
               abs(sf_sprites[end].y0 - sf_sprites[start].y0) <= 2 &&
               sf_sprites[end].x0 <= x1 + 8) {
            if (sf_sprites[end].x1 > x1) x1 = sf_sprites[end].x1;
            end++;
        }
        int edge = sf_edge_for(x0, x1);
        for (int i = start; i < end; i++) {
            int ci = sf_command_index(sf_sprites[i].packet, 3);
            if (ci > 0) sf_anchor(sf_sprites[i].packet, ci, edge);
        }
        if (sf_hud_diag)
            fprintf(sf_hud_diag, "RUN y=%d x=%d..%d n=%d edge=%d\n",
                    sf_sprites[start].y0, x0, x1, end - start, edge);
        start = end;
    }
    sf_sprite_count = 0;
}

static void sf_prim_list_done(struct CPUState* cpu, uint32_t address) {
    (void)address;
    if (!sf_enabled) return;
    uint32_t packet = cpu->gpr[18], record = cpu->gpr[17];
    if (record < SF_HUD_RECORDS_LO || record >= SF_HUD_RECORDS_HI) return;
    int ci = sf_command_index(packet, (int)cpu->gpr[19]);
    int x0, x1;
    if (ci < 0 || !sf_packet_extent(packet, ci, &x0, &x1)) return;
    int edge = sf_edge_for(x0, x1);
    sf_anchor(packet, ci, edge);
    if (sf_hud_diag)
        fprintf(sf_hud_diag, "REC %08X op=%02X x=%d..%d edge=%d\n", record,
                psx_mod_read_word(packet + 4u * (uint32_t)ci) >> 24, x0, x1, edge);
}

static void sf_widescreen_activate(void) {
    char aspect[16];
    if (!psx_mod_option_value(PKG, FEATURE, "aspect", aspect, sizeof aspect))
        strcpy(aspect, "16:9");
    if (strcmp(aspect, "21:9") == 0) {
        (void)psx_mod_set_fixed_display_aspect(21u, 9u);
    } else if (strcmp(aspect, "adaptive") == 0) {
        (void)psx_mod_set_fixed_display_aspect(16u, 9u);
        (void)psx_mod_set_adaptive_display_aspect(21u, 9u);
    } else {
        (void)psx_mod_set_fixed_display_aspect(16u, 9u);
    }
    sf_enabled = 1;
    /* Developer aid: SF_HUD_DIAG=<path> logs every classification decision. */
    const char* diag = getenv("SF_HUD_DIAG");
    if (diag && *diag && !sf_hud_diag) sf_hud_diag = fopen(diag, "w");
}

/*
 * World block cull (0x800D40A4). Each world block is a box; the game projects
 * four corners of its mid-height rectangle and keeps the block only when one
 * corner lies in front of the camera, within the draw distance, and inside
 * |SX| <= a1 (a1 already carries the wide margin, [widescreen.cull] a1_sites).
 * A long block beside the camera can have every corner behind the camera or
 * past the draw distance while its sides cross the view: the 4:3 view never
 * showed those, the wide margins do (black gaps at the screen edges). Before
 * the first corner test this redoes the test properly -- the rectangle clipped
 * to the depth range, then projected -- and, when it overlaps the wide view,
 * makes corner 0 pass so the game's own accept path draws the block.
 */
#define SF_WORLD_BLOCK_TEST 0x800D43F4u /* mfc2 t1,SZ1 (corner 0) */
#define SF_WORLD_PIECE_TEST 0x800D35D0u /* bnez v0 after the piece centre test */
#define SF_DRAW_DISTANCE    0x80116454u
#define SF_NEAR_Z 16
#define SF_PIECE_GUARD 32
#define SF_MAX_PIECE_BLOCKS 256

extern int psx_ws_x_margin(void);

typedef struct { int32_t x, z; } SfCamPt;

static void sf_cam_point(const struct CPUState* cpu, int vx, int vy, int vz, SfCamPt* out) {
    const uint32_t* c = cpu->gte_ctrl;
    const int r11 = s16(c[0]), r12 = (int16_t)(c[0] >> 16), r13 = s16(c[1]);
    const int r31 = s16(c[3]), r32 = (int16_t)(c[3] >> 16), r33 = s16(c[4]);
    out->x = (int32_t)(((int64_t)r11 * vx + (int64_t)r12 * vy + (int64_t)r13 * vz) >> 12) + (int32_t)c[5];
    out->z = (int32_t)(((int64_t)r31 * vx + (int64_t)r32 * vy + (int64_t)r33 * vz) >> 12) + (int32_t)c[7];
}

/* Clips the polygon to z >= lo (keep_above) or z <= hi; returns the new count. */
static int sf_clip_z(const SfCamPt* in, int n, SfCamPt* out, int32_t plane, int keep_above) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        const SfCamPt a = in[i], b = in[(i + 1) % n];
        const int ina = keep_above ? a.z >= plane : a.z <= plane;
        const int inb = keep_above ? b.z >= plane : b.z <= plane;
        if (ina) out[m++] = a;
        if (ina != inb) {
            const int64_t t_num = (int64_t)(plane - a.z), t_den = (int64_t)(b.z - a.z);
            out[m].x = a.x + (int32_t)(((int64_t)(b.x - a.x) * t_num) / t_den);
            out[m].z = plane;
            m++;
        }
    }
    return m;
}

/* Whether a block's mid-height rectangle, clipped to near..far_z, projects
 * inside |SX| <= bound with the current GTE camera (rotation, translation, H). */
static int sf_rect_visible(const struct CPUState* cpu, int32_t x0, int32_t z0, int32_t x1,
                           int32_t z1, int32_t y, int32_t bound, int32_t far_z) {
    const int32_t h = (int32_t)(cpu->gte_ctrl[26] & 0xFFFFu);
    if (bound <= 0 || far_z <= SF_NEAR_Z || h <= 0) return 0;
    SfCamPt quad[4], a[8], b[12];
    sf_cam_point(cpu, x0, y, z0, &quad[0]);
    sf_cam_point(cpu, x0, y, z1, &quad[1]);
    sf_cam_point(cpu, x1, y, z1, &quad[2]);
    sf_cam_point(cpu, x1, y, z0, &quad[3]);
    int n = sf_clip_z(quad, 4, a, SF_NEAR_Z, 1);
    if (n < 3) return 0;
    n = sf_clip_z(a, n, b, far_z, 0);
    if (n < 3) return 0;
    int64_t lo = INT64_MAX, hi = INT64_MIN;
    for (int i = 0; i < n; i++) {
        const int64_t sx = (int64_t)h * b[i].x / b[i].z;
        if (sx < lo) lo = sx;
        if (sx > hi) hi = sx;
    }
    return lo <= bound && hi >= -bound;
}

/* The block test's draw distance (0x800D42B0): 1.375 x the level's. */
static int32_t sf_block_far(void) {
    uint32_t far_z = psx_mod_read_word(SF_DRAW_DISTANCE);
    return (int32_t)(far_z + (far_z >> 2) + (far_z >> 3));
}

static void sf_world_block_test(struct CPUState* cpu, uint32_t address) {
    (void)address;
    if (!sf_enabled || psx_ws_x_margin() <= 0) return;
    const int32_t x0 = s16(cpu->gpr[20]), z0 = s16(cpu->gpr[21]);   /* s4, s5 */
    const int32_t x1 = s16(cpu->gpr[22]), z1 = s16(cpu->gpr[23]);   /* s6, s7 */
    const int32_t y = (int16_t)(cpu->gpr[15] >> 16);                  /* t7 */
    if (!sf_rect_visible(cpu, x0, z0, x1, z1, y, (int32_t)cpu->gpr[5] /* a1 */,
                         (int32_t)cpu->gpr[25] /* t9 */))
        return;
    psx_mod_counter_add("sf.block_kept", 1);
    cpu->gte_data[17] = 1;   /* SZ1: in front, within the draw distance */
    cpu->gte_data[12] = 0;   /* SXY0: on the screen centre line */
}

/*
 * World piece cull (0x800D3100). Each piece (a building, a stretch of street)
 * owns a list of blocks; the game first projects the piece's centre only
 * (0x800D1898: in front, nearer than 0x1F40, |SX| < 0xC8 + margin) and skips
 * the whole piece otherwise. A large piece beside the camera has its centre
 * behind it while its blocks cross the wide margins. When that test rejects,
 * this checks the piece's blocks as above and keeps the piece if one shows.
 */
static void sf_world_piece_test(struct CPUState* cpu, uint32_t address) {
    (void)address;
    if (!sf_enabled || cpu->gpr[2] != 0) return;          /* v0: centre test result */
    psx_mod_counter_add("sf.piece_rejected", 1);
    const int margin = psx_ws_x_margin();
    if (margin <= 0) return;
    const uint32_t piece = cpu->gpr[17];                     /* s1 */
    const uint32_t owner = cpu->gpr[18];                     /* s2 */
    const uint32_t list = psx_mod_read_word(piece + ((psx_mod_read_word(owner + 8u) >> 16 & 4u) ? 0x24u : 0x20u));
    if ((list & 0x1FFFFFFFu) >= 0x00800000u || !list) return;
    const int32_t bound = 160 + margin + SF_PIECE_GUARD;
    const int32_t far_z = sf_block_far();
    for (int i = 1; i <= SF_MAX_PIECE_BLOCKS; i++) {
        const uint32_t block = psx_mod_read_word(list + 4u * (uint32_t)i);
        if (block == 0xFFFFFFFFu || (block & 0x1FFFFFFFu) >= 0x00800000u || !block) return;
        const int32_t y_lo = (int16_t)psx_mod_read_half(block + 0x10u);
        const int32_t y_hi = (int16_t)psx_mod_read_half(block + 0x12u);
        if (sf_rect_visible(cpu, (int16_t)psx_mod_read_half(block + 8u),
                            (int16_t)psx_mod_read_half(block + 0xAu),
                            (int16_t)psx_mod_read_half(block + 0xCu),
                            (int16_t)psx_mod_read_half(block + 0xEu),
                            y_hi - ((y_hi - y_lo) >> 1), bound, far_z)) {
            psx_mod_counter_add("sf.piece_rescued", 1);
            cpu->gpr[2] = 1;
            return;
        }
    }
}

PSX_MOD_CONSTRUCTOR(sf_register_widescreen_plugin) {
    (void)psx_mod_register_instruction_plugin("sf.widescreen", SF_WORLD_BLOCK_TEST,
                                              0x48098800u, sf_world_block_test);
    (void)psx_mod_register_instruction_plugin("sf.widescreen", SF_WORLD_PIECE_TEST,
                                              0x14400018u, sf_world_piece_test);
    (void)psx_mod_register_activation_plugin("sf.widescreen", sf_widescreen_activate);
    (void)psx_mod_register_vblank_plugin("sf.widescreen", sf_widescreen_vblank);
    (void)psx_mod_register_instruction_plugin("sf.widescreen", SF_SORT_SPRITE_DONE,
                                              0x24E70018u, sf_sort_sprite_done);
    (void)psx_mod_register_instruction_plugin("sf.widescreen", SF_ADD_PRIM_DONE,
                                              0xA0600003u, sf_add_prim_done);
    (void)psx_mod_register_instruction_plugin("sf.widescreen", SF_SPRITE_LOOP_EXIT,
                                              0x96420006u, sf_sprite_loop_exit);
    (void)psx_mod_register_instruction_plugin("sf.widescreen", SF_PRIM_LIST_DONE,
                                              0x00131080u, sf_prim_list_done);
}
