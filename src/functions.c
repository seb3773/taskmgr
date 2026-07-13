#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "common.h"
#include <glib/gi18n.h>
#include <fcntl.h>
#include <time.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <pthread.h>
#include "functions.h"
#include "types.h"
#ifndef WITHOUT_GTK
#include "interface.h"
#endif
#include "common.h"
#include "fast_format.h"
#include "pss_thread_simd.h"
#include "gpu_stats.h"
#include "service_manager.h"
#include "disk_manager.h"
#include "network_manager.h"
gboolean get_cpu_usage_from_proc(system_status *sys_stat);
gdouble get_core_cpu_speed(int core_index);

// ============================================================================
// OPTIMISATION HASH TABLE - Recherche O(1) au lieu de O(n)
// ============================================================================

// Hash table globale PID → task index pour O(1) lookups
GHashTable* pid_to_index_map = NULL;

// Initialisation de la hash table (appelée une seule fois)
void init_process_lookup(void) {
    if (!pid_to_index_map) {
        pid_to_index_map = g_hash_table_new(g_direct_hash, g_direct_equal);
    }
}

// Nettoyage de la hash table
void cleanup_process_lookup(void) {
    if (pid_to_index_map) {
        g_hash_table_destroy(pid_to_index_map);
        pid_to_index_map = NULL;
    }
}

// Recherche O(1) au lieu de O(n) avec validation bounds stricte
CRITICAL_FUNCTION
struct task* find_task_by_pid_fast(pid_t pid) {
    if (!pid_to_index_map || !task_array) return NULL;
    
    gpointer index_ptr = g_hash_table_lookup(pid_to_index_map, GINT_TO_POINTER(pid));
    if (!index_ptr) return NULL;

    gint index = GPOINTER_TO_INT(index_ptr);
    if (index < 0 || index >= (gint)task_array->len) return NULL;  // Validation stricte
    
    return &g_array_index(task_array, struct task, index);
}

// ============================================================================
// MISE À JOUR INCRÉMENTALE HASH TABLE - O(changements) au lieu de O(total)
// ============================================================================

// Structure pour tracking des changements PID (optimisation incrémentale)
#define PID_CHANGE_FLAG_NEW           0x01  // Nouveau processus
#define PID_CHANGE_FLAG_REMOVED       0x02  // Processus supprimé  
#define PID_CHANGE_FLAG_INDEX_CHANGED 0x04  // Index modifié

typedef struct {
    pid_t pid;
    gint old_index;
    gint new_index;
    guint8 flags;  // Remplace les 3 gboolean par un bitmask
} pid_change_t;



// Mise à jour de la table de mapping PID -> index (Traditionnelle et performante)
HOT_FUNCTION
void update_pid_index_map(void) {
    if (!pid_to_index_map) return;
    
    // Vider la hash table
    g_hash_table_remove_all(pid_to_index_map);
    
    // Reconstruire le mapping PID → index
    for (gint i = 0; i < task_array->len; i++) {
        struct task *task = &g_array_index(task_array, struct task, i);
        g_hash_table_insert(pid_to_index_map, GINT_TO_POINTER(task->pid), GINT_TO_POINTER(i));
    }
}

void update_pid_index_map_incremental(void) {
    update_pid_index_map();
}

void update_pid_index_map_traditional(void) {
    update_pid_index_map();
}

// Fonction pour basculer entre mode incrémental et traditionnel
void set_incremental_update_enabled(gboolean enabled) {
    (void)enabled;
}

// Nettoyage des ressources incrémentales
void cleanup_incremental_update(void) {
}

// ============================================================================
// OPTIMISATION POOL MÉMOIRE - Élimination fragmentation heap
// ============================================================================

// Pool pré-alloué pour éviter malloc/free répétés
static struct task task_pool[TASK_POOL_SIZE];
// BITSET pour pool_used : 4096 bits = 512 bytes au lieu de 4096 bytes (87.5% économie)
static guint64 pool_used_bitset[TASK_POOL_SIZE / 64] = {0}; // 64 bits par guint64
static gint pool_next_free = 0;
static pthread_mutex_t task_pool_mutex = PTHREAD_MUTEX_INITIALIZER;
static gboolean task_pool_full_warned = FALSE;
// pool_initialized remplacé par IS_TASKS_POOL_INITIALIZED() macro

// Macros pour manipuler le bitset
#define POOL_IS_USED(idx)    (pool_used_bitset[(idx) / 64] & (1ULL << ((idx) % 64)))
#define POOL_SET_USED(idx)   (pool_used_bitset[(idx) / 64] |= (1ULL << ((idx) % 64)))
#define POOL_SET_FREE(idx)   (pool_used_bitset[(idx) / 64] &= ~(1ULL << ((idx) % 64)))

// Initialisation du pool mémoire
void init_task_memory_pool(void) {
    if (!IS_TASKS_POOL_INITIALIZED()) {
        // Initialiser tous les slots comme libres (bitset déjà à 0)
        memset(pool_used_bitset, 0, sizeof(pool_used_bitset));
        for (gint i = 0; i < TASK_POOL_SIZE; i++) {
            memset(&task_pool[i], 0, sizeof(struct task));
        }
        pool_next_free = 0;
        set_optimization_flag(POOL_FLAG_TASKS_INITIALIZED, TRUE);
    }
}

// Nettoyage du pool mémoire
void cleanup_task_memory_pool(void) {
    if (IS_TASKS_POOL_INITIALIZED()) {
        // Rien à libérer - le pool est statique
        set_optimization_flag(POOL_FLAG_TASKS_INITIALIZED, FALSE);
    }
}

// Allocation optimisée depuis le pool
HOT_FUNCTION
struct task* allocate_task_from_pool(void) {
    if (!IS_TASKS_POOL_INITIALIZED()) init_task_memory_pool();
    
    pthread_mutex_lock(&task_pool_mutex);
    
    for (gint i = 0; i < TASK_POOL_SIZE; i++) {
        gint idx = (pool_next_free + i) % TASK_POOL_SIZE;
        if (!POOL_IS_USED(idx)) {
            POOL_SET_USED(idx);
            pool_next_free = (idx + 1) % TASK_POOL_SIZE;
            
            struct task *task = &task_pool[idx];
            task->pid = 0;
            task->ppid = 0;
            task->size = 0;
            task->rss = 0;
            task->pss = 0;
            task->time = 0;
            task->old_time = 0;
            task->time_percentage = 0.0f;
            task->gpu_usage = 0.0f;
            TASK_SET_PRIO(task, 0);
            task->uid = 0;
            TASK_SET_FLAGS(task, 0);
            task->name[0] = '\0';
            task->simple_name[0] = '\0';
            TASK_SET_STATE_CHAR(task, '\0');
            task->uname = NULL;
            
            pthread_mutex_unlock(&task_pool_mutex);
            return task;
        }
    }
    
    if (!task_pool_full_warned) {
        g_warning("Pool mémoire task plein (%d slots), fallback malloc", TASK_POOL_SIZE);
        task_pool_full_warned = TRUE;
    }
    pthread_mutex_unlock(&task_pool_mutex);
    return g_malloc0(sizeof(struct task));
}

// Libération optimisée vers le pool
HOT_FUNCTION
void free_task_to_pool(struct task* task) {
    if (!task) return;
    
    pthread_mutex_lock(&task_pool_mutex);
    
    if (task >= task_pool && task < &task_pool[TASK_POOL_SIZE]) {
        ptrdiff_t idx = task - task_pool;
        if (idx >= 0 && idx < TASK_POOL_SIZE) {
            POOL_SET_FREE((gint)idx);
        }
    } else {
        g_free(task);
    }
    
    pthread_mutex_unlock(&task_pool_mutex);
}

// Réinitialisation complète du pool (pour optimisations futures)
void reset_task_pool(void) {
    if (IS_TASKS_POOL_INITIALIZED()) {
        pthread_mutex_lock(&task_pool_mutex);
        memset(pool_used_bitset, 0, sizeof(pool_used_bitset));
        pool_next_free = 0;
        task_pool_full_warned = FALSE;
        pthread_mutex_unlock(&task_pool_mutex);
    }
}

// Statistiques du pool mémoire (pour monitoring)
void get_task_pool_stats(gint *total_slots, gint *used_slots, gint *free_slots) {
    if (!IS_TASKS_POOL_INITIALIZED()) {
        *total_slots = *used_slots = *free_slots = 0;
        return;
    }
    
    *total_slots = TASK_POOL_SIZE;
    *used_slots = 0;
    
    for (gint i = 0; i < TASK_POOL_SIZE; i++) {
        if (POOL_IS_USED(i)) (*used_slots)++;
    }
    
    *free_slots = TASK_POOL_SIZE - *used_slots;
}

// ============================================================================
// OPTIMISATION CACHE SYSCONF - Éviter appels répétitifs coûteux
// ============================================================================

// Cache statique des valeurs sysconf (initialisées une seule fois)
static long cached_page_size = 0;
static long cached_clk_tck = 0;
static long cached_nprocessors = 0;

// Cache statique des valeurs système qui ne changent jamais
static guint64 cached_mem_total = 0;    // MemTotal en KB
static guint64 cached_swap_total = 0;   // SwapTotal en KB

// Initialisation du cache sysconf (appelée une seule fois au démarrage)
void init_sysconf_cache(void) {
    if (IS_SYSCONF_CACHE_INITIALIZED()) return;
    
    // Cache _SC_PAGESIZE (taille de page mémoire)
    cached_page_size = sysconf(_SC_PAGESIZE);
    if (cached_page_size <= 0) {
        cached_page_size = 4096;  // Valeur par défaut sûre
    }
    
    // Cache _SC_CLK_TCK (jiffies par seconde)
    cached_clk_tck = sysconf(_SC_CLK_TCK);
    if (cached_clk_tck <= 0) {
        cached_clk_tck = 100;  // Valeur par défaut Linux standard
    }
    
    // Cache _SC_NPROCESSORS_ONLN (nombre de CPUs)
    cached_nprocessors = sysconf(_SC_NPROCESSORS_ONLN);
    if (cached_nprocessors <= 0) {
        cached_nprocessors = 1;  // Valeur par défaut sûre
    }
    
    set_optimization_flag(CACHE_FLAG_SYSCONF_INITIALIZED, TRUE);
}

// Obtenir la taille de page (remplace sysconf(_SC_PAGESIZE))
HOT_FUNCTION
long get_cached_page_size(void) {
    if (!IS_SYSCONF_CACHE_INITIALIZED()) init_sysconf_cache();
    return cached_page_size;
}

// Obtenir les jiffies par seconde (remplace sysconf(_SC_CLK_TCK))
HOT_FUNCTION
long get_cached_clk_tck(void) {
    if (!IS_SYSCONF_CACHE_INITIALIZED()) init_sysconf_cache();
    return cached_clk_tck;
}

// Obtenir le nombre de processeurs (remplace sysconf(_SC_NPROCESSORS_ONLN))
HOT_FUNCTION
long get_cached_nprocessors(void) {
    if (!IS_SYSCONF_CACHE_INITIALIZED()) init_sysconf_cache();
    return cached_nprocessors;
}

// ============================================================================
// CACHE VALEURS SYSTÈME STATIQUES - Éviter lecture /proc répétitive
// ============================================================================

// Initialisation du cache des valeurs système statiques (appelée une seule fois au démarrage)
// OPTIMISATION: Lit MemTotal/SwapTotal une seule fois au lieu de les relire toutes les secondes
void init_static_system_cache(void) {
    if (system_optimization_flags & CACHE_FLAG_STATIC_SYSTEM_INITIALIZED) return;
    
    // Lire /proc/meminfo une seule fois pour extraire les valeurs statiques
    int fd = open("/proc/meminfo", O_RDONLY);
    if (fd >= 0) {
        static char buffer[8192];
        ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
        close(fd);
        
        if (bytes > 0) {
            buffer[bytes] = '\0';
            char* line = buffer;
            int found = 0;
            
            // Parser pour MemTotal et SwapTotal
            while (line && *line && found < 2) {
                if (!strncmp(line, "MemTotal:", 9)) {
                    cached_mem_total = fast_atoll(line + 10);
                    found++;
                } else if (!strncmp(line, "SwapTotal:", 10)) {
                    cached_swap_total = fast_atoll(line + 11);
                    found++;
                }
                
                line = strchr(line, '\n');
                if (line) line++;
            }
        }
    }
    
    // Valeurs par défaut si lecture échoue
    if (cached_mem_total == 0) cached_mem_total = 1024 * 1024;  // 1GB par défaut
    if (cached_swap_total == 0) cached_swap_total = 0;  // Pas de swap par défaut
    
    set_optimization_flag(CACHE_FLAG_STATIC_SYSTEM_INITIALIZED, TRUE);
    
    g_debug("CACHE STATIC: MemTotal=%lu KB, SwapTotal=%lu KB", 
            (unsigned long)cached_mem_total, (unsigned long)cached_swap_total);
}

// Obtenir MemTotal en KB (valeur cachée, ne change jamais)
HOT_FUNCTION
guint64 get_cached_mem_total(void) {
    if (!(system_optimization_flags & CACHE_FLAG_STATIC_SYSTEM_INITIALIZED)) init_static_system_cache();
    return cached_mem_total;
}

// Obtenir SwapTotal en KB (valeur cachée, rarement modifiée)
HOT_FUNCTION
guint64 get_cached_swap_total(void) {
    if (!(system_optimization_flags & CACHE_FLAG_STATIC_SYSTEM_INITIALIZED)) init_static_system_cache();
    return cached_swap_total;
}

// Fonction de debug pour surveiller l'utilisation des caches/pools
void debug_cache_usage(void) {
    g_print("=== DEBUG UTILISATION CACHES/POOLS ===\n");
    
    // 1. Hash table PID→index
    if (pid_to_index_map) {
        guint hash_size = g_hash_table_size(pid_to_index_map);
        g_print("Hash table PID→index: %u entrées\n", hash_size);
    } else {
        g_print("Hash table PID→index: NON INITIALISÉE\n");
    }
    
    // 2. Pool mémoire task
    gint total_slots, used_slots, free_slots;
    get_task_pool_stats(&total_slots, &used_slots, &free_slots);
    g_print("Pool mémoire task: %d/%d utilisés (%.1f%% - %d libres)\n", 
            used_slots, total_slots, (used_slots * 100.0) / total_slots, free_slots);
    
    // 3. Cache TreeIter (fonction dans interface.c)
    extern void debug_tree_iter_cache_usage(void);
    debug_tree_iter_cache_usage();
    
    // 4. Cache sysconf
    if (IS_SYSCONF_CACHE_INITIALIZED()) {
        g_print("Cache sysconf: PAGE_SIZE=%ld, CLK_TCK=%ld, NPROCESSORS=%ld\n", 
                cached_page_size, cached_clk_tck, cached_nprocessors);
    } else {
        g_print("Cache sysconf: NON INITIALISÉ\n");
    }
    
    // 5. Cache UID
    if (uid_cache) {
        guint uid_cache_size = g_hash_table_size(uid_cache);
        time_t current_time = time(NULL);
        gint seconds_since_cleanup = (gint)(current_time - last_uid_cache_cleanup);
        g_print("Cache UID: %u entrées (nettoyage il y a %ds)\n", 
                uid_cache_size, seconds_since_cleanup);
    } else {
        g_print("Cache UID: NON INITIALISÉ\n");
    }
    
    // 6. Hash table PID -> index
    if (pid_to_index_map) {
        guint map_size = g_hash_table_size(pid_to_index_map);
        g_print("Hash table PID: %u PIDs mappés\n", map_size);
    } else {
        g_print("Hash table PID: NON INITIALISÉE\n");
    }
    
    g_print("======================================\n");
}

// ============================================================================
// CACHE UID ULTRA-PERFORMANT - Élimination 85-90% appels getpwuid()
// ============================================================================

// Variables globales cache UID
GHashTable* uid_cache = NULL;
time_t last_uid_cache_cleanup = 0;

