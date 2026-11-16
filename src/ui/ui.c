/* ui.c — immediate-mode widgets drawn from external GUI textures. */
#include "wl/ui.h"
#include "wl/config.h"
#include "wl/block.h"
#include "wl/item.h"
#include "wl/tiles.h"
#include "wl/log.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "rlgl.h"

void wl_ui_begin(WlUi *ui, WlAssets *assets, const WlConfig *cfg)
{
    ui->assets = assets;
    ui->scale = cfg ? cfg->ui_scale : 1.0f;
    ui->mouse = GetMousePosition();
    ui->mouse_down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    ui->mouse_pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    ui->mouse_released = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    ui->mouse_right_pressed = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    ui->wheel = (int)GetMouseWheelMove();
    ui->hot = 0;
    ui->next_id = 1;
    ui->any_hover = false;

    ui->char_count = 0;
    int c = GetCharPressed();
    while (c > 0 && ui->char_count < (int)sizeof(ui->chars) - 1) {
        if (c >= 32 && c < 127) ui->chars[ui->char_count++] = (char)c;
        c = GetCharPressed();
    }
    if (IsKeyPressed(KEY_ESCAPE) || (IsWindowFocused() == false))
        ui->focus_textfield = 0;
}

void wl_ui_end(WlUi *ui)
{
    if (ui->mouse_pressed && ui->focus_textfield != 0 && ui->hot != ui->focus_textfield)
        ui->focus_textfield = 0;
    ui->active = ui->active; /* no-op; kept for clarity */
}

Font wl_ui_font(const WlUi *ui) { return wl_assets_font(ui->assets, WL_FONT_UI); }
Font wl_ui_title_font(const WlUi *ui) { return wl_assets_font(ui->assets, WL_FONT_TITLE); }

Rectangle wl_ui_rect(const WlUi *ui, float x, float y, float w, float h)
{
    return (Rectangle){ x * ui->scale, y * ui->scale, w * ui->scale, h * ui->scale };
}

Rectangle wl_ui_centered(const WlUi *ui, float w, float h, float y_frac)
{
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    return (Rectangle){
        (sw - w * ui->scale) / 2.0f,
        sh * y_frac - (h * ui->scale) / 2.0f,
        w * ui->scale, h * ui->scale
    };
}

Vector2 wl_ui_text_size(const WlUi *ui, const char *text, float size, bool title)
{
    Font f = title ? wl_ui_title_font(ui) : wl_ui_font(ui);
    return MeasureTextEx(f, text, size * ui->scale, title ? 2.0f : 1.0f);
}

/* ------------------------------------------------------------------ */
/* drawing helpers                                                     */
/* ------------------------------------------------------------------ */

static void draw_tex_stretched(Texture2D tex, Rectangle r, Color tint)
{
    Rectangle src = { 0, 0, (float)tex.width, (float)tex.height };
    DrawTexturePro(tex, src, r, (Vector2){ 0, 0 }, 0.0f, tint);
}

static bool mouse_over(Rectangle r)
{
    return CheckCollisionPointRec(GetMousePosition(), r);
}

/* ------------------------------------------------------------------ */
/* widgets                                                             */
/* ------------------------------------------------------------------ */

bool wl_ui_button(WlUi *ui, Rectangle r, const char *label)
{
    int id = ui->next_id++;
    bool hover = mouse_over(r);
    bool held = hover && ui->mouse_down;
    bool pressed = hover && ui->mouse_pressed;

    if (hover) {
        ui->hot = id;
        ui->any_hover = true;
    }
    if (pressed) ui->active = id;

    Texture tex = wl_assets_tex(ui->assets, WL_TEX_GUI_BUTTON);
    draw_tex_stretched(tex, r, WHITE);
    if (hover) DrawRectangleRec(r, (Color){ 255, 255, 255, held ? 45 : 26 });
    DrawRectangleLinesEx(r, 1.0f, hover ? (Color){ 230, 230, 180, 220 }
                                        : (Color){ 20, 22, 26, 160 });

    Font f = wl_ui_font(ui);
    float fs = 24.0f * ui->scale;
    Vector2 ts = MeasureTextEx(f, label, fs, 1.0f);
    Vector2 pos = { r.x + (r.width - ts.x) / 2, r.y + (r.height - ts.y) / 2 };
    DrawTextEx(f, label, pos, fs, 1.0f, hover ? RAYWHITE : (Color){ 212, 216, 222, 255 });
    return pressed;
}

