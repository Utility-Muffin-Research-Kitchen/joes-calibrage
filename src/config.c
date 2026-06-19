#include "calibrage.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

static const char *left_name = "joypad.config";
static const char *right_name = "joypad_right.config";

static void set_err(char *err, size_t err_size, const char *fmt, ...)
{
    if (!err || err_size == 0)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, err_size, fmt, ap);
    va_end(ap);
}

static const char *env_or_default(const char *env_name, const char *fallback)
{
    const char *value = getenv(env_name);
    return (value && value[0] != '\0') ? value : fallback;
}

static bool platform_runtime_primary(const jc_platform_info *platform)
{
    return platform->id == JC_PLATFORM_TG5040 ||
           platform->id == JC_PLATFORM_TG5050;
}

const char *jc_config_sd_userdata_root(void)
{
    static char dynamic_root[JC_PATH_MAX];
    const jc_platform_info *platform = jc_platform_current();
    const char *env = getenv("CALIBRAGE_SD_USERDATA_ROOT");
    if (env && env[0] != '\0')
        return env;
    if (platform_runtime_primary(platform)) {
        const char *userdata = getenv("USERDATA_PATH");
        if (userdata && userdata[0] != '\0') {
            int n = snprintf(dynamic_root, sizeof(dynamic_root),
                             "%s/joes-calibrage", userdata);
            if (n > 0 && (size_t)n < sizeof(dynamic_root))
                return dynamic_root;
        }
        return platform->sd_userdata_root;
    }
    if (access("/mnt/SDCARD", F_OK) == 0)
        return platform->sd_userdata_root;
    return "/mnt/sdcard/.userdata/my355/userdata";
}

const char *jc_config_runtime_userdata_root(void)
{
    return env_or_default("CALIBRAGE_RUNTIME_USERDATA_ROOT",
                          jc_platform_current()->runtime_userdata_root);
}

const char *jc_config_inputd_dir(void)
{
    return env_or_default("CALIBRAGE_INPUTD_DIR",
                          jc_platform_current()->inputd_dir);
}

const char *jc_config_reload_trigger_path(void)
{
    static char dynamic_path[JC_PATH_MAX];
    const char *env = getenv("CALIBRAGE_RELOAD_TRIGGER_PATH");
    if (env && env[0] != '\0')
        return env;

    const jc_platform_info *platform = jc_platform_current();
    if (platform->id == JC_PLATFORM_MY355) {
        int n = snprintf(dynamic_path, sizeof(dynamic_path), "%s/cal_update",
                         jc_config_inputd_dir());
        if (n > 0 && (size_t)n < sizeof(dynamic_path))
            return dynamic_path;
    }
    return platform->reload_trigger_path;
}

static const char *joy_type_path(void)
{
    const char *env = getenv("CALIBRAGE_JOY_TYPE_PATH");
    if (env && env[0] != '\0')
        return env;
    return jc_platform_current()->joy_type_path;
}

void jc_config_default(jc_config *cfg)
{
    if (!cfg)
        return;
    const jc_platform_info *platform = jc_platform_current();
    cfg->x_min = platform->default_x_min;
    cfg->x_max = platform->default_x_max;
    cfg->y_min = platform->default_y_min;
    cfg->y_max = platform->default_y_max;
    cfg->x_zero = platform->default_x_zero;
    cfg->y_zero = platform->default_y_zero;
}

bool jc_config_valid(const jc_config *cfg)
{
    if (!cfg)
        return false;
    const jc_platform_info *platform = jc_platform_current();
    int raw_min = platform->raw_min;
    int raw_max = platform->raw_max;
    if (cfg->x_min < raw_min || cfg->x_min > raw_max ||
        cfg->x_max < raw_min || cfg->x_max > raw_max)
        return false;
    if (cfg->y_min < raw_min || cfg->y_min > raw_max ||
        cfg->y_max < raw_min || cfg->y_max > raw_max)
        return false;
    if (cfg->x_zero < raw_min || cfg->x_zero > raw_max ||
        cfg->y_zero < raw_min || cfg->y_zero > raw_max)
        return false;
    if (cfg->x_min >= cfg->x_max || cfg->y_min >= cfg->y_max)
        return false;
    if (cfg->x_zero < cfg->x_min || cfg->x_zero > cfg->x_max)
        return false;
    if (cfg->y_zero < cfg->y_min || cfg->y_zero > cfg->y_max)
        return false;
    return true;
}

