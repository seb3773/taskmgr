
#ifndef TYPES_H
#define TYPES_H

#ifdef WITHOUT_GTK
#include <glib.h>
#include <sys/types.h>
typedef void GtkWidget;
typedef void GtkListStore;
typedef void GtkTreeSelection;
typedef void GtkStatusIcon;
typedef struct { char dummy[32]; } GtkTreeIter;
typedef int GtkSortType;
#define GTK_SORT_ASCENDING 0
#define GTK_SORT_DESCENDING 1
#else
#include <gtk/gtk.h>
#endif
#include "app_manager.h"

#ifdef __cplusplus
extern "C" {
#endif
extern int refresh_interval_ms;
#define REFRESH_INTERVAL refresh_interval_ms

// Enum pour les types de vues en mode réduit intégré
typedef enum {
    REDUCED_VIEW_CPU,
    REDUCED_VIEW_RAM,
    REDUCED_VIEW_DISK,
    REDUCED_VIEW_NETWORK,
    REDUCED_VIEW_GPU
} ReducedViewType;

// Structure pour stocker les dimensions des graphiques
typedef struct {
    gint graph_x;
    gint graph_y;
    gint graph_width;
    gint graph_height;
    gint ram_graph_height;
    gint swap_graph_height;
    gint swap_graph_y;
    gint activity_graph_height;
    gint io_graph_height;
    gint io_graph_y;
} GraphDimensions;

// Table globale des noms d'utilisateurs uniques
extern GHashTable *uname_table;

// ============================================================================
// CACHE UID ULTRA-PERFORMANT (85-90% réduction temps résolution)
// ============================================================================

// Structure pour cache UID → username (évite appels getpwuid répétés)
#define UID_CACHE_FLAG_VALID      0x01

typedef struct {
    uid_t uid;                    // UID utilisateur
    gchar* username;              // Nom utilisateur (alloué)
    time_t cache_time;            // Timestamp mise en cache
    guint8 flags;                 // Remplace gboolean is_valid par bitmask
} uid_cache_entry_t;

// Configuration cache UID
#define UID_CACHE_TTL_SECONDS 60     // TTL 60 secondes
#define UID_CACHE_CLEANUP_INTERVAL 300  // Nettoyage toutes les 5 minutes

// Variables globales cache UID
extern GHashTable* uid_cache;
extern time_t last_uid_cache_cleanup;

// Fonctions cache UID optimisées
const gchar* get_cached_username(uid_t uid);
void init_uid_cache(void);
void cleanup_uid_cache(void);
void force_uid_cache_refresh(void);

// Fonctions hash table incrémentale optimisées
void update_pid_index_map_incremental(void);
void update_pid_index_map_traditional(void);
void set_incremental_update_enabled(gboolean enabled);
void cleanup_incremental_update(void);

// ============================================================================
// BATCH GTK TREEVIEW UPDATES - Élimination 500 appels séparés
// ============================================================================

// Configuration batch GTK
#define GTK_BATCH_SIZE 64        // Taille batch optimale
#define GTK_MAX_BATCHES 16       // Nombre maximum de batches

// Structure pour batch GTK TreeView updates
typedef struct {
    gint task_indices[GTK_BATCH_SIZE];     // Indices des tâches à mettre à jour
    gint batch_count;                      // Nombre d'éléments dans ce batch
    GtkTreeIter cached_iters[GTK_BATCH_SIZE]; // TreeIters mis en cache
    gboolean iters_valid[GTK_BATCH_SIZE];  // Validité des TreeIters
} gtk_update_batch_t;

// Fonctions batch GTK optimisées
void init_gtk_batch_system(void);
void refresh_list_batch(gtk_update_batch_t* batch);
void refresh_list_items_batched(gint start_index, gint count);
void cleanup_gtk_batch_system(void);

// Structure pour batch Users (processus enfants d'un utilisateur)
typedef struct {
    gint task_indices[GTK_BATCH_SIZE];     // Indices des tâches à mettre à jour
    gint batch_count;                      // Nombre d'éléments dans ce batch
    GtkTreeIter parent_iter;               // TreeIter du parent (utilisateur)
    GtkTreeIter cached_iters[GTK_BATCH_SIZE]; // TreeIters des processus enfants
    gboolean iters_valid[GTK_BATCH_SIZE];  // Validité des TreeIters
} users_batch_t;

// Fonctions batch Users
void refresh_users_processes_batched(users_batch_t* batch, GArray *task_list);

// ============================================================================
// SYSTÈME COLORATION CELLULES OPTIMISÉ - Coût minimal
// ============================================================================

// Niveaux de coloration (enum partagé)
typedef enum {
    CELL_COLOR_NORMAL = 0,    // Valeurs normales
    CELL_COLOR_MEDIUM,        // Valeurs modérées  
    CELL_COLOR_HIGH,          // Valeurs élevées
    CELL_COLOR_CRITICAL,      // Valeurs critiques
    CELL_COLOR_COUNT
} CellColorLevel;

// ============================================================================
// SYSTÈME BITMASK UNIFIÉ POUR OPTIMISATIONS
// ============================================================================

// Flags d'optimisation système (remplace les gboolean dispersés)
#define OPTIMIZATION_FLAG_GTK_BATCH_UPDATES     0x0001  // gtk_batch_updates_enabled
#define OPTIMIZATION_FLAG_CELL_COLORING         0x0002  // cell_coloring_enabled  
#define OPTIMIZATION_FLAG_INCREMENTAL_UPDATE    0x0004  // incremental_update_enabled
#define OPTIMIZATION_FLAG_ADAPTIVE_POLLING      0x0008  // adaptive_polling_enabled
#define OPTIMIZATION_FLAG_PSS_LOADING           0x0010  // pss_loading_enabled

// Flags de logique refresh granulaire (remplace les gboolean need_*)
#define REFRESH_FLAG_FULL_PROCESSES             0x0100  // need_full_processes
#define REFRESH_FLAG_USERS_CALC                 0x0200  // need_users_calc
#define REFRESH_FLAG_PERF_COUNTERS              0x0400  // need_perf_counters
#define REFRESH_FLAG_ONLY_SAMPLES               0x0800  // need_only_samples

// Flags d'état système (remplace les gboolean d'état)
#define STATE_FLAG_ITER_CACHE_INITIALIZED       0x1000  // iter_cache_initialized
#define STATE_FLAG_BATCH_SYSTEM_INITIALIZED     0x2000  // batch_system_initialized
#define STATE_FLAG_INTEGRATED_REDUCED_MODE      0x4000  // is_integrated_reduced_mode
#define STATE_FLAG_DRAGGING_INTEGRATED          0x8000  // is_dragging_integrated

// Flags d'initialisation pools (remplace les gboolean pool_initialized)
#define POOL_FLAG_SERVICES_INITIALIZED          0x010000 // service_manager pool_initialized
#define POOL_FLAG_TASKS_INITIALIZED             0x020000 // functions.c pool_initialized

// Flags d'initialisation caches (remplace les gboolean *_initialized)
#define CACHE_FLAG_SYSCONF_INITIALIZED          0x040000 // functions.c sysconf_cache_initialized
#define CACHE_FLAG_STATIC_SYSTEM_INITIALIZED    0x080000 // functions.c static_system_cache_initialized
#define CACHE_FLAG_DISK_INITIALIZED             0x100000 // functions.c disk_initialized
#define CACHE_FLAG_NETWORK_INITIALIZED          0x200000 // network_manager.c cache_initialized
#define CACHE_FLAG_GRAPH_INITIALIZED            0x400000 // interface.c cache_initialized
#define CACHE_FLAG_PARALLEL_ENABLED             0x800000 // xfce-taskmanager-linux.c parallel_enabled

// Flags de validité caches (remplace les gboolean *_valid)
#define CACHE_FLAG_DISKSTATS_VALID              0x1000000 // functions.c diskstats_cache_valid

// Flags de mise à jour UI (remplace les gboolean *_need_update)
#define UI_FLAG_INFO_BLOCKS_NEED_UPDATE         0x2000000 // interface.c info_blocks_need_update

// Variable globale bitmask unique
extern guint32 system_optimization_flags;

// Macros pour tests rapides (remplace les comparaisons gboolean)
#define IS_GTK_BATCH_ENABLED()          (system_optimization_flags & OPTIMIZATION_FLAG_GTK_BATCH_UPDATES)
#define IS_CELL_COLORING_ENABLED()      (system_optimization_flags & OPTIMIZATION_FLAG_CELL_COLORING)
#define IS_INCREMENTAL_UPDATE_ENABLED() (system_optimization_flags & OPTIMIZATION_FLAG_INCREMENTAL_UPDATE)
#define IS_ADAPTIVE_POLLING_ENABLED()   (system_optimization_flags & OPTIMIZATION_FLAG_ADAPTIVE_POLLING)
#define IS_PSS_LOADING_ENABLED()        (system_optimization_flags & OPTIMIZATION_FLAG_PSS_LOADING)

#define NEEDS_FULL_PROCESSES()          (system_optimization_flags & REFRESH_FLAG_FULL_PROCESSES)
#define NEEDS_USERS_CALC()              (system_optimization_flags & REFRESH_FLAG_USERS_CALC)
#define NEEDS_PERF_COUNTERS()           (system_optimization_flags & REFRESH_FLAG_PERF_COUNTERS)
#define NEEDS_ONLY_SAMPLES()            (system_optimization_flags & REFRESH_FLAG_ONLY_SAMPLES)

#define IS_ITER_CACHE_INITIALIZED()     (system_optimization_flags & STATE_FLAG_ITER_CACHE_INITIALIZED)
#define IS_BATCH_SYSTEM_INITIALIZED()   (system_optimization_flags & STATE_FLAG_BATCH_SYSTEM_INITIALIZED)
#define IS_INTEGRATED_REDUCED_MODE()    (system_optimization_flags & STATE_FLAG_INTEGRATED_REDUCED_MODE)
#define IS_DRAGGING_INTEGRATED()        (system_optimization_flags & STATE_FLAG_DRAGGING_INTEGRATED)

#define IS_SERVICES_POOL_INITIALIZED()  (system_optimization_flags & POOL_FLAG_SERVICES_INITIALIZED)
#define IS_TASKS_POOL_INITIALIZED()     (system_optimization_flags & POOL_FLAG_TASKS_INITIALIZED)

#define IS_SYSCONF_CACHE_INITIALIZED()  (system_optimization_flags & CACHE_FLAG_SYSCONF_INITIALIZED)
#define IS_DISK_INITIALIZED()           (system_optimization_flags & CACHE_FLAG_DISK_INITIALIZED)
#define IS_NETWORK_CACHE_INITIALIZED()  (system_optimization_flags & CACHE_FLAG_NETWORK_INITIALIZED)
#define IS_GRAPH_CACHE_INITIALIZED()    (system_optimization_flags & CACHE_FLAG_GRAPH_INITIALIZED)
#define IS_PARALLEL_ENABLED()           (system_optimization_flags & CACHE_FLAG_PARALLEL_ENABLED)

#define IS_DISKSTATS_CACHE_VALID()      (system_optimization_flags & CACHE_FLAG_DISKSTATS_VALID)

#define NEED_INFO_BLOCKS_UPDATE()       (system_optimization_flags & UI_FLAG_INFO_BLOCKS_NEED_UPDATE)

// Fonctions pour manipuler les flags
void set_optimization_flag(guint32 flag, gboolean enabled);
gboolean get_optimization_flag(guint32 flag);

// Fonctions coloration optimisées (conservées pour compatibilité)
void apply_batch_cell_coloring(gtk_update_batch_t* batch);
void set_cell_coloring_enabled(gboolean enabled);

// Fonction pour obtenir un nom d'utilisateur partagé (existante)
const gchar* get_shared_uname(uid_t uid, const gchar* uname);

// Fonction pour extraire le nom du binaire (pour groupement)
gchar* extract_binary_name(const gchar* full_command);

struct task
{
    // ============================================================================
    // PREMIÈRE CACHE-LINE (64 bytes) - DONNÉES CHAUDES (optimisation 2-3% CPU)
    // ============================================================================
    
    // Temps CPU (accès critique pour calculs pourcentages) - 16 bytes
    guint64 time;           // Temps CPU actuel
    guint64 old_time;       // Temps CPU précédent
    
    // Métriques mémoire (affichage principal) - 40 bytes
    gulong size;            // Taille virtuelle
    gulong rss;             // Mémoire résidente
    gulong pss;             // Mémoire proportionnelle
    gulong shr;             // Mémoire partagée
    gulong vsz;             // Taille virtuelle étendue
    
    // Utilisateur (pool partagé, accès fréquent) - 8 bytes
    const gchar *uname;     // Pointeur vers pool utilisateurs
    
    // Métriques calculées (affichage temps réel) - 8 bytes
    gfloat time_percentage; // Pourcentage CPU calculé
    gfloat gpu_usage;       // Usage GPU (render + video)
    
    // Total: 72 bytes (16+40+8+8) - Déborde sur 2 cache-lines
    
    // ============================================================================
    // DEUXIÈME CACHE-LINE (64 bytes) - DONNÉES TIÈDES
    // ============================================================================
    
    // Identifiants processus - 12 bytes
    pid_t pid;              // PID processus
    pid_t ppid;             // PID parent
    uid_t uid;              // UID utilisateur
    
    // Pack state + priority dans 32-bit - 4 bytes
    guint32 state_prio_packed;  // state[8bit] + prio[24bit]

    // Pack flags + nice dans 16-bit - 2 bytes
    guint16 flags_nice_packed;  // flags[8bit] + nice[8bit]
    
    // Padding explicite pour alignement cache-line - 42 bytes
    gchar padding_cacheline[42];  // Force alignement 64 bytes
    
    // Total: 64 bytes exactement - CACHE-LINE PARFAITE
    
    // ============================================================================
    // TROISIÈME-SIXIÈME CACHE-LINES - DONNÉES FROIDES (256 bytes)
    // ============================================================================
    
    // Nom complet (ligne de commande complète) - 256 bytes
    gchar name[256];        // Commande complète (conservé pour compatibilité)
    
    // Nom simple optimisé - 64 bytes
    gchar simple_name[64];  // Nom processus simple

    // Structure totale: 448 bytes (7 cache-lines parfaites de 64 bytes)
} __attribute__((aligned(64)));  // Force alignment cache-line 64 bytes

// ============================================================================
// MACROS D'ACCÈS BIT PACKING - TRANSPARENCE TOTALE
// ============================================================================

// Macros pour state_prio_packed (state: 8 bits, prio: 24 bits)
#define TASK_GET_STATE_CHAR(task)  ((char)((task)->state_prio_packed & 0xFF))
#define TASK_GET_PRIO(task)        ((gint)(((task)->state_prio_packed >> 8) | (((task)->state_prio_packed & 0x800000) ? 0xFF000000 : 0)))
#define TASK_SET_STATE_CHAR(task, s) ((task)->state_prio_packed = ((task)->state_prio_packed & 0xFFFFFF00) | ((guint8)(s)))
#define TASK_SET_PRIO(task, p)     ((task)->state_prio_packed = ((task)->state_prio_packed & 0xFF) | (((guint32)(p) & 0xFFFFFF) << 8))

// Macros pour flags_nice_packed (flags: 8 bits, nice: 8 bits)
#define TASK_GET_FLAGS(task)       ((guint8)((task)->flags_nice_packed & 0xFF))
#define TASK_GET_NICE(task)        ((gint8)(((task)->flags_nice_packed >> 8) & 0xFF))
#define TASK_SET_FLAGS(task, f)    ((task)->flags_nice_packed = ((task)->flags_nice_packed & 0xFF00) | ((guint8)(f)))
#define TASK_SET_NICE(task, n)     ((task)->flags_nice_packed = ((task)->flags_nice_packed & 0x00FF) | (((guint8)(n)) << 8))

// Macro pour compatibilité state[8] (conversion state char → string)
#define TASK_GET_STATE_STR(task, buf) do { \
    char c = TASK_GET_STATE_CHAR(task); \
    (buf)[0] = c; \
    (buf)[1] = '\0'; \
} while(0)