bool wl_ui_slider(WlUi *ui, Rectangle r, float *value, float vmin, float vmax,
                  float step)
{
    int id = ui->next_id++;
    bool changed = false;
    bool hover = mouse_over(r);
    if (hover) { ui->hot = id; ui->any_hover = true; }

    float track_y = r.y + r.height / 2 - 3 * ui->scale;
    Rectangle track = { r.x, track_y, r.width, 6 * ui->scale };
    DrawRectangleRec(track, (Color){ 22, 24, 28, 255 });
    DrawRectangleLinesEx(track, 1.0f, (Color){ 80, 86, 96, 255 });

    float t = (*value - vmin) / (vmax - vmin);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float knob_w = 14 * ui->scale;
    float knob_x = r.x + t * (r.width - knob_w);
    Rectangle knob = { knob_x, r.y + r.height / 2 - 10 * ui->scale,
                       knob_w, 20 * ui->scale };
    DrawRectangleRec(knob, (hover || ui->active == id) ? (Color){ 230, 226, 170, 255 }
                                                       : (Color){ 150, 156, 166, 255 });
    DrawRectangleLinesEx(knob, 1.0f, (Color){ 30, 32, 36, 255 });

    if (hover && ui->mouse_down) {
        ui->active = id;
        float nt = (GetMousePosition().x - r.x) / r.width;
        if (nt < 0) nt = 0;
        if (nt > 1) nt = 1;
        float nv = vmin + nt * (vmax - vmin);
        if (step > 0) nv = vmin + roundf((nv - vmin) / step) * step;
        if (nv < vmin) nv = vmin;
        if (nv > vmax) nv = vmax;
        if (nv != *value) {
            *value = nv;
            changed = true;
        }
    }
    return changed;
}

bool wl_ui_slider_i(WlUi *ui, Rectangle r, int *value, int vmin, int vmax, int step)
{
    float f = (float)*value;
    bool changed = wl_ui_slider(ui, r, &f, (float)vmin, (float)vmax, (float)step);
    *value = (int)f;
    return changed;
}

bool wl_ui_toggle(WlUi *ui, Rectangle r, bool *value)
{
    bool pressed = wl_ui_button(ui, r, *value ? "ON" : "OFF");
    if (pressed) *value = !*value;
    return pressed;
}

bool wl_ui_dropdown(WlUi *ui, Rectangle r, int *index, const char **options,
                    int count)
{
    int id = ui->next_id++;
    bool changed = false;
    bool hover = mouse_over(r);
    if (hover) { ui->hot = id; ui->any_hover = true; }

    bool open = (ui->active == id);
    if (hover && ui->mouse_pressed) {
        open = !open;
        ui->active = open ? id : 0;
    } else if (open && ui->mouse_pressed && !hover) {
        open = false;
        ui->active = 0;
    }

    Texture tex = wl_assets_tex(ui->assets, WL_TEX_GUI_BUTTON);
    draw_tex_stretched(tex, r, WHITE);
    if (hover) DrawRectangleRec(r, (Color){ 255, 255, 255, 22 });

    Font f = wl_ui_font(ui);
    float fs = 22.0f * ui->scale;
    const char *cur = (*index >= 0 && *index < count) ? options[*index] : "?";
    Vector2 ts = MeasureTextEx(f, cur, fs, 1.0f);
    DrawTextEx(f, cur, (Vector2){ r.x + 12 * ui->scale,
                                  r.y + (r.height - ts.y) / 2 }, fs, 1.0f,
               (Color){ 220, 224, 230, 255 });
    DrawTextEx(f, open ? "-" : "+", (Vector2){ r.x + r.width - 26 * ui->scale,
                                               r.y + (r.height - ts.y) / 2 },
               fs, 1.0f, (Color){ 180, 186, 196, 255 });

    if (open) {
        float row_h = 32.0f * ui->scale;
        Rectangle list = { r.x, r.y + r.height + 2, r.width, row_h * count };
        wl_ui_panel(ui, list);
        for (int i = 0; i < count; i++) {
            Rectangle row = { r.x, r.y + r.height + 2 + row_h * i, r.width, row_h };
            bool rh = mouse_over(row);
            if (rh) {
                DrawRectangleRec(row, (Color){ 255, 255, 255, 26 });
                if (ui->mouse_pressed) {
                    *index = i;
                    changed = true;
                    open = false;
                    ui->active = 0;
                }
            }
            Color c = (i == *index) ? (Color){ 235, 232, 180, 255 }
                                    : (Color){ 214, 218, 224, 255 };
            DrawTextEx(f, options[i], (Vector2){ row.x + 12 * ui->scale,
                                                  row.y + (row_h - fs) / 2 },
                       fs, 1.0f, c);
        }
    }
    return changed;
}

