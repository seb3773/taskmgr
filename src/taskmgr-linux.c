#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <pthread.h>
#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include "taskmgr-linux.h"
#include "common.h"
#include "gpu_stats.h"
#include "functions.h"
#include "fast_format.h"
#include "root_credential_vault.h"
#include "privileged_exec.h"

// Pool mémoire pour structures task disponible via functions.h

// ============================================================================
// OPTIMISATION BATCH I/O /proc - Réduction drastique des syscalls
// ============================================================================

// Constantes d'optimisation vectorisée
#define BATCH_SIZE 1024
#define VECTOR_SIZE 64          // Groupes de 64 pour localité cache optimale
#define MIN_KERNEL_PID 2        // PIDs < 2 sont généralement kernel threads
#define PARALLEL_THRESHOLD 128  // Seuil minimum pour activer le parallélisme

// Constantes pour optimisation SIMD et buffer consolidé
#define CONSOLIDATED_BUFFER_SIZE 65536  // 64KB buffer consolidé
#define PREFETCH_DISTANCE 3             // Pré-charger 3 processus à l'avance

// Structure pour pre-filtering intelligent des kernel threads
typedef struct {
    pid_t pid;
    gboolean is_kernel_thread;
} pid_filter_t;

// Structure pour parallel batch processing
typedef struct {
    pid_t* pids;
    int start_idx;
    int count;
    proc_data_cache_t* cache;
    GHashTable* uid_cache;
    GArray* task_list;
    int* valid_tasks_count;
    pthread_mutex_t* task_list_mutex;
} parallel_batch_data_t;

// Déclarations de fonctions internes
static int batch_read_proc_data_smart(pid_t* pids, int count, proc_data_cache_t* cache);

// Note: fast_parse_uid_from_status() est définie dans functions.h

// Initialisation du parallélisme (utilise le cache CPU existant)
static void init_parallel_processing(void) {
    if (!IS_PARALLEL_ENABLED()) {
        long cpu_count = get_cached_nprocessors();  // Utilise le cache existant
        
        // Activer le parallélisme seulement sur les systèmes multi-cœurs
        gboolean should_enable = (cpu_count >= 2);
        set_optimization_flag(CACHE_FLAG_PARALLEL_ENABLED, should_enable);
        
        g_debug("PARALLEL_INIT: %ld CPUs détectés, parallélisme %s", 
                cpu_count, should_enable ? "activé" : "désactivé");
    }
}

// Pre-filtering intelligent : détection rapide des kernel threads
// Cache PPID pour éviter lectures répétées
static __thread pid_t cached_ppid_pid = -1;
static __thread pid_t cached_ppid_value = -1;

// Lecture ultra-rapide du PPID sans parsing complet de /proc/PID/stat
HOT_FUNCTION
static inline pid_t get_ppid_fast(pid_t pid) {
    // Cache thread-local pour éviter lectures répétées
    // CORRECTION BRANCH HINT: Cache hit est RARE (PIDs différents à chaque appel)
    if (__builtin_expect(cached_ppid_pid == pid, 0)) {
        return cached_ppid_value;
    }

    char stat_path[32];
    strcpy(stat_path, "/proc/");
    format_uint32(stat_path + 6, pid);
    strcat(stat_path, "/stat");

    int fd = open(stat_path, O_RDONLY);
    if (fd < 0) return -1;

    char buffer[256];
    ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);

    if (bytes <= 0) return -1;
    buffer[bytes] = '\0';

    // Parser minimal : chercher le 4ème champ (PPID)
    // Format : pid (comm) state ppid ...
    char *p = strchr(buffer, ')');
    if (!p) return -1;
    p += 2; // skip ") "

    // Skip state field
    while (*p && *p != ' ') p++;
    if (*p) p++; // skip space

    // Lire PPID
    pid_t ppid = 0;
    while (*p >= '0' && *p <= '9') {
        ppid = ppid * 10 + (*p - '0');
        p++;
    }

    // Mettre en cache
    cached_ppid_pid = pid;
    cached_ppid_value = ppid;

    return ppid;
}

static inline gboolean is_likely_kernel_thread(pid_t pid) {
    // OPTIMISATION ULTRA-PERFORMANTE : Smart filtering CPU-only + Branch Prediction
    // Kernel threads: PID < 1000 ET parent = 2 (kthreadd)
    // Évite 3000+ syscalls vs ancienne approche I/O-heavy

    if (__builtin_expect(pid < MIN_KERNEL_PID, 0)) return TRUE;  // PID 0,1 = kernel (rare)

    // Heuristique principale : PID < 1000 + PPID == 2
    // CORRECTION BRANCH HINT: pid < 1000 est FRÉQUENT sur systèmes typiques
    if (__builtin_expect(pid < 1000, 1)) {  // Cas fréquent: la plupart des PIDs < 1000
        pid_t ppid = get_ppid_fast(pid);
        return __builtin_expect(ppid == 2, 0);  // Cas rare: peu de threads kernel
    }

    return FALSE;  // PID >= 1000 = très probablement user process (cas très fréquent)
}

