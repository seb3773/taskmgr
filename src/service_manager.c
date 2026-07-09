#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <systemd/sd-bus.h>
#include "service_manager.h"
#include "manager_common.h"
#include "types.h"
#include "functions.h"
#include "fast_format.h"
#include "root_credential_vault.h"
#include "privileged_exec.h"

// Pool de mémoire global pour les services
static memory_pool_t *services_memory_pool = NULL;
// pool_initialized remplacé par IS_SERVICES_POOL_INITIALIZED() macro

// Fonction interne réutilisable pour les appels D-Bus systemd
static sd_bus* get_systemd_bus(void) {
    sd_bus *bus = NULL;
    int r = sd_bus_open_system(&bus);
    return (r >= 0) ? bus : NULL;
}

// Fonction interne pour obtenir le chemin d'objet d'un service
static int get_service_object_path(sd_bus *bus, const char *service_name, char *object_path, size_t path_len) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r;
    const char *path;
    
    r = sd_bus_call_method(bus,
                          "org.freedesktop.systemd1",
                          "/org/freedesktop/systemd1",
                          "org.freedesktop.systemd1.Manager",
                          "GetUnit",
                          &error,
                          &reply,
                          "s",
                          service_name);
    
    if (r >= 0) {
        r = sd_bus_message_read(reply, "o", &path);
        if (r >= 0) {
            strncpy(object_path, path, path_len - 1);
            object_path[path_len - 1] = '\0';
        }
    }
    
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    return r;
}

// Fonction interne pour obtenir une propriété string
static int get_service_property_string(sd_bus *bus, const char *object_path, 
                                     const char *interface, const char *property, 
                                     char *result, size_t result_len) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r;
    const char *value;
    
    r = sd_bus_call_method(bus,
                          "org.freedesktop.systemd1",
                          object_path,
                          "org.freedesktop.DBus.Properties",
                          "Get",
                          &error,
                          &reply,
                          "ss",
                          interface,
                          property);
    
    if (r >= 0) {
        r = sd_bus_message_read(reply, "v", "s", &value);
        if (r >= 0 && value) {
            strncpy(result, value, result_len - 1);
            result[result_len - 1] = '\0';
        } else {
            strcpy(result, "N/A");
        }
    } else {
        strcpy(result, "N/A");
    }
    
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    return r;
}

// Fonction interne pour obtenir le slice depuis le ControlGroup
static int get_service_slice(sd_bus *bus, const char *object_path, char *result, size_t result_len) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r;
    const char *cgroup;
    
    // Obtenir le ControlGroup (chemin cgroup)
    r = sd_bus_call_method(bus,
                          "org.freedesktop.systemd1",
                          object_path,
                          "org.freedesktop.DBus.Properties",
                          "Get",
                          &error,
                          &reply,
                          "ss",
                          "org.freedesktop.systemd1.Service",
                          "ControlGroup");
    
    if (r < 0) {
        // Essayer avec l'interface Unit si Service échoue
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        reply = NULL;
        
        r = sd_bus_call_method(bus,
                              "org.freedesktop.systemd1",
                              object_path,
                              "org.freedesktop.DBus.Properties",
                              "Get",
                              &error,
                              &reply,
                              "ss",
                              "org.freedesktop.systemd1.Unit",
                              "ControlGroup");
    }
    
    if (r < 0) {
        strcpy(result, "-");
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        return r;
    }
    
    // Lire le ControlGroup depuis le variant
    r = sd_bus_message_read(reply, "v", "s", &cgroup);
    if (r >= 0 && cgroup && strlen(cgroup) > 0) {
        // Format: /system.slice/nginx.service
        // ou:     /user.slice/user-1000.slice/session-2.scope
        // Extraire le slice (première partie après /)
        
        if (cgroup[0] == '/') {
            const char *first_slash = cgroup + 1; // Skip le premier /
            const char *second_slash = strchr(first_slash, '/');
            
            if (second_slash) {
                // Extraire entre premier et second /
                size_t len = second_slash - first_slash;
                if (len < result_len) {
                    strncpy(result, first_slash, len);
                    result[len] = '\0';
                } else {
                    strcpy(result, "-");
                }
            } else {
                // Pas de second slash, prendre tout après le premier /
                strncpy(result, first_slash, result_len - 1);
                result[result_len - 1] = '\0';
            }
            
            // Enlever le ".slice" à la fin si présent
            size_t result_length = strlen(result);
            if (result_length > 6 && strcmp(result + result_length - 6, ".slice") == 0) {
                result[result_length - 6] = '\0';
            }
        } else {
            strcpy(result, "-");
        }
    } else {
        strcpy(result, "-");
    }
    
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    return 0;
}

