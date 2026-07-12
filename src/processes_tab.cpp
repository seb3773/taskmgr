/*
 * processes_tab.cpp — Processes tab for taskmgr TQt3 port.
 *
 * Implements the process tree table using TQtTreeStore and TQtMvcTreeView.
 */

#include "processes_tab.h"
#include "process_details_dialog.h"
#include "taskmgr_privileged_ops.h"
#include "process_launcher.h"
#include "backend_bridge.h"
#include "compact_app_list.h"
#include "fast_format.h"
#include "utils.h"
#include "tde_icon_loader.h"

#include "mvc/tqtmvctreeview.h"
#include "mvc/tqttreestore.h"

#include <ntqpopupmenu.h>
#include <ntqmessagebox.h>
#include <ntqheader.h>
#include <ntqscrollbar.h>
#include <ntqevent.h>

#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <limits.h>

struct CharPtrKey {
    const char* str;
    CharPtrKey() : str("") {}
    CharPtrKey(const char* s) : str(s ? s : "") {}
    bool operator<(const CharPtrKey& other) const {
        return strcmp(str, other.str) < 0;
    }
};

/* ====================================================================
 * Cell coloring — background colors matching GTK3 original exactly.
 * Columns: CPU (3), CPU Time (4), Memory (5), Working Set (6), VM-Size (7), GPU (8), Prio (10).
 * Every cell in these columns gets a background (base color is cream #FFF9E3).
 * ==================================================================== */

static const TQColor BG_NORMAL      = TQColor(0xFF, 0xF9, 0xE3);  /* #FFF9E3 (cream base) */
static const TQColor BG_MEDIUM_LOW  = TQColor(0xFF, 0xF5, 0xC4);  /* #FFF5C4 (light yellow) */
static const TQColor BG_PRIO_LOW    = TQColor(0xF7, 0xED, 0xA8);  /* #F7EDA8 (soft yellow) */
static const TQColor BG_MEDIUM      = TQColor(0xFD, 0xD3, 0x67);  /* #FDD367 (amber) */
static const TQColor BG_HIGH        = TQColor(0xFC, 0x68, 0x2A);  /* #FC682A (orange) */
static const TQColor BG_CRITICAL    = TQColor(0xFC, 0x40, 0x40);  /* #FC4040 (red) */

/* CPU % thresholds */
static inline const TQColor& cpuBg(double pct) {
    if (pct < 10.0) return BG_NORMAL;
    if (pct < 20.0) return BG_MEDIUM_LOW;
    if (pct < 50.0) return BG_MEDIUM;
    if (pct < 80.0) return BG_HIGH;
    return BG_CRITICAL;
}

/* GPU % thresholds */
static inline const TQColor& gpuBg(double pct) {
    if (pct < 15.0) return BG_NORMAL;
    if (pct < 40.0) return BG_MEDIUM_LOW;
    if (pct < 90.0) return BG_MEDIUM;
    return BG_HIGH;
}

/* Memory thresholds (bytes → MB) */
static inline const TQColor& memoryBg(guint64 memBytes) {
    double mb = (double)memBytes / (1024.0 * 1024.0);
    if (mb < 50.0)   return BG_NORMAL;
    if (mb < 200.0)  return BG_MEDIUM_LOW;
    if (mb < 500.0)  return BG_MEDIUM;
    if (mb < 1000.0) return BG_HIGH;
    return BG_CRITICAL;
}

/* Priority coloring */
static inline const TQColor& prioBg(int prio) {
    if (prio <= -20) return BG_MEDIUM;
    if (prio <= -10) return BG_PRIO_LOW;
    if (prio <= -5)  return BG_MEDIUM_LOW;
    return BG_NORMAL;
}

static void applyCellColoring(TQtTreeStore* store, int nid, double cpuPct,
                              guint64 cpuTimeSec, guint64 memBytes,
                              guint64 wsBytes, guint64 vszBytes,
                              double gpuPct, int prio)
{
    (void)cpuTimeSec; (void)wsBytes; (void)vszBytes;
    
    /* CPU (column 3) */
    {
        TQtCellStyle s;
        s.background = cpuBg(cpuPct);
        s.hasBackground = true;
        store->setCellStyle(nid, 3, s);
    }

    /* CPU Time (column 4) — constant cream background */
    {
        TQtCellStyle s;
        s.background = BG_NORMAL;
        s.hasBackground = true;
        store->setCellStyle(nid, 4, s);
    }

    /* Memory (column 5) */
    {
        TQtCellStyle s;
        s.background = memoryBg(memBytes);
        s.hasBackground = true;
        store->setCellStyle(nid, 5, s);
    }

    /* GPU (column 8) */
    {
        TQtCellStyle s;
        s.background = gpuBg(gpuPct);
        s.hasBackground = true;
        store->setCellStyle(nid, 8, s);
    }

    /* Priority (column 10) */
    {
        TQtCellStyle s;
        s.background = prioBg(prio);
        s.hasBackground = true;
        store->setCellStyle(nid, 10, s);
    }
}

/* Helper for readable status translation */
static const char* get_readable_status(const char* status_code) {
    if (!status_code || *status_code == '\0') return "Unknown";
    switch (status_code[0]) {
        case 'S': return "Waiting";
        case 'R': return "Running";
        case 'D': return "Uninterruptible Wait";
        case 'T': return "Stopped";
        case 'Z': return "Zombie";
        case 'X': return "Dead";
        default:  return status_code;
    }
}

static TQPixmap getProcessIcon(const TQString& procName) {
    return TdeIconLoader::processIcon(procName);
}