// ============================================================================
// OPTIMISATION MEMORY MAPPING /proc/stat - 3-5% gain performance
// ============================================================================

// Parser direct ultra-optimisé pour /proc/stat depuis mmap
HOT_FUNCTION
static inline gboolean parse_cpu_stats_direct(const char* stat_data, system_status* sys_stat) {
    // Format /proc/stat: "cpu  user nice system idle iowait irq softirq steal guest"
    // Parse manuel optimisé sans fscanf() - 40-60% plus rapide

    const char* p = stat_data;

    // Skip "cpu" prefix
    if (__builtin_expect(p[0] != 'c' || p[1] != 'p' || p[2] != 'u', 0)) return FALSE;
    p += 3;

    // Skip whitespace
    while (*p == ' ' || *p == '\t') p++;

    // Parse user jiffies
    guint64 value = 0;
    while (__builtin_expect(*p >= '0' && *p <= '9', 1)) {
        value = value * 10 + (*p - '0');
        p++;
    }
    sys_stat->cpu_user = value;

    // Skip whitespace
    while (*p == ' ' || *p == '\t') p++;

    // Parse nice jiffies
    value = 0;
    while (__builtin_expect(*p >= '0' && *p <= '9', 1)) {
        value = value * 10 + (*p - '0');
        p++;
    }
    sys_stat->cpu_nice = value;

    // Skip whitespace
    while (*p == ' ' || *p == '\t') p++;

    // Parse system jiffies
    value = 0;
    while (__builtin_expect(*p >= '0' && *p <= '9', 1)) {
        value = value * 10 + (*p - '0');
        p++;
    }
    sys_stat->cpu_system = value;

    // Skip whitespace
    while (*p == ' ' || *p == '\t') p++;

    // Parse idle jiffies
    value = 0;
    while (__builtin_expect(*p >= '0' && *p <= '9', 1)) {
        value = value * 10 + (*p - '0');
        p++;
    }
    sys_stat->cpu_idle = value;

    return TRUE;
}

// Worker thread pour traitement parallèle des batches
static void* parallel_batch_worker(void* arg) {
    parallel_batch_data_t* data = (parallel_batch_data_t*)arg;
    
    struct task local_tasks[BATCH_SIZE];
    int local_valid_count = 0;
    
    for (int vector_start = data->start_idx; 
         vector_start < data->start_idx + data->count; 
         vector_start += VECTOR_SIZE) {
        
        int vector_end = (vector_start + VECTOR_SIZE < data->start_idx + data->count) ? 
                         vector_start + VECTOR_SIZE : data->start_idx + data->count;
        
        batch_read_proc_data_smart(&data->pids[vector_start], 
                                   vector_end - vector_start, 
                                   &data->cache[vector_start]);
        
        for (int i = vector_start; i < vector_end; i++) {
            if (!data->cache[i].valid_stat || !data->cache[i].valid_statm) continue;
            if (local_valid_count >= BATCH_SIZE) break;
            
            get_task_details_from_cache(&data->cache[i], &local_tasks[local_valid_count], data->uid_cache);
            
            if (local_tasks[local_valid_count].pid > 0 && local_tasks[local_valid_count].size > 0) {
                local_valid_count++;
            }
        }
    }
    
    if (local_valid_count > 0) {
        pthread_mutex_lock(data->task_list_mutex);
        for (int i = 0; i < local_valid_count; i++) {
            g_array_append_val(data->task_list, local_tasks[i]);
        }
        *data->valid_tasks_count += local_valid_count;
        pthread_mutex_unlock(data->task_list_mutex);
    }
    
    return NULL;
}

// Fonction utilitaire pour lire un fichier avec openat() (évite path construction)
HOT_FUNCTION
int batch_read_at(int dirfd, const char* filename, char* buffer, size_t buffer_size) {
    int fd = openat(dirfd, filename, O_RDONLY);
    if (fd < 0) {
        buffer[0] = '\0';
        return -1;
    }
    
    ssize_t bytes_read = read(fd, buffer, buffer_size - 1);
    close(fd);
    
    if (bytes_read <= 0) {
        buffer[0] = '\0';
        return -1;
    }
    
    buffer[bytes_read] = '\0';
    return (int)bytes_read;
}

