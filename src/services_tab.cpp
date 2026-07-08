/*
 * services_tab.cpp — Services tab for taskmgr TQt3 port.
 *
 * Implements the systemd services list using TQtListStore and TQtMvcTableView.
 */

#include "services_tab.h"
#include "mvc/tqtliststore.h"
#include "mvc/tqtmvctableview.h"
#include "backend_bridge.h"
#include "service_manager.h"
#include "service_details_dialog.h"
#include "taskmgr_privileged_ops.h"
#include "process_launcher.h"
#include "tde_icon_loader.h"

#include <ntqpopupmenu.h>
#include <ntqmessagebox.h>
#include <ntqprocess.h>
#include <ntqapplication.h>
#include <ntqheader.h>

#include <unistd.h>
#include <sys/types.h>
#include <string.h>

static TQPixmap getServiceIcon(const TQString& status)
{
    if (status == "running")
        return TdeIconLoader::namedIcon("quick_restart", "services");
    if (status == "stopped")
        return TdeIconLoader::namedIcon("cancel", "services");
    return TdeIconLoader::namedIcon("services");
}

/* ====================================================================
 * ServicesTab — Constructor
 * ==================================================================== */

ServicesTab::ServicesTab(TQWidget* parent, const char* name)
    : TQWidget(parent, name), m_selectedRow(-1)
{
    TQVBoxLayout* layout = new TQVBoxLayout(this, 0, 4);

    /* Model (6 columns: Name, PID, Status, RAM, Description, Group) */
    m_store = new TQtListStore(6, this);
    setupColumns();

    /* View */
    m_tableView = new TQtMvcTableView(this);
    m_tableView->setModel(m_store);
    m_tableView->setSortingEnabled(true);
    m_tableView->setFrameStyle(TQFrame::NoFrame);
    
    /* Set default column widths */
    m_tableView->setColumnWidth(0, 210); // Name
    m_tableView->setColumnWidth(1, 65);  // PID
    m_tableView->setColumnWidth(2, 85);  // Status
    m_tableView->setColumnWidth(3, 85);  // RAM
    m_tableView->setColumnWidth(4, 280); // Description
    m_tableView->setColumnWidth(5, 85);  // Group

    layout->addWidget(m_tableView, 1);

    connect(m_tableView, SIGNAL(rowContextMenuRequested(int, int, const TQPoint&)),
            this,        SLOT(onRowContextMenuRequested(int, int, const TQPoint&)));

    connect(m_tableView, SIGNAL(doubleClicked(int, int, int, const TQPoint&)),
            this,        SLOT(onDoubleClicked(int, int, int, const TQPoint&)));
}

ServicesTab::~ServicesTab()
{
}

void ServicesTab::setupColumns()
{
    m_store->setHeader(0, "Name");
    m_store->setHeader(1, "PID");
    m_store->setHeader(2, "Status");
    m_store->setHeader(3, "RAM");
    m_store->setHeader(4, "Description");
    m_store->setHeader(5, "Group");
}

void ServicesTab::refresh()
{
    // Block painting to avoid flicker
    m_tableView->blockPainting();

    m_store->clear();

    SystemdServiceList* list = get_systemd_services();

    if (list && list->count > 0) {
        m_store->beginBatch();
        for (int i = 0; i < list->count; ++i) {
            TQtRow row(6);
            row[0] = TQVariant(list->services[i].name);
            row[1] = TQVariant(list->services[i].pid);
            row[2] = TQVariant(list->services[i].status);
            row[3] = TQVariant(list->services[i].memory_usage);
            row[4] = TQVariant(list->services[i].description);
            row[5] = TQVariant(list->services[i].slice);

            m_store->appendRow(row);

            // Set cell style (icon) for the Name column
            TQtCellStyle style;
            style.setIcon(getServiceIcon(list->services[i].status));
            m_store->setCellStyle(i, 0, style);

            // Conditional coloring for Status column
            TQtCellStyle statusStyle;
            statusStyle.hasBackground = true;
            if (strcasecmp(list->services[i].status, "running") == 0) {
                statusStyle.background = TQColor(225, 245, 225); // Soft green
            } else if (strcasecmp(list->services[i].status, "failed") == 0) {
                statusStyle.background = TQColor(255, 230, 230); // Soft red
            } else {
                statusStyle.background = TQColor(240, 240, 240); // Soft grey
            }
            m_store->setCellStyle(i, 2, statusStyle);
        }
        m_store->endBatch();

        systemd_service_list_free(list);
    }

    if (m_tableView->sortColumn() >= 0)
        m_tableView->sortByColumn(m_tableView->sortColumn(), m_tableView->sortAscending());

    m_tableView->unblockPainting();
}

