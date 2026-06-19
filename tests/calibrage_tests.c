#include "calibrage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static void write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    if (!f)
        return;
    fputs(text, f);
    fclose(f);
}

static void read_text(const char *path, char *buf, size_t size)
{
    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    if (!f)
        return;
    size_t n = fread(buf, 1, size - 1, f);
    buf[n] = '\0';
    fclose(f);
}

static int file_exists(const char *path)
{
    return access(path, F_OK) == 0;
}

static void clear_path_envs(void)
{
    unsetenv("CALIBRAGE_SD_USERDATA_ROOT");
    unsetenv("CALIBRAGE_RUNTIME_USERDATA_ROOT");
    unsetenv("CALIBRAGE_INPUTD_DIR");
    unsetenv("CALIBRAGE_RELOAD_TRIGGER_PATH");
    unsetenv("CALIBRAGE_RAW_DEVICE");
    unsetenv("CALIBRAGE_RAW_LEFT_DEVICE");
    unsetenv("CALIBRAGE_RAW_RIGHT_DEVICE");
    unsetenv("CALIBRAGE_CALIBRATING_FLAG");
    unsetenv("USERDATA_PATH");
}

static void test_parse_and_format(void)
{
    clear_path_envs();
    setenv("CALIBRAGE_PLATFORM", "my355", 1);
    const char *text =
        "x_min=20\nx_max=216\ny_min=35\ny_max=216\nx_zero=116\ny_zero=130\n";
    jc_config cfg;
    char err[128] = {0};
    CHECK(jc_config_parse_text(text, &cfg, err, sizeof(err)) == 0);
    CHECK(cfg.x_min == 20);
    CHECK(cfg.x_max == 216);
    CHECK(cfg.y_zero == 130);
    char out[256];
    CHECK(jc_config_format(&cfg, out, sizeof(out)) == 0);
    CHECK(strstr(out, "x_zero=116") != NULL);

    CHECK(jc_config_parse_text("x_min=1\n", &cfg, err, sizeof(err)) != 0);
}

static void test_tg5040_defaults_and_parse(void)
{
    clear_path_envs();
    setenv("CALIBRAGE_PLATFORM", "tg5040", 1);
    jc_config cfg;
    jc_config_default(&cfg);
    CHECK(cfg.x_min == 1050);
    CHECK(cfg.x_max == 2900);
    CHECK(cfg.y_min == 1050);
    CHECK(cfg.y_max == 2900);
    CHECK(cfg.x_zero == 2150);
    CHECK(cfg.y_zero == 2150);

    const char *text =
        "x_min=1000\nx_max=3100\ny_min=900\ny_max=3200\n"
        "x_zero=2050\ny_zero=2100\n";
    char err[128] = {0};
    CHECK(jc_config_parse_text(text, &cfg, err, sizeof(err)) == 0);
    CHECK(cfg.x_max == 3100);
    CHECK(cfg.y_zero == 2100);

    CHECK(jc_config_parse_text("x_min=0\nx_max=70000\ny_min=0\ny_max=1\nx_zero=0\ny_zero=0\n",
                               &cfg, err, sizeof(err)) != 0);
}

static void test_tg5050_defaults_and_parse(void)
{
    clear_path_envs();
    setenv("CALIBRAGE_PLATFORM", "tg5050", 1);
    jc_config cfg;
    jc_config_default(&cfg);
    CHECK(cfg.x_min == 560);
    CHECK(cfg.x_max == 3600);
    CHECK(cfg.y_min == 400);
    CHECK(cfg.y_max == 3600);
    CHECK(cfg.x_zero == 2048);
    CHECK(cfg.y_zero == 2048);
    CHECK(strstr(jc_config_sd_userdata_root(), ".userdata/tg5050/joes-calibrage") != NULL);
    CHECK(strstr(jc_config_sd_userdata_root(), ".userdata/tg5040") == NULL);

    const char *text =
        "x_min=500\nx_max=3700\ny_min=390\ny_max=3650\n"
        "x_zero=2040\ny_zero=2055\n";
    char err[128] = {0};
    CHECK(jc_config_parse_text(text, &cfg, err, sizeof(err)) == 0);
    CHECK(cfg.x_max == 3700);
    CHECK(cfg.y_zero == 2055);

    CHECK(jc_config_parse_text("x_min=0\nx_max=5000\ny_min=0\ny_max=1\nx_zero=0\ny_zero=0\n",
                               &cfg, err, sizeof(err)) != 0);
}

