/*
 * users_tab.h — Users tab for taskmgr TQt3 port.
 *
 * Tree of logged-in users (collapsed by default) with per-user process lists.
 */

#ifndef USERS_TAB_H
#define USERS_TAB_H

#include <ntqwidget.h>
#include <ntqlayout.h>
#include <ntqmap.h>
#include <ntqstring.h>
#include <ntqvaluevector.h>
#include "backend_bridge.h"

class TQtMvcTreeView;
class TQtTreeStore;
class TQtModelIndex;

class UsersTab : public TQWidget {
    TQ_OBJECT

public:
    UsersTab(TQWidget* parent = 0, const char* name = 0);
    virtual ~UsersTab();

    void refresh();
    int getUserCount() const;
    TQtMvcTreeView* treeView() const { return m_treeView; }

private slots:
    void onHeaderClicked(int col);
    void onRowContextMenuRequested(const TQtModelIndex& index, int col, const TQPoint& globalPos);
    void onDoubleClicked(int row, int col, int button, const TQPoint& mousePos);
    void onContextDisconnect();
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
    void onDelayedRefresh();

private:
    enum SortCriteria {
        SortName = 0,
        SortCpu,
        SortMemory,
        SortGpu,
        SortPid
    };

    void setupColumns();
    void applyUsersCellColoring(TQtTreeStore* store, int nid, double cpuPct,
                                const char* memDisplayStr, double gpuPct,
                                bool isUserNode);
    pid_t getSelectedPid() const;
    TQString getSelectedUsername() const;
    bool isUserNode(int nodeId) const;
    void sortTaskIndices(TQValueVector<int>& indices, GArray* taskList);
    void setProcessPriority(int priorityVal);

    TQtMvcTreeView* m_treeView;
    TQtTreeStore* m_store;
    SortCriteria m_sortCriteria;
    guint8 m_sortFlags;
    TQMap<TQString, bool> m_expandedUsers;
    pid_t m_selectedPid;
    TQString m_selectedUsername;
};

#endif
