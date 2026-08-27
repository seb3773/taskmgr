/*
 * scheduled_tab.cpp — Scheduled tasks & cron tab for taskmgr TQt3 port.
 *
 * Implements the scheduled tasks list using TQtListStore and TQtMvcTableView.
 */

#include "scheduled_tab.h"
#include "mvc/tqtliststore.h"
#include "mvc/tqtmvctableview.h"
#include "backend_bridge.h"
#include "cron_manager.h"
#include "taskmgr_privileged_ops.h"
#include "root_credential_vault.h"
#include "process_launcher.h"
#include "tde_icon_loader.h"

#include <ntqpopupmenu.h>
#include <ntqmessagebox.h>
#include <ntqapplication.h>
#include <ntqheader.h>
#include <ntqtimer.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <stdlib.h>

/* ====================================================================
 * ScheduledTab — Constructor & Destructor
 * ==================================================================== */

ScheduledTab::ScheduledTab(TQWidget* parent, const char* name)
    : TQWidget(parent, name),
      m_selectedRow(-1),
      m_entries(NULL),
      m_entriesCount(0),
      m_loaded(false),
      m_watchedUserCrontabMtime(0)
{
    TQVBoxLayout* layout = new TQVBoxLayout(this, 0, 4);

    /* Model (6 columns: Name, Status, Schedule, Command, User, Source) */
    m_store = new TQtListStore(6, this);
    setupColumns();

    /* View */
    m_tableView = new TQtMvcTableView(this);
    m_tableView->setModel(m_store);
    m_tableView->setSortingEnabled(true);
    m_tableView->setFrameStyle(TQFrame::NoFrame);

    /* Set default column widths */
    m_tableView->setColumnWidth(0, 210); // Name
    m_tableView->setColumnWidth(1, 85);  // Status
    m_tableView->setColumnWidth(2, 190); // Schedule
    m_tableView->setColumnWidth(3, 260); // Command
    m_tableView->setColumnWidth(4, 80);  // User
    m_tableView->setColumnWidth(5, 180); // Source

    layout->addWidget(m_tableView, 1);

    m_watchTimer = new TQTimer(this);
    connect(m_watchTimer, SIGNAL(timeout()), this, SLOT(onWatchTimer()));

    connect(m_tableView, SIGNAL(rowContextMenuRequested(int, int, const TQPoint&)),
            this,        SLOT(onRowContextMenuRequested(int, int, const TQPoint&)));

    connect(m_tableView, SIGNAL(doubleClicked(int, int, int, const TQPoint&)),
            this,        SLOT(onDoubleClicked(int, int, int, const TQPoint&)));
}

ScheduledTab::~ScheduledTab()
{
    if (m_watchTimer) {
        m_watchTimer->stop();
    }
    if (!m_watchedUserCrontabPath.isEmpty()) {
        unlink(m_watchedUserCrontabPath.local8Bit());
    }
    if (m_entries) {
        free_cron_entries(m_entries, m_entriesCount);
        m_entries = NULL;
        m_entriesCount = 0;
    }
}

void ScheduledTab::setupColumns()
{
    m_store->setHeader(0, "Name");
    m_store->setHeader(1, "Status");
    m_store->setHeader(2, "Schedule");
    m_store->setHeader(3, "Command");
    m_store->setHeader(4, "User");
    m_store->setHeader(5, "Source");
}

static const char* source_type_to_string(CronSourceType type, const char* path)
{
    switch (type) {
        case CRON_SOURCE_USER:
            return "User Crontab";
        case CRON_SOURCE_SYSTEM_CRONTAB:
            return "System (/etc/crontab)";
        case CRON_SOURCE_CRON_D:
            return "System Drop-in (/etc/cron.d)";
        case CRON_SOURCE_CRON_HOURLY:
            return "Hourly Script (/etc/cron.hourly)";
        case CRON_SOURCE_CRON_DAILY:
            return "Daily Script (/etc/cron.daily)";
        case CRON_SOURCE_CRON_WEEKLY:
            return "Weekly Script (/etc/cron.weekly)";
        case CRON_SOURCE_CRON_MONTHLY:
            return "Monthly Script (/etc/cron.monthly)";
        default:
            return path ? path : "Cron";
    }
}

