/*
 * privileged_exec.h — Run commands via sudo using the root credential vault.
 */

#ifndef PRIVILEGED_EXEC_H
#define PRIVILEGED_EXEC_H

#include <sys/types.h>
#include <glib.h>
#include "service_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

gboolean privileged_run_sudo_argv(char *const argv[], int *exit_status,
                                char *errmsg, size_t errmsg_len);

gboolean privileged_run_sudo_argv_with_password(const char *password,
                                                char *const argv[],
                                                int *exit_status,
                                                char *errmsg, size_t errmsg_len);

gboolean privileged_kill(pid_t pid, int signal_num);
gboolean privileged_kill_with_password(const char *password, pid_t pid, int signal_num);
gboolean privileged_set_priority(pid_t pid, int prio);
gboolean privileged_set_priority_with_password(const char *password, pid_t pid, int prio);
int privileged_systemd_service_control(const char *service_name, ServiceAction action);
int privileged_systemd_service_control_with_password(const char *password,
                                                     const char *service_name,
                                                     ServiceAction action);
int privileged_systemd_service_enable_disable(const char *service_name, int enable);
int privileged_systemd_service_enable_disable_with_password(const char *password,
                                                              const char *service_name,
                                                              int enable);
gboolean privileged_disconnect_user_sessions(const char *username);
gboolean privileged_disconnect_user_sessions_with_password(const char *password,
                                                           const char *username);
gboolean privileged_move_file(const char *src, const char *dst);
gboolean privileged_move_file_with_password(const char *password,
                                            const char *src, const char *dst);

#ifdef __cplusplus
}
#endif

#endif /* PRIVILEGED_EXEC_H */