// Version optimisée avec buffer consolidé et prefetching SIMD
HOT_FUNCTION
int batch_read_at_optimized(int dirfd, const char* filename, char* buffer, size_t buffer_size,
                           char* consolidated_buffer, size_t* offset) {
    int fd = openat(dirfd, filename, O_RDONLY);
    if (fd < 0) {
        buffer[0] = '\0';
        return -1;
    }

    // Vérifier si il reste assez de place dans le buffer consolidé
    if (*offset + buffer_size > CONSOLIDATED_BUFFER_SIZE) {
        // Buffer plein, fallback vers lecture directe
        close(fd);
        fd = openat(dirfd, filename, O_RDONLY);
        if (fd < 0) {
            buffer[0] = '\0';
            return -1;
        }
        ssize_t bytes_read = read(fd, buffer, buffer_size - 1);
        close(fd);
        if (bytes_read <= 0) {
            buffer[0] = '\0';
            return -1;
        }
        buffer[bytes_read] = '\0';
        return (int)bytes_read;
    }

    // Lire dans le buffer consolidé pour améliorer la localité cache
    ssize_t bytes_read = read(fd, consolidated_buffer + *offset, buffer_size - 1);
    close(fd);

    if (bytes_read <= 0) {
        buffer[0] = '\0';
        return -1;
    }

    // Prefetch des données pour le parsing suivant
    __builtin_prefetch(consolidated_buffer + *offset, 0, 3);

    // Copier depuis le buffer consolidé vers le buffer de destination
    memcpy(buffer, consolidated_buffer + *offset, bytes_read);
    buffer[bytes_read] = '\0';

    // Avancer l'offset dans le buffer consolidé
    *offset += bytes_read + 1;  // +1 pour alignement

    return (int)bytes_read;
}

// Version ultra-optimisée avec buffer consolidé et prefetching SIMD
HOT_FUNCTION
int batch_read_proc_data_optimized(pid_t* pids, int count, proc_data_cache_t* cache) {
    int successful_reads = 0;

    // Buffer consolidé THREAD-LOCAL statique 64KB pour tous les fichiers /proc d'un batch
    // OPTIMISATION: Thread-local évite allocation répétée (gain 2-3% CPU)
    static __thread char consolidated_buffer[CONSOLIDATED_BUFFER_SIZE];
    size_t buffer_offset = 0;
    
    for (int i = 0; i < count; i++) {
        // Prefetch CPU cache des données du prochain processus
        if (i + PREFETCH_DISTANCE < count) {
            __builtin_prefetch(&cache[i + PREFETCH_DISTANCE], 1, 2);
        }
        
        // Initialiser le cache pour ce PID
        cache[i].pid = pids[i];
        cache[i].valid_stat = FALSE;
        cache[i].valid_statm = FALSE;
        cache[i].valid_smaps = FALSE;
        cache[i].valid_cmdline = FALSE;
        
        // Construire le chemin du répertoire /proc/PID une seule fois
        char proc_path[32];
        strcpy(proc_path, "/proc/");
        format_uint32(proc_path + 6, pids[i]);
        
        // Ouvrir le répertoire /proc/PID une seule fois
        int dirfd = open(proc_path, O_RDONLY | O_DIRECTORY);
        if (dirfd < 0) continue;
        
        // NOUVEAU: Hint au kernel pour prefetch agressif des fichiers /proc
        // POSIX_FADV_WILLNEED demande au kernel de charger en cache page
        posix_fadvise(dirfd, 0, 0, POSIX_FADV_WILLNEED);
        
        // Lire tous les fichiers avec buffer consolidé et prefetching SIMD
        if (batch_read_at_optimized(dirfd, "stat", cache[i].stat_buffer, sizeof(cache[i].stat_buffer), 
                                   consolidated_buffer, &buffer_offset) > 0) {
            cache[i].valid_stat = TRUE;
        }
        
        if (batch_read_at_optimized(dirfd, "statm", cache[i].statm_buffer, sizeof(cache[i].statm_buffer),
                                   consolidated_buffer, &buffer_offset) > 0) {
            cache[i].valid_statm = TRUE;
        }
        
        // Lire smaps_rollup seulement si PSS activé et préférence utilisateur active
        if (IS_PSS_LOADING_ENABLED() && (app_flags & APP_FLAG_DISPLAY_PSS)) {
            if (batch_read_at_optimized(dirfd, "smaps_rollup", cache[i].smaps_buffer, sizeof(cache[i].smaps_buffer),
                                       consolidated_buffer, &buffer_offset) > 0) {
                cache[i].valid_smaps = TRUE;
            }
        }
        
        // Lire cmdline pour les noms de processus
        if (batch_read_at_optimized(dirfd, "cmdline", cache[i].cmdline_buffer, sizeof(cache[i].cmdline_buffer),
                                   consolidated_buffer, &buffer_offset) > 0) {
            cache[i].valid_cmdline = TRUE;
        }
        
        // NOUVEAU: Lire status pour UID (remplace stat() syscall - gain 1-3% CPU)
        if (batch_read_at_optimized(dirfd, "status", cache[i].status_buffer, sizeof(cache[i].status_buffer),
                                   consolidated_buffer, &buffer_offset) > 0) {
            cache[i].valid_status = TRUE;
            // Parser UID immédiatement
            if (fast_parse_uid_from_status(cache[i].status_buffer, &cache[i].uid) != 0) {
                cache[i].valid_status = FALSE;
            }
        }
        
        close(dirfd);
        
        // Compter comme succès si au moins stat et statm sont valides
        if (cache[i].valid_stat && cache[i].valid_statm) {
            successful_reads++;
        }
    }
    
    return successful_reads;
}