int jc_config_parse_text(const char *text, jc_config *out, char *err, size_t err_size)
{
    if (!text || !out) {
        set_err(err, err_size, "Missing config text.");
        return -1;
    }

    bool seen[JC_CONFIG_FIELD_COUNT] = {0};
    jc_config cfg = {0};
    const char *p = text;
    while (*p) {
        while (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t')
            p++;
        if (*p == '\0')
            break;

        const char *line_end = strchr(p, '\n');
        size_t len = line_end ? (size_t)(line_end - p) : strlen(p);
        char line[128];
        if (len >= sizeof(line))
            len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';

        char key[32];
        int value = 0;
        if (sscanf(line, " %31[^=]=%d", key, &value) == 2) {
            if (strcmp(key, "x_min") == 0) {
                cfg.x_min = value;
                seen[0] = true;
            } else if (strcmp(key, "x_max") == 0) {
                cfg.x_max = value;
                seen[1] = true;
            } else if (strcmp(key, "y_min") == 0) {
                cfg.y_min = value;
                seen[2] = true;
            } else if (strcmp(key, "y_max") == 0) {
                cfg.y_max = value;
                seen[3] = true;
            } else if (strcmp(key, "x_zero") == 0) {
                cfg.x_zero = value;
                seen[4] = true;
            } else if (strcmp(key, "y_zero") == 0) {
                cfg.y_zero = value;
                seen[5] = true;
            }
        }

        p = line_end ? line_end + 1 : p + strlen(p);
    }

    for (int i = 0; i < JC_CONFIG_FIELD_COUNT; i++) {
        if (!seen[i]) {
            set_err(err, err_size, "Config is missing required fields.");
            return -1;
        }
    }
    if (!jc_config_valid(&cfg)) {
        set_err(err, err_size, "Config values are out of range.");
        return -1;
    }
    *out = cfg;
    return 0;
}

int jc_config_format(const jc_config *cfg, char *buf, size_t buf_size)
{
    if (!cfg || !buf || buf_size == 0 || !jc_config_valid(cfg))
        return -1;
    int n = snprintf(buf, buf_size,
                     "x_min=%d\n"
                     "x_max=%d\n"
                     "y_min=%d\n"
                     "y_max=%d\n"
                     "x_zero=%d\n"
                     "y_zero=%d\n",
                     cfg->x_min, cfg->x_max, cfg->y_min, cfg->y_max,
                     cfg->x_zero, cfg->y_zero);
    return (n > 0 && (size_t)n < buf_size) ? 0 : -1;
}

static int join_path(char *out, size_t out_size, const char *dir, const char *leaf)
{
    int n = snprintf(out, out_size, "%s/%s", dir, leaf);
    return (n > 0 && (size_t)n < out_size) ? 0 : -1;
}

static int read_file(const char *path, char *buf, size_t buf_size,
                     char *err, size_t err_size)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        set_err(err, err_size, "Could not open %s: %s", path, strerror(errno));
        return -1;
    }
    ssize_t n = read(fd, buf, buf_size - 1);
    int saved = errno;
    close(fd);
    if (n < 0) {
        set_err(err, err_size, "Could not read %s: %s", path, strerror(saved));
        return -1;
    }
    buf[n] = '\0';
    return 0;
}

static int mkdir_p(const char *path)
{
    char tmp[JC_PATH_MAX];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp))
        return -1;
    memcpy(tmp, path, len + 1);

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static int parent_dir(const char *path, char *out, size_t out_size)
{
    const char *slash = strrchr(path, '/');
    if (!slash || slash == path) {
        if (out_size < 2)
            return -1;
        strcpy(out, slash == path ? "/" : ".");
        return 0;
    }
    size_t len = (size_t)(slash - path);
    if (len >= out_size)
        return -1;
    memcpy(out, path, len);
    out[len] = '\0';
    return 0;
}

