/* Friend AI for the extra member ("context B").
 *
 * The game's friend AI keeps its state in module globals and drives the two
 * friends from them. The extra member gets a second, private copy of that
 * state: around every AI entry point the mod swaps the globals with its copy
 * ("B"), runs the game's code once more for slot 3, and swaps back. While B is
 * active the AI thinks it is driving friend 1 alone (its friend sources are
 * redirected through g_aisrc), with its own formation spot behind Sora. */
#include <windows.h>
#include "kh1party.h"

/* AI module globals that make up one AI context. 0x23f1580..0x23f1590 (a
 * shared item bitmask) is left shared. */
#define AI_A_START   0x23f09c0
#define AI_A_LEN1    0xbc0
#define AI_A2_START  0x23f1590
/* Steam swaps 0x30 bytes here (the cure-target pointer plus padding). In the
 * Epic build the next, unrelated global starts 0x20 bytes after the pointer,
 * so only 0x20 bytes are swapped. */
#define AI_A2_LEN    0x20
#define AI_FORM      0x505de0      /* formation offsets, 0x20 bytes */

u8 g_ai_ready;   /* AI module started, B initialised */
u8 g_ai_cur;     /* 1 while context B is swapped in */

static __attribute__((aligned(16))) u8 g_ai_store[0xc00];
static __attribute__((aligned(16))) float g_form_store[8] = { 0, 0, 1, 1, 0, 0, 1, 1 };
/* Formation spot of the extra member: straight behind Sora. */
static const float g_form_init[8] = { 0, 0, 1, 1, 0, 0, 1, 1 };

static void swap_mem(u8 *a, u8 *b, size_t n)
{
    for (size_t i = 0; i < n; i += 16) {
        u8 t[16];
        memcpy(t, a + i, 16);
        memcpy(a + i, b + i, 16);
        memcpy(b + i, t, 16);
    }
}

/* Exchange context A (in the game's globals) with B (in g_ai_store). */
void ai_swap(void)
{
    swap_mem(A(AI_A_START), g_ai_store, AI_A_LEN1);
    swap_mem(A(AI_A2_START), g_ai_store + (AI_A2_START - AI_A_START), AI_A2_LEN);
    swap_mem(A(AI_FORM), (u8 *)g_form_store, 0x20);
}

static inline void to_b(void) { if (!g_ai_cur) { ai_swap(); g_ai_cur = 1; } }
static inline void to_a(void) { if (g_ai_ready && g_ai_cur) { ai_swap(); g_ai_cur = 0; } }

/* Original functions. */
void *o_ai_tick, *o_ai_pre, *o_ai_post, *o_ai_init, *o_ai_destroy,
     *o_ai_reset, *o_ai_shutdown, *o_ai_start;

typedef u64 (*fn1)(void *);
typedef u64 (*fn0)(void);

/* Per-entity AI tick. */
u64 ai_tick_hook(ARGS4)
{
    u8 *ent = (u8 *)a1_;
    u64 r;
    if (ent != SLOT3() || !g_ai_ready)
        return CALL4(o_ai_tick);
    to_b();
    r = CALL4(o_ai_tick);
    to_a();
    return r;
}

/* ------------------------------------------------------- party commands */

/* The party-command button sets bit 0 of 0x2d539e0 for one frame. ai_pre
 * reads it and orders one of its two friend sources: the one closer to the
 * target, or the other one if that one is still busy with an earlier order
 * (AI state +0x98 is the order timer, 600 when ordered, counting down). Since
 * ai_pre runs once for A and once for B, both used to react to every press.
 * Now exactly one context sees each press: A while Donald/Goofy (slots 1, 2)
 * have a free ally, then B (the extra member), then round again. */
#define CMD_FLAGS 0x2d539e0

static int orderable(u8 *e)
{
    return e && ent_active(e) && !FN(0x29bbc0, int, u8 *)(e) && !(F32(e, ENT_FLAGS) & 0x200) &&
           HGET(F32(e, ENT_AI_H));
}

