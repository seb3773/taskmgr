/*
 * startup_tab.cpp — Startup tab for taskmgr TQt3 port.
 *
 * Implements the startup applications list using TQtListStore and TQtMvcTableView.
 */

#include "startup_tab.h"
#include "mvc/tqtliststore.h"
#include "mvc/tqtmvctableview.h"
#include "backend_bridge.h"
#include "autostart_manager.h"
#include "taskmgr_privileged_ops.h"
#include "privileged_policy.h"
#include "process_launcher.h"
#include "app_manager.h"
#include "preferences_dialog.h"
#include "tde_icon_loader.h"

#include <ntqpopupmenu.h>
#include <ntqmessagebox.h>
#include <ntqprocess.h>
#include <ntqapplication.h>
#include <ntqheader.h>

#include <unistd.h>
#include <sys/types.h>

/* ====================================================================
 * StartupTab — Constructor
 * ==================================================================== */

StartupTab::StartupTab(TQWidget* parent, const char* name)
    : TQWidget(parent, name), m_selectedRow(-1)
{
    TQVBoxLayout* layout = new TQVBoxLayout(this, 0, 4);

    /* Model (5 columns: Name, Status, Path, Command, Reason) */
    m_store = new TQtListStore(5, this);
    setupColumns();

    /* View */
    m_tableView = new TQtMvcTableView(this);
    m_tableView->setModel(m_store);
    m_tableView->setSortingEnabled(true);
    m_tableView->setFrameStyle(TQFrame::NoFrame);
    
    /* Set default column widths */
    m_tableView->setColumnWidth(0, 210); // Name
    m_tableView->setColumnWidth(1, 85);  // Status
    m_tableView->setColumnWidth(2, 280); // Path
    m_tableView->setColumnWidth(3, 280); // Command

    layout->addWidget(m_tableView, 1);

    connect(m_tableView, SIGNAL(rowContextMenuRequested(int, int, const TQPoint&)),
            this,        SLOT(onRowContextMenuRequested(int, int, const TQPoint&)));

    connect(m_tableView, SIGNAL(doubleClicked(int, int, int, const TQPoint&)),
            this,        SLOT(onDoubleClicked(int, int, int, const TQPoint&)));
}

StartupTab::~StartupTab()
{
}

void StartupTab::setupColumns()
{
    m_store->setHeader(0, "Name");
    m_store->setHeader(1, "Status");
    m_store->setHeader(2, "Path");
    m_store->setHeader(3, "Command");
    m_store->setHeader(4, "Reason");
}