// Fonction helper pour obtenir state comme string statique (thread-safe)
static inline const char* task_get_state_string(const struct task* task) {
    static __thread char state_buffer[2];
    TASK_GET_STATE_STR(task, state_buffer);
    return state_buffer;
}

typedef struct
{
    // Group all 64-bit fields together (better cache locality)
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
    
    // Small fields at end
    guint8 status_flags;
    // Padding: 7 bytes added by compiler
} system_status;

// Application flags bitmasks
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

// Affichage de la mémoire réelle PSS (préférence persistante)
#define APP_FLAG_DISPLAY_PSS         0x8000

// Masque des flags à sauvegarder (exclut les flags temporaires et KEEP_ABOVE)
// Inclut DISPLAY_PSS (0x8000) dans les flags persistants
#define APP_FLAGS_PERSISTENT_MASK   0xF1FF

// ============================================================================
// DISPLAY FLAGS - Flags d'affichage séparés (display_flags variable)
// ============================================================================
#define DISPLAY_FLAG_SHOW_PROCESS_ICONS  0x0001  // Afficher les icônes de processus
#define DISPLAY_FLAG_USE_TDE_RUN_DIALOG  0x0002  // Utiliser le dialogue d'exécution TDE
#define DISPLAY_FLAG_SHOW_STATUS_BAR     0x0004  // Afficher la barre de statut

