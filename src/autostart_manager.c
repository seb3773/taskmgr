#include "common.h"
#include "autostart_manager.h"
#include "manager_common.h"
#include "root_credential_vault.h"
#include "privileged_exec.h"
#include <pwd.h>

#define MAX_PATH 1024
#define MAX_LINE 1024
#define INITIAL_CAPACITY 16

static AutostartEntry *entries = NULL;
static size_t entry_count = 0;
static size_t entry_capacity = 0;
static const char *current_desktop = NULL;
static memory_pool_t *string_pool = NULL;

OPTIMIZE_SIZE
static int add_entry(const char *name, const char *exec, const char *path, int enabled, const char *reason) {
    if (entry_count >= entry_capacity) {
        size_t new_capacity = entry_capacity ? entry_capacity * 2 : INITIAL_CAPACITY;
        AutostartEntry *new_entries = realloc(entries, new_capacity * sizeof(AutostartEntry));
        if (!new_entries) return MANAGER_ERROR_MEMORY;
        entries = new_entries;
        entry_capacity = new_capacity;
    }
    
    entries[entry_count].name = strdup(name);
    if (!entries[entry_count].name) return MANAGER_ERROR_MEMORY;
    
    entries[entry_count].exec = exec ? strdup(exec) : NULL;
    if (exec && !entries[entry_count].exec) {
        free(entries[entry_count].name);
        return MANAGER_ERROR_MEMORY;
    }
    
    entries[entry_count].path = strdup(path);
    if (!entries[entry_count].path) {
        free(entries[entry_count].name);
        MANAGER_SAFE_FREE(entries[entry_count].exec);
        return MANAGER_ERROR_MEMORY;
    }
    
    entries[entry_count].enabled = enabled;
    entries[entry_count].reason = reason ? strdup(reason) : NULL;
    if (reason && !entries[entry_count].reason) {
        free(entries[entry_count].name);
        MANAGER_SAFE_FREE(entries[entry_count].exec);
        free(entries[entry_count].path);
        return MANAGER_ERROR_MEMORY;
    }
    
    entry_count++;
    return MANAGER_OK;
}

// Fonctions utilitaires remplacées par manager_common.h

int find_in_path(const char *program) {
    if (!program || strlen(program) == 0) return 0;
    
    if (strchr(program, '/')) {
        return access(program, X_OK) == 0;
    }
    
    char *path_env = getenv("PATH");
    if (!path_env) return 0;
    
    char *path_copy = strdup(path_env);
    char *saveptr;
    char *dir = strtok_r(path_copy, ":", &saveptr);
    char full_path[MAX_PATH];
    
    while (dir) {
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, program);
        if (access(full_path, X_OK) == 0) {
            free(path_copy);
            return 1;
        }
        dir = strtok_r(NULL, ":", &saveptr);
    }
    
    free(path_copy);
    return 0;
}

char* extract_command(const char *exec_line) {
    if (!exec_line) return NULL;
    
    char *exec_copy = strdup(exec_line);
    char *result = NULL;
    
    char *start = exec_copy;
    while (*start && strchr(start, '=') && strchr(start, ' ')) {
        char *space = strchr(start, ' ');
        char *equals = strchr(start, '=');
        if (equals < space) {
            start = space + 1;
            while (*start == ' ') start++;
        } else {
            break;
        }
    }
    
    char *space = strchr(start, ' ');
    if (space) *space = '\0';
    
    result = strdup(start);
    free(exec_copy);
    return result;
}