static void test_raw_packet_parser(void)
{
    clear_path_envs();
    setenv("CALIBRAGE_PLATFORM", "my355", 1);
    unsigned char packet[6] = {0xff, 0x82, 0x74, 0x7d, 0x7b, 0xfe};
    jc_raw_sample sample = {0};
    CHECK(jc_raw_parse_packet(packet, sizeof(packet), JC_STICK_LEFT, &sample) == 0);
    CHECK(sample.left_y == 0x82);
    CHECK(sample.left_x == 0x74);
    CHECK(sample.right_y == 0x7d);
    CHECK(sample.right_x == 0x7b);
    CHECK(sample.left_valid);
    CHECK(sample.right_valid);

    jc_raw_reader reader;
    jc_raw_reader_init(&reader);
    jc_raw_sample stream_sample = {0};
    for (int i = 0; i < 6; i++)
        (void)jc_raw_parse_byte(&reader, packet[i], &stream_sample);
    CHECK(stream_sample.valid);
    CHECK(stream_sample.right_x == 0x7b);
    CHECK(stream_sample.left_valid);
    CHECK(stream_sample.right_valid);

    setenv("CALIBRAGE_PLATFORM", "tg5040", 1);
    unsigned char tg_packet[8] = {0xff, 0x00, 0x00, 0x04, 0x1a, 0x08, 0x66, 0xfe};
    memset(&sample, 0, sizeof(sample));
    CHECK(jc_raw_parse_packet(tg_packet, sizeof(tg_packet), JC_STICK_LEFT, &sample) == 0);
    CHECK(sample.left_x == 1050);
    CHECK(sample.left_y == 2150);
    CHECK(sample.left_valid);
    CHECK(!sample.right_valid);

    jc_raw_reader_init(&reader);
    memset(&stream_sample, 0, sizeof(stream_sample));
    for (int i = 0; i < 8; i++)
        (void)jc_raw_parse_byte(&reader, tg_packet[i], &stream_sample);
    CHECK(stream_sample.valid);
    CHECK(stream_sample.left_x == 1050);
    CHECK(stream_sample.left_y == 2150);
    CHECK(stream_sample.left_valid);
    CHECK(!stream_sample.right_valid);

    tg_packet[7] = 0xfd;
    CHECK(jc_raw_parse_packet(tg_packet, sizeof(tg_packet), JC_STICK_RIGHT, &sample) != 0);

    setenv("CALIBRAGE_PLATFORM", "tg5050", 1);
    unsigned char tg5050_packet[20] = {
        0xff, 0x00,
        0x01, 0x03, 0x00, 0x00,
        0x30, 0x02, 0x00, 0x08,
        0x10, 0x0e, 0x90, 0x01,
        0x00, 0x00, 0x00, 0x00,
        0xfe, 0x00,
    };
    memset(&sample, 0, sizeof(sample));
    CHECK(jc_raw_parse_packet(tg5050_packet, sizeof(tg5050_packet),
                              JC_STICK_LEFT, &sample) == 0);
    CHECK(sample.left_x == 560);
    CHECK(sample.left_y == 2048);
    CHECK(sample.left_buttons == 0x00000301);
    CHECK(sample.left_valid);
    CHECK(!sample.right_valid);

    memset(&sample, 0, sizeof(sample));
    CHECK(jc_raw_parse_packet(tg5050_packet, sizeof(tg5050_packet),
                              JC_STICK_RIGHT, &sample) == 0);
    CHECK(sample.right_x == 3600);
    CHECK(sample.right_y == 400);
    CHECK(sample.right_buttons == 0x00000301);
    CHECK(sample.right_valid);
    CHECK(!sample.left_valid);

    jc_raw_reader_init(&reader);
    memset(&stream_sample, 0, sizeof(stream_sample));
    unsigned char noisy[42];
    memset(noisy, 0, sizeof(noisy));
    noisy[0] = 0x44;
    noisy[1] = 0xff;
    noisy[2] = 0x01;
    memcpy(noisy + 22, tg5050_packet, sizeof(tg5050_packet));
    for (size_t i = 0; i < sizeof(noisy); i++)
        (void)jc_raw_parse_byte(&reader, noisy[i], &stream_sample);
    CHECK(stream_sample.valid);
    CHECK(stream_sample.left_x == 560);
    CHECK(stream_sample.left_y == 2048);

    tg5050_packet[18] = 0xfd;
    CHECK(jc_raw_parse_packet(tg5050_packet, sizeof(tg5050_packet),
                              JC_STICK_LEFT, &sample) != 0);
}

