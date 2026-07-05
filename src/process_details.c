/*
 * process_details.c — Process details data for taskmgr TQt3 port.
 *
 * Extracted from callbacks.c (GTK show_process_details / fetch_more_details).
 */

#include "process_details.h"
#include "backend_bridge.h"
#include "types.h"
#include "fast_format.h"
#include "utils.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <ctype.h>
#include <limits.h>

static char** split_string(const char* str, const char* delimiter);
static void free_string_array(char** str_array);
static int check_upx_signature(const char *filename);
static void fetch_more_details(pid_t pid, gchar **ident_out, gchar **sched_out,
                               gchar **memio_out, gchar **files_out, gchar **advanced_out);

static inline long pd_fast_strtol(const char** p)
{
    const char* s = *p;
    long result = 0;
    int negative = 0;

    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { negative = 1; s++; }
    else if (*s == '+') s++;

    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }
    while (*s == ' ' || *s == '\t') s++;
    *p = s;
    return negative ? -result : result;
}

static inline int pd_fast_parse_pss(const char* line, unsigned long* out_pss_kb)
{
    const char* p = line;
    if (p[0] != 'P' || p[1] != 's' || p[2] != 's' || p[3] != ':')
        return -1;
    p += 4;
    *out_pss_kb = (unsigned long)pd_fast_strtol(&p);
    return 0;
}

static const char* readable_status(const char* status_code)
{
    if (!status_code || !status_code[0]) return "Unknown";
    switch (status_code[0]) {
        case 'R': return "Running";
        case 'S': return "Waiting";
        case 'D': return "Uninterruptible sleep";
        case 'Z': return "Zombie";
        case 'T': return "Stopped";
        case 't': return "Tracing stop";
        case 'X': return "Dead";
        default:  return status_code;
    }
}

static struct task* find_task_by_pid(pid_t pid)
{
    if (!task_array) return NULL;
    for (guint i = 0; i < task_array->len; i++) {
        struct task *task = &g_array_index(task_array, struct task, i);
        if (task->pid == pid) return task;
    }
    return NULL;
}

static void read_pss_for_pid(pid_t pid, char *pss_buf, gsize pss_buf_size)
{
    pss_buf[0] = '\0';
    struct task *found_task = find_task_by_pid(pid);

    if (bridge_get_app_flags() & APP_FLAG_DISPLAY_PSS) {
        if (found_task && found_task->pss > 0)
            size_to_string(pss_buf, found_task->pss * 1024);
        return;
    }

    if (found_task && found_task->pss > 0) {
        size_to_string(pss_buf, found_task->pss * 1024);
        return;
    }

    if (IS_PSS_LOADING_ENABLED()) {
        gchar smaps_path[64];
        g_snprintf(smaps_path, sizeof(smaps_path), "/proc/%d/smaps_rollup", (int)pid);
        gchar *smaps_content = NULL;
        gsize smaps_len = 0;
        if (g_file_get_contents(smaps_path, &smaps_content, &smaps_len, NULL)) {
            char *pss_line = strstr(smaps_content, "Pss:");
            if (pss_line) {
                unsigned long pss_kb = 0;
                if (pd_fast_parse_pss(pss_line, &pss_kb) == 0)
                    size_to_string(pss_buf, (gulong)pss_kb * 1024);
            }
            g_free(smaps_content);
        }
    }
}

static int check_upx_signature(const char *filename) {
    #define UPX_SIGNATURE "UPX"
    #define UPX_BUF_SIZE 4096
    
    FILE *file = fopen(filename, "rb");
    if (!file) {
        return 0; // Impossible d'ouvrir, considérer comme non-UPX
    }

    char buffer[UPX_BUF_SIZE];
    size_t bytes_read;
    int found = 0;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (memmem(buffer, bytes_read, UPX_SIGNATURE, strlen(UPX_SIGNATURE))) {
            found = 1;
            break;
        }
    }

    fclose(file);
    return found;
}

