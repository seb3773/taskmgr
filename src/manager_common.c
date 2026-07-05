#include "manager_common.h"
#include <ctype.h>
#include <time.h>

// Pas de cache - lectures directes uniquement

// Fonctions utilitaires communes
OPTIMIZE_SIZE
char* manager_strip_whitespace(char* str) {
    char *end;
    
    // Trim leading space
    while(isspace((unsigned char)*str)) str++;
    
    if(*str == 0) return str;
    
    // Trim trailing space
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    
    end[1] = '\0';
    return str;
}

OPTIMIZE_SIZE
char* manager_trim_newline(char* str) {
    char *end = str + strlen(str) - 1;
    while (end > str && (*end == '\n' || *end == '\r')) *end-- = '\0';
    return str;
}

OPTIMIZE_SIZE
int manager_has_suffix(const char *str, const char *suffix) {
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    return lenstr >= lensuffix && strcmp(str + lenstr - lensuffix, suffix) == 0;
}

OPTIMIZE_SIZE
char* manager_expand_home(const char* str, const char* home) {
    if (!str) return NULL;
    if (strncmp(str, "$HOME", 5) == 0) {
        size_t len = strlen(home) + strlen(str) - 5 + 1;
        char *res = malloc(len);
        if (!res) return NULL;
        snprintf(res, len, "%s%s", home, str + 5);
        return res;
    } else if (str[0] == '~') {
        size_t len = strlen(home) + strlen(str) + 1;
        char *res = malloc(len);
        if (!res) return NULL;
        snprintf(res, len, "%s%s", home, str + 1);
        return res;
    }
    return strdup(str);
}

// Fonctions de cache supprimées - lectures directes uniquement

// Fonctions de lecture système directes (sans cache)
OPTIMIZE_SIZE
long manager_read_sys_file_long(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    
    char buffer[64];
    if (fgets(buffer, sizeof(buffer), fp)) {
        manager_trim_newline(buffer);
        fclose(fp);
        return strtol(buffer, NULL, 10);
    }
    
    fclose(fp);
    return -1;
}

OPTIMIZE_SIZE
int manager_read_sys_file_int(const char *path) {
    long result = manager_read_sys_file_long(path);
    return (result == -1) ? -1 : (int)result;
}

OPTIMIZE_SIZE
gboolean manager_read_sys_file_line(const char *path, char *buffer, size_t size) {
    FILE *fp = fopen(path, "r");
    if (!fp) return FALSE;
    
    if (fgets(buffer, size, fp)) {
        manager_trim_newline(buffer);
        manager_strip_whitespace(buffer);
        fclose(fp);
        return TRUE;
    }
    
    fclose(fp);
    return FALSE;
}

// Pool de mémoire
OPTIMIZE_SIZE
memory_pool_t* manager_create_pool(size_t total_size, size_t block_size) {
    memory_pool_t *pool = malloc(sizeof(memory_pool_t));
    if (!pool) return NULL;
    
    pool->memory = malloc(total_size);
    if (!pool->memory) {
        free(pool);
        return NULL;
    }
    
    pool->size = total_size;
    pool->used = 0;
    pool->block_size = block_size;
    
    return pool;
}

OPTIMIZE_SIZE
void* manager_pool_alloc(memory_pool_t *pool) {
    if (!pool || pool->used + pool->block_size > pool->size) {
        return NULL;
    }
    
    void *ptr = (char*)pool->memory + pool->used;
    pool->used += pool->block_size;
    return ptr;
}

OPTIMIZE_SIZE
void manager_pool_reset(memory_pool_t *pool) {
    if (pool) {
        pool->used = 0;
    }
}

OPTIMIZE_SIZE
void manager_destroy_pool(memory_pool_t *pool) {
    if (pool) {
        MANAGER_SAFE_FREE(pool->memory);
        free(pool);
    }
}
