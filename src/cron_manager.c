/*
 * cron_manager.c — Backend implementation for Linux cron jobs and scheduled tasks.
 */

#include "cron_manager.h"
#include "privileged_exec.h"
#include "root_credential_vault.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <pwd.h>
#include <errno.h>

#define DISABLED_PREFIX "# [DISABLED] "

typedef struct {
    CronEntry *entries;
    size_t count;
    size_t capacity;
} CronList;

static void cron_list_init(CronList *list)
{
    list->entries = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void cron_list_add(CronList *list,
                          const char *name,
                          const char *command,
                          const char *schedule_raw,
                          const char *schedule_human,
                          const char *user,
                          const char *source_path,
                          int enabled,
                          int line_number,
                          CronSourceType source_type)
{
    if (list->count >= list->capacity) {
        size_t new_cap = list->capacity == 0 ? 16 : list->capacity * 2;
        list->entries = (CronEntry *)g_realloc(list->entries, new_cap * sizeof(CronEntry));
        list->capacity = new_cap;
    }

    CronEntry *e = &list->entries[list->count++];
    e->name = g_strdup(name ? name : "");
    e->command = g_strdup(command ? command : "");
    e->schedule_raw = g_strdup(schedule_raw ? schedule_raw : "");
    e->schedule_human = g_strdup(schedule_human ? schedule_human : "");
    e->user = g_strdup(user ? user : "root");
    e->source_path = g_strdup(source_path ? source_path : "");
    e->enabled = enabled;
    e->line_number = line_number;
    e->source_type = source_type;
}

void free_cron_entries(CronEntry *entries, size_t count)
{
    if (!entries) return;
    for (size_t i = 0; i < count; ++i) {
        g_free(entries[i].name);
        g_free(entries[i].command);
        g_free(entries[i].schedule_raw);
        g_free(entries[i].schedule_human);
        g_free(entries[i].user);
        g_free(entries[i].source_path);
    }
    g_free(entries);
}

/* Helper to trim leading & trailing whitespace */
static char *trim_whitespace(char *str)
{
    if (!str) return NULL;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

/* Day of week helper */
static const char* dow_to_name(int d)
{
    switch (d) {
        case 0: case 7: return "Sunday";
        case 1: return "Monday";
        case 2: return "Tuesday";
        case 3: return "Wednesday";
        case 4: return "Thursday";
        case 5: return "Friday";
        case 6: return "Saturday";
        default: return "";
    }
}

char* cron_format_schedule_human(const char *raw_expr)
{
    if (!raw_expr || !*raw_expr)
        return g_strdup("Manual");

    char copy[256];
    g_strlcpy(copy, raw_expr, sizeof(copy));
    char *trimmed = trim_whitespace(copy);

    if (g_ascii_strcasecmp(trimmed, "@reboot") == 0)
        return g_strdup("At system boot");
    if (g_ascii_strcasecmp(trimmed, "@yearly") == 0 || g_ascii_strcasecmp(trimmed, "@annually") == 0)
        return g_strdup("Every year (Jan 1, 00:00)");
    if (g_ascii_strcasecmp(trimmed, "@monthly") == 0)
        return g_strdup("Every month (1st, 00:00)");
    if (g_ascii_strcasecmp(trimmed, "@weekly") == 0)
        return g_strdup("Every Sunday at 00:00");
    if (g_ascii_strcasecmp(trimmed, "@daily") == 0 || g_ascii_strcasecmp(trimmed, "@midnight") == 0)
        return g_strdup("Every day at 00:00");
    if (g_ascii_strcasecmp(trimmed, "@hourly") == 0)
        return g_strdup("Every hour (at min 0)");

    /* Tokenize 5 standard fields */
    char min[32] = {0}, hr[32] = {0}, dom[32] = {0}, mon[32] = {0}, dow[32] = {0};
    int n = sscanf(trimmed, "%31s %31s %31s %31s %31s", min, hr, dom, mon, dow);
    if (n == 5) {
        /* * * * * * -> Every minute */
        if (strcmp(min, "*") == 0 && strcmp(hr, "*") == 0 && strcmp(dom, "*") == 0 &&
            strcmp(mon, "*") == 0 && strcmp(dow, "*") == 0) {
            return g_strdup("Every minute");
        }

        /* Every N minutes */
        if (strncmp(min, "*/", 2) == 0 && strcmp(hr, "*") == 0 && strcmp(dom, "*") == 0 &&
            strcmp(mon, "*") == 0 && strcmp(dow, "*") == 0) {
            return g_strdup_printf("Every %s minutes", min + 2);
        }

        /* 0 * * * * -> Every hour */
        if (strcmp(min, "0") == 0 && strcmp(hr, "*") == 0 && strcmp(dom, "*") == 0 &&
            strcmp(mon, "*") == 0 && strcmp(dow, "*") == 0) {
            return g_strdup("Every hour");
        }

        /* 0 */
        if (strcmp(min, "0") == 0 && strncmp(hr, "*/", 2) == 0 && strcmp(dom, "*") == 0 &&
            strcmp(mon, "*") == 0 && strcmp(dow, "*") == 0) {
            return g_strdup_printf("Every %s hours", hr + 2);
        }

        /* Fixed hour & min: M H * * * -> Every day at HH:MM */
        int m_val = -1, h_val = -1, dow_val = -1, dom_val = -1;
        if (isdigit(min[0]) && sscanf(min, "%d", &m_val) == 1 &&
            isdigit(hr[0]) && sscanf(hr, "%d", &h_val) == 1) {

            /* Specific day of week: M H * * D */
            if (strcmp(dom, "*") == 0 && strcmp(mon, "*") == 0 && isdigit(dow[0]) &&
                sscanf(dow, "%d", &dow_val) == 1) {
                const char *dname = dow_to_name(dow_val);
                if (dname[0])
                    return g_strdup_printf("Every %s at %02d:%02d", dname, h_val, m_val);
            }

            /* Weekdays: M H * * 1-5 */
            if (strcmp(dom, "*") == 0 && strcmp(mon, "*") == 0 && strcmp(dow, "1-5") == 0) {
                return g_strdup_printf("Mon-Fri at %02d:%02d", h_val, m_val);
            }

            /* Specific day of month: M H D * * */
            if (isdigit(dom[0]) && sscanf(dom, "%d", &dom_val) == 1 &&
                strcmp(mon, "*") == 0 && strcmp(dow, "*") == 0) {
                return g_strdup_printf("Day %d of month at %02d:%02d", dom_val, h_val, m_val);
            }

            /* Every day at HH:MM */
            if (strcmp(dom, "*") == 0 && strcmp(mon, "*") == 0 && strcmp(dow, "*") == 0) {
                return g_strdup_printf("Every day at %02d:%02d", h_val, m_val);
            }
        }
    }

    return g_strdup_printf("%s", trimmed);
}

/* Checks if a comment is just a standard crontab syntax template line */
static int is_syntax_template_comment(const char *cmt)
{
    if (!cmt) return 1;
    if (strstr(cmt, "m h  dom mon dow") || strstr(cmt, "m h dom mon dow") ||
        strstr(cmt, "min hr dom mon dow") || strstr(cmt, "user-name command") ||
        strstr(cmt, "To run as") || strstr(cmt, "Edit this file"))
        return 1;
    return 0;
}

/* Derives a clean, readable name from a command line if no comment was provided */
static char* derive_name_from_command(const char *cmd)
{
    if (!cmd || !*cmd) return g_strdup("Task");
    const char *p = cmd;
    while (isspace((unsigned char)*p)) p++;

    /* Special patterns for system runners */
    if (strstr(p, "run-parts") && strstr(p, "/etc/cron.hourly"))
        return g_strdup("Hourly Cron Runner");
    if (strstr(p, "run-parts") && strstr(p, "/etc/cron.daily"))
        return g_strdup("Daily Cron Runner");
    if (strstr(p, "run-parts") && strstr(p, "/etc/cron.weekly"))
        return g_strdup("Weekly Cron Runner");
    if (strstr(p, "run-parts") && strstr(p, "/etc/cron.monthly"))
        return g_strdup("Monthly Cron Runner");

    /* Skip environment variable prefixes (e.g. USR=cdef, SERVICE_MODE=1) */
    while (*p) {
        const char *eq = strchr(p, '=');
        const char *sp = strchr(p, ' ');
        if (eq && sp && eq < sp) {
            p = sp + 1;
            while (isspace((unsigned char)*p)) p++;
        } else {
            break;
        }
    }

    /* Skip leading shell wrappers: [ -x ... ] && or test -x ... || */
    if (g_str_has_prefix(p, "test ") || g_str_has_prefix(p, "[ ") || g_str_has_prefix(p, "if ")) {
        const char *target = strstr(p, "/usr/");
        if (!target) target = strstr(p, "/bin/");
        if (!target) target = strstr(p, "/sbin/");
        if (!target) target = strstr(p, "/etc/");
        if (target)
            p = target;
    }

    /* Find end of executable path */
    const char *end = p;
    while (*end && !isspace((unsigned char)*end) && *end != ';' && *end != '&' && *end != '|') end++;

    char buf[256];
    size_t len = (size_t)(end - p);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, p, len);
    buf[len] = '\0';

    char *slash = strrchr(buf, '/');
    if (slash && *(slash + 1))
        return g_strdup(slash + 1);

    return g_strdup(buf[0] ? buf : "Task");
}

/* Parse a crontab formatted stream/file */
static void parse_crontab_stream(CronList *list,
                                 FILE *fp,
                                 const char *source_path,
                                 const char *default_user,
                                 int has_user_field,
                                 CronSourceType source_type)
{
    char line[2048];
    char last_comment[256] = {0};
    int line_num = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_num++;

        /* Strip trailing \r\n */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        char *p = trim_whitespace(line);
        if (!*p) {
            last_comment[0] = '\0';
            continue;
        }

        int is_disabled = 0;
        if (g_str_has_prefix(p, "# [DISABLED] ")) {
            is_disabled = 1;
            p = trim_whitespace(p + strlen("# [DISABLED] "));
        } else if (g_str_has_prefix(p, "#[DISABLED] ")) {
            is_disabled = 1;
            p = trim_whitespace(p + strlen("#[DISABLED] "));
        } else if (g_str_has_prefix(p, "#DISABLED#")) {
            is_disabled = 1;
            p = trim_whitespace(p + strlen("#DISABLED#"));
        } else if (g_str_has_prefix(p, "# DISABLED:")) {
            is_disabled = 1;
            p = trim_whitespace(p + strlen("# DISABLED:"));
        }

        /* If regular comment */
        if (*p == '#') {
            char *cmt = p + 1;
            while (isspace((unsigned char)*cmt)) cmt++;
            if (*cmt)
                g_strlcpy(last_comment, cmt, sizeof(last_comment));
            continue;
        }

        /* Ignore environment variable definitions: KEY=VALUE */
        char *eq = strchr(p, '=');
        if (eq) {
            char *sp = strchr(p, ' ');
            if (!sp || eq < sp) {
                last_comment[0] = '\0';
                continue;
            }
        }

        char schedule_raw[128] = {0};
        char user_field[64] = {0};
        char *cmd_start = NULL;

        if (*p == '@') {
            /* @alias [user] cmd */
            char alias[32] = {0};
            if (has_user_field) {
                char u[64] = {0};
                int consumed = 0;
                if (sscanf(p, "%31s %63s%n", alias, u, &consumed) >= 2) {
                    g_strlcpy(schedule_raw, alias, sizeof(schedule_raw));
                    g_strlcpy(user_field, u, sizeof(user_field));
                    cmd_start = p + consumed;
                }
            } else {
                int consumed = 0;
                if (sscanf(p, "%31s%n", alias, &consumed) >= 1) {
                    g_strlcpy(schedule_raw, alias, sizeof(schedule_raw));
                    g_strlcpy(user_field, default_user ? default_user : "root", sizeof(user_field));
                    cmd_start = p + consumed;
                }
            }
        } else {
            /* Standard fields */
            char f1[32] = {0}, f2[32] = {0}, f3[32] = {0}, f4[32] = {0}, f5[32] = {0};
            if (has_user_field) {
                char u[64] = {0};
                int consumed = 0;
                if (sscanf(p, "%31s %31s %31s %31s %31s %63s%n", f1, f2, f3, f4, f5, u, &consumed) >= 6) {
                    snprintf(schedule_raw, sizeof(schedule_raw), "%s %s %s %s %s", f1, f2, f3, f4, f5);
                    g_strlcpy(user_field, u, sizeof(user_field));
                    cmd_start = p + consumed;
                }
            } else {
                int consumed = 0;
                if (sscanf(p, "%31s %31s %31s %31s %31s%n", f1, f2, f3, f4, f5, &consumed) >= 5) {
                    snprintf(schedule_raw, sizeof(schedule_raw), "%s %s %s %s %s", f1, f2, f3, f4, f5);
                    g_strlcpy(user_field, default_user ? default_user : "root", sizeof(user_field));
                    cmd_start = p + consumed;
                }
            }
        }

        if (cmd_start && schedule_raw[0]) {
            cmd_start = trim_whitespace(cmd_start);
            char *human_sched = cron_format_schedule_human(schedule_raw);
            char *entry_name = NULL;
            if (last_comment[0] && !is_syntax_template_comment(last_comment))
                entry_name = g_strdup(last_comment);
            else
                entry_name = derive_name_from_command(cmd_start);

            cron_list_add(list, entry_name, cmd_start, schedule_raw, human_sched,
                          user_field, source_path, !is_disabled, line_num, source_type);

            g_free(human_sched);
            g_free(entry_name);
        }

        last_comment[0] = '\0';
    }
}