static void test_capture_math(void)
{
    clear_path_envs();
    setenv("CALIBRAGE_PLATFORM", "my355", 1);
    jc_calibration_capture cap;
    jc_capture_reset(&cap);
    for (int i = 0; i < 30; i++) {
        jc_capture_add_range(&cap, 20 + (i % 2) * 190, 35 + (i % 3) * 85);
    }
    for (int i = 0; i < 12; i++)
        jc_capture_add_center(&cap, 116 + (i % 3), 130 + (i % 2));
    jc_config cfg;
    char err[128] = {0};
    CHECK(jc_capture_make_config(&cap, &cfg, err, sizeof(err)) == 0);
    CHECK(cfg.x_min == 20);
    CHECK(cfg.x_max == 210);
    CHECK(cfg.y_min == 35);
    CHECK(cfg.y_max == 205);
    CHECK(cfg.x_zero >= 116 && cfg.x_zero <= 118);

    jc_capture_reset(&cap);
    for (int i = 0; i < 30; i++)
        jc_capture_add_range(&cap, 110, 120);
    for (int i = 0; i < 12; i++)
        jc_capture_add_center(&cap, 110, 120);
    CHECK(jc_capture_make_config(&cap, &cfg, err, sizeof(err)) != 0);

    CHECK(jc_config_normalize_axis(0, 20, 120, 230) == -1.0f);
    CHECK(jc_config_normalize_axis(255, 20, 120, 230) == 1.0f);

    setenv("CALIBRAGE_PLATFORM", "tg5040", 1);
    jc_capture_reset(&cap);
    for (int i = 0; i < 30; i++)
        jc_capture_add_range(&cap, 1000 + (i % 2) * 1900,
                             1100 + (i % 3) * 850);
    for (int i = 0; i < 12; i++)
        jc_capture_add_center(&cap, 2148 + (i % 4), 2151 + (i % 3));
    CHECK(jc_capture_make_config(&cap, &cfg, err, sizeof(err)) == 0);
    CHECK(cfg.x_min == 1000);
    CHECK(cfg.x_max == 2900);
    CHECK(cfg.y_min == 1100);
    CHECK(cfg.y_max == 2800);
    CHECK(cfg.x_zero >= 2148 && cfg.x_zero <= 2151);

    jc_capture_reset(&cap);
    for (int i = 0; i < 30; i++)
        jc_capture_add_range(&cap, 2100, 2120);
    for (int i = 0; i < 12; i++)
        jc_capture_add_center(&cap, 2100, 2120);
    CHECK(jc_capture_make_config(&cap, &cfg, err, sizeof(err)) != 0);

    setenv("CALIBRAGE_PLATFORM", "tg5050", 1);
    jc_capture_reset(&cap);
    for (int i = 0; i < 30; i++)
        jc_capture_add_range(&cap, 560 + (i % 2) * 3040,
                             400 + (i % 3) * 1600);
    for (int i = 0; i < 12; i++)
        jc_capture_add_center(&cap, 2046 + (i % 5), 2048 + (i % 4));
    CHECK(jc_capture_make_config(&cap, &cfg, err, sizeof(err)) == 0);
    CHECK(cfg.x_min == 560);
    CHECK(cfg.x_max == 3600);
    CHECK(cfg.y_min == 400);
    CHECK(cfg.y_max == 3600);
    CHECK(cfg.x_zero >= 2046 && cfg.x_zero <= 2050);

    jc_capture_reset(&cap);
    for (int i = 0; i < 30; i++)
        jc_capture_add_range(&cap, 2000, 2100);
    for (int i = 0; i < 12; i++)
        jc_capture_add_center(&cap, 2048, 2048);
    CHECK(jc_capture_make_config(&cap, &cfg, err, sizeof(err)) != 0);
}

static void make_dir(const char *path)
{
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        perror(path);
        CHECK(0);
    }
}

