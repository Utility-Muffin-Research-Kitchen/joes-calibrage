#include "catastrophe.h"
#include "catastrophe_widgets.h"

#include "calibrage.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int imax(int a, int b)
{
    return a > b ? a : b;
}

static int imin(int a, int b)
{
    return a < b ? a : b;
}

static int ui_gap(int logical_px)
{
    return imax(1, cat_scale(logical_px));
}

static int ui_header_margin(void)
{
    return imax(cat_scale(48), 30);
}

static SDL_Rect ui_content_rect(bool has_footer)
{
    SDL_Rect rect = cat_get_content_rect(true, has_footer, false);
    int margin = ui_header_margin();
    rect.x += margin;
    rect.w -= margin * 2;
    if (rect.w < 1)
        rect.w = 1;
    return rect;
}

static int font_line_h(TTF_Font *font)
{
    return font ? TTF_FontLineSkip(font) : ui_gap(20);
}

static int apply_runtime_reload(char *err, size_t err_size)
{
    if (jc_config_apply_reload(err, err_size) != 0)
        return -1;
    /* (Upstream re-scanned input here after restarting trimui_inputd on TG5040;
       MLP1 has no input-daemon restart, and Catastrophe lazily re-inits input on
       the next poll, so nothing to do.) */
    return 0;
}

static bool platform_uses_split_raw(void)
{
    jc_raw_format format = jc_platform_current()->raw_format;
    return format == JC_RAW_FORMAT_TG5040 || format == JC_RAW_FORMAT_TG5050;
}

typedef enum {
    UI_ACTION_TEST = 0,
    UI_ACTION_CAL_LEFT,
    UI_ACTION_CAL_RIGHT,
    UI_ACTION_VALUES,
    UI_ACTION_RESTORE,
    UI_ACTION_QUIT,
} ui_action;

static void show_message(const char *message, bool error)
{
    (void)error;
    cat_footer_item footer[] = {
        { .button = CAT_BTN_A, .label = "OK", .is_confirm = true },
    };
    cat_message_opts opts = {
        .message = message,
        .footer = footer,
        .footer_count = 1,
    };
    cat_confirm_result result = {0};
    (void)cat_confirmation(&opts, &result);
}

static bool show_confirm(const char *message, const char *confirm_label)
{
    cat_footer_item footer[] = {
        { .button = CAT_BTN_B, .label = "Cancel" },
        { .button = CAT_BTN_A, .label = confirm_label, .is_confirm = true },
    };
    cat_message_opts opts = {
        .message = message,
        .footer = footer,
        .footer_count = 2,
    };
    cat_confirm_result result = {0};
    return cat_confirmation(&opts, &result) == CAT_OK && result.confirmed;
}

static ui_action show_main_menu(void)
{
    bool dual = JC_PLATFORM_HAS_RIGHT_STICK(jc_platform_current());

    /* Single-stick platforms (MLP1) drop the right-stick entry and label the
       lone calibration "Calibrate" rather than "Calibrate Left". A parallel map
       keeps the selected index pointing at the right action. */
    cat_list_item items_dual[] = {
        CAT_LIST_ITEM("Test Sticks", NULL),
        CAT_LIST_ITEM("Calibrate Left", NULL),
        CAT_LIST_ITEM("Calibrate Right", NULL),
        CAT_LIST_ITEM("View Values", NULL),
        CAT_LIST_ITEM("Restore Backup", NULL),
    };
    cat_list_item items_single[] = {
        CAT_LIST_ITEM("Test Stick", NULL),
        CAT_LIST_ITEM("Calibrate", NULL),
        CAT_LIST_ITEM("View Values", NULL),
        CAT_LIST_ITEM("Restore Backup", NULL),
    };
    static const ui_action map_dual[] = {
        UI_ACTION_TEST, UI_ACTION_CAL_LEFT, UI_ACTION_CAL_RIGHT,
        UI_ACTION_VALUES, UI_ACTION_RESTORE,
    };
    static const ui_action map_single[] = {
        UI_ACTION_TEST, UI_ACTION_CAL_LEFT, UI_ACTION_VALUES, UI_ACTION_RESTORE,
    };

    cat_list_item *items = dual ? items_dual : items_single;
    int count = dual ? 5 : 4;
    const ui_action *map = dual ? map_dual : map_single;

    cat_footer_item footer[] = {
        { .button = CAT_BTN_B, .label = "Quit" },
        { .button = CAT_BTN_A, .label = "Select", .is_confirm = true },
    };
    cat_list_opts opts = cat_list_default_opts("Joe's Calibrage", items, count);
    opts.footer = footer;
    opts.footer_count = 2;

    cat_list_result result = {0};
    if (cat_list(&opts, &result) != CAT_OK)
        return UI_ACTION_QUIT;
    if (result.selected_index < 0 || result.selected_index >= count)
        return UI_ACTION_QUIT;
    return map[result.selected_index];
}