static int order_free(u8 *e)   /* orderable and not busy with an order */
{
    return orderable(e) && FF(HGET(F32(e, ENT_AI_H)), 0x98) <= 0.0f;
}

static int g_cmd_last_b;

static int command_goes_to_b(void)
{
    u8 *e1 = PARTY(1), *e2 = PARTY(2), *e3 = SLOT3();
    int r;
    if (!orderable(e3))
        r = 0;
    else if (!orderable(e1) && !orderable(e2))
        r = 1;
    else if (order_free(e1) || order_free(e2))
        r = 0;
    else if (order_free(e3))
        r = 1;
    else
        r = !g_cmd_last_b;          /* everyone busy: alternate */
    g_cmd_last_b = r;
    return r;
}

/* Per-frame AI pre-update: runs once for A (friend sources = slots 1, 2) and
 * once for B (friend source = slot 3). */
u64 ai_pre_hook(ARGS4)
{
    u64 r;
    u32 *cmd = (u32 *)A(CMD_FLAGS), saved = *cmd;
    int b_cmd = 0;
    if (g_debug)
        dbg_frame();
    if ((saved & 1) && g_ai_ready && SLOT3()) {
        b_cmd = command_goes_to_b();
        if (g_debug)
            logf_("party command -> %s", b_cmd ? "extra member" : "friends");
    }
    g_aisrc[0] = PARTY(1);
    g_aisrc[1] = PARTY(2);
    to_a();
    if (b_cmd)
        *cmd = saved & ~1u;
    r = CALL4(o_ai_pre);
    if (g_ai_ready && SLOT3()) {
        *cmd = b_cmd ? saved : (saved & ~1u);
        g_aisrc[0] = SLOT3();
        g_aisrc[1] = NULL;
        to_b();
        CALL4(o_ai_pre);
        to_a();
        g_aisrc[0] = PARTY(1);
        g_aisrc[1] = PARTY(2);
    }
    *cmd = saved;
    return r;
}

/* -------------------------------------------------------- heal targets */

/* Every friend heal decision (Cure, potions, ethers, the item pick in
 * ai_tick) chooses among itself, Sora and "the other friend", which the game
 * reads as G[idx ^ 3] (G = 0x23f09c0: Sora, friend source 1, friend source 2;
 * idx = the AI state's friend index). In context B that is NULL, so the extra
 * member only ever healed Sora; in A it is never the extra member, so Donald
 * and Goofy never healed it. While one of those decisions runs, "the other
 * friend" is the most hurt of the other party members (lowest HP share, or MP
 * share for ethers). The game still applies its own thresholds. */
#define AI_G 0x23f09c0

static const HookSite H_heal_cure = { "heal_cure", 0x1ce650, 7, 0, {0}, {0},
                                      {0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x57} };
static const HookSite H_heal_hp1 = { "heal_hp1", 0x1cf570, 6, 0, {0}, {0}, {0x40, 0x55, 0x41, 0x54, 0x41, 0x57} };
static const HookSite H_heal_hp2 = { "heal_hp2", 0x1d0150, 5, 0, {0}, {0}, {0x4c, 0x8b, 0xdc, 0x53, 0x55} };
static const HookSite H_heal_mp = { "heal_mp", 0x1d07e0, 6, 0, {0}, {0}, {0x40, 0x53, 0x55, 0x56, 0x41, 0x54} };
static const HookSite H_ai_other = { "ai_other", 0x1cd4e0, 10, 1, {6}, {10},
                                     {0x48, 0x63, 0x02, 0x48, 0x8d, 0x0d, 0xd6, 0x34, 0x22, 0x02} };
static const HookSite H_item_other = { "item_other", 0x1cdfeb, 10, 1, {6}, {10},
                                       {0x48, 0x63, 0x06, 0x48, 0x8d, 0x3d, 0x0b, 0x20, 0xe3, 0xff} };

/* What the game's heal code dereferences on a candidate: flags, stats handle
 * (+0x6c) and the handle at +0x148 (read by the Cure loop at 0x1ce7a8). */