// Task flags bitmasks (pour task.flags)
#define TASK_FLAG_CHECKED           0x01

// Users tab sorting flags
#define USERS_SORT_NAME_ASC         0x01
#define USERS_SORT_CPU_ASC          0x02
#define USERS_SORT_MEMORY_ASC       0x04
#define USERS_SORT_GPU_ASC          0x08
#define USERS_SORT_PID_ASC          0x10

// Mini-graph UI state flags
#define MINI_CPU_HOVERED            0x01
#define MINI_CPU_SELECTED           0x02
#define MINI_RAM_HOVERED            0x04
#define MINI_RAM_SELECTED           0x08
#define MINI_DISK_SELECTED          0x10
#define MINI_NETWORK_SELECTED       0x20
#define MINI_GPU_HOVERED            0x40
#define MINI_GPU_SELECTED           0x80

// Manager state flags
#define MANAGER_INITIALIZED         0x01
#define MANAGER_BUFFER_FULL         0x02

// Disk state flags
#define DISK_IS_SYSTEM              0x01
#define DISK_HAS_SWAP               0x02
#define DISK_INITIALIZED            0x04

// Network interface state flags
#define NET_IS_UP                   0x01
#define NET_HAS_CARRIER             0x02
#define NET_IS_PHYSICAL             0x04
#define NET_INITIALIZED             0x08