static SDL_Joystick *open_joystick(void)
{
    SDL_JoystickUpdate();
    int count = SDL_NumJoysticks();
    for (int i = 0; i < count; i++) {
        SDL_Joystick *joy = SDL_JoystickOpen(i);
        if (joy)
            return joy;
    }
    return NULL;
}

static float norm_sdl_axis(Sint16 v)
{
    if (v < 0)
        return (float)v / 32768.0f;
    return (float)v / 32767.0f;
}

static bool read_sdl_sticks(SDL_Joystick *joy, float out[4])
{
    if (!joy)
        return false;
    SDL_JoystickUpdate();
    int axes = SDL_JoystickNumAxes(joy);
    if (axes < 5)
        return false;
    out[0] = norm_sdl_axis(SDL_JoystickGetAxis(joy, 0));
    out[1] = norm_sdl_axis(SDL_JoystickGetAxis(joy, 1));
    out[2] = norm_sdl_axis(SDL_JoystickGetAxis(joy, 3));
    out[3] = norm_sdl_axis(SDL_JoystickGetAxis(joy, 4));
    return true;
}

static void draw_line(int x1, int y1, int x2, int y2, cat_draw_color c)
{
    SDL_Renderer *r = cat_get_renderer();
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLine(r, x1, y1, x2, y2);
}

static void clamp_stick_vector(float *x, float *y)
{
    float len_sq = *x * *x + *y * *y;
    if (len_sq <= 1.0f)
        return;
    float scale = 1.0f / sqrtf(len_sq);
    *x *= scale;
    *y *= scale;
}

static void draw_stick_widget(int cx, int cy, int radius, float x, float y,
                              const char *label, const char *detail,
                              int text_x, int text_w)
{
    cat_theme *t = cat_get_theme();
    cat_draw_color ring = t->hint;
    cat_draw_color dot = t->highlight;
    cat_draw_color cross = t->accent;
    clamp_stick_vector(&x, &y);
    cat_draw_circle(cx, cy, radius, (cat_draw_color){ ring.r, ring.g, ring.b, 55 });
    draw_line(cx - radius, cy, cx + radius, cy, cross);
    draw_line(cx, cy - radius, cx, cy + radius, cross);
    int dx = (int)(x * (float)(radius - cat_scale(8)));
    int dy = (int)(y * (float)(radius - cat_scale(8)));
    cat_draw_circle(cx + dx, cy + dy, cat_scale(7), dot);

    TTF_Font *label_font = cat_get_font(CAT_FONT_TINY);
    TTF_Font *detail_font = cat_get_font(CAT_FONT_MICRO);
    int label_y = cy + radius + ui_gap(6);
    int detail_y = label_y + font_line_h(label_font);
    int label_w = cat_measure_text_ellipsized(label_font, label, text_w);
    int detail_w = cat_measure_text_ellipsized(detail_font, detail, text_w);
    cat_draw_text_ellipsized(label_font, label, text_x + (text_w - label_w) / 2,
                            label_y, t->text, text_w);
    cat_draw_text_ellipsized(detail_font, detail, text_x + (text_w - detail_w) / 2,
                            detail_y, t->hint, text_w);
}

