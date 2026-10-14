/* HUD for the extra member: its HP gauge, level-up prompts, the item pop-up
 * and its figure in the party menu. The game has three party gauges (plus the
 * summon gauge, index 3); the fourth party gauge lives in mod memory and is
 * cloned from the game's friend gauge. */
#include <windows.h>
#include "kh1party.h"

/* The HUD draw loop walks a per-gauge flag (rbp) and the gauge array (rbx) in
 * step. To draw our gauge as one more iteration it points rbp at
 * g_gauge_flags[0] and the loop end at g_gauge_flags[2]: the extra iteration
 * then reads g_gauge_flags[1], the "our gauge is live" flag. */
s32 g_gauge_flags[3];
int g_summoning;            /* a summon is on: gauge index 3 is the summon's */

static u8 g_gauge_tpl[0xdf0];
static u8 g_tpl_ok;

int g_hud_dx = -68, g_hud_dy = -20, g_hud_shift = 20;
int g_mf_x0 = 2025, g_mf_step = 78, g_mf_pct = 80;
__attribute__((aligned(16))) float g_menu_x4[4];

#define GAUGE_TPL   0x28135e0     /* friend gauge (index 2) used as template */
#define GAUGE_SZ    0xdf0

void *o_hud_fcd0, *o_hud_layout, *o_hud_create, *o_hud_flash, *o_hud_950,
     *o_hud_fade, *o_hud_970, *o_hud_9d0, *o_hud_pos, *o_lvlprompt, *o_lv_init,
     *o_lv_active, *o_lv_clear, *o_item_open, *o_item_close, *o_item_reset,
     *o_item_init, *o_item_task;

/* Item window state for the extra member: [0] state, [4] alpha, [8] kind,
 * [0x10] entity. While the AI of the extra member runs, the "is the item
 * window busy" query reports ours. */
u64 hud_fcd0_hook(ARGS4)
{
    u32 *out = (u32 *)a1_;
    u64 r = CALL4(o_hud_fcd0);
    if (g_ai_cur == 1) {
        u32 s = F32(g_itemwin, 0);
        *out = !(s == 0 || s == 4 || s == 5 || s == 6);
    }
    return r;
}

/* HUD layout (room load): grab a clean copy of the friend gauge. */
u64 hud_layout_hook(ARGS4)
{
    u64 r = CALL4(o_hud_layout);
    memcpy(g_gauge_tpl, A(GAUGE_TPL), GAUGE_SZ);
    g_tpl_ok = 1;
    g_gauge_on = 0;
    return r;
}

static int is_friend(u8 *e) { return e && (PARTY(1) == e || PARTY(2) == e || PARTY(3) == e); }

/* Gauge creation. Index 3 is the summon gauge in the game; the extra member
 * (party list position 3) asks for index 3 too, so calls for a friend entity
 * build our own gauge instead. */
u64 hud_create_hook(ARGS4)
{
    u8 *ent = (u8 *)a1_;
    int idx = (int)a2_;
    if (idx != 3 || (ent && !is_friend(ent)))
        return CALL4(o_hud_create);
    if (!ent) {
        if (g_gauge_on) {
            u8 *e = FP(g_gauge, 8);
            if (!ent_active(e))
                g_gauge_on = 0;
        }
        return CALL4(o_hud_create);
    }
    if (!g_tpl_ok)
        return 0;
    {
        u8 *tpl0 = A(GAUGE_TPL), *tpl1 = A(GAUGE_TPL) + GAUGE_SZ;
        u8 *st;
        u32 maxhp, maxmp;
        float scale;
        memcpy(g_gauge, g_gauge_tpl, GAUGE_SZ);
        /* Re-point internal pointers that pointed into the template gauge. */
        for (u32 o = 0; o < GAUGE_SZ; o += 8) {
            u8 *v = FP(g_gauge, o);
            if (v >= tpl0 && v < tpl1)
                FP(g_gauge, o) = v + (g_gauge - tpl0);
        }
        FP(g_gauge, 8) = ent;
        F32(g_gauge, 0) = 2;
        F32(g_gauge, 0xa98) = 1;
        {
            u32 k = FN(0x2690d0, u8, u8 *)(g_gauge);
            F32(g_gauge, 0x108) = k;
            F32(g_gauge, 0x10c) = k;
        }
        F32(g_gauge, 0xaa4) = 0;
        F32(g_gauge, 0x7f0) = 0;
        F32(g_gauge, 0xa50) = 0;
        F64(g_gauge, 0x110) = 0;
        FF(g_gauge, 0xa9c) = 0.0f;     /* alpha */
        FF(g_gauge, 0xaa0) = 0.1f;     /* fade-in speed */
        st = HGET(F32(ent, ENT_STATS_H));
        maxhp = F32(st, 0x44);
        maxmp = F16(st, 0xbe);
        scale = GF(0x3b20a0);
        F32(g_gauge, 0xa60) = maxhp;
        FF(g_gauge, 0xa64) = (float)(s32)maxhp * GF(0x42f96c);
        FN(0x27f760, void, u8 *, float, float, float, float)(g_gauge + 0xa6c, 0, 0, 0, scale);
        F32(g_gauge, 0xa7c) = maxmp;
        FF(g_gauge, 0xa80) = (float)(s32)maxmp;
        FN(0x27f760, void, u8 *, float, float, float, float)(g_gauge + 0xa88, 0, 0, 0, scale);
        F64(g_gauge, 0xaac) = 0;
        g_gauge_on = 1;
    }
    return 0;
}

