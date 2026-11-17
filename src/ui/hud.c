/* hud.c — crosshair, hotbar, health/hunger/air, debug overlay, death screen. */
#include "wl/hud.h"
#include "wl/game.h"
#include "wl/block.h"
#include "wl/item.h"
#include "wl/tiles.h"
#include "wl/util.h"

#include <stdio.h>
#include <math.h>

static void draw_crosshair(WlGame *g)
{
    Texture ch = wl_assets_tex(g->assets, WL_TEX_GUI_CROSSHAIR);
    DrawTexture(ch, GetScreenWidth() / 2 - ch.width / 2,
                GetScreenHeight() / 2 - ch.height / 2, WHITE);
}

static void draw_hotbar(WlGame *g)
{
    WlUi *ui = &g->ui;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    float slot_w = 44.0f * ui->scale;
    float hb_w = slot_w * WL_HOTBAR_SIZE;
    float hb_x = (sw - hb_w) / 2.0f;
    float hb_y = sh - slot_w - 12.0f * ui->scale;

    /* held item name toast */
    if (g->item_toast > 0.0f && g->item_toast_name[0] != '\0') {
        float a = g->item_toast < 0.5f ? g->item_toast / 0.5f : 1.0f;
        Font f = wl_ui_font(ui);
        float fs = 24.0f * ui->scale;
        Vector2 ts = MeasureTextEx(f, g->item_toast_name, fs, 1.0f);
        Color c = { 255, 255, 255, (unsigned char)(a * 255) };
        DrawTextEx(f, g->item_toast_name,
                   (Vector2){ (sw - ts.x) / 2, hb_y - 42 * ui->scale }, fs, 1.0f, c);
    }

    Texture sel = wl_assets_tex(g->assets, WL_TEX_GUI_SLOT_SEL);
    for (int i = 0; i < WL_HOTBAR_SIZE; i++) {
        Rectangle r = { hb_x + i * slot_w, hb_y, slot_w - 2 * ui->scale,
                        slot_w - 2 * ui->scale };
        wl_ui_draw_slot_item(ui, r, &g->player.inv.slots[i]);
        if (i == g->player.selected_slot) {
            Rectangle s = { r.x - 3 * ui->scale, r.y - 3 * ui->scale,
                            r.width + 6 * ui->scale, r.height + 6 * ui->scale };
            Rectangle src = { 0, 0, (float)sel.width, (float)sel.height };
            DrawTexturePro(sel, src, s, (Vector2){ 0, 0 }, 0.0f, WHITE);
        }
    }
}