static void test_save_restore_and_reload(void)
{
    clear_path_envs();
    setenv("CALIBRAGE_PLATFORM", "my355", 1);
    unsetenv("CALIBRAGE_RELOAD_TRIGGER_PATH");
    char root_template[] = "/tmp/calibrage-test-XXXXXX";
    char *root = mkdtemp(root_template);
    CHECK(root != NULL);
    if (!root)
        return;

    char sd[256];
    char rt[256];
    char inputd[256];
    char sd_parent[256];
    snprintf(sd, sizeof(sd), "%s/sd/userdata", root);
    snprintf(rt, sizeof(rt), "%s/runtime", root);
    snprintf(inputd, sizeof(inputd), "%s/inputd", root);
    snprintf(sd_parent, sizeof(sd_parent), "%s/sd", root);
    make_dir(sd_parent);
    make_dir(sd);
    make_dir(rt);
    make_dir(inputd);

    setenv("CALIBRAGE_SD_USERDATA_ROOT", sd, 1);
    setenv("CALIBRAGE_RUNTIME_USERDATA_ROOT", rt, 1);
    setenv("CALIBRAGE_INPUTD_DIR", inputd, 1);

    char left_path[320];
    char right_path[320];
    char runtime_left_path[320];
    snprintf(left_path, sizeof(left_path), "%s/joypad.config", sd);
    snprintf(right_path, sizeof(right_path), "%s/joypad_right.config", sd);
    snprintf(runtime_left_path, sizeof(runtime_left_path), "%s/joypad.config", rt);
    write_text(left_path, "x_min=20\nx_max=216\ny_min=35\ny_max=216\nx_zero=116\ny_zero=130\n");
    write_text(right_path, "x_min=29\nx_max=214\ny_min=37\ny_max=210\nx_zero=128\ny_zero=117\n");
    write_text(runtime_left_path, "x_min=20\nx_max=216\ny_min=35\ny_max=216\nx_zero=116\ny_zero=130\n");

    jc_config cfg = {
        .x_min = 10, .x_max = 230, .y_min = 11,
        .y_max = 231, .x_zero = 120, .y_zero = 121,
    };
    char err[160] = {0};
    CHECK(jc_config_save_stick(JC_STICK_LEFT, &cfg, err, sizeof(err)) == 0);

    char backup[340];
    char runtime_backup[340];
    snprintf(backup, sizeof(backup), "%s.bak", left_path);
    snprintf(runtime_backup, sizeof(runtime_backup), "%s.bak", runtime_left_path);
    CHECK(file_exists(backup));
    CHECK(file_exists(runtime_backup));
    char reload[320];
    snprintf(reload, sizeof(reload), "%s/cal_update", inputd);
    CHECK(file_exists(reload));

    jc_config_pair pair;
    CHECK(jc_config_load_pair(&pair, err, sizeof(err)) == 0);
    CHECK(pair.left.x_min == 10);

    CHECK(jc_config_restore_backup(err, sizeof(err)) == 0);
    CHECK(jc_config_load_pair(&pair, err, sizeof(err)) == 0);
    CHECK(pair.left.x_min == 20);

    char runtime_text[256] = {0};
    read_text(runtime_left_path, runtime_text, sizeof(runtime_text));
    CHECK(strstr(runtime_text, "x_min=20") != NULL);
}