static void show_test_screen(void)
{
    SDL_Joystick *joy = open_joystick();

    bool done = false;
    while (!done) {
        cat_input_event ev;
        while (cat_poll_input(&ev)) {
            if (ev.pressed && ev.button == CAT_BTN_B)
                done = true;
        }

        float axes[4] = {0};
        bool have_axes = read_sdl_sticks(joy, axes);

        bool dual = JC_PLATFORM_HAS_RIGHT_STICK(jc_platform_current());

        cat_clear_screen();
        cat_draw_screen_title(dual ? "Test Sticks" : "Test Stick", NULL);

        SDL_Rect content = ui_content_rect(true);
        TTF_Font *status_font = cat_get_font(CAT_FONT_TINY);
        TTF_Font *label_font = cat_get_font(CAT_FONT_TINY);
        TTF_Font *detail_font = cat_get_font(CAT_FONT_MICRO);
        int footer_top = cat_get_screen_height() - cat_get_footer_height();
        int status_y = content.y + ui_gap(4);
        int widget_top = status_y + font_line_h(status_font) + ui_gap(18);
        int label_h = font_line_h(label_font) + font_line_h(detail_font);
        int widget_gap = ui_gap(20);
        int col_gap = ui_gap(20);
        int col_w = (content.w - col_gap) / 2;
        int max_radius_w = imax(1, (col_w - ui_gap(8)) / 2);
        int max_radius_h = imax(1, (footer_top - widget_top - label_h - widget_gap) / 2);
        int radius = imin(max_radius_w, max_radius_h);
        radius = imax(radius, ui_gap(44));
        radius = imin(radius, max_radius_h);
        int left_x = content.x;
        int right_x = content.x + col_w + col_gap;
        int left_cx = left_x + col_w / 2;
        int right_cx = right_x + col_w / 2;
        int cy = widget_top + radius;

        char left_detail[80];
        char right_detail[80];
        snprintf(left_detail, sizeof(left_detail), have_axes ? "SDL axis" : "No input");
        snprintf(right_detail, sizeof(right_detail), have_axes ? "SDL axis" : "No input");

        char joy_type[32] = {0};
        char status[160];
        if (jc_read_joy_type(joy_type, sizeof(joy_type)) == 0) {
            snprintf(status, sizeof(status), "SDL: %s   platform: %s   joy: %s",
                     have_axes ? "live" : "unavailable",
                     jc_platform_id_name(), joy_type);
        } else {
            snprintf(status, sizeof(status), "SDL: %s   platform: %s",
                     have_axes ? "live" : "unavailable",
                     jc_platform_id_name());
        }
        cat_draw_text_ellipsized(status_font, status, content.x, status_y,
                                cat_get_theme()->hint, content.w);

        if (dual) {
            draw_stick_widget(left_cx, cy, radius, axes[0], axes[1], "Left",
                              left_detail, left_x, col_w);
            draw_stick_widget(right_cx, cy, radius, axes[2], axes[3], "Right",
                              right_detail, right_x, col_w);
        } else {
            /* Single analog stick: one widget centered on the content. */
            draw_stick_widget(content.x + content.w / 2, cy, radius,
                              axes[0], axes[1], "Stick", left_detail,
                              content.x, content.w);
        }

        cat_footer_item footer[] = {
            { .button = CAT_BTN_B, .label = "Back" },
        };
        cat_draw_footer(footer, 1);
        cat_request_frame();
        cat_present();
    }

    if (joy)
        SDL_JoystickClose(joy);
}

static void draw_config_block(const char *name, const jc_config *cfg, bool loaded,
                              int x, int y, int w)
{
    cat_theme *t = cat_get_theme();
    TTF_Font *section_font = cat_get_font(CAT_FONT_SMALL);
    TTF_Font *body_font = cat_get_font(CAT_FONT_TINY);
    char line[96];

    snprintf(line, sizeof(line), "%s (%s)", name, loaded ? "saved" : "default");
    cat_draw_text_ellipsized(section_font, line, x, y, t->text, w);
    y += font_line_h(section_font);

    snprintf(line, sizeof(line), "X: min=%d  zero=%d  max=%d",
             cfg->x_min, cfg->x_zero, cfg->x_max);
    cat_draw_text_ellipsized(body_font, line, x, y, t->hint, w);
    y += font_line_h(body_font);

    snprintf(line, sizeof(line), "Y: min=%d  zero=%d  max=%d",
             cfg->y_min, cfg->y_zero, cfg->y_max);
    cat_draw_text_ellipsized(body_font, line, x, y, t->hint, w);
}