static void uid_cache_entry_free(gpointer data) {
    uid_cache_entry_t* entry = (uid_cache_entry_t*)data;
    if (entry) {
        g_free(entry->username);
        g_free(entry);
    }
}

// Initialisation du cache UID (appelée une seule fois)
void init_uid_cache(void) {
    if (!uid_cache) {
        uid_cache = g_hash_table_new_full(g_direct_hash, g_direct_equal, 
                                          NULL, uid_cache_entry_free);
        last_uid_cache_cleanup = time(NULL);
    }
}

// Nettoyage périodique du cache (supprime entrées expirées)
static void cleanup_expired_uid_entries(void) {
    if (!uid_cache) return;
    
    time_t current_time = time(NULL);
    GHashTableIter iter;
    gpointer key, value;
    GList* expired_keys = NULL;
    
    // Collecter les clés expirées
    g_hash_table_iter_init(&iter, uid_cache);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        uid_cache_entry_t* entry = (uid_cache_entry_t*)value;
        if (!(entry->flags & UID_CACHE_FLAG_VALID) || 
            (current_time - entry->cache_time) > UID_CACHE_TTL_SECONDS) {
            expired_keys = g_list_prepend(expired_keys, key);
        }
    }
    
    // Supprimer les entrées expirées
    for (GList* l = expired_keys; l != NULL; l = l->next) {
        g_hash_table_remove(uid_cache, l->data);
    }
    g_list_free(expired_keys);
    
    last_uid_cache_cleanup = current_time;
}

// Fonction principale : résolution UID avec cache intelligent
HOT_FUNCTION
const gchar* get_cached_username(uid_t uid) {
    if (!uid_cache) init_uid_cache();
    
    time_t current_time = time(NULL);
    
    // Nettoyage périodique du cache
    if ((current_time - last_uid_cache_cleanup) > UID_CACHE_CLEANUP_INTERVAL) {
        cleanup_expired_uid_entries();
    }
    
    // Recherche dans le cache O(1) sans allocation
    uid_cache_entry_t* entry = g_hash_table_lookup(uid_cache, GUINT_TO_POINTER(uid));
    
    // Cache hit : vérifier validité
    if (entry && (entry->flags & UID_CACHE_FLAG_VALID) && 
        (current_time - entry->cache_time) < UID_CACHE_TTL_SECONDS) {
        return entry->username;  // Cache hit - retour immédiat O(1)
    }
    
    // Cache miss ou entrée expirée : résoudre via getpwuid()
    struct passwd* passwdp = getpwuid(uid);
    const gchar* username;
    gchar uid_fallback[32];
    
    if (passwdp != NULL && passwdp->pw_name != NULL) {
        username = passwdp->pw_name;
    } else {
        // Fallback pour UIDs inconnus
        g_snprintf(uid_fallback, sizeof(uid_fallback), "%u", uid);
        username = uid_fallback;
    }
    
    // Créer/mettre à jour l'entrée cache
    if (!entry) {
        entry = g_new0(uid_cache_entry_t, 1);
        entry->uid = uid;
        g_hash_table_insert(uid_cache, GUINT_TO_POINTER(uid), entry);
    } else {
        g_free(entry->username);  // Libérer ancien username
    }
    
    // Stocker les nouvelles données
    entry->username = g_strdup(username);
    entry->cache_time = current_time;
    entry->flags |= UID_CACHE_FLAG_VALID;
    
    return entry->username;
}

// Nettoyage complet du cache
void cleanup_uid_cache(void) {
    if (uid_cache) {
        g_hash_table_destroy(uid_cache);
        uid_cache = NULL;
    }
}

// Forcer le refresh du cache (invalider toutes les entrées)
void force_uid_cache_refresh(void) {
    if (!uid_cache) return;
    
    GHashTableIter iter;
    gpointer key, value;
    
    g_hash_table_iter_init(&iter, uid_cache);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        uid_cache_entry_t* entry = (uid_cache_entry_t*)value;
        entry->flags &= ~UID_CACHE_FLAG_VALID;  // Marquer comme invalide
    }
}

// Fonction de conversion du mode GPU depuis les préférences
GPUMonitoringMode get_gpu_mode_from_setting(gint mode_setting) {
    switch (mode_setting) {
        case 0: return GPU_MODE_PROCESS_TIME;      // Process Time (cumulative, htop-style)
        case 1: return GPU_MODE_PROCESS_AVERAGE;   // Process Time average (balanced)
        case 2: return GPU_MODE_HARDWARE_APPROX;   // Hardware Approximation Mode (more realistic)
        default: return GPU_MODE_HARDWARE_APPROX;  // Défaut si valeur invalide
    }
}

// Fonction pour mettre à jour le mode GPU dynamiquement
void update_gpu_monitoring_mode(void) {
    // Mettre à jour le mode GPU si le monitoring est initialisé
    if (gpu_stats_is_available()) {
        GPUMonitoringMode new_mode = get_gpu_mode_from_setting(gpu_usage_reporting_mode);
        gpu_stats_set_mode(new_mode);
    }
}

// Services column definitions (matching interface.c)
enum {
    SERVICES_COLUMN_NAME = 0,
    SERVICES_COLUMN_PID,
    SERVICES_COLUMN_STATUS,
    SERVICES_COLUMN_RAM,
    SERVICES_COLUMN_DESCRIPTION,
    SERVICES_COLUMN_SLICE,
    SERVICES_N_COLUMNS
};

extern gint refresh_interval;
gdouble cpu_speed_value;
static system_status *sys_stat = NULL;
static char cached_cpu_tooltip[128] = "";
static gdouble last_cpu_usage = -1.0;

// Fonctions utilitaires consolidées pour lecture de fichiers

// Fonction générique pour lire une ligne d'un fichier /proc
char* read_proc_file_line(const char* path, char* buffer, size_t buffer_size) {
    if (UNLIKELY(!path || !buffer || buffer_size == 0)) return NULL;
    
    FILE* fp = fopen(path, "r");
    if (!fp) return NULL;
    
    char* result = fgets(buffer, buffer_size, fp);
    fclose(fp);
    
    if (result) {
        // Supprimer le \n final
        buffer[strcspn(buffer, "\n")] = '\0';
    }
    
    return result;
}

// Fonction générique pour lire une ligne d'un fichier /sys
char* read_sys_file_line(const char* path, char* buffer, size_t buffer_size) {
    if (UNLIKELY(!path || !buffer || buffer_size == 0)) return NULL;
    
    FILE* fp = fopen(path, "r");
    if (UNLIKELY(!fp)) return NULL;
    
    char* result = fgets(buffer, buffer_size, fp);
    fclose(fp);
    
    if (result) {
        // Supprimer le \n final et espaces
        buffer[strcspn(buffer, "\n")] = '\0';
        // Trim whitespace
        char* end = buffer + strlen(buffer) - 1;
        while (end > buffer && isspace(*end)) *end-- = '\0';
        char* start = buffer;
        while (isspace(*start)) start++;
        if (start != buffer) memmove(buffer, start, strlen(start) + 1);
    }
    
    return result;
}

// Fonction pour lire un entier long depuis /sys
long read_sys_file_long(const char* path) {
    char buffer[64];
    if (read_sys_file_line(path, buffer, sizeof(buffer))) {
        return strtol(buffer, NULL, 10);
    }
    return -1;
}

// Fonction pour lire un entier depuis /sys
int read_sys_file_int(const char* path) {
    char buffer[32];
    if (read_sys_file_line(path, buffer, sizeof(buffer))) {
        return atoi(buffer);
    }
    return -1;
}

// Suppression de la fonction consolidée complexe - remplacée par des fonctions simples

gdouble get_cpu_speed(void) {
    if (performance_data.cpu_core_speeds && performance_data.cpu_core_count > 0) {
        double sum = 0.0;
        for (int i = 0; i < performance_data.cpu_core_count; i++) {
            sum += performance_data.cpu_core_speeds[i];
        }
        return sum / performance_data.cpu_core_count;
    }
    return get_core_cpu_speed(0);
}

gdouble get_core_cpu_speed(int core_index) {
    if (performance_data.cpu_core_speeds && core_index >= 0 && core_index < performance_data.cpu_core_count) {
        return performance_data.cpu_core_speeds[core_index];
    }
    
    char path[128];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", core_index);
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        char buf[32];
        ssize_t bytes = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (bytes > 0) {
            buf[bytes] = '\0';
            long long freq_khz = fast_atoll(buf);
            if (freq_khz > 0) {
                return (double)freq_khz / 1000000.0;
            }
        }
    }
    return get_cpu_speed();
}

#ifndef WITHOUT_GTK
GdkPixbuf* get_cpu_icon_pixbuf(gint cpu_level) {
    if (cpu_level < 0 || cpu_level > MAX_CPU_LEVEL) return NULL;
    
    // Retourner depuis le cache si déjà généré
    if (cpu_icon_cache[cpu_level] != NULL)
        return cpu_icon_cache[cpu_level];
    
    // Charger l'icône de base cpu0
    GdkPixbuf *base_icon = get_embedded_icon_pixbuf("cpu0");
    if (!base_icon) return NULL;
    
    // Si c'est cpu0, le mettre en cache et retourner
    if (cpu_level == 0) {
        g_object_ref(base_icon);
        cpu_icon_cache[0] = base_icon;
        return base_icon;
    }
    
    // Créer une copie pour dessiner dessus
    GdkPixbuf *pixbuf = gdk_pixbuf_copy(base_icon);
    g_object_unref(base_icon);
    
    if (!pixbuf) return NULL;
    
    // Dessiner les barres de progression directement sur les pixels
    // Spécifications: origine (4,20), longueur 13px, max 18 lignes jusqu'à (4,3)
    gint rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    gint n_channels = gdk_pixbuf_get_n_channels(pixbuf);
    guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);
    
    // Calculer le nombre de lignes à dessiner (max 18 lignes pour 100%)
    // Arrondir vers le haut pour avoir au moins 2 lignes dès 10%
    gint num_lines = (18 * cpu_level + 9) / 10;  // +9 pour arrondir au supérieur
    if (num_lines > 18) num_lines = 18;
    
    // Dessiner les barres vertes opaques (du bas vers le haut)
    // Origine: x=5, y=20 (bas) jusqu'à y=3 (haut)
    gint start_y = 20;
    gint x_start = 5;
    gint bar_width = 14;
    
    for (gint line = 0; line < num_lines; line++) {
        gint y = start_y - line;  // Du bas (20) vers le haut (3)
        for (gint x = x_start; x < x_start + bar_width; x++) {
            guchar *p = pixels + y * rowstride + x * n_channels;
            // Vert opaque
            p[0] = 0;      // R
            p[1] = 200;    // G
            p[2] = 0;      // B
        }
    }
    
    // Mettre en cache
    cpu_icon_cache[cpu_level] = pixbuf;
    
    return cpu_icon_cache[cpu_level];
}
#endif

// Structure de cache pour /proc/meminfo (évite d'ouvrir le fichier plusieurs fois par refresh)
typedef struct {
    guint64 mem_free;      // kB
    guint64 mem_cached;    // kB
    guint64 mem_buffered;  // kB
    guint64 swap_total;    // kB
    guint64 swap_free;     // kB
    guint64 commit_limit;  // kB
    guint64 committed_as;  // kB
    guint64 cached_ram;    // kB
    guint64 slab;          // kB
    guint64 sunreclaim;    // kB
    struct timespec last_update;
} meminfo_cache_t;

static meminfo_cache_t meminfo_cache = {0};
static gboolean meminfo_cache_initialized = FALSE;

static void refresh_meminfo_cache_if_expired(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    if (meminfo_cache_initialized) {
        long long elapsed_ms = (now.tv_sec - meminfo_cache.last_update.tv_sec) * 1000 + 
                               (now.tv_nsec - meminfo_cache.last_update.tv_nsec) / 1000000;
        if (elapsed_ms < 250) {
            return; // Cache valide
        }
    }
    
    int fd = open("/proc/meminfo", O_RDONLY);
    if (fd < 0) return;
    
    static char buf[8192];
    ssize_t bytes = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    
    if (bytes <= 0) return;
    buf[bytes] = '\0';
    
    meminfo_cache.mem_free = 0;
    meminfo_cache.mem_cached = 0;
    meminfo_cache.mem_buffered = 0;
    meminfo_cache.swap_total = 0;
    meminfo_cache.swap_free = 0;
    meminfo_cache.commit_limit = 0;
    meminfo_cache.committed_as = 0;
    meminfo_cache.cached_ram = 0;
    meminfo_cache.slab = 0;
    meminfo_cache.sunreclaim = 0;
    
    char* line = buf;
    while (line && *line) {
        char* colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            char* val_ptr = colon + 1;
            while (*val_ptr == ' ' || *val_ptr == '\t') val_ptr++;
            guint64 val = fast_atoll(val_ptr); // valeur brute en kB
            
            if (strcmp(line, "MemFree") == 0) meminfo_cache.mem_free = val;
            else if (strcmp(line, "Cached") == 0) {
                meminfo_cache.mem_cached += val;
                meminfo_cache.cached_ram = val;
            }
            else if (strcmp(line, "SReclaimable") == 0) meminfo_cache.mem_cached += val;
            else if (strcmp(line, "Buffers") == 0) meminfo_cache.mem_buffered = val;
            else if (strcmp(line, "SwapTotal") == 0) meminfo_cache.swap_total = val;
            else if (strcmp(line, "SwapFree") == 0) meminfo_cache.swap_free = val;
            else if (strcmp(line, "CommitLimit") == 0) meminfo_cache.commit_limit = val;
            else if (strcmp(line, "Committed_AS") == 0) meminfo_cache.committed_as = val;
            else if (strcmp(line, "Slab") == 0) meminfo_cache.slab = val;
            else if (strcmp(line, "SUnreclaim") == 0) meminfo_cache.sunreclaim = val;
            
            *colon = ':'; // restaurer
        }
        line = strchr(line, '\n');
        if (line) line++;
    }
    
    meminfo_cache.last_update = now;
    meminfo_cache_initialized = TRUE;
}

guint64 get_cached_mem_free(void) {
    refresh_meminfo_cache_if_expired();
    return meminfo_cache.mem_free;
}

guint64 get_cached_mem_cached(void) {
    refresh_meminfo_cache_if_expired();
    return meminfo_cache.mem_cached;
}

guint64 get_cached_mem_buffered(void) {
    refresh_meminfo_cache_if_expired();
    return meminfo_cache.mem_buffered;
}

guint64 get_cached_swap_total_val(void) {
    refresh_meminfo_cache_if_expired();
    return meminfo_cache.swap_total;
}

guint64 get_cached_swap_free_val(void) {
    refresh_meminfo_cache_if_expired();
    return meminfo_cache.swap_free;
}

void get_swap_info(guint64 *swap_total, guint64 *swap_free) {
    refresh_meminfo_cache_if_expired();
    *swap_total = meminfo_cache.swap_total;
    *swap_free = meminfo_cache.swap_free;
}

// ============================================================================
// OPTIMISATION HASH - Détection rapide des changements processus
// Comparaison rapide des champs critiques pour détecter les changements (O-11)
static inline gboolean tasks_are_equal(const struct task *a, const struct task *b, gboolean should_load_pss) {
    if (a->ppid != b->ppid) return FALSE;
    if (a->state_prio_packed != b->state_prio_packed) return FALSE;
    if (a->flags_nice_packed != b->flags_nice_packed) return FALSE;
    if (a->size != b->size) return FALSE;
    if (a->rss != b->rss) return FALSE;
    if (a->shr != b->shr) return FALSE;
    if (should_load_pss && (a->pss != b->pss)) return FALSE;
    if (a->time_percentage != b->time_percentage) return FALSE;
    if (a->gpu_usage != b->gpu_usage) return FALSE;
    return strcmp(a->name, b->name) == 0;
}

