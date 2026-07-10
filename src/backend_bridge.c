/*
 * backend_bridge.c — Implementation of C++ bridge accessor functions.
 *
 * This file is compiled as C and provides accessor functions for
 * struct task fields (since the struct layout is only known in C).
 * It also provides accessors for global state variables.
 */

#include "types.h"
#include "common.h"
#include "taskmgr-linux.h"
#include "functions.h"
#include "disk_manager.h"
#include "network_manager.h"
#include "gpu_stats.h"

#include <unistd.h>

gdouble get_cpu_speed(void);

/* ====================================================================
 * Global state variables (defined in backend to replace main.c)
 * ==================================================================== */

void *global_status_icon = NULL;
void *main_window = NULL;
GArray *task_array = NULL;
gint tasks = 0;
guint refresh_interval = 1;
int refresh_interval_ms = 1000;
uid_t own_uid = 0;
gchar *config_file = NULL;
gint sort_column_id = 1; // COLUMN_NAME
int sort_order = 0; // GTK_SORT_ASCENDING
guint16 app_flags = 0;
guint16 display_flags = 0;
guint8 system_status_flags = 0;
guint16 interface_state_flags = 0;
GHashTable *uname_table = NULL;
gint win_width = 800;
gint win_height = 600;
int page_size = 4096;
gint saved_win_x = -1;
gint saved_win_y = -1;
performance_data_t performance_data = {0};
gchar *cpu_model_name = NULL;
gchar *gpu_model_name = NULL;
cpu_info_t cpu_detailed_info = {0};
gdouble installed_ram_gib = 0.0;
long long commit_limit_bytes = 0;
long long committed_as_bytes = 0;
gint cpu_graph_type = 0;
gint gpu_usage_reporting_mode = 0;
long disk_capacity_gb = 0;
const char *global_display = NULL;
const char *global_xauthority = NULL;

void *startup_treeview = NULL;
void *startup_list_store = NULL;
void *services_treeview = NULL;
void *services_list_store = NULL;
void *services_selection = NULL;

/* Optimisation flags */
guint32 system_optimization_flags = 
    OPTIMIZATION_FLAG_CELL_COLORING |         
    OPTIMIZATION_FLAG_INCREMENTAL_UPDATE |    
    OPTIMIZATION_FLAG_ADAPTIVE_POLLING |      
    REFRESH_FLAG_FULL_PROCESSES |
    UI_FLAG_INFO_BLOCKS_NEED_UPDATE;
guint8 system_disk_flags = 0;

void set_optimization_flag(guint32 flag, gboolean enabled) {
    if (enabled) {
        system_optimization_flags |= flag;
    } else {
        system_optimization_flags &= ~flag;
    }
}

gboolean get_optimization_flag(guint32 flag) {
    return (system_optimization_flags & flag) != 0;
}

#define UNAME_POOL_SIZE 8192
static char uname_pool[UNAME_POOL_SIZE];
static size_t pool_offset = 0;

const gchar* get_shared_uname(uid_t uid, const gchar* uname) {
    if (!uname_table) {
        uname_table = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);
    }
    gchar* stored = g_hash_table_lookup(uname_table, GUINT_TO_POINTER(uid));
    if (!stored) {
        size_t len = strlen(uname) + 1;
        if (pool_offset + len < UNAME_POOL_SIZE) {
            stored = &uname_pool[pool_offset];
            memcpy(stored, uname, len);
            pool_offset += len;
            g_hash_table_insert(uname_table, GUINT_TO_POINTER(uid), stored);
        } else {
            stored = g_strdup(uname);
            g_hash_table_insert(uname_table, GUINT_TO_POINTER(uid), stored);
        }
    }
    return stored;
}

gchar* extract_binary_name(const gchar* full_command) {
    if (!full_command || *full_command == '\0') {
        return g_strdup("");
    }
    const gchar* space = strchr(full_command, ' ');
    if (space) {
        return g_strndup(full_command, space - full_command);
    } else {
        return g_strdup(full_command);
    }
}

void debug_tree_iter_cache_usage(void) {}

long get_jiffies_per_second(void) {
    static long hz = 0;
    if (hz == 0) {
        hz = sysconf(_SC_CLK_TCK);
        if (hz <= 0) hz = 100;
    }
    return hz;
}

gint get_num_cpus(void) {
    return get_cached_nprocessors();
}

/* ====================================================================
 * Task field accessors — GArray of struct task
 * ==================================================================== */

static inline struct task* _get_task(const GArray *tasks, guint index) {
    if (!tasks || index >= tasks->len) return NULL;
    return &g_array_index(tasks, struct task, index);
}

const gchar* bridge_task_name(const GArray *tasks, guint index) {
    struct task *t = _get_task(tasks, index);
    return t ? t->name : "";
}

const gchar* bridge_task_simple_name(const GArray *tasks, guint index) {
    struct task *t = _get_task(tasks, index);
    return t ? t->simple_name : "";
}

pid_t bridge_task_pid(const GArray *tasks, guint index) {
    struct task *t = _get_task(tasks, index);
    return t ? t->pid : 0;
}