void parse_desktop(const char *filepath, const char* home) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) return;

    char line[MAX_LINE];
    char *name = NULL;
    char *exec = NULL;
    int hidden = 0;
    int gnome_enabled = 1;
    int de_allowed = -1;
    int in_entry = 0;
    int no_display = 0;
    char *try_exec = NULL;

    while (fgets(line, sizeof(line), fp)) {
        manager_trim_newline(line);
        char *trimmed = manager_strip_whitespace(line);
        
        if (strcmp(trimmed, "[Desktop Entry]") == 0) { 
            in_entry = 1; 
            continue; 
        }
        if (!in_entry) continue;
        if (trimmed[0] == '[') break;
        
        if (strlen(trimmed) == 0 || trimmed[0] == '#') continue;

        char *key = trimmed;
        char *value = strchr(trimmed, '=');
        if (!value) continue;
        *value++ = '\0';
        
        key = manager_strip_whitespace(key);
        value = manager_strip_whitespace(value);

        if (strcmp(key, "Name") == 0) {
            free(name); 
            name = strdup(value);
        } else if (strcmp(key, "Exec") == 0 || strcmp(key, "Exec[$e]") == 0) {
            free(exec); 
            exec = manager_expand_home(value, home);
        } else if (strcmp(key, "TryExec") == 0) {
            free(try_exec);
            try_exec = manager_expand_home(value, home);
        } else if (strcmp(key, "Hidden") == 0 && strcmp(value, "true") == 0) {
            hidden = 1;
        } else if (strcmp(key, "NoDisplay") == 0 && strcmp(value, "true") == 0) {
            no_display = 1;
        } else if (strcmp(key, "X-GNOME-Autostart-enabled") == 0 && strcmp(value, "false") == 0) {
            gnome_enabled = 0;
        } else if (strcmp(key, "OnlyShowIn") == 0 && current_desktop) {
            de_allowed = strstr(value, current_desktop) ? 1 : 0;
        } else if (strcmp(key, "NotShowIn") == 0 && current_desktop) {
            de_allowed = strstr(value, current_desktop) ? 0 : (de_allowed == -1 ? 1 : de_allowed);
        }
    }
    fclose(fp);

    if (de_allowed == 0) { 
        free(name); 
        free(exec); 
        free(try_exec);
        return; 
    }

    int enabled = 1;
    char *reason = NULL;
    
    if (hidden) {
        enabled = 0;
        reason = strdup("Hidden=true");
    } else if (!gnome_enabled) {
        enabled = 0;
        reason = strdup("X-GNOME-Autostart-enabled=false");
    } else {
        if (try_exec && strlen(try_exec) > 0) {
            char *cmd = extract_command(try_exec);
            if (!find_in_path(cmd)) {
                enabled = 0;
                reason = malloc(256);
                snprintf(reason, 256, "TryExec command not found: %s", cmd);
            }
            free(cmd);
        }
        
        if (enabled && exec && strlen(exec) > 0) {
            char *cmd = extract_command(exec);
            if (!find_in_path(cmd)) {
                enabled = 0;
                reason = malloc(256);
                snprintf(reason, 256, "Exec command not found: %s", cmd);
            }
            free(cmd);
        } else if (!exec || strlen(exec) == 0) {
            enabled = 0;
            reason = strdup("No Exec command specified");
        }
    }

    if (!name) {
        const char *filename = strrchr(filepath, '/');
        filename = filename ? filename + 1 : filepath;
        const char *dot = strrchr(filename, '.');
        name = (dot && strcmp(dot, ".desktop") == 0) ? 
               strndup(filename, dot - filename) : strdup(filename);
    }

    if (add_entry(name, exec, filepath, enabled, reason) != MANAGER_OK) {
        // Log error but continue processing
    }

    free(name); 
    free(exec); 
    free(try_exec);
    free(reason);
}