void ScheduledTab::refresh()
{
    m_loaded = true;
    m_tableView->blockPainting();

    m_store->clear();

    if (m_entries) {
        free_cron_entries(m_entries, m_entriesCount);
        m_entries = NULL;
        m_entriesCount = 0;
    }

    m_entries = get_cron_entries(&m_entriesCount);

    if (m_entries && m_entriesCount > 0) {
        m_store->beginBatch();
        for (size_t i = 0; i < m_entriesCount; ++i) {
            TQtRow row(6);
            row[0] = TQVariant(m_entries[i].name);
            row[1] = TQVariant(m_entries[i].enabled ? "Enabled" : "Disabled");
            row[2] = TQVariant(m_entries[i].schedule_human);
            row[3] = TQVariant(m_entries[i].command);
            row[4] = TQVariant(m_entries[i].user);
            row[5] = TQVariant(source_type_to_string(m_entries[i].source_type, m_entries[i].source_path));

            m_store->appendRow(row);

            // Icon for the Name column
            TQtCellStyle style;
            TQPixmap icon = TdeIconLoader::autostartIcon(m_entries[i].command);
            if (icon.isNull())
                icon = TdeIconLoader::namedIcon("clock", "date");
            style.setIcon(icon);
            m_store->setCellStyle(i, 0, style);

            // Conditional coloring for Status column
            TQtCellStyle statusStyle;
            statusStyle.hasBackground = true;
            if (m_entries[i].enabled) {
                statusStyle.background = TQColor(225, 245, 225); // Soft green
            } else {
                statusStyle.background = TQColor(255, 230, 230); // Soft red
            }
            m_store->setCellStyle(i, 1, statusStyle);
        }
        m_store->endBatch();
    }

    m_tableView->unblockPainting();
}

void ScheduledTab::getCounts(int& enabledCount, int& disabledCount) const
{
    enabledCount = 0;
    disabledCount = 0;
    int rows = m_store->rowCount();
    for (int i = 0; i < rows; ++i) {
        TQString status = m_store->data(i, 1).toString();
        if (status == "Enabled") {
            enabledCount++;
        } else {
            disabledCount++;
        }
    }
}

int ScheduledTab::getSelectedModelRow() const
{
    if (m_selectedRow >= 0 && m_selectedRow < m_store->rowCount())
        return m_selectedRow;
    return -1;
}

void ScheduledTab::onRowContextMenuRequested(int modelRow, int col, const TQPoint& globalPos)
{
    (void)col;
    if (modelRow < 0 || modelRow >= m_store->rowCount() || (size_t)modelRow >= m_entriesCount)
        return;

    m_selectedRow = modelRow;

    TQPopupMenu menu(this);

    int isEnabled = m_entries[modelRow].enabled;
    if (isEnabled) {
        menu.insertItem("Disable", this, SLOT(onContextEnableDisable()));
    } else {
        menu.insertItem("Enable", this, SLOT(onContextEnableDisable()));
    }

    menu.insertItem("Run Now", this, SLOT(onContextRunNow()));
    menu.insertSeparator();
    menu.insertItem("Edit File...", this, SLOT(onContextEdit()));
    menu.insertItem("Delete", this, SLOT(onContextDelete()));
    menu.insertSeparator();
    menu.insertItem("Refresh", this, SLOT(onContextRefresh()));

    menu.exec(globalPos);
}

void ScheduledTab::onDoubleClicked(int row, int col, int button, const TQPoint& mousePos)
{
    (void)col;
    (void)button;
    (void)mousePos;
    if (row < 0 || row >= m_store->rowCount()) return;

    m_selectedRow = row;
    onContextEdit();
}

void ScheduledTab::onContextEnableDisable()
{
    int row = getSelectedModelRow();
    if (row < 0 || (size_t)row >= m_entriesCount) return;

    int new_state = !m_entries[row].enabled;
    char errmsg[256] = {0};

    int ret = TaskmgrPrivilegedOps::toggleCronEntry(this, &m_entries[row], new_state, errmsg, sizeof(errmsg));
    if (ret == 0) {
        refresh();
    } else {
        TQMessageBox::critical(this, "Error",
            TQString("Failed to %1 scheduled task:\n%2\n\n%3")
            .arg(new_state ? "enable" : "disable")
            .arg(m_entries[row].name)
            .arg(errmsg[0] ? errmsg : "Permission denied or operation failed."));
    }
}