void StartupTab::refresh()
{
    // Block painting to avoid flicker
    m_tableView->blockPainting();

    m_store->clear();

    size_t count = 0;
    AutostartEntry* entries = get_autostart_entries(&count);

    if (entries && count > 0) {
        m_store->beginBatch();
        for (size_t i = 0; i < count; ++i) {
            TQtRow row(5);
            row[0] = TQVariant(entries[i].name);
            row[1] = TQVariant(entries[i].enabled ? "Enabled" : "Disabled");
            row[2] = TQVariant(entries[i].path);
            row[3] = TQVariant(entries[i].exec ? entries[i].exec : "");
            row[4] = TQVariant(entries[i].reason ? entries[i].reason : "");

            m_store->appendRow(row);

            // Set cell style (icon) for the Name column
            TQtCellStyle style;
            style.setIcon(TdeIconLoader::autostartIcon(
                entries[i].exec ? entries[i].exec : entries[i].name,
                entries[i].path ? entries[i].path : ""));
            m_store->setCellStyle(i, 0, style);

            // Conditional coloring for Status column
            TQtCellStyle statusStyle;
            statusStyle.hasBackground = true;
            if (entries[i].enabled) {
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

void StartupTab::getCounts(int& enabledCount, int& disabledCount) const
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

void StartupTab::onRowContextMenuRequested(int modelRow, int col, const TQPoint& globalPos)
{
    (void)col;
    if (modelRow < 0 || modelRow >= m_store->rowCount()) return;

    m_selectedRow = modelRow;

    TQPopupMenu* menu = new TQPopupMenu(this);

    // Get current status
    TQString status = m_store->data(modelRow, 1).toString();
    if (status == "Enabled") {
        menu->insertItem("Disable", this, SLOT(onContextEnableDisable()));
    } else {
        menu->insertItem("Enable", this, SLOT(onContextEnableDisable()));
    }

    menu->insertItem("Open file location", this, SLOT(onContextOpenFileLocation()));
    menu->insertItem("Edit", this, SLOT(onContextEdit()));
    menu->insertItem("Delete", this, SLOT(onContextDelete()));

    menu->exec(globalPos);
    delete menu;
}

void StartupTab::onDoubleClicked(int row, int col, int button, const TQPoint& mousePos)
{
    (void)row; (void)col; (void)button; (void)mousePos;
    int mRow = m_tableView->selectedModelRow();
    if (mRow >= 0 && mRow < m_store->rowCount()) {
        m_selectedRow = mRow;
        onContextEnableDisable();
    }
}

void StartupTab::onContextEnableDisable()
{
    if (m_selectedRow < 0 || m_selectedRow >= m_store->rowCount()) return;

    TQString path = m_store->data(m_selectedRow, 2).toString();
    TQString status = m_store->data(m_selectedRow, 1).toString();

    if (!path.isEmpty()) {
        char message[512];
        bool currently_enabled = (status == "Enabled");
        int enable = !currently_enabled;

        ToggleResult result = TaskmgrPrivilegedOps::toggleAutostart(this, path.latin1(), enable, message);

        if (result == TOGGLE_SUCCESS) {
            // Update UI
            m_store->setData(m_selectedRow, 1, TQVariant(enable ? "Enabled" : "Disabled"));
            
            TQtCellStyle statusStyle;
            statusStyle.hasBackground = true;
            if (enable) {
                statusStyle.background = TQColor(225, 245, 225); // Soft green
            } else {
                statusStyle.background = TQColor(255, 230, 230); // Soft red
            }
            m_store->setCellStyle(m_selectedRow, 1, statusStyle);
        } else {
            TQMessageBox::critical(this, "Error",
                TQString("Error toggling startup entry:\n%1\n\nReason: %2")
                .arg(message)
                .arg(toggle_result_to_string(result)));
        }
    }
}

void StartupTab::onContextOpenFileLocation()
{
    if (m_selectedRow < 0 || m_selectedRow >= m_store->rowCount()) return;

    TQString path = m_store->data(m_selectedRow, 2).toString();
    if (!path.isEmpty()) {
        int lastSlash = path.findRev('/');
        if (lastSlash >= 0) {
            TQString dirPath = path.left(lastSlash);
            taskmgr_launch_open_directory(dirPath.latin1());
        }
    }
}

void StartupTab::onContextEdit()
{
    if (m_selectedRow < 0 || m_selectedRow >= m_store->rowCount()) return;

    TQString path = m_store->data(m_selectedRow, 2).toString();
    if (!path.isEmpty()) {
        if (!TaskmgrPrivilegedOps::editAutostart(this, path.latin1())) {
            TQMessageBox::critical(this, "Error", "Failed to launch the text editor.");
        }
    }
}

void StartupTab::onContextDelete()
{
    if (m_selectedRow < 0 || m_selectedRow >= m_store->rowCount()) return;

    TQString name = m_store->data(m_selectedRow, 0).toString();
    TQString path = m_store->data(m_selectedRow, 2).toString();

    int response = TQMessageBox::question(this, "Confirm Delete",
        TQString("Are you sure you want to delete the startup entry '%1'?").arg(name),
        TQMessageBox::Yes, TQMessageBox::No);

    if (response == TQMessageBox::Yes) {
        if (!path.isEmpty()) {
            if (unlink(path.latin1()) == 0) {
                m_store->removeRow(m_selectedRow);
                m_selectedRow = -1;
            } else {
                TQMessageBox::critical(this, "Error",
                    TQString("Failed to delete file:\n%1\n\nReason: %2")
                    .arg(path)
                    .arg(strerror(errno)));
            }
        }
    }
}

#include "startup_tab.moc"