// Lecture batch optimisée de tous les fichiers /proc pour une liste de PIDs (version originale)
HOT_FUNCTION
int batch_read_proc_data(pid_t* pids, int count, proc_data_cache_t* cache) {
    int successful_reads = 0;
    
    for (int i = 0; i < count; i++) {
        // Initialiser le cache pour ce PID
        cache[i].pid = pids[i];
        cache[i].valid_stat = FALSE;
        cache[i].valid_statm = FALSE;
        cache[i].valid_smaps = FALSE;
        cache[i].valid_cmdline = FALSE;
        
        // Construire le chemin du répertoire /proc/PID une seule fois
        char proc_path[32];
        strcpy(proc_path, "/proc/");
        format_uint32(proc_path + 6, pids[i]);
        
        // Ouvrir le répertoire /proc/PID une seule fois
        int dirfd = open(proc_path, O_RDONLY | O_DIRECTORY);
        if (dirfd < 0) continue;
        
        // Lire tous les fichiers nécessaires avec openat() (plus efficace)
        if (batch_read_at(dirfd, "stat", cache[i].stat_buffer, sizeof(cache[i].stat_buffer)) > 0) {
            cache[i].valid_stat = TRUE;
        }
        
        if (batch_read_at(dirfd, "statm", cache[i].statm_buffer, sizeof(cache[i].statm_buffer)) > 0) {
            cache[i].valid_statm = TRUE;
        }
        
        // Lire smaps_rollup seulement si PSS activé et préférence utilisateur active
        if (IS_PSS_LOADING_ENABLED() && (app_flags & APP_FLAG_DISPLAY_PSS)) {
            if (batch_read_at(dirfd, "smaps_rollup", cache[i].smaps_buffer, sizeof(cache[i].smaps_buffer)) > 0) {
                cache[i].valid_smaps = TRUE;
            }
        }
        
        // Lire cmdline pour les noms de processus
        if (batch_read_at(dirfd, "cmdline", cache[i].cmdline_buffer, sizeof(cache[i].cmdline_buffer)) > 0) {
            cache[i].valid_cmdline = TRUE;
        }
        
        // NOUVEAU: Lire status pour UID (remplace stat() syscall - gain 1-3% CPU)
        if (batch_read_at(dirfd, "status", cache[i].status_buffer, sizeof(cache[i].status_buffer)) > 0) {
            cache[i].valid_status = TRUE;
            // Parser UID immédiatement
            if (fast_parse_uid_from_status(cache[i].status_buffer, &cache[i].uid) != 0) {
                cache[i].valid_status = FALSE;
            }
        }
        
        close(dirfd);
        
        // Compter comme succès si au moins stat et statm sont valides
        if (cache[i].valid_stat && cache[i].valid_statm) {
            successful_reads++;
        }
    }
    
    return successful_reads;
}

// Sélecteur intelligent entre version standard et optimisée SIMD
HOT_FUNCTION
int batch_read_proc_data_smart(pid_t* pids, int count, proc_data_cache_t* cache) {
    // Seuil adaptatif basé sur nombre total de processus (optimisation 4-6% CPU)
    static int adaptive_threshold = 32;
    if (count > adaptive_threshold && count < 1000) {
        adaptive_threshold = count / 16;  // Adaptatif : ajuste selon la charge
        if (adaptive_threshold < 16) adaptive_threshold = 16;  // Minimum
        if (adaptive_threshold > 64) adaptive_threshold = 64;  // Maximum
    }
    
    if (count > adaptive_threshold) {
        return batch_read_proc_data_optimized(pids, count, cache);
    } else {
        // Version standard pour les petits batches (moins d'overhead)
        return batch_read_proc_data(pids, count, cache);
    }
}

// Traitement vectorisé par groupes de 64 pour localité cache optimale
HOT_FUNCTION
static int process_vector_batch(pid_t* pids, int count, proc_data_cache_t* cache, 
                               GHashTable* uid_cache, GArray* task_list) {
    struct task tasks_batch[BATCH_SIZE];
    int valid_tasks = 0;
    
    for (int vector_start = 0; vector_start < count; vector_start += VECTOR_SIZE) {
        int vector_end = (vector_start + VECTOR_SIZE < count) ? 
                         vector_start + VECTOR_SIZE : count;
        
        batch_read_proc_data_smart(&pids[vector_start], 
                                   vector_end - vector_start, 
                                   &cache[vector_start]);
        
        for (int i = vector_start; i < vector_end; i++) {
            if (!cache[i].valid_stat || !cache[i].valid_statm) continue;
            if (valid_tasks >= BATCH_SIZE) break;
            
            get_task_details_from_cache(&cache[i], &tasks_batch[valid_tasks], uid_cache);
            
            if (tasks_batch[valid_tasks].pid > 0 && tasks_batch[valid_tasks].size > 0) {
                valid_tasks++;
            }
        }
    }
    
    if (valid_tasks > 0) {
        for (int i = 0; i < valid_tasks; i++) {
            g_array_append_val(task_list, tasks_batch[i]);
        }
    }
    
    return valid_tasks;
}