// Fonction interne pour obtenir une propriété uint32
static uint32_t get_service_property_uint32(sd_bus *bus, const char *object_path, 
                                           const char *interface, const char *property) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r;
    uint32_t value = 0;
    
    r = sd_bus_call_method(bus,
                          "org.freedesktop.systemd1",
                          object_path,
                          "org.freedesktop.DBus.Properties",
                          "Get",
                          &error,
                          &reply,
                          "ss",
                          interface,
                          property);
    
    if (r >= 0) {
        sd_bus_message_read(reply, "v", "u", &value);
    }
    
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    return value;
}

// Fonction interne pour obtenir une propriété uint64
static uint64_t get_service_property_uint64(sd_bus *bus, const char *object_path, 
                                           const char *interface, const char *property) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r;
    uint64_t value = 0;
    
    r = sd_bus_call_method(bus,
                          "org.freedesktop.systemd1",
                          object_path,
                          "org.freedesktop.DBus.Properties",
                          "Get",
                          &error,
                          &reply,
                          "ss",
                          interface,
                          property);
    
    if (r >= 0) {
        sd_bus_message_read(reply, "v", "t", &value);
    }
    
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    return value;
}

// Fonction interne pour obtenir l'usage mémoire d'un service
static void get_service_memory_usage(sd_bus *bus, const char *object_path, char *memory_result) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r;
    uint64_t memory_current = 0;
    
    r = sd_bus_call_method(bus,
                          "org.freedesktop.systemd1",
                          object_path,
                          "org.freedesktop.DBus.Properties",
                          "Get",
                          &error,
                          &reply,
                          "ss",
                          "org.freedesktop.systemd1.Service",
                          "MemoryCurrent");
    
    if (r >= 0) {
        r = sd_bus_message_read(reply, "v", "t", &memory_current);
        if (r >= 0 && memory_current > 0 && memory_current != UINT64_MAX && memory_current < (1ULL << 62)) {
            // Valeur valide : formatage optimisé (20-40x plus rapide que snprintf)
            format_memory_size(memory_result, memory_current);
        } else {
            // Valeur invalide, non disponible ou service sans processus principal
            strcpy(memory_result, "");  // Chaîne vide au lieu de "-"
        }
    } else {
        // Erreur D-Bus ou propriété non disponible
        strcpy(memory_result, "");  // Chaîne vide au lieu de "-"
    }
    
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
}

// Initialiser le pool de mémoire pour les services
static void init_services_memory_pool(void) {
    if (!IS_SERVICES_POOL_INITIALIZED()) {
        // Pool pour 3 allocations complètes (rotation pour éviter les conflits)
        size_t pool_size = sizeof(SystemdService) * MAX_SERVICES * 3;
        services_memory_pool = manager_create_pool(pool_size, sizeof(SystemdService) * MAX_SERVICES);
        set_optimization_flag(POOL_FLAG_SERVICES_INITIALIZED, TRUE);
    }
}

// Réinitialiser le pool de mémoire (pour réutiliser la mémoire)
void reset_services_memory_pool(void) {
    if (services_memory_pool) {
        manager_pool_reset(services_memory_pool);
    }
}

// Nettoyer le pool de mémoire
void cleanup_services_memory_pool(void) {
    if (services_memory_pool) {
        manager_destroy_pool(services_memory_pool);
        services_memory_pool = NULL;
        set_optimization_flag(POOL_FLAG_SERVICES_INITIALIZED, FALSE);
    }
}

// Fonction pour créer une nouvelle liste de services
SystemdServiceList* systemd_service_list_new(void) {
    SystemdServiceList *list = malloc(sizeof(SystemdServiceList));
    if (!list) return NULL;
    
    // Initialiser le pool si nécessaire
    init_services_memory_pool();
    
    // Essayer d'allouer depuis le pool
    if (services_memory_pool) {
        list->services = (SystemdService*)manager_pool_alloc(services_memory_pool);
        if (list->services) {
            // Marquer que cette allocation vient du pool
            list->count = 0;
            list->capacity = MAX_SERVICES;
            return list;
        }
    }
    
    // Fallback vers malloc classique si le pool est plein
    list->services = malloc(sizeof(SystemdService) * MAX_SERVICES);
    if (!list->services) {
        MANAGER_SAFE_FREE(list);
        return NULL;
    }
    
    list->count = 0;
    list->capacity = MAX_SERVICES;
    return list;
}

