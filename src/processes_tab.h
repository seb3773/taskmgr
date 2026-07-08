/*
 * processes_tab.h — Processes tab for taskmgr TQt3 port.
 *
 * Implements the process tree table using TQtTreeStore and TQtMvcTreeView.
 */

#ifndef PROCESSES_TAB_H
#define PROCESSES_TAB_H

#include <ntqwidget.h>
#include <ntqlayout.h>
#include <ntqmap.h>
#include <ntqpoint.h>
#include <ntqstring.h>
#include <ntqvaluevector.h>
#include <ntqpushbutton.h>
#include <sys/types.h>
#include "backend_bridge.h"

class TQtMvcTreeView;
class TQtTreeStore;
class TQtModelIndex;
class TQPopupMenu;
class TQResizeEvent;

class ProcessesTab : public TQWidget {
    TQ_OBJECT

public:
    ProcessesTab(TQWidget* parent = 0, const char* name = 0);
    virtual ~ProcessesTab();

    /* Refresh the process list from the backend */
    void refresh();

    /* Startup fast path: one /proc scan, generic icons; then refreshIconsOnly() */
    void refreshLight();
    void refreshIconsOnly();

    /* Less/More details compact mode */
    void setCompactMode(bool compact);
    bool isCompactMode() const { return m_compactMode; }

    /* Accessor for the tree view (needed for header stats alignment) */
    TQtMvcTreeView* treeView() const { return m_treeView; }

signals:
    void detailsToggleRequested();

private slots:
    void onRowContextMenuRequested(const TQtModelIndex& index, int col, const TQPoint& globalPos);
    void onDoubleClicked(int row, int col, int button, const TQPoint& mousePos);
    
    /* Button slots */
    void onEndTaskClicked();
    void onDetailsClicked();
    
    /* Context menu action slots */
    void onContextStop();
    void onContextContinue();
    void onContextTerminate();
    void onContextEndTask();
    void onContextPrioCritical();
    void onContextPrioVeryHigh();
    void onContextPrioHigh();
    void onContextPrioNormal();
    void onContextPrioLow();
    void onContextPrioVeryLow();
    void onContextOpenFileLocation();
    void onContextSearchOnline();
    void onContextDetails();

private:
    void setupColumns();
    void applyColumnLayout();
    void updateCompactColumnWidth();
    void refreshFull(bool skipIcons = false);
    void refreshCompact();
    void applyIconsToNode(int nodeId);
    void setProcessPriority(int priorityVal);
    pid_t getSelectedPid() const;
    TQValueVector<pid_t> getSelectedMemberPids() const;
    TQValueVector<pid_t> getTargetPidsForAction() const;
    bool isGroupNode(int nodeId) const;
    TQValueVector<pid_t> collectGroupPids(int groupNodeId) const;
    bool isPidStopped(pid_t pid) const;
    bool areAllTargetsStopped(const TQValueVector<pid_t>& pids) const;
    bool applySignalToTargets(int signal, bool confirm, const TQString& confirmMessage);

protected:
    void resizeEvent(TQResizeEvent* e);

    TQtMvcTreeView* m_treeView;
    TQtTreeStore* m_store;
    TQPushButton* m_endTaskBtn;
    TQPushButton* m_detailsBtn;
    bool m_compactMode;
    
    /* Saved expanded group names to preserve tree state across refreshes */
    TQMap<TQString, bool> m_expandedGroups;

    /* Track selected PID or group name to restore selection after refresh */
    pid_t m_selectedPid;
    unsigned long m_selectedWindowId;
    TQString m_selectedGroupName;

    /* Compact mode: representative PID -> all member PIDs to terminate */
    TQMap<pid_t, TQValueVector<pid_t> > m_compactMemberPids;
};

#endif // PROCESSES_TAB_H