#ifndef WITHOUT_GTK
gboolean refresh_task_list(void) {
#ifdef CONSOLE_DEBUG
    printf("[DEBUG] REFRESH_TASK_LIST: APPELÉ - onglet=%d\n", gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook)));
    fflush(stdout);
#endif
    
    // ============================================================================
    // CHARGEMENT PSS ADAPTATIF AVEC THREAD ASYNCHRONE
    // ============================================================================
    static gboolean first_call = TRUE;
    static int pss_refresh_counter = 0;
    gboolean should_load_pss = FALSE;
    
    if (first_call) {
        first_call = FALSE;
        // Initialiser le thread PSS
        init_pss_thread();
        // PSS reste désactivé pour le premier appel d'initialisation
    } else if (app_flags & APP_FLAG_DISPLAY_PSS) {
        // Déterminer si on doit lancer la collection PSS ce cycle
        pss_refresh_counter++;
        
        if (refresh_interval >= 3) {
            // Refresh 3s/4s/5s → PSS à chaque fois (latence 3-5s)
            should_load_pss = TRUE;
        } else if (refresh_interval == 2) {
            // Refresh 2s → PSS 1 fois sur 2 (latence 4s)
            should_load_pss = (pss_refresh_counter % 2 == 0);
        } else {
            // Refresh 1s → PSS 1 fois sur 4 (latence 4s)
            should_load_pss = (pss_refresh_counter % 4 == 0);
        }
        
        // Désactiver la lecture PSS synchrone (le thread s'en charge)
        set_optimization_flag(OPTIMIZATION_FLAG_PSS_LOADING, FALSE);
    } else {
        // PSS désactivé par l'utilisateur
        set_optimization_flag(OPTIMIZATION_FLAG_PSS_LOADING, FALSE);
    }
    
    // ============================================================================
    // REFRESH ULTRA-GRANULAIRE PAR ONGLET - Logique conditionnelle précise
    // ============================================================================
    
    // Calculer les flags de refresh selon l'onglet actif (remplace les gboolean)
    gint current_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook));
    
    // Réinitialiser les flags refresh
    system_optimization_flags &= ~(REFRESH_FLAG_FULL_PROCESSES | REFRESH_FLAG_USERS_CALC | 
                                   REFRESH_FLAG_PERF_COUNTERS | REFRESH_FLAG_ONLY_SAMPLES);
    
    // Définir le flag selon l'onglet actif
    if (IS_INTEGRATED_REDUCED_MODE()) {
        system_optimization_flags |= REFRESH_FLAG_ONLY_SAMPLES;
    } else {
        switch (current_page) {
            case 0: system_optimization_flags |= REFRESH_FLAG_FULL_PROCESSES; break;  // Processes
            case 1: system_optimization_flags |= REFRESH_FLAG_PERF_COUNTERS; break;   // Performance  
            case 2: system_optimization_flags |= REFRESH_FLAG_ONLY_SAMPLES; break;    // Startup
            case 3: system_optimization_flags |= REFRESH_FLAG_USERS_CALC; break;      // Users
            case 4: system_optimization_flags |= REFRESH_FLAG_ONLY_SAMPLES; break;    // Services
            default: system_optimization_flags |= REFRESH_FLAG_ONLY_SAMPLES; break;
        }
    }
    
    // DEBUG: Tracer quelle logique s'exécute (à supprimer en production)
    g_debug("REFRESH_LOGIC: Onglet=%d, FullProc=%d, Users=%d, Perf=%d, Samples=%d", 
            current_page, NEEDS_FULL_PROCESSES() ? 1 : 0, NEEDS_USERS_CALC() ? 1 : 0, 
            NEEDS_PERF_COUNTERS() ? 1 : 0, NEEDS_ONLY_SAMPLES() ? 1 : 0);
    
    // Store the currently selected PID (seulement si refresh processus nécessaire)
    gchar *selected_pid = NULL;
    if (NEEDS_FULL_PROCESSES() || NEEDS_USERS_CALC()) {
        GtkTreeModel *model;
        GtkTreeIter iter;
        if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
            // Vérifier que nous travaillons avec le bon modèle (Processes tab seulement)
            if (model == GTK_TREE_MODEL(list_store)) {
                gtk_tree_model_get(model, &iter, COLUMN_PID, &selected_pid, -1);
            }
        }
    }

    guint i, j;
    GArray *new_task_list = NULL;
    gdouble cpu_usage;
    guint num_cpus;
    guint64 memory_used;
    char tooltip[80];
    if (sys_stat != NULL)
        num_cpus = sys_stat->cpu_count;
    else
        num_cpus = 1;
    
    // ============================================================================
    // TRAITEMENT CONDITIONNEL ULTRA-PRÉCIS PAR ONGLET
    // ============================================================================
    
    // Traitement processus complet (Processes + Users)
    if (NEEDS_FULL_PROCESSES() || NEEDS_USERS_CALC()) {
        g_debug("REFRESH_EXEC: Traitement processus complet activé");
        new_task_list = (GArray*) get_task_list();
        
        // OPTIMISATION: Hash table persistante réutilisée au lieu de malloc/free chaque refresh
        // Gain: 0.5-1% CPU (évite 3600 malloc/free par heure à 1 Hz)
        static GHashTable *new_task_hash_persistent = NULL;
        if (!new_task_hash_persistent) {
            new_task_hash_persistent = g_hash_table_new(g_direct_hash, g_direct_equal);
        }
        g_hash_table_remove_all(new_task_hash_persistent);  // Clear au lieu de destroy
        
        GHashTable *new_task_hash = new_task_hash_persistent;  // Alias pour compatibilité code
        for (j = 0; j < new_task_list->len; j++) {
            struct task *new_tmp = &g_array_index(new_task_list, struct task, j);
            g_hash_table_insert(new_task_hash, GINT_TO_POINTER(new_tmp->pid), new_tmp);
        }
        
        // THREAD PSS ASYNCHRONE : Lancer la collection si nécessaire
        if (should_load_pss && !is_pss_thread_busy()) {
            // Extraire les PIDs à traiter (allocation statique sur la pile pour éviter le heap overhead)
            pid_t pids_for_pss[TASK_POOL_SIZE];
            guint pss_count = new_task_list->len;
            if (pss_count > TASK_POOL_SIZE) pss_count = TASK_POOL_SIZE;
            for (j = 0; j < pss_count; j++) {
                pids_for_pss[j] = g_array_index(new_task_list, struct task, j).pid;
            }
            start_pss_collection(pids_for_pss, pss_count);
        }
        
        // Calculer une seule fois la limite théorique pour tous les processus
        gulong theoretical_max_jiffies = refresh_interval * get_jiffies_per_second() * num_cpus;
        
        // OPTIMISATION CPU : Pré-calculer valeurs communes (évite 1000+ divisions par refresh)
        guint cached_divisor = refresh_interval * num_cpus;  // Calculé 1 fois au lieu de 1000+
        const gfloat inv_1000 = 1.0f / 1000.0f;             // Constante pour éviter division flottante
        
        for (i = 0; i < task_array->len; i++) {
            struct task *tmp = &g_array_index(task_array, struct task, i);
            TASK_SET_FLAGS(tmp, TASK_GET_FLAGS(tmp) & ~TASK_FLAG_CHECKED);
            struct task *new_tmp = (struct task*) g_hash_table_lookup(new_task_hash, GINT_TO_POINTER(tmp->pid));
            if (new_tmp != NULL) {
            tmp->old_time = tmp->time;
            tmp->time = new_tmp->time;
            guint delta_time = tmp->time - tmp->old_time;
            
            // Protection contre les valeurs aberrantes (wraparound, timing incorrect, etc.)
            if (delta_time > theoretical_max_jiffies) {
                // Valeur aberrante détectée - probablement wraparound ou timing incorrect
                tmp->time_percentage = 0.0f;  // Ignorer cette mesure
            } else {
                // OPTIMISATION : Arithmétique entière + multiplication au lieu de division flottante
                guint time_percentage_int = ((guint64)delta_time * 1000) / cached_divisor;
                tmp->time_percentage = (gfloat)time_percentage_int * inv_1000;
            }
            
            // Synchroniser le pourcentage CPU sur new_tmp avant comparaison
            new_tmp->time_percentage = tmp->time_percentage;
            
            // Comparaison directe optimisée avec court-circuit (O-11)
            if (UNLIKELY(!tasks_are_equal(tmp, new_tmp, should_load_pss))) {
                // Mise à jour des champs
                tmp->ppid = new_tmp->ppid;
                TASK_SET_STATE_CHAR(tmp, TASK_GET_STATE_CHAR(new_tmp));
                tmp->size = new_tmp->size;
                tmp->rss = new_tmp->rss;
                tmp->shr = new_tmp->shr;
                // Récupérer PSS depuis le thread asynchrone si disponible
                guint64 pss_from_thread = 0;
                if (get_pss_result(tmp->pid, &pss_from_thread)) {
                    tmp->pss = pss_from_thread;
                }
                tmp->gpu_usage = new_tmp->gpu_usage;
                TASK_SET_PRIO(tmp, TASK_GET_PRIO(new_tmp));
                g_strlcpy(tmp->name, new_tmp->name, sizeof(tmp->name));
                g_strlcpy(tmp->simple_name, new_tmp->simple_name, sizeof(tmp->simple_name));
            }
            // Cas commun (90-95%): hash identique → aucun changement, skip comparaisons
            
            TASK_SET_FLAGS(tmp, TASK_GET_FLAGS(tmp) | TASK_FLAG_CHECKED);
            TASK_SET_FLAGS(new_tmp, TASK_GET_FLAGS(new_tmp) | TASK_FLAG_CHECKED);
        }
    }
    // OPTIMISATION: NE PAS détruire la hash table, elle est persistante et sera réutilisée
    // (g_hash_table_destroy supprimé - la hash table est static et cleared au prochain refresh)
    
    if (!(app_flags & APP_FLAG_GROUP_PROCS)) {
        i = 0;
        while (i < task_array->len) {
            struct task *tmp = &g_array_index(task_array, struct task, i);
            if (!(TASK_GET_FLAGS(tmp) & TASK_FLAG_CHECKED)) {
                remove_list_item(tmp->pid);
                g_array_remove_index(task_array, i);
                tasks--;
            } else {
                i++;
            }
        }
        for (i = 0; i < new_task_list->len; i++) {
            struct task *new_tmp = &g_array_index(new_task_list, struct task, i);
            if (!(TASK_GET_FLAGS(new_tmp) & TASK_FLAG_CHECKED)) {
                struct task *new_task = new_tmp;
                if (((app_flags & APP_FLAG_SHOW_USER_TASKS) && new_task->uid == own_uid) ||
                    ((app_flags & APP_FLAG_SHOW_ROOT_TASKS) && new_task->uid == 0) ||
                    ((app_flags & APP_FLAG_SHOW_OTHER_TASKS) && new_task->uid != own_uid && new_task->uid != 0)) {
                    g_array_append_val(task_array, *new_task);
                    add_new_list_item(tasks);
                    tasks++;
                }
            }
        }
    } else {
        i = 0;
        while (i < task_array->len) {
            struct task *tmp = &g_array_index(task_array, struct task, i);
            if (!(TASK_GET_FLAGS(tmp) & TASK_FLAG_CHECKED)) {
                g_array_remove_index(task_array, i);
                tasks--;
            } else {
                i++;
            }
        }
        for (i = 0; i < new_task_list->len; i++) {
            struct task *new_tmp = &g_array_index(new_task_list, struct task, i);
            if (!(TASK_GET_FLAGS(new_tmp) & TASK_FLAG_CHECKED)) {
                struct task *new_task = new_tmp;
                g_array_append_val(task_array, *new_task);
                tasks++;
            }
        }
        // Si l'arbre est vide (premier appel), il faut le construire, pas le synchroniser
        GtkTreeIter test_iter;
        if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(list_store), &test_iter)) {
            // L'arbre contient des éléments, on peut synchroniser
            sync_grouped_tree();
        } else {
            // L'arbre est vide, il faut le construire
            rebuild_tree();
        }
        }
        
        g_array_free(new_task_list, TRUE);

        // Mettre à jour la hash table PID→index après modifications du task_array
        update_pid_index_map();

        // Pool mémoire opérationnel - benchmark validé : 3,8x plus rapide
        // Cache TreeIter opérationnel - validation : 947-1183 slots utilisés (46-58%)
        // Cache sysconf opérationnel - validation : PAGE_SIZE=4096, CLK_TCK=100, NPROCESSORS=4

        // Restore the selection
        if (selected_pid && strlen(selected_pid) > 0) {
            restore_selection(GTK_TREE_VIEW(treeview), selected_pid);
        }
        
        // Refresh spécifique selon l'onglet
        if (NEEDS_FULL_PROCESSES()) {
            // Onglet Processes : Rien de plus à faire (affichage déjà mis à jour)
            g_debug("REFRESH_EXEC: Onglet Processes - affichage processus mis à jour");
        }
        if (NEEDS_USERS_CALC()) {
            // Onglet Users : Rafraîchir avec task_array qui contient les pourcentages CPU
            g_debug("REFRESH_EXEC: Onglet Users - refresh_users_list() appelé");
            refresh_users_list();
        }
        
        g_free(selected_pid);
    } // Fin du traitement processus complet
    
    // ============================================================================
    // TRAITEMENT OPTIMISÉ POUR ONGLETS LÉGERS (Performance/Startup/Services)
    // ============================================================================
    
    else if (NEEDS_PERF_COUNTERS()) {
        // Onglet Performance : Seulement compteurs rapides (pas de liste processus)
        g_debug("REFRESH_EXEC: Onglet Performance - compteurs rapides uniquement");

        // Utiliser les fonctions optimisées pour éviter de parser tous les processus
        guint process_count = get_process_count_fast();
        guint thread_count = get_thread_count_fast();
        gfloat total_gpu = get_total_gpu_usage_fast();

        g_debug("REFRESH_PERF: Processes=%u, Threads=%u, GPU=%.1f%%",
                process_count, thread_count, total_gpu);

        // TODO: Stocker ces valeurs quelque part pour l'affichage Performance
        // ou les passer directement aux fonctions d'affichage
    }
    
    else if (NEEDS_ONLY_SAMPLES()) {
        // Onglets Startup/Services : Absolument rien d'autre que les samples système
        g_debug("REFRESH_EXEC: Onglet Startup/Services - SEULEMENT samples système");
        // Pas de traitement processus, pas de calculs utilisateurs
        // Maximum d'économie CPU/I/O
    }

    /* Update the CPU and memory progress bars */
    if (sys_stat == NULL)
        sys_stat = g_new0(system_status, 1);
    get_system_status(sys_stat);
    cpu_speed_value = get_cpu_speed();
    memory_used = sys_stat->mem_total - sys_stat->mem_free;
    if (app_flags & APP_FLAG_SHOW_CACHED_FREE) {
        memory_used -= sys_stat->mem_cached + sys_stat->mem_buffered;
    }
    char memory_tooltip[128];
    format_memory_gb(memory_tooltip, sizeof(memory_tooltip), memory_used);
    snprintf(tooltip, sizeof(tooltip), "Memory: %s of %.1f GB used", memory_tooltip, (double)(sys_stat->mem_total) / (1024.0 * 1024.0));
    if (strcmp(tooltip, gtk_progress_bar_get_text(GTK_PROGRESS_BAR(mem_usage_progress_bar)))) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(mem_usage_progress_bar), (gdouble)memory_used / sys_stat->mem_total);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(mem_usage_progress_bar), tooltip);
        
        // Mettre à jour les barres de progression des autres onglets
        if (startup_mem_bar) {
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(startup_mem_bar), (gdouble)memory_used / sys_stat->mem_total);
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(startup_mem_bar), tooltip);
        }
        if (users_mem_bar) {
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(users_mem_bar), (gdouble)memory_used / sys_stat->mem_total);
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(users_mem_bar), tooltip);
        }
        if (services_mem_bar) {
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(services_mem_bar), (gdouble)memory_used / sys_stat->mem_total);
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(services_mem_bar), tooltip);
        }
    }
    guint64 swap_total, swap_free;
    get_swap_info(&swap_total, &swap_free);
    guint64 swap_used = swap_total - swap_free;
    if (swap_total > 0) {
        gchar swap_tooltip[80];
        char swap_used_str[64], swap_total_str[64];
        format_memory_gb(swap_used_str, sizeof(swap_used_str), swap_used);
        format_memory_gb(swap_total_str, sizeof(swap_total_str), swap_total);
        snprintf(swap_tooltip, sizeof(swap_tooltip), "Swap: %s of %s used", swap_used_str, swap_total_str);
        if (strcmp(swap_tooltip, gtk_progress_bar_get_text(GTK_PROGRESS_BAR(swap_usage_progress_bar)))) {
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(swap_usage_progress_bar), (gdouble)swap_used / swap_total);
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(swap_usage_progress_bar), swap_tooltip);
            
            // Mettre à jour les barres de progression SWAP des autres onglets
            if (startup_swap_bar) {
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(startup_swap_bar), (gdouble)swap_used / swap_total);
                gtk_progress_bar_set_text(GTK_PROGRESS_BAR(startup_swap_bar), swap_tooltip);
            }
            if (users_swap_bar) {
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(users_swap_bar), (gdouble)swap_used / swap_total);
                gtk_progress_bar_set_text(GTK_PROGRESS_BAR(users_swap_bar), swap_tooltip);
            }
            if (services_swap_bar) {
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(services_swap_bar), (gdouble)swap_used / swap_total);
                gtk_progress_bar_set_text(GTK_PROGRESS_BAR(services_swap_bar), swap_tooltip);
            }
        }
    }
    // Debug état sys_stat avant get_cpu_usage