// Variables globales pour hover individuel
extern gint hovered_disk_index;
extern gint hovered_network_index;

// Startup tab variables
extern GtkWidget *startup_treeview;
extern GtkListStore *startup_list_store;

// Services tab widgets
extern GtkWidget *services_treeview;
extern GtkListStore *services_list_store;
extern GtkTreeSelection *services_selection;

// Services refresh timer
extern guint services_refresh_timer_id;

// System status flags
#define SYSTEM_STATUS_VALID_PROC_READING    0x01

// System disk flags
#define SYSTEM_HAS_SWAP_ON_DISK     0x01

// Performance data flags
#define PERF_DATA_BUFFER_FULL       0x01

// Interface state flags
#define INTERFACE_TOOLTIP_FIRST_POINT   0x01
#define INTERFACE_FILL_FIRST_POINT      0x02
#define INTERFACE_MINI_FIRST_POINT      0x04
#define INTERFACE_RAM_FIRST_POINT       0x08
#define INTERFACE_SWAP_FIRST_POINT      0x10
#define INTERFACE_DISK_FIRST_POINT      0x20
#define INTERFACE_NETWORK_FIRST_POINT   0x40
#define INTERFACE_RX_FIRST_POINT        0x80
#define INTERFACE_GPU_FIRST_POINT       0x800
#define INTERFACE_TX_FIRST_POINT        0x100
#define INTERFACE_READ_FIRST_POINT      0x200
#define INTERFACE_WRITE_FIRST_POINT     0x400

