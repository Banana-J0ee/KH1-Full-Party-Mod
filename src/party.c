/* Party plumbing: the fourth body in party slot 3, its model memory, weapon
 * and voice slots, room-entry spawning, summons and the party menu swap. */
#include <windows.h>
#include "kh1party.h"

/* Game functions. */
#define find_ent_by_char FN(0x2a3290, u8 *, u32)

u8 *party_find_ent_by_char(int ch) { return find_ent_by_char((u32)ch); }

int party_slot_of(u8 *ent)
{
    if (!ent) return -1;
    for (int i = 0; i < 4; i++)
        if (PARTY(i) == ent) return i;
    return -1;
}

static int is_friend_ent(u8 *e)   /* slots 1-3 */
{
    return e && (PARTY(1) == e || PARTY(2) == e || PARTY(3) == e);
}

/* ------------------------------------------------------------ model memory */

/* 0x2858e0 sp_release(start, count): the call that resets the whole pool at
 * start-up uses count 64; make it cover the extended pool. */
u64 h_sp_release(Ctx *c)
{
    if ((u32)c->rcx == 0 && (u32)c->rdx == SP_BLOCKS_OLD)
        c->rdx = SP_BLOCKS;
    return 0;
}

/* The game clears flag bits on the 64 original records; do the same on ours. */
u64 h_sp_clrfa(Ctx *c)
{
    (void)c;
    for (int i = SP_BLOCKS_OLD; i < SP_BLOCKS; i++)
        g_sprec[i * SP_REC + 2] &= 0xfa;
    return 0;
}

u64 h_sp_keep(Ctx *c)
{
    (void)c;
    for (int i = SP_BLOCKS_OLD; i < SP_BLOCKS; i++)
        g_sprec[i * SP_REC + 2] &= 0xfe;
    return 0;
}

/* ----------------------------------------------------------- weapon slots */

/* Weapon slots: the fourth slot uses its own 128 KB buffer. */
u64 h_w_init(Ctx *c)
{
    (void)c;
    F32(g_wslot, 3 * 0x30) &= ~1u;
    F32(g_wslot, 3 * 0x30 + 0xc) = 0x20000;
    *(u8 **)(g_wslot + 3 * 0x30 + 0x10) = g_w3buf;
    FN(0x295660, void, int)(-11);          /* release the slot's sound group */
    return 0;
}

u64 h_w_clr6(Ctx *c)
{
    (void)c;
    F32(g_wslot, 3 * 0x30) &= ~6u;
    return 0;
}

/* Sound-effect group of weapon slot n is -3-n; slot 3 would collide with a
 * group the game uses for something else, so it gets -11. */
u64 h_w_seid(Ctx *c)
{
    u32 n = (u32)c->rdx;
    c->rbx = (u32)(n == 3 ? -11 : -3 - (s32)n);
    return (u64)A(0x28725b);
}

/* ------------------------------------------------------------ voice banks */

u64 h_v_fee0(Ctx *c)
{
    if ((s32)FS8(g_voice, 3 * 0x22) == (s32)c->rcx)
        g_voice[3 * 0x22 + 1] &= 0xfe;
    return 0;
}

u64 h_v_ff20(Ctx *c)
{
    (void)c;
    g_voice[3 * 0x22 + 1] &= 0xfe;
    return 0;
}

u64 h_v_ffc0(Ctx *c)
{
    (void)c;
    g_voice[3 * 0x22 + 1] &= 0xfe;
    g_voice[3 * 0x22] = 0xff;
    return 0;
}

/* ----------------------------------------------------------- room entry */

/* Party array reset at room load: clear slot 3 too. */
u64 h_party_init(Ctx *c)
{
    (void)c;
    SLOT3() = NULL;
    g_summoning = 0;
    return 0;
}

/* Room actor list (0x78-byte descriptors): the game keeps the descriptors of
 * the two friends in the party and packs their models into consecutive blocks
 * starting at the first friend descriptor's block. Keep the reserve member's
 * descriptor as well and give it its own blocks past the end of the original
 * pool (see SP_EXTRA_BLOCK). */
static int g_kept;

u64 h_friendfilter(Ctx *c)
{
    u8 *desc = (u8 *)c->rbx;
    s32 next = (s32)(u32)c->rdi;            /* running block index, -1 at first */
    u16 ch = F16(desc, 0x48);
    u8 l1 = PARTY_LIST(1), l2 = PARTY_LIST(2), l3 = PARTY_LIST(3);

    if (next < 0) {
        next = FS8(desc, 0x51);
        g_kept = 0;
    }
    if ((l1 == 0xff || ch != l1) && (l2 == 0xff || ch != l2) && (l3 == 0xff || ch != l3)) {
        desc[0] |= 1;                        /* not in the party: skip */
    } else {
        s32 count = FS8(desc, 0x52);
        if (g_kept <= 1) {
            desc[0x51] = (u8)next;
            next += count;
        } else if (SP_EXTRA_BLOCK + count <= SP_BLOCKS) {
            desc[0x51] = SP_EXTRA_BLOCK;
        } else {
            /* Too big for its area: keep the old placement right after
             * the game's blocks (summons may then overwrite it). */
            logf_("WARNING: extra member model needs %d blocks; placed at block %d",
                  count, SP_BLOCKS_OLD);
            desc[0x51] = SP_BLOCKS_OLD;
        }
        g_kept++;
    }
    c->rdi = (u32)next;
    return (u64)A(0x287d64);
}