static int heal_candidate(u8 *c)
{
    if (!ent_active(c) || (F32(c, ENT_FLAGS) & 0x200) || !HGET(F32(c, 0x148)))
        return 0;
    return HGET(F32(c, ENT_STATS_H)) != NULL;
}

static u64 need(u8 *c, int mp)    /* current/max * 2^20, lower = more hurt */
{
    u8 *st = HGET(F32(c, ENT_STATS_H));
    s32 cur = FS32(st, mp ? 0x44 : 0x3c), max = FS32(st, mp ? 0x48 : 0x40);
    if (max <= 0)
        return (u64)1 << 40;
    if (cur < 0)
        cur = 0;
    return ((u64)cur << 20) / (u64)max;
}

static u8 *pick_other(u8 *self, u8 *st, int mp)
{
    u8 **G = (u8 **)A(AI_G);
    s32 i;
    u8 *orig, *best = NULL;
    u64 bn = 0;
    if (!st)
        return NULL;
    i = *(s32 *)st ^ 3;
    if (i != 1 && i != 2)
        return G[i & 3];
    orig = G[i];
    if (!SLOT3() || !g_ai_ready)
        return orig;
    if (heal_candidate(orig)) {
        best = orig;
        bn = need(orig, mp);
    }
    for (int s = 1; s <= 3; s++) {
        u8 *c = PARTY(s);
        if (!c || c == self || c == G[0] || c == orig || !heal_candidate(c))
            continue;
        if (!best || need(c, mp) < bn) {
            best = c;
            bn = need(c, mp);
        }
    }
    return best ? best : orig;
}

typedef u64 (*fn4)(u8 *, u8 *, u64, u64);
static void *o_heal_cure, *o_heal_hp1, *o_heal_hp2, *o_heal_mp;

static u64 heal_wrap(fn4 f, u8 *self, u8 *st, u64 a3, u64 a4, int mp)
{
    u8 **G = (u8 **)A(AI_G);
    s32 i = st ? (*(s32 *)st ^ 3) : 0;
    u8 *save;
    u64 r;
    if (!SLOT3() || !g_ai_ready || (i != 1 && i != 2))
        return f(self, st, a3, a4);
    save = G[i];
    G[i] = pick_other(self, st, mp);
    r = f(self, st, a3, a4);
    G[i] = save;
    return r;
}

static u64 heal_cure_hook(u8 *e, u8 *st, u64 a3, u64 a4) { return heal_wrap((fn4)o_heal_cure, e, st, a3, a4, 0); }
static u64 heal_hp1_hook(u8 *e, u8 *st, u64 a3, u64 a4) { return heal_wrap((fn4)o_heal_hp1, e, st, a3, a4, 0); }
static u64 heal_hp2_hook(u8 *e, u8 *st, u64 a3, u64 a4) { return heal_wrap((fn4)o_heal_hp2, e, st, a3, a4, 0); }
static u64 heal_mp_hook(u8 *e, u8 *st, u64 a3, u64 a4) { return heal_wrap((fn4)o_heal_mp, e, st, a3, a4, 1); }

/* 0x1cd4e0 returns "the other friend" for two more heal handlers: the one
 * called from 0x1d88af tests MP, the one from 0x1d8a01 tests HP. */
static u8 *ai_other_hook(u8 *self, u8 *st)
{
    int mp = __builtin_return_address(0) == (void *)A(0x1d88b4);
    return pick_other(self, st, mp);
}

/* Item target pick inside ai_tick (0x1cdfeb: rdi = G[idx ^ 3]). */
static u64 h_item_other(Ctx *c)
{
    c->rdi = (u64)pick_other((u8 *)c->rbp, (u8 *)c->rsi, 0);
    return (u64)A(0x1ce001);
}

int ai_heal_install(void)
{
    return (o_heal_cure = hook_fn(&H_heal_cure, (void *)heal_cure_hook)) &&
           (o_heal_hp1 = hook_fn(&H_heal_hp1, (void *)heal_hp1_hook)) &&
           (o_heal_hp2 = hook_fn(&H_heal_hp2, (void *)heal_hp2_hook)) &&
           (o_heal_mp = hook_fn(&H_heal_mp, (void *)heal_mp_hook)) &&
           hook_fn(&H_ai_other, (void *)ai_other_hook) &&
           hook_ctx(&H_item_other, h_item_other);
}