pid_t bridge_task_ppid(const GArray *tasks, guint index) {
    struct task *t = _get_task(tasks, index);
    return t ? t->ppid : 0;
}

uid_t bridge_task_uid(const GArray *tasks, guint index) {
    struct task *t = _get_task(tasks, index);
    return t ? t->uid : 0;
}

const gchar* bridge_task_uname(const GArray *tasks, guint index) {
    struct task *t = _get_task(tasks, index);
    return t ? (t->uname ? t->uname : "") : "";
}

guint64 bridge_task_time(const GArray *tasks, guint index) {
    struct task *t = _get_task(tasks, index);
    return t ? t->time : 0;
}

gulong bridge_task_rss(const GArray *tasks, guint index) {
    struct task *t = _get_task(tasks, index);
    return t ? t->rss : 0;
}

gulong bridge_task_pss(const GArray *tasks, guint index) {
    struct task *t = _get_task(tasks, index);
    return t ? (t->pss * 1024) : 0;
}

gulong bridge_task_shr(const GArray *tasks, guint index) {
    struct task *t = _get_task(tasks, index);
    return t ? t->shr : 0;
}

gulong bridge_task_vsz(const GArray *tasks, guint index) {
    struct task *t = _get_task(tasks, index);
    return t ? t->size : 0;
}

gfloat bridge_task_cpu(const GArray *tasks, guint index) {
    struct task *t = _get_task(tasks, index);
    return t ? t->time_percentage : 0.0f;
}

gfloat bridge_task_gpu(const GArray *tasks, guint index) {
    struct task *t = _get_task(tasks, index);
    return t ? t->gpu_usage : 0.0f;
}

char bridge_task_state(const GArray *tasks, guint index) {
    struct task *t = _get_task(tasks, index);
    return t ? TASK_GET_STATE_CHAR(t) : '?';
}

int bridge_task_prio(const GArray *tasks, guint index) {
    struct task *t = _get_task(tasks, index);
    return t ? TASK_GET_PRIO(t) : 0;
}

/* ====================================================================
 * Global state accessors
 * ==================================================================== */

performance_data_t* bridge_get_performance_data(void) {
    return &performance_data;
}

void bridge_update_performance_samples(double cpu_usage, double ram_usage, double swap_usage) {
    gdouble disk_usage = get_disk_activity_percent();
    gdouble disk_read_kbs = 0, disk_write_kbs = 0;
    get_disk_io_rates(&disk_read_kbs, &disk_write_kbs);

    update_performance_samples(cpu_usage, ram_usage, swap_usage, disk_usage, disk_read_kbs, disk_write_kbs);

    check_and_refresh_disk_list();
    update_disk_data();
    update_network_data();
}

static void get_cpu_model(void) {
    FILE *file = fopen("/proc/cpuinfo", "r");
    if (file == NULL) {
        cpu_model_name = g_strdup("Unknown CPU");
        return;
    }
    
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "model name", 10) == 0) {
            char *colon = strchr(line, ':');
            if (colon != NULL) {
                colon++; // Skip the colon
                while (*colon == ' ' || *colon == '\t') colon++; // Skip whitespace
                
                // Remove newline at the end
                char *newline = strchr(colon, '\n');
                if (newline) *newline = '\0';
                
                cpu_model_name = g_strdup(colon);
                fclose(file);
                return;
            }
        }
    }
    
    fclose(file);
    cpu_model_name = g_strdup("Unknown CPU");
}

void bridge_init_backend(void) {
    own_uid = getuid();
    get_cpu_model();
    const char* gpu_name = gpu_stats_get_gpu_name();
    if (gpu_name && strlen(gpu_name) > 0) {
        gpu_model_name = g_strdup(gpu_name);
    } else {
        gpu_model_name = g_strdup("Unknown GPU");
    }
    update_gpu_monitoring_mode();
    get_cpu_detailed_info(&cpu_detailed_info);
    installed_ram_gib = get_installed_ram_gib();
    get_ram_commit_info();
    init_cpu_core_sampling();
}

cpu_info_t* bridge_get_cpu_info(void) {
    return &cpu_detailed_info;
}

const gchar* bridge_get_cpu_model_name(void) {
    return cpu_model_name ? cpu_model_name : "Unknown CPU";
}

const gchar* bridge_get_gpu_model_name(void) {
    return gpu_model_name ? gpu_model_name : "";
}

gdouble bridge_get_installed_ram_gib(void) {
    return installed_ram_gib;
}

long long bridge_get_commit_limit(void) {
    return commit_limit_bytes;
}

long long bridge_get_committed_as(void) {
    return committed_as_bytes;
}

gint bridge_get_cpu_graph_type(void) {
    return cpu_graph_type;
}

void bridge_set_cpu_graph_type(gint type) {
    cpu_graph_type = type;
}

gint bridge_get_gpu_usage_mode(void) {
    return gpu_usage_reporting_mode;
}

void bridge_set_gpu_usage_mode(gint mode) {
    gpu_usage_reporting_mode = mode;
    update_gpu_monitoring_mode();
}

guint16 bridge_get_app_flags(void) {
    return app_flags;
}

