/* The real report_client.c, driven directly (no task). Host test only. */
#include "report_client.c"

int host_send(const char *trigger)
{
    if (s_req == NULL) {
        report_client_init();
    }
    memset(s_req, 0, sizeof(*s_req));
    strlcpy(s_req->trigger, trigger, sizeof(s_req->trigger));
    s_req->boot_count = 701;
    memset(s_req->reset_reason, '"', sizeof(s_req->reset_reason) - 1);
    memset(s_req->prev_reset_reason, '\\', sizeof(s_req->prev_reset_reason) - 1);
    memset(s_req->last_fault, 0x01, sizeof(s_req->last_fault) - 1);   /* \u0001: 6 bytes each */
    memset(s_req->last_cmd, 0x1f, sizeof(s_req->last_cmd) - 1);
    s_req->event_count = REPORT_MAX_EVENTS;
    for (int i = 0; i < REPORT_MAX_EVENTS; i++) {
        s_req->events[i].uptime_s = 4000000000u;
        memset(s_req->events[i].type, 0x02, sizeof(s_req->events[i].type) - 1);
        memset(s_req->events[i].message, 0x03, sizeof(s_req->events[i].message) - 1);
    }
    send_report();
    report_status_t st;
    report_client_get_status(&st);
    printf("  report_client: state %d, \"%s\"\n", (int)st.state, st.message);
    return (int)st.state;
}