static void parse_crontab_file(CronList *list,
                               const char *filepath,
                               const char *default_user,
                               int has_user_field,
                               CronSourceType source_type)
{
    if (!filepath || !*filepath) return;
    FILE *fp = fopen(filepath, "r");
    if (!fp) return;
    parse_crontab_stream(list, fp, filepath, default_user, has_user_field, source_type);
    fclose(fp);
}

/* Scans directory of standalone scheduled scripts (e.g. /etc/cron.daily) */
static void scan_cron_scripts_dir(CronList *list,
                                  const char *dirpath,
                                  const char *schedule_raw,
                                  const char *schedule_human,
                                  CronSourceType source_type)
{
    DIR *d = opendir(dirpath);
    if (!d) return;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        if (strstr(de->d_name, ".dpkg-") || strstr(de->d_name, ".disabled") ||
            strcmp(de->d_name, "README") == 0 || strcmp(de->d_name, ".placeholder") == 0)
            continue;

        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, de->d_name);

        struct stat st;
        if (stat(fullpath, &st) == 0 && S_ISREG(st.st_mode)) {
            int is_executable = (st.st_mode & 0111) != 0;
            cron_list_add(list, de->d_name, fullpath, schedule_raw, schedule_human,
                          "root", fullpath, is_executable, -1, source_type);
        }
    }
    closedir(d);
}