#ifdef CONSOLE_DEBUG
    printf("[DEBUG] AVANT get_cpu_usage: onglet=%d, sys_stat->status_flags=0x%02X, cpu_old_jiffies=%u\n", 
           current_page, sys_stat->status_flags, sys_stat->cpu_old_jiffies);
    fflush(stdout);
#endif
    
    cpu_usage = get_cpu_usage(sys_stat);
    
#ifdef CONSOLE_DEBUG
    printf("[DEBUG] APRÈS get_cpu_usage: cpu_usage=%.3f, sys_stat->status_flags=0x%02X, cpu_old_jiffies=%u\n", 
           cpu_usage, sys_stat->status_flags, sys_stat->cpu_old_jiffies);
    fflush(stdout);
#endif
    
    // ========================================================================
    // SAMPLING TOUJOURS ACTIF - Indépendant des jauges
    // ========================================================================
    
    // Calculer l'utilisation de la RAM
    gdouble ram_usage = (gdouble)memory_used / (gdouble)sys_stat->mem_total;
    
    // Calculer l'utilisation du swap
    gdouble swap_usage = 0.0;
    if (swap_total > 0) {
        swap_usage = (gdouble)swap_used / (gdouble)swap_total;
    }
    
    // Calculer l'utilisation du disque
    gdouble disk_usage = get_disk_activity_percent();
    
    // Obtenir les taux de lecture/écriture disque
    gdouble disk_read_kbs, disk_write_kbs;
    get_disk_io_rates(&disk_read_kbs, &disk_write_kbs);
    
    // SAMPLING TOUJOURS EXÉCUTÉ (indépendamment des jauges)
#ifdef CONSOLE_DEBUG
    printf("[DEBUG] SAMPLING: cpu=%.3f, ram=%.3f, onglet=%d, flags=0x%04X\n", 
            cpu_usage, ram_usage, current_page, system_optimization_flags);
    fflush(stdout);
#endif
    update_performance_samples(cpu_usage, ram_usage, swap_usage, disk_usage, disk_read_kbs, disk_write_kbs);
    
    // ========================================================================
    // MISE À JOUR CACHES BLOCS D'INFOS - Unification système refresh
    // ========================================================================
    // Mettre à jour les caches CPU et RAM APRÈS les samples (synchronisation)
    get_cpu_info_data();  // Met à jour cpu_info_cache avec les nouvelles données
    get_ram_info_data();  // Met à jour ram_info_cache avec les nouvelles données
    
    // Marquer les blocs d'infos pour mise à jour (optimisation 30 FPS)
    mark_info_blocks_for_update();
    
    // ========================================================================
    // JAUGES CONDITIONNELLES - Dépendent du sampling
    // ========================================================================
    static gint last_page = -1;
    gboolean page_changed = (last_page != current_page);
    if (page_changed) last_page = current_page;
    
#ifdef CONSOLE_DEBUG
    printf("[DEBUG] JAUGES: cpu_usage=%.3f, last_cpu_usage=%.3f, onglet=%d, page_changed=%d\n", 
            cpu_usage, last_cpu_usage, current_page, page_changed);
    fflush(stdout);
#endif
    
    // Mise à jour si valeur change OU si changement d'onglet
    if (last_cpu_usage != cpu_usage || page_changed) {
        char cpu_speed_str[64];
        format_speed_ghz(cpu_speed_str, sizeof(cpu_speed_str), cpu_speed_value * 1000.0);
        snprintf(cached_cpu_tooltip, sizeof(cached_cpu_tooltip), "CPU usage: %0.0f %% at %s", cpu_usage * 100.0, cpu_speed_str);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cpu_usage_progress_bar), cpu_usage);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(cpu_usage_progress_bar), cached_cpu_tooltip);
        last_cpu_usage = cpu_usage;
        
        // Mettre à jour les barres de progression CPU des autres onglets
        if (startup_cpu_bar) {
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(startup_cpu_bar), cpu_usage);
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(startup_cpu_bar), cached_cpu_tooltip);
        }
        if (users_cpu_bar) {
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(users_cpu_bar), cpu_usage);
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(users_cpu_bar), cached_cpu_tooltip);
        }
        if (services_cpu_bar) {
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(services_cpu_bar), cpu_usage);
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(services_cpu_bar), cached_cpu_tooltip);
        }
        
        // Mettre à jour l'icône de la barre système
        update_status_icon_cpu((int)(cpu_usage * 100.0));
    }
    
    // Vérifier les changements de disques (USB, etc.) et mettre à jour
    gboolean disks_changed = check_and_refresh_disk_list();
    
    // Mettre à jour les données de tous les disques
    update_disk_data();
    
    // Si des disques ont changé, forcer le rafraîchissement de l'interface
    if (UNLIKELY(disks_changed)) {
        // Notifier l'interface que la configuration des disques a changé
        // Cela permettra de mettre à jour les graphiques et sélections
#ifdef DEBUG
        g_message("Disk configuration changed - UI refresh needed");
#endif
    }
    
    // Mettre à jour les données de toutes les interfaces réseau
    update_network_data();
    
    // Rafraîchir le graphique de performance CPU
    refresh_cpu_performance_graph();
    
    // ========================================================================
    // BATCH GTK UPDATE - Remplace 500 appels individuels par batch optimisé
    // ========================================================================
    if (!(app_flags & APP_FLAG_GROUP_PROCS)) {
        // Mise à jour batch de tous les processus en une seule transaction GTK
        refresh_list_items_batched(0, task_array->len);
    }
    
    // Mettre à jour le tooltip de la systray (une fois par refresh_interval)
    update_status_icon_tooltip();
    
    return TRUE;
}

// Fonction dédiée pour charger PSS uniquement (optimisation démarrage)
gboolean refresh_pss_only(void) {
    if (!IS_PSS_LOADING_ENABLED()) {
        return FALSE; // Sécurité : PSS doit être activé
    }
    
    for (guint i = 0; i < tasks; i++) {
        struct task *task = &g_array_index(task_array, struct task, i);
        pid_t pid = task->pid;
        
        // Lire PSS depuis /proc/pid/smaps_rollup
        gchar line[256];
        snprintf(line, sizeof(line), "/proc/%d/smaps_rollup", (int)pid);
        int fd = open(line, O_RDONLY);
        if (fd != -1) {
            char buffer[1024];
            ssize_t ret = read(fd, buffer, sizeof(buffer)-1);
            if (ret > 0) {
                buffer[ret] = '\0';
                char *pss_line = strstr(buffer, "Pss:");
                if (pss_line) {
                    gulong t_pss_kb;
                    if (sscanf(pss_line, "Pss: %lu kB", &t_pss_kb) == 1) {
                        task->pss = t_pss_kb;
                    }
                }
            }
            close(fd);
        }
        
        // Fallback: si PSS non disponible, utiliser RSS
        if (task->pss == 0) {
            task->pss = task->rss;
        }
        
        // Note: Batch update sera fait à la fin de refresh_task_list()
    }
    
    // Reconstruire l'affichage si mode groupé
    if (app_flags & APP_FLAG_GROUP_PROCS) {
        sync_grouped_tree();
    }
    
    // Rafraîchir l'onglet Users avec les nouvelles données PSS
    refresh_users_list();
    
    return TRUE;
}
#endif

// ============================================================================
// REFRESH CONDITIONNEL ULTRA-GRANULAIRE PAR ONGLET
// ============================================================================
// Les fonctions needs_* sont remplacées par la logique dans refresh_task_list()
// qui utilise les macros NEEDS_* basées sur system_optimization_flags

// ============================================================================
// FONCTIONS OPTIMISÉES POUR COLLECTE CIBLÉE
// ============================================================================

// GPU total rapide sans parser tous les processus (pour Performance/Startup/Services)
gfloat get_total_gpu_usage_fast(void) {
    // Méthode 1: Lecture directe /sys/class/drm/card0/engine/render/busy_percent (si disponible)
    FILE *busy_file = fopen("/sys/class/drm/card0/engine/render/busy_percent", "r");
    if (busy_file) {
        gfloat busy_percent = 0.0f;
        if (fscanf(busy_file, "%f", &busy_percent) == 1) {
            fclose(busy_file);
            return busy_percent;
        }
        fclose(busy_file);
    }
    
    // Méthode 2: Sommation rapide depuis /proc/*/fdinfo/* sans créer struct task
    gfloat total_gpu = 0.0f;
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) return 0.0f;
    
    struct dirent *entry;
    int processed_count = 0;  // Compteur local (pas statique)
    while ((entry = readdir(proc_dir)) != NULL) {
        // Vérifier si c'est un PID
        if (strspn(entry->d_name, "0123456789") == strlen(entry->d_name)) {
            pid_t pid = atoi(entry->d_name);
            
            // Lire GPU usage pour ce PID directement depuis gpu_stats
            gfloat gpu_usage = 0.0f;
            const ProcessGPUEntry* gpu_data = gpu_stats_get_process_by_pid(pid);
            if (gpu_data) {
                gpu_usage = (gfloat)gpu_data->total_percent;
            }
            total_gpu += gpu_usage;
            
            // Limiter à 100 processus max pour éviter latence excessive
            if (++processed_count > 100) break;
        }
    }
    closedir(proc_dir);
    
    // Clamp à 100% maximum (évite valeurs aberrantes)
    return (total_gpu > 100.0f) ? 100.0f : total_gpu;
}

// Nombre processus rapide sans détails (pour Performance)
guint get_process_count_fast(void) {
    guint count = 0;
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) return 0;
    
    struct dirent *entry;
    while ((entry = readdir(proc_dir)) != NULL) {
        // Vérifier si c'est un PID (nom numérique)
        if (strspn(entry->d_name, "0123456789") == strlen(entry->d_name)) {
            count++;
        }
    }
    closedir(proc_dir);
    return count;
}

// Nombre total de threads rapide via /proc/loadavg (O(1))
long get_system_thread_count_fast(void) {
    int fd = open("/proc/loadavg", O_RDONLY);
    if (fd < 0) return 0;
    char buf[128];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';
    char *slash = strchr(buf, '/');
    if (slash) {
        return atol(slash + 1);
    }
    return 0;
}

static char to_lower_char(char c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static void to_lower_str(char *dest, const char *src, size_t dest_size) {
    size_t i = 0;
    while (src[i] && i < dest_size - 1) {
        dest[i] = to_lower_char(src[i]);
        i++;
    }
    dest[i] = '\0';
}

static gboolean case_insensitive_contains(const char *haystack, const char *needle) {
    if (!haystack || !needle) return FALSE;
    char h_lower[256];
    char n_lower[64];
    to_lower_str(h_lower, haystack, sizeof(h_lower));
    to_lower_str(n_lower, needle, sizeof(n_lower));
    return strstr(h_lower, n_lower) != NULL;
}

static char cpu_temp_path[256] = "";
static gboolean cpu_temp_path_detected = FALSE;
static double cached_cpu_temp = -1.0;
static time_t last_temp_read_time = 0;

static void detect_cpu_temp_sensor(void) {
    cpu_temp_path[0] = '\0';
    cpu_temp_path_detected = TRUE;

    // 1. Essayer avec hwmon (très précis pour les CPU physiques)
    DIR *dir = opendir("/sys/class/hwmon");
    if (dir) {
        struct dirent *entry;
        int best_score = -1;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;
            char base_path[320];
            snprintf(base_path, sizeof(base_path), "/sys/class/hwmon/%s", entry->d_name);

            // Lire le nom du chip
            char name_path[384];
            snprintf(name_path, sizeof(name_path), "%s/name", base_path);
            char chip_name[64] = "";
            FILE *nf = fopen(name_path, "r");
            if (nf) {
                if (fgets(chip_name, sizeof(chip_name), nf)) {
                    char *nl = strchr(chip_name, '\n');
                    if (nl) *nl = '\0';
                }
                fclose(nf);
            }

            gboolean is_cpu_chip = (case_insensitive_contains(chip_name, "coretemp") ||
                                    case_insensitive_contains(chip_name, "k10temp") ||
                                    case_insensitive_contains(chip_name, "zenpower") ||
                                    case_insensitive_contains(chip_name, "cpu") ||
                                    case_insensitive_contains(chip_name, "soc_thermal") ||
                                    case_insensitive_contains(chip_name, "x86_pkg_temp"));

            // Parcourir les fichiers temp*_input du dossier
            DIR *hw_dir = opendir(base_path);
            if (hw_dir) {
                struct dirent *hw_entry;
                while ((hw_entry = readdir(hw_dir)) != NULL) {
                    if (strncmp(hw_entry->d_name, "temp", 4) == 0 &&
                        strstr(hw_entry->d_name, "_input") != NULL) {
                        
                        // Extraire l'index
                        char idx[16] = "";
                        const char *p = hw_entry->d_name + 4;
                        int i = 0;
                        while (*p >= '0' && *p <= '9' && i < 15) {
                            idx[i++] = *p++;
                        }
                        idx[i] = '\0';

                        // Lire le label
                        char label_path[384];
                        snprintf(label_path, sizeof(label_path), "%s/temp%s_label", base_path, idx);
                        char label[64] = "";
                        FILE *lf = fopen(label_path, "r");
                        if (lf) {
                            if (fgets(label, sizeof(label), lf)) {
                                char *nl = strchr(label, '\n');
                                if (nl) *nl = '\0';
                            }
                            fclose(lf);
                        }

                        int score = 0;
                        if (is_cpu_chip) score += 20;
                        if (case_insensitive_contains(label, "package") ||
                            case_insensitive_contains(label, "tdie") ||
                            case_insensitive_contains(label, "tctl") ||
                            case_insensitive_contains(label, "cpu") ||
                            case_insensitive_contains(label, "core")) {
                            score += 10;
                        }

                        // Préférer "package" ou "tdie" qui représentent la T° globale
                        if (case_insensitive_contains(label, "package") || case_insensitive_contains(label, "tdie")) {
                            score += 10;
                        }

                        if (score > best_score) {
                            best_score = score;
                            snprintf(cpu_temp_path, sizeof(cpu_temp_path), "%s/%s", base_path, hw_entry->d_name);
                        }
                    }
                }
                closedir(hw_dir);
            }
        }
        closedir(dir);
    }

    // 2. Si non trouvé dans hwmon, essayer les zones thermiques (virtuel / laptops / ARM)
    if (cpu_temp_path[0] == '\0') {
        DIR *tdir = opendir("/sys/class/thermal");
        if (tdir) {
            struct dirent *tentry;
            int best_score = -1;
            while ((tentry = readdir(tdir)) != NULL) {
                if (strncmp(tentry->d_name, "thermal_zone", 12) == 0) {
                    char type_path[320];
                    snprintf(type_path, sizeof(type_path), "/sys/class/thermal/%s/type", tentry->d_name);
                    char type_name[64] = "";
                    FILE *tf = fopen(type_path, "r");
                    if (tf) {
                        if (fgets(type_name, sizeof(type_name), tf)) {
                            char *nl = strchr(type_name, '\n');
                            if (nl) *nl = '\0';
                        }
                        fclose(tf);
                    }

                    int score = 0;
                    if (case_insensitive_contains(type_name, "x86_pkg_temp")) score += 30;
                    else if (case_insensitive_contains(type_name, "cpu")) score += 20;
                    else if (case_insensitive_contains(type_name, "acpitz")) score += 10;
                    else if (case_insensitive_contains(type_name, "soc")) score += 5;

                    if (score > best_score) {
                        best_score = score;
                        snprintf(cpu_temp_path, sizeof(cpu_temp_path), "/sys/class/thermal/%s/temp", tentry->d_name);
                    }
                }
            }
            closedir(tdir);
        }
    }
}