static void show_values_screen(void)
{
    jc_config_pair cfg;
    char err[160] = {0};
    (void)jc_config_load_pair(&cfg, err, sizeof(err));

    bool done = false;
    while (!done) {
        cat_input_event ev;
        while (cat_poll_input(&ev)) {
            if (ev.pressed && (ev.button == CAT_BTN_A || ev.button == CAT_BTN_B))
                done = true;
        }

        cat_clear_screen();
        cat_draw_screen_title("Values", NULL);
        SDL_Rect content = ui_content_rect(true);
        cat_theme *t = cat_get_theme();
        TTF_Font *body_font = cat_get_font(CAT_FONT_TINY);
        TTF_Font *diag_font = cat_get_font(CAT_FONT_MICRO);
        TTF_Font *section_font = cat_get_font(CAT_FONT_SMALL);
        int y = content.y + ui_gap(4);
        int block_h = font_line_h(section_font) + font_line_h(body_font) * 2;

        bool dual = JC_PLATFORM_HAS_RIGHT_STICK(jc_platform_current());
        draw_config_block(dual ? "Left" : "Stick", &cfg.left, cfg.have_left,
                          content.x, y, content.w);
        y += block_h + ui_gap(12);
        if (dual) {
            draw_config_block("Right", &cfg.right, cfg.have_right,
                              content.x, y, content.w);
            y += block_h + ui_gap(14);
        } else {
            y += ui_gap(14);
        }

        char line[JC_PATH_MAX * 2 + 64];
        snprintf(line, sizeof(line), "Platform: %s (%s)",
                 jc_platform_display_name(), jc_platform_id_name());
        cat_draw_text_ellipsized(diag_font, line, content.x, y, t->hint, content.w);
        y += font_line_h(diag_font);
        snprintf(line, sizeof(line), "Runtime: %s", jc_config_runtime_userdata_root());
        cat_draw_text_ellipsized(diag_font, line, content.x, y, t->hint, content.w);
        y += font_line_h(diag_font);
        snprintf(line, sizeof(line), "SD mirror: %s", jc_config_sd_userdata_root());
        cat_draw_text_ellipsized(diag_font, line, content.x, y, t->hint, content.w);
        y += font_line_h(diag_font);
        if (platform_uses_split_raw()) {
            snprintf(line, sizeof(line), "Raw: L %s  R %s",
                     jc_raw_left_device_path(), jc_raw_right_device_path());
        } else {
            snprintf(line, sizeof(line), "Raw: %s", jc_raw_device_path());
        }
        cat_draw_text_ellipsized(diag_font, line, content.x, y, t->hint, content.w);

        cat_footer_item footer[] = {
            { .button = CAT_BTN_A, .label = "OK", .is_confirm = true },
        };
        cat_draw_footer(footer, 1);
        cat_request_frame();
        cat_present();
    }
}

static bool range_ready(const jc_calibration_capture *cap)
{
    int min_range = jc_platform_current()->min_range;
    return cap->range_count >= 20 &&
           (cap->x_max - cap->x_min) >= min_range &&
           (cap->y_max - cap->y_min) >= min_range;
}

static void transform_calibration_display_axes(float *x, float *y)
{
    if (jc_platform_current()->raw_format == JC_RAW_FORMAT_TG5050)
        *y = -*y;
    (void)x;
}