bool wl_ui_textfield(WlUi *ui, int id, Rectangle r, char *buf, size_t cap)
{
    bool activated = false;
    bool hover = mouse_over(r);
    if (hover) ui->any_hover = true;
    if (hover && ui->mouse_pressed) {
        ui->focus_textfield = id;
        activated = true;
    }
    bool focused = (ui->focus_textfield == id);

    /* inset background */
    DrawRectangleRec(r, (Color){ 18, 20, 24, 235 });
    DrawLineEx((Vector2){ r.x, r.y + r.height }, (Vector2){ r.x + r.width, r.y + r.height },
               2.0f * ui->scale, focused ? (Color){ 230, 226, 170, 255 }
                                          : (Color){ 90, 96, 106, 255 });

    Font f = wl_ui_font(ui);
    float fs = 24.0f * ui->scale;
    if (buf[0] == '\0' && !focused) {
        DrawTextEx(f, "...", (Vector2){ r.x + 10, r.y + (r.height - fs) / 2 },
                   fs, 1.0f, (Color){ 120, 126, 136, 255 });
    } else {
        DrawTextEx(f, buf, (Vector2){ r.x + 10, r.y + (r.height - fs) / 2 },
                   fs, 1.0f, (Color){ 232, 236, 240, 255 });
    }
    if (focused) {
        Vector2 ts = MeasureTextEx(f, buf, fs, 1.0f);
        float blink = fmodf((float)GetTime(), 1.0f) < 0.55f ? 1.0f : 0.0f;
        if (blink > 0.0f)
            DrawRectangle((int)(r.x + 12 + ts.x), (int)(r.y + r.height * 0.22f),
                          (int)(2 * ui->scale), (int)(r.height * 0.56f),
                          (Color){ 235, 232, 180, 255 });
    }

    if (focused) {
        if (IsKeyPressed(KEY_BACKSPACE) && buf[0] != '\0')
            buf[strlen(buf) - 1] = '\0';
        if (ui->char_count > 0 && strlen(buf) + (size_t)ui->char_count < cap)
            for (int i = 0; i < ui->char_count; i++)
                buf[strlen(buf)] = ui->chars[i]; /* append (chars validated ASCII) */
        if (IsKeyPressed(KEY_ENTER)) {
            ui->focus_textfield = 0;
            activated = true;
        }
    }
    return activated;
}

void wl_ui_label(WlUi *ui, Rectangle r, const char *text, Color color, bool title)
{
    Font f = title ? wl_ui_title_font(ui) : wl_ui_font(ui);
    float fs = (title ? 18.0f : 24.0f) * ui->scale;
    DrawTextEx(f, text, (Vector2){ r.x, r.y + (r.height - fs) / 2 }, fs,
               title ? 2.0f : 1.0f, color);
}