static TQPixmap genericListIcon()
{
    static TQPixmap icon;
    if (icon.isNull())
        icon = TdeIconLoader::placeholderIcon();
    return icon;
}

static TQPixmap processListIcon(const TQString& name, bool skipIcons)
{
    if (skipIcons)
        return genericListIcon();
    return getProcessIcon(name);
}

/* ====================================================================
 * ProcessesTab — Constructor
 * ==================================================================== */

ProcessesTab::ProcessesTab(TQWidget* parent, const char* name)
    : TQWidget(parent, name), m_selectedPid(0), m_selectedWindowId(0), m_selectedGroupName(""), m_compactMode(false)
{
    TQVBoxLayout* layout = new TQVBoxLayout(this, 0, 4);

    /* Model (12 columns) */
    m_store = new TQtTreeStore(12, this);
    setupColumns();

    /* View */
    m_treeView = new TQtMvcTreeView(this);
    m_treeView->setModel(m_store);
    m_treeView->setSortingEnabled(true);
    m_treeView->setFrameStyle(TQFrame::NoFrame);
    
    /* Set default column widths to resemble the GTK3 layout */
    m_treeView->setColumnWidth(0, 210); // Name
    m_treeView->setColumnWidth(1, 85);  // Status
    m_treeView->setColumnWidth(2, 66);  // User
    m_treeView->setColumnWidth(3, 55);  // CPU
    m_treeView->setColumnWidth(4, 95);  // CPU time
    m_treeView->setColumnWidth(5, 85);  // Memory
    m_treeView->setColumnWidth(6, 95);  // Working Set
    m_treeView->setColumnWidth(7, 85);  // VM-Size
    m_treeView->setColumnWidth(8, 55);  // GPU
    m_treeView->setColumnWidth(9, 65);  // PID
    m_treeView->setColumnWidth(10, 50); // Prio
    m_treeView->setColumnWidth(11, 65); // PPID

    layout->addWidget(m_treeView, 1);

    /* Bottom button bar — Less details left, End Task right */
    TQHBoxLayout* buttonBar = new TQHBoxLayout(0, 0, 6);

    m_detailsBtn = new TQPushButton("&Less details", this);
    m_detailsBtn->setMinimumWidth(100);
    buttonBar->addWidget(m_detailsBtn);

    buttonBar->addStretch(1);

    m_endTaskBtn = new TQPushButton("&End Task", this);
    m_endTaskBtn->setMinimumWidth(90);
    buttonBar->addWidget(m_endTaskBtn);
    
    layout->addLayout(buttonBar);

    connect(m_treeView, SIGNAL(rowContextMenuRequested(const TQtModelIndex&, int, const TQPoint&)),
            this,       SLOT(onRowContextMenuRequested(const TQtModelIndex&, int, const TQPoint&)));

    connect(m_treeView, SIGNAL(doubleClicked(int, int, int, const TQPoint&)),
            this,       SLOT(onDoubleClicked(int, int, int, const TQPoint&)));

    connect(m_endTaskBtn, SIGNAL(clicked()), this, SLOT(onEndTaskClicked()));
    connect(m_detailsBtn, SIGNAL(clicked()), this, SLOT(onDetailsClicked()));
}

ProcessesTab::~ProcessesTab()
{
}

void ProcessesTab::setupColumns()
{
    m_store->setHeader(0, "Name");
    m_store->setHeader(1, "Status");
    m_store->setHeader(2, "User");
    m_store->setHeader(3, "CPU");
    m_store->setHeader(4, "CPU time");
    m_store->setHeader(5, "Memory");
    m_store->setHeader(6, "Working Set");
    m_store->setHeader(7, "VM-Size");
    m_store->setHeader(8, "GPU");
    m_store->setHeader(9, "PID");
    m_store->setHeader(10, "Prio");
    m_store->setHeader(11, "PPID");
}

pid_t ProcessesTab::getSelectedPid() const
{
    TQtModelIndex selIdx = m_treeView->selectedIndex();
    if (!selIdx.isValid()) return 0;
    TQString pidStr = m_store->data(TQtModelIndex(selIdx.nodeId, 9)).toString();
    bool ok;
    int pid = pidStr.toInt(&ok);
    return ok ? (pid_t)pid : 0;
}

bool ProcessesTab::isGroupNode(int nodeId) const
{
    if (!m_store || m_compactMode)
        return false;
    TQString pidStr = m_store->data(TQtModelIndex(nodeId, 9)).toString();
    if (!pidStr.isEmpty())
        return false;
    return m_store->childCount(nodeId) > 0;
}

TQValueVector<pid_t> ProcessesTab::collectGroupPids(int groupNodeId) const
{
    TQValueVector<pid_t> pids;
    if (!m_store)
        return pids;

    int n = m_store->childCount(groupNodeId);
    for (int i = 0; i < n; ++i) {
        int childNid = m_store->childAt(groupNodeId, i);
        TQString pidStr = m_store->data(TQtModelIndex(childNid, 9)).toString();
        bool ok;
        int pid = pidStr.toInt(&ok);
        if (ok && pid > 0)
            pids.push_back((pid_t)pid);
    }
    return pids;
}

TQValueVector<pid_t> ProcessesTab::getTargetPidsForAction() const
{
    if (m_compactMode)
        return getSelectedMemberPids();

    TQtModelIndex selIdx = m_treeView->selectedIndex();
    if (!selIdx.isValid())
        return TQValueVector<pid_t>();

    if (isGroupNode(selIdx.nodeId))
        return collectGroupPids(selIdx.nodeId);

    pid_t pid = getSelectedPid();
    TQValueVector<pid_t> pids;
    if (pid > 0)
        pids.push_back(pid);
    return pids;
}

