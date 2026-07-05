#include "manager_registry.h"
#include "types.h"

static manager_registry_t registry = {0};

OPTIMIZE_SIZE
int manager_registry_init(void) {
    if (registry.flags & MANAGER_INITIALIZED) return MANAGER_OK;
    
    registry.capacity = 8;
    registry.managers = malloc(sizeof(manager_interface_t*) * registry.capacity);
    MANAGER_CHECK_ALLOC(registry.managers);
    
    registry.count = 0;
    registry.flags |= MANAGER_INITIALIZED;
    
    // Pas de cache système - lectures directes
    
    return MANAGER_OK;
}

OPTIMIZE_SIZE
void manager_registry_cleanup(void) {
    if (!(registry.flags & MANAGER_INITIALIZED)) return;
    
    // Cleanup tous les managers
    for (int i = 0; i < registry.count; i++) {
        if (registry.managers[i] && registry.managers[i]->cleanup) {
            registry.managers[i]->cleanup();
        }
    }
    
    MANAGER_SAFE_FREE(registry.managers);
    registry.count = 0;
    registry.capacity = 0;
    registry.flags &= ~MANAGER_INITIALIZED;
    
    // Pas de cleanup de cache - lectures directes
}

OPTIMIZE_SIZE
int manager_registry_register(manager_interface_t *manager) {
    MANAGER_CHECK_NULL(manager);
    if (!(registry.flags & MANAGER_INITIALIZED)) return MANAGER_ERROR_INIT;
    
    // Vérifier si on a besoin d'agrandir le tableau
    if (registry.count >= registry.capacity) {
        registry.capacity *= 2;
        registry.managers = realloc(registry.managers, 
                                   sizeof(manager_interface_t*) * registry.capacity);
        MANAGER_CHECK_ALLOC(registry.managers);
    }
    
    // Initialiser le manager
    if (manager->init && manager->init() != MANAGER_OK) {
        return MANAGER_ERROR_INIT;
    }
    
    registry.managers[registry.count++] = manager;
    return MANAGER_OK;
}

OPTIMIZE_SIZE
void manager_registry_update_all(void) {
    if (!(registry.flags & MANAGER_INITIALIZED)) return;
    
    for (int i = 0; i < registry.count; i++) {
        if (registry.managers[i] && registry.managers[i]->update) {
            registry.managers[i]->update();
        }
    }
}

OPTIMIZE_SIZE
manager_interface_t* manager_registry_get(const char *name) {
    if (!(registry.flags & MANAGER_INITIALIZED) || !name) return NULL;
    
    for (int i = 0; i < registry.count; i++) {
        if (registry.managers[i] && 
            strcmp(registry.managers[i]->name, name) == 0) {
            return registry.managers[i];
        }
    }
    
    return NULL;
}