u64 ai_post_hook(ARGS4)
{
    u64 r;
    to_a();
    r = CALL4(o_ai_post);
    if (g_ai_ready && SLOT3()) {
        to_b();
        CALL4(o_ai_post);
        to_a();
    }
    return r;
}

/* AI state creation for an entity. For slot 3 it is created in context B,
 * with slot 3 temporarily posing as friend 1. */
u64 ai_init_hook(ARGS4)
{
    u8 *ent = (u8 *)a1_;
    u8 *save;
    u64 r;
    if (ent != SLOT3() || !g_ai_ready) {
        if (ent == SLOT3())
            logf_("WARNING: slot 3 AI init before the AI module was started");
        return CALL4(o_ai_init);
    }
    save = PARTY(1);
    PARTY(1) = ent;
    to_b();
    r = CALL4(o_ai_init);
    to_a();
    PARTY(1) = save;
    if (g_debug) {
        u8 *st = HGET(F32(ent, ENT_AI_H));
        logf_("AI init for slot 3: state=%p idx=%d", st, st ? (int)F32(st, 0) : -99);
    }
    return r;
}

u64 ai_destroy_hook(ARGS4)
{
    u8 *ent = (u8 *)a1_;
    u64 r;
    /* The others may remember this party member as their last heal target
     * (AI state +0xe0, a raw pointer); forget it. */
    if (ent && party_slot_of(ent) >= 0) {
        for (int i = 1; i <= 3; i++) {
            u8 *f = PARTY(i), *st = (f && f != ent) ? HGET(F32(f, ENT_AI_H)) : NULL;
            if (st && FP(st, 0xe0) == ent)
                FP(st, 0xe0) = NULL;
        }
    }
    if (ent != SLOT3() || !g_ai_ready)
        return CALL4(o_ai_destroy);
    to_b();
    r = CALL4(o_ai_destroy);
    to_a();
    return r;
}

/* Tear down whatever context B still holds. */
void ai_b_cleanup(void)
{
    u8 *e;
    if (g_ai_ready && !g_ai_cur) {
        ai_swap();
        g_ai_cur = 1;
    }
    e = SLOT3();
    if (ent_active(e)) {
        u8 *st = HGET(F32(e, ENT_AI_H));
        if (st)
            FN(0x1cd1e0, void, u8 *, u8 *)(e, st);
    }
    G64(0x23f0da0) = 0;
    G64(0x23f0d90) = 0;
    G64(0x23f0d80) = 0;
    FN(0x1d3d90, void, void)();
    to_a();
}

u64 ai_reset_hook(ARGS4)
{
    u64 r;
    to_a();
    r = CALL4(o_ai_reset);
    if (g_ai_ready)
        ai_b_cleanup();
    return r;
}

u64 ai_shutdown_hook(ARGS4)
{
    if (g_ai_ready) {
        to_b();
        for (u32 o = 0x23f0d80; o <= 0x23f0da0; o += 0x10) {
            u32 *p = (u32 *)G64(o);
            if (p) {
                *p = 0;
                G64(o) = 0;
            }
        }
        to_a();
        ai_b_cleanup();
        to_a();
    }
    return CALL4(o_ai_shutdown);
}

/* AI module start (room load): B starts as a copy of the fresh A. */
u64 ai_start_hook(ARGS4)
{
    u64 r;
    to_a();
    r = CALL4(o_ai_start);
    memcpy(g_ai_store, A(AI_A_START), AI_A_LEN1);
    memcpy(g_ai_store + (AI_A2_START - AI_A_START), A(AI_A2_START), AI_A2_LEN);
    if (!g_ai_ready)
        memcpy(g_form_store, g_form_init, sizeof g_form_store);
    g_ai_ready = 1;
    g_ai_cur = 0;
    return r;
}