/* Damage / heal flash on gauge index 3. */
u64 hud_flash_hook(ARGS4)
{
    int idx = (int)a1_;
    u32 kind = (u32)a2_;
    static const u32 obj[3] = { 0x2a0, 0x360, 0x420 };
    static const u32 val[3] = { 0x5270fc, 0x527134, 0x52726c };
    if (idx != 3 || !g_gauge_on) {
        return CALL4(o_hud_flash);
    }
    if (kind <= 2 && F64(g_gauge, 0x110) == 0) {
        u8 *o = g_gauge + obj[kind];
        FP(g_gauge, 0x110) = o;
        FN(0x27e710, void, u8 *)(o);
        F32(g_gauge, 0xab0) = G32(val[kind]);
    }
    return 0;
}

u64 hud_950_hook(ARGS4)
{
    if ((int)a1_ == 3 && g_gauge_on)
        return FN(0x26e180, u64, u8 *)(g_gauge);
    return CALL4(o_hud_950);
}

/* Fade in / out on gauge index 3: apply it to our gauge too (the game then
 * handles its summon gauge as usual). */
u64 hud_fade_hook(ARGS4)
{
    int idx = (int)a1_;
    u32 kind = (u32)a2_;
    if (idx == 3 && g_gauge_on && kind <= 3) {
        static const float a[4] = { 1.0f, 0.0f, 1.0f, 0.0f };
        static const float d[4] = { -0.1f, 0.1f, 0.0f, 0.0f };
        FF(g_gauge, 0xa9c) = a[kind];
        FF(g_gauge, 0xaa0) = d[kind];
    }
    return CALL4(o_hud_fade);
}

u64 hud_970_hook(ARGS4)
{
    u8 *ent = (u8 *)a1_;
    if (g_gauge_on && FP(g_gauge, 8) == ent) {
        F32(g_gauge, 0x7f4) = 1;
        return 0;
    }
    return CALL4(o_hud_970);
}

u64 hud_9d0_hook(ARGS4)
{
    u8 *ent = (u8 *)a1_;
    if (g_gauge_on && FP(g_gauge, 8) == ent) {
        if (!F32(g_gauge, 0x7f0))
            F32(g_gauge, 0x7f0) = 1;
        return 0;
    }
    return CALL4(o_hud_9d0);
}

/* Gauge position: friend gauges move up by g_hud_shift to make room for ours,
 * which sits below them (offset g_hud_dx / g_hud_dy). Like the game's gauges,
 * ours shakes while its hit effect plays (gauge +0x110). */
u64 hud_pos_hook(ARGS4)
{
    u8 *gauge = (u8 *)a1_;
    s32 *icon = (s32 *)a2_, *pos = (s32 *)a3_;
    u64 r;
    if (gauge == g_gauge) {
        u8 *fx;
        icon[0] = GS32(0x5272c0);
        icon[1] = GS32(0x5272c4);
        pos[0] = GS32(0x527330) + GS32(0x232dccc) + g_hud_dx;
        pos[1] = g_hud_dy + GS32(0x527334) + g_hud_shift;
        (void)fx;
        return 0;
    }
    r = CALL4(o_hud_pos);
    if (g_gauge_on && (F32(gauge, 0) == 1 || F32(gauge, 0) == 2))
        pos[1] += g_hud_shift;
    return r;
}

/* -------------------------------------------------------- level-up prompts */

/* The game shows level-up prompts for list positions 0-2. Prompts for the
 * extra member go into our queue of up to five per frame. Queue layout:
 * [0] count, [4] frame stamp, entries of 0xba0 bytes from +0x10. */