void ScheduledTab::onContextRunNow()
{
    int row = getSelectedModelRow();
    if (row < 0 || (size_t)row >= m_entriesCount) return;

    const char* cmd = m_entries[row].command;
    if (!cmd || !*cmd) return;

    if (run_cron_command(cmd) != 0) {
        TQMessageBox::critical(this, "Execution Failed",
            TQString("Failed to run scheduled command:\n%1").arg(cmd));
    }
}

void ScheduledTab::editCurrentUserCrontab()
{
    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);
    const char *cur_user = pw ? pw->pw_name : "user";

    TQString tmpPath = TQString("/tmp/crontab.%1.taskmgr").arg(cur_user);
    TQString exportCmd = TQString("crontab -l > \"%1\" 2>/dev/null").arg(tmpPath);
    system(exportCmd.local8Bit());
    chmod(tmpPath.local8Bit(), 0600);

    struct stat st;
    if (stat(tmpPath.local8Bit(), &st) == 0) {
        m_watchedUserCrontabMtime = st.st_mtime;
    } else {
        m_watchedUserCrontabMtime = 0;
    }

    m_watchedUserCrontabPath = tmpPath;
    m_watchTimer->start(1000);

    if (!taskmgr_launch_edit_file(tmpPath.local8Bit())) {
        TQMessageBox::critical(this, "Error",
            TQString("Failed to open file in editor:\n%1").arg(tmpPath));
    }
}

void ScheduledTab::onWatchTimer()
{
    if (m_watchedUserCrontabPath.isEmpty()) {
        m_watchTimer->stop();
        return;
    }

    struct stat st;
    if (stat(m_watchedUserCrontabPath.local8Bit(), &st) == 0) {
        if (m_watchedUserCrontabMtime != 0 && st.st_mtime != m_watchedUserCrontabMtime) {
            m_watchedUserCrontabMtime = st.st_mtime;
            TQString importCmd = TQString("crontab \"%1\" 2>/dev/null").arg(m_watchedUserCrontabPath);
            system(importCmd.local8Bit());
            refresh();
        }
    }
}

void ScheduledTab::onContextEdit()
{
    int row = getSelectedModelRow();
    if (row < 0 || (size_t)row >= m_entriesCount) return;

    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);
    const char *cur_user = pw ? pw->pw_name : "";

    if (!root_mode_is_active() && m_entries[row].source_type == CRON_SOURCE_USER &&
        strcmp(m_entries[row].user, cur_user) == 0) {
        editCurrentUserCrontab();
        return;
    }

    const char* path = m_entries[row].source_path;
    if (!path || !*path) return;

    if (!TaskmgrPrivilegedOps::editCronFile(this, path)) {
        TQMessageBox::critical(this, "Error",
            TQString("Failed to open file in editor:\n%1").arg(path));
    }
}

void ScheduledTab::onContextDelete()
{
    int row = getSelectedModelRow();
    if (row < 0 || (size_t)row >= m_entriesCount) return;

    TQString name = m_entries[row].name;
    TQString cmd = m_entries[row].command;

    int response = TQMessageBox::question(this, "Confirm Delete",
        TQString("Are you sure you want to delete the scheduled task '%1'?\n\nCommand: %2")
        .arg(name).arg(cmd),
        TQMessageBox::Yes, TQMessageBox::No);

    if (response == TQMessageBox::Yes) {
        char errmsg[256] = {0};
        int ret = TaskmgrPrivilegedOps::deleteCronEntry(this, &m_entries[row], errmsg, sizeof(errmsg));
        if (ret == 0) {
            refresh();
            m_selectedRow = -1;
        } else {
            TQMessageBox::critical(this, "Error",
                TQString("Failed to delete scheduled task:\n%1\n\n%2")
                .arg(name)
                .arg(errmsg[0] ? errmsg : "Permission denied or operation failed."));
        }
    }
}

void ScheduledTab::onContextRefresh()
{
    refresh();
}

#include "scheduled_tab.moc"