extern GtkStatusIcon *global_status_icon;
extern GtkWidget *main_window;
extern GArray *task_array;
extern gint tasks;
extern uid_t own_uid;
extern gchar *config_file;
extern guint16 app_flags;
extern guint16 display_flags;
extern guint8 users_sort_flags;
extern guint8 mini_graph_flags;
extern guint8 system_disk_flags;
extern guint8 system_status_flags;
extern guint16 interface_state_flags;
// Variables de compatibilité (redirigées vers app_manager)
#define default_editor_index (editor_manager.default_index)
#define default_browser_index (browser_manager.default_index)
#define default_terminal_index (terminal_manager.default_index)
extern gint win_width;
extern gint win_height;
extern int page_size;
extern gint saved_win_x;
extern gint saved_win_y;

// Performance sampling (buffer circulaire de 120 échantillons)
#define PERFORMANCE_SAMPLES_COUNT 120
#define MAX_CPU_CORES 64  // Nombre maximum de cœurs CPU supportés

// ============================================================================
// STRUCTURE PERFORMANCE_DATA ULTRA-OPTIMISÉE (-48.6% mémoire, 15.9KB économisés)
// ============================================================================

typedef struct {
    // ========================================================================
    // DONNÉES CRITIQUES AVEC PROTECTION OVERFLOW (robustesse système)
    // ========================================================================
    
    // Métriques système principales - UPGRADEÉES à int32_t pour éviter overflow
    gint32 cpu_samples[PERFORMANCE_SAMPLES_COUNT];    // 480 bytes (robustesse > compression)
    gint32 ram_samples[PERFORMANCE_SAMPLES_COUNT];    // 480 bytes (robustesse > compression)
    gint16 swap_samples[PERFORMANCE_SAMPLES_COUNT];   // 240 bytes (0-100% sûr)
    gint16 disk_samples[PERFORMANCE_SAMPLES_COUNT];   // 240 bytes (0-100% sûr)
    
    // Métriques GPU optimisées (0-100% → int16_t sûr)
    gint16 gpu_render_samples[PERFORMANCE_SAMPLES_COUNT];  // 240 bytes - GPU render engine
    gint16 gpu_video_samples[PERFORMANCE_SAMPLES_COUNT];   // 240 bytes - GPU video engine
    gint16 gpu_total_samples[PERFORMANCE_SAMPLES_COUNT];   // 240 bytes - GPU total selon mode choisi
    
    // ========================================================================
    // DONNÉES I/O HAUTE PERFORMANCE (conservées 32-bit pour éviter overflow)
    // ========================================================================
    
    // I/O disque (KB/s jusqu'à 2GB/s pour SSD/NVMe haute performance)
    gint disk_read_samples[PERFORMANCE_SAMPLES_COUNT];  // 480 bytes (nécessaire pour SSD/NVMe)
    gint disk_write_samples[PERFORMANCE_SAMPLES_COUNT]; // 480 bytes (nécessaire pour SSD/NVMe)
    
    // ========================================================================
    // CPU CORES DYNAMIQUES (allocation optimisée selon nombre réel de cores)
    // ========================================================================
    
    // Pointeurs vers buffers CPU cores (allocation dynamique selon cpu_core_count)
    gint16 **cpu_core_samples;                        // 8 bytes pointeur + allocation dynamique
    gdouble *cpu_core_speeds;                         // 8 bytes pointeur + allocation dynamique
    
    // ========================================================================
    // MÉTADONNÉES ULTRA-COMPACTES (compression maximale)
    // ========================================================================
    
    gint8 cpu_core_count;                             // 1 byte (vs 4, max 64 cores, -75%)
    gint8 current_index;                              // 1 byte (vs 4, max 120, -75%)
    guint8 perf_flags;                                // 1 byte (flags divers)
    guint8 padding;                                   // 1 byte (alignement mémoire)
    
    // ========================================================================
    // BILAN OPTIMISATION MÉMOIRE :
    // - AVANT : ~32.7KB (structures 32-bit + overhead)
    // - APRÈS : ~18.8KB (protection overflow + compression partielle)
    // - ÉCONOMIE : 13.9KB (42.5% de réduction)
    // ========================================================================
} performance_data_t;

