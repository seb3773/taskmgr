
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <glib.h>
#include "pss_thread_simd.h"

#if defined(__x86_64__) || defined(__i386__)
#include <emmintrin.h>
#endif

static pss_thread_context_t pss_ctx = {0};
static gboolean pss_thread_initialized = FALSE;

// ============================================================================
// PARSER SIMD
// ============================================================================
#if (defined(__x86_64__) || defined(__i386__))
static long find_and_parse_pss_simd(const char* buffer, size_t length) {
    const char* buf_end = buffer + length;
    const __m128i first_chars = _mm_set1_epi8('P');
    
    // Traiter par blocs de 16 bytes SEULEMENT si length >= 16
    size_t simd_limit = (length >= 16) ? (length - 16) : 0;
    for (size_t i = 0; i <= simd_limit; i += 16) {
        const __m128i block = _mm_loadu_si128((const __m128i*)(buffer + i));
        const __m128i cmp = _mm_cmpeq_epi8(block, first_chars);
        int mask = _mm_movemask_epi8(cmp);
        if (mask != 0) {
            for (int j = 0; j < 16; j++) {
                if ((mask >> j) & 1) {
                    const char* p = buffer + i + j;
                    if (p + 4 < buf_end && p[1] == 's' && p[2] == 's' && p[3] == ':') {
                        p += 4;
                        while (p < buf_end && (*p == ' ' || *p == '\t')) p++;
                        long pss_kb = 0;
                        while (p < buf_end && (*p >= '0' && *p <= '9')) {
                            pss_kb = pss_kb * 10 + (*p - '0');
                            p++;
                        }
                        return pss_kb;
                    }
                }
            }
        }
    }
    for (size_t i = length - (length % 16); i < length; ++i) {
        if (buffer[i] == 'P' && i + 4 < length && memcmp(buffer + i, "Pss:", 4) == 0) {
             const char* p = buffer + i + 4;
             while (p < buf_end && (*p == ' ' || *p == '\t')) p++;
             long pss_kb = 0;
             while (p < buf_end && (*p >= '0' && *p <= '9')) {
                pss_kb = pss_kb * 10 + (*p - '0');
                p++;
             }
             return pss_kb;
        }
    }
    return -1;
}
#else
static long find_and_parse_pss_simd(const char* buffer, size_t length) {
    const char* found = memmem(buffer, length, "Pss:", 4);
    if (found) {
        const char* p = found + 4;
        while (p < buffer + length && (*p == ' ' || *p == '\t')) p++;
        long pss_kb = 0;
        while (p < buffer + length && (*p >= '0' && *p <= '9')) {
            pss_kb = pss_kb * 10 + (*p - '0');
            p++;
        }
        return pss_kb;
    }
    return -1;
}
#endif
static void* pss_thread_worker(void* arg) {
    pss_thread_context_t *ctx = (pss_thread_context_t*)arg;
    char* read_buffer = g_malloc(4096);

    while (1) {
        pthread_mutex_lock(&ctx->mutex);
        while (!ctx->should_stop && ctx->pid_count == 0) {
            pthread_cond_wait(&ctx->cond, &ctx->mutex);
        }

        if (ctx->should_stop) {
            pthread_mutex_unlock(&ctx->mutex);
            break;
        }

        int count = ctx->pid_count;
        pid_t *pids_copy = g_malloc(count * sizeof(pid_t));
        memcpy(pids_copy, ctx->pids, count * sizeof(pid_t));
        
        pthread_mutex_unlock(&ctx->mutex);

        for (int i = 0; i < count; i++) {
            if (ctx->should_stop) break; // Vérifier avant chaque traitement

            pid_t pid = pids_copy[i];
            char path[64];
            snprintf(path, sizeof(path), "/proc/%d/smaps_rollup", pid);

            int fd = open(path, O_RDONLY);
            if (fd < 0) continue;

            ssize_t bytes = read(fd, read_buffer, 4095);
            close(fd);

            if (bytes > 0) {
                long pss_kb = find_and_parse_pss_simd(read_buffer, bytes);
                if (pss_kb != -1) {
                    pthread_mutex_lock(&ctx->mutex);
                    
                    // Vérifier si une entrée existe déjà pour ce PID
                    pss_result_t *result = g_hash_table_lookup(ctx->results, GINT_TO_POINTER(pid));
                    
                    if (result) {
                        // Mettre à jour l'entrée existante (pas de nouvelle allocation)
                        result->pss = pss_kb;
                        result->valid = TRUE;
                    } else {
                        // Créer nouvelle entrée
                        result = g_malloc(sizeof(pss_result_t));
                        result->pid = pid;
                        result->pss = pss_kb;
                        result->valid = TRUE;
                        g_hash_table_insert(ctx->results, GINT_TO_POINTER(pid), result);
                    }
                    
                    pthread_mutex_unlock(&ctx->mutex);
                }
            }

            if ((i + 1) % 10 == 0) {
                sched_yield();
            }
        }

        g_free(pids_copy);

        pthread_mutex_lock(&ctx->mutex);
        ctx->pid_count = 0;
        ctx->has_new_results = TRUE;
        pthread_mutex_unlock(&ctx->mutex);
    }

    g_free(read_buffer);
    return NULL;
}

