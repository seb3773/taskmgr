/*
 * users_tab.cpp — Users tab for taskmgr TQt3 port.
 */

#include "users_tab.h"
#include "backend_bridge.h"
#include "taskmgr_privileged_ops.h"
#include "process_details_dialog.h"
#include "process_launcher.h"
#include "mvc/tqtmvctreeview.h"
#include "mvc/tqttreestore.h"
#include "tde_icon_loader.h"
#include "fast_format.h"
#include "utils.h"
#include "app_manager.h"

#include <ntqpopupmenu.h>
#include <ntqmessagebox.h>
#include <ntqheader.h>
#include <ntqframe.h>
#include <ntqtimer.h>
#include <ntqvaluevector.h>

#include <unistd.h>
#include <limits.h>
#include <signal.h>

#include <string.h>
#include <glib.h>

static const TQColor BG_NORMAL      = TQColor(0xFF, 0xF9, 0xE3);
static const TQColor BG_MEDIUM_LOW  = TQColor(0xFF, 0xF5, 0xC4);
static const TQColor BG_MEDIUM      = TQColor(0xFD, 0xD3, 0x67);
static const TQColor BG_HIGH        = TQColor(0xFC, 0x68, 0x2A);
static const TQColor BG_CRITICAL    = TQColor(0xFC, 0x40, 0x40);

static inline const TQColor& usersCpuBg(double pct) {
    if (pct < 10.0) return BG_NORMAL;
    if (pct < 20.0) return BG_MEDIUM_LOW;
    if (pct < 50.0) return BG_MEDIUM;
    if (pct < 80.0) return BG_HIGH;
    return BG_CRITICAL;
}

static inline const TQColor& usersGpuBg(double pct) {
    if (pct < 15.0) return BG_NORMAL;
    if (pct < 40.0) return BG_MEDIUM_LOW;
    if (pct < 90.0) return BG_MEDIUM;
    return BG_HIGH;
}

