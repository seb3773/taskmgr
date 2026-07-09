/*
 * services_tab.h — Services tab for taskmgr TQt3 port.
 *
 * Implements the systemd services list using TQtListStore and TQtMvcTableView.
 */

#ifndef SERVICES_TAB_H
#define SERVICES_TAB_H

#include <ntqwidget.h>
#include <ntqlayout.h>
#include <ntqpoint.h>
#include <ntqstring.h>
#include "service_manager.h"

class TQtMvcTableView;
class TQtListStore;
class TQPopupMenu;

class ServicesTab : public TQWidget {
    TQ_OBJECT

public:
    ServicesTab(TQWidget* parent = 0, const char* name = 0);
    virtual ~ServicesTab();

    /* Refresh the services list from the backend */
    void refresh();

    /* Accessor for the table view */
    TQtMvcTableView* tableView() const { return m_tableView; }

private slots:
    void onRowContextMenuRequested(int modelRow, int col, const TQPoint& globalPos);
    void onDoubleClicked(int row, int col, int button, const TQPoint& mousePos);
    
    /* Context menu action slots */
    void onContextStart();
    void onContextStop();
    void onContextRestart();
    void onContextEnable();
    void onContextDisable();
    void onContextSearchOnline();
    void onContextDetails();
    void onContextEdit();

private:
    void setupColumns();
    int getSelectedModelRow() const;

    TQtMvcTableView* m_tableView;
    TQtListStore* m_store;
    int m_selectedRow;
};

#endif // SERVICES_TAB_H
