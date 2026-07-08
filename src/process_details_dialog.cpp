/*
 * process_details_dialog.cpp — Process Details Dialog for taskmgr TQt3 port.
 */

#include "process_details_dialog.h"
#include "backend_bridge.h"
#include "taskmgr_privileged_ops.h"
#include "taskmgr-linux.h"
#include <ntqlayout.h>
#include <ntqapplication.h>
#include <ntqclipboard.h>
#include <ntqmessagebox.h>
#include <ntqregexp.h>
#include <string.h>

static TQString plainToHtml(const char* text)
{
    if (!text) return TQString();
    TQString s = TQString::fromLocal8Bit(text);
    s.replace("&", "&amp;");
    s.replace("<", "&lt;");
    s.replace(">", "&gt;");
    s.replace("\n", "<br>");
    return s;
}

static TQString sectionHtml(const char* title, const char* body)
{
    return TQString("<b>- %1:</b><br><br>%2").arg(title).arg(plainToHtml(body));
}

ProcessDetailsDialog::ProcessDetailsDialog(pid_t pid, TQWidget* parent)
    : TQDialog(parent, "process_details_dialog", true),
      m_pid(pid),
      m_expanded(false),
      m_moreLoaded(false),
      m_firstLayout(true)
{
    memset(&m_basic, 0, sizeof(m_basic));
    memset(&m_more, 0, sizeof(m_more));

    setCaption("Process Details");
    setBackgroundColor(TQt::white);

    TQVBoxLayout* dialogLayout = new TQVBoxLayout(this, 10, 8);

    m_mainSplitLayout = new TQHBoxLayout(dialogLayout);

    m_leftPanel = new TQWidget(this);
    m_leftPanel->setBackgroundColor(TQt::white);
    TQVBoxLayout* leftLayout = new TQVBoxLayout(m_leftPanel, 0, 0);

    m_leftTextEdit = new TQTextEdit(m_leftPanel);
    m_leftTextEdit->setReadOnly(true);
    m_leftTextEdit->setFrameStyle(TQFrame::NoFrame);
    m_leftTextEdit->setBackgroundColor(TQt::white);
    leftLayout->addWidget(m_leftTextEdit);

    m_mainSplitLayout->addWidget(m_leftPanel, 1);

    m_rightPanel = new TQWidget(this);
    m_rightPanel->setBackgroundColor(TQt::white);
    TQVBoxLayout* rightLayout = new TQVBoxLayout(m_rightPanel, 0, 0);

    m_rightTextEdit = new TQTextEdit(m_rightPanel);
    m_rightTextEdit->setReadOnly(true);
    m_rightTextEdit->setFrameStyle(TQFrame::NoFrame);
    m_rightTextEdit->setBackgroundColor(TQt::white);
    rightLayout->addWidget(m_rightTextEdit);

    m_mainSplitLayout->addWidget(m_rightPanel, 1);
    m_rightPanel->hide();

    TQHBoxLayout* btnBar = new TQHBoxLayout(dialogLayout, 6);

    m_btnMoreLess = new TQPushButton("more details", this);
    m_btnCopy = new TQPushButton("Copy", this);
    m_btnRefresh = new TQPushButton("Refresh", this);
    m_btnKill = new TQPushButton("Kill", this);
    m_btnOk = new TQPushButton("OK", this);

    m_btnMoreLess->setFlat(true);
    m_btnCopy->setFlat(true);
    m_btnRefresh->setFlat(true);
    m_btnKill->setFlat(true);
    m_btnOk->setFlat(true);

    btnBar->addWidget(m_btnMoreLess);
    btnBar->addWidget(m_btnCopy);
    btnBar->addWidget(m_btnRefresh);
    btnBar->addStretch(1);
    btnBar->addWidget(m_btnKill);
    btnBar->addWidget(m_btnOk);

    connect(m_btnMoreLess, SIGNAL(clicked()), this, SLOT(onMoreLessDetailsClicked()));
    connect(m_btnCopy, SIGNAL(clicked()), this, SLOT(onCopyClicked()));
    connect(m_btnRefresh, SIGNAL(clicked()), this, SLOT(onRefreshClicked()));
    connect(m_btnKill, SIGNAL(clicked()), this, SLOT(onKillClicked()));
    connect(m_btnOk, SIGNAL(clicked()), this, SLOT(onOkClicked()));

    if (get_process_details_basic(m_pid, &m_basic) != 0) {
        TQMessageBox::critical(this, "Error", "Failed to retrieve process details.");
        reject();
        return;
    }

    formatPanels();
    updateLayout();
}

