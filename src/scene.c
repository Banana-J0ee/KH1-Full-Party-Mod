/* Cutscenes and scripted scenes.
 *
 * Event scripts only know about Sora and two friends. While a scene steers or
 * hides the other friends, the extra member is taken out of the scene (put
 * under event control and hidden); when the scene ends it is shown again next
 * to the friends, or behind Sora.
 *
 * Known limitation (unchanged in this version): the extra member and "the
 * others" are found by party-list position (list[3] vs list[1], list[2]) rather
 * than by body slot, so after reordering the party in the menu without leaving
 * the room the wrong character can be taken out of a scene. */
#include <windows.h>
#include "kh1party.h"

void *o_ev_orders;

static u8 *g_ev_ent;     /* extra member being managed */
static u8  g_ev_ctl;     /* we put it under event control */
static u8  g_ev_hid;     /* we hid it */

static u8 *active_by_list(int i)
{
    u8 *save = SAVE(), *e;
    if (!save || PARTY_LIST(i) == 0xff)
        return NULL;
    e = party_find_ent_by_char(PARTY_LIST(i));
    return ent_active(e) ? e : NULL;
}

static int char_of(u8 *e)
{
    u8 *d = HGET(F32(e, ENT_DESC_H));
    return d ? F16(d, 0x4c) : -1;
}

u64 ev_orders_hook(ARGS4)
{
    u64 r = CALL4(o_ev_orders);
    u8 *save = SAVE(), *x;
    int others_hidden = 0, others_steered = 0;

    if (!save || PARTY_LIST(3) == 0xff ||
        !(x = party_find_ent_by_char(PARTY_LIST(3))) || !ent_active(x) || x == PARTY(0)) {
        g_ev_ent = NULL;
        g_ev_ctl = g_ev_hid = 0;
        return r;
    }
    if (g_ev_ent != x) {
        g_ev_ent = x;
        g_ev_ctl = g_ev_hid = 0;
    }

    for (int i = 1; i <= 2; i++) {
        u8 *o;
        if (!SAVE() || PARTY_LIST(i) == 0xff)
            continue;
        o = party_find_ent_by_char(PARTY_LIST(i));
        if (!o || o == x || !ent_active(o))
            continue;
        if (!(F32(o, ENT_FLAGS) & 1))
            others_hidden = 1;
        if (F32(o, ENT_EVFLAGS) & 1)
            others_steered = 1;
    }

    if (others_hidden || others_steered) {
        if (!(F32(x, ENT_EVFLAGS) & 1)) {
            F32(x, ENT_EVFLAGS) |= 1;
            x[0] &= 0xfb;
            if (HGET(F32(x, ENT_AI_H)))
                FN(0x1cf300, void, u8 *)(x);
            FN(0x29b960, void, u8 *)(x);
            g_ev_ctl = 1;
            if (g_debug)
                logf_("scene: extra member (char %d) hidden (others %s)", char_of(x),
                      others_hidden ? "hidden" : "being steered");
        }
        if (F32(x, ENT_FLAGS) & 1) {
            F32(x, ENT_FLAGS) = (F32(x, ENT_FLAGS) & ~0x33u) | 0x30;
            g_ev_hid = 1;
        }
        return r;
    }

    if (g_ev_ctl) {
        g_ev_ctl = 0;
        x[ENT_EVFLAGS] &= 0xfe;
    }
    if (!g_ev_hid)
        return r;
    g_ev_hid = 0;
    if (F32(x, ENT_FLAGS) & 1)
        return r;
    F32(x, ENT_FLAGS) |= 3;
    {
        u8 *s = PARTY(0), *a = active_by_list(1), *b = active_by_list(2), *p, *q;
        float pos[4];
        int placed = 0;
        if (!ent_active(s)) {
            x[0] &= 0xcf;
            return r;
        }
        p = a ? a : b;
        q = (a && b) ? b : (p ? s : NULL);
        if (p && q) {
            float mx = (FF(p, 0x10) + FF(q, 0x10)) * 0.5f;
            float my = (FF(p, 0x14) + FF(q, 0x14)) * 0.5f;
            float mz = (FF(p, 0x18) + FF(q, 0x18)) * 0.5f;
            float dx = mx - FF(s, 0x10), dz = mz - FF(s, 0x18);
            if (dx * dx + dz * dz < 160000.0f) {
                pos[0] = mx; pos[1] = my; pos[2] = mz;
                placed = 1;
            }
        }
        if (!placed) {
            pos[0] = FF(s, 0x10) - FF(s, 0x30) * 70.0f;
            pos[1] = FF(s, 0x14);
            pos[2] = FF(s, 0x18) - FF(s, 0x38) * 70.0f;
        }
        pos[3] = FF(s, 0x3c);
        {
            u8 *st = HGET(F32(x, ENT_AI_H));
            if (st)
                FN(0x1d2530, void, u8 *, u8 *, float *)(x, st, pos);
            else
                FN(0x292660, void, u8 *, float *)(x, pos);
        }
        if (g_debug)
            logf_("scene over: extra member (char %d) placed at (%.0f %.0f %.0f)", char_of(x),
                  pos[0], pos[1], pos[2]);
    }
    return r;
}
