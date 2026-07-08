/*
 * service_details_dialog.cpp — Service Details Dialog for taskmgr TQt3 port.
 *
 * Implements the collapsible/expandable split details panel matching GTK3 original.
 */

#include "service_details_dialog.h"
#include "service_manager.h"
#include "taskmgr_privileged_ops.h"
#include <ntqlayout.h>
#include <ntqapplication.h>
#include <ntqclipboard.h>
#include <ntqmessagebox.h>
#include <ntqregexp.h>

/* ====================================================================
 * ServiceDetailsDialog — Implementation
 * ==================================================================== */

ServiceDetailsDialog::ServiceDetailsDialog(const TQString& serviceName, TQWidget* parent)
    : TQDialog(parent, "service_details_dialog", true),
      m_serviceName(serviceName),
      m_expanded(false),
      m_firstLayout(true)
{
    setCaption("Service Details");
    setBackgroundColor(TQt::white);

    // Main Vertical Layout
    TQVBoxLayout* dialogLayout = new TQVBoxLayout(this, 10, 8);

    // Horizontal Split Layout for Panels
    m_mainSplitLayout = new TQHBoxLayout(dialogLayout);

    // 1. Left Panel (Always Visible)
    m_leftPanel = new TQWidget(this);
    m_leftPanel->setBackgroundColor(TQt::white);
    TQVBoxLayout* leftLayout = new TQVBoxLayout(m_leftPanel, 0, 0);

    m_leftTextEdit = new TQTextEdit(m_leftPanel);
    m_leftTextEdit->setReadOnly(true);
    m_leftTextEdit->setFrameStyle(TQFrame::NoFrame);
    m_leftTextEdit->setBackgroundColor(TQt::white);
    leftLayout->addWidget(m_leftTextEdit);

    m_mainSplitLayout->addWidget(m_leftPanel, 1);

    // 2. Right Panel (Collapsible)
    m_rightPanel = new TQWidget(this);
    m_rightPanel->setBackgroundColor(TQt::white);
    TQVBoxLayout* rightLayout = new TQVBoxLayout(m_rightPanel, 0, 0);

    m_rightTextEdit = new TQTextEdit(m_rightPanel);
    m_rightTextEdit->setReadOnly(true);
    m_rightTextEdit->setFrameStyle(TQFrame::NoFrame);
    m_rightTextEdit->setBackgroundColor(TQt::white);
    rightLayout->addWidget(m_rightTextEdit);

    m_mainSplitLayout->addWidget(m_rightPanel, 1);

    // Hide right panel initially
    m_rightPanel->hide();

    // 3. Bottom Button Bar
    TQHBoxLayout* btnBar = new TQHBoxLayout(dialogLayout, 6);
    
    m_btnMoreLess = new TQPushButton("more details", this);
    m_btnCopy = new TQPushButton("Copy", this);
    m_btnStop = new TQPushButton("Stop", this);
    m_btnStart = new TQPushButton("Start", this);
    m_btnOk = new TQPushButton("OK", this);

    // Set buttons to flat style
    m_btnMoreLess->setFlat(true);
    m_btnCopy->setFlat(true);
    m_btnStop->setFlat(true);
    m_btnStart->setFlat(true);
    m_btnOk->setFlat(true);

    btnBar->addWidget(m_btnMoreLess);
    btnBar->addWidget(m_btnCopy);
    btnBar->addStretch(1);
    btnBar->addWidget(m_btnStop);
    btnBar->addWidget(m_btnStart);
    btnBar->addWidget(m_btnOk);

    // Connections
    connect(m_btnMoreLess, SIGNAL(clicked()), this, SLOT(onMoreLessDetailsClicked()));
    connect(m_btnCopy, SIGNAL(clicked()), this, SLOT(onCopyClicked()));
    connect(m_btnStop, SIGNAL(clicked()), this, SLOT(onStopClicked()));
    connect(m_btnStart, SIGNAL(clicked()), this, SLOT(onStartClicked()));
    connect(m_btnOk, SIGNAL(clicked()), this, SLOT(onOkClicked()));

    loadDetails();
    updateLayout();
}

ServiceDetailsDialog::~ServiceDetailsDialog()
{
}

