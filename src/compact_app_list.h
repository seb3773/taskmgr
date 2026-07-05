/*
 * compact_app_list.h — Build the simplified "Less details" process list.
 *
 * Groups visible X11 windows into one row per window (Chrome-like apps
 * collapse to a single entry per window). Falls back to user processes
 * with a distinct application icon when no window mapping exists.
 */

#ifndef COMPACT_APP_LIST_H
#define COMPACT_APP_LIST_H

#include <glib.h>
#include <sys/types.h>
#include <ntqstring.h>
#include <ntqvaluelist.h>
#include <ntqvaluevector.h>

struct CompactAppEntry {
    pid_t repPid;
    TQString displayName;
    TQString iconKey;
    TQString wmClass;
    TQString wmInstance;
    TQValueVector<int> taskIndices;
};

class CompactAppList {
public:
    static TQValueList<CompactAppEntry> build(const GArray* tasks, uid_t ownUid);
};

#endif
