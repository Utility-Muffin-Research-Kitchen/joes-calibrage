/* TSV table loader for Joe's Calibrage — mirrors Thing-File's i18n module.
 *
 * One bounded read at init(); after that T() is a linear lookup and performs
 * no I/O. Any problem -- missing file, malformed or duplicate or
 * format-incompatible entry -- skips that entry (or that table) and falls back
 * to English: translation trouble must never keep the calibrator from opening.
 *
 * Table format (UTF-8, one entry per line):
 *     English<TAB>translation
 * '#' lines are comments. printf conversions in a translation must match its
 * key (same count, same order, same length modifiers); a mismatch is refused
 * so vsnprintf can never misread its arguments.
 */

#include "i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JC_I18N_MAX_TABLE_BYTES (1024u * 1024u)
#define JC_I18N_MAX_LANGUAGE_LEN 15
#define JC_I18N_PATH_MAX 512

typedef struct {
    char *key;
    char *val;
} jc_i18n_entry;

static jc_i18n_entry *g_entries = NULL;
static size_t g_count = 0;
static size_t g_capacity = 0;
static bool g_loaded = false;
static char g_language[JC_I18N_MAX_LANGUAGE_LEN + 1] = "en";

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *copy = (char *)malloc(n);
    if (copy)
        memcpy(copy, s, n);
    return copy;
}

static void entries_free_all(void)
{
    for (size_t i = 0; i < g_count; i++) {
        free(g_entries[i].key);
        free(g_entries[i].val);
    }
    g_count = 0;
    g_capacity = 0;
    free(g_entries);
    g_entries = NULL;
}

/* Per-file seen set: a duplicated key (second or later occurrence) makes that
 * key fall back to English -- first occurrence wins, then any repeat marks it
 * ambiguous and drops it, mirroring Thing-File's loader. */
typedef struct {
    char **names;
    size_t count;
    size_t capacity;
} jc_i18n_seen;

static void seen_free(jc_i18n_seen *seen)
{
    for (size_t i = 0; i < seen->count; i++)
        free(seen->names[i]);
    free(seen->names);
    seen->names = NULL;
    seen->count = 0;
    seen->capacity = 0;
}

/* Records `key`. Returns true when this is the key's first occurrence in the
 * file (caller may add it to the table); false when it was already seen
 * (duplicate -- skip the row). */
static bool seen_add(jc_i18n_seen *seen, const char *key)
{
    for (size_t i = 0; i < seen->count; i++) {
        if (strcmp(seen->names[i], key) == 0)
            return false;
    }
    if (seen->count == seen->capacity) {
        size_t cap = seen->capacity ? seen->capacity * 2 : 32;
        char **grown = (char **)realloc(seen->names, cap * sizeof(*grown));
        if (!grown)
            return true; /* out of memory: treat as first-seen, entry_add will fail too */
        seen->names = grown;
        seen->capacity = cap;
    }
    seen->names[seen->count] = dup_str(key);
    if (!seen->names[seen->count])
        return true;
    seen->count++;
    return true;
}

static int entry_find(const char *key)
{
    for (size_t i = 0; i < g_count; i++) {
        if (strcmp(g_entries[i].key, key) == 0)
            return (int)i;
    }
    return -1;
}

static bool entry_add(const char *key, const char *val)
{
    if (g_count == g_capacity) {
        size_t cap = g_capacity ? g_capacity * 2 : 64;
        jc_i18n_entry *grown = (jc_i18n_entry *)realloc(g_entries, cap * sizeof(*grown));
        if (!grown)
            return false;
        g_entries = grown;
        g_capacity = cap;
    }
    g_entries[g_count].key = dup_str(key);
    if (!g_entries[g_count].key)
        return false;
    g_entries[g_count].val = dup_str(val);
    if (!g_entries[g_count].val) {
        free(g_entries[g_count].key);
        return false;
    }
    g_count++;
    return true;
}

