#ifndef SERVICE_MANAGER_H
#define SERVICE_MANAGER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_SERVICES 1000
#define SERVICE_NAME_LEN 256
#define SERVICE_PID_LEN 20
#define SERVICE_DESC_LEN 512
#define SERVICE_STATUS_LEN 32

typedef struct {
    char name[SERVICE_NAME_LEN];
    char pid[SERVICE_PID_LEN];
    char description[SERVICE_DESC_LEN];
    char slice[128];  // Slice systemd (ex: system.slice, user.slice)
    char status[SERVICE_STATUS_LEN];
    char memory_usage[32];
} SystemdService;

typedef struct {
    SystemdService *services;
    int count;
    int capacity;
} SystemdServiceList;

typedef struct {
    char name[SERVICE_NAME_LEN];
    char description[SERVICE_DESC_LEN];
    char active_state[32];
    char sub_state[32];
    char load_state[32];
    char unit_file_state[32];
    char main_pid[SERVICE_PID_LEN];
    char control_pid[SERVICE_PID_LEN];
    char service_type[32];
    char restart[32];
    char exec_start[512];
    char memory_usage[32];
    char cpu_usage[32];
    char start_timestamp[64];
    char active_enter_timestamp[64];
    // Nouveaux champs pour "More Details"
    char exec_main_start_timestamp[64];
    char cpu_usage_nsec[32];
    char tasks_current[16];
    char user[64];
    char group[64];
    char working_directory[256];
    char environment[512];
    // Ressources
    char limit_nproc[32];
    char limit_nofile[32];
    char memory_current[32];
    char cgroup_path[256];
    // Dépendances
    char requires[512];
    char wants[512];
    char after[512];
    char part_of[256];
    // Sécurité
    char protect_system[32];
    char no_new_privileges[16];
    char capability_bounding_set[128];
} SystemdServiceDetails;

typedef enum {
    SERVICE_ACTION_START = 0,
    SERVICE_ACTION_STOP = 1,
    SERVICE_ACTION_RESTART = 2
} ServiceAction;

// Fonctions principales
SystemdServiceList* get_systemd_services(void);
int get_systemd_service_details(const char *service_name, SystemdServiceDetails *details);
int systemd_service_control(const char *service_name, ServiceAction action);
int systemd_service_enable_disable(const char *service_name, int enable);
int get_systemd_service_fragment_path(const char *service_name, char *path, size_t path_len);

// Fonctions utilitaires
SystemdServiceList* systemd_service_list_new(void);
void systemd_service_list_free(SystemdServiceList *list);

// Gestion du pool de mémoire
void reset_services_memory_pool(void);
void cleanup_services_memory_pool(void);

#ifdef __cplusplus
}
#endif

#endif // SERVICE_MANAGER_H
