/*
 * backend_bridge.h — C/C++ bridge for taskmgr
 *
 * This header exposes the pure-C backend functions to C++ code.
 * It should be included ONLY from C++ translation units.
 * The backend .c files are compiled as C and linked in.
 *
 * GLib types (gint, guint64, GArray, GHashTable, etc.) are used
 * throughout — GLib is a link-time dependency for both C and C++.
 */

#ifndef BACKEND_BRIDGE_H
#define BACKEND_BRIDGE_H

#include <glib.h>
#include <sys/types.h>
#include <signal.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ====================================================================
 * Core types — must match the C backend definitions exactly.
 * We forward-declare / include the backend headers that are GTK-free.
 * ==================================================================== */

/* app_manager.h — no GTK deps */
#include "app_manager.h"

/* fast_format.h — no GTK deps */
#include "fast_format.h"

/* ====================================================================
 * struct task — defined in types.h but that file has GTK externs.
 * We re-declare the struct and key types here to avoid pulling GTK.
 * ==================================================================== */

/* Forward declaration only — the real struct is in types.h compiled in C */
struct task;

/* system_status — plain C struct, no GTK deps */
typedef struct
{
    guint64 mem_total;
    guint64 mem_free;
    guint64 mem_cached;
    guint64 mem_buffered;
    guint64 swap_total;
    guint64 swap_used;
    guint64 cpu_count;
    guint64 cpu_idle;
    guint64 cpu_user;
    guint64 cpu_nice;
    guint64 cpu_system;
    guint64 cpu_old_jiffies;
    guint64 cpu_old_used;
    guint8 status_flags;
} system_status_bridge;

/* cpu_info_t */
typedef struct {
    int base_mhz;
    int sockets;
    int cores_per_socket;
    int logical_processors;
    int virtualization;
    int l1d_cache_kb;
    int l1i_cache_kb;
    int l1_cache_kb;
    int l2_cache_kb;
    int l3_cache_kb;
} cpu_info_bridge;

/* Performance sampling */
#define PERFORMANCE_SAMPLES_COUNT 120
#define MAX_CPU_CORES 64

typedef struct {
    gint32 cpu_samples[PERFORMANCE_SAMPLES_COUNT];
    gint32 ram_samples[PERFORMANCE_SAMPLES_COUNT];
    gint16 swap_samples[PERFORMANCE_SAMPLES_COUNT];
    gint16 disk_samples[PERFORMANCE_SAMPLES_COUNT];
    gint16 gpu_render_samples[PERFORMANCE_SAMPLES_COUNT];
    gint16 gpu_video_samples[PERFORMANCE_SAMPLES_COUNT];
    gint16 gpu_total_samples[PERFORMANCE_SAMPLES_COUNT];
    gint disk_read_samples[PERFORMANCE_SAMPLES_COUNT];
    gint disk_write_samples[PERFORMANCE_SAMPLES_COUNT];
    gint16 **cpu_core_samples;
    gdouble *cpu_core_speeds;
    gint8 cpu_core_count;
    gint8 current_index;
    guint8 perf_flags;
    guint8 padding;
} performance_data_bridge;

/* CellColorLevel */
typedef enum {
    CELL_COLOR_NORMAL_B = 0,
    CELL_COLOR_MEDIUM_B,
    CELL_COLOR_HIGH_B,
    CELL_COLOR_CRITICAL_B,
    CELL_COLOR_COUNT_B
} CellColorLevel_bridge;

/* ====================================================================
 * Task access macros (duplicated from types.h for C++ access)
 * ==================================================================== */

#define TASK_GET_STATE_CHAR_B(task)  ((char)(((task)->state_prio_packed) & 0xFF))
#define TASK_GET_PRIO_B(task)       ((int)((((task)->state_prio_packed) >> 8) | ((((task)->state_prio_packed) & 0x800000) ? 0xFF000000 : 0)))
#define TASK_GET_FLAGS_B(task)      ((unsigned char)(((task)->flags_nice_packed) & 0xFF))

/* App flags */
#define APP_FLAG_SHOW_USER_TASKS    0x01
#define APP_FLAG_SHOW_ROOT_TASKS    0x02
#define APP_FLAG_SHOW_OTHER_TASKS   0x04
#define APP_FLAG_SHOW_FULL_PATH     0x08
#define APP_FLAG_SHOW_CACHED_FREE   0x10
#define APP_FLAG_GROUP_PROCS        0x20
#define APP_FLAG_FULL_VIEW          0x40
#define APP_FLAG_MINIMIZE_TO_TRAY   0x80
#define APP_FLAG_SHOW_HEADER_GAUGES 0x100
#define APP_FLAG_KEEP_ABOVE         0x200
#define APP_FLAG_WINDOW_HIDDEN      0x400
#define APP_FLAG_QUIT_CALLED        0x800
#define APP_FLAG_ALLOW_MULTIPLE_INSTANCES 0x1000
#define APP_FLAG_SMOOTH_SCROLLING   0x2000
#define APP_FLAG_ENABLE_ANTIALIASING 0x4000
#define APP_FLAG_DISPLAY_PSS        0x8000

#define PERF_DATA_BUFFER_FULL_B     0x01
#define MANAGER_BUFFER_FULL_B       0x02

/* ====================================================================
 * taskmgr-linux.h — Process listing and signals
 * ==================================================================== */