static void test_tg5040_save_restore_and_restart_signal(void)
{
    clear_path_envs();
    setenv("CALIBRAGE_PLATFORM", "tg5040", 1);
    unsetenv("USERDATA_PATH");

    char root_template[] = "/tmp/calibrage-tg-test-XXXXXX";
    char *root = mkdtemp(root_template);
    CHECK(root != NULL);
    if (!root)
        return;

    char mirror[256];
    char runtime[256];
    char trigger[256];
    char root_sd[256];
    snprintf(mirror, sizeof(mirror), "%s/sd/.userdata/tg5040/joes-calibrage", root);
    snprintf(runtime, sizeof(runtime), "%s/UDISK", root);
    snprintf(trigger, sizeof(trigger), "%s/trimui_inputd_restart", root);
    snprintf(root_sd, sizeof(root_sd), "%s/sd", root);
    make_dir(root_sd);
    make_dir(runtime);

    setenv("CALIBRAGE_SD_USERDATA_ROOT", mirror, 1);
    setenv("CALIBRAGE_RUNTIME_USERDATA_ROOT", runtime, 1);
    setenv("CALIBRAGE_RELOAD_TRIGGER_PATH", trigger, 1);

    char runtime_left[320];
    char runtime_right[320];
    snprintf(runtime_left, sizeof(runtime_left), "%s/joypad.config", runtime);
    snprintf(runtime_right, sizeof(runtime_right), "%s/joypad_right.config", runtime);
    write_text(runtime_left,
               "x_min=1050\nx_max=2900\ny_min=1050\ny_max=2900\n"
               "x_zero=2150\ny_zero=2150\n");
    write_text(runtime_right,
               "x_min=1100\nx_max=2800\ny_min=1000\ny_max=3000\n"
               "x_zero=2100\ny_zero=2200\n");

    jc_config cfg = {
        .x_min = 900, .x_max = 3100, .y_min = 950,
        .y_max = 3050, .x_zero = 2050, .y_zero = 2100,
    };
    char err[160] = {0};
    CHECK(jc_config_save_stick(JC_STICK_LEFT, &cfg, err, sizeof(err)) == 0);

    char mirror_left[360];
    snprintf(mirror_left, sizeof(mirror_left), "%s/joypad.config", mirror);
    CHECK(file_exists(runtime_left));
    CHECK(file_exists(mirror_left));
    CHECK(file_exists(trigger));

    char runtime_backup[340];
    snprintf(runtime_backup, sizeof(runtime_backup), "%s.bak", runtime_left);
    CHECK(file_exists(runtime_backup));

    jc_config_pair pair;
    CHECK(jc_config_load_pair(&pair, err, sizeof(err)) == 0);
    CHECK(pair.left.x_min == 900);
    CHECK(pair.right.x_zero == 2100);

    CHECK(jc_config_restore_backup(err, sizeof(err)) == 0);
    CHECK(jc_config_load_pair(&pair, err, sizeof(err)) == 0);
    CHECK(pair.left.x_min == 1050);

    char mirror_text[256] = {0};
    read_text(mirror_left, mirror_text, sizeof(mirror_text));
    CHECK(strstr(mirror_text, "x_min=1050") != NULL);
}

static void test_tg5050_save_restore_and_cal_update(void)
{
    clear_path_envs();
    setenv("CALIBRAGE_PLATFORM", "tg5050", 1);

    char root_template[] = "/tmp/calibrage-tg5050-test-XXXXXX";
    char *root = mkdtemp(root_template);
    CHECK(root != NULL);
    if (!root)
        return;

    char mirror[256];
    char runtime[256];
    char trigger[256];
    char restart[256];
    char root_sd[256];
    snprintf(mirror, sizeof(mirror), "%s/sd/.userdata/tg5050/joes-calibrage", root);
    snprintf(runtime, sizeof(runtime), "%s/UDISK", root);
    snprintf(trigger, sizeof(trigger), "%s/trimui_inputd/cal_update", root);
    snprintf(restart, sizeof(restart), "%s/trimui_inputd_restart", root);
    snprintf(root_sd, sizeof(root_sd), "%s/sd", root);
    make_dir(root_sd);
    make_dir(runtime);

    setenv("CALIBRAGE_SD_USERDATA_ROOT", mirror, 1);
    setenv("CALIBRAGE_RUNTIME_USERDATA_ROOT", runtime, 1);
    setenv("CALIBRAGE_RELOAD_TRIGGER_PATH", trigger, 1);

    char runtime_left[320];
    char runtime_right[320];
    snprintf(runtime_left, sizeof(runtime_left), "%s/joypad.config", runtime);
    snprintf(runtime_right, sizeof(runtime_right), "%s/joypad_right.config", runtime);
    write_text(runtime_left,
               "x_min=560\nx_max=3600\ny_min=400\ny_max=3600\n"
               "x_zero=2048\ny_zero=2048\n");
    write_text(runtime_right,
               "x_min=600\nx_max=3500\ny_min=450\ny_max=3550\n"
               "x_zero=2000\ny_zero=2100\n");

    jc_config cfg = {
        .x_min = 520, .x_max = 3700, .y_min = 420,
        .y_max = 3650, .x_zero = 2040, .y_zero = 2050,
    };
    char err[160] = {0};
    CHECK(jc_config_save_stick(JC_STICK_LEFT, &cfg, err, sizeof(err)) == 0);

    char mirror_left[360];
    snprintf(mirror_left, sizeof(mirror_left), "%s/joypad.config", mirror);
    CHECK(file_exists(runtime_left));
    CHECK(file_exists(mirror_left));
    CHECK(file_exists(trigger));
    CHECK(!file_exists(restart));
    CHECK(strstr(mirror_left, ".userdata/tg5050/joes-calibrage") != NULL);
    CHECK(strstr(mirror_left, ".userdata/tg5040") == NULL);

    char runtime_backup[340];
    snprintf(runtime_backup, sizeof(runtime_backup), "%s.bak", runtime_left);
    CHECK(file_exists(runtime_backup));

    jc_config_pair pair;
    CHECK(jc_config_load_pair(&pair, err, sizeof(err)) == 0);
    CHECK(pair.left.x_min == 520);
    CHECK(pair.right.x_zero == 2000);

    CHECK(jc_config_restore_backup(err, sizeof(err)) == 0);
    CHECK(jc_config_load_pair(&pair, err, sizeof(err)) == 0);
    CHECK(pair.left.x_min == 560);

    char mirror_text[256] = {0};
    read_text(mirror_left, mirror_text, sizeof(mirror_text));
    CHECK(strstr(mirror_text, "x_min=560") != NULL);
}