static inline const TQColor& usersMemoryBgFromDisplay(const char* memStr)
{
    if (!memStr || !memStr[0]) return BG_NORMAL;

    char buf[64];
    strncpy(buf, memStr, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    guint64 memoryBytes = string_to_size(buf);
    guint64 memoryMb = memoryBytes / (1024 * 1024);

    if (memoryMb < 25)  return BG_NORMAL;
    if (memoryMb < 100) return BG_MEDIUM_LOW;
    if (memoryMb < 250) return BG_MEDIUM;
    if (memoryMb < 500) return BG_HIGH;
    return BG_CRITICAL;
}

UsersTab::UsersTab(TQWidget* parent, const char* name)
    : TQWidget(parent, name),
      m_sortCriteria(SortName),
      m_sortFlags(USERS_SORT_NAME_ASC | USERS_SORT_PID_ASC),
      m_selectedPid(0)
{
    TQVBoxLayout* layout = new TQVBoxLayout(this, 0, 4);

    m_store = new TQtTreeStore(5, this);
    setupColumns();

    m_treeView = new TQtMvcTreeView(this);
    m_treeView->setModel(m_store);
    m_treeView->setSortingEnabled(false);
    m_treeView->setFrameStyle(TQFrame::NoFrame);

    m_treeView->setColumnWidth(0, 320);
    m_treeView->setColumnWidth(1, 55);
    m_treeView->setColumnWidth(2, 95);
    m_treeView->setColumnWidth(3, 55);
    m_treeView->setColumnWidth(4, 65);

    layout->addWidget(m_treeView, 1);

    connect(m_treeView->horizontalHeader(), SIGNAL(clicked(int)),
            this, SLOT(onHeaderClicked(int)));
    connect(m_treeView, SIGNAL(rowContextMenuRequested(const TQtModelIndex&, int, const TQPoint&)),
            this, SLOT(onRowContextMenuRequested(const TQtModelIndex&, int, const TQPoint&)));
    connect(m_treeView, SIGNAL(doubleClicked(int, int, int, const TQPoint&)),
            this, SLOT(onDoubleClicked(int, int, int, const TQPoint&)));
}

UsersTab::~UsersTab()
{
}

void UsersTab::setupColumns()
{
    m_store->setHeader(0, "Name");
    m_store->setHeader(1, "CPU");
    m_store->setHeader(2, "Memory");
    m_store->setHeader(3, "GPU");
    m_store->setHeader(4, "PID");
}

static void freeLoggedUsersList(user_with_sessions_t** users)
{
    if (!users) return;
    for (size_t i = 0; users[i]; ++i) {
        free(users[i]->username);
        for (size_t s = 0; s < users[i]->session_count; ++s)
            free(users[i]->session_ids[s]);
        free(users[i]->session_ids);
        free(users[i]);
    }
    free(users);
}

bool UsersTab::isUserNode(int nodeId) const
{
    return m_store->data(TQtModelIndex(nodeId, 4)).toString().isEmpty();
}

void UsersTab::sortTaskIndices(TQValueVector<int>& indices, GArray* taskList)
{
    if (indices.size() <= 1 || !taskList)
        return;

    for (int i = 0; i < (int)indices.size() - 1; ++i) {
        for (int j = i + 1; j < (int)indices.size(); ++j) {
            int ia = indices[i];
            int ib = indices[j];
            int cmp = 0;
            int dir = 1;

            switch (m_sortCriteria) {
            case SortName:
                dir = (m_sortFlags & USERS_SORT_NAME_ASC) ? 1 : -1;
                {
                    const char* na = bridge_task_name(taskList, ia);
                    const char* nb = bridge_task_name(taskList, ib);
                    cmp = strcmp(na ? na : "", nb ? nb : "");
                }
                break;
            case SortCpu:
                dir = (m_sortFlags & USERS_SORT_CPU_ASC) ? 1 : -1;
                {
                    gfloat ca = bridge_task_cpu(taskList, ia);
                    gfloat cb = bridge_task_cpu(taskList, ib);
                    if (ca > cb) cmp = 1;
                    else if (ca < cb) cmp = -1;
                }
                break;
            case SortMemory:
                dir = (m_sortFlags & USERS_SORT_MEMORY_ASC) ? 1 : -1;
                {
                    gulong ra = bridge_task_rss(taskList, ia);
                    gulong rb = bridge_task_rss(taskList, ib);
                    if (ra > rb) cmp = 1;
                    else if (ra < rb) cmp = -1;
                }
                break;
            case SortGpu:
                dir = (m_sortFlags & USERS_SORT_GPU_ASC) ? 1 : -1;
                {
                    gfloat ga = bridge_task_gpu(taskList, ia);
                    gfloat gb = bridge_task_gpu(taskList, ib);
                    if (ga > gb) cmp = 1;
                    else if (ga < gb) cmp = -1;
                }
                break;
            case SortPid:
                dir = (m_sortFlags & USERS_SORT_PID_ASC) ? 1 : -1;
                cmp = (int)bridge_task_pid(taskList, ia) - (int)bridge_task_pid(taskList, ib);
                break;
            }

            if (cmp * dir > 0) {
                int tmp = indices[i];
                indices[i] = indices[j];
                indices[j] = tmp;
            }
        }
    }
}

void UsersTab::applyUsersCellColoring(TQtTreeStore* store, int nid,
                                      double cpuPct, const char* memDisplayStr,
                                      double gpuPct, bool isUserNode)
{
    (void)isUserNode;

    TQtCellStyle cpuStyle;
    cpuStyle.background = usersCpuBg(cpuPct);
    cpuStyle.hasBackground = true;
    store->setCellStyle(nid, 1, cpuStyle);

    TQtCellStyle memStyle;
    memStyle.background = usersMemoryBgFromDisplay(memDisplayStr);
    memStyle.hasBackground = true;
    store->setCellStyle(nid, 2, memStyle);

    TQtCellStyle gpuStyle;
    gpuStyle.background = usersGpuBg(gpuPct);
    gpuStyle.hasBackground = true;
    store->setCellStyle(nid, 3, gpuStyle);
}

void UsersTab::refresh()
{
    bridge_refresh_tasks();

    m_treeView->blockPainting();

    int savedScrollX = m_treeView->contentsX();
    int savedScrollY = m_treeView->contentsY();

    m_selectedPid = 0;
    m_selectedUsername = "";
    TQtModelIndex selIdx = m_treeView->selectedIndex();
    if (selIdx.isValid()) {
        if (isUserNode(selIdx.nodeId)) {
            TQString name = m_store->data(TQtModelIndex(selIdx.nodeId, 0)).toString();
            int space = name.find(' ');
            if (space >= 0) name = name.left(space);
            m_selectedUsername = name;
        } else {
            TQString pidStr = m_store->data(TQtModelIndex(selIdx.nodeId, 4)).toString();
            bool ok;
            int pid = pidStr.toInt(&ok);
            if (ok && pid > 0) m_selectedPid = pid;
        }
    }

    m_expandedUsers.clear();
    int rootCount = m_store->childCount(TQtTreeStore::RootNodeId);
    for (int i = 0; i < rootCount; ++i) {
        int nid = m_store->childAt(TQtTreeStore::RootNodeId, i);
        if (nid >= 0 && m_treeView->isExpanded(nid)) {
            TQString name = m_store->data(TQtModelIndex(nid, 0)).toString();
            int space = name.find(' ');
            if (space >= 0) name = name.left(space);
            m_expandedUsers.insert(name, true);
        }
    }

    GArray* taskList = task_array;
    user_with_sessions_t** loggedUsers = get_logged_in_users_with_session_info();
    if (!loggedUsers) {
        m_store->clear();
        m_treeView->unblockPainting();
        return;
    }

    m_store->beginBatch();
    m_store->clear();

    guint16 flags = bridge_get_app_flags();
    bool showPss = (flags & APP_FLAG_DISPLAY_PSS) != 0;
    char sizeBuf[64];
    char cpuBuf[32];
    char gpuBuf[32];
    char pidBuf[16];
    int selectNodeId = -1;

    if (taskList) {
        for (size_t ui = 0; loggedUsers[ui]; ++ui) {
            const char* username = loggedUsers[ui]->username;
            if (!username) continue;

            TQValueVector<int> userTasks;
            for (guint j = 0; j < taskList->len; ++j) {
                const char* uname = bridge_task_uname(taskList, j);
                if (uname && strcmp(uname, username) == 0)
                    userTasks.push_back((int)j);
            }

            sortTaskIndices(userTasks, taskList);

            double totalCpu = 0.0;
            double totalGpu = 0.0;
            guint64 totalRam = 0;

            for (int k = 0; k < (int)userTasks.size(); ++k) {
                int idx = userTasks[k];
                totalCpu += bridge_task_cpu(taskList, idx);
                totalGpu += bridge_task_gpu(taskList, idx);
                guint64 rss = bridge_task_rss(taskList, idx);
                guint64 pss = bridge_task_pss(taskList, idx);
                totalRam += showPss ? (pss > 0 ? pss : rss) : rss;
            }

            GString* sessionsStr = g_string_new("");
            for (size_t s = 0; s < loggedUsers[ui]->session_count; ++s) {
                if (s > 0) g_string_append(sessionsStr, ", ");
                g_string_append(sessionsStr, loggedUsers[ui]->session_ids[s]);
            }

            char displayName[256];
            snprintf(displayName, sizeof(displayName), "%s [sessions: %s] (%u processes)",
                     username, sessionsStr->str, (unsigned)userTasks.size());
            g_string_free(sessionsStr, TRUE);

            format_cpu_percentage(cpuBuf, (guint32)totalCpu);
            format_cpu_percentage(gpuBuf, (guint32)totalGpu);
            if (showPss) {
                if (totalRam > 0)
                    format_memory_size(sizeBuf, totalRam);
                else
                    sizeBuf[0] = '\0';
            } else {
                format_memory_size(sizeBuf, totalRam);
            }

            TQtRow parentRow(5);
            parentRow[0] = displayName;
            parentRow[1] = cpuBuf;
            parentRow[2] = sizeBuf;
            parentRow[3] = gpuBuf;
            parentRow[4] = "";

            int parentNid = m_store->appendNode(TQtTreeStore::RootNodeId, parentRow);
            if (m_selectedPid == 0 && m_selectedUsername == username)
                selectNodeId = parentNid;

            applyUsersCellColoring(m_store, parentNid, totalCpu, sizeBuf, totalGpu, true);

            for (int k = 0; k < (int)userTasks.size(); ++k) {
                int idx = userTasks[k];
                pid_t pid = bridge_task_pid(taskList, idx);
                TQString name = bridge_task_name(taskList, idx);
                TQString simpleName = bridge_task_simple_name(taskList, idx);

                double cpuPct = bridge_task_cpu(taskList, idx);
                if (cpuPct > 100.0) cpuPct = 100.0;
                if (cpuPct < 0.0) cpuPct = 0.0;
                double gpuPct = bridge_task_gpu(taskList, idx);

                guint64 rss = bridge_task_rss(taskList, idx);
                guint64 pss = bridge_task_pss(taskList, idx);

                format_cpu_percentage(cpuBuf, (guint32)cpuPct);
                format_cpu_percentage(gpuBuf, (guint32)gpuPct);
                if (showPss) {
                    if (pss > 0)
                        format_memory_size(sizeBuf, pss);
                    else
                        sizeBuf[0] = '\0';
                } else {
                    format_memory_size(sizeBuf, rss);
                }
                format_pid(pidBuf, (guint32)pid);

                TQtRow childRow(5);
                childRow[0] = name;
                childRow[1] = cpuBuf;
                childRow[2] = sizeBuf;
                childRow[3] = gpuBuf;
                childRow[4] = pidBuf;

                int childNid = m_store->appendNode(parentNid, childRow);
                if (pid == m_selectedPid)
                    selectNodeId = childNid;

                TQtCellStyle iconStyle;
                iconStyle.setIcon(TdeIconLoader::processIcon(simpleName));
                m_store->setCellStyle(childNid, 0, iconStyle);

                applyUsersCellColoring(m_store, childNid, cpuPct, sizeBuf, gpuPct, false);
            }

            if (m_expandedUsers.contains(username))
                m_treeView->setExpandedNoRebuild(parentNid, true);
        }
    }

    m_store->endBatch();

    if (selectNodeId >= 0)
        m_treeView->selectIndex(TQtModelIndex(selectNodeId, 0));

    m_treeView->setContentsPos(savedScrollX, savedScrollY);
    m_treeView->unblockPainting();

    freeLoggedUsersList(loggedUsers);
}

void UsersTab::onHeaderClicked(int col)
{
    switch (col) {
    case 0:
        if (m_sortCriteria == SortName)
            m_sortFlags ^= USERS_SORT_NAME_ASC;
        else {
            m_sortCriteria = SortName;
            m_sortFlags |= USERS_SORT_NAME_ASC;
        }
        break;
    case 1:
        if (m_sortCriteria == SortCpu)
            m_sortFlags ^= USERS_SORT_CPU_ASC;
        else {
            m_sortCriteria = SortCpu;
            m_sortFlags &= ~USERS_SORT_CPU_ASC;
        }
        break;
    case 2:
        if (m_sortCriteria == SortMemory)
            m_sortFlags ^= USERS_SORT_MEMORY_ASC;
        else {
            m_sortCriteria = SortMemory;
            m_sortFlags &= ~USERS_SORT_MEMORY_ASC;
        }
        break;
    case 3:
        if (m_sortCriteria == SortGpu)
            m_sortFlags ^= USERS_SORT_GPU_ASC;
        else {
            m_sortCriteria = SortGpu;
            m_sortFlags &= ~USERS_SORT_GPU_ASC;
        }
        break;
    case 4:
        if (m_sortCriteria == SortPid)
            m_sortFlags ^= USERS_SORT_PID_ASC;
        else {
            m_sortCriteria = SortPid;
            m_sortFlags |= USERS_SORT_PID_ASC;
        }
        break;
    default:
        return;
    }
    refresh();
}

pid_t UsersTab::getSelectedPid() const
{
    TQtModelIndex selIdx = m_treeView->selectedIndex();
    if (!selIdx.isValid()) return 0;
    TQString pidStr = m_store->data(TQtModelIndex(selIdx.nodeId, 4)).toString();
    bool ok;
    int pid = pidStr.toInt(&ok);
    return ok ? (pid_t)pid : 0;
}

TQString UsersTab::getSelectedUsername() const
{
    TQtModelIndex selIdx = m_treeView->selectedIndex();
    if (!selIdx.isValid()) return TQString();
    TQString name = m_store->data(TQtModelIndex(selIdx.nodeId, 0)).toString();
    int space = name.find(' ');
    if (space >= 0) name = name.left(space);
    return name;
}

void UsersTab::onRowContextMenuRequested(const TQtModelIndex& index, int col, const TQPoint& globalPos)
{
    (void)col;
    if (!index.isValid()) return;

    if (isUserNode(index.nodeId)) {
        m_selectedUsername = getSelectedUsername();
        TQPopupMenu* menu = new TQPopupMenu(this);
        menu->insertItem("Disconnect", this, SLOT(onContextDisconnect()));
        menu->exec(globalPos);
        delete menu;
        return;
    }

    TQString pidStr = m_store->data(TQtModelIndex(index.nodeId, 4)).toString();
    bool ok;
    int pid = pidStr.toInt(&ok);
    if (!ok || pid <= 0) return;
    m_selectedPid = pid;

    TQPopupMenu* menu = new TQPopupMenu(this);
    menu->insertItem("&End Task", this, SLOT(onContextEndTask()));
    menu->insertSeparator();

    TQPopupMenu* prioMenu = new TQPopupMenu(menu);
    prioMenu->insertItem("Realtime (Critical)", this, SLOT(onContextPrioCritical()));
    prioMenu->insertItem("Very High", this, SLOT(onContextPrioVeryHigh()));
    prioMenu->insertItem("High", this, SLOT(onContextPrioHigh()));
    prioMenu->insertItem("Normal", this, SLOT(onContextPrioNormal()));
    prioMenu->insertItem("Low", this, SLOT(onContextPrioLow()));
    prioMenu->insertItem("Very Low", this, SLOT(onContextPrioVeryLow()));
    menu->insertItem("Set Priority", prioMenu);
    menu->insertSeparator();
    menu->insertItem("Open file location", this, SLOT(onContextOpenFileLocation()));
    menu->insertItem("Search online", this, SLOT(onContextSearchOnline()));
    menu->insertSeparator();
    menu->insertItem("Details", this, SLOT(onContextDetails()));
    menu->exec(globalPos);
    delete menu;
}

void UsersTab::onDoubleClicked(int row, int col, int button, const TQPoint& mousePos)
{
    (void)row; (void)col; (void)button; (void)mousePos;
}

void UsersTab::onContextDisconnect()
{
    TQString username = getSelectedUsername();
    if (username.isEmpty()) return;

    if (!TaskmgrPrivilegedOps::disconnectUser(this, username.latin1())) {
        TQMessageBox::critical(this, "Disconnect Error",
            TQString("Can't disconnect user %1 sessions.").arg(username));
        return;
    }

    TQTimer::singleShot(1500, this, SLOT(onDelayedRefresh()));
}

void UsersTab::onDelayedRefresh()
{
    refresh();
}

void UsersTab::onContextEndTask()
{
    pid_t pid = getSelectedPid();
    if (pid <= 0) return;

    int response = TQMessageBox::question(this, "Confirm End Task",
        TQString("Are you sure you want to end the process %1?").arg(pid),
        TQMessageBox::Yes, TQMessageBox::No);

    if (response == TQMessageBox::Yes) {
        if (TaskmgrPrivilegedOps::killProcess(this, pid, SIGTERM))
            refresh();
        else
            TQMessageBox::critical(this, "Error", "Failed to terminate the process.\nYou may need administrator privileges.");
    }
}

void UsersTab::setProcessPriority(int priorityVal)
{
    pid_t pid = getSelectedPid();
    if (pid <= 0) return;
    TaskmgrPrivilegedOps::setProcessPriority(this, pid, priorityVal);
    refresh();
}

void UsersTab::onContextPrioCritical()   { setProcessPriority(-20); }
void UsersTab::onContextPrioVeryHigh()   { setProcessPriority(-15); }
void UsersTab::onContextPrioHigh()       { setProcessPriority(-5); }
void UsersTab::onContextPrioNormal()     { setProcessPriority(0); }
void UsersTab::onContextPrioLow()        { setProcessPriority(10); }
void UsersTab::onContextPrioVeryLow()    { setProcessPriority(19); }

void UsersTab::onContextOpenFileLocation()
{
    pid_t pid = getSelectedPid();
    if (pid <= 0) return;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/proc/%d/exe", pid);
    char linkTarget[PATH_MAX];
    ssize_t len = readlink(path, linkTarget, sizeof(linkTarget) - 1);
    if (len <= 0) return;
    linkTarget[len] = '\0';
    int slash = -1;
    for (int i = 0; linkTarget[i]; ++i)
        if (linkTarget[i] == '/') slash = i;
    if (slash >= 0) linkTarget[slash] = '\0';
    else return;
    taskmgr_launch_open_directory(linkTarget);
}

void UsersTab::onContextSearchOnline()
{
    TQtModelIndex selIdx = m_treeView->selectedIndex();
    if (!selIdx.isValid()) return;
    TQString processName = m_store->data(TQtModelIndex(selIdx.nodeId, 0)).toString();
    if (processName.isEmpty()) return;

    if (default_browser_binary_path && strlen(default_browser_binary_path) > 0) {
        TQString query = TQString("linux+%1").arg(processName).replace(" ", "+");
        TQString url = TQString("https://www.google.com/search?q=%1").arg(query);
        if (!taskmgr_launch_open_url(url.latin1())) {
            TQMessageBox::critical(this, "Error", "Failed to open the web browser.");
        }
    } else {
        TQMessageBox::information(this, "Search online",
            "Configure a default web browser in Settings first.");
    }
}

void UsersTab::onContextDetails()
{
    pid_t pid = getSelectedPid();
    if (pid <= 0) return;

    ProcessDetailsDialog dlg(pid, this);
    dlg.exec();
}

#include "users_tab.moc"
