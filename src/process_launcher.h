/*
 * process_launcher.h — Spawn external programs without a shell.
 */

#ifndef PROCESS_LAUNCHER_H
#define PROCESS_LAUNCHER_H

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

gboolean taskmgr_spawn_async_argv(char **argv);
gboolean taskmgr_launch_open_directory(const char *dir_path);
gboolean taskmgr_launch_open_url(const char *url);
gboolean taskmgr_launch_edit_file(const char *file_path);
gboolean taskmgr_launch_edit_file_with_password(const char *password, const char *file_path);
gboolean taskmgr_execute_program(const char *program);
gboolean taskmgr_execute_program_advanced(const char *program,
                                          gboolean in_terminal,
                                          gboolean as_user,
                                          const char *username,
                                          const char *password);

#ifdef __cplusplus
}
#endif

#endif /* PROCESS_LAUNCHER_H */
