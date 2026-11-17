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

/* ------------------------------------------------------------------ */
/* test seam (--uitest): replaces physical mouse/keyboard input         */
/* ------------------------------------------------------------------ */

static bool s_dbg_active = false;
static Vector2 s_dbg_pos;
static bool s_dbg_left_down = false, s_dbg_left_pressed = false;
static int s_dbg_wheel = 0;
static char s_dbg_chars[16];
static int s_dbg_char_count = 0;

void wl_ui_debug_input(bool enable, float x, float y, bool left_down,
                       bool left_pressed, int wheel, const char *chars)
{
    s_dbg_active = enable;
    s_dbg_pos = (Vector2){ x, y };
    s_dbg_left_down = left_down;
    s_dbg_left_pressed = left_pressed;
    s_dbg_wheel = wheel;
    s_dbg_char_count = 0;
    if (chars) {
        while (chars[s_dbg_char_count] && s_dbg_char_count < 15) {
            s_dbg_chars[s_dbg_char_count] = chars[s_dbg_char_count];
            s_dbg_char_count++;
        }
    }
}

void wl_ui_begin(WlUi *ui, WlAssets *assets, const WlConfig *cfg)
{
    ui->assets = assets;
    ui->scale = cfg ? cfg->ui_scale : 1.0f;
    ui->hot = 0;
    ui->next_id = 1;
    ui->any_hover = false;
    ui->scroll_active = false;

    if (s_dbg_active) {
        ui->mouse = s_dbg_pos;
        ui->mouse_down = s_dbg_left_down;
        ui->mouse_pressed = s_dbg_left_pressed;
        ui->mouse_released = false;
        ui->mouse_right_pressed = false;
        ui->wheel = s_dbg_wheel;
        ui->char_count = 0;
        for (int i = 0; i < s_dbg_char_count; i++)
            ui->chars[ui->char_count++] = s_dbg_chars[i];
        s_dbg_char_count = 0; /* one shot per frame */
        ui->chars[ui->char_count] = '\0';
    } else {
        ui->mouse = GetMousePosition();
        ui->mouse_down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        ui->mouse_pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        ui->mouse_released = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
        ui->mouse_right_pressed = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
        ui->wheel = (int)GetMouseWheelMove();

        ui->char_count = 0;
        int c = GetCharPressed();
        while (c > 0 && ui->char_count < (int)sizeof(ui->chars) - 1) {
            if (c >= 32 && c < 127) ui->chars[ui->char_count++] = (char)c;
            c = GetCharPressed();
        }
        ui->chars[ui->char_count] = '\0';
        if (IsKeyPressed(KEY_ESCAPE) || (IsWindowFocused() == false))
            ui->focus_textfield = 0;
    }
}

void wl_ui_end(WlUi *ui)
{
    /* textfield focus: clicking anything else releases it */
    if (ui->mouse_pressed && ui->focus_textfield != 0 && ui->hot != ui->focus_textfield)
        ui->focus_textfield = 0;

    /* keep the overlay rectangle alive until the end of the closing frame
     * so widgets underneath cannot react to the same click that closed
     * it; drop it once the dropdown is no longer active */
    if (ui->overlay_open && ui->active != ui->overlay_id)
        ui->overlay_open = false;
}

