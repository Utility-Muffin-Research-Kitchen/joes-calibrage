#ifndef JOES_CALIBRAGE_I18N_H
#define JOES_CALIBRAGE_I18N_H

#include <stdbool.h>
#include <stddef.h>

/* User-facing string lookup — the same shape as Thing-File's T().
 *
 * The English string is the key. T("OK") returns the translation when a table
 * is loaded and a pointer into the literal itself otherwise, so a missing or
 * partial translation degrades to English with no call-site handling.
 *
 * Identical English needing different translations takes a context prefix that
 * is stripped from the displayed text on fallback: T("verb|Open") and
 * T("noun|Open") are distinct keys, both rendering "Open" when untranslated.
 * Only a "token|Text" prefix -- non-empty token, no spaces or tabs -- is
 * stripped, so a literal pipe in real UI text stays visible.
 *
 * Wrap ONLY user-facing text. Log messages, paths, config keys, shell command
 * names, and file extensions stay unwrapped.
 *
 * jc_i18n_init() is called once in main() before the UI starts. Before init
 * (and when no table is available) every lookup falls back to English, so the
 * toolkit-agnostic engine can call T() freely and the native tests need no
 * init. After init the module performs no I/O: T() is safe on the render path.
 */

#define T(s) (jc_i18n_t(s))

/* Resolve the language and load the first valid table. Search order:
 *   1. $USERDATA_PATH/joes-calibrage/i18n/<lang>.tsv  (live reviewer override)
 *   2. $JOES_CALIBRAGE_I18N_DIR/<lang>.tsv            (native dev build dir)
 *   3. <res_dir>/i18n/<lang>.tsv                      (packaged table, "res" by
 *      default relative to the working directory -- launch.sh cd's to the pak)
 * The language is $UMRK_LANGUAGE, then $JAWAKA_LANGUAGE; missing, empty, or
 * unknown means "en", which loads nothing. Any table problem is logged once
 * and falls back to English; localization can never block startup. Safe to
 * call again (e.g. in tests) -- it reloads from scratch. */
void jc_i18n_init(void);

/* The language actually in use, "en" when no table loaded. */
const char *jc_i18n_language(void);

/* True when the active language should get the CJK face first in the font
 * stack (zh/ja/ko). */
bool jc_i18n_is_cjk(void);

/* Translate `english`. Never returns NULL for a non-NULL argument; returns the
 * (context-stripped) English key on any miss. The pointer is owned by the
 * table or the call-site literal and stays valid for the process lifetime. */
const char *jc_i18n_t(const char *english);

#endif /* JOES_CALIBRAGE_I18N_H */
