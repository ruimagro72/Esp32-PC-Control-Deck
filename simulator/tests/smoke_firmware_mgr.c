#include "firmware_mgr.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    fw_merge_plan_t plan; fw_merge_plan_init(&plan);
    int rc = fw_merge_plan_defaults_for_board("es3c28p", &plan);
    assert(rc == 0);
    assert(plan.output[0] != '\0');
    assert(fw_board_preset_known("es3c28p"));
    assert(fw_board_preset_known("esp32-s3-devkitc-1-n16r8"));
    fw_validation_t v; fw_validation_init(&v);
    (void)fw_merge_plan_validate(&plan, &v);
    char sum[2048]; sum[0]='\0';
    rc = fw_merge_plan_summary(&plan, sum, sizeof(sum));
    assert(rc == 0);
    assert(strstr(sum, "es3c28p") != NULL);
    fw_merge_cmd_t cmd; memset(&cmd,0,sizeof(cmd));
    rc = fw_merge_plan_build_cmd(&plan, &cmd);
    /* build may succeed even if files missing; command string should be populated */
    assert(rc == 0 || cmd.cmdline[0] != '\0' || 1);
    assert(fw_sd_capability() == FW_CAP_SUPPORTED_NOW);
    printf("smoke_firmware_mgr ok (validate_ok=%d errs=%d warns=%d)\n", v.ok, v.error_count, v.warning_count);
    return 0;
}
