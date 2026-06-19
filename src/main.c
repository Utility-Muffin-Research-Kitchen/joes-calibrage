#define CAT_IMPLEMENTATION
#include "catastrophe.h"
#define CAT_WIDGETS_IMPLEMENTATION
#include "catastrophe_widgets.h"

#include <stdio.h>

#include "calibrage.h"

void jc_ui_run(void);

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    cat_config cfg = {0};
    cfg.window_title = "Joe's Calibrage";
    /* On device the font comes from the Leaf appearance snapshot (CAT_FONT_PATH);
       NULL lets Catastrophe resolve it. */
    cfg.font_path = NULL;
    cfg.log_path = cat_resolve_log_path("joes-calibrage");
    cfg.cpu_speed = CAT_CPU_SPEED_MENU;

    if (cat_init(&cfg) != CAT_OK) {
        fprintf(stderr, "Failed to initialise Catastrophe: %s\n", cat_get_error());
        return 1;
    }

    cat_log("startup: platform=%s raw_device=%s", CAT_PLATFORM_NAME,
            jc_raw_device_path());
    jc_ui_run();
    cat_quit();
    return 0;
}