bool ProcessesTab::isPidStopped(pid_t pid) const
{
    if (!task_array)
        return false;

    for (guint i = 0; i < task_array->len; ++i) {
        if (bridge_task_pid(task_array, i) == pid)
            return bridge_task_state(task_array, i) == 'T';
    }
    return false;
}

bool ProcessesTab::areAllTargetsStopped(const TQValueVector<pid_t>& pids) const
{
    if (pids.isEmpty())
        return false;

    for (unsigned i = 0; i < pids.size(); ++i) {
        if (!isPidStopped(pids[i]))
            return false;
    }
    return true;
}

bool ProcessesTab::applySignalToTargets(int signal, bool confirm, const TQString& confirmMessage)
{
    TQValueVector<pid_t> pids = getTargetPidsForAction();
    if (pids.isEmpty())
        return false;

    if (signal == SIGSTOP) {
        for (unsigned i = 0; i < pids.size(); ++i) {
            if (pids[i] == (pid_t)getpid()) {
                TQMessageBox::critical(this, "Error", "Can't stop process self");
                return false;
            }
        }
    }

    if (confirm) {
        int response = TQMessageBox::question(this, "Confirm", confirmMessage,
            TQMessageBox::Yes, TQMessageBox::No);
        if (response != TQMessageBox::Yes)
            return false;
    }

    unsigned failed = 0;
    for (unsigned i = 0; i < pids.size(); ++i) {
        if (!TaskmgrPrivilegedOps::killProcess(this, pids[i], signal))
            failed++;
    }

    if (failed > 0) {
        TQMessageBox::critical(this, "Error",
            TQString("Failed to send signal to %1 process(es).\nYou may need administrator privileges.")
                .arg(failed));
        return false;
    }

    refresh();
    return true;
}

/* ====================================================================
 * refresh() — Anti-flicker strategy:
 *   1. setUpdatesEnabled(false) to suppress repaints during rebuild
 *   2. Save scroll position (contentsX/Y) and selected PID
 *   3. Rebuild store inside beginBatch/endBatch (single modelReset)
 *   4. Restore scroll position and selection WITHOUT auto-scroll
 *   5. setUpdatesEnabled(true) → single repaint
 * ==================================================================== */

void ProcessesTab::setCompactMode(bool compact)
{
    if (m_compactMode == compact)
        return;

    m_compactMode = compact;
    m_detailsBtn->setText(compact ? "&More details" : "&Less details");
    applyColumnLayout();
    refresh();
}

void ProcessesTab::updateCompactColumnWidth()
{
    if (!m_compactMode || !m_treeView)
        return;

    int w = m_treeView->visibleWidth();
    if (w <= 0)
        w = m_treeView->width() - 2 * m_treeView->frameWidth();

    TQScrollBar* vsb = m_treeView->verticalScrollBar();
    if (vsb && vsb->isVisible())
        w -= vsb->width();

    if (w < 80)
        w = 80;

    m_treeView->setColumnWidth(0, w);
}

void ProcessesTab::applyColumnLayout()
{
    if (m_compactMode) {
        for (int col = 1; col < 12; ++col)
            m_treeView->hideColumn(col);
        m_treeView->showColumn(0);
        updateCompactColumnWidth();
    } else {
        for (int col = 0; col < 12; ++col)
            m_treeView->showColumn(col);

        m_treeView->setColumnWidth(0, 210);
        m_treeView->setColumnWidth(1, 85);
        m_treeView->setColumnWidth(2, 66);
        m_treeView->setColumnWidth(3, 55);
        m_treeView->setColumnWidth(4, 95);
        m_treeView->setColumnWidth(5, 85);
        m_treeView->setColumnWidth(6, 95);
        m_treeView->setColumnWidth(7, 85);
        m_treeView->setColumnWidth(8, 55);
        m_treeView->setColumnWidth(9, 65);
        m_treeView->setColumnWidth(10, 50);
        m_treeView->setColumnWidth(11, 65);

        guint16 flags = bridge_get_app_flags();
        if (!(flags & APP_FLAG_DISPLAY_PSS))
            m_treeView->hideColumn(6);
    }
}

void ProcessesTab::resizeEvent(TQResizeEvent* e)
{
    TQWidget::resizeEvent(e);
    if (m_compactMode)
        updateCompactColumnWidth();
}

void ProcessesTab::refresh()
{
    if (m_compactMode)
        refreshCompact();
    else
        refreshFull(false);
}

static int findNodeIdByPid(TQtTreeStore* store, int parentId, pid_t pid) {
    int count = store->childCount(parentId);
    for (int i = 0; i < count; i++) {
        int childId = store->childAt(parentId, i);
        TQString pidStr = store->data(TQtModelIndex(childId, 9)).toString();
        if (pidStr.toInt() == pid) {
            return childId;
        }
        int found = findNodeIdByPid(store, childId, pid);
        if (found >= 0) {
            return found;
        }
    }
    return -1;
}