static int copy_file_if_missing(const char *src, const char *dst)
{
    if (access(dst, F_OK) == 0)
        return 0;

    char buf[256];
    int in = open(src, O_RDONLY | O_CLOEXEC);
    if (in < 0)
        return -1;
    int out = open(dst, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (out < 0) {
        close(in);
        return -1;
    }
    for (;;) {
        ssize_t n = read(in, buf, sizeof(buf));
        if (n < 0) {
            close(in);
            close(out);
            return -1;
        }
        if (n == 0)
            break;
        ssize_t off = 0;
        while (off < n) {
            ssize_t wrote = write(out, buf + off, (size_t)(n - off));
            if (wrote < 0) {
                close(in);
                close(out);
                return -1;
            }
            off += wrote;
        }
    }
    fsync(out);
    close(in);
    close(out);
    return 0;
}

static int write_file_atomic(const char *path, const char *data,
                             char *err, size_t err_size)
{
    char dir[JC_PATH_MAX];
    if (parent_dir(path, dir, sizeof(dir)) != 0 || mkdir_p(dir) != 0) {
        set_err(err, err_size, "Could not create parent directory for %s", path);
        return -1;
    }

    char tmp[JC_PATH_MAX];
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid());
    if (n <= 0 || (size_t)n >= sizeof(tmp)) {
        set_err(err, err_size, "Path too long: %s", path);
        return -1;
    }

    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        set_err(err, err_size, "Could not write %s: %s", tmp, strerror(errno));
        return -1;
    }

    size_t len = strlen(data);
    size_t off = 0;
    while (off < len) {
        ssize_t wrote = write(fd, data + off, len - off);
        if (wrote < 0) {
            int saved = errno;
            close(fd);
            unlink(tmp);
            set_err(err, err_size, "Could not write %s: %s", tmp, strerror(saved));
            return -1;
        }
        off += (size_t)wrote;
    }
    if (fsync(fd) != 0) {
        int saved = errno;
        close(fd);
        unlink(tmp);
        set_err(err, err_size, "Could not flush %s: %s", tmp, strerror(saved));
        return -1;
    }
    if (close(fd) != 0) {
        int saved = errno;
        unlink(tmp);
        set_err(err, err_size, "Could not close %s: %s", tmp, strerror(saved));
        return -1;
    }
    if (rename(tmp, path) != 0) {
        int saved = errno;
        unlink(tmp);
        set_err(err, err_size, "Could not replace %s: %s", path, strerror(saved));
        return -1;
    }
    return 0;
}

static int load_one(const char *root, const char *name, jc_config *out,
                    char *err, size_t err_size)
{
    char path[JC_PATH_MAX];
    char text[512];
    if (join_path(path, sizeof(path), root, name) != 0) {
        set_err(err, err_size, "Config path too long.");
        return -1;
    }
    if (read_file(path, text, sizeof(text), err, err_size) != 0)
        return -1;
    return jc_config_parse_text(text, out, err, err_size);
}

int jc_config_load_pair(jc_config_pair *pair, char *err, size_t err_size)
{
    if (!pair)
        return -1;
    memset(pair, 0, sizeof(*pair));

    const jc_platform_info *platform = jc_platform_current();
    const char *first_root = jc_config_sd_userdata_root();
    const char *second_root = jc_config_runtime_userdata_root();
    if (platform_runtime_primary(platform)) {
        first_root = jc_config_runtime_userdata_root();
        second_root = jc_config_sd_userdata_root();
    }

    char local_err[160] = {0};
    if (load_one(first_root, left_name, &pair->left, local_err, sizeof(local_err)) == 0) {
        pair->have_left = true;
    } else if (load_one(second_root, left_name, &pair->left,
                        local_err, sizeof(local_err)) == 0) {
        pair->have_left = true;
    } else {
        jc_config_default(&pair->left);
    }

    if (load_one(first_root, right_name, &pair->right, local_err, sizeof(local_err)) == 0) {
        pair->have_right = true;
    } else if (load_one(second_root, right_name, &pair->right,
                        local_err, sizeof(local_err)) == 0) {
        pair->have_right = true;
    } else {
        jc_config_default(&pair->right);
    }

    if (!pair->have_left || !pair->have_right) {
        set_err(err, err_size, "Loaded defaults for missing calibration files.");
        return 1;
    }
    return 0;
}

