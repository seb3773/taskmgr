/*
 * compact_app_list.cpp — X11 window enumeration and compact list builder.
 *
 * Uses EWMH _NET_CLIENT_LIST (same source as wmctrl) — no shell commands.
 */

#include "compact_app_list.h"
#include "backend_bridge.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <limits.h>
#include <unistd.h>

#include <ntqmap.h>
#include <ntqvaluevector.h>

struct WindowInfo {
    Window window;
    pid_t pid;
    TQString title;
    TQString wmClass;
    TQString wmInstance;
};

static Display* openDisplay()
{
    const char* displayName = getenv("DISPLAY");
    return XOpenDisplay(displayName ? displayName : 0);
}

static TQString getUtf8Property(Display* dpy, Window win, Atom prop)
{
    Atom actualType;
    int actualFormat;
    unsigned long nitems = 0;
    unsigned long bytesAfter = 0;
    unsigned char* data = 0;

    if (XGetWindowProperty(dpy, win, prop, 0, 1024, False, AnyPropertyType,
                           &actualType, &actualFormat, &nitems, &bytesAfter,
                           &data) != Success || !data || nitems == 0) {
        if (data)
            XFree(data);
        return TQString();
    }

    TQString value;
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    if (actualType == XA_STRING || actualType == utf8)
        value = TQString::fromUtf8((const char*)data);

    XFree(data);
    return value.stripWhiteSpace();
}

static bool getWindowPid(Display* dpy, Window win, pid_t* pidOut)
{
    Atom netWmPid = XInternAtom(dpy, "_NET_WM_PID", False);
    Atom actualType;
    int actualFormat;
    unsigned long nitems = 0;
    unsigned long bytesAfter = 0;
    unsigned char* data = 0;

    if (XGetWindowProperty(dpy, win, netWmPid, 0, 1, False, XA_CARDINAL,
                           &actualType, &actualFormat, &nitems, &bytesAfter,
                           &data) != Success || !data || nitems < 1) {
        if (data)
            XFree(data);
        return false;
    }

    unsigned long raw = *(unsigned long*)data;
    XFree(data);

    if (raw == 0 || raw > (unsigned long)INT_MAX)
        return false;

    *pidOut = (pid_t)raw;
    return true;
}

static bool getWindowPidWithParents(Display* dpy, Window win, pid_t* pidOut)
{
    Window current = win;
    for (int depth = 0; depth < 10; ++depth) {
        if (getWindowPid(dpy, current, pidOut))
            return true;

        Window rootReturn, parentReturn;
        Window* children = 0;
        unsigned int nchildren = 0;
        if (!XQueryTree(dpy, current, &rootReturn, &parentReturn, &children, &nchildren))
            break;
        if (children)
            XFree(children);
        if (parentReturn == rootReturn || parentReturn == 0)
            break;
        current = parentReturn;
    }
    return false;
}

static bool getWmClass(Display* dpy, Window win, TQString* instance, TQString* resClass)
{
    XClassHint hint;
    if (!XGetClassHint(dpy, win, &hint))
        return false;

    if (hint.res_name)
        *instance = hint.res_name;
    if (hint.res_class)
        *resClass = hint.res_class;

    if (hint.res_name)
        XFree(hint.res_name);
    if (hint.res_class)
        XFree(hint.res_class);

    return !resClass->isEmpty();
}

static bool isExcludedWindowType(Display* dpy, Window win)
{
    Atom netWmWindowType = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom actualType;
    int actualFormat;
    unsigned long nitems = 0;
    unsigned long bytesAfter = 0;
    Atom* data = 0;

    if (XGetWindowProperty(dpy, win, netWmWindowType, 0, 16, False, XA_ATOM,
                           &actualType, &actualFormat, &nitems, &bytesAfter,
                           (unsigned char**)&data) != Success || !data || nitems == 0) {
        if (data)
            XFree(data);
        return false;
    }

    Atom dockType = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    Atom desktopType = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    Atom menuType = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_MENU", False);
    Atom tooltipType = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_TOOLTIP", False);
    Atom notificationType = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
    Atom splashType = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_SPLASH", False);

    bool excluded = false;
    for (unsigned long i = 0; i < nitems; ++i) {
        Atom t = data[i];
        if (t == dockType || t == desktopType || t == menuType ||
            t == tooltipType || t == notificationType || t == splashType) {
            excluded = true;
            break;
        }
    }

    XFree(data);
    return excluded;
}

static bool isHiddenWindow(Display* dpy, Window win)
{
    Atom netWmState = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom actualType;
    int actualFormat;
    unsigned long nitems = 0;
    unsigned long bytesAfter = 0;
    Atom* data = 0;

    if (XGetWindowProperty(dpy, win, netWmState, 0, 32, False, XA_ATOM,
                           &actualType, &actualFormat, &nitems, &bytesAfter,
                           (unsigned char**)&data) != Success || !data) {
        if (data)
            XFree(data);
        return false;
    }

    Atom hiddenState = XInternAtom(dpy, "_NET_WM_STATE_HIDDEN", False);
    bool hidden = false;
    for (unsigned long i = 0; i < nitems; ++i) {
        if (data[i] == hiddenState) {
            hidden = true;
            break;
        }
    }

    XFree(data);
    return hidden;
}