// Fonction pour libérer une liste de services
void systemd_service_list_free(SystemdServiceList *list) {
    if (list) {
        // Ne pas libérer list->services si elle vient du pool
        // Le pool gère sa propre mémoire et sera réinitialisé lors du prochain cycle
        if (list->services) {
            // Vérifier si l'adresse est dans le pool
            if (services_memory_pool && 
                list->services >= (SystemdService*)services_memory_pool->memory &&
                list->services < (SystemdService*)((char*)services_memory_pool->memory + services_memory_pool->size)) {
                // Cette allocation vient du pool - ne pas la libérer
                list->services = NULL;
            } else {
                // Allocation classique - libérer normalement
                MANAGER_SAFE_FREE(list->services);
            }
        }
        MANAGER_SAFE_FREE(list);
    }
}

/**
 * Récupère la liste de tous les services systemd
 * 
 * @return SystemdServiceList* - Liste des services (NULL en cas d'erreur)
 */
SystemdServiceList* get_systemd_services(void) {
    sd_bus *bus = get_systemd_bus();
    if (!bus) return NULL;
    
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r;
    
    SystemdServiceList *service_list = systemd_service_list_new();
    if (!service_list) {
        sd_bus_unref(bus);
        return NULL;
    }
    
    // Obtenir la liste des unités
    r = sd_bus_call_method(bus,
                          "org.freedesktop.systemd1",
                          "/org/freedesktop/systemd1",
                          "org.freedesktop.systemd1.Manager",
                          "ListUnits",
                          &error,
                          &reply,
                          NULL);
    
    if (r < 0) {
        sd_bus_error_free(&error);
        sd_bus_unref(bus);
        systemd_service_list_free(service_list);
        return NULL;
    }
    
    // Lire le tableau des unités
    r = sd_bus_message_enter_container(reply, 'a', "(ssssssouso)");
    if (r < 0) {
        sd_bus_message_unref(reply);
        sd_bus_unref(bus);
        systemd_service_list_free(service_list);
        return NULL;
    }
    
    // Itérer sur chaque unité
    while (1) {
        const char *name, *description_basic, *load_state, *active_state, *sub_state, *following;
        const char *object_path, *job_type, *job_object_path;
        uint32_t job_id;
        
        r = sd_bus_message_read(reply, "(ssssssouso)", 
                               &name, &description_basic, &load_state, 
                               &active_state, &sub_state, &following,
                               &object_path, &job_id, &job_type, &job_object_path);
        
        if (r <= 0) break;
        
        // Ne traiter que les services
        if (strstr(name, ".service") != NULL && service_list->count < service_list->capacity) {
            SystemdService *service = &service_list->services[service_list->count];
            
            strncpy(service->name, name, SERVICE_NAME_LEN - 1);
            service->name[SERVICE_NAME_LEN - 1] = '\0';
            
            if (description_basic && strlen(description_basic) > 0) {
                strncpy(service->description, description_basic, SERVICE_DESC_LEN - 1);
                service->description[SERVICE_DESC_LEN - 1] = '\0';
            } else {
                strcpy(service->description, "N/A");
            }
            
            // Récupérer le Slice
            get_service_slice(bus, object_path, service->slice, sizeof(service->slice));
            
            // Status, PID et usage mémoire
            if (strcmp(active_state, "active") == 0) {
                strcpy(service->status, "running");
                uint32_t main_pid = get_service_property_uint32(bus, object_path, 
                                                               "org.freedesktop.systemd1.Service", "MainPID");
                if (main_pid > 0) {
                    format_uint32(service->pid, main_pid);
                } else {
                    strcpy(service->pid, "");  // Chaîne vide au lieu de "-"
                }
                // Obtenir l'usage mémoire pour les services actifs
                get_service_memory_usage(bus, object_path, service->memory_usage);
            } else {
                if (strcmp(active_state, "inactive") == 0) strcpy(service->status, "stopped");
                else if (strcmp(active_state, "failed") == 0) strcpy(service->status, "failed");
                else if (strcmp(active_state, "activating") == 0) strcpy(service->status, "starting");
                else if (strcmp(active_state, "deactivating") == 0) strcpy(service->status, "stopping");
                else strncpy(service->status, active_state, SERVICE_STATUS_LEN - 1);
                strcpy(service->pid, "");  // Chaîne vide au lieu de "-"
                strcpy(service->memory_usage, "");  // Chaîne vide au lieu de "-"
            }
            
            service_list->count++;
        }
    }
    
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    sd_bus_unref(bus);
    
    return service_list;
}