void ProcessesTab::selectPid(pid_t pid)
{
    refresh();

    int targetNodeId = findNodeIdByPid(m_store, TQtTreeStore::RootNodeId, pid);
    if (targetNodeId >= 0) {
        int parentId = m_store->parentId(targetNodeId);
        while (parentId != TQtTreeStore::RootNodeId) {
            if (!m_treeView->isExpanded(parentId)) {
                m_treeView->setExpanded(parentId, true);
            }
            parentId = m_store->parentId(parentId);
        }

        TQtModelIndex idx(targetNodeId, 0);
        m_treeView->selectIndex(idx);
        m_treeView->ensureIndexVisible(idx);
    }
}

void ProcessesTab::refreshLight()
{
    if (m_compactMode)
        refreshCompact();
    else
        refreshFull(true);
}

void ProcessesTab::refreshIconsOnly()
{
    if (m_compactMode || !m_store)
        return;

    m_treeView->blockPainting();
    int n = m_store->childCount(TQtTreeStore::RootNodeId);
    for (int i = 0; i < n; ++i) {
        int nid = m_store->childAt(TQtTreeStore::RootNodeId, i);
        if (nid >= 0)
            applyIconsToNode(nid);
    }
    m_treeView->unblockPainting();
}

void ProcessesTab::applyIconsToNode(int nodeId)
{
    TQString displayName = m_store->data(TQtModelIndex(nodeId, 0)).toString();
    int childCount = m_store->childCount(nodeId);

    TQtCellStyle style;
    style.hasIcon = true;

    if (childCount > 0) {
        TQString groupName = displayName;
        int paren = groupName.findRev('(');
        if (paren >= 0)
            groupName = groupName.left(paren).stripWhiteSpace();
        style.icon = getProcessIcon(groupName);
        m_store->setCellStyle(nodeId, 0, style);
        for (int i = 0; i < childCount; ++i) {
            int childNid = m_store->childAt(nodeId, i);
            if (childNid >= 0)
                applyIconsToNode(childNid);
        }
    } else {
        style.icon = getProcessIcon(displayName);
        m_store->setCellStyle(nodeId, 0, style);
    }
}

