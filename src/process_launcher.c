/*
 * process_launcher.c — Spawn external programs without a shell.
 */

#include "process_launcher.h"
#include "app_manager.h"

#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

static char* shell_escape(const char* str) {
    if (!str) return strdup("");
    char *escaped = malloc(strlen(str) * 4 + 1);
    char *dst = escaped;
    const char *src = str;
    while (*src) {
        if (*src == '\'') {
            *dst++ = '\'';
            *dst++ = '\\';
            *dst++ = '\'';
            *dst++ = '\'';
        } else {
            *dst++ = *src;
        }
        src++;
    }
    *dst = '\0';
    return escaped;
}

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

    const char *editor = NULL;
    if (default_editor_binary_path && default_editor_binary_path[0]) {
        editor = default_editor_binary_path;
    } else if (available_editors_count > 0 && available_editors[0].binary_path) {
        editor = available_editors[0].binary_path;
    }

    if (editor) {
        argv[0] = (char *)editor;
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

gboolean taskmgr_execute_program_advanced(const char *program,
                                          gboolean in_terminal,
                                          gboolean as_user,
                                          const char *username,
                                          const char *password)
{
    if (!program || !program[0])
        return FALSE;

    // Get the terminal path if running in terminal
    const char *terminal = NULL;
    if (in_terminal) {
        if (default_terminal_binary_path && default_terminal_binary_path[0]) {
            terminal = default_terminal_binary_path;
        } else if (available_terminals_count > 0 && available_terminals[0].binary_path) {
            terminal = available_terminals[0].binary_path;
        } else {
            terminal = "xterm"; // fallback
        }
    }

    if (in_terminal) {
        // Case B and Case C: Run in terminal
        char *escaped_program = shell_escape(program);
        char *terminal_cmd = NULL;

        if (as_user && username && username[0]) {
            char *escaped_password = shell_escape(password ? password : "");
            // Build the command line: echo 'password' | sudo -u 'username' -S sh -c 'program'
            terminal_cmd = g_strdup_printf("echo '%s' | sudo -u '%s' -S sh -c '%s'",
                                           escaped_password, username, escaped_program);
            free(escaped_password);
        } else {
            terminal_cmd = g_strdup_printf("sh -c '%s'", escaped_program);
        }
        free(escaped_program);

        // Now run terminal with terminal_cmd
        char *argv[6];
        argv[0] = (char *)terminal;
        argv[1] = "-e";
        argv[2] = "sh";
        argv[3] = "-c";
        argv[4] = terminal_cmd;
        argv[5] = NULL;

        gboolean ok = taskmgr_spawn_async_argv(argv);
        g_free(terminal_cmd);
        return ok;
    } else {
        // No terminal
        if (as_user && username && username[0]) {
            // Case A: Run as user (no terminal)
            // Parse program into arguments using glib shell parse
            gint argc;
            gchar **cmd_argv = NULL;
            GError *error = NULL;
            gboolean parsed = g_shell_parse_argv(program, &argc, &cmd_argv, &error);

            if (!parsed || !cmd_argv) {
                // If parsing fails, run via sh -c
                gint stdin_pipe = -1;
                char *sudo_argv[11];
                sudo_argv[0] = "sudo";
                sudo_argv[1] = "-u";
                sudo_argv[2] = (char *)username;
                sudo_argv[3] = "-S";
                sudo_argv[4] = "-p";
                sudo_argv[5] = "";
                sudo_argv[6] = "--";
                sudo_argv[7] = "sh";
                sudo_argv[8] = "-c";
                sudo_argv[9] = (char *)program;
                sudo_argv[10] = NULL;

                gboolean ok = g_spawn_async_with_pipes(
                    NULL, sudo_argv, NULL,
                    G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                    NULL, NULL, NULL, &stdin_pipe, NULL, NULL, &error
                );
                if (ok && stdin_pipe != -1) {
                    if (password) {
                        write(stdin_pipe, password, strlen(password));
                        write(stdin_pipe, "\n", 1);
                    }
                    close(stdin_pipe);
                }
                if (error) g_error_free(error);
                return ok;
            } else {
                // Succeeded parsing cmd_argv
                int si = 0;
                char *sudo_argv[128];
                sudo_argv[si++] = "sudo";
                sudo_argv[si++] = "-u";
                sudo_argv[si++] = (char *)username;
                sudo_argv[si++] = "-S";
                sudo_argv[si++] = "-p";
                sudo_argv[si++] = "";
                sudo_argv[si++] = "--";
                for (int i = 0; cmd_argv[i] && si < 120; i++) {
                    sudo_argv[si++] = cmd_argv[i];
                }
                sudo_argv[si] = NULL;

                gint stdin_pipe = -1;
                gboolean ok = g_spawn_async_with_pipes(
                    NULL, sudo_argv, NULL,
                    G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                    NULL, NULL, NULL, &stdin_pipe, NULL, NULL, &error
                );
                if (ok && stdin_pipe != -1) {
                    if (password) {
                        write(stdin_pipe, password, strlen(password));
                        write(stdin_pipe, "\n", 1);
                    }
                    close(stdin_pipe);
                }
                g_strfreev(cmd_argv);
                if (error) g_error_free(error);
                return ok;
            }
        } else {
            // Case 1: Normal run (no terminal, no user)
            return taskmgr_execute_program(program);
        }
    }
}

gboolean taskmgr_launch_edit_file_with_password(const char *password, const char *file_path)
{
    if (!file_path || !file_path[0])
        return FALSE;

    const char *editor = NULL;
    if (default_editor_binary_path && default_editor_binary_path[0]) {
        editor = default_editor_binary_path;
    } else if (available_editors_count > 0 && available_editors[0].binary_path) {
        editor = available_editors[0].binary_path;
    }

    char *sudo_argv[8];
    sudo_argv[0] = "sudo";
    sudo_argv[1] = "-u";
    sudo_argv[2] = "root";
    sudo_argv[3] = "-S";
    sudo_argv[4] = "-p";
    sudo_argv[5] = "";
    sudo_argv[6] = "--";

    if (editor) {
        sudo_argv[7] = (char *)editor;
    } else {
        sudo_argv[7] = "xdg-open";
    }

    char *argv_full[10];
    for (int i = 0; i < 8; i++) {
        argv_full[i] = sudo_argv[i];
    }
    argv_full[8] = (char *)file_path;
    argv_full[9] = NULL;

    gint stdin_pipe = -1;
    GError *error = NULL;
    gboolean ok = g_spawn_async_with_pipes(
        NULL, argv_full, NULL,
        G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
        NULL, NULL, NULL, &stdin_pipe, NULL, NULL, &error
    );

    if (ok && stdin_pipe != -1) {
        if (password) {
            write(stdin_pipe, password, strlen(password));
            write(stdin_pipe, "\n", 1);
        }
        close(stdin_pipe);
    }
    if (error) {
        g_error_free(error);
    }
    return ok;
}