static bool language_tag_valid(const char *lang)
{
    if (!lang || !lang[0])
        return false;
    size_t len = strlen(lang);
    if (len > JC_I18N_MAX_LANGUAGE_LEN)
        return false;
    for (size_t i = 0; i < len; i++) {
        char c = lang[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    }
    return true;
}

static bool language_is_cjk(const char *lang)
{
    return lang && (strncmp(lang, "zh", 2) == 0 ||
                    strncmp(lang, "ja", 2) == 0 ||
                    strncmp(lang, "ko", 2) == 0);
}

/* A key may carry a "context|" prefix so identical English can translate two
 * ways. Display strips it, so an untranslated T("verb|Open") still shows
 * "Open" rather than leaking the disambiguator to the user. Only a prefix
 * without spaces counts, so a literal pipe in real UI text is left alone. */
static const char *strip_context(const char *key)
{
    const char *bar = strchr(key, '|');
    if (!bar || bar == key)
        return key;
    for (const char *p = key; p < bar; p++) {
        if (*p == ' ' || *p == '\t')
            return key;
    }
    return bar + 1;
}

/* printf-conversion fingerprint of a string, for validating translations of
 * format strings. A translation that turns %zu into %s would make vsnprintf
 * read the wrong argument, so a value whose conversions do not match its
 * key's -- same count, same order, same modifiers -- is refused and the
 * English format used instead. */
static int fmt_sig(const char *s, char *out, size_t out_size)
{
    int n = 0;
    size_t o = 0;
    const char *p = s;
    while (*p) {
        if (*p != '%') {
            p++;
            continue;
        }
        p++;
        if (*p == '%') {
            p++; /* literal %% */
            continue;
        }
        if (!*p)
            return -1; /* trailing lone % */
        while (*p && strchr("-+ #0123456789.*'", *p))
            p++;
        while (*p && strchr("hlLqjzt", *p)) {
            if (o + 1 >= out_size)
                return -1;
            out[o++] = *p++;
        }
        if (!*p)
            return -1;
        if (o + 1 >= out_size)
            return -1;
        out[o++] = *p++;
        n++;
    }
    if (o >= out_size)
        return -1;
    out[o] = '\0';
    return n;
}

static bool fmt_compatible(const char *key, const char *val)
{
    char a[64];
    char b[64];
    int na = fmt_sig(key, a, sizeof(a));
    int nb = fmt_sig(val, b, sizeof(b));
    if (na < 0 || nb < 0)
        return false;
    return na == nb && strcmp(a, b) == 0;
}

/* Loads "english<TAB>translation" lines into the global table. '#' lines are
 * comments. Returns false for a file that cannot serve as a table at all
 * (silent for a missing or empty file, like Thing-File); individual bad rows
 * are skipped and only counted. */
static bool load_tsv(const char *path, const char *lang)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return false;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        fprintf(stderr, "i18n: ignoring %s: seek failed\n", path);
        return false;
    }
    long size = ftell(fp);
    if (size <= 0 || (unsigned long)size > JC_I18N_MAX_TABLE_BYTES) {
        fclose(fp);
        if ((unsigned long)size > JC_I18N_MAX_TABLE_BYTES)
            fprintf(stderr, "i18n: ignoring %s: too large\n", path);
        return false;
    }
    rewind(fp);

    char *text = (char *)malloc((size_t)size + 1);
    if (!text) {
        fclose(fp);
        fprintf(stderr, "i18n: ignoring %s: out of memory\n", path);
        return false;
    }
    size_t got = fread(text, 1, (size_t)size, fp);
    fclose(fp);
    text[got] = '\0';
    if (strlen(text) != got) {
        free(text);
        fprintf(stderr, "i18n: ignoring %s: embedded NUL\n", path);
        return false;
    }

    size_t skipped = 0;
    jc_i18n_seen seen = {0};
    char *save_line = NULL;
    char *line = strtok_r(text, "\n", &save_line);
    while (line) {
        /* trim trailing CR */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n'))
            line[--len] = '\0';
        if (line[0] != '\0' && line[0] != '#') {
            char *tab = strchr(line, '\t');
            if (tab && tab != line) {
                *tab = '\0';
                const char *key = line;
                const char *val = tab + 1;
                if (val[0] != '\0' && !strchr(val, '\t') &&
                    fmt_compatible(key, val)) {
                    if (seen_add(&seen, key)) {
                        if (!entry_add(key, val))
                            skipped++;
                    } else {
                        /* ambiguous duplicate: this key falls back to English */
                        skipped++;
                    }
                } else {
                    skipped++;
                }
            } else {
                skipped++;
            }
        }
        line = strtok_r(NULL, "\n", &save_line);
    }
    seen_free(&seen);
    free(text);

    if (g_count == 0) {
        entries_free_all();
        fprintf(stderr, "i18n: ignoring %s: no usable entries\n", path);
        return false;
    }
    if (skipped > 0)
        fprintf(stderr, "i18n: %s: skipped %zu malformed/incompatible/duplicate line(s)\n",
                path, skipped);
    return true;
}

/* Try each candidate path in order; the first valid table wins (a missing key
 * in that table falls back to English rather than chaining to a later table). */
static bool try_load(const char *lang)
{
    char path[JC_I18N_PATH_MAX];
    const char *userdata = getenv("USERDATA_PATH");
    if (userdata && userdata[0]) {
        snprintf(path, sizeof(path), "%s/joes-calibrage/i18n/%s.tsv", userdata, lang);
        if (load_tsv(path, lang))
            return true;
    }
    const char *i18n_dir = getenv("JOES_CALIBRAGE_I18N_DIR");
    if (i18n_dir && i18n_dir[0]) {
        snprintf(path, sizeof(path), "%s/%s.tsv", i18n_dir, lang);
        if (load_tsv(path, lang))
            return true;
    }
    const char *res_dir = getenv("JOES_CALIBRAGE_RES_DIR");
    const char *base = (res_dir && res_dir[0]) ? res_dir : "res";
    snprintf(path, sizeof(path), "%s/i18n/%s.tsv", base, lang);
    if (load_tsv(path, lang))
        return true;
    return false;
}

void jc_i18n_init(void)
{
    entries_free_all();
    g_loaded = false;
    strcpy(g_language, "en");

    const char *lang = getenv("UMRK_LANGUAGE");
    if (!lang || !lang[0])
        lang = getenv("JAWAKA_LANGUAGE");
    if (!language_tag_valid(lang) || strcmp(lang, "en") == 0)
        return; /* English needs no table */

    if (try_load(lang)) {
        g_loaded = true;
        strncpy(g_language, lang, JC_I18N_MAX_LANGUAGE_LEN);
        g_language[JC_I18N_MAX_LANGUAGE_LEN] = '\0';
        fprintf(stderr, "i18n: %s loaded (%zu entries)\n", g_language, g_count);
        return;
    }
    fprintf(stderr, "i18n: no table for %s; using English\n", lang);
}

const char *jc_i18n_language(void)
{
    return g_language;
}

bool jc_i18n_is_cjk(void)
{
    return language_is_cjk(g_language);
}

const char *jc_i18n_t(const char *english)
{
    if (!english)
        return "";
    if (g_loaded) {
        int index = entry_find(english);
        if (index >= 0)
            return g_entries[index].val;
    }
    return strip_context(english);
}