/**
 * Récupère tous les détails d'un service systemd
 * 
 * @param service_name - Nom du service
 * @param details - Structure pour stocker les détails (allouée par l'appelant)
 * @return int - 0 en cas de succès, -1 en cas d'erreur
 */
int get_systemd_service_details(const char *service_name, SystemdServiceDetails *details) {
    if (!service_name || !details) return -1;
    
    sd_bus *bus = get_systemd_bus();
    if (!bus) return -1;
    
    char object_path[512];
    if (get_service_object_path(bus, service_name, object_path, sizeof(object_path)) < 0) {
        sd_bus_unref(bus);
        return -1;
    }
    
    // Initialiser la structure
    memset(details, 0, sizeof(SystemdServiceDetails));
    strncpy(details->name, service_name, SERVICE_NAME_LEN - 1);
    
    // Récupérer les propriétés de l'unité
    get_service_property_string(bus, object_path, "org.freedesktop.systemd1.Unit", 
                                "Description", details->description, SERVICE_DESC_LEN);
    get_service_property_string(bus, object_path, "org.freedesktop.systemd1.Unit", 
                                "ActiveState", details->active_state, sizeof(details->active_state));
    get_service_property_string(bus, object_path, "org.freedesktop.systemd1.Unit", 
                                "SubState", details->sub_state, sizeof(details->sub_state));
    get_service_property_string(bus, object_path, "org.freedesktop.systemd1.Unit", 
                                "LoadState", details->load_state, sizeof(details->load_state));
    get_service_property_string(bus, object_path, "org.freedesktop.systemd1.Unit", 
                                "UnitFileState", details->unit_file_state, sizeof(details->unit_file_state));
    
    // PIDs
    uint32_t main_pid = get_service_property_uint32(bus, object_path, "org.freedesktop.systemd1.Service", "MainPID");
    uint32_t control_pid = get_service_property_uint32(bus, object_path, "org.freedesktop.systemd1.Service", "ControlPID");
    
    if (main_pid > 0) format_uint32(details->main_pid, main_pid);
    else strcpy(details->main_pid, "-");
    
    if (control_pid > 0) format_uint32(details->control_pid, control_pid);
    else strcpy(details->control_pid, "-");
    
    // Propriétés du service
    get_service_property_string(bus, object_path, "org.freedesktop.systemd1.Service", 
                                "Type", details->service_type, sizeof(details->service_type));
    get_service_property_string(bus, object_path, "org.freedesktop.systemd1.Service", 
                                "Restart", details->restart, sizeof(details->restart));
    
    // Récupérer l'usage mémoire
    get_service_memory_usage(bus, object_path, details->memory_usage);
    
    // Récupérer ExecStart (commande d'exécution)
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r;
    
    r = sd_bus_call_method(bus,
                          "org.freedesktop.systemd1",
                          object_path,
                          "org.freedesktop.DBus.Properties",
                          "Get",
                          &error,
                          &reply,
                          "ss",
                          "org.freedesktop.systemd1.Service",
                          "ExecStart");
    
    if (r >= 0) {
        // ExecStart est un array de structures (path, argv, ignore, start_time, exit_time, pid, code, status)
        const char *signature;
        if (sd_bus_message_peek_type(reply, NULL, &signature) >= 0) {
            // Entrer dans le variant
            r = sd_bus_message_enter_container(reply, 'v', NULL);
            if (r >= 0) {
                // Entrer dans l'array
                r = sd_bus_message_enter_container(reply, 'a', NULL);
                if (r >= 0) {
                    // Lire la première entrée (structure principale)
                    r = sd_bus_message_enter_container(reply, 'r', NULL);
                    if (r >= 0) {
                        const char *path = NULL;
                        // Lire le chemin de l'exécutable
                        r = sd_bus_message_read(reply, "s", &path);
                        if (r >= 0 && path) {
                            // Entrer dans l'array des arguments
                            r = sd_bus_message_enter_container(reply, 'a', "s");
                            if (r >= 0) {
                                char command_line[512] = "";
                                const char *arg;
                                int first = 1;
                                
                                // Construire la ligne de commande complète
                                while (sd_bus_message_read(reply, "s", &arg) > 0) {
                                    if (!first) {
                                        strncat(command_line, " ", sizeof(command_line) - strlen(command_line) - 1);
                                    }
                                    strncat(command_line, arg, sizeof(command_line) - strlen(command_line) - 1);
                                    first = 0;
                                }
                                
                                if (strlen(command_line) > 0) {
                                    strncpy(details->exec_start, command_line, sizeof(details->exec_start) - 1);
                                    details->exec_start[sizeof(details->exec_start) - 1] = '\0';
                                } else {
                                    strncpy(details->exec_start, path, sizeof(details->exec_start) - 1);
                                    details->exec_start[sizeof(details->exec_start) - 1] = '\0';
                                }
                                
                                sd_bus_message_exit_container(reply);
                            }
                        }
                        sd_bus_message_exit_container(reply);
                    }
                    sd_bus_message_exit_container(reply);
                }
                sd_bus_message_exit_container(reply);
            }
        }
    }
    
    if (strlen(details->exec_start) == 0) {
        strcpy(details->exec_start, "N/A");
    }
    
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    
    // Autres détails simplifiés
    strcpy(details->cpu_usage, "N/A");
    strcpy(details->start_timestamp, "N/A");
    strcpy(details->active_enter_timestamp, "N/A");
    
    // Nouveaux champs pour More Details
    // Récupérer ActiveEnterTimestamp (en microsecondes depuis epoch)
    uint64_t timestamp_usec = get_service_property_uint64(bus, object_path, 
                                                           "org.freedesktop.systemd1.Unit",
                                                           "ActiveEnterTimestamp");
    if (timestamp_usec > 0) {
        time_t timestamp_sec = timestamp_usec / 1000000; // Convertir en secondes
        struct tm *tm_info = localtime(&timestamp_sec);
        if (tm_info) {
            strftime(details->exec_main_start_timestamp, 
                    sizeof(details->exec_main_start_timestamp),
                    "%Y-%m-%d %H:%M:%S", tm_info);
        } else {
            strcpy(details->exec_main_start_timestamp, "N/A");
        }
    } else {
        strcpy(details->exec_main_start_timestamp, "Not started");
    }
    
    uint64_t cpu_nsec = get_service_property_uint64(bus, object_path, 
                                                     "org.freedesktop.systemd1.Service", 
                                                     "CPUUsageNSec");
    snprintf(details->cpu_usage_nsec, sizeof(details->cpu_usage_nsec), "%lu", cpu_nsec);
    
    uint32_t tasks = get_service_property_uint32(bus, object_path,
                                                  "org.freedesktop.systemd1.Service",
                                                  "TasksCurrent");
    if (tasks > 0) {
        snprintf(details->tasks_current, sizeof(details->tasks_current), "%u", tasks);
    } else {
        strcpy(details->tasks_current, "0");
    }
    
    get_service_property_string(bus, object_path, "org.freedesktop.systemd1.Service",
                                "User", details->user, sizeof(details->user));
    if (strlen(details->user) == 0) {
        strcpy(details->user, "root");
    }
    
    get_service_property_string(bus, object_path, "org.freedesktop.systemd1.Service",
                                "Group", details->group, sizeof(details->group));
    if (strlen(details->group) == 0) {
        strcpy(details->group, "root");
    }
    
    // WorkingDirectory - peut être vide si non défini
    get_service_property_string(bus, object_path, "org.freedesktop.systemd1.Service",
                                "WorkingDirectory", details->working_directory, 
                                sizeof(details->working_directory));
    if (strlen(details->working_directory) == 0) {
        strcpy(details->working_directory, "(inherited)");
    }
    
    // Environment - récupérer le tableau de variables d'environnement
    sd_bus_error env_error = SD_BUS_ERROR_NULL;
    sd_bus_message *env_reply = NULL;
    r = sd_bus_call_method(bus,
                          "org.freedesktop.systemd1",
                          object_path,
                          "org.freedesktop.DBus.Properties",
                          "Get",
                          &env_error,
                          &env_reply,
                          "ss",
                          "org.freedesktop.systemd1.Service",
                          "Environment");
    
    if (r >= 0) {
        // Environment est un array de strings
        r = sd_bus_message_enter_container(env_reply, 'v', NULL);
        if (r >= 0) {
            r = sd_bus_message_enter_container(env_reply, 'a', "s");
            if (r >= 0) {
                const char *env_var;
                int env_count = 0;
                char env_buffer[512] = "";
                
                while (sd_bus_message_read(env_reply, "s", &env_var) > 0 && env_count < 5) {
                    if (env_count > 0) {
                        strncat(env_buffer, ", ", sizeof(env_buffer) - strlen(env_buffer) - 1);
                    }
                    strncat(env_buffer, env_var, sizeof(env_buffer) - strlen(env_buffer) - 1);
                    env_count++;
                }
                
                if (env_count > 0) {
                    strncpy(details->environment, env_buffer, sizeof(details->environment) - 1);
                    details->environment[sizeof(details->environment) - 1] = '\0';
                } else {
                    strcpy(details->environment, "(none)");
                }
                
                sd_bus_message_exit_container(env_reply);
            }
            sd_bus_message_exit_container(env_reply);
        }
    } else {
        strcpy(details->environment, "(none)");
    }
    
    sd_bus_error_free(&env_error);
    sd_bus_message_unref(env_reply);
    
    // Ressources - Limites et métriques
    uint64_t limit_nproc = get_service_property_uint64(bus, object_path,
                                                        "org.freedesktop.systemd1.Service",
                                                        "LimitNPROC");
    if (limit_nproc == UINT64_MAX) {
        strcpy(details->limit_nproc, "unlimited");
    } else {
        snprintf(details->limit_nproc, sizeof(details->limit_nproc), "%lu", limit_nproc);
    }
    
    uint64_t limit_nofile = get_service_property_uint64(bus, object_path,
                                                         "org.freedesktop.systemd1.Service",
                                                         "LimitNOFILE");
    if (limit_nofile == UINT64_MAX) {
        strcpy(details->limit_nofile, "unlimited");
    } else {
        snprintf(details->limit_nofile, sizeof(details->limit_nofile), "%lu", limit_nofile);
    }
    
    uint64_t mem_current = get_service_property_uint64(bus, object_path,
                                                        "org.freedesktop.systemd1.Service",
                                                        "MemoryCurrent");
    if (mem_current > 0) {
        if (mem_current >= 1073741824) { // >= 1 GB
            snprintf(details->memory_current, sizeof(details->memory_current), 
                    "%.2f GB", mem_current / 1073741824.0);
        } else if (mem_current >= 1048576) { // >= 1 MB
            snprintf(details->memory_current, sizeof(details->memory_current), 
                    "%.2f MB", mem_current / 1048576.0);
        } else if (mem_current >= 1024) { // >= 1 KB
            snprintf(details->memory_current, sizeof(details->memory_current), 
                    "%.2f KB", mem_current / 1024.0);
        } else {
            snprintf(details->memory_current, sizeof(details->memory_current), 
                    "%lu B", mem_current);
        }
    } else {
        strcpy(details->memory_current, "0 B");
    }
    
    get_service_property_string(bus, object_path, "org.freedesktop.systemd1.Service",
                                "ControlGroup", details->cgroup_path, 
                                sizeof(details->cgroup_path));
    if (strlen(details->cgroup_path) == 0) {
        strcpy(details->cgroup_path, "(none)");
    }
    
    // Dépendances - récupérer les tableaux de strings
    sd_bus_error dep_error = SD_BUS_ERROR_NULL;
    sd_bus_message *dep_reply = NULL;
    
    // Requires
    r = sd_bus_call_method(bus, "org.freedesktop.systemd1", object_path,
                          "org.freedesktop.DBus.Properties", "Get",
                          &dep_error, &dep_reply, "ss",
                          "org.freedesktop.systemd1.Unit", "Requires");
    if (r >= 0) {
        r = sd_bus_message_enter_container(dep_reply, 'v', "as");
        if (r >= 0) {
            r = sd_bus_message_enter_container(dep_reply, 'a', "s");
            if (r >= 0) {
                const char *dep;
                int count = 0;
                char buffer[512] = "";
                while (sd_bus_message_read(dep_reply, "s", &dep) > 0 && count < 3) {
                    if (count > 0) strncat(buffer, ", ", sizeof(buffer) - strlen(buffer) - 1);
                    strncat(buffer, dep, sizeof(buffer) - strlen(buffer) - 1);
                    count++;
                }
                if (count > 0) {
                    strncpy(details->requires, buffer, sizeof(details->requires) - 1);
                } else {
                    strcpy(details->requires, "(none)");
                }
                sd_bus_message_exit_container(dep_reply);
            }
            sd_bus_message_exit_container(dep_reply);
        }
    } else {
        strcpy(details->requires, "(none)");
    }
    sd_bus_error_free(&dep_error);
    sd_bus_message_unref(dep_reply);
    
    // Wants
    dep_error = SD_BUS_ERROR_NULL;
    dep_reply = NULL;
    r = sd_bus_call_method(bus, "org.freedesktop.systemd1", object_path,
                          "org.freedesktop.DBus.Properties", "Get",
                          &dep_error, &dep_reply, "ss",
                          "org.freedesktop.systemd1.Unit", "Wants");
    if (r >= 0) {
        r = sd_bus_message_enter_container(dep_reply, 'v', "as");
        if (r >= 0) {
            r = sd_bus_message_enter_container(dep_reply, 'a', "s");
            if (r >= 0) {
                const char *dep;
                int count = 0;
                char buffer[512] = "";
                while (sd_bus_message_read(dep_reply, "s", &dep) > 0 && count < 3) {
                    if (count > 0) strncat(buffer, ", ", sizeof(buffer) - strlen(buffer) - 1);
                    strncat(buffer, dep, sizeof(buffer) - strlen(buffer) - 1);
                    count++;
                }
                if (count > 0) {
                    strncpy(details->wants, buffer, sizeof(details->wants) - 1);
                } else {
                    strcpy(details->wants, "(none)");
                }
                sd_bus_message_exit_container(dep_reply);
            }
            sd_bus_message_exit_container(dep_reply);
        }
    } else {
        strcpy(details->wants, "(none)");
    }
    sd_bus_error_free(&dep_error);
    sd_bus_message_unref(dep_reply);
    
    // After
    dep_error = SD_BUS_ERROR_NULL;
    dep_reply = NULL;
    r = sd_bus_call_method(bus, "org.freedesktop.systemd1", object_path,
                          "org.freedesktop.DBus.Properties", "Get",
                          &dep_error, &dep_reply, "ss",
                          "org.freedesktop.systemd1.Unit", "After");
    if (r >= 0) {
        r = sd_bus_message_enter_container(dep_reply, 'v', "as");
        if (r >= 0) {
            r = sd_bus_message_enter_container(dep_reply, 'a', "s");
            if (r >= 0) {
                const char *dep;
                int count = 0;
                char buffer[512] = "";
                while (sd_bus_message_read(dep_reply, "s", &dep) > 0 && count < 3) {
                    if (count > 0) strncat(buffer, ", ", sizeof(buffer) - strlen(buffer) - 1);
                    strncat(buffer, dep, sizeof(buffer) - strlen(buffer) - 1);
                    count++;
                }
                if (count > 0) {
                    strncpy(details->after, buffer, sizeof(details->after) - 1);
                } else {
                    strcpy(details->after, "(none)");
                }
                sd_bus_message_exit_container(dep_reply);
            }
            sd_bus_message_exit_container(dep_reply);
        }
    } else {
        strcpy(details->after, "(none)");
    }
    sd_bus_error_free(&dep_error);
    sd_bus_message_unref(dep_reply);
    
    // PartOf
    get_service_property_string(bus, object_path, "org.freedesktop.systemd1.Unit",
                                "PartOf", details->part_of, sizeof(details->part_of));
    if (strlen(details->part_of) == 0) {
        strcpy(details->part_of, "(none)");
    }
    
    // Sécurité
    get_service_property_string(bus, object_path, "org.freedesktop.systemd1.Service",
                                "ProtectSystem", details->protect_system, 
                                sizeof(details->protect_system));
    if (strlen(details->protect_system) == 0) {
        strcpy(details->protect_system, "no");
    }
    
    // NoNewPrivileges (booléen)
    sd_bus_error sec_error = SD_BUS_ERROR_NULL;
    sd_bus_message *sec_reply = NULL;
    int no_new_privs = 0;
    r = sd_bus_call_method(bus, "org.freedesktop.systemd1", object_path,
                          "org.freedesktop.DBus.Properties", "Get",
                          &sec_error, &sec_reply, "ss",
                          "org.freedesktop.systemd1.Service", "NoNewPrivileges");
    if (r >= 0) {
        sd_bus_message_read(sec_reply, "v", "b", &no_new_privs);
        strcpy(details->no_new_privileges, no_new_privs ? "yes" : "no");
    } else {
        strcpy(details->no_new_privileges, "no");
    }
    sd_bus_error_free(&sec_error);
    sd_bus_message_unref(sec_reply);
    
    // CapabilityBoundingSet
    uint64_t caps = get_service_property_uint64(bus, object_path,
                                                 "org.freedesktop.systemd1.Service",
                                                 "CapabilityBoundingSet");
    if (caps == UINT64_MAX) {
        strcpy(details->capability_bounding_set, "all");
    } else if (caps == 0) {
        strcpy(details->capability_bounding_set, "none");
    } else {
        snprintf(details->capability_bounding_set, sizeof(details->capability_bounding_set),
                "0x%lx", caps);
    }
    
    sd_bus_unref(bus);
    return 0;
}