GArray* get_task_list(void);
gboolean get_cpu_usage_from_proc(system_status_bridge *sys_stat);
gboolean get_system_status(system_status_bridge *sys_stat);
gdouble get_cpu_usage(system_status_bridge *sys_stat);
gboolean send_signal_to_task(gint task_id, gint signal);
void set_priority_to_task(gint task_id, gint prio);

/* Bridge-level refresh: computes CPU % without GTK dependency */
void bridge_refresh_tasks(void);


/* Global task array — updated by refresh_task_list() */
extern GArray *task_array;

/* ====================================================================
 * functions.h — System info parsing
 * ==================================================================== */

/* Task struct field access (the struct is opaque from C++) */
const gchar* bridge_task_name(const GArray *tasks, guint index);
const gchar* bridge_task_simple_name(const GArray *tasks, guint index);
pid_t bridge_task_pid(const GArray *tasks, guint index);
pid_t bridge_task_ppid(const GArray *tasks, guint index);
uid_t bridge_task_uid(const GArray *tasks, guint index);
const gchar* bridge_task_uname(const GArray *tasks, guint index);
guint64 bridge_task_time(const GArray *tasks, guint index);
gulong bridge_task_rss(const GArray *tasks, guint index);
gulong bridge_task_pss(const GArray *tasks, guint index);
gulong bridge_task_shr(const GArray *tasks, guint index);
gulong bridge_task_vsz(const GArray *tasks, guint index);
gfloat bridge_task_cpu(const GArray *tasks, guint index);
gfloat bridge_task_gpu(const GArray *tasks, guint index);
char bridge_task_state(const GArray *tasks, guint index);
int bridge_task_prio(const GArray *tasks, guint index);

/* System info */
gdouble get_cpu_speed(void);
gdouble get_core_cpu_speed(int core_index);
long get_jiffies_per_second(void);
gint get_num_cpus(void);
gulong get_system_uptime(void);
gint get_process_count(void);
long get_thread_count(void);
gint get_handle_count(void);
void get_swap_info(guint64 *swap_total, guint64 *swap_free);

/* Initialization / cleanup */
void init_uid_cache(void);
void cleanup_uid_cache(void);
void cleanup_system_status(void);
void cleanup_disk_system_info(void);
void load_config(void);
void save_config(void);

/* ====================================================================
 * disk_manager.h
 * ==================================================================== */
#include "disk_manager.h"

/* ====================================================================
 * network_manager.h
 * ==================================================================== */
#include "network_manager.h"

/* ====================================================================
 * gpu_stats.h
 * ==================================================================== */
#include "gpu_stats.h"

/* ====================================================================
 * service_manager.h
 * ==================================================================== */
#include "service_manager.h"

/* ====================================================================
 * autostart_manager.h
 * ==================================================================== */
#include "autostart_manager.h"

/* ====================================================================
 * pss_thread.h — needs types.h for struct task, but we include
 * the header here since it only uses GLib + pthread
 * ==================================================================== */
void init_pss_thread(void);
void start_pss_collection(pid_t *pids, int count);
gboolean get_pss_result(pid_t pid, guint64 *pss_out);
void cleanup_pss_thread(void);
gboolean is_pss_thread_busy(void);

/* ====================================================================
 * Global state accessors (defined in the C backend)
 * ==================================================================== */

/* Performance data */
void bridge_init_backend(void);

performance_data_bridge* bridge_get_performance_data(void);
void bridge_update_performance_samples(double cpu_usage, double ram_usage, double swap_usage);
cpu_info_bridge* bridge_get_cpu_info(void);
const gchar* bridge_get_cpu_model_name(void);
const gchar* bridge_get_gpu_model_name(void);
gdouble bridge_get_installed_ram_gib(void);
long long bridge_get_commit_limit(void);
long long bridge_get_committed_as(void);
gint bridge_get_cpu_graph_type(void);
void bridge_set_cpu_graph_type(gint type);
gint bridge_get_gpu_usage_mode(void);
void bridge_set_gpu_usage_mode(gint mode);

/* RAM / CPU Detailed Info structs */


RAMInfoData* get_ram_info_data(void);
CPUInfoData* get_cpu_info_data(void);
long get_system_thread_count_fast(void);
unsigned int get_process_count_fast(void);
long long get_paged_pool(void);
long long get_non_paged_pool(void);

/* App state */
guint16 bridge_get_app_flags(void);
void bridge_set_app_flags(guint16 flags);
guint16 bridge_get_display_flags(void);
void bridge_set_display_flags(guint16 flags);
uid_t bridge_get_own_uid(void);

/* Config */
gint bridge_get_refresh_interval(void);
void bridge_set_refresh_interval(gint interval_ms);

/* Users tab sort flags — from types.h */
#define USERS_SORT_NAME_ASC         0x01
#define USERS_SORT_CPU_ASC          0x02
#define USERS_SORT_MEMORY_ASC       0x04
#define USERS_SORT_GPU_ASC          0x08
#define USERS_SORT_PID_ASC          0x10



user_with_sessions_t **get_logged_in_users_with_session_info(void);
gboolean disconnect_user_sessions(const char *username);

pid_t backend_pick_window_pid(void);
int backend_kill_pid(pid_t pid);
void backend_get_process_name(pid_t pid, char* name_buffer, int buffer_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* BACKEND_BRIDGE_H */
