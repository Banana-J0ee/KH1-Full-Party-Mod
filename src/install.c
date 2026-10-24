/* Applies every patch and hook. If anything fails to apply, everything that
 * was applied is undone and the game runs unmodified. */
#include <windows.h>
#include "kh1party.h"

u8 *g_spmem, *g_sprec, *g_wslot, *g_voice, *g_slot3, *g_gauge, *g_lv,
   *g_menumtx, *g_itemwin, *g_w3buf;
u8 **g_cursor, **g_aisrc;

/* handlers (party.c, ai.c, hud.c, scene.c, fixes.c) */
u64 h_sp_release(Ctx *), h_sp_clrfa(Ctx *), h_sp_keep(Ctx *), h_w_init(Ctx *),
    h_w_clr6(Ctx *), h_w_seid(Ctx *), h_v_fee0(Ctx *), h_v_ff20(Ctx *), h_v_ffc0(Ctx *),
    h_party_init(Ctx *), h_friendfilter(Ctx *), h_slotassign(Ctx *), h_spawnpos(Ctx *),
    h_ev_cleartgt(Ctx *), h_ev_aa190(Ctx *), h_ev_aa230(Ctx *), h_summon_ex1(Ctx *),
    h_summon_ex2(Ctx *), h_summon_start(Ctx *), h_isfriend(Ctx *), h_flag_bc730(Ctx *),
    h_partyswap(Ctx *), h_lv_tail(Ctx *), h_hud_tail(Ctx *), h_gauge_rebuild(Ctx *),
    h_item_tail(Ctx *), h_menu_mtx(Ctx *), h_menu_x(Ctx *), h_pm_load(Ctx *);
void ai_tick_hook(void), ai_pre_hook(void), ai_post_hook(void), ai_init_hook(void),
     ai_destroy_hook(void), ai_reset_hook(void), ai_shutdown_hook(void), ai_start_hook(void),
     hud_fcd0_hook(void), hud_layout_hook(void), hud_create_hook(void), hud_flash_hook(void),
     hud_950_hook(void), hud_fade_hook(void), hud_970_hook(void), hud_9d0_hook(void),
     hud_pos_hook(void), lvlprompt_hook(void), lv_init_hook(void), lv_active_hook(void),
     lv_clear_hook(void), item_open_hook(void), item_close_hook(void), item_reset_hook(void),
     item_init_hook(void), item_task_hook(void), ev_orders_hook(void);
extern void *o_ai_tick, *o_ai_pre, *o_ai_post, *o_ai_init, *o_ai_destroy, *o_ai_reset,
     *o_ai_shutdown, *o_ai_start, *o_hud_fcd0, *o_hud_layout, *o_hud_create, *o_hud_flash,
     *o_hud_950, *o_hud_fade, *o_hud_970, *o_hud_9d0, *o_hud_pos, *o_lvlprompt, *o_lv_init,
     *o_lv_active, *o_lv_clear, *o_item_open, *o_item_close, *o_item_reset, *o_item_init,
     *o_item_task, *o_ev_orders;

