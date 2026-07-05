#ifndef PSS_THREAD_H
#define PSS_THREAD_H

#include <glib.h>
#include <pthread.h>
#include "types.h"

// ============================================================================
// THREAD PSS ASYNCHRONE - Lecture PSS en arrière-plan pour UI fluide
// ============================================================================

// Structure pour stocker les résultats PSS
typedef struct {
    pid_t pid;
    guint64 pss;  // En KB
    gboolean valid;
} pss_result_t;

// Contexte du thread PSS
typedef struct {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    
    gboolean running;
    gboolean should_stop;
    gboolean has_new_results;
    
    // PIDs à traiter
    pid_t *pids;
    int pid_count;
    
    // Résultats
    GHashTable *results;  // pid_t → pss_result_t*
    
} pss_thread_context_t;

// Fonctions publiques
void init_pss_thread(void);
void start_pss_collection(pid_t *pids, int count);
gboolean get_pss_result(pid_t pid, guint64 *pss_out);
void cleanup_pss_thread(void);
gboolean is_pss_thread_busy(void);

#endif // PSS_THREAD_H