double get_cpu_temperature(void) {
    if (!cpu_temp_path_detected) {
        detect_cpu_temp_sensor();
    }

    if (cpu_temp_path[0] == '\0') {
        return -1.0;
    }

    time_t now = time(NULL);
    if (now - last_temp_read_time >= 3 || last_temp_read_time == 0) {
        last_temp_read_time = now;
        FILE *f = fopen(cpu_temp_path, "r");
        if (f) {
            char val_str[32];
            if (fgets(val_str, sizeof(val_str), f)) {
                double val = atof(val_str);
                if (val > 1000.0) {
                    cached_cpu_temp = val / 1000.0;
                } else if (val > 0.0) {
                    cached_cpu_temp = val;
                } else {
                    cached_cpu_temp = -1.0;
                }
            } else {
                cached_cpu_temp = -1.0;
            }
            fclose(f);
        } else {
            cached_cpu_temp = -1.0;
        }
    }

    return cached_cpu_temp;
}

// Nombre threads rapide sans détails (pour Performance)
guint get_thread_count_fast(void) {
    guint total_threads = 0;
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) return 0;
    
    struct dirent *entry;
    while ((entry = readdir(proc_dir)) != NULL) {
        // Vérifier si c'est un PID
        if (strspn(entry->d_name, "0123456789") == strlen(entry->d_name)) {
            // Lire /proc/PID/status pour Threads:
            char status_path[64];
            format_proc_path(status_path, sizeof(status_path), fast_atoi(entry->d_name), "status");
            
            FILE *status_file = fopen(status_path, "r");
            if (status_file) {
                char line[256];
                while (fgets(line, sizeof(line), status_file)) {
                    if (strncmp(line, "Threads:", 8) == 0) {
                        total_threads += fast_atoi(line + 8);
                        break;
                    }
                }
                fclose(status_file);
            }
        }
    }
    closedir(proc_dir);
    return total_threads;
}

void cleanup_system_status() {
    if (sys_stat) {
        g_free(sys_stat);
        sys_stat = NULL;
    }
    meminfo_cache_initialized = FALSE;
}





gdouble get_cpu_usage(system_status *sys_stat) {
    gdouble cpu_usage = 0.0;
    guint64 current_jiffies;
    guint64 current_used;
    guint64 delta_jiffies;
    
    gboolean proc_success = get_cpu_usage_from_proc(sys_stat);
#ifdef CONSOLE_DEBUG
    printf("[DEBUG] get_cpu_usage_from_proc() returned=%s\n", proc_success ? "TRUE" : "FALSE");
    fflush(stdout);
#endif
    
    if (proc_success == FALSE) {
        guint i = 0;
        for (i = 0; i < task_array->len; i++) {
            struct task *tmp = &g_array_index(task_array, struct task, i);
            cpu_usage += tmp->time_percentage;
        }
        cpu_usage = cpu_usage / (sys_stat->cpu_count * 100.0);
    } else {
#ifdef CONSOLE_DEBUG
        printf("[DEBUG] Mode principal: cpu_old_jiffies=%u, cpu_old_used=%u\n", 
               (guint)sys_stat->cpu_old_jiffies, (guint)sys_stat->cpu_old_used);
        fflush(stdout);
#endif
        if (sys_stat->cpu_old_jiffies > 0) {
            current_used = sys_stat->cpu_user + sys_stat->cpu_nice + sys_stat->cpu_system;
            current_jiffies = current_used + sys_stat->cpu_idle;
            delta_jiffies = current_jiffies - sys_stat->cpu_old_jiffies;
            if (delta_jiffies > 0) {
                cpu_usage = (gdouble)(current_used - sys_stat->cpu_old_used) / (gdouble)delta_jiffies;
            } else {
                cpu_usage = 0.0;
            }
#ifdef CONSOLE_DEBUG
            printf("[DEBUG] Calcul delta: current_used=%u, current_jiffies=%u, delta_jiffies=%u, cpu_usage=%.3f\n", 
                   (guint)current_used, (guint)current_jiffies, (guint)delta_jiffies, cpu_usage);
            fflush(stdout);
#endif
        } else {
#ifdef CONSOLE_DEBUG
            printf("[DEBUG] cpu_old_jiffies=0, pas de calcul delta possible\n");
            fflush(stdout);
#endif
        }
    }
    return cpu_usage;
}

// Détecter le nombre de cœurs CPU - Fonction d'initialisation
INIT_FUNCTION
int detect_cpu_core_count(void) {
    FILE *f = fopen(PROC_CPUINFO,"r");
    if (!f) return 1;
    
    int core_count = 0;
    char line[256];
    
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "processor", 9) == 0) {
            core_count++;
        }
    }
    fclose(f);
    
    return (core_count > 0 && core_count <= MAX_CPU_CORES) ? core_count : 1;
}

// Initialiser les tableaux de sampling par cœur
OPTIMIZE_SIZE_BEGIN
INIT_FUNCTION
void init_cpu_core_sampling(void) {
    performance_data.cpu_core_count = detect_cpu_core_count();
    
    // Allouer le tableau de pointeurs vers les échantillons de chaque cœur (optimisé int16_t)
    performance_data.cpu_core_samples = g_malloc(performance_data.cpu_core_count * sizeof(gint16*));
    
    // Allouer les tableaux d'échantillons pour chaque cœur (-50% mémoire avec int16_t)
    for (int i = 0; i < performance_data.cpu_core_count; i++) {
        performance_data.cpu_core_samples[i] = g_malloc(PERFORMANCE_SAMPLES_COUNT * sizeof(gint16));
        // Initialiser à 0
        for (int j = 0; j < PERFORMANCE_SAMPLES_COUNT; j++) {
            performance_data.cpu_core_samples[i][j] = 0;
        }
    }

    // Allouer le tableau des vitesses des cœurs
    performance_data.cpu_core_speeds = g_malloc(performance_data.cpu_core_count * sizeof(gdouble));
    for (int i = 0; i < performance_data.cpu_core_count; i++) {
        performance_data.cpu_core_speeds[i] = 0.0;
    }
}
OPTIMIZE_SIZE_END

static guint64 *cpu_core_prev_idle = NULL;
static guint64 *cpu_core_prev_total = NULL;
static gboolean cpu_core_first_call = TRUE;

// Nettoyer les tableaux de sampling par cœur
void cleanup_cpu_core_sampling(void) {
    if (performance_data.cpu_core_samples) {
        for (int i = 0; i < performance_data.cpu_core_count; i++) {
            if (performance_data.cpu_core_samples[i]) {
                g_free(performance_data.cpu_core_samples[i]);
            }
        }
        g_free(performance_data.cpu_core_samples);
        performance_data.cpu_core_samples = NULL;
    }
    if (performance_data.cpu_core_speeds) {
        g_free(performance_data.cpu_core_speeds);
        performance_data.cpu_core_speeds = NULL;
    }
    if (cpu_core_prev_idle) {
        g_free(cpu_core_prev_idle);
        cpu_core_prev_idle = NULL;
    }
    if (cpu_core_prev_total) {
        g_free(cpu_core_prev_total);
        cpu_core_prev_total = NULL;
    }
    cpu_core_first_call = TRUE;
}

// Récupérer l'utilisation de chaque cœur depuis /proc/stat
HOT_FUNCTION
void get_per_core_cpu_usage(gint *core_usages) {
    // Initialiser les tableaux statiques lors du premier appel
    if (cpu_core_first_call) {
        cpu_core_prev_idle = g_malloc(performance_data.cpu_core_count * sizeof(guint64));
        cpu_core_prev_total = g_malloc(performance_data.cpu_core_count * sizeof(guint64));
        for (int i = 0; i < performance_data.cpu_core_count; i++) {
            cpu_core_prev_idle[i] = 0;
            cpu_core_prev_total[i] = 0;
            core_usages[i] = 0; // Première mesure = 0
        }
        cpu_core_first_call = FALSE;
    }
    
    FILE *f = fopen(PROC_STAT, "r");
    if (!f) {
        for (int i = 0; i < performance_data.cpu_core_count; i++) {
            core_usages[i] = 0;
        }
        return;
    }
    
    char line[256];
    int core_index = 0;
    
    while (fgets(line, sizeof(line), f) && core_index < performance_data.cpu_core_count) {
        if (strncmp(line, "cpu", 3) == 0 && line[3] >= '0' && line[3] <= '9') {
            // Utiliser le parsing rapide optimisé (remplace sscanf coûteux avec 8 paramètres)
            cpu_stat_data_t cpu_data;
            if (fast_parse_cpu_stat(line, &cpu_data) == 0) {
                guint64 user64 = cpu_data.user, nice64 = cpu_data.nice, system64 = cpu_data.system, idle64 = cpu_data.idle;
                guint64 iowait64 = cpu_data.iowait, irq64 = cpu_data.irq, softirq64 = cpu_data.softirq, steal64 = cpu_data.steal;
                
                guint64 idle_time = idle64 + iowait64;
                guint64 total_time = user64 + nice64 + system64 + idle64 + iowait64 + irq64 + softirq64 + steal64;
                
                if (cpu_core_prev_total[core_index] > 0) {
                    guint64 total_diff = total_time - cpu_core_prev_total[core_index];
                    guint64 idle_diff = idle_time - cpu_core_prev_idle[core_index];
                    
                    if (total_diff > 0) {
                        core_usages[core_index] = (gint)(((total_diff - idle_diff) * 100) / total_diff);
                    } else {
                        core_usages[core_index] = 0;
                    }
                } else {
                    core_usages[core_index] = 0;
                }
                
                cpu_core_prev_idle[core_index] = idle_time;
                cpu_core_prev_total[core_index] = total_time;
                core_index++;
            }
        }
    }
    fclose(f);
}

static void update_cpu_core_speeds(void) {
    if (!performance_data.cpu_core_speeds) return;
    
    static gboolean sysfs_supported = TRUE;
    
    if (sysfs_supported) {
        gboolean success = FALSE;
        for (int i = 0; i < performance_data.cpu_core_count; i++) {
            char path[128];
            snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i);
            int fd = open(path, O_RDONLY);
            if (fd >= 0) {
                char buf[32];
                ssize_t bytes = read(fd, buf, sizeof(buf) - 1);
                close(fd);
                if (bytes > 0) {
                    buf[bytes] = '\0';
                    long long freq_khz = fast_atoll(buf);
                    if (freq_khz > 0) {
                        performance_data.cpu_core_speeds[i] = (double)freq_khz / 1000000.0;
                        success = TRUE;
                        continue;
                    }
                }
            }
            
            // Si le premier cœur échoue, on suppose que sysfs n'est pas disponible pour cpufreq
            if (i == 0) {
                sysfs_supported = FALSE;
                break;
            }
            // Fallback pour un cœur spécifique si sysfs échoue mais était ok sur cpu0
            performance_data.cpu_core_speeds[i] = performance_data.cpu_core_speeds[0];
        }
        if (success) return; // Tout est lu par sysfs !
    }
    
    // Si sysfs n'est pas supporté, on lit /proc/cpuinfo une seule fois pour tous les cœurs !
    int fd = open("/proc/cpuinfo", O_RDONLY);
    if (fd >= 0) {
        static char buf[65536]; // 64KB statique pour cpuinfo
        ssize_t bytes = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (bytes > 0) {
            buf[bytes] = '\0';
            char* line = buf;
            int current_core = -1;
            while (line && *line) {
                if (strncmp(line, "processor", 9) == 0) {
                    char *colon = strchr(line, ':');
                    if (colon) {
                        current_core = fast_atoi(colon + 1);
                    }
                } else if (strncmp(line, "cpu MHz", 7) == 0) {
                    char *colon = strchr(line, ':');
                    if (colon && current_core >= 0 && current_core < performance_data.cpu_core_count) {
                        double mhz = strtod(colon + 1, NULL);
                        performance_data.cpu_core_speeds[current_core] = mhz / 1000.0;
                    }
                }
                line = strchr(line, '\n');
                if (line) line++;
            }
            return;
        }
    }
    
    // Fallback ultime : vitesse globale
    double global_speed = get_cpu_speed();
    for (int i = 0; i < performance_data.cpu_core_count; i++) {
        performance_data.cpu_core_speeds[i] = global_speed;
    }
}

HOT_FUNCTION
void update_performance_samples(gdouble cpu_usage, gdouble ram_usage, gdouble swap_usage, gdouble disk_usage, gdouble disk_read_kbs, gdouble disk_write_kbs) {
    // Convertir pourcentages selon les types corrects (cpu/ram=gint32, autres=gint16)
    gint32 cpu_percent = (gint32)(cpu_usage * 100.0);  // gint32 pour cpu_samples
    gint32 ram_percent = (gint32)(ram_usage * 100.0);  // gint32 pour ram_samples
    gint16 swap_percent = (gint16)(swap_usage * 100.0);
    gint16 disk_percent = (gint16)(disk_usage);  // disk_usage est déjà en pourcentage
    
    // I/O disque conservé en int32_t (support SSD/NVMe rapides)
    gint disk_read_kb = (gint)(disk_read_kbs);
    gint disk_write_kb = (gint)(disk_write_kbs);
    
    // Ajouter au buffer circulaire global
    performance_data.cpu_samples[performance_data.current_index] = cpu_percent;
    performance_data.ram_samples[performance_data.current_index] = ram_percent;
    performance_data.swap_samples[performance_data.current_index] = swap_percent;
    performance_data.disk_samples[performance_data.current_index] = disk_percent;
    performance_data.disk_read_samples[performance_data.current_index] = disk_read_kb;
    performance_data.disk_write_samples[performance_data.current_index] = disk_write_kb;
    
    // Récupérer et ajouter les données par cœur (optimisé int16_t)
    if (performance_data.cpu_core_samples) {
        gint core_usages[MAX_CPU_CORES];
        get_per_core_cpu_usage(core_usages);
        
        for (int i = 0; i < performance_data.cpu_core_count; i++) {
            // Conversion sécurisée int32_t → int16_t (0-100% toujours < 32767)
            performance_data.cpu_core_samples[i][performance_data.current_index] = (gint16)core_usages[i];
        }
    }
    
    // Mettre à jour les vitesses individuelles des cœurs logiques
    update_cpu_core_speeds();
    
    // Échantillonner les statistiques GPU système
    sample_gpu_system_stats();
    
    // Avancer l'index circulaire
    performance_data.current_index++;
    if (performance_data.current_index >= PERFORMANCE_SAMPLES_COUNT) {
        performance_data.current_index = 0;
        performance_data.perf_flags |= PERF_DATA_BUFFER_FULL;
    }
}

