#include "calibrage.h"
#include "i18n.h"

#include <stdio.h>
#include <string.h>

int jc_clamp_raw(int value)
{
    const jc_platform_info *platform = jc_platform_current();
    if (value < platform->raw_min)
        return platform->raw_min;
    if (value > platform->raw_max)
        return platform->raw_max;
    return value;
}

void jc_capture_reset(jc_calibration_capture *cap)
{
    if (!cap)
        return;
    const jc_platform_info *platform = jc_platform_current();
    cap->x_min = platform->raw_max;
    cap->x_max = platform->raw_min;
    cap->y_min = platform->raw_max;
    cap->y_max = platform->raw_min;
    cap->x_zero_sum = 0;
    cap->y_zero_sum = 0;
    cap->zero_count = 0;
    cap->range_count = 0;
    cap->x_zero_min = platform->raw_max;
    cap->x_zero_max = platform->raw_min;
    cap->y_zero_min = platform->raw_max;
    cap->y_zero_max = platform->raw_min;
    cap->rest_cycles = 0;
    cap->rest_armed = true;   /* the center step starts with the stick coming back */
    cap->win_count = 0;
    cap->fuzz = 0;
    cap->flat = 0;
}

void jc_capture_set_input_filter(jc_calibration_capture *cap, int fuzz, int flat)
{
    if (!cap)
        return;
    cap->fuzz = fuzz > 0 ? fuzz : 0;
    cap->flat = flat > 0 ? flat : 0;
}

void jc_capture_add_range(jc_calibration_capture *cap, int x, int y)
{
    if (!cap)
        return;
    x = jc_clamp_raw(x);
    y = jc_clamp_raw(y);
    if (x < cap->x_min)
        cap->x_min = x;
    if (x > cap->x_max)
        cap->x_max = x;
    if (y < cap->y_min)
        cap->y_min = y;
    if (y > cap->y_max)
        cap->y_max = y;
    cap->range_count++;
}

/* Consecutive still samples before a rest counts as settled (~200 ms of frames). */
#define JC_CENTER_SETTLE_SAMPLES 12

static int iabs(int v)
{
    return v < 0 ? -v : v;
}

/* Narrower captured axis span, or the raw span before any range is captured. */
static int capture_span(const jc_calibration_capture *cap)
{
    int sx = cap->x_max - cap->x_min;
    int sy = cap->y_max - cap->y_min;
    int span = sx < sy ? sx : sy;
    if (span <= 0) {
        const jc_platform_info *platform = jc_platform_current();
        span = platform->raw_max - platform->raw_min;
    }
    return span;
}

/* How far a sample may sit from the window's first sample and still count as the
   same rest. An evdev axis with fuzz only reports a new value when the stick
   really moves (the input core swallows smaller changes), so any visible step
   means it is still settling. A raw serial stream jitters by a few counts, so
   allow a sliver of the captured range. */
static int still_band(const jc_calibration_capture *cap)
{
    if (cap->fuzz > 0)
        return cap->fuzz / 8;
    int band = capture_span(cap) / 64;
    return band < 2 ? 2 : band;
}

/* Settled samples only count as a rest inside this area around the platform's
   nominal zero. Anything outside it is the stick being pushed away, which arms
   the next release cycle. It also keeps a stick held part-way out from being
   mistaken for its rest. */
static int rest_area(const jc_calibration_capture *cap)
{
    return capture_span(cap) / 8;
}

static void merge_rest(jc_calibration_capture *cap, long sum_x, long sum_y, int count,
                       int x_min, int x_max, int y_min, int y_max)
{
    cap->x_zero_sum += sum_x;
    cap->y_zero_sum += sum_y;
    cap->zero_count += count;
    if (x_min < cap->x_zero_min)
        cap->x_zero_min = x_min;
    if (x_max > cap->x_zero_max)
        cap->x_zero_max = x_max;
    if (y_min < cap->y_zero_min)
        cap->y_zero_min = y_min;
    if (y_max > cap->y_zero_max)
        cap->y_zero_max = y_max;
}

int jc_capture_rest_cycles_needed(void)
{
    int needed = jc_platform_current()->rest_cycles;
    return needed > 0 ? needed : 1;
}

bool jc_capture_center_ready(const jc_calibration_capture *cap)
{
    return cap && cap->zero_count >= 8 &&
           cap->rest_cycles >= jc_capture_rest_cycles_needed();
}

/* The center step collects where the stick comes to rest after each release.
   A stick does not return to exactly the same spot every time, and on an evdev
   axis with fuzz the reported value latches anywhere within fuzz/2 of that spot
   (a resting stick sends no events, so EVIOCGABS keeps returning the latched
   value). One settle therefore shows a single point of the rest band with zero
   variance. Instead, each push-and-release cycle contributes its settled rest,
   and the extent of those rests sizes the band in jc_capture_make_config(). */
