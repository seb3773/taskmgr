/*
 * privileged_exec.c — Run commands via sudo using the root credential vault.
 */

#include "privileged_exec.h"
#include "root_credential_vault.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <signal.h>

static char privileged_self_path[PATH_MAX];

static const char *privileged_self_executable(void)
{
    if (privileged_self_path[0])
        return privileged_self_path;

    ssize_t len = readlink("/proc/self/exe", privileged_self_path,
                           sizeof(privileged_self_path) - 1);
    if (len <= 0)
        return NULL;
    privileged_self_path[len] = '\0';
    return privileged_self_path;
}

static gboolean run_sudo_with_password(const char *password,
                                       char *const argv[],
                                       int *exit_status,
                                       char *errmsg, size_t errmsg_len)
{
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        if (errmsg && errmsg_len)
            g_strlcpy(errmsg, "Failed to create pipe.", errmsg_len);
        return FALSE;
    }

    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        execvp("sudo", argv);
        _exit(127);
    }
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        if (errmsg && errmsg_len)
            g_strlcpy(errmsg, "Failed to fork sudo.", errmsg_len);
        return FALSE;
    }

    close(pipefd[0]);
    dprintf(pipefd[1], "%s\n", password);
    close(pipefd[1]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        if (errmsg && errmsg_len)
            g_strlcpy(errmsg, "Failed waiting for sudo.", errmsg_len);
        return FALSE;
    }

    if (exit_status)
        *exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (errmsg && errmsg_len)
            g_strlcpy(errmsg, "Privileged command failed.", errmsg_len);
        return FALSE;
    }

    return TRUE;
}

static gboolean run_sudo_wrapper_with_password(const char *password,
                                               char *const argv[],
                                               int *exit_status,
                                               char *errmsg,
                                               size_t errmsg_len)
{
    char *wrapper[72];
    int wi = 0;
    wrapper[wi++] = "sudo";
    wrapper[wi++] = "-S";
    wrapper[wi++] = "-p";
    wrapper[wi++] = "";

    for (int i = 0; argv && argv[i] && wi < (int)(sizeof(wrapper) / sizeof(wrapper[0]) - 1); ++i)
        wrapper[wi++] = argv[i];
    wrapper[wi] = NULL;

    return run_sudo_with_password(password, wrapper, exit_status, errmsg, errmsg_len);
}

gboolean privileged_run_sudo_argv_with_password(const char *password,
                                                char *const argv[],
                                                int *exit_status,
                                                char *errmsg,
                                                size_t errmsg_len)
{
    if (!password || !password[0]) {
        if (errmsg && errmsg_len)
            g_strlcpy(errmsg, "Password required.", errmsg_len);
        return FALSE;
    }

    return run_sudo_wrapper_with_password(password, argv, exit_status, errmsg, errmsg_len);
}

gboolean privileged_run_sudo_argv(char *const argv[],
                                  int *exit_status,
                                  char *errmsg, size_t errmsg_len)
{
    if (!root_mode_is_active()) {
        if (errmsg && errmsg_len)
            g_strlcpy(errmsg, "Root mode is not active.", errmsg_len);
        return FALSE;
    }

    char password[ROOT_VAULT_MAX_PASS];
    if (!root_vault_borrow_password(password, sizeof(password))) {
        if (errmsg && errmsg_len)
            g_strlcpy(errmsg, "Root credentials unavailable.", errmsg_len);
        return FALSE;
    }

    gboolean ok = run_sudo_wrapper_with_password(password, argv, exit_status, errmsg, errmsg_len);
    root_vault_secure_zero(password, sizeof(password));
    return ok;
}

static gboolean privileged_kill_impl(const char *password, pid_t pid, int signal_num)
{
    if (pid <= 0 || signal_num == 0)
        return FALSE;

    char sig_name[32];
    switch (signal_num) {
        case SIGTERM: g_strlcpy(sig_name, "TERM", sizeof(sig_name)); break;
        case SIGKILL: g_strlcpy(sig_name, "KILL", sizeof(sig_name)); break;
        case SIGSTOP: g_strlcpy(sig_name, "STOP", sizeof(sig_name)); break;
        case SIGCONT: g_strlcpy(sig_name, "CONT", sizeof(sig_name)); break;
        default:
            g_snprintf(sig_name, sizeof(sig_name), "%d", signal_num);
            break;
    }

    char pid_str[16];
    g_snprintf(pid_str, sizeof(pid_str), "%d", (int)pid);

    char *argv[] = {"kill", "-s", sig_name, pid_str, NULL};
    if (password)
        return privileged_run_sudo_argv_with_password(password, argv, NULL, NULL, 0);
    return privileged_run_sudo_argv(argv, NULL, NULL, 0);
}

gboolean privileged_kill(pid_t pid, int signal_num)
{
    return privileged_kill_impl(NULL, pid, signal_num);
}