void wl_ui_draw_overlay(WlUi *ui)
{
    /* draws the open dropdown list. Must be called INSIDE the frame
     * (between BeginDrawing and EndDrawing) after the other widgets, so
     * the list renders above them; wl_ui_draw_overlay handles the
     * rendering, wl_ui_end only the input bookkeeping. */
    if (!ui->overlay_open) return;
    Rectangle list = ui->overlay_rect;
    wl_ui_panel(ui, list);
    DrawRectangleLinesEx(list, 1.0f, (Color){ 20, 22, 26, 220 });
    Font f = wl_ui_font(ui);
    float fs = 22.0f * ui->scale;
    float row_h = 32.0f * ui->scale;
    for (int i = 0; i < ui->overlay_count; i++) {
        Rectangle row = { list.x, list.y + row_h * i, list.width, row_h };
        bool rh = CheckCollisionPointRec(ui->mouse, row);
        if (rh) DrawRectangleRec(row, (Color){ 255, 255, 255, 26 });
        Color c = (i == *ui->overlay_index) ? (Color){ 235, 232, 180, 255 }
                                            : (Color){ 214, 218, 224, 255 };
        DrawTextEx(f, ui->overlay_options[i],
                   (Vector2){ row.x + 12 * ui->scale, row.y + (row_h - fs) / 2 },
                   fs, 1.0f, c);
    }
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

/* hover test with the two UI-context guards:
 *  - while a dropdown list is open, controls underneath it do not react
 *  - inside a scroll region, rows clipped out of the viewport are dead */
static bool mouse_over(WlUi *ui, Rectangle r)
{
    if (!CheckCollisionPointRec(ui->mouse, r)) return false;
    if (ui->overlay_open && CheckCollisionPointRec(ui->mouse, ui->overlay_rect))
        return false;
    if (ui->scroll_active && CheckCollisionRecs(r, ui->scroll_view) &&
        !CheckCollisionPointRec(ui->mouse, ui->scroll_view))
        return false;
    return true;
}

/* ------------------------------------------------------------------ */
/* scroll region                                                       */
/* ------------------------------------------------------------------ */

void wl_ui_scroll_begin(WlUi *ui, Rectangle viewport, float content_h, float *scroll)
{
    if (scroll) {
        float max_scroll = content_h - viewport.height;
        if (max_scroll < 0) max_scroll = 0;
        bool over_list = ui->overlay_open &&
                         CheckCollisionPointRec(ui->mouse, ui->overlay_rect);
        if (ui->wheel != 0 && CheckCollisionPointRec(ui->mouse, viewport) &&
            !over_list) {
            *scroll -= ui->wheel * 42.0f;
        }
        if (*scroll > max_scroll) *scroll = max_scroll;
        if (*scroll < 0) *scroll = 0;
    }
    ui->scroll_active = true;
    ui->scroll_view = viewport;
    BeginScissorMode((int)viewport.x, (int)viewport.y,
                     (int)viewport.width, (int)viewport.height);
}

void wl_ui_scroll_end(WlUi *ui)
{
    EndScissorMode();
    ui->scroll_active = false;
}

/* ------------------------------------------------------------------ */
/* widgets                                                             */
/* ------------------------------------------------------------------ */

bool wl_ui_button(WlUi *ui, Rectangle r, const char *label)
{
    int id = ui->next_id++;
    bool hover = mouse_over(ui, r);
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
    bool hover = mouse_over(ui, r);
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
        float nt = (ui->mouse.x - r.x) / r.width;
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
    bool hover = mouse_over(ui, r);
    if (hover) { ui->hot = id; ui->any_hover = true; }

    /* toggle / close first, so the overlay is registered on the very
     * frame the list opens (not one frame later) */
    if (ui->mouse_pressed && hover) {
        ui->active = (ui->active == id) ? 0 : id;
    } else if (ui->active == id && ui->mouse_pressed && !hover) {
        /* a click inside the expanded list is a row pick, handled below */
        bool click_in_list = CheckCollisionPointRec(ui->mouse, ui->overlay_rect);
        if (!click_in_list) ui->active = 0;
    }
    bool open = (ui->active == id);

    /* row input is processed here, immediately; the list itself is drawn
     * at the end of the frame (wl_ui_end) so it renders above the rows
     * that follow this widget */
    if (open) {
        float row_h = 32.0f * ui->scale;
        float list_h = row_h * count;
        Rectangle list = { r.x, r.y + r.height + 2, r.width, list_h };
        if (list.y + list.height > (float)GetScreenHeight())
            list.y = r.y - list_h - 2; /* flip up when it would overflow */

        ui->overlay_open = true;
        ui->overlay_id = id;
        ui->overlay_rect = list;
        ui->overlay_options = options;
        ui->overlay_count = count;
        ui->overlay_index = index;

        for (int i = 0; i < count; i++) {
            Rectangle row = { list.x, list.y + row_h * i, list.width, row_h };
            if (CheckCollisionPointRec(ui->mouse, row) && ui->mouse_pressed) {
                *index = i;
                changed = true;
                open = false;
                ui->active = 0; /* closed; overlay lives until wl_ui_end */
            }
        }
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
    return changed;
}

/* ------------------------------------------------------------------ */
/* text field                                                          */
/* ------------------------------------------------------------------ */

bool wl_ui_textfield_edit(char *buf, size_t cap, int *caret,
                          const char *chars, bool backspace, bool del,
                          bool left, bool right, bool home, bool end)
{
    bool changed = false;
    int len = (int)strlen(buf);
    if (*caret < 0) *caret = 0;
    if (*caret > len) *caret = len;

    if (backspace && *caret > 0) {
        memmove(&buf[*caret - 1], &buf[*caret], (size_t)(len - *caret) + 1);
        (*caret)--;
        len--;
        changed = true;
    }
    if (del && *caret < len) {
        memmove(&buf[*caret], &buf[*caret + 1], (size_t)(len - *caret));
        len--;
        changed = true;
    }
    if (left && *caret > 0) (*caret)--;
    if (right && *caret < len) (*caret)++;
    if (home) *caret = 0;
    if (end) *caret = len;

    if (chars && chars[0]) {
        for (const char *p = chars; *p; p++) {
            if ((size_t)len + 1 >= cap) break; /* keep room for the NUL */
            memmove(&buf[*caret + 1], &buf[*caret], (size_t)(len - *caret) + 1);
            buf[(*caret)++] = *p;
            len++;
            changed = true;
        }
    }
    return changed;
}

bool wl_ui_textfield(WlUi *ui, int id, Rectangle r, char *buf, size_t cap)
{
    bool activated = false;
    bool hover = mouse_over(ui, r);
    if (hover) {
        ui->any_hover = true;
        ui->hot = id; /* wl_ui_end keeps focus only while the field is hot */
    }
    if (hover && ui->mouse_pressed) {
        if (ui->focus_textfield != id) {
            ui->focus_textfield = id;
            ui->caret = (int)strlen(buf); /* caret starts at the end */
        }
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
        /* caret at the current edit position */
        char prefix[WL_UI_TEXTFIELD_CHARS];
        int n = ui->caret < (int)strlen(buf) ? ui->caret : (int)strlen(buf);
        if (n > WL_UI_TEXTFIELD_CHARS - 1) n = WL_UI_TEXTFIELD_CHARS - 1;
        memcpy(prefix, buf, (size_t)n);
        prefix[n] = '\0';
        Vector2 ts = MeasureTextEx(f, prefix, fs, 1.0f);
        float blink = fmodf((float)GetTime(), 1.0f) < 0.55f ? 1.0f : 0.0f;
        if (blink > 0.0f)
            DrawRectangle((int)(r.x + 12 + ts.x), (int)(r.y + r.height * 0.22f),
                          (int)(2 * ui->scale), (int)(r.height * 0.56f),
                          (Color){ 235, 232, 180, 255 });
    }

    if (focused) {
        wl_ui_textfield_edit(buf, cap, &ui->caret, ui->chars,
                             IsKeyPressed(KEY_BACKSPACE), IsKeyPressed(KEY_DELETE),
                             IsKeyPressed(KEY_LEFT), IsKeyPressed(KEY_RIGHT),
                             IsKeyPressed(KEY_HOME), IsKeyPressed(KEY_END));
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
        bool hover = mouse_over(ui, tr);
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
