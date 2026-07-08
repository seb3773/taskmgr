/*
 * main.cpp — Entry point for the TQt3 taskmgr port.
 *
 * Initializes the C backend subsystems, loads configuration,
 * enforces a single running instance via KUniqueApplication, and runs
 * the TQt3 application event loop.
 */

#include <ntqmessagebox.h>
#include <ntqtimer.h>
#include <ntqapplication.h>
#include <tdeapplication.h>
#include <tdeaboutdata.h>
#include <tdecmdlineargs.h>
#include <kuniqueapplication.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <pwd.h>
#include <limits.h>
#include <sys/resource.h>

#include "preferences_dialog.h"
#include "taskmgr_mainwindow.h"
#include "backend_bridge.h"

// Global config path defined in backend C code
extern "C" gchar *config_file;
extern "C" gdouble installed_ram_gib;

static int run_privileged_helper(int argc, char **argv)
{
    if (argc < 4 || strcmp(argv[1], "--set-priority") != 0)
        return -1;
    if (getuid() != 0)
        return 1;

    pid_t pid = (pid_t)atoi(argv[2]);
    int prio = atoi(argv[3]);
    if (pid <= 0)
        return 1;

    return setpriority(PRIO_PROCESS, (id_t)pid, prio) == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    int helper_rc = run_privileged_helper(argc, argv);
    if (helper_rc >= 0)
        return helper_rc;

    char *custom_config_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-version") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("taskmgr ★ TQt3 linux task manager ala windows10\n");
            return 0;
        } else if (strcmp(argv[i], "-help") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("taskmgr ★ TQt3 linux task manager ala windows10\n\n");
            printf("Usage: taskmgr [OPTIONS]\n"
                   "Options:\n"
                   "  --config=<path>          Use custom config file\n"
                   "  --version                Show version and quit\n"
                   "  --help                   Show this help message\n");
            return 0;
        } else if (strncmp(argv[i], "-config=", 8) == 0) {
            custom_config_path = argv[i] + 8;
        } else if (strncmp(argv[i], "--config=", 9) == 0) {
            custom_config_path = argv[i] + 9;
        }
    }

    // Set config file path
    config_file = (gchar*)g_malloc(PATH_MAX);
    if (custom_config_path && strlen(custom_config_path) > 0) {
        snprintf(config_file, PATH_MAX, "%s", custom_config_path);
    } else {
        const char *home_dir = getenv("HOME");
        if (!home_dir) {
            struct passwd *pw = getpwuid(getuid());
            home_dir = pw ? pw->pw_dir : "/tmp";
        }
        char config_dir[PATH_MAX];
        snprintf(config_dir, sizeof(config_dir), "%s/.config", home_dir);
        mkdir(config_dir, 0755);
        snprintf(config_file, PATH_MAX, "%s/.config/taskmgr.conf", home_dir);
    }

    // Initialize backend caches and preferences
    load_config();

    TDEAboutData aboutData("taskmgr", I18N_NOOP("Task Manager"), "1.0",
                           I18N_NOOP("Task Manager"), TDEAboutData::License_GPL);
    TDECmdLineArgs::init(argc, argv, &aboutData);
    KUniqueApplication::addCmdLineOptions();

    // Always allow only one instance; a second launch activates the running one.
    if (!KUniqueApplication::start())
        return 0;

    // Scan available editors and browsers
    scan_available_apps(&editor_manager);
    scan_available_apps(&browser_manager);
    scan_available_apps(&terminal_manager);

    // Initialize C backend subsystems
    init_uid_cache();
    init_pss_thread();
    set_optimization_flag(OPTIMIZATION_FLAG_PSS_LOADING, TRUE);
    
    // Initialize backend structures and data
    bridge_init_backend();

    // Core CPU & RAM initialization
    get_cpu_speed(); // triggers jiffies and speed caching

    // Initialize device managers
    init_disk_manager();
    init_network_manager();
    gpu_stats_init();

    KUniqueApplication app;

    // Apply custom color theme if set
    applyAppPalette();

    // Create the Main Window
    TaskMgrMainWindow *mw = new TaskMgrMainWindow();
    app.setMainWidget(mw);
    mw->show();
    if (tqApp)
        tqApp->processEvents();
    mw->runInitialRefresh();

    int ret = app.exec();

    delete mw;

    // Clean up C backend subsystems
    cleanup_uid_cache();
    cleanup_pss_thread();
    g_free(config_file);

    return ret;
}

#include "main.moc"
