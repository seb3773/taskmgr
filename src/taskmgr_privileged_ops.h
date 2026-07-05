/*
 * taskmgr_privileged_ops.h — Ephemeral root elevation for privileged UI actions.
 */

#ifndef TASKMGR_PRIVILEGED_OPS_H
#define TASKMGR_PRIVILEGED_OPS_H

#include <sys/types.h>
#include <glib.h>

#include "service_manager.h"
#include "autostart_manager.h"

class TQWidget;
class TQString;

class TaskmgrPrivilegedOps {
public:
    static gboolean killProcess(TQWidget* parent, pid_t pid, int signal);
    static gboolean setProcessPriority(TQWidget* parent, pid_t pid, int prio);
    static int serviceControl(TQWidget* parent, const char* serviceName, ServiceAction action);
    static int serviceEnableDisable(TQWidget* parent, const char* serviceName, int enable);
    static gboolean disconnectUser(TQWidget* parent, const char* username);
    static ToggleResult toggleAutostart(TQWidget* parent, const char* filepath,
                                        int enable, char* message);
};

#endif /* TASKMGR_PRIVILEGED_OPS_H */
