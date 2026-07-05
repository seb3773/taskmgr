/*
 * user_disconnect.c — Disconnect logged-in user sessions via systemd/logind.
 */

#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>
#include "root_credential_vault.h"
#include "privileged_exec.h"

gboolean disconnect_user_sessions(const char *username)
{
    if (root_mode_is_active())
        return privileged_disconnect_user_sessions(username);

    sd_bus *bus = NULL;
    sd_bus_message *msg = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int r;
    gboolean success = TRUE;

    if (!username || !*username)
        return FALSE;

    r = sd_bus_open_system(&bus);
    if (r < 0)
        return FALSE;

    r = sd_bus_call_method(bus,
                           "org.freedesktop.login1",
                           "/org/freedesktop/login1",
                           "org.freedesktop.login1.Manager",
                           "ListSessions",
                           &error,
                           &msg,
                           "");
    if (r < 0) {
        sd_bus_error_free(&error);
        sd_bus_unref(bus);
        return FALSE;
    }

    r = sd_bus_message_enter_container(msg, SD_BUS_TYPE_ARRAY, "(susso)");
    if (r < 0)
        goto finish;

    while ((r = sd_bus_message_enter_container(msg, SD_BUS_TYPE_STRUCT, "susso")) > 0) {
        const char *session_id, *user, *seat, *objpath;
        uint32_t uid;

        r = sd_bus_message_read(msg, "susso", &session_id, &uid, &user, &seat, &objpath);
        if (r < 0)
            break;

        if (strcmp(user, username) == 0) {
            sd_bus_message *terminate_msg = NULL;
            sd_bus_error terminate_error = SD_BUS_ERROR_NULL;

            r = sd_bus_call_method(bus,
                                   "org.freedesktop.login1",
                                   objpath,
                                   "org.freedesktop.login1.Session",
                                   "Terminate",
                                   &terminate_error,
                                   &terminate_msg,
                                   "");

            if (r < 0)
                success = FALSE;

            sd_bus_error_free(&terminate_error);
            if (terminate_msg)
                sd_bus_message_unref(terminate_msg);
        }

        sd_bus_message_exit_container(msg);
    }
    sd_bus_message_exit_container(msg);

finish:
    sd_bus_error_free(&error);
    if (msg)
        sd_bus_message_unref(msg);
    sd_bus_unref(bus);
    return success;
}