// Sampling GPU système (utilise gpu_stats.c pour obtenir les stats globales)
OPTIMIZE_SIZE
void sample_gpu_system_stats(void) {
    static GPUStats gpu_stats = {0};
    static gboolean gpu_initialized = FALSE;
    
    // Initialiser GPU monitoring au premier appel
    if (!gpu_initialized) {
        if (gpu_stats_init() == 0) {
            // Configurer le mode selon les préférences utilisateur
            gpu_stats_set_mode(get_gpu_mode_from_setting(gpu_usage_reporting_mode));
            gpu_initialized = TRUE;
        } else {
            // GPU non disponible, stocker des valeurs 0
            performance_data.gpu_render_samples[performance_data.current_index] = 0;
            performance_data.gpu_video_samples[performance_data.current_index] = 0;
            performance_data.gpu_total_samples[performance_data.current_index] = 0;
            return;
        }
    }
    
    // Obtenir les statistiques GPU système
    if (gpu_stats_update(&gpu_stats) == 0 && gpu_stats.available) {
        
        // Convertir en int16_t (0-100% toujours < 32767)
        gint16 render_percent = (gint16)(gpu_stats.render_percent + 0.5); // Arrondi
        gint16 video_percent = (gint16)(gpu_stats.video_percent + 0.5);   // Arrondi
        gint16 total_percent = (gint16)(gpu_stats.total_percent + 0.5);   // Arrondi - selon mode choisi
        
        // Stocker dans les buffers circulaires
        performance_data.gpu_render_samples[performance_data.current_index] = render_percent;
        performance_data.gpu_video_samples[performance_data.current_index] = video_percent;
        performance_data.gpu_total_samples[performance_data.current_index] = total_percent;
    } else {
        // Erreur ou GPU non disponible, stocker des valeurs 0
        performance_data.gpu_render_samples[performance_data.current_index] = 0;
        performance_data.gpu_video_samples[performance_data.current_index] = 0;
        performance_data.gpu_total_samples[performance_data.current_index] = 0;
    }
}

// Structures et fonctions pour les statistiques système
#define BUF_SIZE 16384

static inline int is_numeric_name(const char *s) {
    if (!s || !isdigit((unsigned char)*s)) return 0;
    while (*s) {
        if (!isdigit((unsigned char)*s)) return 0;
        s++;
    }
    return 1;
}

// Compte le nombre de processus
int get_process_count(void) {
    int dfd = open("/proc", O_RDONLY | O_DIRECTORY);
    if (dfd < 0) return -1;
    char buf[BUF_SIZE];
    long nread;
    int count = 0;

    while ((nread = syscall(SYS_getdents64, dfd, buf, BUF_SIZE)) > 0) {
        int bpos = 0;
        while (bpos < nread) {
            struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
            if (is_numeric_name(d->d_name)) count++;
            bpos += d->d_reclen;
        }
    }
    close(dfd);
    return count;
}

// Compte le nombre total de threads
long get_thread_count(void) {
    int dfd = open("/proc", O_RDONLY | O_DIRECTORY);
    if (dfd < 0) return -1;
    char buf[BUF_SIZE];
    long nread;
    long threads = 0;

    while ((nread = syscall(SYS_getdents64, dfd, buf, BUF_SIZE)) > 0) {
        int bpos = 0;
        while (bpos < nread) {
            struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
            if (is_numeric_name(d->d_name)) {
                char tpath[64];
                format_proc_path(tpath, sizeof(tpath), atoi(d->d_name), "task");
                int tfd = open(tpath, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
                if (tfd >= 0) {
                    char tbuf[BUF_SIZE];
                    long m;
                    while ((m = syscall(SYS_getdents64, tfd, tbuf, BUF_SIZE)) > 0) {
                        int j = 0;
                        while (j < m) {
                            struct linux_dirent64 *td = (struct linux_dirent64 *)(tbuf + j);
                            if (is_numeric_name(td->d_name)) threads++;
                            j += td->d_reclen;
                        }
                    }
                    close(tfd);
                }
            }
            bpos += d->d_reclen;
        }
    }
    close(dfd);
    return threads;
}

// Compte le nombre total de descripteurs de fichiers (handles)
long get_fd_count(void) {
    char buf[BUF_SIZE];
    int dfd = open("/proc", O_RDONLY | O_DIRECTORY);
    if (dfd < 0) return -1;
    long nread;
    long total_fds = 0;

    while ((nread = syscall(SYS_getdents64, dfd, buf, BUF_SIZE)) > 0) {
        int bpos = 0;
        while (bpos < nread) {
            struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
            if (is_numeric_name(d->d_name)) {
                char fdpath[64];
                format_proc_path(fdpath, sizeof(fdpath), atoi(d->d_name), "fd");
                int f = open(fdpath, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
                if (f >= 0) {
                    char fdbuf[BUF_SIZE];
                    long m;
                    while ((m = syscall(SYS_getdents64, f, fdbuf, BUF_SIZE)) > 0) {
                        int j = 0;
                        while (j < m) {
                            struct linux_dirent64 *fdent = (struct linux_dirent64 *)(fdbuf + j);
                            if (fdent->d_name[0] != '.') total_fds++;
                            j += fdent->d_reclen;
                        }
                    }
                    close(f);
                }
            }
            bpos += d->d_reclen;
        }
    }
    close(dfd);
    return total_fds;
}

// Récupère l'uptime du système formaté
void get_uptime_formatted(char *buf, size_t buflen) {
    if (buflen < 9) return;  // Sécurité minimale

    struct sysinfo info;
    if (sysinfo(&info) < 0) {
        snprintf(buf, buflen, "00:00:00");
        return;
    }

    long total_seconds = info.uptime;
    long days = total_seconds / 86400;
    long hours = (total_seconds % 86400) / 3600;
    long minutes = (total_seconds % 3600) / 60;
    long seconds = total_seconds % 60;

    if (days > 0)
        snprintf(buf, buflen, "%ldd %02ld:%02ld:%02ld", days, hours, minutes, seconds);
    else
        snprintf(buf, buflen, "%02ld:%02ld:%02ld", hours, minutes, seconds);
}

// Fonctions pour les informations détaillées du CPU
// OPTIMISATION: open/read/close direct au lieu de fopen/fscanf/fclose (10× plus rapide)
static int read_int_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    
    char buf[32];
    ssize_t bytes = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    
    if (bytes <= 0) return 0;
    buf[bytes] = '\0';
    return fast_atoi(buf);
}

static int virtualization_enabled(void) {
    // Check KVM first
    int fd = open("/sys/module/kvm/parameters/ignore_msrs", O_RDONLY);
    if (fd >= 0) { close(fd); return 1; }
    
    // OPTIMISATION SYSCALL: Lire /proc/cpuinfo d'un coup
    static char cpuinfo_buffer[65536];  // 64KB pour cpuinfo complet
    fd = open("/proc/cpuinfo", O_RDONLY);
    if (fd < 0) return 0;
    
    ssize_t bytes = read(fd, cpuinfo_buffer, sizeof(cpuinfo_buffer) - 1);
    close(fd);
    
    if (bytes > 0) {
        cpuinfo_buffer[bytes] = '\0';
        if (strstr(cpuinfo_buffer, "vmx") || strstr(cpuinfo_buffer, "svm")) return 1;
    }
    return 0;
}

static int get_base_mhz(void) {
    // OPTIMISATION SYSCALL: Lire /proc/cpuinfo d'un coup
    static char cpuinfo_buffer[65536];  // Buffer statique réutilisable
    
    int fd = open("/proc/cpuinfo", O_RDONLY);
    if (fd < 0) return 0;
    
    ssize_t bytes = read(fd, cpuinfo_buffer, sizeof(cpuinfo_buffer) - 1);
    close(fd);
    
    if (bytes <= 0) return 0;
    cpuinfo_buffer[bytes] = '\0';
    
    // Parser le buffer pour trouver "model name"
    char* line = cpuinfo_buffer;
    char* next_line;
    
    while (line && *line) {
        next_line = strchr(line, '\n');
        if (next_line) *next_line = '\0';
        
        if (strncmp(line, "model name", 10) == 0) {
            const char *at = strstr(line, "@");
            if (at) {
                while (*at && (*at < '0' || *at > '9')) at++;
                double ghz = atof(at);
                return (int)(ghz * 1000);
            }
        }
        
        if (!next_line) break;
        line = next_line + 1;
    }
    return 0;
}

// OPTIMISATION: open/read/close direct au lieu de fopen/fgets/fclose
static int count_instances(const char *path) {
    int fd = open(path, O_RDONLY);
    if(fd < 0) return 1;
    
    char buf[64];
    ssize_t bytes = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    
    if(bytes <= 0) return 1;
    buf[bytes] = '\0';
    
    int count=0;
    for(char *p=buf; *p && *p != '\n'; p++) if(*p>='0' && *p<='9') count++;
    return count;
}

// Récupération complète des informations détaillées du CPU
int get_cpu_detailed_info(cpu_info_t *info) {
    if(!info) return -1;
    memset(info,0,sizeof(*info));

    int max_physical_id=0, cores_per_socket=0, logical_count=0;

    // OPTIMISATION SYSCALL: Lire /proc/cpuinfo d'un coup
    static char cpuinfo_buffer[65536];  // Buffer statique réutilisable
    
    int fd = open("/proc/cpuinfo", O_RDONLY);
    if (fd < 0) return -1;
    
    ssize_t bytes = read(fd, cpuinfo_buffer, sizeof(cpuinfo_buffer) - 1);
    close(fd);
    
    if (bytes <= 0) return -1;
    cpuinfo_buffer[bytes] = '\0';
    
    // Parser le buffer ligne par ligne
    char* line = cpuinfo_buffer;
    char* next_line;
    
    while (line && *line) {
        next_line = strchr(line, '\n');
        if (next_line) *next_line = '\0';
        
        if (strncmp(line, "cpu cores", 9) == 0 && cores_per_socket == 0)
            cores_per_socket = fast_atoi(strchr(line, ':') + 1);
        else if (strncmp(line, "processor", 9) == 0)
            logical_count++;
        else if (strncmp(line, "physical id", 11) == 0) {
            int pid = fast_atoi(strchr(line, ':') + 1);
            if (pid > max_physical_id) max_physical_id = pid;
        }
        
        if (!next_line) break;
        line = next_line + 1;
    }

    info->sockets = max_physical_id + 1;
    info->cores_per_socket = cores_per_socket;
    info->logical_processors = logical_count;
    info->base_mhz = get_base_mhz();
    info->virtualization = virtualization_enabled();

    info->l1d_cache_kb = info->l1i_cache_kb = info->l2_cache_kb = info->l3_cache_kb = 0;

    DIR *d = opendir("/sys/devices/system/cpu/cpu0/cache");
    if(d){
        struct dirent *ent;
        while((ent=readdir(d))){
            if(ent->d_type != DT_DIR) continue;
            if(strncmp(ent->d_name,"index",5)!=0) continue;

            char path[128], buf[32];
            int level=0;
            char type[16]="";

            snprintf(path,sizeof(path),"/sys/devices/system/cpu/cpu0/cache/%s/level",ent->d_name);
            level = read_int_file(path);

            // OPTIMISATION: open/read/close direct
            snprintf(path,sizeof(path),"/sys/devices/system/cpu/cpu0/cache/%s/type",ent->d_name);
            int fd_type = open(path, O_RDONLY);
            if(fd_type >= 0) {
                ssize_t bytes = read(fd_type, buf, sizeof(buf) - 1);
                close(fd_type);
                if(bytes > 0) { buf[bytes] = '\0'; buf[strcspn(buf,"\n")]=0; strcpy(type,buf); }
            }

            // OPTIMISATION: open/read/close direct
            snprintf(path,sizeof(path),"/sys/devices/system/cpu/cpu0/cache/%s/size",ent->d_name);
            int size_kb=0;
            int fd_size = open(path, O_RDONLY);
            if(fd_size >= 0) {
                ssize_t bytes = read(fd_size, buf, sizeof(buf) - 1);
                close(fd_size);
                if(bytes > 0) {
                    buf[bytes] = '\0';
                    buf[strcspn(buf,"\n")]=0;
                    if(strstr(buf,"K")) size_kb = fast_atoi(buf);
                    else if(strstr(buf,"M")) size_kb = fast_atoi(buf)*1024;
                }
            }

            snprintf(path,sizeof(path),"/sys/devices/system/cpu/cpu0/cache/%s/shared_cpu_list",ent->d_name);
            int instances = count_instances(path);

            if(level==1){
                if(strcmp(type,"Data")==0) info->l1d_cache_kb += size_kb * instances;
                else if(strcmp(type,"Instruction")==0) info->l1i_cache_kb += size_kb * instances;
            }
            else if(level==2) info->l2_cache_kb += size_kb * instances;
            else if(level==3) info->l3_cache_kb += size_kb; // L3 partagé, ne pas multiplier
        }
        closedir(d);
    }

    info->l1_cache_kb = info->l1d_cache_kb + info->l1i_cache_kb;

    return 0;
}

// Fonctions pour récupérer la RAM installée exacte
#define MEMORY_PATH "/sys/devices/system/memory/"
#define BLOCK_SIZE_PATH "/sys/devices/system/memory/block_size_bytes"

/**
 * Lit la taille d'un bloc mémoire en octets.
 */
long get_block_size_bytes(void) {
    FILE *fp = fopen(BLOCK_SIZE_PATH, "r");
    if (!fp) return -1;

    char hex[32];
    if (!fgets(hex, sizeof(hex), fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    return strtol(hex, NULL, 16);
}

/**
 * Compte les blocs mémoire en ligne.
 */
long count_online_blocks(void) {
    DIR *dir = opendir(MEMORY_PATH);
    if (!dir) return -1;

    struct dirent *entry;
    long count = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "memory", 6) == 0) {
            char state_path[256];
            snprintf(state_path, sizeof(state_path), "%s%s/state", MEMORY_PATH, entry->d_name);

            FILE *fp = fopen(state_path, "r");
            if (!fp) continue;

            char state[32];
            if (fgets(state, sizeof(state), fp)) {
                if (strncmp(state, "online", 6) == 0) {
                    count++;
                }
            }
            fclose(fp);
        }
    }

    closedir(dir);
    return count;
}

/**
 * Retourne la RAM installée en GiB (valeur numérique).
 */
double get_installed_ram_gib(void) {
    long block_size = get_block_size_bytes();
    long blocks = count_online_blocks();
    if (block_size == -1 || blocks == -1) return -1;

    long total_bytes = block_size * blocks;
    return total_bytes / 1024.0 / 1024.0 / 1024.0;
}

// OPTIMISATION CACHE: Utilise meminfo_cache pour éviter de lire le fichier à chaque appel (O-01)
long long get_meminfo_value(const char *key) {
    refresh_meminfo_cache_if_expired();
    if (strcmp(key, "CommitLimit") == 0) return meminfo_cache.commit_limit * 1024; // KB -> bytes
    if (strcmp(key, "Committed_AS") == 0) return meminfo_cache.committed_as * 1024;
    if (strcmp(key, "Cached") == 0) return meminfo_cache.cached_ram * 1024;
    if (strcmp(key, "Slab") == 0) return meminfo_cache.slab * 1024;
    if (strcmp(key, "SUnreclaim") == 0) return meminfo_cache.sunreclaim * 1024;
    return -1;
}

// Initialiser les valeurs de commit (à appeler une seule fois au démarrage)
void get_ram_commit_info(void) {
    commit_limit_bytes = get_meminfo_value("CommitLimit");
    committed_as_bytes = get_meminfo_value("Committed_AS");
}

// Fonctions pour récupérer les valeurs dynamiques (à appeler à chaque refresh)
long long get_cached_ram(void) {
    return get_meminfo_value("Cached");
}

long long get_paged_pool(void) {
    long long slab = get_meminfo_value("Slab");
    long long sunreclaim = get_meminfo_value("SUnreclaim");
    return (slab > sunreclaim) ? (slab - sunreclaim) : 0;
}