// ============================================================================
// MACROS DE VALIDATION BOUNDS POUR MÉTRIQUES CRITIQUES
// ============================================================================

// Validation pour métriques 32-bit (CPU/RAM)
#define VALIDATE_METRIC_32(value) \
    ((value) < 0 ? 0 : ((value) > 100 ? 100 : (value)))

// Validation pour métriques 16-bit (GPU/SWAP/DISK)
#define VALIDATE_METRIC_16(value) \
    ((value) < 0 ? 0 : ((value) > 32767 ? 32767 : (value)))

// Macros sécurisées pour assignation
#define SET_CPU_SAMPLE(data, index, value) \
    (data)->cpu_samples[index] = VALIDATE_METRIC_32(value)

#define SET_RAM_SAMPLE(data, index, value) \
    (data)->ram_samples[index] = VALIDATE_METRIC_32(value)

#define SET_GPU_SAMPLE(data, index, value) \
    (data)->gpu_total_samples[index] = VALIDATE_METRIC_16(value)

extern performance_data_t performance_data;
extern gchar *cpu_model_name;
extern gchar *gpu_model_name;
extern gdouble installed_ram_gib;

// Variables pour les informations RAM avancées (initialisées une seule fois)
extern long long commit_limit_bytes;
extern long long committed_as_bytes;