CronEntry* get_cron_entries(size_t *count)
{
    CronList list;
    cron_list_init(&list);

    /* 1. User crontabs in /var/spool/cron/crontabs */
    DIR *d = opendir("/var/spool/cron/crontabs");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (de->d_name[0] == '.') continue;
            char path[1024];
            snprintf(path, sizeof(path), "/var/spool/cron/crontabs/%s", de->d_name);
            parse_crontab_file(&list, path, de->d_name, 0, CRON_SOURCE_USER);
        }
        closedir(d);
    } else {
        /* Fallback for unprivileged user on Debian: check own user crontab */
        uid_t uid = getuid();
        struct passwd *pw = getpwuid(uid);
        const char *username = pw ? pw->pw_name : NULL;

        if (username) {
            char path[1024];
            snprintf(path, sizeof(path), "/var/spool/cron/crontabs/%s", username);
            if (access(path, R_OK) == 0) {
                parse_crontab_file(&list, path, username, 0, CRON_SOURCE_USER);
            } else {
                /* Use crontab -l pipeline as clean non-root fallback */
                FILE *pfp = popen("crontab -l 2>/dev/null", "r");
                if (pfp) {
                    parse_crontab_stream(&list, pfp, path, username, 0, CRON_SOURCE_USER);
                    pclose(pfp);
                }
            }
        }
    }

    /* 2. System main crontab: /etc/crontab */
    parse_crontab_file(&list, "/etc/crontab", "root", 1, CRON_SOURCE_SYSTEM_CRONTAB);

    /* 3. System drop-in directory: /etc/cron.d */
    DIR *cd = opendir("/etc/cron.d");
    if (cd) {
        struct dirent *de;
        while ((de = readdir(cd)) != NULL) {
            if (de->d_name[0] == '.') continue;
            if (strstr(de->d_name, ".dpkg-") || strcmp(de->d_name, ".placeholder") == 0)
                continue;
            char path[1024];
            snprintf(path, sizeof(path), "/etc/cron.d/%s", de->d_name);
            struct stat st;
            if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
                parse_crontab_file(&list, path, "root", 1, CRON_SOURCE_CRON_D);
        }
        closedir(cd);
    }

    /* 4. Periodic system script directories */
    scan_cron_scripts_dir(&list, "/etc/cron.hourly", "@hourly", "Every hour (system script)", CRON_SOURCE_CRON_HOURLY);
    scan_cron_scripts_dir(&list, "/etc/cron.daily", "@daily", "Every day (system script)", CRON_SOURCE_CRON_DAILY);
    scan_cron_scripts_dir(&list, "/etc/cron.weekly", "@weekly", "Every week (system script)", CRON_SOURCE_CRON_WEEKLY);
    scan_cron_scripts_dir(&list, "/etc/cron.monthly", "@monthly", "Every month (system script)", CRON_SOURCE_CRON_MONTHLY);

    if (count) *count = list.count;
    return list.entries;
}