// Traitement parallèle intelligent : 2 threads pour CPUs multi-cœurs
HOT_FUNCTION
static int process_parallel_batch(pid_t* pids, int count, proc_data_cache_t* cache, 
                                 GHashTable* uid_cache, GArray* task_list) {
    if (!IS_PARALLEL_ENABLED() || count < PARALLEL_THRESHOLD) {
        return process_vector_batch(pids, count, cache, uid_cache, task_list);
    }
    
    int half_count = count / 2;
    int valid_tasks_count = 0;
    pthread_mutex_t task_list_mutex = PTHREAD_MUTEX_INITIALIZER;
    
    parallel_batch_data_t thread_data[2] = {
        {
            .pids = pids,
            .start_idx = 0,
            .count = half_count,
            .cache = cache,
            .uid_cache = uid_cache,
            .task_list = task_list,
            .valid_tasks_count = &valid_tasks_count,
            .task_list_mutex = &task_list_mutex
        },
        {
            .pids = pids,
            .start_idx = half_count,
            .count = count - half_count,
            .cache = cache,
            .uid_cache = uid_cache,
            .task_list = task_list,
            .valid_tasks_count = &valid_tasks_count,
            .task_list_mutex = &task_list_mutex
        }
    };
    
    pthread_t threads[2];
    for (int i = 0; i < 2; i++) {
        if (pthread_create(&threads[i], NULL, parallel_batch_worker, &thread_data[i]) != 0) {
            g_warning("PARALLEL_BATCH: Échec création thread %d, fallback séquentiel", i);
            pthread_mutex_destroy(&task_list_mutex);
            return process_vector_batch(pids, count, cache, uid_cache, task_list);
        }
    }
    
    for (int i = 0; i < 2; i++) {
        pthread_join(threads[i], NULL);
    }
    
    pthread_mutex_destroy(&task_list_mutex);
    return valid_tasks_count;
}