/**
 * Démarre, arrête ou redémarre un service systemd
 * 
 * @param service_name - Nom du service
 * @param action - SERVICE_ACTION_START (0), SERVICE_ACTION_STOP (1), SERVICE_ACTION_RESTART (2)
 * @return int - 0 en cas de succès, -1 en cas d'erreur
 */
int systemd_service_control(const char *service_name, ServiceAction action) {
    if (!service_name) return -1;

    if (root_mode_is_active())
        return privileged_systemd_service_control(service_name, action);
    
    sd_bus *bus = get_systemd_bus();
    if (!bus) return -1;
    
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r;
    
    const char *method;
    
    switch (action) {
        case SERVICE_ACTION_START:
            method = "StartUnit";
            break;
        case SERVICE_ACTION_STOP:
            method = "StopUnit";
            break;
        case SERVICE_ACTION_RESTART:
            method = "RestartUnit";
            break;
        default:
            sd_bus_unref(bus);
            return -1;
    }
    
    r = sd_bus_call_method(bus,
                          "org.freedesktop.systemd1",
                          "/org/freedesktop/systemd1",
                          "org.freedesktop.systemd1.Manager",
                          method,
                          &error,
                          &reply,
                          "ss",
                          service_name,
                          "replace");  // Mode de remplacement
    
    int success = (r >= 0);
    
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    
    return success ? 0 : -1;
}

