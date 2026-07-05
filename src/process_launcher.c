/*
 * process_launcher.c — Spawn external programs without a shell.
 */

#include "process_launcher.h"
#include "app_manager.h"

#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

gboolean taskmgr_spawn_async_argv(char **argv)
{
    GError *error = NULL;

    if (!argv || !argv[0])
        return FALSE;

    if (!g_spawn_async(NULL, argv, NULL,
                       G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                       NULL, NULL, NULL, &error)) {
        if (error)
            g_error_free(error);
        return FALSE;
    }

    return TRUE;
}

gboolean taskmgr_launch_open_directory(const char *dir_path)
{
    char *argv[3];

    if (!dir_path || !dir_path[0])
        return FALSE;

    argv[0] = "xdg-open";
    argv[1] = (char *)dir_path;
    argv[2] = NULL;
    return taskmgr_spawn_async_argv(argv);
}

gboolean taskmgr_launch_open_url(const char *url)
{
    char *argv[3];

    if (!url || !url[0])
        return FALSE;

    if (default_browser_binary_path && default_browser_binary_path[0]) {
        argv[0] = default_browser_binary_path;
        argv[1] = (char *)url;
        argv[2] = NULL;
        return taskmgr_spawn_async_argv(argv);
    }

    argv[0] = "xdg-open";
    argv[1] = (char *)url;
    argv[2] = NULL;
    return taskmgr_spawn_async_argv(argv);
}

gboolean taskmgr_launch_edit_file(const char *file_path)
{
    char *argv[3];

    if (!file_path || !file_path[0])
        return FALSE;

    if (default_editor_binary_path && default_editor_binary_path[0]) {
        argv[0] = default_editor_binary_path;
        argv[1] = (char *)file_path;
        argv[2] = NULL;
        return taskmgr_spawn_async_argv(argv);
    }

    argv[0] = "xdg-open";
    argv[1] = (char *)file_path;
    argv[2] = NULL;
    return taskmgr_spawn_async_argv(argv);
}

gboolean taskmgr_execute_program(const char *program)
{
    gint argc;
    gchar **argv = NULL;
    GError *error = NULL;
    gboolean parsed;

    if (!program || !program[0])
        return FALSE;

    parsed = g_shell_parse_argv(program, &argc, &argv, &error);

    pid_t pid = fork();
    if (pid == 0) {
        if (parsed && argv) {
            freopen("/dev/null", "w", stdout);
            freopen("/dev/null", "w", stderr);
            execvp(argv[0], argv);
            _exit(1);
        } else {
            execl("/bin/sh", "sh", "-c", program, (char *)NULL);
            _exit(1);
        }
    } else if (pid < 0) {
        if (argv)
            g_strfreev(argv);
        if (error)
            g_error_free(error);
        return FALSE;
    }

    if (argv)
        g_strfreev(argv);
    if (error)
        g_error_free(error);
    return TRUE;
}