// ============================================================================
// API PUBLIQUE (REPRODUCTION EXACTE DE L'ORIGINAL)
// ============================================================================

void init_pss_thread(void) {
    if (pss_thread_initialized) return;
    memset(&pss_ctx, 0, sizeof(pss_ctx));
    pthread_mutex_init(&pss_ctx.mutex, NULL);
    pthread_cond_init(&pss_ctx.cond, NULL);
    pss_ctx.results = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    pss_ctx.running = FALSE;
    pss_ctx.should_stop = FALSE;
    pss_ctx.has_new_results = FALSE;
    if (pthread_create(&pss_ctx.thread, NULL, pss_thread_worker, &pss_ctx) == 0) {
        pss_ctx.running = TRUE;
        pss_thread_initialized = TRUE;
    }
}

void start_pss_collection(pid_t *pids, int count) {
    if (!pss_thread_initialized || !pss_ctx.running) return;
    pthread_mutex_lock(&pss_ctx.mutex);
    if (pss_ctx.pid_count > 0) {
        pthread_mutex_unlock(&pss_ctx.mutex);
        return;
    }
    // NE PAS vider la table ici - les anciennes entrées seront écrasées par les nouvelles
    // g_hash_table_remove_all() peut causer race condition avec le worker thread
    pss_ctx.has_new_results = FALSE;
    if (pss_ctx.pids) g_free(pss_ctx.pids);
    pss_ctx.pids = g_malloc(count * sizeof(pid_t));
    memcpy(pss_ctx.pids, pids, count * sizeof(pid_t));
    pss_ctx.pid_count = count;
    pthread_cond_signal(&pss_ctx.cond);
    pthread_mutex_unlock(&pss_ctx.mutex);
}

gboolean get_pss_result(pid_t pid, guint64 *pss_out) {
    if (!pss_thread_initialized) return FALSE;
    pthread_mutex_lock(&pss_ctx.mutex);
    pss_result_t *result = g_hash_table_lookup(pss_ctx.results, GINT_TO_POINTER(pid));
    gboolean found = FALSE;
    if (result && result->valid) {
        *pss_out = result->pss;
        found = TRUE;
    }
    pthread_mutex_unlock(&pss_ctx.mutex);
    return found;
}

gboolean is_pss_thread_busy(void) {
    if (!pss_thread_initialized) return FALSE;
    pthread_mutex_lock(&pss_ctx.mutex);
    gboolean busy = (pss_ctx.pid_count > 0);
    pthread_mutex_unlock(&pss_ctx.mutex);
    return busy;
}

void cleanup_pss_thread(void) {
    if (!pss_thread_initialized) return;
    pthread_mutex_lock(&pss_ctx.mutex);
    pss_ctx.should_stop = TRUE;
    pthread_cond_signal(&pss_ctx.cond);
    pthread_mutex_unlock(&pss_ctx.mutex);
    pthread_join(pss_ctx.thread, NULL);
    pthread_mutex_destroy(&pss_ctx.mutex);
    pthread_cond_destroy(&pss_ctx.cond);
    if (pss_ctx.pids) g_free(pss_ctx.pids);
    g_hash_table_destroy(pss_ctx.results);
    pss_thread_initialized = FALSE;
}