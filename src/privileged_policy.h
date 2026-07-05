/*
 * privileged_policy.h — Decide when ephemeral root elevation is required.
 */

#ifndef PRIVILEGED_POLICY_H
#define PRIVILEGED_POLICY_H

#include <sys/types.h>
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

uid_t taskmgr_process_uid(pid_t pid);

gboolean taskmgr_kill_needs_elevation(pid_t pid);
gboolean taskmgr_priority_needs_elevation(pid_t pid, int prio);
gboolean taskmgr_autostart_needs_elevation(const char *filepath);

#ifdef __cplusplus
}
#endif

#endif /* PRIVILEGED_POLICY_H */
