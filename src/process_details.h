/*
 * process_details.h — Process details data for taskmgr TQt3 port.
 *
 * GTK-free helpers to populate the Process Details dialog panels.
 */

#ifndef PROCESS_DETAILS_H
#define PROCESS_DETAILS_H

#include <sys/types.h>
#include <glib.h>

typedef struct {
    gchar *details;
    gchar *cmdline;
    gchar *extra;
} ProcessDetailsBasic;

typedef struct {
    gchar *ident;
    gchar *sched;
    gchar *memio;
    gchar *files;
    gchar *advanced;
} ProcessDetailsMore;

#ifdef __cplusplus
extern "C" {
#endif

int get_process_details_basic(pid_t pid, ProcessDetailsBasic *out);
int get_process_details_more(pid_t pid, ProcessDetailsMore *out);

void free_process_details_basic(ProcessDetailsBasic *panels);
void free_process_details_more(ProcessDetailsMore *panels);

#ifdef __cplusplus
}
#endif

#endif /* PROCESS_DETAILS_H */
