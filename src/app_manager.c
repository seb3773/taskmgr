#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "common.h"
#include "app_manager.h"
#include <pwd.h>

// Liste des éditeurs supportés
const char *editors_list[] = {
    "kwrite", "kate", "gedit", "gnome-text-editor",
    "pluma", "mousepad", "leafpad", "xed", "notepadqq", "textadept", 
    "geany", "sublime_text", "atom", "brackets", "lighttable",
    "medit", "bluefish", "codelite", "code", "gvim", "featherpad", "tea", 
    "beaver", "zed", "slimtext", "xemacs", "komodo", "jedit", "bluegriffon", "anjuta", "xedit",
    NULL
};

// Liste des navigateurs supportés
const char *browsers_list[] = {
    "chrome", "palemoon", "chromium", "falkon",
    "microsoft-edge-stable", "firefox", "konqueror",
    "brave", "opera", "midori", "epiphany",
    "vivaldi", "seawolf", "tor-browser",
    "iceweasel", "waterfox", "qutebrowser", "google-chrome", "google-chrome-stable",
    "microsoft-edge", "edge", "opera-stable", "opera-developer",
    "brave-browser", "brave-bin", "vivaldi-stable", "chromium-browser", "firefox-esr",
    "librewolf", "ungoogled-chromium", "firedragon", "nyxt", "surf", "dillo",
    "netsurf", "otter-browser", "slimjet", "qupzilla", "dooble", "luakit", "vimb",
    "badwolf", "basilisk", "epiphany-browser", "rekonq", "arora",
    "icecat", "gnome-web", "kazehakase", "galeon", "abrowser",
    "min", "beaker", "angelfish", "fifth", "amaya", "chimera", "conkeror", "iceape",
    NULL
};

// Gestionnaires globaux
AppManager editor_manager = {
    .type = APP_TYPE_EDITOR,
    .type_name = "editor",
    .app_list = editors_list,
    .available_apps = NULL,
    .available_apps_count = 0,
    .default_binary_path = NULL,
    .default_index = 0
};

AppManager browser_manager = {
    .type = APP_TYPE_BROWSER,
    .type_name = "browser",
    .app_list = browsers_list,
    .available_apps = NULL,
    .available_apps_count = 0,
    .default_binary_path = NULL,
    .default_index = 0
};

// Liste des terminaux supportés
const char *terminals_list[] = {
    "konsole", "gnome-terminal", "xfce4-terminal", "mate-terminal",
    "lxterminal", "qterminal", "alacritty", "kitty", "xterm",
    "rxvt", "urxvt", "tilix", "terminology", "tilda", "guake",
    NULL
};

AppManager terminal_manager = {
    .type = APP_TYPE_TERMINAL,
    .type_name = "terminal",
    .app_list = terminals_list,
    .available_apps = NULL,
    .available_apps_count = 0,
    .default_binary_path = NULL,
    .default_index = 0
};

static char* check_dirs(const char *cmd, const char **dirs, int count) {
    for (int i = 0; i < count; i++) {
        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirs[i], cmd);
        if (access(fullpath, X_OK) == 0) {
            return strdup(fullpath);
        }
    }
    return NULL;
}

char* find_app_binary(const char *cmd) {
    char *path_env = getenv("PATH");
    char *paths_dup = path_env ? strdup(path_env) : NULL;

    if (paths_dup) {
        int count = 0;
        for (char *p = paths_dup; *p; p++) if (*p == ':') count++;
        count++; // nombre de chemins

        const char **dirs = malloc(sizeof(char*) * count);
        if (!dirs) { free(paths_dup); return NULL; }

        int i = 0;
        char *token = strtok(paths_dup, ":");
        while (token) { dirs[i++] = token; token = strtok(NULL, ":"); }

        char *result = check_dirs(cmd, dirs, count);
        free(dirs); free(paths_dup);
        return result;
    }
    return NULL;
}

void scan_available_apps(AppManager *manager) {
    // Vérification sécurité
    if (!manager) {
        g_warning("scan_available_apps: manager is NULL");
        return;
    }
    if (!manager->app_list) {
        g_warning("scan_available_apps: manager->app_list is NULL for type %s", 
                  manager->type_name ? manager->type_name : "unknown");
        return;
    }
    
    // Compter le nombre d'applications dans la liste
    int total_apps = 0;
    while (manager->app_list[total_apps]) total_apps++;

    // Allouer l'espace pour toutes les applications
    manager->available_apps = calloc(total_apps, sizeof(AppInfo));
    if (!manager->available_apps) return;

    manager->available_apps_count = 0;

    // Scanner chaque application de la liste
    for (int i = 0; manager->app_list[i]; i++) {
        char *binary_path = find_app_binary(manager->app_list[i]);
        if (binary_path) {
            manager->available_apps[manager->available_apps_count].name = manager->app_list[i];
            manager->available_apps[manager->available_apps_count].binary_path = binary_path;
            manager->available_apps_count++;
        }
    }
}

void cleanup_apps(AppManager *manager) {
    if (manager->available_apps) {
        for (int i = 0; i < manager->available_apps_count; i++) {
            if (manager->available_apps[i].binary_path) {
                free(manager->available_apps[i].binary_path);
            }
        }
        free(manager->available_apps);
        manager->available_apps = NULL;
    }
    manager->available_apps_count = 0;
    
    if (manager->default_binary_path) {
        free(manager->default_binary_path);
        manager->default_binary_path = NULL;
    }
}

void set_default_app(AppManager *manager, int app_index) {
    manager->default_index = app_index;
    
    if (manager->default_binary_path) {
        free(manager->default_binary_path);
        manager->default_binary_path = NULL;
    }
    
    if (app_index > 0 && app_index <= manager->available_apps_count) {
        int array_index = app_index - 1;
        if (manager->available_apps[array_index].binary_path) {
            manager->default_binary_path = strdup(manager->available_apps[array_index].binary_path);
        }
    }
}

const char* get_app_name_from_path(AppManager *manager, const char *path) {
    if (!path || !manager->available_apps) return NULL;
    
    for (int i = 0; i < manager->available_apps_count; i++) {
        if (manager->available_apps[i].binary_path && 
            strcmp(manager->available_apps[i].binary_path, path) == 0) {
            return manager->available_apps[i].name;
        }
    }
    return NULL;
}
