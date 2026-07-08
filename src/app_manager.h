#ifndef APP_MANAGER_H
#define APP_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

// Type d'application
typedef enum {
    APP_TYPE_EDITOR,
    APP_TYPE_BROWSER,
    APP_TYPE_TERMINAL
} app_type_t;

// Structure pour stocker les informations d'une application
typedef struct {
    const char *name;
    char *binary_path;  // NULL si non trouvé
} AppInfo;

// Structure pour un gestionnaire d'applications générique
typedef struct {
    app_type_t type;
    const char *type_name;
    const char **app_list;
    AppInfo *available_apps;
    int available_apps_count;
    char *default_binary_path;
    int default_index;
} AppManager;

// Fonctions génériques
char* find_app_binary(const char *cmd);
void scan_available_apps(AppManager *manager);
void cleanup_apps(AppManager *manager);
void set_default_app(AppManager *manager, int app_index);
const char* get_app_name_from_path(AppManager *manager, const char *path);

// Gestionnaires globaux
extern AppManager editor_manager;
extern AppManager browser_manager;
extern AppManager terminal_manager;

// Fonctions de compatibilité pour l'existant
#define scan_available_editors() scan_available_apps(&editor_manager)
#define cleanup_editors() cleanup_apps(&editor_manager)
#define set_default_editor(index) set_default_app(&editor_manager, index)
#define get_editor_name_from_path(path) get_app_name_from_path(&editor_manager, path)

#define scan_available_browsers() scan_available_apps(&browser_manager)
#define cleanup_browsers() cleanup_apps(&browser_manager)
#define set_default_browser(index) set_default_app(&browser_manager, index)
#define get_browser_name_from_path(path) get_app_name_from_path(&browser_manager, path)

#define scan_available_terminals() scan_available_apps(&terminal_manager)
#define cleanup_terminals() cleanup_apps(&terminal_manager)
#define set_default_terminal(index) set_default_app(&terminal_manager, index)
#define get_terminal_name_from_path(path) get_app_name_from_path(&terminal_manager, path)

// Variables de compatibilité (pointeurs vers les gestionnaires)
#define available_editors (editor_manager.available_apps)
#define available_editors_count (editor_manager.available_apps_count)
#define default_editor_binary_path (editor_manager.default_binary_path)

#define available_browsers (browser_manager.available_apps)
#define available_browsers_count (browser_manager.available_apps_count)
#define default_browser_binary_path (browser_manager.default_binary_path)

#define available_terminals (terminal_manager.available_apps)
#define available_terminals_count (terminal_manager.available_apps_count)
#define default_terminal_binary_path (terminal_manager.default_binary_path)

#ifdef __cplusplus
}
#endif

#endif // APP_MANAGER_H