void ProcessesTab::refreshFull(bool skipIcons)
{
    /* ---- 1. Let C backend compute CPU percentages (time_percentage) ---- */
    bridge_refresh_tasks();

    /* ---- 2. Freeze the view (suppress all repaints) ---- */
    m_treeView->blockPainting();

    /* ---- 3. Save UI state ---- */
    int savedScrollX = m_treeView->contentsX();
    int savedScrollY = m_treeView->contentsY();
    
    /* Save selection state (PID or Group Name) */
    m_selectedPid = 0;
    m_selectedGroupName = "";
    TQtModelIndex selIdx = m_treeView->selectedIndex();
    if (selIdx.isValid()) {
        TQString pidStr = m_store->data(TQtModelIndex(selIdx.nodeId, 9)).toString();
        if (!pidStr.isEmpty()) {
            bool ok;
            int pid = pidStr.toInt(&ok);
            if (ok && pid > 0) m_selectedPid = pid;
        }
        if (m_selectedPid == 0) {
            /* It's a group node, get name (strip count suffix) */
            TQString name = m_store->data(TQtModelIndex(selIdx.nodeId, 0)).toString();
            int paren = name.findRev('(');
            if (paren >= 0) name = name.left(paren).stripWhiteSpace();
            m_selectedGroupName = name;
        }
    }

    /* Save expanded groups */
    m_expandedGroups.clear();
    if (m_store) {
        int n = m_store->childCount(TQtTreeStore::RootNodeId);
        for (int i = 0; i < n; ++i) {
            int nid = m_store->childAt(TQtTreeStore::RootNodeId, i);
            if (nid >= 0 && m_treeView->isExpanded(nid)) {
                if (m_store->childCount(nid) > 0) {
                    TQString name = m_store->data(TQtModelIndex(nid, 0)).toString();
                    int paren = name.findRev('(');
                    if (paren >= 0) name = name.left(paren).stripWhiteSpace();
                    m_expandedGroups.insert(name, true);
                }
            }
        }
    }

    /* ---- 4. Rebuild store ---- */
    m_store->beginBatch();
    m_store->clear();

    /* Use the global task_array updated by refresh_task_list() */
    GArray* taskList = task_array;
    if (!taskList || taskList->len == 0) {
        m_store->endBatch();
        m_treeView->unblockPainting();
        return;
    }



    guint16 flags = bridge_get_app_flags();
    uid_t ownUid = bridge_get_own_uid();
    bool showPss = (flags & APP_FLAG_DISPLAY_PSS) != 0;

    /* Filter tasks */
    TQMap<CharPtrKey, TQValueVector<int> > groups;
    for (guint i = 0; i < taskList->len; ++i) {
        uid_t uid = bridge_task_uid(taskList, i);
        bool show = false;
        if (uid == ownUid && (flags & APP_FLAG_SHOW_USER_TASKS)) show = true;
        else if (uid == 0 && (flags & APP_FLAG_SHOW_ROOT_TASKS)) show = true;
        else if (uid != ownUid && uid != 0 && (flags & APP_FLAG_SHOW_OTHER_TASKS)) show = true;
        if (!show) continue;

        const char* groupName = bridge_task_simple_name(taskList, i);
        if (!groupName || groupName[0] == '\0') groupName = "unknown";
        groups[groupName].push_back(i);
    }

    int selectNodeId = -1;
    char sizeBuf[64];
    long jps = get_jiffies_per_second();
    if (jps <= 0) jps = 100;

    /* ---- Populate store ---- */
    if (flags & APP_FLAG_GROUP_PROCS) {
        /* Grouped mode */
        for (TQMap<CharPtrKey, TQValueVector<int> >::Iterator it = groups.begin(); it != groups.end(); ++it) {
            TQString groupName = it.key().str;
            const TQValueVector<int>& indices = it.data();
            int count = indices.size();

            if (count > 1) {
                /* ---- Parent group node ---- */
                TQtRow parentRow(12);
                parentRow[0] = TQString("%1 (%2)").arg(groupName).arg(count);
                
                double totalCpu = 0.0;
                guint64 totalCpuTime = 0;
                guint64 totalMem = 0;
                guint64 totalWs = 0;
                guint64 totalVsz = 0;
                double totalGpu = 0.0;

                int parentNid = m_store->appendNode(TQtTreeStore::RootNodeId, parentRow);
                if (m_selectedPid == 0 && !m_selectedGroupName.isEmpty() && groupName == m_selectedGroupName) {
                    selectNodeId = parentNid;
                }

                TQtCellStyle parentIconStyle;
                parentIconStyle.icon = processListIcon(groupName, skipIcons);
                parentIconStyle.hasIcon = true;
                m_store->setCellStyle(parentNid, 0, parentIconStyle);

                /* ---- Children ---- */
                for (int j = 0; j < count; ++j) {
                    int idx = indices[j];
                    pid_t pid = bridge_task_pid(taskList, idx);
                    pid_t ppid = bridge_task_ppid(taskList, idx);
                    TQString name = bridge_task_name(taskList, idx);
                    TQString simpleName = bridge_task_simple_name(taskList, idx);
                    TQString uname = bridge_task_uname(taskList, idx);
                    int prio = bridge_task_prio(taskList, idx);
                    guint64 taskTime = bridge_task_time(taskList, idx);
                    
                    /* CPU % — read from backend (computed by refresh_task_list) */
                    double cpuPct = bridge_task_cpu(taskList, idx);
                    if (cpuPct > 100.0) cpuPct = 100.0;
                    if (cpuPct < 0.0) cpuPct = 0.0;
                    totalCpu += cpuPct;

                    double gpuPct = bridge_task_gpu(taskList, idx);
                    totalGpu += gpuPct;
                    totalCpuTime += taskTime;

                    guint64 rss = bridge_task_rss(taskList, idx);
                    guint64 pss = bridge_task_pss(taskList, idx);
                    guint64 vsz = bridge_task_vsz(taskList, idx);
                    guint64 memVal = showPss ? (pss > 0 ? pss : rss) : rss;
                    totalMem += memVal;
                    totalWs += rss;
                    totalVsz += vsz;

                    guint64 totalSec = taskTime / jps;
                    guint64 hours = totalSec / 3600;
                    guint64 minutes = (totalSec % 3600) / 60;
                    guint64 seconds = totalSec % 60;
                    char timeStr[32];
                    format_cpu_time_optimized(timeStr, hours, minutes, seconds);

                    TQtRow childRow(12);
                    childRow[0] = name;
                    char stateChar = bridge_task_state(taskList, idx);
                    char stateStr[2] = { stateChar, '\0' };
                    childRow[1] = get_readable_status(stateStr);
                    childRow[2] = uname;
                    childRow[3] = cpuPct > 0.05 ? TQString("%1 %").arg(cpuPct, 0, 'f', 0) : "0 %";
                    childRow[4] = timeStr;
                    format_memory_size(sizeBuf, memVal);
                    childRow[5] = sizeBuf;
                    if (showPss) {
                        format_memory_size(sizeBuf, rss);
                        childRow[6] = sizeBuf;
                    } else {
                        childRow[6] = "";
                    }
                    format_memory_size(sizeBuf, vsz);
                    childRow[7] = sizeBuf;
                    childRow[8] = gpuPct > 0.05 ? TQString("%1 %").arg(gpuPct, 0, 'f', 0) : "0 %";
                    childRow[9] = TQString::number(pid);
                    childRow[10] = TQString::number(prio);
                    childRow[11] = TQString::number(ppid);

                    int childNid = m_store->appendNode(parentNid, childRow);
                    if (pid == m_selectedPid) selectNodeId = childNid;

                    TQtCellStyle childIconStyle;
                    childIconStyle.icon = processListIcon(simpleName, skipIcons);
                    childIconStyle.hasIcon = true;
                    m_store->setCellStyle(childNid, 0, childIconStyle);

                    applyCellColoring(m_store, childNid, cpuPct, totalSec, memVal, rss, vsz, gpuPct, prio);
                }

                /* ---- Parent aggregated data ---- */
                m_store->setData(parentNid, 3, totalCpu > 0.05 ? TQString("%1 %").arg(totalCpu, 0, 'f', 0) : "0 %");
                
                guint64 parentTotalSec = totalCpuTime / jps;
                guint64 ph = parentTotalSec / 3600;
                guint64 pm = (parentTotalSec % 3600) / 60;
                guint64 ps = parentTotalSec % 60;
                char ptimeStr[32];
                format_cpu_time_optimized(ptimeStr, ph, pm, ps);
                m_store->setData(parentNid, 4, ptimeStr);

                format_memory_size(sizeBuf, totalMem);
                m_store->setData(parentNid, 5, sizeBuf);
                if (showPss) {
                    format_memory_size(sizeBuf, totalWs);
                    m_store->setData(parentNid, 6, sizeBuf);
                }
                format_memory_size(sizeBuf, totalVsz);
                m_store->setData(parentNid, 7, sizeBuf);
                m_store->setData(parentNid, 8, totalGpu > 0.05 ? TQString("%1 %").arg(totalGpu, 0, 'f', 0) : "0 %");

                /* Restore expanded state */
                if (m_expandedGroups.contains(groupName))
                    m_treeView->setExpandedNoRebuild(parentNid, true);

                applyCellColoring(m_store, parentNid, totalCpu, parentTotalSec, totalMem, totalWs, totalVsz, totalGpu, 0);

            } else {
                /* ---- Single process (count == 1) ---- */
                int idx = indices[0];
                pid_t pid = bridge_task_pid(taskList, idx);
                pid_t ppid = bridge_task_ppid(taskList, idx);
                TQString name = bridge_task_name(taskList, idx);
                TQString simpleName = bridge_task_simple_name(taskList, idx);
                TQString uname = bridge_task_uname(taskList, idx);
                int prio = bridge_task_prio(taskList, idx);
                guint64 taskTime = bridge_task_time(taskList, idx);
                
                double cpuPct = bridge_task_cpu(taskList, idx);
                if (cpuPct > 100.0) cpuPct = 100.0;
                if (cpuPct < 0.0) cpuPct = 0.0;

                double gpuPct = bridge_task_gpu(taskList, idx);
                guint64 rss = bridge_task_rss(taskList, idx);
                guint64 pss = bridge_task_pss(taskList, idx);
                guint64 vsz = bridge_task_vsz(taskList, idx);
                guint64 memVal = showPss ? (pss > 0 ? pss : rss) : rss;

                guint64 totalSec = taskTime / jps;
                guint64 hours = totalSec / 3600;
                guint64 minutes = (totalSec % 3600) / 60;
                guint64 seconds = totalSec % 60;
                char timeStr[32];
                format_cpu_time_optimized(timeStr, hours, minutes, seconds);

                TQtRow row(12);
                row[0] = name;
                char stateChar = bridge_task_state(taskList, idx);
                char stateStr[2] = { stateChar, '\0' };
                row[1] = get_readable_status(stateStr);
                row[2] = uname;
                row[3] = cpuPct > 0.05 ? TQString("%1 %").arg(cpuPct, 0, 'f', 0) : "0 %";
                row[4] = timeStr;
                format_memory_size(sizeBuf, memVal);
                row[5] = sizeBuf;
                if (showPss) {
                    format_memory_size(sizeBuf, rss);
                    row[6] = sizeBuf;
                } else {
                    row[6] = "";
                }
                format_memory_size(sizeBuf, vsz);
                row[7] = sizeBuf;
                row[8] = gpuPct > 0.05 ? TQString("%1 %").arg(gpuPct, 0, 'f', 0) : "0 %";
                row[9] = TQString::number(pid);
                row[10] = TQString::number(prio);
                row[11] = TQString::number(ppid);

                int nid = m_store->appendNode(TQtTreeStore::RootNodeId, row);
                if (m_selectedPid > 0 && pid == m_selectedPid) {
                    selectNodeId = nid;
                } else if (m_selectedPid == 0 && !m_selectedGroupName.isEmpty() && simpleName == m_selectedGroupName) {
                    selectNodeId = nid;
                }

                TQtCellStyle iconStyle;
                iconStyle.icon = processListIcon(simpleName, skipIcons);
                iconStyle.hasIcon = true;
                m_store->setCellStyle(nid, 0, iconStyle);

                applyCellColoring(m_store, nid, cpuPct, totalSec, memVal, rss, vsz, gpuPct, prio);
            }
        }
    } else {
        /* ---- Flat mode (group processes disabled) ---- */
        for (TQMap<CharPtrKey, TQValueVector<int> >::Iterator it = groups.begin(); it != groups.end(); ++it) {
            const TQValueVector<int>& indices = it.data();
            for (int j = 0; j < (int)indices.size(); ++j) {
                int idx = indices[j];
                pid_t pid = bridge_task_pid(taskList, idx);
                pid_t ppid = bridge_task_ppid(taskList, idx);
                TQString name = bridge_task_name(taskList, idx);
                TQString simpleName = bridge_task_simple_name(taskList, idx);
                TQString uname = bridge_task_uname(taskList, idx);
                int prio = bridge_task_prio(taskList, idx);
                guint64 taskTime = bridge_task_time(taskList, idx);
                
                double cpuPct = bridge_task_cpu(taskList, idx);
                if (cpuPct > 100.0) cpuPct = 100.0;
                if (cpuPct < 0.0) cpuPct = 0.0;

                double gpuPct = bridge_task_gpu(taskList, idx);
                guint64 rss = bridge_task_rss(taskList, idx);
                guint64 pss = bridge_task_pss(taskList, idx);
                guint64 vsz = bridge_task_vsz(taskList, idx);
                guint64 memVal = showPss ? (pss > 0 ? pss : rss) : rss;

                guint64 totalSec = taskTime / jps;
                guint64 hours = totalSec / 3600;
                guint64 minutes = (totalSec % 3600) / 60;
                guint64 seconds = totalSec % 60;
                char timeStr[32];
                format_cpu_time_optimized(timeStr, hours, minutes, seconds);

                TQtRow row(12);
                row[0] = name;
                char stateChar = bridge_task_state(taskList, idx);
                char stateStr[2] = { stateChar, '\0' };
                row[1] = get_readable_status(stateStr);
                row[2] = uname;
                row[3] = cpuPct > 0.05 ? TQString("%1 %").arg(cpuPct, 0, 'f', 0) : "0 %";
                row[4] = timeStr;
                format_memory_size(sizeBuf, memVal);
                row[5] = sizeBuf;
                if (showPss) {
                    format_memory_size(sizeBuf, rss);
                    row[6] = sizeBuf;
                } else {
                    row[6] = "";
                }
                format_memory_size(sizeBuf, vsz);
                row[7] = sizeBuf;
                row[8] = gpuPct > 0.05 ? TQString("%1 %").arg(gpuPct, 0, 'f', 0) : "0 %";
                row[9] = TQString::number(pid);
                row[10] = TQString::number(prio);
                row[11] = TQString::number(ppid);

                int nid = m_store->appendNode(TQtTreeStore::RootNodeId, row);
                if (pid == m_selectedPid) selectNodeId = nid;

                TQtCellStyle iconStyle;
                iconStyle.icon = processListIcon(simpleName, skipIcons);
                iconStyle.hasIcon = true;
                m_store->setCellStyle(nid, 0, iconStyle);

                applyCellColoring(m_store, nid, cpuPct, totalSec, memVal, rss, vsz, gpuPct, prio);
            }
        }
    }

    /* Apply sorting if user has chosen a sort column */
    if (m_treeView->sortColumn() >= 0)
        m_store->sortAllChildren(m_treeView->sortColumn(), m_treeView->sortAscending());

    m_store->endBatch();

    /* Hide/show Working Set column */
    if (showPss) {
        m_treeView->showColumn(6);
    } else {
        m_treeView->hideColumn(6);
    }

    /* ---- 5. Restore UI state (selection + scroll) WITHOUT auto-scroll ---- */
    if (selectNodeId >= 0) {
        m_treeView->selectIndex(TQtModelIndex(selectNodeId, 0));
    } else {
        m_treeView->clearSelection();
    }

    /* Restore scroll position exactly where it was */
    m_treeView->setContentsPos(savedScrollX, savedScrollY);

    /* ---- 6. Unfreeze — single repaint ---- */
    m_treeView->unblockPainting();

    /* NOTE: Do NOT free task_array — it is the global managed by refresh_task_list() */
}