#define LV_N     5
#define LV_ENT   0xba0
#define LV_E(k)  (g_lv + 0x10 + (k) * LV_ENT)

u64 lvlprompt_hook(ARGS4)
{
    u8 *ent = (u8 *)a1_;
    void *a = (void *)a2_, *b = (void *)a3_;
    int ch = -1, idx;
    if (ent) {
        u8 *d = HGET(F32(ent, ENT_DESC_H));
        if (d) ch = F16(d, 0x4c);
    }
    idx = FN(0x285030, int, int)(ch);
    if ((u32)idx <= 2) {
        return CALL4(o_lvlprompt);
    }
    if (idx != 3)
        return 0;
    {
        u32 frame = G32(0x28461fc);
        s32 k = 0;
        u8 *e;
        u32 off;
        if (F32(g_lv, 4) == frame) {
            k = FS32(g_lv, 0);
            if (k > 4)
                return 0;
        }
        F32(g_lv, 4) = frame;
        F32(g_lv, 0) = k + 1;
        e = LV_E(k);
        F32(e, 0x0) = 1;
        F64(e, 0x8) = 3;
        F32(e, 0x18) = (a != NULL) + (b != NULL);
        *(void **)(e + 0x20) = a;
        *(void **)(e + 0x28) = b;
        FF(e, 0xb80) = 1.0f;
        if (ch == 0 || ch == 1) {
            off = ch == 1 ? 0x10 : 0;
            FP(e, 0xb88) = A(0x527a10) + off;
            FP(e, 0xb90) = A(0x527a50) + off;
        } else {
            off = ch != 2 ? 0x10 : 0;
            FP(e, 0xb88) = A(0x527a30) + off;
            FP(e, 0xb90) = A(0x527a70) + off;
            if ((u32)ch > 9)
                return 0;
        }
        FP(e, 0x30) = FN(0x28f9b0, u8 *, int)(ch + 0x127);
    }
    return 0;
}

u64 lv_init_hook(ARGS4)
{
    u8 *res, *font;
    u64 r = CALL4(o_lv_init);
    memset(g_lv, 0, 0x10 + LV_N * LV_ENT);
    res = A(0x28462f0);
    font = FP(GP(0x280ca60), 0x38) + 0xc;
    for (int k = 0; k < LV_N; k++) {
        u8 *e = LV_E(k);
        FN(0x27e1c0, void, u8 *, u8 *, u8 *)(A(0x5279c0), e + 0x40, res);
        FN(0x27e1c0, void, u8 *, u8 *, u8 *)(A(0x5279d8), e + 0x340, res);
        FN(0x27e1c0, void, u8 *, u8 *, u8 *)(A(0x5279e8), e + 0x4c0, res);
        FN(0x27e1c0, void, u8 *, u8 *, u8 *)(font, e + 0x940, res);
    }
    return r;
}

u64 lv_active_hook(ARGS4)
{
    s32 n;
    if (CALL4(o_lv_active))
        return 1;
    n = FS32(g_lv, 0);
    if (n > LV_N) n = LV_N;
    for (int k = 0; k < n; k++)
        if (F32(LV_E(k), 0))
            return 1;
    return 0;
}

u64 lv_clear_hook(ARGS4)
{
    u64 r = CALL4(o_lv_clear);
    for (int k = 0; k < LV_N; k++)
        F32(LV_E(k), 0) = 0;
    return r;
}

/* Prompt draw loop: after the game's last entry, walk ours. */
u64 h_lv_tail(Ctx *c)
{
    u8 *cur = (u8 *)c->rbp;
    if (cur == A(0x283b38c)) {
        if (FS32(g_lv, 0) > 0) {
            c->rbp = (u64)g_lv;
            c->rax = (u64)(g_lv + 0x20);
            *(u8 **)(CTX_RSP(c) + 0xf0) = g_lv + 0x20;
            return (u64)A(0x272fb0);
        }
        return (u64)A(0x27316c);
    }
    return (u64)A(cur == g_lv + 4 ? 0x27316c : 0x272fb0);
}

