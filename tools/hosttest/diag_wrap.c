/* The real diag.c, plus a worst-case breadcrumb fixture. Host test only. */
#include "diag.c"

/* The previous boot's breadcrumb, as bad as it can be: the ring wrapped,
 * full of characters JSON must escape (each costs 2 bytes), control bytes,
 * and page/cmd with no terminator. */
void host_breadcrumb_worst(void)
{
    memset(&s_rtc, 0, sizeof(s_rtc));
    s_rtc.magic = DIAG_MAGIC;
    s_rtc.boot = 700;
    s_rtc.head = 5 * DIAG_RING_SIZE + 17;
    for (size_t i = 0; i < DIAG_RING_SIZE; i++) {
        s_rtc.ring[i] = (i % 97 == 0) ? '\n' : (i % 2) ? '"' : '\\';
    }
    memset(s_rtc.page, 'P', sizeof(s_rtc.page));
    memset(s_rtc.cmd, '"', sizeof(s_rtc.cmd));
    seal();
}

#ifndef HOST_1_2_1
bool host_guard_set(void)
{
    return s_report_guard.magic == REPORT_GUARD_MAGIC;
}
#endif