/* Helper to rewrite a file line-by-line atomically */
static int rewrite_crontab_file(const char *password,
                                const char *filepath,
                                int target_line,
                                int is_delete,
                                int enable,
                                char *errmsg,
                                size_t errmsg_len)
{
    FILE *in = fopen(filepath, "r");
    if (!in) {
        if (errmsg && errmsg_len)
            snprintf(errmsg, errmsg_len, "Cannot open %s: %s", filepath, strerror(errno));
        return -1;
    }

    char tmppath[1024];
    snprintf(tmppath, sizeof(tmppath), "%s.taskmgr_tmp_XXXXXX", filepath);
    int fd = mkstemp(tmppath);
    if (fd < 0) {
        /* If cannot create in same dir, create in /tmp */
        snprintf(tmppath, sizeof(tmppath), "/tmp/taskmgr_cron_tmp_XXXXXX");
        fd = mkstemp(tmppath);
    }
    if (fd < 0) {
        fclose(in);
        if (errmsg && errmsg_len)
            snprintf(errmsg, errmsg_len, "Cannot create temporary file: %s", strerror(errno));
        return -1;
    }

    FILE *out = fdopen(fd, "w");
    if (!out) {
        close(fd);
        unlink(tmppath);
        fclose(in);
        if (errmsg && errmsg_len)
            snprintf(errmsg, errmsg_len, "Cannot write temporary file.");
        return -1;
    }

    char line[2048];
    int cur_line = 0;
    while (fgets(line, sizeof(line), in)) {
        cur_line++;
        if (cur_line == target_line) {
            if (is_delete) {
                /* Skip this line to delete */
                continue;
            }

            /* Strip existing disabled prefix if present */
            char *p = line;
            if (g_str_has_prefix(p, "# [DISABLED] "))
                p += strlen("# [DISABLED] ");
            else if (g_str_has_prefix(p, "#[DISABLED] "))
                p += strlen("#[DISABLED] ");
            else if (g_str_has_prefix(p, "#DISABLED#"))
                p += strlen("#DISABLED#");
            else if (g_str_has_prefix(p, "# DISABLED:"))
                p += strlen("# DISABLED:");

            if (enable) {
                fputs(p, out);
            } else {
                fprintf(out, "%s%s", DISABLED_PREFIX, p);
            }
        } else {
            fputs(line, out);
        }
    }

    fclose(in);
    fclose(out);

    /* Preserve original permissions */
    struct stat st;
    if (stat(filepath, &st) == 0) {
        chmod(tmppath, st.st_mode);
        if (chown(tmppath, st.st_uid, st.st_gid) != 0) {
            /* Ignore chown errors if unprivileged */
        }
    }

    /* Move temp file over destination */
    if (rename(tmppath, filepath) == 0) {
        return 0;
    }

    /* If user's own crontab, install via crontab command without needing root */
    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);
    const char *cur_user = pw ? pw->pw_name : "";
    if (strstr(filepath, "/var/spool/cron/crontabs/") && cur_user[0] && strstr(filepath, cur_user)) {
        char *cmd = g_strdup_printf("crontab '%s' 2>/dev/null", tmppath);
        int res = system(cmd);
        g_free(cmd);
        unlink(tmppath);
        if (res == 0)
            return 0;
    }

    /* If rename failed (cross-device or permissions), use privileged move */
    gboolean ok = FALSE;
    if (password)
        ok = privileged_move_file_with_password(password, tmppath, filepath);
    else
        ok = privileged_move_file(tmppath, filepath);

    unlink(tmppath);

    if (!ok) {
        if (errmsg && errmsg_len)
            snprintf(errmsg, errmsg_len, "Failed to update %s (permission denied).", filepath);
        return -1;
    }

    return 0;
}

