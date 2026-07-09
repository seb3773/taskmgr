/*
 * taskmgr_privileged_ops.cpp — Ephemeral root elevation for privileged UI actions.
 */

#include "taskmgr_privileged_ops.h"
#include "root_password_dialog.h"
#include "root_credential_vault.h"
#include "privileged_policy.h"
#include "privileged_exec.h"
#include "backend_bridge.h"
#include "process_launcher.h"

#include <ntqmessagebox.h>
#include <ntqstring.h>
#include <ntqcstring.h>

typedef gboolean (*EphemeralActionFn)(const char *password, void *ctx);

struct KillCtx {
    pid_t pid;
    int signal;
};

struct PrioCtx {
    pid_t pid;
    int prio;
};

struct ServiceCtx {
    const char *name;
    ServiceAction action;
    int result;
};

struct ServiceEnableCtx {
    const char *name;
    int enable;
    int result;
};

struct DisconnectCtx {
    const char *username;
    gboolean ok;
};

struct AutostartCtx {
    const char *filepath;
    int enable;
    char *message;
    ToggleResult result;
};

struct EditServiceCtx {
    const char *filepath;
    gboolean ok;
};

static gboolean runEphemeral(TQWidget *parent, const TQString &prompt,
                             EphemeralActionFn fn, void *ctx)
{
    RootPasswordDialog dlg(parent, RootPasswordEphemeralMode, prompt);
    if (dlg.exec() != TQDialog::Accepted)
        return FALSE;

    TQCString pass = dlg.password().local8Bit();
    gboolean ok = fn(pass.data(), ctx);
    root_vault_secure_zero(pass.data(), pass.length());
    return ok;
}

static gboolean killWithPassword(const char *password, void *v)
{
    KillCtx *c = (KillCtx *)v;
    return privileged_kill_with_password(password, c->pid, c->signal);
}

static gboolean prioWithPassword(const char *password, void *v)
{
    PrioCtx *c = (PrioCtx *)v;
    return privileged_set_priority_with_password(password, c->pid, c->prio);
}

static gboolean serviceWithPassword(const char *password, void *v)
{
    ServiceCtx *c = (ServiceCtx *)v;
    c->result = privileged_systemd_service_control_with_password(password, c->name, c->action);
    return c->result == 0;
}

static gboolean serviceEnableWithPassword(const char *password, void *v)
{
    ServiceEnableCtx *c = (ServiceEnableCtx *)v;
    c->result = privileged_systemd_service_enable_disable_with_password(password, c->name, c->enable);
    return c->result == 0;
}

static gboolean disconnectWithPassword(const char *password, void *v)
{
    DisconnectCtx *c = (DisconnectCtx *)v;
    c->ok = privileged_disconnect_user_sessions_with_password(password, c->username);
    return c->ok;
}

static gboolean autostartWithPassword(const char *password, void *v)
{
    AutostartCtx *c = (AutostartCtx *)v;
    c->result = toggle_autostart_entry_with_password(password, c->filepath,
                                                       c->enable, c->message);
    return c->result == TOGGLE_SUCCESS;
}

static gboolean editServiceWithPassword(const char *password, void *v)
{
    EditServiceCtx *c = (EditServiceCtx *)v;
    c->ok = taskmgr_launch_edit_file_with_password(password, c->filepath);
    return c->ok;
}

gboolean TaskmgrPrivilegedOps::killProcess(TQWidget *parent, pid_t pid, int signal)
{
    if (root_mode_is_active() || !taskmgr_kill_needs_elevation(pid))
        return send_signal_to_task(pid, signal);

    KillCtx ctx = { pid, signal };
    return runEphemeral(parent,
                        TQString("Administrator password required to control this process."),
                        killWithPassword, &ctx);
}

gboolean TaskmgrPrivilegedOps::setProcessPriority(TQWidget *parent, pid_t pid, int prio)
{
    if (root_mode_is_active() || !taskmgr_priority_needs_elevation(pid, prio)) {
        set_priority_to_task(pid, prio);
        return TRUE;
    }

    PrioCtx ctx = { pid, prio };
    if (runEphemeral(parent,
                     TQString("Administrator password required to change this process priority."),
                     prioWithPassword, &ctx))
        return TRUE;

    TQMessageBox::critical(parent, "Error",
        TQString("Can't set priority %1 to task ID %2.").arg(prio).arg((int)pid));
    return FALSE;
}

int TaskmgrPrivilegedOps::serviceControl(TQWidget *parent, const char *serviceName,
                                         ServiceAction action)
{
    if (root_mode_is_active())
        return systemd_service_control(serviceName, action);

    ServiceCtx ctx = { serviceName, action, -1 };
    if (runEphemeral(parent,
                     TQString("Administrator password required to control system services."),
                     serviceWithPassword, &ctx))
        return ctx.result;

    return -1;
}

int TaskmgrPrivilegedOps::serviceEnableDisable(TQWidget *parent, const char *serviceName,
                                               int enable)
{
    if (root_mode_is_active())
        return systemd_service_enable_disable(serviceName, enable);

    ServiceEnableCtx ctx = { serviceName, enable, -1 };
    if (runEphemeral(parent,
                     TQString("Administrator password required to enable or disable system services."),
                     serviceEnableWithPassword, &ctx))
        return ctx.result;

    return -1;
}

gboolean TaskmgrPrivilegedOps::disconnectUser(TQWidget *parent, const char *username)
{
    if (root_mode_is_active())
        return disconnect_user_sessions(username);

    DisconnectCtx ctx = { username, FALSE };
    if (runEphemeral(parent,
                     TQString("Administrator password required to disconnect user sessions."),
                     disconnectWithPassword, &ctx))
        return ctx.ok;

    return FALSE;
}

ToggleResult TaskmgrPrivilegedOps::toggleAutostart(TQWidget *parent, const char *filepath,
                                                   int enable, char *message)
{
    if (root_mode_is_active() || !taskmgr_autostart_needs_elevation(filepath))
        return toggle_autostart_entry(filepath, enable, message);

    AutostartCtx ctx = { filepath, enable, message, TOGGLE_ERROR_UNKNOWN };
    if (runEphemeral(parent,
                     TQString("Administrator password required to change this startup entry."),
                     autostartWithPassword, &ctx))
        return ctx.result;

    return TOGGLE_ERROR_UNKNOWN;
}

gboolean TaskmgrPrivilegedOps::editService(TQWidget *parent, const char *serviceName)
{
    char fragment_path[512] = "";
    if (get_systemd_service_fragment_path(serviceName, fragment_path, sizeof(fragment_path)) != 0) {
        TQMessageBox::critical(parent, "Error", "Failed to retrieve the service unit file path.");
        return FALSE;
    }

    if (root_mode_is_active())
        return taskmgr_launch_edit_file(fragment_path);

    EditServiceCtx ctx = { fragment_path, FALSE };
    if (runEphemeral(parent,
                     TQString("Administrator password required to edit this service unit file."),
                     editServiceWithPassword, &ctx))
        return ctx.ok;

    return FALSE;
}

gboolean TaskmgrPrivilegedOps::editAutostart(TQWidget *parent, const char *filepath)
{
    if (!filepath || !*filepath)
        return FALSE;

    if (root_mode_is_active() || !taskmgr_autostart_needs_elevation(filepath))
        return taskmgr_launch_edit_file(filepath);

    EditServiceCtx ctx = { filepath, FALSE };
    if (runEphemeral(parent,
                     TQString("Administrator password required to edit this startup entry."),
                     editServiceWithPassword, &ctx))
        return ctx.ok;

    return FALSE;
}
