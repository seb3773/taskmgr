/*
 * run_new_task_dialog.cpp — "Create new task" dialog (GTK3 parity).
 */

#include "run_new_task_dialog.h"
#include "app_icons.h"
#include "process_launcher.h"

#include <ntqlayout.h>
#include <ntqlabel.h>
#include <ntqfiledialog.h>

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

    TQHBoxLayout* btnRow = new TQHBoxLayout(0, 0, 8);
    btnRow->addStretch(1);
    m_btnCancel = new TQPushButton("Cancel", this);
    m_btnRun = new TQPushButton("Run", this);
    m_btnRun->setDefault(true);
    btnRow->addWidget(m_btnCancel);
    btnRow->addWidget(m_btnRun);
    layout->addLayout(btnRow);

    connect(browseBtn, SIGNAL(clicked()), this, SLOT(onBrowseClicked()));
    connect(m_btnRun, SIGNAL(clicked()), this, SLOT(onRunClicked()));
    connect(m_btnCancel, SIGNAL(clicked()), this, SLOT(onCancelClicked()));
    connect(m_programEdit, SIGNAL(returnPressed()), this, SLOT(onRunClicked()));

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
    TQCString program = m_programEdit->text().stripWhiteSpace().local8Bit();
    if (program.isEmpty())
        return;

    if (!taskmgr_execute_program(program.data()))
        return;

    accept();
}

void RunNewTaskDialog::onCancelClicked()
{
    reject();
}

#include "run_new_task_dialog.moc"