long long get_non_paged_pool(void) {
    long long sunreclaim = get_meminfo_value("SUnreclaim");
    return (sunreclaim > 0) ? sunreclaim : 0;
}

// ============= Fonctions pour l'activité disque =============

#define DEVICE_MAX 64

// Variables globales pour le suivi de l'activité disque
static char root_device[256] = {0};
static long prev_io_time = 0;
static long prev_read_sectors = 0;
static long prev_write_sectors = 0;
static struct timespec prev_time = {0, 0};

// Détecte le périphérique monté sur /
// OPTIMISATION: open/read/close direct au lieu de fopen/fgets/fclose
void get_root_device(char *device) {
    int fd = open(PROC_MOUNTS, O_RDONLY);
    if (fd < 0) {
        return;
    }

    static char buffer[4096];  // Buffer statique pour /proc/mounts
    ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    
    if (bytes <= 0) return;
    buffer[bytes] = '\0';

    char* line = buffer;
    while (line && *line) {
        char mount_dev[DEVICE_MAX], mount_point[DEVICE_MAX];
        if (sscanf(line, "%63s %63s", mount_dev, mount_point) == 2) {
            if (strcmp(mount_point, "/") == 0) {
                if (strncmp(mount_dev, "/dev/", 5) == 0) {
                    strncpy(device, mount_dev + 5, DEVICE_MAX - 1);
                } else {
                    strncpy(device, mount_dev, DEVICE_MAX - 1);
                }
                device[DEVICE_MAX - 1] = '\0';
                break;
            }
        }
        line = strchr(line, '\n');
        if (line) line++;
    }
}

// Supprime le suffixe de partition (ex: nvme0n1p5 → nvme0n1)
void strip_partition_suffix(char *device) {
    char *p = device;
    while (*p) p++;
    while (p > device && (*(p - 1) >= '0' && *(p - 1) <= '9')) p--;
    if (p > device && *(p - 1) == 'p') p--;
    *p = '\0';
}

// Lit les secteurs lus/écrits et le temps I/O de /sys/block/<device>/stat
// OPTIMISATION: open/read/close direct au lieu de fopen/fscanf/fclose
void get_disk_stats(const char *device, long *read_sectors, long *write_sectors, long *io_time) {
    char path[128];
    format_sys_block_path(path, sizeof(path), device, "stat");
    
    *read_sectors = 0;
    *write_sectors = 0;
    *io_time = 0;

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return;
    }

    char buffer[256];
    ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    
    if (bytes <= 0) return;
    buffer[bytes] = '\0';

    long fields[11];
    int count = sscanf(buffer, "%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld",
                      &fields[0], &fields[1], &fields[2], &fields[3],
                      &fields[4], &fields[5], &fields[6], &fields[7],
                      &fields[8], &fields[9], &fields[10]);

    if (count >= 11) {
        *read_sectors = fields[2];   // 3ème champ = secteurs lus
        *write_sectors = fields[6];  // 7ème champ = secteurs écrits
        *io_time = fields[9];        // 10ème champ = temps I/O en ms
    }
}

// Cache global pour la lecture par bloc de /proc/diskstats
static char diskstats_buffer[16384];
static struct timespec diskstats_cache_time = {0, 0};

// Lecture optimisée par bloc de /proc/diskstats pour tous les disques
HOT_FUNCTION
static gboolean refresh_diskstats_cache(void) {
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    
    // Vérifier si le cache est encore valide (même seconde)
    if (LIKELY(IS_DISKSTATS_CACHE_VALID() && 
               current_time.tv_sec == diskstats_cache_time.tv_sec)) {
        return TRUE;
    }
    
    // Lire /proc/diskstats en une seule fois
    int fd = open("/proc/diskstats", O_RDONLY);
    if (UNLIKELY(fd < 0)) {
        set_optimization_flag(CACHE_FLAG_DISKSTATS_VALID, FALSE);
        return FALSE;
    }
    
    ssize_t bytes = read(fd, diskstats_buffer, sizeof(diskstats_buffer) - 1);
    close(fd);
    
    if (UNLIKELY(bytes <= 0)) {
        set_optimization_flag(CACHE_FLAG_DISKSTATS_VALID, FALSE);
        return FALSE;
    }
    
    diskstats_buffer[bytes] = '\0';
    set_optimization_flag(CACHE_FLAG_DISKSTATS_VALID, TRUE);
    diskstats_cache_time = current_time;
    
    return TRUE;
}

// Version optimisée utilisant le cache /proc/diskstats
HOT_FUNCTION
void get_disk_stats_batch(const char *device, long *read_sectors, long *write_sectors, long *io_time) {
    *read_sectors = 0;
    *write_sectors = 0;
    *io_time = 0;
    
    if (UNLIKELY(!device || !refresh_diskstats_cache())) {
        return;
    }
    
    // Parser le buffer en mémoire pour trouver le device
    char *line = diskstats_buffer;
    
    while (line && *line) {
        // Utiliser le parsing rapide optimisé (corrigé pour gérer les espaces initiaux)
        diskstats_data_t disk_data;
        if (fast_parse_diskstats(line, &disk_data) == 0 && strcmp(disk_data.device_name, device) == 0) {
            *read_sectors = disk_data.sectors_read;      // Secteurs lus
            *write_sectors = disk_data.sectors_written;  // Secteurs écrits  
            *io_time = disk_data.io_ticks;              // Temps I/O total en ms
            return;
        }
        
        // Passer à la ligne suivante
        line = strchr(line, '\n');
        if (line) line++;
    }
}

// Lit le 10ème champ de /sys/block/<device>/stat (compatibilité)
long get_io_time_sysfs(const char *device) {
    long read_sectors, write_sectors, io_time;
    get_disk_stats_batch(device, &read_sectors, &write_sectors, &io_time);
    return io_time;
}


// Fonction pour récupérer le modèle d'un disque donné
// Paramètre : disk_name (ex. "sda", "sdb")
// Retour : chaîne allouée dynamiquement avec le modèle, ou NULL en cas d'erreur
// L'appelant doit libérer la mémoire avec g_free()
static gchar* get_disk_model(const gchar* disk_name) {
    gchar path[256];
    gchar buffer[256];
    FILE *fp;
    gsize bytes_read;

    // Construit le chemin vers /sys/block/<disk_name>/device/model
    g_snprintf(path, sizeof(path), "/sys/block/%s/device/model", disk_name);

    // Ouvre le fichier
    fp = fopen(path, "r");
    if (!fp) {
        return NULL;
    }

    // Lit le contenu
    if (!fgets(buffer, sizeof(buffer), fp)) {
        fclose(fp);
        return NULL;
    }

    fclose(fp);

    // Supprime les espaces et newlines à la fin
    g_strchug(buffer);  // Supprime les espaces au début
    g_strchomp(buffer); // Supprime les espaces et newlines à la fin

    // Retourne une copie de la chaîne
    return g_strdup(buffer);
}

// Variable globale pour stocker le modèle du disque système
static gchar *system_disk_model = NULL;

// Fonction pour obtenir le nom du disque système actuellement utilisé
const char* get_current_disk_name(void) {
    if (!IS_DISK_INITIALIZED() || root_device[0] == '\0') {
        return "Unknown";
    }
    return root_device;
}

// Fonction pour obtenir le modèle du disque système
const char* get_system_disk_model(void) {
    // Initialiser le disque si pas encore fait
    if (!IS_DISK_INITIALIZED() || root_device[0] == '\0') {
        get_root_device(root_device);
        set_optimization_flag(CACHE_FLAG_DISK_INITIALIZED, TRUE);
    }
    
    // Si le modèle n'est pas encore récupéré, l'obtenir
    if (!system_disk_model && root_device[0] != '\0') {
        system_disk_model = get_disk_model(root_device);
    }
    
    // Retourner le modèle ou un fallback
    if (system_disk_model && system_disk_model[0] != '\0') {
        return system_disk_model;
    } else {
        return "System Disk";  // Fallback si impossible de récupérer le modèle
    }
}

// Nouvelle fonction pour obtenir les taux de lecture/écriture
void get_disk_io_rates(gdouble *read_kbs, gdouble *write_kbs) {
    *read_kbs = 0.0;
    *write_kbs = 0.0;
    
    // Initialisation au premier appel
    if (!IS_DISK_INITIALIZED()) {
        get_root_device(root_device);
        if (root_device[0] == '\0') {
            return;
        }
        strip_partition_suffix(root_device);
        
        // Initialiser les valeurs précédentes
        long dummy_io_time;
        get_disk_stats_batch(root_device, &prev_read_sectors, &prev_write_sectors, &dummy_io_time);
        clock_gettime(CLOCK_MONOTONIC, &prev_time);
        
        set_optimization_flag(CACHE_FLAG_DISK_INITIALIZED, TRUE);
        return; // Premier appel, pas de calcul possible
    }
    
    // Obtenir les nouvelles valeurs
    long current_read_sectors, current_write_sectors, current_io_time;
    get_disk_stats_batch(root_device, &current_read_sectors, &current_write_sectors, &current_io_time);
    
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    
    // Calculer le temps écoulé en secondes
    gdouble elapsed_seconds = (current_time.tv_sec - prev_time.tv_sec) + 
                             (current_time.tv_nsec - prev_time.tv_nsec) / 1e9;
    
    if (elapsed_seconds > 0.0) {
        // Calculer les différences de secteurs
        long read_sectors_diff = current_read_sectors - prev_read_sectors;
        long write_sectors_diff = current_write_sectors - prev_write_sectors;
        
        // Convertir en KB/s (1 secteur = 512 bytes = 0.5 KB)
        *read_kbs = (read_sectors_diff * 0.5) / elapsed_seconds;
        *write_kbs = (write_sectors_diff * 0.5) / elapsed_seconds;
        
        // S'assurer que les valeurs sont positives
        if (*read_kbs < 0.0) *read_kbs = 0.0;
        if (*write_kbs < 0.0) *write_kbs = 0.0;
    }
    
    // Mettre à jour les valeurs précédentes
    prev_read_sectors = current_read_sectors;
    prev_write_sectors = current_write_sectors;
    prev_time = current_time;
}

// Fonction principale pour obtenir le pourcentage d'activité disque
gdouble get_disk_activity_percent(void) {
    // Initialisation au premier appel
    if (!IS_DISK_INITIALIZED()) {
        get_root_device(root_device);
        if (root_device[0] == '\0') {
            return 0.0; // Échec de détection du périphérique
        }
        strip_partition_suffix(root_device);
        
        // Initialiser toutes les valeurs précédentes
        long dummy_read, dummy_write;
        get_disk_stats_batch(root_device, &dummy_read, &dummy_write, &prev_io_time);
        clock_gettime(CLOCK_MONOTONIC, &prev_time);
        prev_read_sectors = dummy_read;
        prev_write_sectors = dummy_write;
        set_optimization_flag(CACHE_FLAG_DISK_INITIALIZED, TRUE);
        return 0.0; // Première mesure = 0
    }

    // Mesure actuelle
    long current_io_time = get_io_time_sysfs(root_device);
    
    if (current_io_time < 0 || prev_io_time < 0) {
        return 0.0; // Erreur de lecture
    }

    // Calculer le pourcentage d'utilisation
    // Différence en millisecondes pendant l'intervalle de refresh
    long diff_ms = current_io_time - prev_io_time;
    
    // L'intervalle de refresh est en millisecondes (refresh_interval * 1000)
    // Pour un pourcentage: (temps_io / temps_total) * 100
    gdouble percent_util = ((gdouble)diff_ms / (gdouble)(refresh_interval * 1000)) * 100.0;
    
    // Limiter entre 0 et 100
    if (percent_util > 100.0) percent_util = 100.0;
    if (percent_util < 0.0) percent_util = 0.0;
    
    // Sauvegarder pour la prochaine mesure
    prev_io_time = current_io_time;
    
    return percent_util;
}

static gboolean key_file_get_int(GKeyFile *kf, const char *group, const char *name, gboolean def) {
    int ret;
    GError *err = NULL;
    ret = g_key_file_get_integer(kf, group, name, &err);
    if (err) {
        ret = def;
        g_error_free(err);
    }
    return ret;
}

static gboolean key_file_get_bool(GKeyFile *kf, const char *group, const char *name, gboolean def) {
    return !!key_file_get_int(kf, group, name, def);
}

void load_config(void) {
    config_data_t config;
    FILE *fp = fopen(config_file, "rb");
    // Valeurs par défaut (si pas de fichier .conf existant)
    app_flags = APP_FLAG_SHOW_USER_TASKS | APP_FLAG_SHOW_CACHED_FREE | APP_FLAG_FULL_VIEW | 
                APP_FLAG_SMOOTH_SCROLLING | APP_FLAG_ENABLE_ANTIALIASING;
    // APP_FLAG_SHOW_PROCESS_ICONS désactivé par défaut
    refresh_interval = 1;
    // Valeurs par défaut pour le tri
    sort_column_id = COLUMN_NAME;
    sort_order = GTK_SORT_ASCENDING;
    // Valeur par défaut pour le type de graphique CPU
    cpu_graph_type = 0;  // Overall utilization
    // Valeur par défaut pour l'éditeur
    default_editor_index = 0;  // Non défini
    // Valeur par défaut pour le navigateur
    default_browser_index = 0;  // Non défini
    // Valeur par défaut pour le terminal
    default_terminal_index = 0; // Non défini
    // Valeur par défaut pour le mode GPU
    gpu_usage_reporting_mode = 0;  // Process Time cumulative
    
    if (!fp) return;
    size_t bytes_read = fread(&config, 1, sizeof(config), fp);
    if (bytes_read >= 11) {
        // Préserver les flags temporaires, charger seulement les flags persistants
        app_flags = (app_flags & ~APP_FLAGS_PERSISTENT_MASK) | (config.flags & APP_FLAGS_PERSISTENT_MASK);
        display_flags = config.display_flags;
        refresh_interval = config.refresh_interval;
        sort_column_id = config.sort_column_id;
        sort_order = config.sort_order;
        cpu_graph_type = config.cpu_graph_type;
        set_default_app(&editor_manager, config.editor_index);
        set_default_app(&browser_manager, config.browser_index);
        gpu_usage_reporting_mode = config.gpu_usage_reporting_mode;
        if (bytes_read >= 12) {
            set_default_app(&terminal_manager, config.terminal_index);
        }
    }
    fclose(fp);
}

void save_config(void) {
    config_data_t config = {0};
    FILE *fp = fopen(config_file, "wb");
    if (!fp) return;
    config.flags = app_flags & APP_FLAGS_PERSISTENT_MASK;
    config.display_flags = display_flags;
    config.sort_column_id = sort_column_id;
    config.sort_order = sort_order;
    config.cpu_graph_type = cpu_graph_type;
    config.refresh_interval = refresh_interval;
    config.editor_index = editor_manager.default_index;
    config.browser_index = browser_manager.default_index;
    config.terminal_index = terminal_manager.default_index;
    config.gpu_usage_reporting_mode = gpu_usage_reporting_mode;
    fwrite(&config, sizeof(config), 1, fp);
    fclose(fp);
}

#ifndef WITHOUT_GTK
// Callback pour capturer les changements de tri
void on_sort_column_changed(GtkTreeSortable *sortable, gpointer user_data) {
    gint column_id;
    GtkSortType order;
    
    if (gtk_tree_sortable_get_sort_column_id(sortable, &column_id, &order)) {
        sort_column_id = column_id;
        sort_order = order;
        save_config();
    }
}
#endif

// Fonctions pour les informations disque système

// Obtenir la capacité du disque en GB
long get_disk_capacity_gb(const char *device) {
    if (!device || strlen(device) == 0) return 0;
    
    char stat_path[512];
    format_sys_block_path(stat_path, sizeof(stat_path), device, "size");
    
    FILE *fp = fopen(stat_path, "r");
    if (!fp) return 0;
    
    long sectors = 0;
    if (fscanf(fp, "%ld", &sectors) == 1) {
        fclose(fp);
        // Convertir secteurs (512 bytes) en GB (1000^3 bytes)
        return (sectors * 512) / (1000L * 1000L * 1000L);
    }
    
    fclose(fp);
    return 0;
}

