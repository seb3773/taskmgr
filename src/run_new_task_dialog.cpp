/*
 * run_new_task_dialog.cpp — "Create new task" dialog (GTK3 parity).
 */

#include "run_new_task_dialog.h"
#include "app_icons.h"
#include "process_launcher.h"

#include <ntqlayout.h>
#include <ntqlabel.h>
#include <ntqfiledialog.h>
#include <ntqmessagebox.h>
#include <ntqcheckbox.h>

RunNewTaskDialog::RunNewTaskDialog(TQWidget* parent)
    : TQDialog(parent, "run_new_task_dialog", true)
{
    setCaption("Create new task");
    setBackgroundColor(TQt::white);
    setFixedSize(450, 150);

    TQPixmap icon = embeddedRunIcon();
    if (!icon.isNull())
        setIcon(icon);

    TQVBoxLayout* layout = new TQVBoxLayout(this, 15, 10);

    TQLabel* instruction = new TQLabel(
        "Type the name of a program to execute:", this);
    instruction->setAlignment(TQt::AlignLeft | TQt::AlignVCenter);
    layout->addWidget(instruction);

    TQHBoxLayout* row = new TQHBoxLayout(0, 0, 8);
    row->addWidget(new TQLabel("Open:", this));
    m_programEdit = new TQLineEdit(this);
    row->addWidget(m_programEdit, 1);
    TQPushButton* browseBtn = new TQPushButton("Browse...", this);
    row->addWidget(browseBtn);
    layout->addLayout(row);

    // Advanced widget (initially hidden)
    m_advancedWidget = new TQWidget(this);
    m_advancedWidget->setBackgroundColor(TQt::white);
    TQVBoxLayout* advLayout = new TQVBoxLayout(m_advancedWidget, 0, 8);
    advLayout->setMargin(0);

    m_chkTerminal = new TQCheckBox("Run in terminal", m_advancedWidget);
    m_chkTerminal->setBackgroundColor(TQt::white);
    advLayout->addWidget(m_chkTerminal);

    m_chkUser = new TQCheckBox("Run as another user", m_advancedWidget);
    m_chkUser->setBackgroundColor(TQt::white);
    advLayout->addWidget(m_chkUser);

    TQHBoxLayout* userRow = new TQHBoxLayout(advLayout, 6);
    m_lblUser = new TQLabel("User name:", m_advancedWidget);
    m_lblUser->setBackgroundColor(TQt::white);
    userRow->addWidget(m_lblUser);
    m_userEdit = new TQLineEdit(m_advancedWidget);
    userRow->addWidget(m_userEdit, 1);

    m_lblPass = new TQLabel("Password:", m_advancedWidget);
    m_lblPass->setBackgroundColor(TQt::white);
    userRow->addWidget(m_lblPass);
    m_passEdit = new TQLineEdit(m_advancedWidget);
    m_passEdit->setEchoMode(TQLineEdit::Password);
    userRow->addWidget(m_passEdit, 1);

    layout->addWidget(m_advancedWidget);
    m_advancedWidget->hide();

    // Disable user/pass components initially
    m_lblUser->setEnabled(false);
    m_userEdit->setEnabled(false);
    m_lblPass->setEnabled(false);
    m_passEdit->setEnabled(false);

    TQHBoxLayout* btnRow = new TQHBoxLayout(0, 0, 8);
    m_btnAdvanced = new TQPushButton("Advanced", this);
    m_btnAdvanced->setToggleButton(true);
    m_btnAdvanced->setFlat(true);
    btnRow->addWidget(m_btnAdvanced);
    btnRow->addStretch(1);
    m_btnCancel = new TQPushButton("Cancel", this);
    m_btnCancel->setFlat(true);
    m_btnRun = new TQPushButton("Run", this);
    m_btnRun->setFlat(true);
    m_btnRun->setDefault(true);
    btnRow->addWidget(m_btnCancel);
    btnRow->addWidget(m_btnRun);
    layout->addLayout(btnRow);

    connect(browseBtn, SIGNAL(clicked()), this, SLOT(onBrowseClicked()));
    connect(m_btnRun, SIGNAL(clicked()), this, SLOT(onRunClicked()));
    connect(m_btnCancel, SIGNAL(clicked()), this, SLOT(onCancelClicked()));
    connect(m_btnAdvanced, SIGNAL(toggled(bool)), this, SLOT(onAdvancedToggled(bool)));
    connect(m_programEdit, SIGNAL(returnPressed()), this, SLOT(onRunClicked()));

    connect(m_chkUser, SIGNAL(toggled(bool)), m_lblUser, SLOT(setEnabled(bool)));
    connect(m_chkUser, SIGNAL(toggled(bool)), m_userEdit, SLOT(setEnabled(bool)));
    connect(m_chkUser, SIGNAL(toggled(bool)), m_lblPass, SLOT(setEnabled(bool)));
    connect(m_chkUser, SIGNAL(toggled(bool)), m_passEdit, SLOT(setEnabled(bool)));

    m_programEdit->setFocus();
}

void RunNewTaskDialog::onBrowseClicked()
{
    TQString file = TQFileDialog::getOpenFileName("/usr/bin", TQString::null, this,
                                                    "run_new_task_browse", "Select");
    if (!file.isEmpty())
        m_programEdit->setText(file);
}

void RunNewTaskDialog::onRunClicked()
{
    TQString program = m_programEdit->text().stripWhiteSpace();
    if (program.isEmpty())
        return;

    bool inTerminal = m_chkTerminal->isChecked();
    bool asUser = m_chkUser->isChecked();
    TQString username = m_userEdit->text().stripWhiteSpace();
    TQString password = m_passEdit->text();

    if (asUser && username.isEmpty()) {
        TQMessageBox::warning(this, "Input Required", "Please enter a user name to run as another user.");
        return;
    }

    gboolean success = taskmgr_execute_program_advanced(
        program.latin1(),
        inTerminal,
        asUser,
        username.latin1(),
        password.latin1()
    );

    if (!success) {
        TQMessageBox::critical(this, "Execution Failed", "Failed to execute program. Please check your command, user name and password.");
        return;
    }

    accept();
}

void RunNewTaskDialog::onCancelClicked()
{
    reject();
}

void RunNewTaskDialog::onAdvancedToggled(bool checked)
{
    if (checked) {
        m_advancedWidget->show();
        setFixedSize(450, 310);
    } else {
        m_advancedWidget->hide();
        setFixedSize(450, 150);
    }
}

#include "run_new_task_dialog.moc"
