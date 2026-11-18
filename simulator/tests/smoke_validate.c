#include "validate.h"
#include "config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int has_code(const validate_report_t *r, const char *code) {
    for (int i = 0; i < r->count; i++)
        if (strcmp(r->items[i].code, code) == 0) return 1;
    return 0;
}

int main(void){
    launcher_config_t cfg; launcher_config_defaults(&cfg);
    launcher_config_resolve_paths(&cfg);
    validate_report_t rep; validate_report_init(&rep);
    validate_extra_t extra; memset(&extra,0,sizeof(extra));
    extra.sd_requested = cfg.storage.sd.enabled;
    (void)validate_config(&cfg, &extra, ".", VALIDATE_CHECK_RANGES|VALIDATE_CHECK_CAPS|VALIDATE_CHECK_CONFLICTS, &rep);
    assert(validate_report_count(&rep) == rep.count);

    /* SD blocks Start only when a successful probe finds no --sd (esp-emu 1.1.0 has --sd). */
    {
        emu_caps_probe_t p; emu_caps_probe_init(&p);
        validate_extra_t e; memset(&e,0,sizeof(e)); e.sd_requested = true; e.emu_caps = &p;
        validate_report_t r;

        emu_caps_probe_text(&p, "  --sd <FILE[,ro][,slot=N|,cs=PIN][,cd=PIN]>\n", "esp-emu 1.1.0");
        validate_report_init(&r);
        (void)validate_config(&cfg, &e, ".", VALIDATE_CHECK_CAPS, &r);
        assert(!has_code(&r, "CAP_SD_BLOCKED_BY_EMU"));

        emu_caps_probe_text(&p, "usage: esp-emu [OPTIONS]\n", "esp-emu 1.1.0");
        validate_report_init(&r);
        (void)validate_config(&cfg, &e, ".", VALIDATE_CHECK_CAPS, &r);
        assert(has_code(&r, "CAP_SD_BLOCKED_BY_EMU"));

        emu_caps_probe_init(&p);
        (void)emu_caps_probe_path(&p); /* empty path: probe fails, not a block */
        validate_report_init(&r);
        (void)validate_config(&cfg, &e, ".", VALIDATE_CHECK_CAPS, &r);
        assert(!has_code(&r, "CAP_SD_BLOCKED_BY_EMU"));
    }
    printf("smoke_validate ok (items=%d has_errors=%d has_warnings=%d)\n",
           rep.count,
           validate_report_has_errors(&rep) ? 1 : 0,
           validate_report_has_warnings(&rep) ? 1 : 0);
    return 0;
}