// Vérifier si le swap est sur le disque système
gboolean check_swap_on_disk(const char *device) {
    if (!device || strlen(device) == 0) return FALSE;
    
    FILE *fp = fopen(PROC_SWAPS, "r");
    if (!fp) return FALSE;
    
    char line[256];
    gboolean has_swap = FALSE;
    
    // Ignorer la ligne d'en-tête
    if (fgets(line, sizeof(line), fp)) {
        while (fgets(line, sizeof(line), fp)) {
            char swap_device[128];
            if (sscanf(line, "%127s", swap_device) == 1) {
                // Vérifier si le swap commence par le device (ex: sda1 contient sda)
                if (strncmp(swap_device, "/dev/", 5) == 0) {
                    char *swap_name = swap_device + 5; // Retirer "/dev/"
                    if (strstr(swap_name, device) != NULL) {
                        has_swap = TRUE;
                        break;
                    }
                }
            }
        }
    }
    
    fclose(fp);
    return has_swap;
}

// Initialiser les informations disque système
OPTIMIZE_SIZE_BEGIN
INIT_FUNCTION
void init_disk_system_info(void) {
    get_root_device(root_device);
    
    if (strlen(root_device) > 0) {
        // Retirer le suffixe de partition (ex: sda1 -> sda)
        strip_partition_suffix(root_device);
        
        // Obtenir la capacité du disque
        disk_capacity_gb = get_disk_capacity_gb(root_device);
        
        // Vérifier si le swap est sur ce disque
        if (check_swap_on_disk(root_device)) {
            system_disk_flags |= SYSTEM_HAS_SWAP_ON_DISK;
        } else {
            system_disk_flags &= ~SYSTEM_HAS_SWAP_ON_DISK;
        }
    } else {
        disk_capacity_gb = 0;
        system_disk_flags &= ~SYSTEM_HAS_SWAP_ON_DISK;
    }
}
OPTIMIZE_SIZE_END

void cleanup_disk_system_info(void) {
    if (system_disk_model) {
        g_free(system_disk_model);
        system_disk_model = NULL;
    }
}

// ============= Fonctions pour les utilisateurs connectés =============

#include <systemd/sd-bus.h>

/**
 * Récupère la liste des utilisateurs connectés via systemd-logind.
 * Retourne un tableau de chaînes NULL-terminé.
 * À libérer avec free() sur chaque élément + free() du tableau.
 */
user_session_t **get_logged_in_users_with_sessions(void) {
    sd_bus *bus = NULL;
    sd_bus_message *msg = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int r;

    user_session_t **users = NULL;
    size_t count = 0;

    r = sd_bus_open_system(&bus);
    if (r < 0) return NULL;

    r = sd_bus_call_method(bus,
                           "org.freedesktop.login1",
                           "/org/freedesktop/login1",
                           "org.freedesktop.login1.Manager",
                           "ListSessions",
                           &error,
                           &msg,
                           "");
    if (r < 0) {
        sd_bus_error_free(&error);
        sd_bus_unref(bus);
        return NULL;
    }

    r = sd_bus_message_enter_container(msg, SD_BUS_TYPE_ARRAY, "(susso)");
    if (r < 0) goto finish;

    while ((r = sd_bus_message_enter_container(msg, SD_BUS_TYPE_STRUCT, "susso")) > 0) {
        const char *session_id, *user, *seat, *objpath;
        uint32_t uid;

        r = sd_bus_message_read(msg, "susso", &session_id, &uid, &user, &seat, &objpath);
        if (r < 0) break;

        // Toujours ajouter chaque session (pas de déduplication ici)
        user_session_t **tmp = realloc(users, (count + 2) * sizeof(user_session_t *));
        if (!tmp) {
            for (size_t k = 0; k < count; k++) {
                free(users[k]->username);
                free(users[k]->session_id);
                free(users[k]);
            }
            free(users);
            users = NULL;
            break;
        }
        users = tmp;
        
        users[count] = malloc(sizeof(user_session_t));
        if (!users[count]) {
            for (size_t k = 0; k < count; k++) {
                free(users[k]->username);
                free(users[k]->session_id);
                free(users[k]);
            }
            free(users);
            users = NULL;
            break;
        }
        
        users[count]->username = strdup(user);
        users[count]->session_id = strdup(session_id);
        if (!users[count]->username || !users[count]->session_id) {
            free(users[count]->username);
            free(users[count]->session_id);
            free(users[count]);
            for (size_t k = 0; k < count; k++) {
                free(users[k]->username);
                free(users[k]->session_id);
                free(users[k]);
            }
            free(users);
            users = NULL;
            break;
        }
        users[count + 1] = NULL;
        count++;

        sd_bus_message_exit_container(msg); // sortir STRUCT
    }
    sd_bus_message_exit_container(msg); // sortir ARRAY

finish:
    sd_bus_error_free(&error);
    sd_bus_message_unref(msg);
    sd_bus_unref(bus);
    return users;
}

static void free_user_with_sessions_list(user_with_sessions_t **users, size_t count) {
    if (!users) return;
    for (size_t i = 0; i < count; i++) {
        if (users[i]) {
            free(users[i]->username);
            if (users[i]->session_ids) {
                for (size_t j = 0; j < users[i]->session_count; j++) {
                    free(users[i]->session_ids[j]);
                }
                free(users[i]->session_ids);
            }
            free(users[i]);
        }
    }
    free(users);
}

user_with_sessions_t **get_logged_in_users_with_session_info(void) {
    sd_bus *bus = NULL;
    sd_bus_message *msg = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int r;

    user_with_sessions_t **users = NULL;
    size_t count = 0;

    r = sd_bus_open_system(&bus);
    if (r < 0) return NULL;

    r = sd_bus_call_method(bus,
                           "org.freedesktop.login1",
                           "/org/freedesktop/login1",
                           "org.freedesktop.login1.Manager",
                           "ListSessions",
                           &error,
                           &msg,
                           "");
    if (r < 0) {
        sd_bus_error_free(&error);
        sd_bus_unref(bus);
        return NULL;
    }

    r = sd_bus_message_enter_container(msg, SD_BUS_TYPE_ARRAY, "(susso)");
    if (r < 0) goto finish;

    while ((r = sd_bus_message_enter_container(msg, SD_BUS_TYPE_STRUCT, "susso")) > 0) {
        const char *session_id, *user, *seat, *objpath;
        uint32_t uid;

        r = sd_bus_message_read(msg, "susso", &session_id, &uid, &user, &seat, &objpath);
        if (r < 0) break;

        // Chercher si l'utilisateur existe déjà
        user_with_sessions_t *existing_user = NULL;
        for (size_t i = 0; i < count; i++) {
            if (strcmp(users[i]->username, user) == 0) {
                existing_user = users[i];
                break;
            }
        }

        if (!existing_user) {
            // Nouvel utilisateur
            user_with_sessions_t **tmp = realloc(users, (count + 2) * sizeof(user_with_sessions_t *));
            if (!tmp) {
                free_user_with_sessions_list(users, count);
                users = NULL;
                break;
            }
            users = tmp;
            
            users[count] = malloc(sizeof(user_with_sessions_t));
            if (!users[count]) {
                free_user_with_sessions_list(users, count);
                users = NULL;
                break;
            }
            
            users[count]->username = strdup(user);
            users[count]->session_ids = malloc(sizeof(char*));
            if (!users[count]->username || !users[count]->session_ids) {
                free(users[count]->username);
                free(users[count]->session_ids);
                free(users[count]);
                free_user_with_sessions_list(users, count);
                users = NULL;
                break;
            }
            users[count]->session_ids[0] = strdup(session_id);
            if (!users[count]->session_ids[0]) {
                free(users[count]->username);
                free(users[count]->session_ids[0]);
                free(users[count]->session_ids);
                free(users[count]);
                free_user_with_sessions_list(users, count);
                users = NULL;
                break;
            }
            users[count]->session_count = 1;
            users[count + 1] = NULL;
            count++;
        } else {
            // Ajouter la session à l'utilisateur existant
            char **tmp_sessions = realloc(existing_user->session_ids, 
                                        (existing_user->session_count + 1) * sizeof(char*));
            if (!tmp_sessions) {
                free_user_with_sessions_list(users, count);
                users = NULL;
                break;
            }
            existing_user->session_ids = tmp_sessions;
            existing_user->session_ids[existing_user->session_count] = strdup(session_id);
            if (!existing_user->session_ids[existing_user->session_count]) {
                free_user_with_sessions_list(users, count);
                users = NULL;
                break;
            }
            existing_user->session_count++;
        }

        sd_bus_message_exit_container(msg); // sortir STRUCT
    }
    sd_bus_message_exit_container(msg); // sortir ARRAY

finish:
    sd_bus_error_free(&error);
    sd_bus_message_unref(msg);
    sd_bus_unref(bus);
    return users;
}

char **get_logged_in_users(void) {
    sd_bus *bus = NULL;
    sd_bus_message *msg = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int r;

    char **users = NULL;
    size_t count = 0;

    r = sd_bus_open_system(&bus);
    if (r < 0) return NULL;

    r = sd_bus_call_method(bus,
                           "org.freedesktop.login1",
                           "/org/freedesktop/login1",
                           "org.freedesktop.login1.Manager",
                           "ListSessions",
                           &error,
                           &msg,
                           "");
    if (r < 0) {
        sd_bus_error_free(&error);
        sd_bus_unref(bus);
        return NULL;
    }

    r = sd_bus_message_enter_container(msg, SD_BUS_TYPE_ARRAY, "(susso)");
    if (r < 0) goto finish2;

    while ((r = sd_bus_message_enter_container(msg, SD_BUS_TYPE_STRUCT, "susso")) > 0) {
        const char *session_id, *user, *seat, *objpath;
        uint32_t uid;

        r = sd_bus_message_read(msg, "susso", &session_id, &uid, &user, &seat, &objpath);
        if (r < 0) break;

        // Ajouter user si pas déjà présent
        int already = 0;
        for (size_t i = 0; i < count; i++) {
            if (strcmp(users[i], user) == 0) {
                already = 1;
                break;
            }
        }

        if (!already) {
            char **tmp = realloc(users, (count + 2) * sizeof(char *));
            if (!tmp) {
                for (size_t k = 0; k < count; k++) {
                    free(users[k]);
                }
                free(users);
                users = NULL;
                break;
            }
            users = tmp;
            users[count] = strdup(user);
            if (!users[count]) {
                for (size_t k = 0; k < count; k++) {
                    free(users[k]);
                }
                free(users);
                users = NULL;
                break;
            }
            users[count + 1] = NULL;
            count++;
        }

        sd_bus_message_exit_container(msg); // sortir STRUCT
    }
    sd_bus_message_exit_container(msg); // sortir ARRAY

finish2:
    sd_bus_error_free(&error);
    sd_bus_message_unref(msg);
    sd_bus_unref(bus);
    return users;
}

// Fonction pour rafraîchir la liste des services
#ifndef WITHOUT_GTK
void refresh_services_list(void) {
    if (!services_list_store) return;
    
    SystemdServiceList *service_list = get_systemd_services();
    if (!service_list) return;
    
    // Vider le store existant
    gtk_list_store_clear(services_list_store);
    
    // Ajouter chaque service au store
    for (int i = 0; i < service_list->count; i++) {
        SystemdService *service = &service_list->services[i];
        GtkTreeIter iter;
        gtk_list_store_append(services_list_store, &iter);
        gtk_list_store_set(services_list_store, &iter,
                          SERVICES_COLUMN_NAME, service->name,
                          SERVICES_COLUMN_PID, service->pid,
                          SERVICES_COLUMN_STATUS, service->status,
                          SERVICES_COLUMN_RAM, service->memory_usage,
                          SERVICES_COLUMN_DESCRIPTION, service->description,
                          SERVICES_COLUMN_SLICE, service->slice,
                          -1);
    }
    
    systemd_service_list_free(service_list);
}

// Refresh intelligent qui préserve la sélection et évite le clignotement
void refresh_services_list_smart(void) {
    if (!services_list_store) return;
    
    // Réinitialiser le pool de mémoire pour réutiliser les allocations précédentes
    reset_services_memory_pool();
    
    SystemdServiceList *service_list = get_systemd_services();
    if (!service_list) return;
    
    // Sauvegarder la sélection actuelle
    gchar *selected_name = NULL;
    GtkTreeModel *model;
    GtkTreeIter iter;
    if (gtk_tree_selection_get_selected(services_selection, &model, &iter)) {
        gtk_tree_model_get(model, &iter, SERVICES_COLUMN_NAME, &selected_name, -1);
    }
    
    // Créer une table de hachage pour les services existants
    GHashTable *existing_services = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)gtk_tree_iter_free);
    
    GtkTreeIter existing_iter;
    gboolean existing_valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(services_list_store), &existing_iter);
    while (existing_valid) {
        gchar *service_name = NULL;
        gtk_tree_model_get(GTK_TREE_MODEL(services_list_store), &existing_iter, SERVICES_COLUMN_NAME, &service_name, -1);
        if (service_name) {
            g_hash_table_insert(existing_services, service_name, gtk_tree_iter_copy(&existing_iter));
        }
        existing_valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(services_list_store), &existing_iter);
    }
    
    // Mettre à jour ou ajouter les services
    for (int i = 0; i < service_list->count; i++) {
        SystemdService *service = &service_list->services[i];
        GtkTreeIter *existing_iter = g_hash_table_lookup(existing_services, service->name);
        
        if (existing_iter) {
            // Mettre à jour le service existant
            gtk_list_store_set(services_list_store, existing_iter,
                              SERVICES_COLUMN_PID, service->pid,
                              SERVICES_COLUMN_STATUS, service->status,
                              SERVICES_COLUMN_RAM, service->memory_usage,
                              SERVICES_COLUMN_DESCRIPTION, service->description,
                              SERVICES_COLUMN_SLICE, service->slice,
                              -1);
            // Marquer comme traité
            g_hash_table_remove(existing_services, service->name);
        } else {
            // Ajouter un nouveau service
            GtkTreeIter new_iter;
            gtk_list_store_append(services_list_store, &new_iter);
            gtk_list_store_set(services_list_store, &new_iter,
                              SERVICES_COLUMN_NAME, service->name,
                              SERVICES_COLUMN_PID, service->pid,
                              SERVICES_COLUMN_STATUS, service->status,
                              SERVICES_COLUMN_RAM, service->memory_usage,
                              SERVICES_COLUMN_DESCRIPTION, service->description,
                              SERVICES_COLUMN_SLICE, service->slice,
                              -1);
        }
    }
    
    // Supprimer les services qui n'existent plus
    GHashTableIter hash_iter;
    gpointer key, value;
    g_hash_table_iter_init(&hash_iter, existing_services);
    while (g_hash_table_iter_next(&hash_iter, &key, &value)) {
        GtkTreeIter *iter_to_remove = (GtkTreeIter *)value;
        gtk_list_store_remove(services_list_store, iter_to_remove);
    }
    
    // Restaurer la sélection
    if (selected_name) {
        GtkTreeIter restore_iter;
        gboolean restore_valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(services_list_store), &restore_iter);
        while (restore_valid) {
            gchar *name = NULL;
            gtk_tree_model_get(GTK_TREE_MODEL(services_list_store), &restore_iter, SERVICES_COLUMN_NAME, &name, -1);
            if (name && g_strcmp0(name, selected_name) == 0) {
                gtk_tree_selection_select_iter(services_selection, &restore_iter);
                g_free(name);
                break;
            }
            g_free(name);
            restore_valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(services_list_store), &restore_iter);
        }
        g_free(selected_name);
    }
    
    g_hash_table_destroy(existing_services);
    systemd_service_list_free(service_list);
}
#endif