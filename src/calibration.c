#include "calibrage.h"

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

/* Restart the center accumulator (keeps the range capture). */
static void jc__center_reset(jc_calibration_capture *cap)
{
    const jc_platform_info *platform = jc_platform_current();
    cap->x_zero_sum = 0;
    cap->y_zero_sum = 0;
    cap->zero_count = 0;
    cap->x_zero_min = platform->raw_max;
    cap->x_zero_max = platform->raw_min;
    cap->y_zero_min = platform->raw_max;
    cap->y_zero_max = platform->raw_min;
}

void jc_capture_add_center(jc_calibration_capture *cap, int x, int y)
{
    if (!cap)
        return;
    int cx = jc_clamp_raw(x);
    int cy = jc_clamp_raw(y);

    /* The center step starts the moment the user presses A, while the stick may
       still be traveling back from the edge. Only the settled rest should count,
       so if a sample jumps away from the running average (the stick is still
       moving), restart accumulation from here. Once the stick is at rest, samples
       cluster tightly and accumulate into a clean center + small jitter. */
    if (cap->zero_count > 0) {
        const jc_platform_info *platform = jc_platform_current();
        int thresh = (platform->raw_max - platform->raw_min) / 16;
        if (thresh < 8)
            thresh = 8;
        int mean_x = (int)(cap->x_zero_sum / cap->zero_count);
        int mean_y = (int)(cap->y_zero_sum / cap->zero_count);
        int dx = cx - mean_x; if (dx < 0) dx = -dx;
        int dy = cy - mean_y; if (dy < 0) dy = -dy;
        if (dx > thresh || dy > thresh)
            jc__center_reset(cap);
    }

    cap->x_zero_sum += cx;
    cap->y_zero_sum += cy;
    if (cx < cap->x_zero_min)
        cap->x_zero_min = cx;
    if (cx > cap->x_zero_max)
        cap->x_zero_max = cx;
    if (cy < cap->y_zero_min)
        cap->y_zero_min = cy;
    if (cy > cap->y_zero_max)
        cap->y_zero_max = cy;
    cap->zero_count++;
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
        set_err(err, err_size, "Missing calibration capture.");
        return -1;
    }
    if (cap->range_count < 20) {
        set_err(err, err_size, "Not enough movement samples.");
        return -1;
    }
    int min_range = jc_platform_current()->min_range;
    if ((cap->x_max - cap->x_min) < min_range ||
        (cap->y_max - cap->y_min) < min_range) {
        set_err(err, err_size, "Move the stick farther in every direction.");
        return -1;
    }
    if (cap->zero_count < 8) {
        set_err(err, err_size, "Not enough center samples.");
        return -1;
    }

    out->x_min = jc_clamp_raw(cap->x_min);
    out->x_max = jc_clamp_raw(cap->x_max);
    out->y_min = jc_clamp_raw(cap->y_min);
    out->y_max = jc_clamp_raw(cap->y_max);
    out->x_zero = jc_clamp_raw((int)((cap->x_zero_sum + cap->zero_count / 2) /
                                    cap->zero_count));
    out->y_zero = jc_clamp_raw((int)((cap->y_zero_sum + cap->zero_count / 2) /
                                    cap->zero_count));

    if (out->x_zero < out->x_min)
        out->x_zero = out->x_min;
    if (out->x_zero > out->x_max)
        out->x_zero = out->x_max;
    if (out->y_zero < out->y_min)
        out->y_zero = out->y_min;
    if (out->y_zero > out->y_max)
        out->y_zero = out->y_max;

    /* Peak center jitter: the largest deviation of any center sample from the
       averaged center, across both axes. Drives the runtime deadzone. */
    int nx_lo = out->x_zero - cap->x_zero_min;
    int nx_hi = cap->x_zero_max - out->x_zero;
    int ny_lo = out->y_zero - cap->y_zero_min;
    int ny_hi = cap->y_zero_max - out->y_zero;
    int noise = nx_lo;
    if (nx_hi > noise) noise = nx_hi;
    if (ny_lo > noise) noise = ny_lo;
    if (ny_hi > noise) noise = ny_hi;
    out->center_noise = noise > 0 ? noise : 0;

    if (!jc_config_valid(out)) {
        set_err(err, err_size, "Calibration values are invalid.");
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