void ProcessesTab::refreshCompact()
{
    bridge_refresh_tasks();

    m_treeView->blockPainting();

    int savedScrollX = m_treeView->contentsX();
    int savedScrollY = m_treeView->contentsY();

    m_selectedPid = 0;
    m_selectedWindowId = 0;
    TQtModelIndex selIdx = m_treeView->selectedIndex();
    if (selIdx.isValid()) {
        TQString pidStr = m_store->data(TQtModelIndex(selIdx.nodeId, 9)).toString();
        bool ok;
        int pid = pidStr.toInt(&ok);
        if (ok && pid > 0)
            m_selectedPid = pid;

        TQString winIdStr = m_store->data(TQtModelIndex(selIdx.nodeId, 10)).toString();
        unsigned long winId = winIdStr.toULong(&ok);
        if (ok && winId > 0)
            m_selectedWindowId = winId;
    }

    m_store->beginBatch();
    m_store->clear();
    m_compactMemberPids.clear();

    GArray* taskList = task_array;
    if (!taskList || taskList->len == 0) {
        m_store->endBatch();
        m_treeView->unblockPainting();
        return;
    }

    uid_t ownUid = bridge_get_own_uid();
    (void)ownUid;

    TQValueList<CompactAppEntry> apps = CompactAppList::build(taskList, ownUid);
    int selectNodeId = -1;

    for (TQValueList<CompactAppEntry>::ConstIterator it = apps.begin(); it != apps.end(); ++it) {
        const CompactAppEntry& app = *it;
        if (app.taskIndices.isEmpty())
            continue;

        TQValueVector<pid_t> memberPids;
        for (unsigned j = 0; j < app.taskIndices.size(); ++j) {
            int idx = app.taskIndices[j];
            memberPids.push_back(bridge_task_pid(taskList, (guint)idx));
        }
        m_compactMemberPids.insert(app.repPid, memberPids);

        TQtRow row(12);
        row[0] = app.displayName;
        row[9] = TQString::number(app.repPid);
        row[10] = TQString::number(app.windowId);

        int nid = m_store->appendNode(TQtTreeStore::RootNodeId, row);
        if (app.repPid == m_selectedPid && app.windowId == m_selectedWindowId)
            selectNodeId = nid;

        TQtCellStyle iconStyle;
        iconStyle.icon = TdeIconLoader::windowIcon(app.wmClass, app.wmInstance, app.iconKey);
        iconStyle.hasIcon = true;
        m_store->setCellStyle(nid, 0, iconStyle);
    }

    int sortCol = m_treeView->sortColumn();
    bool sortAsc = m_treeView->sortAscending();
    if (sortCol < 0) {
        sortCol = 0;
        sortAsc = true;
    }
    m_store->sortAllChildren(sortCol, sortAsc);

    m_store->endBatch();

    if (selectNodeId >= 0)
        m_treeView->selectIndex(TQtModelIndex(selectNodeId, 0));
    else
        m_treeView->clearSelection();

    updateCompactColumnWidth();
    m_treeView->setContentsPos(savedScrollX, savedScrollY);
    m_treeView->unblockPainting();
}

