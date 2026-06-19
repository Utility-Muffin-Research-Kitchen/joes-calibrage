#ifndef CALIBRAGE_H
#define CALIBRAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JC_CONFIG_FIELD_COUNT 6
#define JC_PATH_MAX 512
#define JC_RAW_MAX_PACKET 20

typedef enum {
    JC_STICK_LEFT = 0,
    JC_STICK_RIGHT = 1,
} jc_stick;

typedef enum {
    JC_PLATFORM_MY355 = 0,
    JC_PLATFORM_TG5040 = 1,
    JC_PLATFORM_TG5050 = 2,
    JC_PLATFORM_MLP1 = 3,
} jc_platform_id;

typedef enum {
    JC_RAW_FORMAT_MY355 = 0,
    JC_RAW_FORMAT_TG5040 = 1,
    JC_RAW_FORMAT_TG5050 = 2,
    JC_RAW_FORMAT_MLP1 = 3,   /* evdev EV_ABS (ABS_X/ABS_Y), not a serial stream */
} jc_raw_format;

/* MLP1 has a single analog stick (no right stick). */
#define JC_PLATFORM_HAS_RIGHT_STICK(p) ((p)->id != JC_PLATFORM_MLP1)

typedef struct {
    jc_platform_id id;
    const char *id_name;
    const char *display_name;
    int raw_min;
    int raw_max;
    int min_range;
    int default_x_min;
    int default_x_max;
    int default_y_min;
    int default_y_max;
    int default_x_zero;
    int default_y_zero;
    const char *sd_userdata_root;
    const char *runtime_userdata_root;
    const char *inputd_dir;
    const char *reload_trigger_path;
    const char *joy_type_path;
    const char *raw_combined_device;
    const char *raw_left_device;
    const char *raw_right_device;
    const char *calibration_flag_path;
    int raw_baud;
    jc_raw_format raw_format;
} jc_platform_info;

typedef struct {
    int x_min;
    int x_max;
    int y_min;
    int y_max;
    int x_zero;
    int y_zero;
} jc_config;

typedef struct {
    jc_config left;
    jc_config right;
    bool have_left;
    bool have_right;
} jc_config_pair;

typedef struct {
    int left_x;
    int left_y;
    int right_x;
    int right_y;
    int left_buttons;
    int right_buttons;
    bool valid;
    bool left_valid;
    bool right_valid;
} jc_raw_sample;

typedef struct {
    int x_min;
    int x_max;
    int y_min;
    int y_max;
    long x_zero_sum;
    long y_zero_sum;
    int zero_count;
    int range_count;
} jc_calibration_capture;

typedef struct {
    int fd;
    unsigned char packet[JC_RAW_MAX_PACKET];
    int packet_pos;
    jc_stick stick;
    bool combined;
    const char *path;
} jc_raw_stream;

typedef struct {
    jc_raw_stream streams[2];
    int stream_count;
    jc_raw_sample last;
    bool have_left;
    bool have_right;
    char error[160];
} jc_raw_reader;

const jc_platform_info *jc_platform_current(void);
const char *jc_platform_id_name(void);
const char *jc_platform_display_name(void);

void jc_config_default(jc_config *cfg);
int jc_config_parse_text(const char *text, jc_config *out, char *err, size_t err_size);
int jc_config_format(const jc_config *cfg, char *buf, size_t buf_size);
bool jc_config_valid(const jc_config *cfg);
int jc_config_load_pair(jc_config_pair *pair, char *err, size_t err_size);
int jc_config_save_stick(jc_stick stick, const jc_config *cfg, char *err, size_t err_size);
int jc_config_restore_backup(char *err, size_t err_size);
int jc_config_trigger_reload(char *err, size_t err_size);
int jc_config_apply_reload(char *err, size_t err_size);
const char *jc_config_sd_userdata_root(void);
const char *jc_config_runtime_userdata_root(void);
const char *jc_config_inputd_dir(void);
const char *jc_config_reload_trigger_path(void);
int jc_read_joy_type(char *buf, size_t buf_size);

void jc_capture_reset(jc_calibration_capture *cap);
void jc_capture_add_range(jc_calibration_capture *cap, int x, int y);
void jc_capture_add_center(jc_calibration_capture *cap, int x, int y);
int jc_capture_make_config(const jc_calibration_capture *cap, jc_config *out,
                           char *err, size_t err_size);
int jc_clamp_raw(int value);
float jc_config_normalize_axis(int raw, int min, int zero, int max);

void jc_raw_reader_init(jc_raw_reader *reader);
int jc_raw_reader_open(jc_raw_reader *reader);
int jc_raw_reader_open_stick(jc_raw_reader *reader, jc_stick stick);
void jc_raw_reader_close(jc_raw_reader *reader);
int jc_raw_reader_poll(jc_raw_reader *reader, jc_raw_sample *out);
int jc_raw_parse_byte(jc_raw_reader *reader, unsigned char byte, jc_raw_sample *out);
int jc_raw_parse_packet(const unsigned char *packet, size_t packet_size,
                        jc_stick stick, jc_raw_sample *out);
int jc_raw_begin_calibration(char *err, size_t err_size);
void jc_raw_end_calibration(void);
const char *jc_raw_device_path(void);
const char *jc_raw_left_device_path(void);
const char *jc_raw_right_device_path(void);

#endif
