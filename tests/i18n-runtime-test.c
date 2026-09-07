/* Native runtime test for src/i18n.c -- mirrors Thing-File's i18n-runtime-test.
 *
 * Exercises language resolution (UMRK_LANGUAGE -> JAWAKA_LANGUAGE), table
 * loading from $JOES_CALIBRAGE_I18N_DIR, English fallback, context prefixes,
 * duplicate-key and malformed-row handling, and printf-conversion validation,
 * all against throwaway TSV tables under /tmp. Runs on the host (no
 * Catastrophe); the engine and its tests are toolkit-agnostic. */

#include "i18n.h"

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

static void clear_env(void)
{
    unsetenv("UMRK_LANGUAGE");
    unsetenv("JAWAKA_LANGUAGE");
    unsetenv("USERDATA_PATH");
    unsetenv("JOES_CALIBRAGE_I18N_DIR");
    unsetenv("JOES_CALIBRAGE_RES_DIR");
}

static void test_defaults_english(void)
{
    clear_env();
    jc_i18n_init();
    CHECK(strcmp(jc_i18n_language(), "en") == 0);
    CHECK(!jc_i18n_is_cjk());
    CHECK(strcmp(jc_i18n_t("OK"), "OK") == 0);      /* English fallback */
    CHECK(strcmp(jc_i18n_t(""), "") == 0);
    CHECK(strcmp(jc_i18n_t(NULL), "") == 0);
}

static void test_load_and_lookup(void)
{
    clear_env();
    char root_template[] = "/tmp/calibrage-i18n-XXXXXX";
    char *root = mkdtemp(root_template);
    CHECK(root != NULL);
    if (!root)
        return;

    char i18n_dir[256];
    snprintf(i18n_dir, sizeof(i18n_dir), "%s/i18n", root);
    CHECK(mkdir(i18n_dir, 0755) == 0);
    char tsv[300];
    snprintf(tsv, sizeof(tsv), "%s/zh_CN.tsv", i18n_dir);

    write_text(tsv,
        "# comment line\n"
        "OK\t确定\n"
        "verb|Open\t打开\n"
        "noun|Open\t打开（名词）\n"
        "Count: %d\t计数：%s\n"        /* incompatible conversion: refused */
        "Empty\t\n"                     /* empty value: refused */
        "NoTabNoVal\n"                  /* no tab: refused */
        "Dup\t甲\n"
        "Dup\t乙\n"                     /* duplicate: key falls back to English */
        "Left\t左\n"
        "Right\t右\r\n"                 /* CRLF tolerated */
        "Center reset.%s%s\t已重置摇杆中心。%s%s\n");

    setenv("JOES_CALIBRAGE_I18N_DIR", i18n_dir, 1);
    setenv("UMRK_LANGUAGE", "zh_CN", 1);
    jc_i18n_init();

    CHECK(strcmp(jc_i18n_language(), "zh_CN") == 0);
    CHECK(jc_i18n_is_cjk());
    CHECK(strcmp(jc_i18n_t("OK"), "确定") == 0);
    CHECK(strcmp(jc_i18n_t("verb|Open"), "打开") == 0);
    CHECK(strcmp(jc_i18n_t("noun|Open"), "打开（名词）") == 0);
    CHECK(strcmp(jc_i18n_t("Count: %d"), "Count: %d") == 0);    /* refused val */
    CHECK(strcmp(jc_i18n_t("Empty"), "Empty") == 0);
    CHECK(strcmp(jc_i18n_t("NoTabNoVal"), "NoTabNoVal") == 0);
    CHECK(strcmp(jc_i18n_t("Dup"), "Dup") == 0);                /* ambiguous */
    CHECK(strcmp(jc_i18n_t("Left"), "左") == 0);
    CHECK(strcmp(jc_i18n_t("Right"), "右") == 0);               /* CRLF line */
    CHECK(strcmp(jc_i18n_t("Center reset.%s%s"), "已重置摇杆中心。%s%s") == 0);
    CHECK(strcmp(jc_i18n_t("not a key"), "not a key") == 0);    /* miss */
    CHECK(strcmp(jc_i18n_t("miss|with context"), "with context") == 0);
}

static void test_language_fallback_chain(void)
{
    /* Keep the table from the previous test on disk via the env vars, but
       re-resolve the language through the JAWAKA_LANGUAGE fallback. */
    clear_env();
    char root_template[] = "/tmp/calibrage-i18n2-XXXXXX";
    char *root = mkdtemp(root_template);
    CHECK(root != NULL);
    if (!root)
        return;
    char i18n_dir[256];
    snprintf(i18n_dir, sizeof(i18n_dir), "%s/i18n", root);
    CHECK(mkdir(i18n_dir, 0755) == 0);
    char tsv[300];
    snprintf(tsv, sizeof(tsv), "%s/zh_CN.tsv", i18n_dir);
    write_text(tsv, "OK\t确定\nCancel\t取消\n");

    /* No language at all -> English. */
    setenv("JOES_CALIBRAGE_I18N_DIR", i18n_dir, 1);
    jc_i18n_init();
    CHECK(strcmp(jc_i18n_language(), "en") == 0);
    CHECK(strcmp(jc_i18n_t("OK"), "OK") == 0);

    /* Unknown language -> English (no table for it). */
    setenv("UMRK_LANGUAGE", "fr", 1);
    jc_i18n_init();
    CHECK(strcmp(jc_i18n_language(), "en") == 0);

    /* JAWAKA_LANGUAGE is honored when UMRK_LANGUAGE is absent. */
    setenv("JAWAKA_LANGUAGE", "zh_CN", 1);
    jc_i18n_init();
    CHECK(strcmp(jc_i18n_language(), "zh_CN") == 0);
    CHECK(strcmp(jc_i18n_t("OK"), "确定") == 0);
    CHECK(strcmp(jc_i18n_t("Cancel"), "取消") == 0);

    /* UMRK_LANGUAGE wins over JAWAKA_LANGUAGE. */
    setenv("UMRK_LANGUAGE", "zh_CN", 1);
    setenv("JAWAKA_LANGUAGE", "ko", 1);
    jc_i18n_init();
    CHECK(strcmp(jc_i18n_language(), "zh_CN") == 0);
}

int main(void)
{
    test_defaults_english();
    test_load_and_lookup();
    test_language_fallback_chain();

    if (failures > 0) {
        fprintf(stderr, "%d i18n test failure(s)\n", failures);
        return 1;
    }
    printf("i18n runtime tests passed\n");
    return 0;
}