/* ====================================================================
 * Button slots
 * ==================================================================== */

void ProcessesTab::onEndTaskClicked()
{
    onContextEndTask();
}

void ProcessesTab::onDetailsClicked()
{
    emit detailsToggleRequested();
}

TQValueVector<pid_t> ProcessesTab::getSelectedMemberPids() const
{
    pid_t pid = getSelectedPid();
    TQValueVector<pid_t> pids;
    if (pid <= 0)
        return pids;

    if (m_compactMode && m_compactMemberPids.contains(pid))
        return m_compactMemberPids[pid];

    pids.push_back(pid);
    return pids;
}

/* ====================================================================
 * Context menu
 * ==================================================================== */

void ProcessesTab::onRowContextMenuRequested(const TQtModelIndex& index, int col, const TQPoint& globalPos)
{
    (void)col;
    if (!index.isValid())
        return;

    const int nodeId = index.nodeId;
    const bool isGroup = isGroupNode(nodeId);
    TQValueVector<pid_t> targetPids;

    if (isGroup) {
        targetPids = collectGroupPids(nodeId);
        if (targetPids.isEmpty())
            return;
    } else {
        TQString pidStr = m_store->data(TQtModelIndex(nodeId, 9)).toString();
        bool ok;
        int pid = pidStr.toInt(&ok);
        if (!ok || pid <= 0)
            return;
        targetPids.push_back((pid_t)pid);
        m_selectedPid = pid;
    }

    TQPopupMenu* menu = new TQPopupMenu(this);

    if (m_compactMode) {
        menu->insertItem("&End Task", this, SLOT(onContextEndTask()));
        menu->insertSeparator();
        menu->insertItem("Open file location", this, SLOT(onContextOpenFileLocation()));
        menu->insertItem("Search online", this, SLOT(onContextSearchOnline()));
    } else {
        const bool stopped = areAllTargetsStopped(targetPids);
        if (stopped)
            menu->insertItem("&Continue", this, SLOT(onContextContinue()));
        else
            menu->insertItem("&Stop", this, SLOT(onContextStop()));

        menu->insertItem("&Terminate", this, SLOT(onContextTerminate()));

        if (isGroup)
            menu->insertItem("&End Tasks", this, SLOT(onContextEndTask()));
        else
            menu->insertItem("&End Task", this, SLOT(onContextEndTask()));

        if (!isGroup) {
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
        }
    }

    menu->exec(globalPos);
    delete menu;
}