static void fetch_more_details(pid_t pid, gchar **ident_out, gchar **sched_out, gchar **memio_out, gchar **files_out, gchar **advanced_out) {
    GString *ident_section = g_string_new("");
    GString *sched_section = g_string_new("");
    GString *memio_section = g_string_new("");
    GString *files_section = g_string_new("");
    GString *advanced_section = g_string_new("");
    
    // === SECTION 1: Identification et état ===
    
    // USER: Nom de l'utilisateur propriétaire
    gchar *status_path = g_strdup_printf("/proc/%d/status", pid);
    gchar *status_content = NULL;
    gchar user_name[256] = "N/A";
    uid_t uid = 0;
    
    if (g_file_get_contents(status_path, &status_content, NULL, NULL)) {
        char **lines = split_string(status_content, "\n");
        for (int i = 0; lines[i]; i++) {
            if (g_str_has_prefix(lines[i], "Uid:")) {
                sscanf(lines[i], "Uid:\t%u", &uid);
                struct passwd *pw = getpwuid(uid);
                if (pw) {
                    g_strlcpy(user_name, pw->pw_name, sizeof(user_name));
                }
                break;
            }
        }
        free_string_array(lines);
        g_free(status_content);
    }
    g_string_append_printf(ident_section, "USER: %s\n", user_name);
    
    // TTY: Terminal associé (depuis /proc/PID/stat champ 8)
    gchar *stat_path = g_strdup_printf("/proc/%d/stat", pid);
    gchar *stat_content = NULL;
    gchar tty_str[32] = "no tty";
    
    if (g_file_get_contents(stat_path, &stat_content, NULL, NULL)) {
        char *p = strrchr(stat_content, ')');
        if (p) {
            int tty_nr = 0;
            // Format: pid (comm) state ppid pgrp session tty_nr ...
            // Lire jusqu'au champ 8 (tty_nr)
            if (sscanf(p + 1, " %*c %*d %*d %*d %d", &tty_nr) == 1) {
                if (tty_nr == 0) {
                    g_strlcpy(tty_str, "no tty", sizeof(tty_str));
                } else {
                    int major = (tty_nr >> 8) & 0xff;
                    int minor = tty_nr & 0xff;
                    if (major == 136) {
                        g_snprintf(tty_str, sizeof(tty_str), "pts/%d", minor);
                    } else if (major == 4) {
                        g_snprintf(tty_str, sizeof(tty_str), "tty%d", minor);
                    } else {
                        g_snprintf(tty_str, sizeof(tty_str), "%d:%d", major, minor);
                    }
                }
            }
        }
        g_free(stat_content);
    }
    g_free(stat_path);
    g_string_append_printf(ident_section, "TTY: %s\n", tty_str);
    
    // Comm: Nom court du programme (depuis /proc/PID/comm)
    gchar *comm_path = g_strdup_printf("/proc/%d/comm", pid);
    gchar *comm_content = NULL;
    if (g_file_get_contents(comm_path, &comm_content, NULL, NULL)) {
        g_strchomp(comm_content);
        g_string_append_printf(ident_section, "Comm: %s\n", comm_content);
        g_free(comm_content);
    }
    g_free(comm_path);
    
    // GID: GID effectif
    gchar gid_str[32] = "N/A";
    if (g_file_get_contents(status_path, &status_content, NULL, NULL)) {
        char **lines = split_string(status_content, "\n");
        for (int i = 0; lines[i]; i++) {
            if (g_str_has_prefix(lines[i], "Gid:")) {
                gid_t gid;
                if (sscanf(lines[i], "Gid:\t%u", &gid) == 1) {
                    g_snprintf(gid_str, sizeof(gid_str), "%u", gid);
                }
                break;
            }
        }
        free_string_array(lines);
        g_free(status_content);
    }
    g_free(status_path);
    g_string_append_printf(ident_section, "GID: %s\n", gid_str);
    
    // === SECTION 2: Scheduling et performance ===
    
    // Policy: Politique de scheduling (depuis /proc/PID/sched)
    gchar *sched_path = g_strdup_printf("/proc/%d/sched", pid);
    gchar *sched_content = NULL;
    gchar policy_str[64] = "N/A";
    if (g_file_get_contents(sched_path, &sched_content, NULL, NULL)) {
        char **sched_lines = split_string(sched_content, "\n");
        for (int i = 0; sched_lines[i]; i++) {
            if (g_str_has_prefix(sched_lines[i], "policy")) {
                char *colon = strchr(sched_lines[i], ':');
                if (colon) {
                    int policy_num = atoi(colon + 1);
                    switch (policy_num) {
                        case 0: g_strlcpy(policy_str, "SCHED_OTHER", sizeof(policy_str)); break;
                        case 1: g_strlcpy(policy_str, "SCHED_FIFO", sizeof(policy_str)); break;
                        case 2: g_strlcpy(policy_str, "SCHED_RR", sizeof(policy_str)); break;
                        case 3: g_strlcpy(policy_str, "SCHED_BATCH", sizeof(policy_str)); break;
                        case 5: g_strlcpy(policy_str, "SCHED_IDLE", sizeof(policy_str)); break;
                        case 6: g_strlcpy(policy_str, "SCHED_DEADLINE", sizeof(policy_str)); break;
                        default: g_snprintf(policy_str, sizeof(policy_str), "%d", policy_num); break;
                    }
                }
                break;
            }
        }
        free_string_array(sched_lines);
        g_free(sched_content);
    }
    g_free(sched_path);
    g_string_append_printf(sched_section, "Policy: %s\n", policy_str);
    
    // Priority: Priorité RT (depuis /proc/PID/stat champ 18)
    stat_path = g_strdup_printf("/proc/%d/stat", pid);
    gchar priority_str[32] = "N/A";
    if (g_file_get_contents(stat_path, &stat_content, NULL, NULL)) {
        char *p = strrchr(stat_content, ')');
        if (p) {
            int priority = 0;
            // Format après ): state ppid pgrp session tty_nr tpgid flags minflt cminflt majflt cmajflt utime stime cutime cstime priority ...
            if (sscanf(p + 1, " %*c %*d %*d %*d %*d %*d %*u %*lu %*lu %*lu %*lu %*lu %*lu %*ld %*ld %d", &priority) == 1) {
                g_snprintf(priority_str, sizeof(priority_str), "%d", priority);
            }
        }
        g_free(stat_content);
    }
    g_free(stat_path);
    g_string_append_printf(sched_section, "Priority: %s\n", priority_str);
    
    // Threads: Nombre de threads (depuis /proc/PID/status)
    status_path = g_strdup_printf("/proc/%d/status", pid);
    gchar threads_str[32] = "N/A";
    if (g_file_get_contents(status_path, &status_content, NULL, NULL)) {
        char **lines = split_string(status_content, "\n");
        for (int i = 0; lines[i]; i++) {
            if (g_str_has_prefix(lines[i], "Threads:")) {
                int threads = 0;
                if (sscanf(lines[i], "Threads:\t%d", &threads) == 1) {
                    g_snprintf(threads_str, sizeof(threads_str), "%d", threads);
                }
                break;
            }
        }
        free_string_array(lines);
        g_free(status_content);
    }
    g_free(status_path);
    g_string_append_printf(sched_section, "Threads: %s\n", threads_str);
    
    // Start time: Heure de démarrage (depuis /proc/PID/stat champ 22)
    stat_path = g_strdup_printf("/proc/%d/stat", pid);
    gchar start_time_str[64] = "N/A";
    if (g_file_get_contents(stat_path, &stat_content, NULL, NULL)) {
        char *p = strrchr(stat_content, ')');
        if (p) {
            unsigned long long starttime = 0;
            // Lire jusqu'au champ 22 (starttime)
            if (sscanf(p + 1, " %*c %*d %*d %*d %*d %*d %*u %*lu %*lu %*lu %*lu %*lu %*lu %*ld %*ld %*d %*d %*d %*d %llu", &starttime) == 1) {
                // starttime est en jiffies depuis le boot
                // Lire l'uptime du système
                gchar *uptime_content = NULL;
                if (g_file_get_contents("/proc/uptime", &uptime_content, NULL, NULL)) {
                    double uptime_sec = 0;
                    sscanf(uptime_content, "%lf", &uptime_sec);
                    g_free(uptime_content);
                    
                    // Calculer le temps de démarrage du processus
                    long clk_tck = sysconf(_SC_CLK_TCK);
                    double process_start_sec = (double)starttime / clk_tck;
                    time_t boot_time = time(NULL) - (time_t)uptime_sec;
                    time_t process_start_time = boot_time + (time_t)process_start_sec;
                    
                    struct tm *tm_info = localtime(&process_start_time);
                    strftime(start_time_str, sizeof(start_time_str), "%Y-%m-%d %H:%M:%S", tm_info);
                }
            }
        }
        g_free(stat_content);
    }
    g_free(stat_path);
    g_string_append_printf(sched_section, "Start time: %s\n", start_time_str);
    
    // ETIME: Temps écoulé depuis le démarrage
    stat_path = g_strdup_printf("/proc/%d/stat", pid);
    gchar etime_str[64] = "N/A";
    if (g_file_get_contents(stat_path, &stat_content, NULL, NULL)) {
        char *p = strrchr(stat_content, ')');
        if (p) {
            unsigned long long starttime = 0;
            if (sscanf(p + 1, " %*c %*d %*d %*d %*d %*d %*u %*lu %*lu %*lu %*lu %*lu %*lu %*ld %*ld %*d %*d %*d %*d %llu", &starttime) == 1) {
                gchar *uptime_content = NULL;
                if (g_file_get_contents("/proc/uptime", &uptime_content, NULL, NULL)) {
                    double uptime_sec = 0;
                    sscanf(uptime_content, "%lf", &uptime_sec);
                    g_free(uptime_content);
                    
                    long clk_tck = sysconf(_SC_CLK_TCK);
                    double process_start_sec = (double)starttime / clk_tck;
                    double elapsed_sec = uptime_sec - process_start_sec;
                    
                    int days = (int)(elapsed_sec / 86400);
                    int hours = (int)((elapsed_sec - days * 86400) / 3600);
                    int minutes = (int)((elapsed_sec - days * 86400 - hours * 3600) / 60);
                    int seconds = (int)(elapsed_sec - days * 86400 - hours * 3600 - minutes * 60);
                    
                    if (days > 0) {
                        g_snprintf(etime_str, sizeof(etime_str), "%d-%02d:%02d:%02d", days, hours, minutes, seconds);
                    } else {
                        g_snprintf(etime_str, sizeof(etime_str), "%02d:%02d:%02d", hours, minutes, seconds);
                    }
                }
            }
        }
        g_free(stat_content);
    }
    g_free(stat_path);
    g_string_append_printf(sched_section, "ETIME: %s\n", etime_str);
    
    // === SECTION 3: Memory and IO Resources ===
    
    // %MEM: Pourcentage de mémoire physique utilisée
    gchar mem_percent_str[32] = "N/A";
    gchar *meminfo_path = "/proc/meminfo";
    gchar *meminfo_content = NULL;
    unsigned long total_mem = 0;
    
    if (g_file_get_contents(meminfo_path, &meminfo_content, NULL, NULL)) {
        char **meminfo_lines = split_string(meminfo_content, "\n");
        for (int i = 0; meminfo_lines[i]; i++) {
            if (g_str_has_prefix(meminfo_lines[i], "MemTotal:")) {
                sscanf(meminfo_lines[i], "MemTotal: %lu", &total_mem);
                break;
            }
        }
        free_string_array(meminfo_lines);
        g_free(meminfo_content);
    }
    
    status_path = g_strdup_printf("/proc/%d/status", pid);
    unsigned long rss = 0;
    if (g_file_get_contents(status_path, &status_content, NULL, NULL)) {
        char **lines = split_string(status_content, "\n");
        for (int i = 0; lines[i]; i++) {
            if (g_str_has_prefix(lines[i], "VmRSS:")) {
                sscanf(lines[i], "VmRSS: %lu", &rss);
                break;
            }
        }
        free_string_array(lines);
        g_free(status_content);
    }
    
    if (total_mem > 0 && rss > 0) {
        double mem_percent = (double)rss / total_mem * 100.0;
        g_snprintf(mem_percent_str, sizeof(mem_percent_str), "%.1f%%", mem_percent);
    }
    g_string_append_printf(memio_section, "%%MEM: %s\n", mem_percent_str);
    
    // Swap: Mémoire swap utilisée
    gchar swap_str[32] = "N/A";
    if (g_file_get_contents(status_path, &status_content, NULL, NULL)) {
        char **lines = split_string(status_content, "\n");
        for (int i = 0; lines[i]; i++) {
            if (g_str_has_prefix(lines[i], "VmSwap:")) {
                unsigned long swap_kb = 0;
                if (sscanf(lines[i], "VmSwap: %lu", &swap_kb) == 1) {
                    g_snprintf(swap_str, sizeof(swap_str), "%lu kB", swap_kb);
                }
                break;
            }
        }
        free_string_array(lines);
        g_free(status_content);
    }
    g_free(status_path);
    g_string_append_printf(memio_section, "Swap: %s\n", swap_str);
    
    // MajFlt: Page faults majeurs
    gchar majflt_str[32] = "N/A";
    status_path = g_strdup_printf("/proc/%d/status", pid);
    if (g_file_get_contents(status_path, &status_content, NULL, NULL)) {
        char **lines = split_string(status_content, "\n");
        for (int i = 0; lines[i]; i++) {
            if (g_str_has_prefix(lines[i], "MajFlt:")) {
                unsigned long majflt = 0;
                if (sscanf(lines[i], "MajFlt:\t%lu", &majflt) == 1) {
                    g_snprintf(majflt_str, sizeof(majflt_str), "%lu", majflt);
                }
                break;
            }
        }
        free_string_array(lines);
        g_free(status_content);
    }
    g_free(status_path);
    g_string_append_printf(memio_section, "MajFlt: %s\n", majflt_str);
    
    // Read bytes et Write bytes depuis /proc/PID/io
    gchar *io_path = g_strdup_printf("/proc/%d/io", pid);
    gchar *io_content = NULL;
    gchar read_bytes_str[64] = "N/A";
    gchar write_bytes_str[64] = "N/A";
    
    if (g_file_get_contents(io_path, &io_content, NULL, NULL)) {
        char **io_lines = split_string(io_content, "\n");
        for (int i = 0; io_lines[i]; i++) {
            if (g_str_has_prefix(io_lines[i], "read_bytes:")) {
                unsigned long long read_bytes = 0;
                if (sscanf(io_lines[i], "read_bytes: %llu", &read_bytes) == 1) {
                    if (read_bytes >= 1073741824) { // >= 1 GB
                        g_snprintf(read_bytes_str, sizeof(read_bytes_str), "%.2f GB", read_bytes / 1073741824.0);
                    } else if (read_bytes >= 1048576) { // >= 1 MB
                        g_snprintf(read_bytes_str, sizeof(read_bytes_str), "%.2f MB", read_bytes / 1048576.0);
                    } else if (read_bytes >= 1024) { // >= 1 KB
                        g_snprintf(read_bytes_str, sizeof(read_bytes_str), "%.2f KB", read_bytes / 1024.0);
                    } else {
                        g_snprintf(read_bytes_str, sizeof(read_bytes_str), "%llu B", read_bytes);
                    }
                }
            } else if (g_str_has_prefix(io_lines[i], "write_bytes:")) {
                unsigned long long write_bytes = 0;
                if (sscanf(io_lines[i], "write_bytes: %llu", &write_bytes) == 1) {
                    if (write_bytes >= 1073741824) {
                        g_snprintf(write_bytes_str, sizeof(write_bytes_str), "%.2f GB", write_bytes / 1073741824.0);
                    } else if (write_bytes >= 1048576) {
                        g_snprintf(write_bytes_str, sizeof(write_bytes_str), "%.2f MB", write_bytes / 1048576.0);
                    } else if (write_bytes >= 1024) {
                        g_snprintf(write_bytes_str, sizeof(write_bytes_str), "%.2f KB", write_bytes / 1024.0);
                    } else {
                        g_snprintf(write_bytes_str, sizeof(write_bytes_str), "%llu B", write_bytes);
                    }
                }
            }
        }
        free_string_array(io_lines);
        g_free(io_content);
    }
    g_free(io_path);
    
    g_string_append_printf(memio_section, "Read bytes: %s\n", read_bytes_str);
    g_string_append_printf(memio_section, "Write bytes: %s\n", write_bytes_str);
    
    // === SECTION 4: Files, Network and Security ===
    
    // Fichiers ouverts: Nombre de descripteurs ouverts
    gchar open_files_str[32] = "N/A";
    gchar *fd_path = g_strdup_printf("/proc/%d/fd", pid);
    GDir *fd_dir = g_dir_open(fd_path, 0, NULL);
    if (fd_dir) {
        gint fd_count = 0;
        const gchar *entry;
        while ((entry = g_dir_read_name(fd_dir)) != NULL) {
            fd_count++;
        }
        g_dir_close(fd_dir);
        g_snprintf(open_files_str, sizeof(open_files_str), "%d", fd_count);
    }
    g_free(fd_path);
    g_string_append_printf(files_section, "Open files: %s\n", open_files_str);
    
    // Ports: Compter les ports TCP/UDP ouverts
    gint tcp_count = 0;
    gint udp_count = 0;
    gchar *net_tcp_path = g_strdup_printf("/proc/%d/net/tcp", pid);
    gchar *net_udp_path = g_strdup_printf("/proc/%d/net/udp", pid);
    
    // Compter les ports TCP
    gchar *tcp_content = NULL;
    if (g_file_get_contents(net_tcp_path, &tcp_content, NULL, NULL)) {
        char **tcp_lines = split_string(tcp_content, "\n");
        for (int i = 1; tcp_lines[i]; i++) { // Sauter l'en-tête
            if (strlen(tcp_lines[i]) > 0) {
                tcp_count++;
            }
        }
        free_string_array(tcp_lines);
        g_free(tcp_content);
    }
    g_free(net_tcp_path);
    
    // Compter les ports UDP
    gchar *udp_content = NULL;
    if (g_file_get_contents(net_udp_path, &udp_content, NULL, NULL)) {
        char **udp_lines = split_string(udp_content, "\n");
        for (int i = 1; udp_lines[i]; i++) {
            if (strlen(udp_lines[i]) > 0) {
                udp_count++;
            }
        }
        free_string_array(udp_lines);
        g_free(udp_content);
    }
    g_free(net_udp_path);
    
    g_string_append_printf(files_section, "TCP ports: %d\n", tcp_count);
    g_string_append_printf(files_section, "UDP ports: %d\n", udp_count);
    
    // Cwd: Répertoire de travail courant
    gchar cwd_str[PATH_MAX] = "N/A";
    gchar *cwd_path = g_strdup_printf("/proc/%d/cwd", pid);
    ssize_t len = readlink(cwd_path, cwd_str, sizeof(cwd_str) - 1);
    if (len != -1) {
        cwd_str[len] = '\0';
    } else {
        g_strlcpy(cwd_str, "N/A", sizeof(cwd_str));
    }
    g_free(cwd_path);
    g_string_append_printf(files_section, "Cwd: %s\n", cwd_str);
    
    // Exe: Chemin de l'exécutable
    gchar exe_str[PATH_MAX] = "N/A";
    gchar *exe_path = g_strdup_printf("/proc/%d/exe", pid);
    len = readlink(exe_path, exe_str, sizeof(exe_str) - 1);
    if (len != -1) {
        exe_str[len] = '\0';
    } else {
        g_strlcpy(exe_str, "N/A", sizeof(exe_str));
    }
    g_free(exe_path);
    g_string_append_printf(files_section, "Exe: %s\n", exe_str);
    
    // === SECTION 5: Advanced ===
    
    // OOM score
    gchar oom_score_str[32] = "N/A";
    gchar *oom_score_path = g_strdup_printf("/proc/%d/oom_score", pid);
    gchar *oom_score_content = NULL;
    if (g_file_get_contents(oom_score_path, &oom_score_content, NULL, NULL)) {
        g_strchomp(oom_score_content);
        g_strlcpy(oom_score_str, oom_score_content, sizeof(oom_score_str));
        g_free(oom_score_content);
    }
    g_free(oom_score_path);
    g_string_append_printf(advanced_section, "OOM score: %s\n", oom_score_str);
    
    // Limits: Afficher seulement quelques limites importantes
    gchar *limits_path = g_strdup_printf("/proc/%d/limits", pid);
    gchar *limits_content = NULL;
    if (g_file_get_contents(limits_path, &limits_content, NULL, NULL)) {
        char **limits_lines = split_string(limits_content, "\n");
        for (int i = 0; limits_lines[i]; i++) {
            if (g_str_has_prefix(limits_lines[i], "Max stack size") ||
                g_str_has_prefix(limits_lines[i], "Max core file size") ||
                g_str_has_prefix(limits_lines[i], "Max open files")) {
                // Extraire la limite (soft et hard)
                char limit_name[64], soft[32], hard[32];
                if (sscanf(limits_lines[i], "%63[^:]: %31s %31s", limit_name, soft, hard) >= 2) {
                    g_string_append_printf(advanced_section, "%s: %s\n", limit_name, soft);
                }
            }
        }
        free_string_array(limits_lines);
        g_free(limits_content);
    }
    g_free(limits_path);
    
    // Cgroup: Première ligne seulement
    gchar *cgroup_path = g_strdup_printf("/proc/%d/cgroup", pid);
    gchar *cgroup_content = NULL;
    if (g_file_get_contents(cgroup_path, &cgroup_content, NULL, NULL)) {
        char **cgroup_lines = split_string(cgroup_content, "\n");
        if (cgroup_lines[0] && strlen(cgroup_lines[0]) > 0) {
            g_string_append_printf(advanced_section, "Cgroup: %s\n", cgroup_lines[0]);
        }
        free_string_array(cgroup_lines);
        g_free(cgroup_content);
    }
    g_free(cgroup_path);
    
    // SigPnd: Signaux en attente
    gchar sigpnd_str[64] = "N/A";
    status_path = g_strdup_printf("/proc/%d/status", pid);
    if (g_file_get_contents(status_path, &status_content, NULL, NULL)) {
        char **lines = split_string(status_content, "\n");
        for (int i = 0; lines[i]; i++) {
            if (g_str_has_prefix(lines[i], "SigPnd:")) {
                char *tab = strchr(lines[i], '\t');
                if (tab) {
                    g_strlcpy(sigpnd_str, tab + 1, sizeof(sigpnd_str));
                }
                break;
            }
        }
        free_string_array(lines);
        g_free(status_content);
    }
    g_free(status_path);
    g_string_append_printf(advanced_section, "SigPnd: %s\n", sigpnd_str);
    
    // Voluntary switches: Changements de contexte volontaires
    gchar vol_switches_str[32] = "N/A";
    status_path = g_strdup_printf("/proc/%d/status", pid);
    if (g_file_get_contents(status_path, &status_content, NULL, NULL)) {
        char **lines = split_string(status_content, "\n");
        for (int i = 0; lines[i]; i++) {
            if (g_str_has_prefix(lines[i], "voluntary_ctxt_switches:")) {
                unsigned long vol_switches = 0;
                if (sscanf(lines[i], "voluntary_ctxt_switches:\t%lu", &vol_switches) == 1) {
                    g_snprintf(vol_switches_str, sizeof(vol_switches_str), "%lu", vol_switches);
                }
                break;
            }
        }
        free_string_array(lines);
        g_free(status_content);
    }
    g_free(status_path);
    g_string_append_printf(advanced_section, "Voluntary switches: %s\n", vol_switches_str);
    
    // Vérifier si le binaire est packé avec UPX
    gchar *exe_path_upx = g_strdup_printf("/proc/%d/exe", pid);
    gchar exe_resolved[PATH_MAX];
    ssize_t len_upx = readlink(exe_path_upx, exe_resolved, sizeof(exe_resolved) - 1);
    if (len_upx != -1) {
        exe_resolved[len_upx] = '\0';
        if (check_upx_signature(exe_resolved)) {
            g_string_append(advanced_section, "Binary is packed with UPX\n");
        }
    }
    g_free(exe_path_upx);
    
    // Détection du toolkit GUI
    gchar *maps_path = g_strdup_printf("/proc/%d/maps", pid);
    gchar *maps_content = NULL;
    if (g_file_get_contents(maps_path, &maps_content, NULL, NULL)) {
        int gtk_detected = 0, qt_detected = 0, wx_detected = 0, fltk_detected = 0;
        
        char **maps_lines = split_string(maps_content, "\n");
        for (int i = 0; maps_lines[i]; i++) {
            if (strstr(maps_lines[i], "libgtk")) gtk_detected = 1;
            if (strstr(maps_lines[i], "libQt")) qt_detected = 1;
            if (strstr(maps_lines[i], "libwx")) wx_detected = 1;
            if (strstr(maps_lines[i], "libfltk")) fltk_detected = 1;
        }
        free_string_array(maps_lines);
        g_free(maps_content);
        
        // Afficher les toolkits détectés
        if (gtk_detected || qt_detected || wx_detected || fltk_detected) {
            g_string_append(advanced_section, "GUI Toolkit: ");
            int first = 1;
            if (gtk_detected) {
                g_string_append(advanced_section, "GTK");
                first = 0;
            }
            if (qt_detected) {
                if (!first) g_string_append(advanced_section, ", ");
                g_string_append(advanced_section, "Qt");
                first = 0;
            }
            if (wx_detected) {
                if (!first) g_string_append(advanced_section, ", ");
                g_string_append(advanced_section, "wxWidgets");
                first = 0;
            }
            if (fltk_detected) {
                if (!first) g_string_append(advanced_section, ", ");
                g_string_append(advanced_section, "FLTK");
            }
            g_string_append(advanced_section, "\n");
        }
    }
    g_free(maps_path);
    
    // Extraire les 20 premières strings du binaire
    gchar *exe_path_strings = g_strdup_printf("/proc/%d/exe", pid);
    gchar exe_resolved_strings[PATH_MAX];
    ssize_t len_strings = readlink(exe_path_strings, exe_resolved_strings, sizeof(exe_resolved_strings) - 1);
    if (len_strings != -1) {
        exe_resolved_strings[len_strings] = '\0';
        
        int fd = open(exe_resolved_strings, O_RDONLY);
        if (fd != -1) {
            struct stat st;
            if (fstat(fd, &st) == 0 && st.st_size > 0) {
                void *data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
                if (data != MAP_FAILED) {
                    int string_count = 0;
                    char *ptr = (char *)data;
                    GString *current_string = g_string_new("");
                    
                    g_string_append(advanced_section, "Binary strings (first 20):\n");
                    
                    for (size_t i = 0; i < st.st_size && string_count < 20; i++) {
                        if (isprint((unsigned char)ptr[i]) || ptr[i] == ' ') {
                            g_string_append_c(current_string, ptr[i]);
                        } else {
                            // Fin d'une chaîne
                            if (current_string->len >= 4) { // Minimum 4 caractères pour être considéré comme une string valide
                                g_string_append_printf(advanced_section, "  %d: %s\n", ++string_count, current_string->str);
                            }
                            g_string_truncate(current_string, 0);
                        }
                    }
                    
                    // Dernière chaîne si elle existe
                    if (current_string->len >= 4 && string_count < 20) {
                        g_string_append_printf(advanced_section, "  %d: %s\n", ++string_count, current_string->str);
                    }
                    
                    g_string_free(current_string, TRUE);
                    munmap(data, st.st_size);
                }
            }
            close(fd);
        }
    }
    g_free(exe_path_strings);
    
    // Retourner les cinq chaînes séparées
    *ident_out = g_string_free(ident_section, FALSE);
    *sched_out = g_string_free(sched_section, FALSE);
    *memio_out = g_string_free(memio_section, FALSE);
    *files_out = g_string_free(files_section, FALSE);
    *advanced_out = g_string_free(advanced_section, FALSE);
}
static char** split_string(const char* str, const char* delimiter) {
    if (!str || !delimiter) return NULL;
    
    // Count occurrences to allocate array
    int count = 1;
    const char* tmp = str;
    while ((tmp = strstr(tmp, delimiter)) != NULL) {
        count++;
        tmp += strlen(delimiter);
    }
    
    // Allocate array for pointers + NULL terminator
    char** result = malloc((count + 1) * sizeof(char*));
    if (!result) return NULL;
    
    // Copy and split
    char* str_copy = strdup(str);
    if (!str_copy) {
        free(result);
        return NULL;
    }
    
    int i = 0;
    char* token = strtok(str_copy, delimiter);
    while (token && i < count) {
        result[i] = strdup(token);
        token = strtok(NULL, delimiter);
        i++;
    }
    result[i] = NULL;
    
    free(str_copy);
    return result;
}