/* Gauge draw loop: after the game's last gauge, draw ours. */
u64 h_hud_tail(Ctx *c)
{
    u8 *e;
    if ((u8 *)c->rbp != A(0x28119f0) || !g_gauge_on)
        return 0;
    e = FP(g_gauge, 8);
    if (!ent_active(e) || !is_friend(e)) {
        g_gauge_on = 0;
        return 0;
    }
    /* Fade together with the friend gauges. */
    {
        u32 off = 0;
        if (G32(0x28119e4)) off = 0xdf0;
        else if (G32(0x28119e8)) off = 0x1be0;
        if (off) {
            F32(g_gauge, 0xa9c) = G32(0x281249c + off);
            F32(g_gauge, 0xaa0) = G32(0x28124a0 + off);
        }
    }
    {
        u8 *st = HGET(F32(e, ENT_STATS_H));
        if (st) {
            if (FS32(st, 0x3c) > 0)
                F32(g_gauge, 0xa54) = 1;
            else if (!F32(g_gauge, 0xa50))
                F32(g_gauge, 0xa50) = 1;
        }
    }
    c->rbp = (u64)&g_gauge_flags[0];
    c->rbx = (u64)(g_gauge - GAUGE_SZ);
    c->r15 = (u64)&g_gauge_flags[2];
    return 0;
}

u64 h_gauge_rebuild(Ctx *c)
{
    (void)c;
    g_gauge_on = 0;
    return 0;
}

/* ---------------------------------------------------------------- item pop-up */

u64 item_open_hook(ARGS4)
{
    u32 kind = (u32)a1_;
    u8 *ent = (u8 *)a2_;
    if (!ent || ent != SLOT3())
        return CALL4(o_item_open);
    if (F32(g_itemwin, 0))
        return 0;
    F32(g_itemwin, 8) = kind;
    FP(g_itemwin, 0x10) = ent;
    F32(g_itemwin, 0) = 1;
    FF(g_itemwin, 4) = 1.0f;
    return 0;
}

u64 item_close_hook(ARGS4)
{
    u8 *ent = (u8 *)a1_;
    if (!ent || ent != SLOT3())
        return CALL4(o_item_close);
    if (F32(g_itemwin, 0))
        F32(g_itemwin, 0) = 4;
    return 0;
}

u64 item_reset_hook(ARGS4)
{
    u64 r = CALL4(o_item_reset);
    F32(g_itemwin, 0) = 0;
    return r;
}

u64 item_init_hook(ARGS4)
{
    memset(g_itemwin, 0, 0x18);
    return CALL4(o_item_init);
}

u64 item_task_hook(ARGS4)
{
    if (F32(g_itemwin, 0)) {
        u8 *e = FP(g_itemwin, 0x10);
        if (!e || e != SLOT3() || !ent_active(e) || FN(0x297950, int, u8 *)(e))
            F32(g_itemwin, 0) = 0;
    }
    return CALL4(o_item_task);
}

/* Item pop-up draw loop: one more element, ours. */
u64 h_item_tail(Ctx *c)
{
    u8 *next = (u8 *)c->rsi + 0x18;
    if (next == g_itemwin + 0x1c)
        return (u64)A(0x27013e);
    c->rsi = (u64)(next == A(0x2822024) ? g_itemwin + 4 : next);
    return (u64)A(0x26fee0);
}

/* ------------------------------------------------------------- party menu */

/* Party menu with four figures: scale the figures down (g_mf_pct) and lay
 * them out from g_mf_x0 in steps of g_mf_step. */
void hud_menu_rebuild(void)
{
    float s = g_mf_pct > 0 ? (float)g_mf_pct / 100.0f : 1.0f;
    if (G32(0x2e1f570) != 4)
        s = 1.0f;
    memcpy(g_menumtx, A(0x52abb0), 0x100);
    for (int m = 0; m < 4; m++) {
        FF(g_menumtx, 0x34 + m * 0x40) /= s;
        FF(g_menumtx, 0x38 + m * 0x40) /= s;
    }
    for (int i = 0; i < 4; i++)
        g_menu_x4[i] = (float)(g_mf_x0 + i * g_mf_step);
}

u64 h_menu_mtx(Ctx *c)
{
    hud_menu_rebuild();
    c->rdx = (u64)g_menumtx;
    return (u64)A(0x2dbb1b);
}

u64 h_menu_x(Ctx *c)
{
    u64 n = c->rax;
    c->rax = n == 4 ? (u64)g_menu_x4 : G64(0x52b0e8 + n * 8);
    return (u64)A(0x2dbc7d);
}

u64 h_pm_load(Ctx *c)
{
    u8 *save = SAVE();
    c->rax = (u64)save;
    if (F8(save, 0x491) != 0xff)
        return (u64)A(G32(0x2e20238) == 0 ? 0x2dccf4 : 0x2dcd47);
    return (u64)A(0x2dcd47);
}