void bridge_set_app_flags(guint16 flags) {
    app_flags = flags;
}

guint16 bridge_get_display_flags(void) {
    return display_flags;
}

void bridge_set_display_flags(guint16 flags) {
    display_flags = flags;
}

uid_t bridge_get_own_uid(void) {
    return own_uid;
}

gint bridge_get_refresh_interval(void) {
    return REFRESH_INTERVAL;
}

void bridge_set_refresh_interval(gint interval_ms) {
    refresh_interval_ms = interval_ms;
}

/* ====================================================================
 * bridge_refresh_tasks() — GTK-free equivalent of refresh_task_list().
 * Computes time_percentage for each task by delta with previous task_array.
 * Formula matches original: time_percentage = delta_time / (refresh_interval_sec * num_cpus)
 * ==================================================================== */
void bridge_refresh_tasks(void) {
    sample_gpu_system_stats();

    GArray *new_list = get_task_list();
    if (!new_list) return;

    /* Determine cpu count */
    system_status sys;
    memset(&sys, 0, sizeof(sys));
    get_system_status(&sys);
    guint num_cpus = sys.cpu_count;
    if (num_cpus == 0) num_cpus = 1;

    /* Divisor = refresh_interval_seconds * num_cpus */
    double interval_sec = (double)REFRESH_INTERVAL / 1000.0;
    double divisor = interval_sec * (double)num_cpus;
    if (divisor <= 0.0) divisor = 1.0;

    if (task_array && task_array->len > 0) {
        /* Build hash: pid -> index in old task_array for fast lookup */
        GHashTable *old_hash = g_hash_table_new(g_direct_hash, g_direct_equal);
        for (guint i = 0; i < task_array->len; i++) {
            struct task *t = &g_array_index(task_array, struct task, i);
            g_hash_table_insert(old_hash, GINT_TO_POINTER(t->pid), t);
        }

        /* Theoretical max jiffies (for sanity check) */
        gulong max_jiffies = (gulong)(interval_sec * (double)get_jiffies_per_second() * (double)num_cpus);

        for (guint j = 0; j < new_list->len; j++) {
            struct task *new_t = &g_array_index(new_list, struct task, j);
            struct task *old_t = (struct task *)g_hash_table_lookup(old_hash, GINT_TO_POINTER(new_t->pid));
            if (old_t) {
                guint delta = new_t->time - old_t->time;
                if (delta > max_jiffies) {
                    new_t->time_percentage = 0.0f;
                } else {
                    double pct = (double)delta / divisor;
                    new_t->time_percentage = (gfloat)pct;
                }
            } else {
                new_t->time_percentage = 0.0f;
            }
        }
        g_hash_table_destroy(old_hash);
    } else {
        /* First call — no previous data */
        for (guint j = 0; j < new_list->len; j++) {
            g_array_index(new_list, struct task, j).time_percentage = 0.0f;
        }
    }

    /* Swap: free old, install new */
    if (task_array) {
        g_array_free(task_array, TRUE);
    }
    task_array = new_list;
}

static RAMInfoData ram_info_cache = {0.0, 0.0, 0.0, 0.0, 0.0};

RAMInfoData* get_ram_info_data(void) {
    system_status ram_stat;
    get_system_status(&ram_stat);
    guint64 ram_used = ram_stat.mem_total - ram_stat.mem_free;
    if (bridge_get_app_flags() & APP_FLAG_SHOW_CACHED_FREE) {
        ram_used -= ram_stat.mem_cached + ram_stat.mem_buffered;
    }

    ram_info_cache.ram_in_use_gb = (double)ram_used / (1024.0 * 1024.0);
    ram_info_cache.ram_available_gb = (double)ram_stat.mem_total / (1024.0 * 1024.0);
    ram_info_cache.committed_gb = (double)committed_as_bytes / (1024.0 * 1024.0 * 1024.0);
    ram_info_cache.commit_limit_gb = (double)commit_limit_bytes / (1024.0 * 1024.0 * 1024.0);

    long long cached_bytes = get_cached_ram();
    ram_info_cache.cached_gb = (double)cached_bytes / (1024.0 * 1024.0 * 1024.0);

    return &ram_info_cache;
}

static CPUInfoData cpu_info_cache = {0, 0.0, 0, 0, 0, ""};

CPUInfoData* get_cpu_info_data(void) {
    if (performance_data.current_index > 0) {
        cpu_info_cache.current_cpu = performance_data.cpu_samples[performance_data.current_index - 1];
    } else if (performance_data.perf_flags & PERF_DATA_BUFFER_FULL) {
        cpu_info_cache.current_cpu = performance_data.cpu_samples[PERFORMANCE_SAMPLES_COUNT - 1];
    } else {
        cpu_info_cache.current_cpu = 0;
    }

    cpu_info_cache.cpu_speed = get_cpu_speed();
    cpu_info_cache.process_count = get_process_count();
    cpu_info_cache.thread_count = get_thread_count();
    cpu_info_cache.handle_count = get_fd_count();
    get_uptime_formatted(cpu_info_cache.uptime, sizeof(cpu_info_cache.uptime));

    return &cpu_info_cache;
}
