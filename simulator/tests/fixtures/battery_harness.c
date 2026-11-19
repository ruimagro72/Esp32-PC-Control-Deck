/* Test harness for the battery profile block (run_extensive.sh checks "battery: ...").
 *   battery_harness roundtrip IN.json OUT.json   load + save through profile.c
 *   battery_harness arg IN.json                  print launcher_config_battery_arg(); exit 0 = printed, 1 = none
 * Build: see run_extensive.sh (profile.c config.c, no GTK). */
#include "profile.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s roundtrip IN OUT | arg IN\n", argv[0]); return 2; }
    sim_profile_t p; sim_profile_init(&p);
    sim_profile_diags_t d; sim_profile_diags_init(&d);
    if (sim_profile_load_json(&p, argv[2], &d) < 0) { fprintf(stderr, "load failed: %s\n", argv[2]); return 2; }
    int rc = 2;
    if (strcmp(argv[1], "roundtrip") == 0 && argc >= 4) {
        rc = sim_profile_save_json(&p, argv[3], &d) < 0 ? 2 : 0;
    } else if (strcmp(argv[1], "arg") == 0) {
        launcher_config_t cfg; launcher_config_defaults(&cfg);
        sim_profile_to_launcher_config(&p, &cfg);
        char buf[512];
        rc = launcher_config_battery_arg(&cfg, buf, sizeof buf);
        if (rc == 0) puts(buf);
    }
    sim_profile_diags_free(&d); sim_profile_free(&p);
    return rc;
}
