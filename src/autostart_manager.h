#ifndef AUTOSTART_MANAGER_H
#define AUTOSTART_MANAGER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *name;
    char *exec;
    char *path;
    int enabled;
    char *reason;
    char *tde_condition;  /* X-TDE-autostart-condition value, NULL if not TDE */
} AutostartEntry;

typedef enum {
    TOGGLE_SUCCESS = 0,
    TOGGLE_ERROR_FILE_NOT_FOUND,
    TOGGLE_ERROR_PERMISSION_DENIED,
    TOGGLE_ERROR_UNSUPPORTED_TYPE,
    TOGGLE_ERROR_READ_FAILED,
    TOGGLE_ERROR_WRITE_FAILED,
    TOGGLE_ERROR_ALREADY_STATE,
    TOGGLE_ERROR_UNKNOWN
} ToggleResult;

// Fonctions principales
AutostartEntry* get_autostart_entries(size_t* count);
void free_autostart_entries(AutostartEntry* entries, size_t count);
ToggleResult toggle_autostart_entry(const char *filepath, int enable, char *message);
ToggleResult toggle_autostart_entry_with_password(const char *password,
                                                  const char *filepath,
                                                  int enable,
                                                  char *message);
const char* toggle_result_to_string(ToggleResult result);

// Fonctions utilitaires (maintenant dans manager_common.h)
int find_in_path(const char *program);
char* extract_command(const char *exec_line);

// Fonctions internes (add_entry est maintenant statique)
void parse_desktop(const char *filepath, const char* home);
void scan_directory(const char *dirpath, int parse_shell, const char* home);

#ifdef __cplusplus
}
#endif

#endif // AUTOSTART_MANAGER_H