int toggle_cron_entry_with_password(const char *password,
                                    const CronEntry *entry,
                                    int enable,
                                    char *errmsg,
                                    size_t errmsg_len)
{
    if (!entry || !entry->source_path) {
        if (errmsg && errmsg_len)
            snprintf(errmsg, errmsg_len, "Invalid entry.");
        return -1;
    }

    /* Standalone scripts in /etc/cron.daily, etc. */
    if (entry->line_number < 0) {
        mode_t mode = enable ? 0755 : 0644;
        if (chmod(entry->source_path, mode) == 0)
            return 0;

        /* Try via privileged command */
        char mode_str[8];
        snprintf(mode_str, sizeof(mode_str), "%o", (unsigned int)mode);
        char *argv[] = {"chmod", mode_str, (char *)entry->source_path, NULL};
        gboolean ok = FALSE;
        if (password)
            ok = privileged_run_sudo_argv_with_password(password, argv, NULL, errmsg, errmsg_len);
        else
            ok = privileged_run_sudo_argv(argv, NULL, errmsg, errmsg_len);
        return ok ? 0 : -1;
    }

    return rewrite_crontab_file(password, entry->source_path, entry->line_number, 0, enable, errmsg, errmsg_len);
}

int toggle_cron_entry(const CronEntry *entry, int enable, char *errmsg, size_t errmsg_len)
{
    return toggle_cron_entry_with_password(NULL, entry, enable, errmsg, errmsg_len);
}

int delete_cron_entry_with_password(const char *password,
                                    const CronEntry *entry,
                                    char *errmsg,
                                    size_t errmsg_len)
{
    if (!entry || !entry->source_path) {
        if (errmsg && errmsg_len)
            snprintf(errmsg, errmsg_len, "Invalid entry.");
        return -1;
    }

    /* Standalone script file */
    if (entry->line_number < 0) {
        if (unlink(entry->source_path) == 0)
            return 0;

        gboolean ok = FALSE;
        if (password)
            ok = privileged_delete_file_with_password(password, entry->source_path);
        else
            ok = privileged_delete_file(entry->source_path);
        return ok ? 0 : -1;
    }

    return rewrite_crontab_file(password, entry->source_path, entry->line_number, 1, 0, errmsg, errmsg_len);
}

int delete_cron_entry(const CronEntry *entry, char *errmsg, size_t errmsg_len)
{
    return delete_cron_entry_with_password(NULL, entry, errmsg, errmsg_len);
}

int run_cron_command(const char *cmd)
{
    if (!cmd || !*cmd) return -1;

    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        /* Close standard file descriptors or redirect to /dev/null */
        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) {
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2) close(fd);
        }
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }

    return (pid > 0) ? 0 : -1;
}