static int same_existing_file(const char *a, const char *b)
{
    struct stat sa;
    struct stat sb;
    if (stat(a, &sa) != 0 || stat(b, &sb) != 0)
        return 0;
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

static int save_to_path_with_backup(const char *path, const char *data,
                                    char *err, size_t err_size)
{
    if (access(path, F_OK) == 0) {
        char backup[JC_PATH_MAX];
        int n = snprintf(backup, sizeof(backup), "%s.bak", path);
        if (n <= 0 || (size_t)n >= sizeof(backup)) {
            set_err(err, err_size, "Backup path too long.");
            return -1;
        }
        if (copy_file_if_missing(path, backup) != 0 && access(backup, F_OK) != 0) {
            set_err(err, err_size, "Could not create backup for %s", path);
            return -1;
        }
    }
    return write_file_atomic(path, data, err, err_size);
}

/* Overwrite dst with src's contents (unlike copy_file_if_missing). Best-effort:
   returns 0 on success, -1 otherwise. */
static int copy_file_overwrite(const char *src, const char *dst)
{
    FILE *f = fopen(src, "rb");
    if (!f)
        return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    if (sz < 0 || sz > (1 << 20) || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    char *buf = (char *)malloc((size_t)sz + 1u);
    if (!buf) { fclose(f); return -1; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return -1; }
    buf[sz] = '\0';
    int rc = write_file_atomic(dst, buf, NULL, 0);
    free(buf);
    return rc;
}

/* The MLP1 stick calibration persists as a JSON profile (consumed by Leaf's
   input proxy), not the stock joypad.config. Path mirrors the proxy's reader:
   $USERDATA_PATH/input/loong-gamepad-calibration.json. */
static int jc_profile_save_mlp1(const jc_config *cfg, char *err, size_t err_size)
{
    char dir[JC_PATH_MAX];
    const char *userdata = getenv("USERDATA_PATH");
    if (userdata && userdata[0]) {
        if ((size_t)snprintf(dir, sizeof(dir), "%s/input", userdata) >= sizeof(dir)) {
            set_err(err, err_size, "Profile path too long.");
            return -1;
        }
    } else {
        snprintf(dir, sizeof(dir), "%s", jc_platform_current()->sd_userdata_root);
    }
    if (mkdir_p(dir) != 0) {
        set_err(err, err_size, "Could not create %s", dir);
        return -1;
    }

    char active[JC_PATH_MAX];
    if (join_path(active, sizeof(active), dir, "loong-gamepad-calibration.json") != 0) {
        set_err(err, err_size, "Profile path too long.");
        return -1;
    }

    /* Backups: .first.bak is first-run only (never overwritten); .previous.json
       is refreshed on every successful save. */
    if (access(active, F_OK) == 0) {
        char first[JC_PATH_MAX];
        char previous[JC_PATH_MAX];
        if ((size_t)snprintf(first, sizeof(first), "%s.first.bak", active) < sizeof(first))
            copy_file_if_missing(active, first);
        if (join_path(previous, sizeof(previous), dir,
                      "loong-gamepad-calibration.previous.json") == 0)
            copy_file_overwrite(active, previous);
    }

    int ax = cfg->x_max > -cfg->x_min ? cfg->x_max : -cfg->x_min;
    int ay = cfg->y_max > -cfg->y_min ? cfg->y_max : -cfg->y_min;
    int radius = ax > ay ? ax : ay;

    char json[1100];
    int n = snprintf(json, sizeof(json),
        "{\n"
        "  \"version\": 1,\n"
        "  \"platform\": \"mlp1\",\n"
        "  \"device_name\": \"Loong Gamepad\",\n"
        "  \"source\": { \"kind\": \"evdev\", \"abs_x\": \"ABS_X\", \"abs_y\": \"ABS_Y\", "
        "\"declared_min\": -32768, \"declared_max\": 32767 },\n"
        "  \"left\": {\n"
        "    \"x_min\": %d, \"x_max\": %d,\n"
        "    \"y_min\": %d, \"y_max\": %d,\n"
        "    \"x_zero\": %d, \"y_zero\": %d,\n"
        "    \"center_noise\": %d,\n"
        "    \"radius_p95\": %d, \"radius_max\": %d\n"
        "  },\n"
        "  \"derived\": {\n"
        "    \"normalization_policy\": \"axis_deadzone_scale\",\n"
        "    \"normalized_abs_min\": -32768, \"normalized_abs_max\": 32767,\n"
        "    \"stock_range_strategy\": \"conservative_inside_measured_extent\", "
        "\"stock_threshold\": 0.05\n"
        "  },\n"
        "  \"captured_at_unix\": %ld,\n"
        "  \"app_version\": \"0.1.0\"\n"
        "}\n",
        cfg->x_min, cfg->x_max, cfg->y_min, cfg->y_max,
        cfg->x_zero, cfg->y_zero, cfg->center_noise,
        radius, radius, (long)time(NULL));
    if (n <= 0 || (size_t)n >= sizeof(json)) {
        set_err(err, err_size, "Profile JSON too large.");
        return -1;
    }

    if (write_file_atomic(active, json, err, err_size) != 0)
        return -1;
    sync();
    return 0;
}

int jc_config_save_stick(jc_stick stick, const jc_config *cfg, char *err, size_t err_size)
{
    if (!jc_config_valid(cfg)) {
        set_err(err, err_size, "Refusing to save invalid calibration values.");
        return -1;
    }

    /* MLP1 writes a JSON calibration profile for Leaf's input proxy, not the
       stock joypad.config files. */
    if (jc_platform_current()->raw_format == JC_RAW_FORMAT_MLP1)
        return jc_profile_save_mlp1(cfg, err, err_size);

    const char *name = (stick == JC_STICK_LEFT) ? left_name : right_name;
    char data[256];
    if (jc_config_format(cfg, data, sizeof(data)) != 0) {
        set_err(err, err_size, "Could not format calibration values.");
        return -1;
    }

    const jc_platform_info *platform = jc_platform_current();
    const char *primary_root = jc_config_sd_userdata_root();
    const char *secondary_root = jc_config_runtime_userdata_root();
    if (platform_runtime_primary(platform)) {
        primary_root = jc_config_runtime_userdata_root();
        secondary_root = jc_config_sd_userdata_root();
    }

    char primary_path[JC_PATH_MAX];
    char secondary_path[JC_PATH_MAX];
    if (join_path(primary_path, sizeof(primary_path), primary_root, name) != 0 ||
        join_path(secondary_path, sizeof(secondary_path), secondary_root, name) != 0) {
        set_err(err, err_size, "Calibration path too long.");
        return -1;
    }

    if (save_to_path_with_backup(primary_path, data, err, err_size) != 0)
        return -1;
    if (!same_existing_file(primary_path, secondary_path)) {
        if (save_to_path_with_backup(secondary_path, data, err, err_size) != 0)
            return -1;
    }

    sync();
    if (jc_config_trigger_reload(err, err_size) != 0)
        return -1;
    return 0;
}

static int restore_one(const char *root, const char *name, char *err, size_t err_size)
{
    char path[JC_PATH_MAX];
    char backup[JC_PATH_MAX];
    char text[512];
    if (join_path(path, sizeof(path), root, name) != 0) {
        set_err(err, err_size, "Restore path too long.");
        return -1;
    }
    int n = snprintf(backup, sizeof(backup), "%s.bak", path);
    if (n <= 0 || (size_t)n >= sizeof(backup)) {
        set_err(err, err_size, "Backup path too long.");
        return -1;
    }
    if (access(backup, F_OK) != 0)
        return 1;
    if (read_file(backup, text, sizeof(text), err, err_size) != 0)
        return -1;
    jc_config cfg;
    if (jc_config_parse_text(text, &cfg, err, err_size) != 0)
        return -1;
    return write_file_atomic(path, text, err, err_size);
}

static int mirror_existing_config(const char *src_root, const char *dst_root,
                                  const char *name, char *err, size_t err_size)
{
    char src[JC_PATH_MAX];
    char dst[JC_PATH_MAX];
    char text[512];
    if (join_path(src, sizeof(src), src_root, name) != 0 ||
        join_path(dst, sizeof(dst), dst_root, name) != 0) {
        set_err(err, err_size, "Mirror path too long.");
        return -1;
    }
    if (same_existing_file(src, dst))
        return 0;
    if (read_file(src, text, sizeof(text), err, err_size) != 0)
        return 0;
    jc_config cfg;
    if (jc_config_parse_text(text, &cfg, err, err_size) != 0)
        return -1;
    return write_file_atomic(dst, text, err, err_size);
}

int jc_config_restore_backup(char *err, size_t err_size)
{
    const jc_platform_info *platform = jc_platform_current();
    const char *first_root = jc_config_sd_userdata_root();
    const char *second_root = jc_config_runtime_userdata_root();
    if (platform_runtime_primary(platform)) {
        first_root = jc_config_runtime_userdata_root();
        second_root = jc_config_sd_userdata_root();
    }

    int restored = 0;
    int rc = restore_one(first_root, left_name, err, err_size);
    if (rc < 0)
        return -1;
    restored += rc == 0;
    rc = restore_one(first_root, right_name, err, err_size);
    if (rc < 0)
        return -1;
    restored += rc == 0;

    char first_left[JC_PATH_MAX];
    char second_left[JC_PATH_MAX];
    if (join_path(first_left, sizeof(first_left), first_root, left_name) != 0 ||
        join_path(second_left, sizeof(second_left), second_root, left_name) != 0) {
        set_err(err, err_size, "Restore path too long.");
        return -1;
    }
    if (!same_existing_file(first_left, second_left)) {
        rc = restore_one(second_root, left_name, err, err_size);
        if (rc < 0)
            return -1;
        restored += rc == 0;
        rc = restore_one(second_root, right_name, err, err_size);
        if (rc < 0)
            return -1;
        restored += rc == 0;
    }
    if (restored == 0) {
        set_err(err, err_size, "No calibration backups found.");
        return -1;
    }
    if (mirror_existing_config(first_root, second_root, left_name, err, err_size) != 0 ||
        mirror_existing_config(first_root, second_root, right_name, err, err_size) != 0)
        return -1;
    sync();
    return jc_config_trigger_reload(err, err_size);
}

int jc_config_trigger_reload(char *err, size_t err_size)
{
    const char *path = jc_config_reload_trigger_path();
    char dir[JC_PATH_MAX];
    if (parent_dir(path, dir, sizeof(dir)) != 0 || mkdir_p(dir) != 0) {
        set_err(err, err_size, "Could not create parent directory for %s", path);
        return -1;
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        set_err(err, err_size, "Could not trigger input reload: %s", strerror(errno));
        return -1;
    }
    close(fd);
    return 0;
}

int jc_config_apply_reload(char *err, size_t err_size)
{
    if (jc_platform_current()->id != JC_PLATFORM_TG5040)
        return 0;

    const char *skip = getenv("CALIBRAGE_SKIP_INPUTD_RESTART");
    if (skip && skip[0] != '\0')
        return 0;

    sync();
    int rc = system("killall -9 trimui_inputd >/dev/null 2>&1; "
                    "sleep 0.2; "
                    "trimui_inputd >/dev/null 2>&1 & "
                    "sleep 0.6");
    if (rc != 0) {
        set_err(err, err_size, "Could not restart trimui_inputd.");
        return -1;
    }
    unlink(jc_config_reload_trigger_path());
    return 0;
}

int jc_read_joy_type(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0)
        return -1;
    const char *path = joy_type_path();
    if (!path)
        return -1;
    char text[128];
    if (read_file(path, text, sizeof(text), NULL, 0) != 0)
        return -1;
    char first[32];
    if (sscanf(text, " %31s", first) != 1)
        return -1;
    snprintf(buf, buf_size, "%s", first);
    return 0;
}