static void draw_calibration_screen(jc_stick stick, int step,
                                    const jc_calibration_capture *cap,
                                    const jc_raw_sample *raw,
                                    const char *status_message)
{
    cat_clear_screen();
    const char *cal_title = !JC_PLATFORM_HAS_RIGHT_STICK(jc_platform_current())
        ? "Calibrate"
        : (stick == JC_STICK_LEFT ? "Calibrate Left" : "Calibrate Right");
    cat_draw_screen_title(cal_title, NULL);
    SDL_Rect content = ui_content_rect(true);
    cat_theme *t = cat_get_theme();

    const char *instruction = step == 0
        ? "Rotate fully around the edge, then press A."
        : "Release the stick and keep it centered, then press Y.";
    TTF_Font *instruction_font = cat_get_font(CAT_FONT_TINY);
    TTF_Font *label_font = cat_get_font(CAT_FONT_TINY);
    TTF_Font *detail_font = cat_get_font(CAT_FONT_MICRO);
    TTF_Font *stats_font = cat_get_font(CAT_FONT_MICRO);
    int instruction_y = content.y + ui_gap(4);
    int instruction_h = cat_draw_text_wrapped(instruction_font, instruction,
                                             content.x, instruction_y, content.w,
                                             t->text, CAT_ALIGN_LEFT);

    int x = stick == JC_STICK_LEFT ? raw->left_x : raw->right_x;
    int y = stick == JC_STICK_LEFT ? raw->left_y : raw->right_y;
    float nx = 0.0f;
    float ny = 0.0f;
    bool have_selected_raw = stick == JC_STICK_LEFT ? raw->left_valid
                                                    : raw->right_valid;
    if (have_selected_raw) {
        jc_config def;
        jc_config_default(&def);
        int zx = cap->zero_count > 0 ? (int)(cap->x_zero_sum / cap->zero_count)
                                     : def.x_zero;
        int zy = cap->zero_count > 0 ? (int)(cap->y_zero_sum / cap->zero_count)
                                     : def.y_zero;
        int xmin = cap->range_count > 0 ? cap->x_min : def.x_min;
        int xmax = cap->range_count > 0 ? cap->x_max : def.x_max;
        int ymin = cap->range_count > 0 ? cap->y_min : def.y_min;
        int ymax = cap->range_count > 0 ? cap->y_max : def.y_max;
        nx = jc_config_normalize_axis(x, xmin, zx, xmax);
        ny = jc_config_normalize_axis(y, ymin, zy, ymax);
        transform_calibration_display_axes(&nx, &ny);
    }

    int footer_top = cat_get_screen_height() - cat_get_footer_height();
    int stats_h = font_line_h(stats_font) * 2;
    int label_h = font_line_h(label_font) + font_line_h(detail_font);
    int widget_top = instruction_y + instruction_h + ui_gap(14);
    int widget_bottom = footer_top - stats_h - ui_gap(16);
    int max_radius_h = imax(1, (widget_bottom - widget_top - label_h) / 2);
    int radius = imin(content.w / 5, max_radius_h);
    radius = imax(radius, ui_gap(38));
    radius = imin(radius, max_radius_h);
    int cx = content.x + content.w / 2;
    int circle_y = widget_top + radius;
    char detail[96];
    snprintf(detail, sizeof(detail), "raw %d,%d", x, y);
    draw_stick_widget(cx, circle_y, radius, nx, ny,
                      stick == JC_STICK_LEFT ? "Left" : "Right", detail,
                      content.x, content.w);

    int stats_y = footer_top - stats_h - ui_gap(8);
    char stats[96];
    snprintf(stats, sizeof(stats), "range x:%d-%d y:%d-%d",
             cap->x_min, cap->x_max, cap->y_min, cap->y_max);
    cat_draw_text_ellipsized(stats_font, stats, content.x, stats_y, t->hint, content.w);
    snprintf(stats, sizeof(stats), "samples:%d  center:%d",
             cap->range_count, cap->zero_count);
    cat_draw_text_ellipsized(stats_font, stats, content.x,
                            stats_y + font_line_h(stats_font), t->hint, content.w);
    char auto_status[96] = {0};
    const char *display_status = status_message && status_message[0] ? status_message : NULL;
    if (step == 0 && range_ready(cap)) {
        snprintf(auto_status, sizeof(auto_status), "Range captured. Press A.");
        display_status = auto_status;
    }
    if (display_status && display_status[0]) {
        cat_draw_text_ellipsized(stats_font, display_status, content.x,
                                stats_y - font_line_h(stats_font) - ui_gap(2),
                                t->text, content.w);
    }
}

static unsigned raw_button_state(const jc_raw_sample *sample)
{
    if (jc_platform_current()->raw_format == JC_RAW_FORMAT_TG5050)
        return (unsigned)sample->left_buttons | (unsigned)sample->right_buttons;
    if (jc_platform_current()->raw_format == JC_RAW_FORMAT_TG5040)
        return (unsigned)sample->right_buttons;
    return 0;
}

static bool tg5050_rotate_270_active(void)
{
    const char *path = getenv("CALIBRAGE_TG5050_ROTATE_270_PATH");
    if (!path || path[0] == '\0')
        path = "/var/trimui_inputd/rotate_270";
    return access(path, F_OK) == 0;
}