// Custom function to free string array (replaces g_strfreev)
static void free_string_array(char** str_array) {
    if (!str_array) return;
    
    for (int i = 0; str_array[i]; i++) {
        free(str_array[i]);
    }
    free(str_array);
}

int get_process_details_basic(pid_t pid, ProcessDetailsBasic *out)
{
    if (!out) return -1;
    out->details = NULL;
    out->cmdline = NULL;
    out->extra = NULL;

    struct task *found_task = find_task_by_pid(pid);

    gchar pid_str[12], ppid_str[12], prio_str[8];
    format_pid(pid_str, (uint32_t)pid);

    const char *name = "Unknown";
    const char *uname = "Unknown";
    const char *state = "Unknown";
    gchar cpu_time_formatted[32] = "0:00:00";
    gchar memory_buf[32] = "0 B";
    gchar vmsize_buf[32] = "0 B";
    gchar pss_buf[64] = "";

    if (found_task) {
        name = found_task->name;
        uname = found_task->uname;
        state = readable_status(task_get_state_string(found_task));
        format_pid(ppid_str, (uint32_t)found_task->ppid);
        format_priority(prio_str, TASK_GET_PRIO(found_task));

        guint64 total_seconds = found_task->time / 100;
        guint64 hours = total_seconds / 3600;
        guint64 minutes = (total_seconds % 3600) / 60;
        guint64 seconds = total_seconds % 60;
        g_snprintf(cpu_time_formatted, sizeof(cpu_time_formatted),
                   "%lu:%02lu:%02lu", hours, minutes, seconds);

        size_to_string(memory_buf, found_task->rss * 1024);
        size_to_string(vmsize_buf, found_task->size * 1024);
        read_pss_for_pid(pid, pss_buf, sizeof(pss_buf));
    } else {
        g_strlcpy(ppid_str, "0", sizeof(ppid_str));
        g_strlcpy(prio_str, "0", sizeof(prio_str));
    }

    GString *details = g_string_new("");
    g_string_append_printf(details,
        "PID: %s\nParent PID: %s\nName: %s\nUser: %s\n"
        "State: %s\nPriority: %s\nCPU Time: %s\nMemory(PSS): %s\nMemory(RSS): %s\nVM: %s",
        pid_str, ppid_str, name, uname, state, prio_str, cpu_time_formatted,
        pss_buf, memory_buf, vmsize_buf);

    GString *cmdline = g_string_new("");
    gchar *cmdline_path = g_strdup_printf("/proc/%d/cmdline", pid);
    gchar *cmdline_content = NULL;
    gsize length = 0;
    if (g_file_get_contents(cmdline_path, &cmdline_content, &length, NULL)) {
        for (gsize i = 0; i + 1 < length; i++) {
            if (cmdline_content[i] == '\0')
                cmdline_content[i] = ' ';
        }
        g_string_append(cmdline, cmdline_content);
        g_free(cmdline_content);
    }
    g_free(cmdline_path);

    GString *extra = g_string_new("");
    gchar *status_path = g_strdup_printf("/proc/%d/status", pid);
    gchar *status_content = NULL;
    if (g_file_get_contents(status_path, &status_content, NULL, NULL)) {
        char **lines = split_string(status_content, "\n");
        for (int i = 0; lines && lines[i]; i++) {
            if (g_str_has_prefix(lines[i], "Threads:") ||
                g_str_has_prefix(lines[i], "VmPeak:") ||
                g_str_has_prefix(lines[i], "VmData:") ||
                g_str_has_prefix(lines[i], "VmStk:") ||
                g_str_has_prefix(lines[i], "VmExe:") ||
                g_str_has_prefix(lines[i], "FDSize:") ||
                g_str_has_prefix(lines[i], "Groups:")) {
                g_string_append_printf(extra, "%s\n", lines[i]);
            }
        }
        free_string_array(lines);
        g_free(status_content);
    }
    g_free(status_path);

    out->details = g_string_free(details, FALSE);
    out->cmdline = g_string_free(cmdline, FALSE);
    out->extra = g_string_free(extra, FALSE);
    return 0;
}

int get_process_details_more(pid_t pid, ProcessDetailsMore *out)
{
    if (!out) return -1;
    out->ident = NULL;
    out->sched = NULL;
    out->memio = NULL;
    out->files = NULL;
    out->advanced = NULL;

    fetch_more_details(pid, &out->ident, &out->sched, &out->memio, &out->files, &out->advanced);
    return 0;
}

void free_process_details_basic(ProcessDetailsBasic *panels)
{
    if (!panels) return;
    g_free(panels->details);
    g_free(panels->cmdline);
    g_free(panels->extra);
    panels->details = NULL;
    panels->cmdline = NULL;
    panels->extra = NULL;
}

void free_process_details_more(ProcessDetailsMore *panels)
{
    if (!panels) return;
    g_free(panels->ident);
    g_free(panels->sched);
    g_free(panels->memio);
    g_free(panels->files);
    g_free(panels->advanced);
    panels->ident = NULL;
    panels->sched = NULL;
    panels->memio = NULL;
    panels->files = NULL;
    panels->advanced = NULL;
}