#define HOOKF(name, handler, orig) \
    do { if (!((orig) = hook_fn(&H_##name, (void *)(handler)))) return 0; } while (0)
#define HOOKC(name, handler) \
    do { if (!hook_ctx(&H_##name, handler)) return 0; } while (0)
#define OK(x) do { if (!(x)) return 0; } while (0)

static int alloc_tables(void)
{
    g_spmem = near_alloc(SP_BLOCKS * SP_BLOCK + 0x1000);
    g_sprec = near_alloc(SP_RECS_ALLOC * SP_REC);
    g_wslot = near_alloc(0x100);
    g_voice = near_alloc(0xc8);
    g_cursor = near_alloc(0x10);
    g_aisrc = near_alloc(0x10);
    g_slot3 = near_alloc(0x190);
    g_gauge = near_alloc(0xdf0);
    g_lv = near_alloc(0x3a30);
    g_menumtx = near_alloc(0x100);
    g_itemwin = near_alloc(0x40);
    g_w3buf = VirtualAlloc(NULL, 0x20000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_spmem || !g_sprec || !g_wslot || !g_voice || !g_cursor || !g_aisrc || !g_slot3 ||
        !g_gauge || !g_lv || !g_menumtx || !g_itemwin || !g_w3buf)
        return 0;

    *g_cursor = A(0x2d372a0);
    for (int i = SP_BLOCKS_OLD; i < SP_RECS_ALLOC; i++)
        g_sprec[i * SP_REC] = 0xff;
    for (int i = 0; i < 4; i++)
        g_voice[i * 0x22] = 0xff;
    F32(g_wslot, 0x90) &= ~1u;
    F32(g_wslot, 0x9c) = 0x20000;
    *(u8 **)(g_wslot + 0xa0) = g_w3buf;
    return 1;
}

/* Model memory pool: 64 -> 80 blocks in mod memory. */
static int install_spmem(void)
{
    static const u32 sites[] = { 0x28594c, 0x285b80, 0x285db3, 0x285eaf, 0x285f2a,
                                 0x2862da, 0x2864e0, 0x29121c };
    for (size_t i = 0; i < sizeof sites / sizeof sites[0]; i++)
        OK(patch_riprel(sites[i], 0, RVA_SPMEM, g_spmem, "spmem"));
    OK(patch_riprel(0x285fa4, 0, RVA_SPMEM + 0x1c, g_spmem + 0x1c, "spmem"));
    OK(patch_riprel(0x285fab, 0, RVA_SPMEM + SP_BLOCKS_OLD * SP_BLOCK + 0x1c,
                    g_spmem + SP_BLOCKS * SP_BLOCK + 0x1c, "spmem end"));
    for (int i = 0; i < R_SPREC_N; i++)
        OK(patch_riprel(R_SPREC[i].site, R_SPREC[i].extra, RVA_SPREC + R_SPREC[i].off,
                        g_sprec + R_SPREC[i].off, "sprec"));
    OK(patch_u8(0x285dc0, 0x40, SP_BLOCKS, "sp reverse limit"));
    /* Second-file loader: find the owning record among all blocks (the
     * extra member's model is past block 64). */
    OK(patch_u8(0x28639e, 0x40, SP_BLOCKS, "sp second-file lookup"));
    HOOKC(sp_release, h_sp_release);
    HOOKC(sp_clrfa, h_sp_clrfa);
    HOOKC(sp_keep, h_sp_keep);
    return 1;
}

/* Weapon slots 3 -> 4. */
static int install_wslot(void)
{
    static const u32 counts[] = { 0x286816, 0x28698d, 0x286c5f, 0x286d99, 0x286f0b,
                                  0x287095, 0x287115, 0x28715b };
    for (int i = 0; i < R_WSLOT_N; i++)
        OK(patch_riprel(R_WSLOT[i].site, R_WSLOT[i].extra, RVA_WSLOT + R_WSLOT[i].off,
                        g_wslot + R_WSLOT[i].off, "wslot"));
    OK(patch_riprel(0x28721c, 0, RVA_WSLOT + 0xa0, g_wslot + 0xd0, "wslot end a"));
    OK(patch_riprel(0x2872be, 0, RVA_WSLOT + 0xa0, g_wslot + 0xd0, "wslot end b"));
    OK(patch_riprel(0x287382, 0, RVA_WSLOT + 0xa0, g_wslot + 0xd0, "wslot end c"));
    for (size_t i = 0; i < sizeof counts / sizeof counts[0]; i++)
        OK(patch_u8(counts[i], 3, 4, "wslot count"));
    OK(patch_u8(0x286c82, 2, 3, "wslot friend count a"));
    OK(patch_u8(0x287138, 2, 3, "wslot friend count b"));
    HOOKC(w_init, h_w_init);
    HOOKC(w_clr6a, h_w_clr6);
    HOOKC(w_clr6b, h_w_clr6);
    HOOKC(w_seid, h_w_seid);
    return 1;
}

/* Voice banks 3 -> 4. */
static int install_voice(void)
{
    for (int i = 0; i < R_VOICE_N; i++)
        OK(patch_riprel(R_VOICE[i].site, R_VOICE[i].extra, RVA_VOICE + R_VOICE[i].off,
                        g_voice + R_VOICE[i].off, "voice"));
    OK(patch_u8(0x29049f, 3, 4, "voice slots a"));
    OK(patch_u8(0x2906d1, 3, 4, "voice slots b"));
    OK(patch_u8(0x290593, 3, 4, "voice list count"));
    HOOKC(v_fee0, h_v_fee0);
    HOOKC(v_ff20, h_v_ff20);
    HOOKC(v_ffc0, h_v_ffc0);
    return 1;
}

/* Party array 3 -> 4: the entity allocator cursor that followed it moves to
 * mod memory, and loops over the party run to slot 3. */
static int install_party(void)
{
    static const u32 party_end[] = { 0x285b9c, 0x291e34, 0x2921c5, 0x2923d9, 0x2928a0, 0x2a2fc1,
                                     0x2a63a1, 0x2a7afa, 0x2ac12b, 0x2ad19b, 0x3a0c12 };
    static const u32 friend_end[] = { 0x289546, 0x289659, 0x3a17bd };
    static const struct { u32 rva; const char *name; } four[] = {
        { 0x2a3203, "find slot by char" }, { 0x2a32d6, "find ent by char" },
        { 0x2a4e7b, "stat writeback a" }, { 0x2a584c, "stat writeback b" },
        { 0x2c295b, "status effect all" }, { 0x29447e, "command menu targets" },
    };
    static const u8 dim_e[4] = { 0x66, 0x0f, 0x76, 0xe5 };   /* pcmpeqd xmm4,xmm5 */
    static const u8 dim_r[4] = { 0x66, 0x0f, 0xef, 0xe4 };   /* pxor xmm4,xmm4 */

    OK(patch_riprel(0x290de7, 0, 0x2d37298, g_cursor, "cursor rd"));
    OK(patch_riprel(0x290e47, 0, 0x2d37298, g_cursor, "cursor wr"));
    OK(patch_riprel(0x291f75, 0, 0x2d37298, g_cursor, "cursor init"));
    for (size_t i = 0; i < sizeof party_end / sizeof party_end[0]; i++)
        OK(patch_riprel(party_end[i], 0, 0x2d37298, A(0x2d372a0), "party end"));
    for (size_t i = 0; i < sizeof friend_end / sizeof friend_end[0]; i++)
        OK(patch_riprel(friend_end[i], 0, 0x2d37290, A(0x2d37298), "friend end"));
    for (size_t i = 0; i < sizeof four / sizeof four[0]; i++)
        OK(patch_u8(four[i].rva, 3, 4, four[i].name));
    OK(patch_bytes(0x2dc802, dim_e, dim_r, 4, "party menu dim (simd)"));
    OK(patch_u8(0x2dc8df, 3, 0x7f, "party menu dim (scalar)"));
    /* Equipment menu: it skips the "commit" step (0x304f30, which swaps the
     * weapon model on the character's body via 0x286720) for party list
     * position 3, because that is the reserve, who has no body in the
     * unmodded game. Let it run for position 3 too: the reserve member's
     * weapon model then changes as soon as the new weapon is confirmed. With
     * no body for that member, 0x286720 skips the model and just finishes. */
    OK(patch_u8(0x30581a, 3, 0x7f, "equip menu: weapon swap for position 3"));

    HOOKC(party_init, h_party_init);
    HOOKC(friendfilter, h_friendfilter);
    HOOKC(slotassign, h_slotassign);
    HOOKC(spawnpos, h_spawnpos);
    HOOKC(ev_cleartgt, h_ev_cleartgt);
    HOOKC(ev_aa190, h_ev_aa190);
    HOOKC(ev_aa230, h_ev_aa230);
    HOOKC(summon_ex1, h_summon_ex1);
    HOOKC(summon_ex2, h_summon_ex2);
    HOOKC(summon_start, h_summon_start);
    HOOKC(isfriend, h_isfriend);
    HOOKC(flag_bc730, h_flag_bc730);
    HOOKC(partyswap, h_partyswap);
    return 1;
}

static int install_ai(void)
{
    static const u32 src1[] = { 0x1d353a, 0x1d354a, 0x1d356a };
    static const u32 src2[] = { 0x1d3578, 0x1d3588, 0x1d35a8 };
    for (int i = 0; i < 3; i++) {
        OK(patch_riprel(src1[i], 0, 0x2d37288, &g_aisrc[0], "ai src1"));
        OK(patch_riprel(src2[i], 0, 0x2d37290, &g_aisrc[1], "ai src2"));
    }
    HOOKF(ai_tick, ai_tick_hook, o_ai_tick);
    HOOKF(ai_pre, ai_pre_hook, o_ai_pre);
    HOOKF(ai_post, ai_post_hook, o_ai_post);
    HOOKF(ai_init, ai_init_hook, o_ai_init);
    HOOKF(ai_destroy, ai_destroy_hook, o_ai_destroy);
    HOOKF(ai_reset, ai_reset_hook, o_ai_reset);
    HOOKF(ai_shutdown, ai_shutdown_hook, o_ai_shutdown);
    HOOKF(ai_start, ai_start_hook, o_ai_start);
    OK(ai_heal_install());
    return 1;
}

static int install_hud(void)
{
    HOOKF(hud_fcd0, hud_fcd0_hook, o_hud_fcd0);
    HOOKF(hud_layout, hud_layout_hook, o_hud_layout);
    HOOKF(hud_create, hud_create_hook, o_hud_create);
    HOOKF(hud_flash, hud_flash_hook, o_hud_flash);
    HOOKF(hud_950, hud_950_hook, o_hud_950);
    HOOKF(hud_fade, hud_fade_hook, o_hud_fade);
    HOOKF(hud_970, hud_970_hook, o_hud_970);
    HOOKF(hud_9d0, hud_9d0_hook, o_hud_9d0);
    HOOKF(hud_pos, hud_pos_hook, o_hud_pos);
    HOOKF(lvlprompt, lvlprompt_hook, o_lvlprompt);
    HOOKF(lv_init, lv_init_hook, o_lv_init);
    HOOKF(lv_active, lv_active_hook, o_lv_active);
    HOOKF(lv_clear, lv_clear_hook, o_lv_clear);
    HOOKC(lv_tail, h_lv_tail);
    HOOKC(hud_tail, h_hud_tail);
    HOOKC(gauge_rebuild, h_gauge_rebuild);
    HOOKF(item_open, item_open_hook, o_item_open);
    HOOKF(item_close, item_close_hook, o_item_close);
    HOOKF(item_reset, item_reset_hook, o_item_reset);
    HOOKF(item_init, item_init_hook, o_item_init);
    HOOKF(item_task, item_task_hook, o_item_task);
    HOOKC(item_tail, h_item_tail);
    HOOKF(ev_orders, ev_orders_hook, o_ev_orders);

    /* Party menu: four figures. */
    memcpy(g_menumtx, A(0x52abb0), 0x100);
    OK(patch_u8(0x2d980e, 3, 4, "menu figure count"));
    OK(patch_nop_riprel(0x2d981f, 7, 3, 0x2e20238, "menu figure 3 keep"));
    HOOKC(menu_mtx, h_menu_mtx);
    HOOKC(menu_x, h_menu_x);
    HOOKC(pm_load, h_pm_load);
    return 1;
}

int mod_install(void)
{
    g_patch_errors = 0;
    if (!version_detect() || !alloc_tables() ||
        !install_spmem() || !install_wslot() || !install_voice() || !install_party() ||
        !install_ai() || !install_hud() || g_patch_errors || g_unmapped) {
        logf_("install failed (%d errors, %d untranslated addresses): all changes undone, "
              "the game runs unmodified", g_patch_errors, g_unmapped);
        patches_rollback();
        return 0;
    }
    return 1;
}
