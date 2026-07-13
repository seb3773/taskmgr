/*
 * root_credential_vault.c — Encrypted root password vault for taskmgr.
 */

#include "root_credential_vault.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/mman.h>

#define VAULT_MAX_PASS ROOT_VAULT_MAX_PASS

static unsigned char s_key[32];
static unsigned char s_encrypted[VAULT_MAX_PASS];
static size_t s_pass_len;
static int s_key_ready;
static RootElevationKind s_kind = ROOT_ELEVATION_NONE;

static void ensure_key(void)
{
    if (s_key_ready)
        return;

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        read(fd, s_key, sizeof(s_key));
        close(fd);
    } else {
        for (size_t i = 0; i < sizeof(s_key); ++i)
            s_key[i] = (unsigned char)(0xA5 ^ (i * 17));
    }

    mlock(s_key, sizeof(s_key));
    s_key_ready = 1;
}

void root_vault_secure_zero(void *ptr, size_t len)
{
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len--)
        *p++ = 0;
}

static void vault_clear_storage(void)
{
    root_vault_secure_zero(s_encrypted, sizeof(s_encrypted));
    root_vault_secure_zero(s_key, sizeof(s_key));
    s_pass_len = 0;
    s_key_ready = 0;
    s_kind = ROOT_ELEVATION_NONE;
}

static void vault_store(const char *password)
{
    ensure_key();
    s_pass_len = strlen(password);
    if (s_pass_len >= VAULT_MAX_PASS)
        s_pass_len = VAULT_MAX_PASS - 1;

    for (size_t i = 0; i < s_pass_len; ++i)
        s_encrypted[i] = (unsigned char)password[i] ^ s_key[i % sizeof(s_key)];

    mlock(s_encrypted, sizeof(s_encrypted));
    s_kind = ROOT_ELEVATION_FULL;
}

static gboolean sudo_validate_password(const char *password)
{
    int pipefd[2];
    if (pipe(pipefd) != 0)
        return FALSE;

    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        execlp("sudo", "sudo", "-S", "-k", "-p", "", "true", (char *)NULL);
        _exit(127);
    }
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return FALSE;
    }

    close(pipefd[0]);
    dprintf(pipefd[1], "%s\n", password);
    close(pipefd[1]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        return FALSE;

    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

gboolean root_mode_is_active(void)
{
    return (s_kind == ROOT_ELEVATION_FULL && s_pass_len > 0) || (geteuid() == 0);
}

RootElevationKind root_mode_kind(void)
{
    if (geteuid() == 0) {
        return ROOT_ELEVATION_FULL;
    }
    return s_kind;
}

gboolean root_mode_activate_with_password(const char *password,
                                          char *errmsg, size_t errmsg_len)
{
    if (!password || !password[0]) {
        if (errmsg && errmsg_len)
            g_strlcpy(errmsg, "Password required.", errmsg_len);
        return FALSE;
    }

    if (!sudo_validate_password(password)) {
        if (errmsg && errmsg_len)
            g_strlcpy(errmsg, "Invalid password.", errmsg_len);
        return FALSE;
    }

    if (root_mode_is_active())
        vault_clear_storage();

    vault_store(password);
    return TRUE;
}

void root_mode_deactivate(void)
{
    vault_clear_storage();
}

gboolean root_validate_password(const char *password,
                                char *errmsg, size_t errmsg_len)
{
    if (!password || !password[0]) {
        if (errmsg && errmsg_len)
            g_strlcpy(errmsg, "Password required.", errmsg_len);
        return FALSE;
    }

    if (!sudo_validate_password(password)) {
        if (errmsg && errmsg_len)
            g_strlcpy(errmsg, "Invalid password.", errmsg_len);
        return FALSE;
    }

    return TRUE;
}

gboolean root_vault_borrow_password(char *out, size_t out_size)
{
    if (!root_mode_is_active() || !out || out_size == 0)
        return FALSE;
    if (out_size <= s_pass_len)
        return FALSE;

    for (size_t i = 0; i < s_pass_len; ++i)
        out[i] = (char)(s_encrypted[i] ^ s_key[i % sizeof(s_key)]);
    out[s_pass_len] = '\0';
    return TRUE;
}
