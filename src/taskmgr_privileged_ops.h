/*
 * taskmgr_privileged_ops.h — Ephemeral root elevation for privileged UI actions.
 */

#ifndef TASKMGR_PRIVILEGED_OPS_H
#define TASKMGR_PRIVILEGED_OPS_H

#include <sys/types.h>
#include <glib.h>

#include "service_manager.h"
#include "autostart_manager.h"
#include "cron_manager.h"

class TQWidget;
class TQString;

class TaskmgrPrivilegedOps {
public:
    static gboolean killProcess(TQWidget* parent, pid_t pid, int signal);
    static gboolean setProcessPriority(TQWidget* parent, pid_t pid, int prio);
    static int serviceControl(TQWidget* parent, const char* serviceName, ServiceAction action);
    static int serviceEnableDisable(TQWidget* parent, const char* serviceName, int enable);
    static gboolean disconnectUser(TQWidget* parent, const char* username);
    static gboolean editService(TQWidget* parent, const char* serviceName);
    static gboolean editAutostart(TQWidget* parent, const char* filepath);
    static gboolean deleteAutostart(TQWidget* parent, const char* filepath);
    static ToggleResult toggleAutostart(TQWidget* parent, const char* filepath,
                                        int enable, char* message);
    static int toggleCronEntry(TQWidget* parent, const CronEntry* entry,
                               int enable, char* errmsg, size_t errmsg_len);
    static int deleteCronEntry(TQWidget* parent, const CronEntry* entry,
                               char* errmsg, size_t errmsg_len);
    static gboolean editCronFile(TQWidget* parent, const char* filepath);
};

#endif /* TASKMGR_PRIVILEGED_OPS_H */