ProcessDetailsDialog::~ProcessDetailsDialog()
{
    free_process_details_basic(&m_basic);
    free_process_details_more(&m_more);
}

void ProcessDetailsDialog::formatPanels()
{
    TQString leftText;
    leftText += sectionHtml("Details", m_basic.details);
    leftText += "<br><br>";
    leftText += sectionHtml("Command Line", m_basic.cmdline);
    leftText += "<br><br>";
    leftText += sectionHtml("Extra Infos", m_basic.extra);
    m_leftTextEdit->setText(leftText);

    if (m_moreLoaded) {
        TQString rightText;
        rightText += sectionHtml("Identification and State", m_more.ident);
        rightText += "<br><br>";
        rightText += sectionHtml("Scheduling and Performance", m_more.sched);
        rightText += "<br><br>";
        rightText += sectionHtml("Memory and IO Resources", m_more.memio);
        rightText += "<br><br>";
        rightText += sectionHtml("Files, Network and Security", m_more.files);
        rightText += "<br><br>";
        rightText += sectionHtml("Advanced", m_more.advanced);
        m_rightTextEdit->setText(rightText);
    }
}

void ProcessDetailsDialog::loadDetails()
{
    free_process_details_basic(&m_basic);
    memset(&m_basic, 0, sizeof(m_basic));

    if (get_process_details_basic(m_pid, &m_basic) != 0) {
        TQMessageBox::critical(this, "Error", "Failed to refresh process details.");
        return;
    }

    if (m_moreLoaded) {
        free_process_details_more(&m_more);
        memset(&m_more, 0, sizeof(m_more));
        m_moreLoaded = false;
        if (m_expanded)
            loadMoreDetails();
    }

    formatPanels();
}

void ProcessDetailsDialog::loadMoreDetails()
{
    if (m_moreLoaded) return;

    if (get_process_details_more(m_pid, &m_more) != 0) {
        TQMessageBox::critical(this, "Error", "Failed to retrieve extended process details.");
        return;
    }

    m_moreLoaded = true;
    formatPanels();
}

void ProcessDetailsDialog::updateLayout()
{
    int currentHeight = m_firstLayout ? 680 : height();
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

void ProcessDetailsDialog::onMoreLessDetailsClicked()
{
    m_expanded = !m_expanded;
    if (m_expanded)
        loadMoreDetails();
    updateLayout();
}

void ProcessDetailsDialog::onCopyClicked()
{
    TQString copyText;
    if (m_expanded && m_moreLoaded) {
        copyText = m_leftTextEdit->text() + "<br><br>" + m_rightTextEdit->text();
    } else {
        copyText = m_leftTextEdit->text();
    }

    copyText.replace(TQRegExp("<[^>]*>"), "");
    copyText.replace("&nbsp;", " ");
    copyText.replace("<br>", "\n");
    copyText.replace("&amp;", "&");
    copyText.replace("&lt;", "<");
    copyText.replace("&gt;", ">");

    TQApplication::clipboard()->setText(copyText);
}

void ProcessDetailsDialog::onRefreshClicked()
{
    loadDetails();
}

void ProcessDetailsDialog::onKillClicked()
{
    int response = TQMessageBox::question(this, "Confirm Kill",
        TQString("Really kill the task?"),
        TQMessageBox::Yes, TQMessageBox::No);

    if (response != TQMessageBox::Yes)
        return;

    if (TaskmgrPrivilegedOps::killProcess(this, m_pid, SIGNAL_KILL)) {
        accept();
    } else {
        TQMessageBox::critical(this, "Error",
            TQString("Can't send signal to task ID %1.\nYou may need administrator privileges.").arg(m_pid));
    }
}

void ProcessDetailsDialog::onOkClicked()
{
    accept();
}

#include "process_details_dialog.moc"