void scan_directory(const char *dirpath, int parse_shell, const char* home) {
    DIR *dir = opendir(dirpath);
    if (!dir) return;

    struct dirent *ent;
    char filepath[MAX_PATH];

    while ((ent = readdir(dir))) {
        if (ent->d_type != DT_REG && ent->d_type != DT_LNK && ent->d_type != DT_UNKNOWN) continue;
        snprintf(filepath, sizeof(filepath), "%s/%s", dirpath, ent->d_name);
        
        struct stat st;
        if (stat(filepath, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        if (manager_has_suffix(ent->d_name, ".desktop")) {
            parse_desktop(filepath, home);
        } else if (parse_shell && manager_has_suffix(ent->d_name, ".sh") && access(filepath, X_OK) == 0) {
            char *name = strdup(ent->d_name);
            char *dot = strrchr(name, '.');
            if (dot) *dot = 0;
            if (add_entry(name, filepath, filepath, 1, NULL) != MANAGER_OK) {
                // Log error but continue processing
            }
            free(name);
        }
    }
    closedir(dir);
}

OPTIMIZE_SIZE
static int autostart_manager_init(void) {
    entries = NULL;
    entry_count = 0;
    entry_capacity = 0;
    string_pool = manager_create_pool(8192, 64); // 8KB pool with 64-byte blocks
    return string_pool ? MANAGER_OK : MANAGER_ERROR_MEMORY;
}

OPTIMIZE_SIZE
static void autostart_manager_cleanup(void) {
    free_autostart_entries(entries, entry_count);
    entries = NULL;
    entry_count = 0;
    entry_capacity = 0;
    manager_destroy_pool(string_pool);
    string_pool = NULL;
}

OPTIMIZE_SIZE
static void autostart_manager_update(void) {
    // Autostart entries don't need frequent updates
    // Only refresh when explicitly requested
}

OPTIMIZE_SIZE
static int autostart_manager_get_count(void) {
    return (int)entry_count;
}

OPTIMIZE_SIZE
static void* autostart_manager_get_info(int index) {
    if (index < 0 || index >= (int)entry_count) return NULL;
    return &entries[index];
}

// Interface publique pour le manager
manager_interface_t autostart_manager_interface = {
    .name = "autostart",
    .init = autostart_manager_init,
    .cleanup = autostart_manager_cleanup,
    .update = autostart_manager_update,
    .get_count = autostart_manager_get_count,
    .get_info = autostart_manager_get_info
};

AutostartEntry* get_autostart_entries(size_t* count) {
    // Cleanup previous entries
    if (entries) {
        free_autostart_entries(entries, entry_count);
    }
    entries = NULL;
    entry_count = 0;
    entry_capacity = 0;
    
    char *home = getenv("HOME");
    if (!home) { 
        struct passwd *pw = getpwuid(getuid()); 
        if (pw) home = pw->pw_dir; 
    }
    if (!home) { 
        *count = 0; 
        return NULL; 
    }

    current_desktop = getenv("XDG_CURRENT_DESKTOP");

    char path[MAX_PATH];

    const char *user_dirs[] = {
        ".local/share/autostart",
        ".kde/Autostart",
        ".kde4/Autostart", 
        ".config/plasma-workspace/env",
        ".config/plasma-workspace/shutdown",
        ".gnome2/session",
        ".config/gnome-session/autostart",
        ".config/openbox",
        ".fluxbox/startup",
        ".trinity/Autostart",
        ".config/lxsession/LXDE/autostart",
        ".config/xfce4/autostart",
        ".e/e/applications/startup",
        ".config/i3",
        ".config/sway/config.d"
    };
    
    for (size_t i = 0; i < sizeof(user_dirs)/sizeof(user_dirs[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", home, user_dirs[i]);
        scan_directory(path, 1, home);
    }

    char *xdg_config = getenv("XDG_CONFIG_HOME");
    if (!xdg_config) {
        snprintf(path, sizeof(path), "%s/.config", home);
        xdg_config = path;
    }
    snprintf(path, sizeof(path), "%s/autostart", xdg_config);
    scan_directory(path, 1, home);

    char *xdg_dirs = getenv("XDG_CONFIG_DIRS");
    if (!xdg_dirs) xdg_dirs = "/etc/xdg";
    
    char *copy = strdup(xdg_dirs);
    char *saveptr;
    char *tok = strtok_r(copy, ":", &saveptr);
    while (tok) {
        snprintf(path, sizeof(path), "%s/autostart", tok);
        scan_directory(path, 0, home);
        tok = strtok_r(NULL, ":", &saveptr);
    }
    free(copy);

    *count = entry_count;
    return entries;
}

void free_autostart_entries(AutostartEntry* entries, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(entries[i].name);
        if (entries[i].exec) free(entries[i].exec);
        free(entries[i].path);
        if (entries[i].reason) free(entries[i].reason);
    }
    free(entries);
}

static int has_prefix(const char *str, const char *prefix) {
    if (!str || !prefix) return 0;
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

static ToggleResult toggle_desktop_entry(const char *filepath, int enable,
                                           char *message, const char *sudo_password) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        snprintf(message, 512, "Cannot read file %s: %s", filepath, strerror(errno));
        return (errno == EACCES) ? TOGGLE_ERROR_PERMISSION_DENIED : TOGGLE_ERROR_READ_FAILED;
    }

    char temp_path[512];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", filepath);
    
    FILE *temp_fp = fopen(temp_path, "w");
    if (!temp_fp) {
        fclose(fp);
        snprintf(message, 512, "Cannot create temporary file %s: %s", temp_path, strerror(errno));
        return TOGGLE_ERROR_WRITE_FAILED;
    }

    char line[1024];
    int in_desktop_entry = 0;
    int found_hidden = 0;
    int found_gnome_enabled = 0;
    int current_hidden_state = 0;
    int current_gnome_state = 1;

    // Première passe : lire et analyser l'état actuel
    while (fgets(line, sizeof(line), fp)) {
        char line_copy[1024];
        strcpy(line_copy, line);
        char *trimmed = manager_strip_whitespace(line_copy);
        
        if (strcmp(trimmed, "[Desktop Entry]") == 0) {
            in_desktop_entry = 1;
        } else if (in_desktop_entry && trimmed[0] == '[') {
            in_desktop_entry = 0;
        }

        if (in_desktop_entry) {
            if (has_prefix(trimmed, "Hidden=")) {
                found_hidden = 1;
                current_hidden_state = manager_has_suffix(trimmed, "true");
            } else if (has_prefix(trimmed, "X-GNOME-Autostart-enabled=")) {
                found_gnome_enabled = 1;
                current_gnome_state = manager_has_suffix(trimmed, "true");
            }
        }
    }

    // Déterminer l'état actuel
    int currently_enabled = (!found_hidden || !current_hidden_state) && 
                           (!found_gnome_enabled || current_gnome_state);

    // Vérifier si le changement est nécessaire
    if (currently_enabled == enable) {
        fclose(fp);
        fclose(temp_fp);
        unlink(temp_path);
        snprintf(message, 512, "Entry %s is already %s", 
                 filepath, enable ? "enabled" : "disabled");
        return TOGGLE_ERROR_ALREADY_STATE;
    }

    // Revenir au début du fichier pour la réécriture
    rewind(fp);
    in_desktop_entry = 0;

    while (fgets(line, sizeof(line), fp)) {
        char line_copy[1024];
        strcpy(line_copy, line);
        char *trimmed = manager_strip_whitespace(line_copy);
        
        if (strcmp(trimmed, "[Desktop Entry]") == 0) {
            in_desktop_entry = 1;
            fprintf(temp_fp, "%s", line);
            continue;
        }
        
        if (in_desktop_entry && trimmed[0] == '[') {
            // Avant de sortir de la section [Desktop Entry]
            if (!enable) {
                // Pour désactiver : ajouter Hidden=true si pas trouvé
                if (!found_hidden) {
                    fprintf(temp_fp, "Hidden=true\n");
                }
                // Ajouter X-GNOME-Autostart-enabled=false si pas trouvé
                if (!found_gnome_enabled) {
                    fprintf(temp_fp, "X-GNOME-Autostart-enabled=false\n");
                }
            }
            in_desktop_entry = 0;
        }

        int skip_line = 0;
        
        if (in_desktop_entry) {
            if (enable) {
                // Pour activer : corriger les deux champs
                if (has_prefix(trimmed, "Hidden=")) {
                    fprintf(temp_fp, "Hidden=false\n");
                    skip_line = 1;
                } else if (has_prefix(trimmed, "X-GNOME-Autostart-enabled=")) {
                    fprintf(temp_fp, "X-GNOME-Autostart-enabled=true\n");
                    skip_line = 1;
                }
            } else {
                // Pour désactiver : gérer les deux champs
                if (has_prefix(trimmed, "Hidden=")) {
                    fprintf(temp_fp, "Hidden=true\n");
                    skip_line = 1;
                } else if (has_prefix(trimmed, "X-GNOME-Autostart-enabled=")) {
                    fprintf(temp_fp, "X-GNOME-Autostart-enabled=false\n");
                    skip_line = 1;
                }
            }
        }
        
        if (!skip_line) {
            fprintf(temp_fp, "%s", line);
        }
    }

    // Si on est encore dans [Desktop Entry] à la fin du fichier
    if (in_desktop_entry && !enable) {
        if (!found_hidden) {
            fprintf(temp_fp, "Hidden=true\n");
        }
        if (!found_gnome_enabled) {
            fprintf(temp_fp, "X-GNOME-Autostart-enabled=false\n");
        }
    }

    fclose(fp);
    fclose(temp_fp);

    // Remplacer le fichier original
    gboolean replaced = FALSE;
    if (sudo_password && sudo_password[0])
        replaced = privileged_move_file_with_password(sudo_password, temp_path, filepath);
    else if (root_mode_is_active())
        replaced = privileged_move_file(temp_path, filepath);
    else
        replaced = (rename(temp_path, filepath) == 0);

    if (!replaced) {
        snprintf(message, 512, "Cannot replace original file %s: %s", filepath, strerror(errno));
        unlink(temp_path);
        return TOGGLE_ERROR_WRITE_FAILED;
    }

    snprintf(message, 512, "Successfully %s desktop entry: %s", 
             enable ? "enabled" : "disabled", filepath);
    return TOGGLE_SUCCESS;
}

ToggleResult toggle_autostart_entry(const char *filepath, int enable, char *message) {
    return toggle_autostart_entry_with_password(NULL, filepath, enable, message);
}

ToggleResult toggle_autostart_entry_with_password(const char *password,
                                                  const char *filepath,
                                                  int enable,
                                                  char *message) {
    if (!filepath || !message) {
        if (message) snprintf(message, 512, "Invalid parameters");
        return TOGGLE_ERROR_UNKNOWN;
    }

    // Vérifier que le fichier existe
    if (access(filepath, F_OK) != 0) {
        snprintf(message, 512, "File not found: %s", filepath);
        return TOGGLE_ERROR_FILE_NOT_FOUND;
    }

    size_t len = strlen(filepath);
    
    // Déterminer le type de fichier et appeler la bonne fonction
    if (len > 8 && manager_has_suffix(filepath, ".desktop")) {
        return toggle_desktop_entry(filepath, enable, message, password);
    } else {
        snprintf(message, 512, "Unsupported file type: %s (only .desktop files are supported)", filepath);
        return TOGGLE_ERROR_UNSUPPORTED_TYPE;
    }
}

const char* toggle_result_to_string(ToggleResult result) {
    switch (result) {
        case TOGGLE_SUCCESS: return "Success";
        case TOGGLE_ERROR_FILE_NOT_FOUND: return "File not found";
        case TOGGLE_ERROR_PERMISSION_DENIED: return "Permission denied";
        case TOGGLE_ERROR_UNSUPPORTED_TYPE: return "Unsupported file type";
        case TOGGLE_ERROR_READ_FAILED: return "Read failed";
        case TOGGLE_ERROR_WRITE_FAILED: return "Write failed";
        case TOGGLE_ERROR_ALREADY_STATE: return "Already in requested state";
        case TOGGLE_ERROR_UNKNOWN: return "Unknown error";
        default: return "Invalid result code";
    }
}