static unsigned raw_button_mask(int button)
{
    if (jc_platform_current()->raw_format == JC_RAW_FORMAT_TG5050) {
        if (tg5050_rotate_270_active()) {
            switch (button) {
            case CAT_BTN_A: return 0x00000001u;
            case CAT_BTN_B: return 0x00000002u;
            case CAT_BTN_X: return 0x00010000u;
            case CAT_BTN_Y: return 0x00020000u;
            default: return 0;
            }
        } else {
            switch (button) {
            case CAT_BTN_A: return 0x00000100u;
            case CAT_BTN_B: return 0x00000001u;
            case CAT_BTN_X: return 0x00000200u;
            case CAT_BTN_Y: return 0x00000002u;
            default: return 0;
            }
        }
    }
    if (jc_platform_current()->raw_format == JC_RAW_FORMAT_TG5040) {
        switch (button) {
        case CAT_BTN_A: return 0x10u;
        case CAT_BTN_B: return 0x20u;
        case CAT_BTN_X: return 0x04u;
        case CAT_BTN_Y: return 0x08u;
        default: return 0;
        }
    }
    return 0;
}

static void handle_calibration_button(int button, int *step,
                                      jc_calibration_capture *cap,
                                      jc_stick stick, bool *done,
                                      bool *cancelled,
                                      char *status_message,
                                      size_t status_size,
                                      char *final_message,
                                      size_t final_size,
                                      bool *final_error,
                                      bool *saved)
{
    char err[160] = {0};
    if (status_message && status_size > 0)
        status_message[0] = '\0';
    if (button == CAT_BTN_B) {
        *cancelled = true;
        *done = true;
    } else if (button == CAT_BTN_X) {
        jc_capture_reset(cap);
        *step = 0;
    } else if (*step == 0 && button == CAT_BTN_A) {
        if (range_ready(cap)) {
            *step = 1;
        } else {
            snprintf(status_message, status_size,
                     "Move the stick farther in every direction.");
        }
    } else if (*step == 1 && button == CAT_BTN_Y) {
        jc_config cfg;
        if (jc_capture_make_config(cap, &cfg, err, sizeof(err)) != 0) {
            snprintf(final_message, final_size, "%s", err);
            *final_error = true;
            *done = true;
        } else if (jc_config_save_stick(stick, &cfg, err, sizeof(err)) != 0) {
            snprintf(final_message, final_size, "%s", err);
            *final_error = true;
            *done = true;
        } else {
            snprintf(final_message, final_size, "Calibration saved.");
            *final_error = false;
            *saved = true;
            *done = true;
        }
    }
}

static void handle_raw_calibration_buttons(unsigned raw_pressed, int *step,
                                           jc_calibration_capture *cap,
                                           jc_stick stick, bool *done,
                                           bool *cancelled,
                                           char *status_message,
                                           size_t status_size,
                                           char *final_message,
                                           size_t final_size,
                                           bool *final_error,
                                           bool *saved)
{
    if (raw_pressed & raw_button_mask(CAT_BTN_B))
        handle_calibration_button(CAT_BTN_B, step, cap, stick, done, cancelled,
                                  status_message, status_size,
                                  final_message, final_size, final_error, saved);
    if (*done)
        return;
    if (raw_pressed & raw_button_mask(CAT_BTN_X))
        handle_calibration_button(CAT_BTN_X, step, cap, stick, done, cancelled,
                                  status_message, status_size,
                                  final_message, final_size, final_error, saved);
    if (*done)
        return;
    if (raw_pressed & raw_button_mask(CAT_BTN_A))
        handle_calibration_button(CAT_BTN_A, step, cap, stick, done, cancelled,
                                  status_message, status_size,
                                  final_message, final_size, final_error, saved);
    if (*done)
        return;
    if (raw_pressed & raw_button_mask(CAT_BTN_Y))
        handle_calibration_button(CAT_BTN_Y, step, cap, stick, done, cancelled,
                                  status_message, status_size,
                                  final_message, final_size, final_error, saved);
}

