/*
 * scheduled_tab.h — Scheduled tasks & cron tab for taskmgr TQt3 port.
 *
 * Implements the scheduled tasks list using TQtListStore and TQtMvcTableView.
 */

#ifndef SCHEDULED_TAB_H
#define SCHEDULED_TAB_H

#include <ntqwidget.h>
#include <ntqlayout.h>
#include <ntqpoint.h>
#include <ntqstring.h>
#include <time.h>
#include "cron_manager.h"

class TQtMvcTableView;
class TQtListStore;
class TQPopupMenu;
class TQTimer;

class ScheduledTab : public TQWidget {
    TQ_OBJECT

public:
    ScheduledTab(TQWidget* parent = 0, const char* name = 0);
    virtual ~ScheduledTab();

    /* Check if entries have been retrieved at least once */
    bool isLoaded() const { return m_loaded; }

    /* Refresh the scheduled tasks list from the backend */
    void refresh();

    /* Count enabled and disabled scheduled tasks */
    void getCounts(int& enabledCount, int& disabledCount) const;

    /* Accessor for the table view */
    TQtMvcTableView* tableView() const { return m_tableView; }

private slots:
    void onRowContextMenuRequested(int modelRow, int col, const TQPoint& globalPos);
    void onDoubleClicked(int row, int col, int button, const TQPoint& mousePos);

    /* Context menu action slots */
    void onContextEnableDisable();
    void onContextRunNow();
    void onContextEdit();
    void onContextDelete();
    void onContextRefresh();

    /* Timer slot for user crontab file edits */
    void onWatchTimer();

private:
    void setupColumns();
    int getSelectedModelRow() const;
    void editCurrentUserCrontab();

    TQtMvcTableView* m_tableView;
    TQtListStore* m_store;
    int m_selectedRow;

    CronEntry* m_entries;
    size_t m_entriesCount;
    bool m_loaded;

    TQString m_watchedUserCrontabPath;
    time_t m_watchedUserCrontabMtime;
    TQTimer* m_watchTimer;
};

#endif // SCHEDULED_TAB_H