static void test_mlp1_signed_defaults_capture_and_normalize(void)
{
    clear_path_envs();
    setenv("CALIBRAGE_PLATFORM", "mlp1", 1);

    /* Single analog stick, signed 16-bit axes. */
    CHECK(!JC_PLATFORM_HAS_RIGHT_STICK(jc_platform_current()));
    CHECK(jc_platform_current()->raw_min == -32768);
    CHECK(jc_platform_current()->raw_max == 32767);

    jc_config cfg;
    jc_config_default(&cfg);
    CHECK(cfg.x_min == -22000);
    CHECK(cfg.x_max == 22000);
    CHECK(cfg.y_min == -22000);
    CHECK(cfg.y_max == 22000);
    CHECK(cfg.x_zero == 0);
    CHECK(cfg.y_zero == 0);

    /* Parse signed (negative) values. */
    const char *text =
        "x_min=-22410\nx_max=24067\ny_min=-21909\ny_max=23520\n"
        "x_zero=0\ny_zero=0\n";
    char err[128] = {0};
    CHECK(jc_config_parse_text(text, &cfg, err, sizeof(err)) == 0);
    CHECK(cfg.x_min == -22410);
    CHECK(cfg.x_max == 24067);
    CHECK(cfg.y_max == 23520);

    /* Capture math over a signed range that straddles zero. */
    jc_calibration_capture cap;
    jc_capture_reset(&cap);
    for (int i = 0; i < 40; i++) {
        int x = (i % 2 == 0) ? -21000 : 23000;
        int y = (i % 2 == 0) ? -20000 : 22000;
        jc_capture_add_range(&cap, x, y);
    }
    for (int i = 0; i < 12; i++)
        jc_capture_add_center(&cap, (i % 3) - 1, (i % 2));
    CHECK(jc_capture_make_config(&cap, &cfg, err, sizeof(err)) == 0);
    CHECK(cfg.x_min == -21000);
    CHECK(cfg.x_max == 23000);
    CHECK(cfg.y_min == -20000);
    CHECK(cfg.y_max == 22000);
    CHECK(cfg.x_zero >= -1 && cfg.x_zero <= 1);
    CHECK(cfg.y_zero >= 0 && cfg.y_zero <= 1);

    /* Reject a throw narrower than the minimum acceptable span (12000). */
    jc_capture_reset(&cap);
    for (int i = 0; i < 30; i++) {
        int v = (i % 2 == 0) ? -3000 : 3000;   /* span 6000 < 12000 */
        jc_capture_add_range(&cap, v, v);
    }
    for (int i = 0; i < 12; i++)
        jc_capture_add_center(&cap, 0, 0);
    CHECK(jc_capture_make_config(&cap, &cfg, err, sizeof(err)) != 0);

    /* Normalization maps the signed extremes to the full -1..1 range. */
    CHECK(jc_config_normalize_axis(-32768, -22000, 0, 22000) == -1.0f);
    CHECK(jc_config_normalize_axis(22000, -22000, 0, 22000) == 1.0f);
    CHECK(jc_config_normalize_axis(0, -22000, 0, 22000) == 0.0f);
}