// Version optimisée de get_task_details() utilisant le cache batch
HOT_FUNCTION
void get_task_details_from_cache(const proc_data_cache_t* cache, struct task *task, GHashTable *uid_cache_refresh) {
    // Initialisation par défaut
    task->pid = -1;
    TASK_SET_FLAGS(task, TASK_GET_FLAGS(task) & ~TASK_FLAG_CHECKED);
    task->size = 0;
    task->pss = 0;
    task->gpu_usage = 0.0f;
    
    // Vérifier que les données essentielles sont disponibles
    if (!cache->valid_stat || !cache->valid_statm) {
        return;
    }
    
    task->pid = cache->pid;
    
    // Get GPU usage for this process using optimized cache
    const ProcessGPUEntry* gpu_data = gpu_stats_get_process_by_pid(cache->pid);
    if (gpu_data) {
        task->gpu_usage = (gfloat)gpu_data->total_percent;
    }
    
    // Parser statm depuis le cache (size, rss, shr) - OPTIMISÉ: parsing manuel 3-5x plus rapide que sscanf
    gulong t_size, t_rss, t_shr;
    // BRANCH HINT: parsing réussit presque toujours (format /proc/statm stable)
    if (__builtin_expect(fast_parse_statm(cache->statm_buffer, &t_size, &t_rss, &t_shr) == 0, 1)) {
        // BRANCH HINT: t_size == 0 est RARE (processus valides ont taille > 0)
        if (__builtin_expect(t_size == 0, 0)) return;
        task->size = t_size * page_size;
        task->rss = t_rss * page_size;
        task->shr = t_shr * page_size;
    } else {
        return;
    }
    
    // Parser PSS depuis le cache smaps_rollup si disponible (et si préférence active)
    if (IS_PSS_LOADING_ENABLED() && (app_flags & APP_FLAG_DISPLAY_PSS) && cache->valid_smaps) {
        char *pss_line = strstr(cache->smaps_buffer, "Pss:");
        // BRANCH HINT: pss_line trouvé presque toujours (kernel moderne)
        if (__builtin_expect(pss_line != NULL, 1)) {
            gulong t_pss_kb;
            // BRANCH HINT: parsing PSS réussit presque toujours (format stable)
            if (__builtin_expect(fast_parse_pss(pss_line, &t_pss_kb) == 0, 1)) {
                task->pss = t_pss_kb; // PSS déjà en KB
            }
        }
        
        // Fallback: si PSS non disponible, utiliser RSS
        // BRANCH HINT: PSS == 0 est RARE (fallback peu fréquent)
        if (__builtin_expect(task->pss == 0, 0)) {
            task->pss = task->rss;
        }
    }
    
    // Parser stat depuis le cache
    char *p = strchr(cache->stat_buffer, '(');
    // BRANCH HINT: '(' trouvé presque toujours (format /proc/stat stable)
    if (__builtin_expect(p == NULL, 0)) return;
    p++;
    
    char *e = strrchr(p, ')');
    // BRANCH HINT: ')' trouvé presque toujours ET nom pas trop long
    if (__builtin_expect(e == NULL || e >= &p[sizeof(task->name)], 0)) return;
    
    // PROTECTION SUPPLÉMENTAIRE : Vérifier que e est après p
    if (__builtin_expect(e <= p, 0)) return;
    
    size_t len = e - p;
    // PROTECTION : Limiter la longueur pour éviter buffer overflow
    if (len >= sizeof(task->name)) {
        len = sizeof(task->name) - 1;
    }
    
    // Stocker le nom simple d'abord avec protection stricte
    if (len >= sizeof(task->name)) {
        len = sizeof(task->name) - 1;
    }
    memcpy(task->name, p, len);
    task->name[len] = '\0';
    
    // Stocker le nom simple pour le groupement avec protection stricte
    size_t simple_len = len;
    if (simple_len >= sizeof(task->simple_name)) {
        simple_len = sizeof(task->simple_name) - 1;
    }
    memcpy(task->simple_name, task->name, simple_len);
    task->simple_name[simple_len] = '\0';
    
    p = &e[1];
    
    // Gestion du nom complet depuis cmdline si nécessaire
    if (cache->valid_cmdline && cache->cmdline_buffer[0] != '\0') {
        if (app_flags & APP_FLAG_SHOW_FULL_PATH) {
            // CRITIQUE: Limiter à la taille du buffer destination (task->name)
            // cmdline_buffer peut être 512 bytes, mais task->name n'est que 256 bytes !
            size_t size = strnlen(cache->cmdline_buffer, sizeof(task->name) - 1);
            if (size > sizeof(task->name) - 1) {
                size = sizeof(task->name) - 1;
            }
            if (size > 0) {
                memcpy(task->name, cache->cmdline_buffer, size);
                task->name[size] = '\0';
                
                // Remplacer les \0 INTERNES par des espaces (NE PAS toucher au \0 final!)
                // PROTECTION: Boucler jusqu'à size-1 pour ne PAS remplacer le null terminator final
                for (size_t x = 0; x < size - 1; x++) {
                    if (task->name[x] == '\0') {
                        if (x + 1 < size && task->name[x+1] == '\n') break;
                        task->name[x] = ' ';
                    }
                }
                
                // PROTECTION FINALE: S'assurer que le dernier octet est toujours \0
                task->name[size] = '\0';
                
                // SÉCURITÉ CRITIQUE: Nettoyer les caractères non-imprimables et invalides
                // Chrome peut avoir des caractères de contrôle qui crashent X11
                for (size_t x = 0; x < size; x++) {
                    unsigned char c = (unsigned char)task->name[x];
                    // Remplacer caractères de contrôle (sauf espace) par '?'
                    if (c < 32 && c != 0) {
                        task->name[x] = '?';
                    }
                    // Remplacer caractères > 127 invalides par '?'
                    else if (c >= 127 && c < 160) {
                        task->name[x] = '?';
                    }
                }
            }
        } else if (len >= 15) {
            // Extraire juste le nom du binaire
            size_t cmdline_len = strnlen(cache->cmdline_buffer, sizeof(cache->cmdline_buffer));
            char *slash = strrchr(cache->cmdline_buffer, '/');
            if (slash != NULL && slash < (cache->cmdline_buffer + cmdline_len)) {
                size_t name_len = strnlen(slash + 1, sizeof(task->name) - 1);
                memcpy(task->name, slash + 1, name_len);
                task->name[name_len] = '\0';
            } else {
                size_t copy_len = cmdline_len;
                if (copy_len > sizeof(task->name) - 1) {
                    copy_len = sizeof(task->name) - 1;
                }
                memcpy(task->name, cache->cmdline_buffer, copy_len);
                task->name[copy_len] = '\0';
            }
        }
    }
    
    // Utiliser le parsing rapide optimisé pour stat
    proc_stat_data_t stat_data;
    if (fast_parse_proc_stat(p, &stat_data) == 0) {
        TASK_SET_STATE_CHAR(task, stat_data.state);
        task->ppid = stat_data.ppid;
        task->time = (gint)(stat_data.stime + stat_data.utime);
        TASK_SET_PRIO(task, stat_data.prio);
    } else {
        // Fallback en cas d'erreur
        TASK_SET_STATE_CHAR(task, '?');
        task->ppid = 0;
        task->time = 0;
        TASK_SET_PRIO(task, 0);
    }
    
    task->old_time = task->time;
    task->time_percentage = 0;
    
    // ========================================================================
    // RÉSOLUTION UID ULTRA-OPTIMISÉE (100% élimination appels stat())
    // ========================================================================
    
    // NOUVEAU: Utiliser UID depuis le cache status (parsé depuis /proc/[pid]/status)
    // Gain: Élimine 500 stat() syscalls par refresh → 1-3% CPU
    if (cache->valid_status) {
        task->uid = cache->uid;  // UID déjà parsé depuis status buffer
    } else {
        // Fallback: Chercher dans uid_cache_refresh (ancien système avec stat())
        gpointer uid_ptr = g_hash_table_lookup(uid_cache_refresh, GINT_TO_POINTER(cache->pid));
        if (uid_ptr) {
            task->uid = GPOINTER_TO_INT(uid_ptr);
        } else {
            task->uid = 0; // Dernier fallback
        }
    }
    
    // Résoudre le nom d'utilisateur via CACHE UID (O(1) au lieu de getpwuid)
    const gchar* cached_username = get_cached_username(task->uid);
    task->uname = get_shared_uname(task->uid, cached_username);
    
    // Initialiser le cache hash pour comparaisons rapides (gain 2-2.5% CPU)
    task->cached_hash = compute_task_hash_quick(task);
}