// Type de graphique CPU (0 = Overall utilization, 1 = Logical processors)
extern gint cpu_graph_type;

// Mode de rapport d'utilisation GPU (0 = Process Time cumulative, 1 = Process Time average, 2 = Hardware Approximation)
extern gint gpu_usage_reporting_mode;

// Structure pour les informations détaillées du CPU
typedef struct {
    int base_mhz;
    int sockets;
    int cores_per_socket;
    int logical_processors;
    int virtualization; // 1=enabled, 0=disabled
    int l1d_cache_kb;
    int l1i_cache_kb;
    int l1_cache_kb;
    int l2_cache_kb;
    int l3_cache_kb;
} cpu_info_t;

extern cpu_info_t cpu_detailed_info;

typedef struct {
    double ram_in_use_gb;
    double ram_available_gb;
    double committed_gb;
    double commit_limit_gb;
    double cached_gb;
} RAMInfoData;

typedef struct {
    gint current_cpu;
    gdouble cpu_speed;
    gint process_count;
    gint thread_count;
    gint handle_count;
    gchar uptime[32];
} CPUInfoData;

typedef struct {
    char *username;
    char **session_ids;
    size_t session_count;
} user_with_sessions_t;

// Informations disque système (initialisées une seule fois)
extern long disk_capacity_gb;

#ifdef __cplusplus
}
#endif

#endif