void ProcessesTab::onDoubleClicked(int row, int col, int button, const TQPoint& mousePos)
{
    (void)row; (void)col; (void)button; (void)mousePos;
}

void ProcessesTab::onContextStop()
{
    applySignalToTargets(SIGSTOP, false, TQString());
}

void ProcessesTab::onContextContinue()
{
    applySignalToTargets(SIGCONT, false, TQString());
}

void ProcessesTab::onContextTerminate()
{
    TQValueVector<pid_t> pids = getTargetPidsForAction();
    if (pids.isEmpty())
        return;

    TQString message;
    if (pids.size() > 1)
        message = TQString("Really terminate all %1 processes in this group?").arg(pids.size());
    else
        message = "Really terminate the task?";

    applySignalToTargets(SIGINT, true, message);
}

void ProcessesTab::onContextEndTask()
{
    TQValueVector<pid_t> pids = getTargetPidsForAction();
    if (pids.isEmpty())
        return;

    TQString message;
    if (pids.size() > 1)
        message = TQString("Really kill all %1 processes in this group?").arg(pids.size());
    else
        message = "Really kill the task?";

    applySignalToTargets(SIGKILL, true, message);
}

void ProcessesTab::setProcessPriority(int priorityVal)
{
    pid_t pid = getSelectedPid();
    if (pid <= 0) return;

    TaskmgrPrivilegedOps::setProcessPriority(this, pid, priorityVal);
    refresh();
}

void ProcessesTab::onContextPrioCritical() { setProcessPriority(-20); }
void ProcessesTab::onContextPrioVeryHigh() { setProcessPriority(-15); }
void ProcessesTab::onContextPrioHigh()     { setProcessPriority(-5);  }
void ProcessesTab::onContextPrioNormal()   { setProcessPriority(0);   }
void ProcessesTab::onContextPrioLow()      { setProcessPriority(10);  }
void ProcessesTab::onContextPrioVeryLow()  { setProcessPriority(19);  }

void ProcessesTab::onContextOpenFileLocation()
{
    pid_t pid = getSelectedPid();
    if (pid <= 0) return;

    char exePath[PATH_MAX];
    char linkPath[128];
    snprintf(linkPath, sizeof(linkPath), "/proc/%d/exe", pid);
    ssize_t len = readlink(linkPath, exePath, sizeof(exePath) - 1);

    if (len != -1) {
        exePath[len] = '\0';
        TQString path(exePath);
        int lastSlash = path.findRev('/');
        if (lastSlash >= 0) {
            TQString dirPath = path.left(lastSlash);
            taskmgr_launch_open_directory(dirPath.latin1());
        }
    } else {
        TQMessageBox::critical(this, "Error", "Failed to read process executable path.");
    }
}

void ProcessesTab::onContextSearchOnline()
{
    TQtModelIndex selIdx = m_treeView->selectedIndex();
    if (!selIdx.isValid()) return;

    TQString procName = m_store->data(TQtModelIndex(selIdx.nodeId, 0)).toString();
    int paren = procName.findRev('(');
    if (paren >= 0) procName = procName.left(paren).stripWhiteSpace();

    TQString url = TQString("https://www.google.com/search?q=%1").arg(procName);
    if (!taskmgr_launch_open_url(url.latin1())) {
        TQMessageBox::critical(this, "Error", "Failed to open the web browser.");
    }
}

void ProcessesTab::onContextDetails()
{
    pid_t pid = getSelectedPid();
    if (pid <= 0) return;

    ProcessDetailsDialog dlg(pid, this);
    dlg.exec();
}

#include "processes_tab.moc"
