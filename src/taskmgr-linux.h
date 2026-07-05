
#ifndef XFCE4_TASKMANAGER_LINUX_H
#define XFCE4_TASKMANAGER_LINUX_H

#include <glib.h>
#include <dirent.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "types.h"
#include "utils.h"

#define SIGNAL_NO   0
#define SIGNAL_KILL SIGKILL
#define SIGNAL_TERM SIGINT
#define SIGNAL_CONT SIGCONT
#define SIGNAL_STOP SIGSTOP

// Flag global pour chargement progressif PSS (remplacé par IS_PSS_LOADING_ENABLED() macro)

// Structure de cache pour optimisation batch I/O /proc
typedef struct {
    char stat_buffer[2048];
    char statm_buffer[256];
    char smaps_buffer[1024];
    char cmdline_buffer[512];
    char status_buffer[512];       // NOUVEAU: Pour lecture UID (évite stat() syscall)
    pid_t pid;
    uid_t uid;                      // NOUVEAU: UID parsé depuis status
    gboolean valid_stat;
    gboolean valid_statm;
    gboolean valid_smaps;
    gboolean valid_cmdline;
    gboolean valid_status;          // NOUVEAU: Flag pour status
} proc_data_cache_t;

// Fonctions d'optimisation batch I/O (nouvelles, optimisées)
int batch_read_at(int dirfd, const char* filename, char* buffer, size_t buffer_size);
int batch_read_proc_data(pid_t* pids, int count, proc_data_cache_t* cache);
void get_task_details_from_cache(const proc_data_cache_t* cache, struct task *task, GHashTable *uid_cache_refresh);

// Fonctions principales
void get_task_details(pid_t pid,struct task *task);  // OBSOLÈTE - Remplacée par batch I/O
GArray *get_task_list(void);  // Optimisée avec batch I/O + pool mémoire
gboolean get_cpu_usage_from_proc(system_status *sys_stat);
gboolean get_system_status(system_status *sys_stat);
gboolean send_signal_to_task(gint task_id, gint signal);
void set_priority_to_task(gint task_id, gint prio);

#endif