static void calibrate_stick(jc_stick stick)
{
    jc_raw_reader raw;
    jc_raw_reader_init(&raw);

    char err[160] = {0};
    if (jc_raw_begin_calibration(err, sizeof(err)) != 0) {
        show_message(err, true);
        return;
    }
    if (jc_raw_reader_open_stick(&raw, stick) != 0) {
        jc_raw_end_calibration();
        show_message(raw.error[0] ? raw.error : "Raw stick stream unavailable.", true);
        return;
    }

    jc_calibration_capture cap;
    jc_capture_reset(&cap);
    int step = 0;
    bool done = false;
    bool cancelled = false;
    jc_raw_sample sample = {0};
    unsigned last_raw_buttons = 0;
    char status_message[128] = {0};
    char final_message[160] = {0};
    bool final_error = false;
    bool saved = false;

    while (!done) {
        int poll = jc_raw_reader_poll(&raw, &sample);
        if (poll < 0) {
            show_message(raw.error[0] ? raw.error : "Could not read raw stick stream.", true);
            cancelled = true;
            break;
        }
        bool fresh_for_stick = stick == JC_STICK_LEFT ? sample.left_valid
                                                      : sample.right_valid;
        if (poll > 0 && fresh_for_stick) {
            int x = stick == JC_STICK_LEFT ? sample.left_x : sample.right_x;
            int y = stick == JC_STICK_LEFT ? sample.left_y : sample.right_y;
            if (step == 0)
                jc_capture_add_range(&cap, x, y);
            else
                jc_capture_add_center(&cap, x, y);
        }
        if (poll > 0 && platform_uses_split_raw()) {
            unsigned raw_buttons = raw_button_state(&sample);
            unsigned raw_pressed = raw_buttons & ~last_raw_buttons;
            last_raw_buttons = raw_buttons;
            handle_raw_calibration_buttons(raw_pressed, &step, &cap, stick,
                                           &done, &cancelled,
                                           status_message, sizeof(status_message),
                                           final_message, sizeof(final_message),
                                           &final_error, &saved);
        }

        cat_input_event ev;
        while (cat_poll_input(&ev)) {
            if (!ev.pressed)
                continue;
            handle_calibration_button(ev.button, &step, &cap, stick,
                                      &done, &cancelled,
                                      status_message, sizeof(status_message),
                                      final_message, sizeof(final_message),
                                      &final_error, &saved);
        }

        draw_calibration_screen(stick, step, &cap, &sample, status_message);
        cat_footer_item footer_step0[] = {
            { .button = CAT_BTN_B, .label = "Cancel" },
            { .button = CAT_BTN_X, .label = "Reset" },
            { .button = CAT_BTN_A, .label = "Next", .is_confirm = true },
        };
        cat_footer_item footer_step1[] = {
            { .button = CAT_BTN_B, .label = "Cancel" },
            { .button = CAT_BTN_X, .label = "Reset" },
            { .button = CAT_BTN_Y, .label = "Save", .is_confirm = true },
        };
        if (step == 0)
            cat_draw_footer(footer_step0, 3);
        else
            cat_draw_footer(footer_step1, 3);
        cat_request_frame();
        cat_present();
    }

    jc_raw_end_calibration();
    jc_raw_reader_close(&raw);
    if (saved) {
        if (apply_runtime_reload(err, sizeof(err)) != 0) {
            snprintf(final_message, sizeof(final_message),
                     "Calibration saved, but input restart failed.");
            final_error = true;
        }
    }
    if (final_message[0])
        show_message(final_message, final_error);
    else if (cancelled)
        show_message("Calibration cancelled.", false);
}

static void restore_backup_flow(void)
{
    if (!show_confirm("Restore the first-run backups for both sticks?", "Restore"))
        return;
    char err[160] = {0};
    if (jc_config_restore_backup(err, sizeof(err)) != 0)
        show_message(err[0] ? err : "Could not restore backups.", true);
    else if (apply_runtime_reload(err, sizeof(err)) != 0)
        show_message(err[0] ? err : "Backups restored, but input restart failed.", true);
    else
        show_message("Backups restored.", false);
}

void jc_ui_run(void)
{
    for (;;) {
        switch (show_main_menu()) {
        case UI_ACTION_TEST:
            show_test_screen();
            break;
        case UI_ACTION_CAL_LEFT:
            calibrate_stick(JC_STICK_LEFT);
            break;
        case UI_ACTION_CAL_RIGHT:
            calibrate_stick(JC_STICK_RIGHT);
            break;
        case UI_ACTION_VALUES:
            show_values_screen();
            break;
        case UI_ACTION_RESTORE:
            restore_backup_flow();
            break;
        case UI_ACTION_QUIT:
        default:
            return;
        }
    }
}
