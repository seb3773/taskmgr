/*
 * startup_tab.h — Startup tab for taskmgr TQt3 port.
 *
 * Implements the startup applications list using TQtListStore and TQtMvcTableView.
 */

#ifndef STARTUP_TAB_H
#define STARTUP_TAB_H

#include <ntqwidget.h>
#include <ntqlayout.h>
#include <ntqpoint.h>
#include <ntqstring.h>
#include "autostart_manager.h"

class TQtMvcTableView;
class TQtListStore;
class TQPopupMenu;

class StartupTab : public TQWidget {
    TQ_OBJECT

public:
    StartupTab(TQWidget* parent = 0, const char* name = 0);
    virtual ~StartupTab();

    /* Refresh the startup list from the backend */
    void refresh();

    /* Count enabled and disabled startup entries */
    void getCounts(int& enabledCount, int& disabledCount) const;

    /* Accessor for the table view */
    TQtMvcTableView* tableView() const { return m_tableView; }

private slots:
    void onRowContextMenuRequested(int modelRow, int col, const TQPoint& globalPos);
    void onDoubleClicked(int row, int col, int button, const TQPoint& mousePos);
    
    /* Context menu action slots */
    void onContextEnableDisable();
    void onContextOpenFileLocation();
    void onContextEdit();
    void onContextDelete();

private:
    void setupColumns();
    int getSelectedModelRow() const;

    TQtMvcTableView* m_tableView;
    TQtListStore* m_store;
    int m_selectedRow;
};

#endif // STARTUP_TAB_H