void ServiceDetailsDialog::loadDetails()
{
    if (get_systemd_service_details(m_serviceName.latin1(), &m_details) != 0) {
        TQMessageBox::critical(this, "Error", "Failed to retrieve service details.");
        reject();
        return;
    }

    // Update Start/Stop button states based on active state
    TQString activeState = m_details.active_state;
    if (activeState == "active") {
        m_btnStart->setEnabled(false);
        m_btnStop->setEnabled(true);
    } else {
        m_btnStart->setEnabled(true);
        m_btnStop->setEnabled(false);
    }

    // Format Left Panel Text (Basic Information)
    TQString leftText;
    leftText += "<b>- Service Information:</b><br><br>";
    leftText += TQString("Service: %1<br>").arg(m_details.name);
    leftText += TQString("Description: %1<br><br>").arg(m_details.description);

    leftText += "<b>- Status:</b><br><br>";
    leftText += TQString("Active State: %1<br>").arg(m_details.active_state);
    leftText += TQString("Sub State: %1<br>").arg(m_details.sub_state);
    leftText += TQString("Load State: %1<br>").arg(m_details.load_state);
    leftText += TQString("Unit File State: %1<br>").arg(m_details.unit_file_state);
    leftText += TQString("Main PID: %1<br>").arg(m_details.main_pid);
    leftText += TQString("Control PID: %1<br>").arg(m_details.control_pid);
    leftText += TQString("Memory Usage: %1<br><br>").arg(m_details.memory_usage);

    leftText += "<b>- Configuration:</b><br><br>";
    leftText += TQString("Service Type: %1<br>").arg(m_details.service_type);
    leftText += TQString("Restart: %1<br>").arg(m_details.restart);
    leftText += TQString("Command: %1<br>").arg(m_details.exec_start);

    m_leftTextEdit->setText(leftText);

    // Format Right Panel Text (Advanced/More Details)
    TQString rightText;
    rightText += "<b>- Processus:</b><br><br>";
    rightText += TQString("Start Time: %1<br>").arg(m_details.exec_main_start_timestamp);
    rightText += TQString("CPU Usage: %1 ns<br>").arg(m_details.cpu_usage_nsec);
    rightText += TQString("Tasks: %1<br>").arg(m_details.tasks_current);
    rightText += TQString("User: %1<br>").arg(m_details.user);
    rightText += TQString("Group: %1<br><br>").arg(m_details.group);

    rightText += "<b>- Configuration:</b><br><br>";
    rightText += TQString("Working Directory: %1<br>").arg(m_details.working_directory);
    rightText += TQString("Environment: %1<br><br>").arg(m_details.environment);

    rightText += "<b>- Ressources:</b><br><br>";
    rightText += TQString("Limit NPROC: %1<br>").arg(m_details.limit_nproc);
    rightText += TQString("Limit NOFILE: %1<br>").arg(m_details.limit_nofile);
    rightText += TQString("Memory Current: %1<br>").arg(m_details.memory_current);
    rightText += TQString("CGroup: %1<br><br>").arg(m_details.cgroup_path);

    rightText += "<b>- Dependencies:</b><br><br>";
    rightText += TQString("Requires: %1<br>").arg(m_details.requires);
    rightText += TQString("Wants: %1<br>").arg(m_details.wants);
    rightText += TQString("After: %1<br>").arg(m_details.after);
    rightText += TQString("PartOf: %1<br><br>").arg(m_details.part_of);

    rightText += "<b>- Security:</b><br><br>";
    rightText += TQString("Protect System: %1<br>").arg(m_details.protect_system);
    rightText += TQString("No New Privileges: %1<br>").arg(m_details.no_new_privileges);
    rightText += TQString("Capability Bounding Set: %1<br>").arg(m_details.capability_bounding_set);

    m_rightTextEdit->setText(rightText);
}

void ServiceDetailsDialog::updateLayout()
{
    int currentHeight = m_firstLayout ? 570 : height();
    m_firstLayout = false;
    if (m_expanded) {
        m_rightPanel->show();
        m_btnMoreLess->setText("less details");
        resize(960, currentHeight);
    } else {
        m_rightPanel->hide();
        m_btnMoreLess->setText("more details");
        resize(540, currentHeight);
    }
}

void ServiceDetailsDialog::onMoreLessDetailsClicked()
{
    m_expanded = !m_expanded;
    updateLayout();
}

void ServiceDetailsDialog::onCopyClicked()
{
    TQString copyText;
    if (m_expanded) {
        // Copy both left and right text in plain text
        copyText = m_leftTextEdit->text() + "\n\n" + m_rightTextEdit->text();
    } else {
        copyText = m_leftTextEdit->text();
    }

    // Strip HTML tags for clipboard
    copyText.replace(TQRegExp("<[^>]*>"), "");
    copyText.replace("&nbsp;", " ");
    copyText.replace("<br>", "\n");

    TQApplication::clipboard()->setText(copyText);
}

void ServiceDetailsDialog::onStopClicked()
{
    if (TaskmgrPrivilegedOps::serviceControl(this, m_details.name, SERVICE_ACTION_STOP) == 0) {
        loadDetails();
    } else {
        TQMessageBox::critical(this, "Error", "Failed to stop service.\nYou may need administrator privileges.");
    }
}

void ServiceDetailsDialog::onStartClicked()
{
    if (TaskmgrPrivilegedOps::serviceControl(this, m_details.name, SERVICE_ACTION_START) == 0) {
        loadDetails();
    } else {
        TQMessageBox::critical(this, "Error", "Failed to start service.\nYou may need administrator privileges.");
    }
}

void ServiceDetailsDialog::onOkClicked()
{
    accept();
}

#include "service_details_dialog.moc"