gboolean privileged_kill_with_password(const char *password, pid_t pid, int signal_num)
{
    return privileged_kill_impl(password, pid, signal_num);
}

static gboolean privileged_set_priority_impl(const char *password, pid_t pid, int prio)
{
    if (pid <= 0)
        return FALSE;

    if (geteuid() == 0)
        return setpriority(PRIO_PROCESS, (id_t)pid, prio) == 0;

    const char *self = privileged_self_executable();
    if (!self)
        return FALSE;

    char pid_str[16];
    char prio_str[16];
    g_snprintf(pid_str, sizeof(pid_str), "%d", (int)pid);
    g_snprintf(prio_str, sizeof(prio_str), "%d", prio);

    char *argv[] = { (char *)self, "--set-priority", pid_str, prio_str, NULL };
    if (password)
        return privileged_run_sudo_argv_with_password(password, argv, NULL, NULL, 0);
    return privileged_run_sudo_argv(argv, NULL, NULL, 0);
}

gboolean privileged_set_priority(pid_t pid, int prio)
{
    return privileged_set_priority_impl(NULL, pid, prio);
}

gboolean privileged_set_priority_with_password(const char *password, pid_t pid, int prio)
{
    return privileged_set_priority_impl(password, pid, prio);
}

static int privileged_systemd_service_control_impl(const char *password,
                                                   const char *service_name,
                                                   ServiceAction action)
{
    if (!service_name)
        return -1;

    const char *verb;
    switch (action) {
        case SERVICE_ACTION_START:   verb = "start"; break;
        case SERVICE_ACTION_STOP:    verb = "stop"; break;
        case SERVICE_ACTION_RESTART: verb = "restart"; break;
        default: return -1;
    }

    char *argv[] = {"systemctl", (char *)verb, (char *)service_name, NULL};
    if (password) {
        if (privileged_run_sudo_argv_with_password(password, argv, NULL, NULL, 0))
            return 0;
        return -1;
    }
    if (privileged_run_sudo_argv(argv, NULL, NULL, 0))
        return 0;
    return -1;
}

int privileged_systemd_service_control(const char *service_name, ServiceAction action)
{
    return privileged_systemd_service_control_impl(NULL, service_name, action);
}

int privileged_systemd_service_control_with_password(const char *password,
                                                     const char *service_name,
                                                     ServiceAction action)
{
    return privileged_systemd_service_control_impl(password, service_name, action);
}

static int privileged_systemd_service_enable_disable_impl(const char *password,
                                                          const char *service_name,
                                                          int enable)
{
    if (!service_name)
        return -1;

    const char *verb = enable ? "enable" : "disable";
    char *argv[] = {"systemctl", (char *)verb, (char *)service_name, NULL};
    if (password) {
        if (privileged_run_sudo_argv_with_password(password, argv, NULL, NULL, 0))
            return 0;
        return -1;
    }
    if (privileged_run_sudo_argv(argv, NULL, NULL, 0))
        return 0;
    return -1;
}

int privileged_systemd_service_enable_disable(const char *service_name, int enable)
{
    return privileged_systemd_service_enable_disable_impl(NULL, service_name, enable);
}

int privileged_systemd_service_enable_disable_with_password(const char *password,
                                                              const char *service_name,
                                                              int enable)
{
    return privileged_systemd_service_enable_disable_impl(password, service_name, enable);
}

static gboolean privileged_disconnect_user_sessions_impl(const char *password,
                                                       const char *username)
{
    if (!username || !*username)
        return FALSE;

    char *argv[] = {"loginctl", "terminate-user", (char *)username, NULL};
    if (password)
        return privileged_run_sudo_argv_with_password(password, argv, NULL, NULL, 0);
    return privileged_run_sudo_argv(argv, NULL, NULL, 0);
}

gboolean privileged_disconnect_user_sessions(const char *username)
{
    return privileged_disconnect_user_sessions_impl(NULL, username);
}

gboolean privileged_disconnect_user_sessions_with_password(const char *password,
                                                           const char *username)
{
    return privileged_disconnect_user_sessions_impl(password, username);
}

static gboolean privileged_move_file_impl(const char *password,
                                          const char *src, const char *dst)
{
    if (!src || !dst)
        return FALSE;

    char *argv[] = {"mv", (char *)src, (char *)dst, NULL};
    if (password)
        return privileged_run_sudo_argv_with_password(password, argv, NULL, NULL, 0);
    return privileged_run_sudo_argv(argv, NULL, NULL, 0);
}

gboolean privileged_move_file(const char *src, const char *dst)
{
    return privileged_move_file_impl(NULL, src, dst);
}

gboolean privileged_move_file_with_password(const char *password,
                                            const char *src, const char *dst)
{
    return privileged_move_file_impl(password, src, dst);
}