static void draw_survival_stats(WlGame *g)
{
    if (wl_world_gamemode(g->world) != WL_WORLD_GAMEMODE_SURVIVAL) return;

    WlUi *ui = &g->ui;
    WlPlayer *p = &g->player;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    float slot_w = 44.0f * ui->scale;
    float hb_x = (sw - slot_w * WL_HOTBAR_SIZE) / 2.0f;
    float hb_y = sh - slot_w - 12.0f * ui->scale;
    float icon = 16.0f * ui->scale;
    float gap = 2.0f * ui->scale;
    float row_y = hb_y - icon - 8 * ui->scale;
    float hunger_y = row_y - icon - 5 * ui->scale; /* own row: no overlap */

    Texture heart = wl_assets_tex(g->assets, WL_TEX_GUI_HEART);
    Texture heart_e = wl_assets_tex(g->assets, WL_TEX_GUI_HEART_EMPTY);
    Texture hunger = wl_assets_tex(g->assets, WL_TEX_GUI_HUNGER);
    Texture hunger_e = wl_assets_tex(g->assets, WL_TEX_GUI_HUNGER_EMPTY);

    /* hearts: 10 icons, 2 health points each (bottom row, left side) */
    for (int i = 0; i < 10; i++) {
        float x = hb_x + i * (icon + gap);
        Rectangle dst = { x, row_y, icon, icon };
        Rectangle src_full = { 0, 0, (float)heart.width, (float)heart.height };
        DrawTexturePro(heart_e, src_full, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
        float hp = p->health - i * 2.0f;
        if (hp >= 2.0f) {
            DrawTexturePro(heart, src_full, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
        } else if (hp > 0.0f) {
            Rectangle half = { 0, 0, (float)heart.width / 2, (float)heart.height };
            DrawTexturePro(heart, half, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
        }
    }

    /* hunger: 10 icons on its OWN row above the hearts, right-aligned
     * to the hotbar's right edge — never touching the hearts at any
     * UI scale or resolution */
    float hb_r = hb_x + slot_w * WL_HOTBAR_SIZE;
    for (int i = 0; i < 10; i++) {
        float x = hb_r - icon - i * (icon + gap);
        Rectangle dst = { x, hunger_y, icon, icon };
        Rectangle src_full = { 0, 0, (float)hunger.width, (float)hunger.height };
        DrawTexturePro(hunger_e, src_full, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
        float hu = p->hunger - i * 2.0f;
        if (hu >= 2.0f) {
            DrawTexturePro(hunger, src_full, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
        } else if (hu > 0.0f) {
            Rectangle half = { 0, 0, (float)hunger.width / 2, (float)hunger.height };
            DrawTexturePro(hunger, half, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
        }
    }

    /* air bubbles while underwater */
    if (p->head_in_water || p->air < 10.0f) {
        int bubbles = (int)ceilf(p->air / 2.0f);
        for (int i = 0; i < bubbles && i < 10; i++) {
            float x = hb_x + i * (icon + 2 * ui->scale);
            DrawCircle((int)(x + icon / 2), (int)(hunger_y),
                       icon * 0.42f, (Color){ 70, 130, 210, 230 });
            DrawCircleLines((int)(x + icon / 2), (int)(hunger_y),
                            icon * 0.42f, (Color){ 30, 60, 110, 255 });
        }
    }
}

static void draw_debug(WlGame *g)
{
    WlUi *ui = &g->ui;
    WlPlayer *p = &g->player;
    Font f = wl_ui_font(ui);
    float fs = 20.0f * ui->scale;

    const WlWorldGen *gen = wl_world_gen(g->world);
    WlWorldStats st = wl_world_stats(g->world);
    const char *lines[10];
    int n = 0;
    lines[n++] = TextFormat("Wildlands %s  |  %.0f fps  |  %.1f ms", WL_VERSION_STRING,
                            GetFPS(), GetFrameTime() * 1000.0f);
    lines[n++] = TextFormat("XYZ %.2f / %.2f / %.2f  (pitch %.0f deg)",
                            p->pos.x, p->pos.y, p->pos.z, p->pitch * 57.2958f);
    lines[n++] = TextFormat("chunk %d,%d  biome %s  time %u/24000",
                            (int)floorf(p->pos.x) >> 4, (int)floorf(p->pos.z) >> 4,
                            wl_biome_name(wl_worldgen_biome_at(
                                gen, (int)floorf(p->pos.x), (int)floorf(p->pos.z))),
                            wl_world_time(g->world));
    lines[n++] = TextFormat("seed %llu  gamemode %s",
                            (unsigned long long)gen->seed,
                            wl_world_gamemode(g->world) == WL_WORLD_GAMEMODE_CREATIVE
                                ? "creative" : "survival");
    lines[n++] = TextFormat("chunks %d loaded / %d visible / %d drawn  |  %ld tris",
                            st.loaded, g->rend.visible_chunks, g->rend.drawn_chunks,
                            g->rend.drawn_tris);
    lines[n++] = TextFormat("gen queue %d  pending %d", st.queued_gen,
                            wl_world_pending(g->world, p->pos.x, p->pos.z));
    lines[n++] = TextFormat("%.2f ms avg | worst %.2f ms (1%% low %.0f fps) | rss %d MiB",
                            g->ft_ema_ms, g->ft_worst_ms,
                            g->ft_worst_ms > 0.01f ? 1000.0f / g->ft_worst_ms : 0.0f,
                            wl_util_rss_mb());
    lines[n++] = TextFormat("chunk gen: %d chunks, %.2f ms avg | mesh: %.2f ms avg, %.2f last",
                            st.gen_count,
                            st.gen_count > 0 ? st.gen_ms_total / (double)st.gen_count : 0.0,
                            st.mesh_ms_ema, st.mesh_ms_last);
    if (g->gpu_info[0] != '\0')
        lines[n++] = TextFormat("gpu %s", g->gpu_info);

    for (int i = 0; i < n; i++)
        DrawTextEx(f, lines[i], (Vector2){ 8, 8 + i * (fs + 4) }, fs, 1.0f,
                   (Color){ 235, 235, 235, 240 });
}

static void draw_death_screen(WlGame *g)
{
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    wl_ui_dim(0.55f);
    DrawRectangle(0, 0, sw, sh, (Color){ 120, 0, 0, 60 });

    WlUi *ui = &g->ui;
    Font tf = wl_ui_title_font(ui);
    const char *msg = "You died";
    float fs = 36.0f * ui->scale;
    Vector2 ts = MeasureTextEx(tf, msg, fs, 2.0f);
    DrawTextEx(tf, msg, (Vector2){ (sw - ts.x) / 2, sh * 0.34f }, fs, 2.0f, RAYWHITE);

    Font f = wl_ui_font(ui);
    const char *msg2 = "Press R to respawn";
    float fs2 = 26.0f * ui->scale;
    Vector2 ts2 = MeasureTextEx(f, msg2, fs2, 1.0f);
    DrawTextEx(f, msg2, (Vector2){ (sw - ts2.x) / 2, sh * 0.34f + fs + 24 }, fs2, 1.0f,
               (Color){ 220, 220, 220, 255 });
}

void wl_hud_draw(WlGame *g)
{
    if (g->overlay != WL_OVERLAY_INVENTORY && g->overlay != WL_OVERLAY_CRAFTING)
        draw_crosshair(g);

    draw_hotbar(g);
    draw_survival_stats(g);

    if (g->player.damage_flash > 0.0f) {
        float a = (float)(g->player.damage_flash / 0.45) * 110.0f;
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                      (Color){ 200, 30, 30, (unsigned char)a });
    }

    if (g->cfg.show_debug_info) draw_debug(g);
    if (g->overlay == WL_OVERLAY_DEATH || g->player.dead) draw_death_screen(g);
}