void wl_ui_label_centered(WlUi *ui, Rectangle r, const char *text, Color color,
                          bool title)
{
    Font f = title ? wl_ui_title_font(ui) : wl_ui_font(ui);
    float fs = (title ? 18.0f : 24.0f) * ui->scale;
    Vector2 ts = MeasureTextEx(f, text, fs, title ? 2.0f : 1.0f);
    DrawTextEx(f, text, (Vector2){ r.x + (r.width - ts.x) / 2,
                                   r.y + (r.height - ts.y) / 2 },
               fs, title ? 2.0f : 1.0f, color);
}

void wl_ui_panel(WlUi *ui, Rectangle r)
{
    Texture tex = wl_assets_tex(ui->assets, WL_TEX_GUI_PANEL);
    draw_tex_stretched(tex, r, WHITE);
}

void wl_ui_dim(float alpha)
{
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                  (Color){ 0, 0, 0, (unsigned char)(alpha * 255.0f) });
}

bool wl_ui_tabbar(WlUi *ui, Rectangle r, const char **tabs, int count, int *active)
{
    bool changed = false;
    float tab_w = r.width / count;
    for (int i = 0; i < count; i++) {
        Rectangle tr = { r.x + tab_w * i, r.y, tab_w - 2, r.height };
        bool hover = mouse_over(tr);
        if (hover) ui->any_hover = true;
        bool on = (i == *active);
        DrawRectangleRec(tr, on ? (Color){ 52, 58, 66, 255 }
                                : (Color){ 30, 33, 38, 255 });
        if (hover) DrawRectangleRec(tr, (Color){ 255, 255, 255, 18 });
        DrawRectangleLinesEx(tr, 1.0f, on ? (Color){ 220, 216, 160, 200 }
                                          : (Color){ 60, 64, 72, 255 });
        wl_ui_label_centered(ui, tr, tabs[i],
                             on ? (Color){ 240, 238, 210, 255 }
                                : (Color){ 170, 176, 186, 255 }, false);
        if (hover && ui->mouse_pressed && !on) {
            *active = i;
            changed = true;
        }
    }
    return changed;
}

void wl_ui_tooltip(WlUi *ui, const char *text)
{
    Font f = wl_ui_font(ui);
    float fs = 20.0f * ui->scale;
    Vector2 ts = MeasureTextEx(f, text, fs, 1.0f);
    Rectangle r = { ui->mouse.x + 14, ui->mouse.y + 10, ts.x + 16, ts.y + 10 };
    if (r.x + r.width > GetScreenWidth())
        r.x = GetScreenWidth() - r.width - 4;
    DrawRectangleRec(r, (Color){ 12, 14, 18, 240 });
    DrawRectangleLinesEx(r, 1.0f, (Color){ 90, 96, 106, 255 });
    DrawTextEx(f, text, (Vector2){ r.x + 8, r.y + 5 }, fs, 1.0f, RAYWHITE);
}

/* ------------------------------------------------------------------ */
/* item icons                                                          */
/* ------------------------------------------------------------------ */

/* one skewed quad from the atlas: p0..p3 (BL, BR, TR, TL), tile uv, shade */
static void icon_quad(Texture2D atlas, int tile, const Vector2 p[4], float shade)
{
    int tc = tile % WL_ATLAS_GRID, tr = tile / WL_ATLAS_GRID;
    float s = 1.0f / WL_ATLAS_GRID;
    float u0 = tc * s, u1 = (tc + 1) * s;
    float v0 = tr * s, v1 = (tr + 1) * s;
    unsigned char c = (unsigned char)(shade * 255.0f);

    rlSetTexture(atlas.id);
    rlBegin(RL_QUADS);
    rlColor4ub(c, c, c, 255);
    rlTexCoord2f(u0, v1); rlVertex2f(p[0].x, p[0].y);
    rlTexCoord2f(u1, v1); rlVertex2f(p[1].x, p[1].y);
    rlTexCoord2f(u1, v0); rlVertex2f(p[2].x, p[2].y);
    rlTexCoord2f(u0, v0); rlVertex2f(p[3].x, p[3].y);
    rlEnd();
    rlSetTexture(0);
}

