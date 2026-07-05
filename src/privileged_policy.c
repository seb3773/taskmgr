/*
 * privileged_policy.c — Decide when ephemeral root elevation is required.
 */

#include "privileged_policy.h"
#include "root_credential_vault.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

uid_t taskmgr_process_uid(pid_t pid)
{
    if (pid <= 0)
        return (uid_t)-1;

    char path[64];
    g_snprintf(path, sizeof(path), "/proc/%d/status", (int)pid);

    FILE *fp = fopen(path, "r");
    if (!fp)
        return (uid_t)-1;

    char line[256];
    uid_t uid = (uid_t)-1;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "Uid:", 4) == 0) {
            unsigned long val = 0;
            if (sscanf(line + 4, "%lu", &val) == 1)
                uid = (uid_t)val;
            break;
        }
    }
    fclose(fp);
    return uid;
}

gboolean taskmgr_kill_needs_elevation(pid_t pid)
{
    if (root_mode_is_active())
        return FALSE;

    uid_t proc_uid = taskmgr_process_uid(pid);
    if (proc_uid == (uid_t)-1)
        return FALSE;

    return proc_uid != geteuid();
}

gboolean taskmgr_priority_needs_elevation(pid_t pid, int prio)
{
    if (root_mode_is_active())
        return FALSE;

    if (prio < 0)
        return TRUE;

    uid_t proc_uid = taskmgr_process_uid(pid);
    if (proc_uid == (uid_t)-1)
        return prio < 0;

    return proc_uid != geteuid();
}

gboolean taskmgr_autostart_needs_elevation(const char *filepath)
{
    if (!filepath || !*filepath)
        return FALSE;
    if (root_mode_is_active())
        return FALSE;

    if (access(filepath, W_OK) == 0)
        return FALSE;

    return TRUE;
}