static void test_center_stability(void)
{
    clear_path_envs();
    setenv("CALIBRAGE_PLATFORM", "mlp1", 1);
    jc_calibration_capture cap;
    jc_capture_reset(&cap);
    for (int i = 0; i < 40; i++) {
        int x = (i % 2 == 0) ? -21000 : 23000;
        int y = (i % 2 == 0) ? -20000 : 22000;
        jc_capture_add_range(&cap, x, y);
    }
    /* Center step begins while the stick is still traveling back from the edge:
       a few far-apart transition samples, then a tight rest near (-1000, 500). */
    int transition[] = { 23000, 17000, 11000, 5000 };
    for (size_t i = 0; i < sizeof(transition) / sizeof(transition[0]); i++)
        jc_capture_add_center(&cap, transition[i], transition[i]);
    for (int i = 0; i < 30; i++)
        jc_capture_add_center(&cap, -1000 + (i % 3) - 1, 500 + (i % 2));

    jc_config cfg;
    char err[128] = {0};
    CHECK(jc_capture_make_config(&cap, &cfg, err, sizeof(err)) == 0);
    /* Center reflects the rest, not the transition; jitter is small. */
    CHECK(cfg.x_zero >= -1001 && cfg.x_zero <= -999);
    CHECK(cfg.y_zero >= 500 && cfg.y_zero <= 501);
    CHECK(cfg.center_noise <= 30);
}

static void test_mlp1_profile_save_and_backup(void)
{
    clear_path_envs();
    setenv("CALIBRAGE_PLATFORM", "mlp1", 1);
    char root_template[] = "/tmp/calibrage-mlp1-XXXXXX";
    char *root = mkdtemp(root_template);
    CHECK(root != NULL);
    if (!root)
        return;
    setenv("USERDATA_PATH", root, 1);

    jc_config cfg = { .x_min = -21000, .x_max = 23000, .y_min = -20000,
                      .y_max = 22000, .x_zero = 0, .y_zero = -1000,
                      .center_noise = 300 };
    char err[160] = {0};
    CHECK(jc_config_save_stick(JC_STICK_LEFT, &cfg, err, sizeof(err)) == 0);

    char active[512];
    snprintf(active, sizeof(active), "%s/input/loong-gamepad-calibration.json", root);
    CHECK(file_exists(active));
    char buf[2048] = {0};
    read_text(active, buf, sizeof(buf));
    CHECK(strstr(buf, "\"x_min\": -21000") != NULL);
    CHECK(strstr(buf, "\"x_zero\": 0") != NULL);
    CHECK(strstr(buf, "\"y_zero\": -1000") != NULL);
    CHECK(strstr(buf, "\"center_noise\": 300") != NULL);
    CHECK(strstr(buf, "\"normalized_abs_max\": 32767") != NULL);
    CHECK(strstr(buf, "\"platform\": \"mlp1\"") != NULL);

    /* Second save: .first.bak keeps the original, active is updated. */
    jc_config cfg2 = cfg;
    cfg2.x_min = -19000;
    CHECK(jc_config_save_stick(JC_STICK_LEFT, &cfg2, err, sizeof(err)) == 0);

    char first[600];
    char prev[600];
    snprintf(first, sizeof(first), "%s.first.bak", active);
    snprintf(prev, sizeof(prev), "%s/input/loong-gamepad-calibration.previous.json", root);
    CHECK(file_exists(first));
    CHECK(file_exists(prev));
    char fbuf[2048] = {0};
    read_text(first, fbuf, sizeof(fbuf));
    CHECK(strstr(fbuf, "\"x_min\": -21000") != NULL);   /* original preserved */
    char abuf[2048] = {0};
    read_text(active, abuf, sizeof(abuf));
    CHECK(strstr(abuf, "\"x_min\": -19000") != NULL);   /* active updated */

    unsetenv("USERDATA_PATH");
}

int main(void)
{
    test_parse_and_format();
    test_tg5040_defaults_and_parse();
    test_tg5050_defaults_and_parse();
    test_raw_packet_parser();
    test_capture_math();
    test_mlp1_signed_defaults_capture_and_normalize();
    test_center_stability();
    test_mlp1_profile_save_and_backup();
    test_save_restore_and_reload();
    test_tg5040_save_restore_and_restart_signal();
    test_tg5050_save_restore_and_cal_update();

    if (failures) {
        fprintf(stderr, "%d test failure(s)\n", failures);
        return 1;
    }
    printf("calibrage tests passed\n");
    return 0;
}