/* Party slot assignment on spawn: slot = position in the party list. The
 * game's version handles positions 0-2; position 3 goes to slot 3 and uses
 * the mod's per-slot state block. */
u64 h_slotassign(Ctx *c)
{
    u32 idx = (u32)c->rax;
    u8 *ent = (u8 *)c->rdi;
    u32 slot;
    if (idx > 3 || ent[ENT_TYPE] != 3)
        return (u64)A(0x2913b1);
    if (idx == 3) {
        slot = 3;
        SLOT3() = ent;
        c->rbx = 3;
        memset(g_slot3, 0, 0x190);
        c->rcx = (u64)g_slot3;
    } else {
        slot = idx == 0 ? 0 : (PARTY(1) ? 2 : 1);
        PARTY(slot) = ent;
        c->rbx = slot;
        c->rcx = (u64)(A(RVA_SLOTSTATE) + slot * 0x190);
    }
    if (g_debug) {
        u8 *d = HGET(F32(ent, ENT_DESC_H));
        logf_("spawn party member char=%d list=%u slot=%d ent=%p", d ? F16(d, 0x4c) : -1, idx, slot, ent);
    }
    return (u64)A(0x2912b2);
}

/* Room-entry position of the extra member: the game has entrance positions
 * for Sora and the two friends only. Put the extra member between the two
 * friends, at friend 2's height and facing. */
u64 h_spawnpos(Ctx *c)
{
    float a[4], b[4];
    u8 *out;
    if ((u8 *)c->rdi != SLOT3())
        return 0;
    out = (u8 *)c->rbp - 0x19;
    FN(0x192170, void, float *)(a);
    FN(0x192260, void, float *)(b);
    FF(out, 0x0) = (a[0] + b[0]) * 0.5f;
    FF(out, 0x4) = b[1];
    FF(out, 0x8) = (a[2] + b[2]) * 0.5f;
    FF(out, 0xc) = b[3];
    return (u64)A(0x2917d0);
}

/* ----------------------------------------------------- event script helpers */

/* Event "clear targets" for slot 3 when the cleared entity is the extra member. */
u64 h_ev_cleartgt(Ctx *c)
{
    u8 *e = SLOT3();
    if (e && F32(e, ENT_ID) == *(u32 *)c->rdi)
        FN(0x1d2920, void, u8 *)(e);
    return 0;
}

u64 h_ev_aa190(Ctx *c)
{
    u8 *e = SLOT3();
    if (e) {
        u8 *o = (u8 *)c->rbx;
        FN(0x2a2e30, void, u8 *, u32)(e, F32(o, 0x198 + FS32(o, 0x194) * 4));
    }
    return 0;
}

u64 h_ev_aa230(Ctx *c)
{
    (void)c;
    if (SLOT3())
        FN(0x2a3280, void, u8 *)(SLOT3());
    return 0;
}

/* --------------------------------------------------------------- summons */

/* Summon end: entities other than the friends get their flags popped here;
 * the friends (now including slot 3) are restored by the game's own friend
 * loop, whose end was moved to cover slot 3. */
u64 h_summon_ex1(Ctx *c)
{
    g_summoning = 0;
    return (u64)A(is_friend_ent((u8 *)c->rbx) ? 0x2890b3 : 0x2890ab);
}

u64 h_summon_ex2(Ctx *c)
{
    g_summoning = 0;
    return (u64)A(is_friend_ent((u8 *)c->rbx) ? 0x2897c8 : 0x2897c0);
}

/* Summon start: hide the extra member like the other two, and fade out its
 * gauge immediately. */
u64 h_summon_start(Ctx *c)
{
    (void)c;
    g_summoning = 1;
    if (SLOT3())
        SLOT3()[3] |= 1;
    if (g_gauge_on)
        F64(g_gauge, 0xa9c) = 0;
    if (g_debug)
        logf_("summon start: extra member %p", SLOT3());
    return 0;
}

/* "Is this a friend" check used by the summon / party code. */
u64 h_isfriend(Ctx *c)
{
    return (u64)A(is_friend_ent((u8 *)c->rdi) ? 0x28e9b0 : 0x28e8b1);
}

u64 h_flag_bc730(Ctx *c)
{
    (void)c;
    if (SLOT3())
        F32(SLOT3(), 0) |= 0x1000080;
    return 0;
}

/* ------------------------------------------------------------ party menu */

/* Party menu: the game swaps the reserve member's body for the newly chosen
 * one (unload + load). When both are already spawned (one of them is the
 * extra member) nothing needs loading. */
u64 h_partyswap(Ctx *c)
{
    u8 newc = ((u8 *)c->rdi)[3];
    u8 oldc = PARTY_LIST(3);
    int have_new = 0;
    if (newc == oldc || !SLOT3())
        return 0;
    if (newc != 0xff)
        have_new = find_ent_by_char(newc) != NULL;
    if (oldc == 0xff)
        return 0;
    if (have_new && find_ent_by_char(oldc))
        return (u64)A(0x3a1642);
    return 0;
}