void ServicesTab::onRowContextMenuRequested(int modelRow, int col, const TQPoint& globalPos)
{
    (void)col;
    if (modelRow < 0 || modelRow >= m_store->rowCount()) return;

    m_selectedRow = modelRow;

    TQPopupMenu* menu = new TQPopupMenu(this);

    // Get current status
    TQString status = m_store->data(modelRow, 2).toString();
    if (status == "running") {
        menu->insertItem("Stop", this, SLOT(onContextStop()));
        menu->insertItem("Restart", this, SLOT(onContextRestart()));
    } else {
        menu->insertItem("Start", this, SLOT(onContextStart()));
    }

    menu->insertItem("Search online", this, SLOT(onContextSearchOnline()));

    TQString name = m_store->data(modelRow, 0).toString();
    SystemdServiceDetails details;
    memset(&details, 0, sizeof(details));
    if (get_systemd_service_details(name.latin1(), &details) == 0) {
        TQString unitState = details.unit_file_state;
        if (unitState == "enabled" || unitState == "enabled-runtime")
            menu->insertItem("Disable", this, SLOT(onContextDisable()));
        else if (unitState == "disabled")
            menu->insertItem("Enable", this, SLOT(onContextEnable()));
    }

    menu->insertSeparator();
    menu->insertItem("Details", this, SLOT(onContextDetails()));

    menu->exec(globalPos);
    delete menu;
}

void ServicesTab::onDoubleClicked(int row, int col, int button, const TQPoint& mousePos)
{
    (void)row; (void)col; (void)button; (void)mousePos;
    int mRow = m_tableView->selectedModelRow();
    if (mRow >= 0 && mRow < m_store->rowCount()) {
        m_selectedRow = mRow;
        onContextDetails();
    }
}

void ServicesTab::onContextStart()
{
    if (m_selectedRow < 0 || m_selectedRow >= m_store->rowCount()) return;

    TQString name = m_store->data(m_selectedRow, 0).toString();
    if (TaskmgrPrivilegedOps::serviceControl(this, name.latin1(), SERVICE_ACTION_START) == 0) {
        refresh();
    } else {
        TQMessageBox::critical(this, "Error", "Failed to start service.\nYou may need administrator privileges.");
    }
}

void ServicesTab::onContextStop()
{
    if (m_selectedRow < 0 || m_selectedRow >= m_store->rowCount()) return;

    TQString name = m_store->data(m_selectedRow, 0).toString();
    if (TaskmgrPrivilegedOps::serviceControl(this, name.latin1(), SERVICE_ACTION_STOP) == 0) {
        refresh();
    } else {
        TQMessageBox::critical(this, "Error", "Failed to stop service.\nYou may need administrator privileges.");
    }
}

void ServicesTab::onContextRestart()
{
    if (m_selectedRow < 0 || m_selectedRow >= m_store->rowCount()) return;

    TQString name = m_store->data(m_selectedRow, 0).toString();
    if (TaskmgrPrivilegedOps::serviceControl(this, name.latin1(), SERVICE_ACTION_RESTART) == 0) {
        refresh();
    } else {
        TQMessageBox::critical(this, "Error", "Failed to restart service.\nYou may need administrator privileges.");
    }
}

void ServicesTab::onContextEnable()
{
    if (m_selectedRow < 0 || m_selectedRow >= m_store->rowCount()) return;

    TQString name = m_store->data(m_selectedRow, 0).toString();
    if (TaskmgrPrivilegedOps::serviceEnableDisable(this, name.latin1(), 1) == 0) {
        refresh();
    } else {
        TQMessageBox::critical(this, "Error", "Failed to enable service.\nYou may need administrator privileges.");
    }
}

void ServicesTab::onContextDisable()
{
    if (m_selectedRow < 0 || m_selectedRow >= m_store->rowCount()) return;

    TQString name = m_store->data(m_selectedRow, 0).toString();
    if (TaskmgrPrivilegedOps::serviceEnableDisable(this, name.latin1(), 0) == 0) {
        refresh();
    } else {
        TQMessageBox::critical(this, "Error", "Failed to disable service.\nYou may need administrator privileges.");
    }
}

void ServicesTab::onContextSearchOnline()
{
    if (m_selectedRow < 0 || m_selectedRow >= m_store->rowCount()) return;

    TQString name = m_store->data(m_selectedRow, 0).toString();
    TQString query = TQString("systemd service %1").arg(name);
    TQString url = TQString("https://www.google.com/search?q=%1").arg(query);
    if (!taskmgr_launch_open_url(url.latin1())) {
        TQMessageBox::critical(this, "Error", "Failed to open the web browser.");
    }
}

void ServicesTab::onContextDetails()
{
    if (m_selectedRow < 0 || m_selectedRow >= m_store->rowCount()) return;

    TQString name = m_store->data(m_selectedRow, 0).toString();
    ServiceDetailsDialog dlg(name, this);
    if (dlg.exec() == TQDialog::Accepted) {
        refresh();
    }
}

#include "services_tab.moc"