void wl_ui_draw_item_icon(WlAssets *assets, WlItemId item_id, Rectangle r)
{
    const WlItem *item = wl_item_get(item_id);
    if (!item) return;
    Texture atlas = wl_assets_tex(assets, WL_TEX_ATLAS);

    if (item->is_block) {
        const WlBlock *blk = wl_block_get(item->block_id);
        if (!blk) return;

        if (blk->shape == WL_RENDER_CROSS) {
            /* flat sprite for plants */
            int tc = blk->tex[0] % WL_ATLAS_GRID, tr = blk->tex[0] / WL_ATLAS_GRID;
            Rectangle src = { tc * 16.0f, tr * 16.0f, 16, 16 };
            DrawTexturePro(atlas, src, r, (Vector2){ 0, 0 }, 0.0f, WHITE);
            return;
        }

        float x = r.x, y = r.y, s = r.width;
        Vector2 top[4] = {
            { x + s * 0.50f, y },            { x + s, y + s * 0.25f },
            { x + s * 0.50f, y + s * 0.50f }, { x, y + s * 0.25f },
        };
        Vector2 left[4] = {
            { x, y + s * 0.25f },            { x + s * 0.50f, y + s * 0.50f },
            { x + s * 0.50f, y + s },        { x, y + s * 0.75f },
        };
        Vector2 right[4] = {
            { x + s * 0.50f, y + s * 0.50f }, { x + s, y + s * 0.25f },
            { x + s, y + s * 0.75f },        { x + s * 0.50f, y + s },
        };
        icon_quad(atlas, blk->tex[2], top, 1.00f);   /* +Y */
        icon_quad(atlas, blk->tex[1], left, 0.62f);  /* -X */
        icon_quad(atlas, blk->tex[4], right, 0.80f); /* +Z */
        return;
    }

    int tc = item->tile % WL_ATLAS_GRID, tr = item->tile / WL_ATLAS_GRID;
    Rectangle src = { tc * 16.0f, tr * 16.0f, 16, 16 };
    DrawTexturePro(atlas, src, r, (Vector2){ 0, 0 }, 0.0f, WHITE);
}

void wl_ui_draw_slot_item(WlUi *ui, Rectangle r, const WlSlot *slot)
{
    Texture slot_tex = wl_assets_tex(ui->assets, WL_TEX_GUI_SLOT);
    draw_tex_stretched(slot_tex, r, WHITE);

    if (slot->item == WL_ITEM_NONE) return;

    const WlItem *item = wl_item_get(slot->item);
    if (!item) return;

    Rectangle icon = { r.x + r.width * 0.14f, r.y + r.height * 0.12f,
                       r.width * 0.72f, r.height * 0.72f };
    wl_ui_draw_item_icon(ui->assets, slot->item, icon);

    Font f = wl_ui_font(ui);
    if (slot->count > 1) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", slot->count);
        float fs = 18.0f * ui->scale;
        Vector2 ts = MeasureTextEx(f, buf, fs, 1.0f);
        DrawTextEx(f, buf, (Vector2){ r.x + r.width - ts.x - 5, r.y + r.height - ts.y - 3 },
                   fs, 1.0f, RAYWHITE);
    }
    /* durability bar for damaged tools */
    if (item->durability > 0 && slot->dur > 0 && slot->dur < item->durability) {
        float frac = (float)slot->dur / (float)item->durability;
        float bw = r.width * 0.72f;
        Rectangle bar_bg = { r.x + r.width * 0.14f, r.y + r.height - 7, bw, 4 };
        DrawRectangleRec(bar_bg, (Color){ 20, 20, 20, 200 });
        Color col = frac > 0.5f ? (Color){ 90, 200, 90, 255 }
                  : frac > 0.25f ? (Color){ 220, 190, 60, 255 }
                                 : (Color){ 220, 70, 60, 255 };
        DrawRectangle((int)bar_bg.x, (int)bar_bg.y, (int)(bw * frac), 4, col);
    }
}