/**
 * Active ou désactive un service systemd
 * 
 * @param service_name - Nom du service
 * @param enable - 1 pour activer, 0 pour désactiver
 * @return int - 0 en cas de succès, -1 en cas d'erreur
 */
int systemd_service_enable_disable(const char *service_name, int enable) {
    if (!service_name) return -1;

    if (root_mode_is_active())
        return privileged_systemd_service_enable_disable(service_name, enable);
    
    sd_bus *bus = get_systemd_bus();
    if (!bus) return -1;
    
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r;
    
    const char *method = enable ? "EnableUnitFiles" : "DisableUnitFiles";
    
    r = sd_bus_call_method(bus,
                          "org.freedesktop.systemd1",
                          "/org/freedesktop/systemd1",
                          "org.freedesktop.systemd1.Manager",
                          method,
                          &error,
                          &reply,
                          "asbb",
                          1, service_name,  // Array d'une string
                          0,                // runtime = false (permanent)
                          1);               // force = true
    
    int success = (r >= 0);
    
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    
    return success ? 0 : -1;
}

int get_systemd_service_fragment_path(const char *service_name, char *path, size_t path_len) {
    if (!service_name || !path || path_len == 0) return -1;
    
    sd_bus *bus = get_systemd_bus();
    if (!bus) return -1;
    
    char object_path[512];
    if (get_service_object_path(bus, service_name, object_path, sizeof(object_path)) < 0) {
        sd_bus_unref(bus);
        return -1;
    }
    
    int r = get_service_property_string(bus, object_path, "org.freedesktop.systemd1.Unit", 
                                        "FragmentPath", path, path_len);
    sd_bus_unref(bus);
    return (r >= 0 && strcmp(path, "N/A") != 0 && strlen(path) > 0) ? 0 : -1;
}