static TQValueList<WindowInfo> enumerateClientWindows()
{
    TQValueList<WindowInfo> windows;
    Display* dpy = openDisplay();
    if (!dpy)
        return windows;

    Window root = DefaultRootWindow(dpy);
    Atom netClientList = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    Atom actualType;
    int actualFormat;
    unsigned long nitems = 0;
    unsigned long bytesAfter = 0;
    Window* clientWindows = 0;

    if (XGetWindowProperty(dpy, root, netClientList, 0, 4096, False, XA_WINDOW,
                           &actualType, &actualFormat, &nitems, &bytesAfter,
                           (unsigned char**)&clientWindows) != Success ||
        !clientWindows || nitems == 0) {
        if (clientWindows)
            XFree(clientWindows);
        XCloseDisplay(dpy);
        return windows;
    }

    Atom visibleNameAtom = XInternAtom(dpy, "_NET_WM_VISIBLE_NAME", False);
    Atom wmNameAtom = XInternAtom(dpy, "_NET_WM_NAME", False);

    for (unsigned long i = 0; i < nitems; ++i) {
        Window win = clientWindows[i];
        if (isExcludedWindowType(dpy, win))
            continue;

        pid_t pid = 0;
        if (!getWindowPidWithParents(dpy, win, &pid))
            continue;

        if (pid == getpid())
            continue;

        WindowInfo info;
        info.window = win;
        info.pid = pid;
        info.title = getUtf8Property(dpy, win, visibleNameAtom);
        if (info.title.isEmpty())
            info.title = getUtf8Property(dpy, win, wmNameAtom);
        getWmClass(dpy, win, &info.wmInstance, &info.wmClass);
        windows.append(info);
    }

    XFree(clientWindows);
    XCloseDisplay(dpy);
    return windows;
}

static int findTaskIndexByPid(const GArray* tasks, pid_t pid)
{
    if (!tasks)
        return -1;

    for (guint i = 0; i < tasks->len; ++i) {
        if (bridge_task_pid(tasks, i) == pid)
            return (int)i;
    }
    return -1;
}

static void buildPidMaps(const GArray* tasks,
                         TQMap<pid_t, int>& pidToIndex,
                         TQMap<pid_t, TQValueVector<pid_t> >& childrenByParent)
{
    if (!tasks)
        return;

    for (guint i = 0; i < tasks->len; ++i) {
        pid_t pid = bridge_task_pid(tasks, i);
        pid_t ppid = bridge_task_ppid(tasks, i);
        pidToIndex.insert(pid, (int)i);
        childrenByParent[ppid].push_back(pid);
    }
}

static void collectDescendants(pid_t root, const TQMap<pid_t, TQValueVector<pid_t> >& childrenByParent,
                               TQMap<pid_t, bool>& visited, TQValueVector<int>& outIndices,
                               const TQMap<pid_t, int>& pidToIndex)
{
    if (visited.contains(root))
        return;
    visited.insert(root, true);

    TQMap<pid_t, int>::ConstIterator it = pidToIndex.find(root);
    if (it != pidToIndex.end())
        outIndices.push_back(it.data());

    TQMap<pid_t, TQValueVector<pid_t> >::ConstIterator childIt = childrenByParent.find(root);
    if (childIt == childrenByParent.end())
        return;

    const TQValueVector<pid_t>& children = childIt.data();
    for (unsigned j = 0; j < children.size(); ++j)
        collectDescendants(children[j], childrenByParent, visited, outIndices, pidToIndex);
}

static pid_t findAppRootPid(pid_t pid, const GArray* tasks, const TQMap<pid_t, int>& pidToIndex)
{
    TQMap<pid_t, int>::ConstIterator startIt = pidToIndex.find(pid);
    if (startIt == pidToIndex.end())
        return pid;

    TQString appName = bridge_task_simple_name(tasks, (guint)startIt.data());
    pid_t root = pid;
    pid_t current = pid;

    while (true) {
        TQMap<pid_t, int>::ConstIterator it = pidToIndex.find(current);
        if (it == pidToIndex.end())
            break;
        if (bridge_task_simple_name(tasks, (guint)it.data()) != appName)
            break;

        root = current;
        pid_t ppid = bridge_task_ppid(tasks, (guint)it.data());
        if (ppid <= 1)
            break;

        TQMap<pid_t, int>::ConstIterator parentIt = pidToIndex.find(ppid);
        if (parentIt == pidToIndex.end())
            break;
        if (bridge_task_simple_name(tasks, (guint)parentIt.data()) != appName)
            break;

        current = ppid;
    }

    return root;
}