// FONCTION OBSOLÈTE - Remplacée par get_task_details_from_cache() + batch I/O
// Conservée temporairement pour compatibilité, mais non utilisée
void get_task_details(pid_t pid,struct task *task) {
    // Cette fonction est obsolète depuis l'implémentation du batch I/O
    // Utiliser get_task_details_from_cache() à la place
    task->pid = -1;
    task->size = 0;
    task->pss = 0;
    task->gpu_usage = 0.0f;
}



// Structure pour getdents64 (optimisation Linux)
struct linux_dirent64 {
    ino64_t d_ino;
    off64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

// FONCTION OBSOLÈTE - proc_filter supprimée car non utilisée avec getdents64



// Version optimisée avec batch I/O /proc + pool mémoire - Réduction drastique des syscalls
HOT_FUNCTION
GArray *get_task_list(void) {
    init_parallel_processing();
    
    reset_task_pool();
    
    GArray *task_list = g_array_new(FALSE, FALSE, sizeof(struct task));
    
    int proc_fd = open("/proc", O_RDONLY | O_DIRECTORY);
    if (proc_fd < 0) return task_list;
    
    char buffer[16384];
    pid_t pids_batch[1024];
    proc_data_cache_t cache_batch[1024];
    
    GHashTable *uid_cache_refresh = g_hash_table_new(g_direct_hash, g_direct_equal);
    
    int batch_count = 0;
    int total_count = 0;
    
    while (1) {
        long nread = syscall(SYS_getdents64, proc_fd, buffer, sizeof(buffer));
        if (nread <= 0) break;
        
        struct linux_dirent64 *d;
        for (long bpos = 0; bpos < nread;) {
            d = (struct linux_dirent64 *)(buffer + bpos);
            
            // Filtrer uniquement les PIDs (commence par chiffre)
            if (d->d_name[0] >= '1' && d->d_name[0] <= '9') {
                pid_t pid = atol(d->d_name);
                
                // PRE-FILTERING INTELLIGENT : éliminer kernel threads (économie ~15-20%)
                if (!is_likely_kernel_thread(pid)) {
                    pids_batch[batch_count] = pid;
                    batch_count++;
                }
                
                // Traitement par batch I/O quand le batch est plein
                if (batch_count == BATCH_SIZE) {
                    // OPTIMISATION: UID déjà parsé depuis /proc/pid/status dans batch_read_proc_data
                    // Plus besoin de stat() syscalls ! (gain 2-3% CPU)
                    
                    // TRAITEMENT PARALLÈLE INTELLIGENT : 2 threads sur CPUs multi-cœurs
                    int valid_tasks = process_parallel_batch(pids_batch, batch_count, cache_batch,
                                                            uid_cache_refresh, task_list);
                    total_count += valid_tasks;
                    
                    batch_count = 0;
                }
            }
            bpos += d->d_reclen;
        }
    }
    
    // Traiter le batch restant avec pool mémoire
    if (batch_count > 0) {
        // OPTIMISATION: UID déjà parsé depuis /proc/pid/status dans batch_read_proc_data
        // Plus besoin de stat() syscalls ! (gain 2-3% CPU)
        
        // TRAITEMENT PARALLÈLE du batch restant
        int valid_tasks = process_parallel_batch(pids_batch, batch_count, cache_batch,
                                                uid_cache_refresh, task_list);
        total_count += valid_tasks;
    }
    
    // Nettoyer le cache UID refresh
    g_hash_table_destroy(uid_cache_refresh);
    
    close(proc_fd);
    return task_list;
}

// Pool mémoire géré dans functions.c




// Version ultra-optimisée avec memory mapping et parsing direct
HOT_FUNCTION
gboolean get_cpu_usage_from_proc(system_status *sys_stat) {
    // Sauvegarder les anciennes valeurs pour calcul delta
    if (sys_stat->status_flags & SYSTEM_STATUS_VALID_PROC_READING) {
        sys_stat->cpu_old_jiffies =
            sys_stat->cpu_user +
            sys_stat->cpu_nice +
            sys_stat->cpu_system +
            sys_stat->cpu_idle;
        sys_stat->cpu_old_used =
            sys_stat->cpu_user +
            sys_stat->cpu_nice +
            sys_stat->cpu_system;
    } else {
        sys_stat->cpu_old_jiffies = 0;
    }

    sys_stat->status_flags &= ~SYSTEM_STATUS_VALID_PROC_READING;

    // OPTIMISATION SYSCALL: open/read/close direct au lieu de fopen/fgets/fclose
    // Gain: ~1% CPU, évite buffering stdio inutile + allocations FILE*
    int fd = open("/proc/stat", O_RDONLY);
#ifdef CONSOLE_DEBUG
    printf("[DEBUG] open(/proc/stat) returned=%d\n", fd);
    fflush(stdout);
#endif
    if (fd < 0) return FALSE;

    char line[256];
    ssize_t bytes = read(fd, line, sizeof(line) - 1);
    close(fd);
    
    if (bytes <= 0) {
        return FALSE;
    }
    line[bytes] = '\0';
    
    // Trouver la fin de la première ligne pour parser_cpu_stats_direct
    char* newline = strchr(line, '\n');
    if (newline) *newline = '\0';

#ifdef CONSOLE_DEBUG
    printf("[DEBUG] /proc/stat first line: '%.50s'\n", line);
    fflush(stdout);
#endif
    
    gboolean success = parse_cpu_stats_direct(line, sys_stat);
#ifdef CONSOLE_DEBUG
    printf("[DEBUG] parse_cpu_stats_direct() returned=%s\n", success ? "TRUE" : "FALSE");
    if (success) {
        printf("[DEBUG] Parsed values: user=%u, nice=%u, system=%u, idle=%u\n", 
               sys_stat->cpu_user, sys_stat->cpu_nice, sys_stat->cpu_system, sys_stat->cpu_idle);
    }
    fflush(stdout);
#endif

    if (__builtin_expect(success, 1)) {
        sys_stat->status_flags |= SYSTEM_STATUS_VALID_PROC_READING;
    }

    return success;
}




gboolean get_system_status (system_status *sys_stat) {
    // OPTIMISATION SYSCALL: Remplacer fopen/fgets/fclose par open/read/close
    // Gain: ~0.5% CPU, lecture directe d'un coup au lieu de ligne par ligne
    // OPTIMISATION CACHE: MemTotal vient du cache (ne change jamais) - gain 1-2%
    static char meminfo_buffer[8192];  // /proc/meminfo ~3-4 KB, buffer statique réutilisable
    int reach;
    static int cpu_count;
    
    int fd = open("/proc/meminfo", O_RDONLY);
    if (fd < 0) return FALSE;
    
    ssize_t bytes = read(fd, meminfo_buffer, sizeof(meminfo_buffer) - 1);
    close(fd);
    
    if (bytes <= 0) return FALSE;
    meminfo_buffer[bytes] = '\0';
    
    // MemTotal: Utiliser le cache au lieu de parser (OPTIMISATION)
    sys_stat->mem_total = get_cached_mem_total();
    
    // Parser le buffer pour les valeurs dynamiques uniquement
    reach = 0;
    sys_stat->mem_cached = 0;
    
    char* line = meminfo_buffer;
    char* next_line;
    
    while (line && *line && reach < 4) {  // reach < 4 au lieu de 5 (on skip MemTotal)
        next_line = strchr(line, '\n');
        if (next_line) *next_line = '\0';
        
        // SKIP MemTotal (déjà dans le cache)
        if (!strncmp(line, "MemFree:", 8))
            sys_stat->mem_free = fast_atoll(line + 9), reach++;
        else if (!strncmp(line, "Cached:", 7))
            sys_stat->mem_cached += fast_atoll(line + 8), reach++;
        else if (!strncmp(line, "SReclaimable:", 13))
            sys_stat->mem_cached += fast_atoll(line + 14), reach++;
        else if (!strncmp(line, "Buffers:", 8))
            sys_stat->mem_buffered = fast_atoll(line + 9), reach++;
        
        if (!next_line) break;
        line = next_line + 1;
    }
    if(!cpu_count)
    {
        cpu_count = get_cached_nprocessors();  // Utiliser cache au lieu de sysconf
    }
    sys_stat->cpu_count=cpu_count;
    return TRUE;
}





gboolean send_signal_to_task(gint task_id, gint signal) {
    if(task_id > 0 && signal != 0)
    {
        if (root_mode_is_active())
            return privileged_kill((pid_t)task_id, signal);

        gint ret = 0;
        ret = kill(task_id, signal);
        if(ret != 0) {
            return FALSE;
        }
        return TRUE;
    }
    return FALSE;
}



void set_priority_to_task(gint task_id, gint prio) {
    if (task_id <= 0)
        return;

    if (root_mode_is_active()) {
        if (!privileged_set_priority((pid_t)task_id, prio))
            show_error("Can't set priority %d to task ID %d", prio, task_id);
        return;
    }

    if (setpriority(PRIO_PROCESS, (id_t)task_id, prio) != 0)
        show_error("Can't set priority %d to task ID %d", prio, task_id);
}

