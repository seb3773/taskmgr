#ifndef MANAGER_REGISTRY_H
#define MANAGER_REGISTRY_H

#include "manager_common.h"

// Registry pour tous les managers
typedef struct {
    manager_interface_t **managers;
    int count;
    int capacity;
    guint8 flags;  // MANAGER_INITIALIZED
} manager_registry_t;

// Fonctions du registry
OPTIMIZE_SIZE int manager_registry_init(void);
OPTIMIZE_SIZE void manager_registry_cleanup(void);
OPTIMIZE_SIZE int manager_registry_register(manager_interface_t *manager);
OPTIMIZE_SIZE void manager_registry_update_all(void);
OPTIMIZE_SIZE manager_interface_t* manager_registry_get(const char *name);

// Déclarations des interfaces des managers
extern manager_interface_t autostart_manager_interface;
extern manager_interface_t disk_manager_interface;
extern manager_interface_t network_manager_interface;
extern manager_interface_t editor_manager_interface;

#endif // MANAGER_REGISTRY_H