static void collectAppMembers(pid_t appRoot, const GArray* tasks,
                              const TQMap<pid_t, int>& pidToIndex,
                              const TQMap<pid_t, TQValueVector<pid_t> >& childrenByParent,
                              const TQString& appName, TQValueVector<int>& outIndices)
{
    if (!pidToIndex.contains(appRoot))
        return;

    TQMap<pid_t, bool> visited;
    TQValueVector<pid_t> stack;
    stack.push_back(appRoot);

    while (!stack.isEmpty()) {
        pid_t current = stack.back();
        stack.pop_back();

        if (visited.contains(current))
            continue;
        visited.insert(current, true);

        TQMap<pid_t, int>::ConstIterator it = pidToIndex.find(current);
        if (it == pidToIndex.end())
            continue;

        if (bridge_task_simple_name(tasks, (guint)it.data()) == appName)
            outIndices.push_back(it.data());

        TQMap<pid_t, TQValueVector<pid_t> >::ConstIterator childIt = childrenByParent.find(current);
        if (childIt != childrenByParent.end()) {
            const TQValueVector<pid_t>& children = childIt.data();
            for (unsigned j = 0; j < children.size(); ++j)
                stack.push_back(children[j]);
        }
    }
}

static int countProcessesWithName(const GArray* tasks, const TQString& appName)
{
    int count = 0;
    if (!tasks)
        return 0;

    for (guint i = 0; i < tasks->len; ++i) {
        if (bridge_task_simple_name(tasks, i) == appName)
            count++;
    }
    return count;
}

static TQString displayNameForWindow(const WindowInfo& win, const GArray* tasks)
{
    if (!win.title.isEmpty())
        return win.title;

    int idx = findTaskIndexByPid(tasks, win.pid);
    if (idx >= 0)
        return bridge_task_name(tasks, (guint)idx);

    if (!win.wmClass.isEmpty())
        return win.wmClass;

    return TQString("Unknown");
}

static TQString iconKeyForWindow(const WindowInfo& win, const GArray* tasks)
{
    int idx = findTaskIndexByPid(tasks, win.pid);
    if (idx >= 0)
        return bridge_task_simple_name(tasks, (guint)idx);

    if (!win.wmInstance.isEmpty())
        return win.wmInstance.lower();

    if (!win.wmClass.isEmpty())
        return win.wmClass.lower();

    return TQString("unknown");
}

TQValueList<CompactAppEntry> CompactAppList::build(const GArray* tasks, uid_t ownUid)
{
    TQValueList<CompactAppEntry> entries;
    (void)ownUid;

    if (!tasks || tasks->len == 0)
        return entries;

    TQMap<pid_t, int> pidToIndex;
    TQMap<pid_t, TQValueVector<pid_t> > childrenByParent;
    buildPidMaps(tasks, pidToIndex, childrenByParent);

    TQMap<TQString, bool> usedGroupKeys;

    TQValueList<WindowInfo> windows = enumerateClientWindows();
    for (TQValueList<WindowInfo>::ConstIterator wit = windows.begin(); wit != windows.end(); ++wit) {
        const WindowInfo& win = *wit;

        TQString groupKey = TQString::number((unsigned long)win.window);
        if (usedGroupKeys.contains(groupKey))
            continue;
        usedGroupKeys.insert(groupKey, true);

        int winTaskIdx = findTaskIndexByPid(tasks, win.pid);
        pid_t appRoot = win.pid;
        TQString appName;

        if (winTaskIdx >= 0) {
            appRoot = findAppRootPid(win.pid, tasks, pidToIndex);
            appName = bridge_task_simple_name(tasks, (guint)winTaskIdx);
        } else if (!win.wmInstance.isEmpty()) {
            appName = win.wmInstance.lower();
        } else if (!win.wmClass.isEmpty()) {
            appName = win.wmClass.lower();
        }

        CompactAppEntry entry;
        entry.repPid = appRoot;
        entry.windowId = (unsigned long)win.window;
        entry.displayName = displayNameForWindow(win, tasks);
        entry.iconKey = iconKeyForWindow(win, tasks);
        entry.wmClass = win.wmClass;
        entry.wmInstance = win.wmInstance;

        if (!appName.isEmpty() && countProcessesWithName(tasks, appName) > 1)
            collectAppMembers(appRoot, tasks, pidToIndex, childrenByParent, appName, entry.taskIndices);
        else {
            TQMap<pid_t, bool> visited;
            collectDescendants(win.pid, childrenByParent, visited, entry.taskIndices, pidToIndex);
        }

        if (entry.taskIndices.isEmpty() && winTaskIdx >= 0)
            entry.taskIndices.push_back(winTaskIdx);

        if (entry.taskIndices.isEmpty())
            continue;

        entries.append(entry);
    }

    return entries;
}