void jc_capture_add_center(jc_calibration_capture *cap, int x, int y)
{
    if (!cap)
        return;
    int cx = jc_clamp_raw(x);
    int cy = jc_clamp_raw(y);

    const jc_platform_info *platform = jc_platform_current();
    int area = rest_area(cap);
    if (iabs(cx - platform->default_x_zero) > area ||
        iabs(cy - platform->default_y_zero) > area) {
        cap->rest_armed = true;
        cap->win_count = 0;
        return;
    }

    /* Still moving (traveling back, bouncing, creeping): restart the window. */
    int still = still_band(cap);
    if (cap->win_count > 0 &&
        (iabs(cx - cap->win_x) > still || iabs(cy - cap->win_y) > still))
        cap->win_count = 0;

    if (cap->win_count == 0) {
        cap->win_x = cx;
        cap->win_y = cy;
        cap->win_x_sum = 0;
        cap->win_y_sum = 0;
        cap->win_x_min = cap->win_x_max = cx;
        cap->win_y_min = cap->win_y_max = cy;
    }
    cap->win_x_sum += cx;
    cap->win_y_sum += cy;
    if (cx < cap->win_x_min)
        cap->win_x_min = cx;
    if (cx > cap->win_x_max)
        cap->win_x_max = cx;
    if (cy < cap->win_y_min)
        cap->win_y_min = cy;
    if (cy > cap->win_y_max)
        cap->win_y_max = cy;
    cap->win_count++;

    if (cap->win_count == JC_CENTER_SETTLE_SAMPLES) {
        /* Settled: the whole window is rest. Only the first settle after the stick
           was pushed away counts as a new cycle; a re-settle without leaving the
           rest area (a slow creep) still widens the band. */
        merge_rest(cap, cap->win_x_sum, cap->win_y_sum, cap->win_count,
                   cap->win_x_min, cap->win_x_max, cap->win_y_min, cap->win_y_max);
        if (cap->rest_armed) {
            cap->rest_cycles++;
            cap->rest_armed = false;
        }
    } else if (cap->win_count > JC_CENTER_SETTLE_SAMPLES) {
        merge_rest(cap, cx, cy, 1, cx, cx, cy, cy);
    }
}

static void set_err(char *err, size_t err_size, const char *msg)
{
    if (err && err_size > 0)
        snprintf(err, err_size, "%s", msg);
}

static float clamp_norm(float value)
{
    if (value < -1.0f)
        return -1.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}

int jc_capture_make_config(const jc_calibration_capture *cap, jc_config *out,
                           char *err, size_t err_size)
{
    if (!cap || !out) {
        set_err(err, err_size, T("Missing calibration capture."));
        return -1;
    }
    if (cap->range_count < 20) {
        set_err(err, err_size, T("Not enough movement samples."));
        return -1;
    }
    int min_range = jc_platform_current()->min_range;
    if ((cap->x_max - cap->x_min) < min_range ||
        (cap->y_max - cap->y_min) < min_range) {
        set_err(err, err_size, T("Move the stick farther in every direction."));
        return -1;
    }
    if (!jc_capture_center_ready(cap)) {
        set_err(err, err_size, T("Not enough center samples."));
        return -1;
    }

    out->x_min = jc_clamp_raw(cap->x_min);
    out->x_max = jc_clamp_raw(cap->x_max);
    out->y_min = jc_clamp_raw(cap->y_min);
    out->y_max = jc_clamp_raw(cap->y_max);
    /* Zero sits in the middle of the settled rest extent, so the band around it
       is as narrow as it can be while still covering every observed rest. */
    out->x_zero = jc_clamp_raw(cap->x_zero_min +
                               (cap->x_zero_max - cap->x_zero_min) / 2);
    out->y_zero = jc_clamp_raw(cap->y_zero_min +
                               (cap->y_zero_max - cap->y_zero_min) / 2);

    if (out->x_zero < out->x_min)
        out->x_zero = out->x_min;
    if (out->x_zero > out->x_max)
        out->x_zero = out->x_max;
    if (out->y_zero < out->y_min)
        out->y_zero = out->y_min;
    if (out->y_zero > out->y_max)
        out->y_zero = out->y_max;

    /* Rest band half-width, which drives the runtime deadzone. Start from the
       largest deviation of any settled rest from the zero, across both axes.
       With an input fuzz, every observed rest may itself be a value latched up
       to fuzz/2 short of where the stick really sat, and a later release can
       latch up to fuzz/2 beyond that, so widen by a full fuzz. The driver's
       declared flat is a floor. */
    int nx_lo = out->x_zero - cap->x_zero_min;
    int nx_hi = cap->x_zero_max - out->x_zero;
    int ny_lo = out->y_zero - cap->y_zero_min;
    int ny_hi = cap->y_zero_max - out->y_zero;
    int noise = nx_lo;
    if (nx_hi > noise) noise = nx_hi;
    if (ny_lo > noise) noise = ny_lo;
    if (ny_hi > noise) noise = ny_hi;
    if (noise < 0)
        noise = 0;
    noise += cap->fuzz;
    if (noise < cap->flat)
        noise = cap->flat;
    out->center_noise = noise;

    if (!jc_config_valid(out)) {
        set_err(err, err_size, T("Calibration values are invalid."));
        return -1;
    }
    return 0;
}

float jc_config_normalize_axis(int raw, int min, int zero, int max)
{
    raw = jc_clamp_raw(raw);
    if (raw < zero) {
        int span = zero - min;
        if (span <= 0)
            return 0.0f;
        return clamp_norm(-((float)(zero - raw) / (float)span));
    }

    int span = max - zero;
    if (span <= 0)
        return 0.0f;
    return clamp_norm((float)(raw - zero) / (float)span);
}
