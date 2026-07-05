#ifndef MANAGER_COMMON_H
#define MANAGER_COMMON_H

#include "common.h"

// Interface commune pour tous les managers
typedef struct {
    const char *name;
    int (*init)(void);
    void (*cleanup)(void);
    void (*update)(void);
    int (*get_count)(void);
    void *(*get_info)(int index);
} manager_interface_t;

// Types d'erreurs communes
typedef enum {
    MANAGER_OK = 0,
    MANAGER_ERROR_INIT = -1,
    MANAGER_ERROR_MEMORY = -2,
    MANAGER_ERROR_IO = -3,
    MANAGER_ERROR_INVALID_PARAM = -4
} manager_error_t;

// Macros pour la gestion d'erreurs
#define MANAGER_CHECK_NULL(ptr) \
    do { if (!(ptr)) return MANAGER_ERROR_INVALID_PARAM; } while(0)

#define MANAGER_CHECK_ALLOC(ptr) \
    do { if (!(ptr)) return MANAGER_ERROR_MEMORY; } while(0)

#define MANAGER_SAFE_FREE(ptr) \
    do { if (ptr) { free(ptr); (ptr) = NULL; } } while(0)

// Pool d'allocation pour réduire les malloc/free
typedef struct {
    void *memory;
    size_t size;
    size_t used;
    size_t block_size;
} memory_pool_t;

// Fonctions utilitaires communes
OPTIMIZE_SIZE char* manager_strip_whitespace(char* str);
OPTIMIZE_SIZE char* manager_trim_newline(char* str);
OPTIMIZE_SIZE int manager_has_suffix(const char *str, const char *suffix);
OPTIMIZE_SIZE char* manager_expand_home(const char* str, const char* home);

// Fonctions de lecture système directes (sans cache)
OPTIMIZE_SIZE long manager_read_sys_file_long(const char *path);
OPTIMIZE_SIZE int manager_read_sys_file_int(const char *path);
OPTIMIZE_SIZE gboolean manager_read_sys_file_line(const char *path, char *buffer, size_t size);

// Pool de mémoire
OPTIMIZE_SIZE memory_pool_t* manager_create_pool(size_t total_size, size_t block_size);
OPTIMIZE_SIZE void* manager_pool_alloc(memory_pool_t *pool);
OPTIMIZE_SIZE void manager_pool_reset(memory_pool_t *pool);
OPTIMIZE_SIZE void manager_destroy_pool(memory_pool_t *pool);

// Buffer circulaire optimisé
#define CIRCULAR_BUFFER_NEXT(index, size) (((index) + 1) % (size))
#define CIRCULAR_BUFFER_PREV(index, size) (((index) + (size) - 1) % (size))

// Macros inline pour les opérations communes sur buffers
#define UPDATE_CIRCULAR_BUFFER(buffer, index, value, size) \
    do { \
        (buffer)[(index)] = (value); \
        (index) = CIRCULAR_BUFFER_NEXT((index), (size)); \
    } while(0)

// Structure commune pour les managers avec buffers circulaires
typedef struct {
    int current_index;
    gboolean buffer_full;
    int selected_index;
    int count;
} manager_base_t;

// Initialisation commune des managers
#define INIT_MANAGER_BASE(mgr) \
    do { \
        (mgr)->current_index = 0; \
        (mgr)->buffer_full = FALSE; \
        (mgr)->selected_index = -1; \
        (mgr)->count = 0; \
    } while(0)

#endif // MANAGER_COMMON_H
