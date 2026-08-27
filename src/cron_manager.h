/*
 * cron_manager.h — Backend subsystem for scanning, parsing, and managing
 * Linux cron jobs, system crontabs, and scheduled scripts.
 */

#ifndef CRON_MANAGER_H
#define CRON_MANAGER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CRON_SOURCE_USER = 0,       /* /var/spool/cron/crontabs/<user> */
    CRON_SOURCE_SYSTEM_CRONTAB, /* /etc/crontab */
    CRON_SOURCE_CRON_D,         /* /etc/cron.d/<file> */
    CRON_SOURCE_CRON_HOURLY,    /* /etc/cron.hourly/<file> */
    CRON_SOURCE_CRON_DAILY,     /* /etc/cron.daily/<file> */
    CRON_SOURCE_CRON_WEEKLY,    /* /etc/cron.weekly/<file> */
    CRON_SOURCE_CRON_MONTHLY    /* /etc/cron.monthly/<file> */
} CronSourceType;

typedef struct {
    char *name;            /* Descriptive name (from comment, script name, or command) */
    char *command;         /* Command or script path */
    char *schedule_raw;    /* Raw cron expression ("0 3 * * *", "@daily", etc.) */
    char *schedule_human;  /* Human-friendly translation ("Every day at 03:00") */
    char *user;            /* Target username ("root", "cdef", etc.) */
    char *source_path;     /* Source configuration or script file */
    int enabled;           /* 1 if active, 0 if disabled (# [DISABLED] or non-executable) */
    int line_number;       /* 1-based line number in source file (or -1 for script files) */
    CronSourceType source_type;
} CronEntry;

/*
 * Scans all available cron sources and returns an allocated array of CronEntry.
 * Returns NULL if no entries found. Sets *count to the number of entries.
 */
CronEntry* get_cron_entries(size_t *count);

/*
 * Frees memory allocated for an array of CronEntry.
 */
void free_cron_entries(CronEntry *entries, size_t count);

/*
 * Toggles an entry between Enabled (1) and Disabled (0).
 * Returns 0 on success, non-zero on error with optional error message.
 */
int toggle_cron_entry(const CronEntry *entry, int enable, char *errmsg, size_t errmsg_len);
int toggle_cron_entry_with_password(const char *password, const CronEntry *entry, int enable, char *errmsg, size_t errmsg_len);

/*
 * Deletes a scheduled task entry.
 * Returns 0 on success, non-zero on error with optional error message.
 */
int delete_cron_entry(const CronEntry *entry, char *errmsg, size_t errmsg_len);
int delete_cron_entry_with_password(const char *password, const CronEntry *entry, char *errmsg, size_t errmsg_len);

/*
 * Converts a raw cron schedule expression (e.g. "0 3 * * *") to human-readable text.
 * Caller must free the returned string with g_free / free.
 */
char* cron_format_schedule_human(const char *raw_expr);

/*
 * Runs a cron command detached using /bin/sh -c.
 * Returns 0 on success, non-zero on fork/exec failure.
 */
int run_cron_command(const char *cmd);

#ifdef __cplusplus
}
#endif

#endif // CRON_MANAGER_H
