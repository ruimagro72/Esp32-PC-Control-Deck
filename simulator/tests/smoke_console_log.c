#include "console_log.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    clog_log_t *l = clog_new(8);
    assert(l);
    assert(clog_append_now(l, CLOG_LEVEL_INFO, CLOG_ORIGIN_EMU, "hello world"));
    assert(clog_append_now(l, CLOG_LEVEL_WARN, CLOG_ORIGIN_HOST, "warn host"));
    assert(clog_count(l) >= 2);
    clog_filter_set_substring(l, "hello");
    assert(clog_visible_count(l) >= 1);
    clog_filter_reset(l);
    char buf[256]; size_t n = clog_snapshot_text(l, true, buf, sizeof(buf));
    assert(n > 0);
    clog_clear(l);
    assert(clog_count(l) == 0);
    clog_destroy(l);
    printf("smoke_console_log ok\n");
    return 0;
}
