/*
 * run_new_task_dialog.h — "Create new task" dialog (GTK3 parity).
 */

#ifndef RUN_NEW_TASK_DIALOG_H
#define RUN_NEW_TASK_DIALOG_H

#include <ntqdialog.h>
#include <ntqlineedit.h>
#include <ntqpushbutton.h>
#include <ntqcheckbox.h>
#include <ntqlabel.h>

class RunNewTaskDialog : public TQDialog {
    TQ_OBJECT

public:
    explicit RunNewTaskDialog(TQWidget* parent = 0);

private slots:
    void onBrowseClicked();
    void onRunClicked();
    void onCancelClicked();
    void onAdvancedToggled(bool checked);

private:
    TQLineEdit* m_programEdit;
    TQPushButton* m_btnRun;
    TQPushButton* m_btnCancel;
    TQPushButton* m_btnAdvanced;
    TQWidget* m_advancedWidget;
    TQCheckBox* m_chkTerminal;
    TQCheckBox* m_chkUser;
    TQLineEdit* m_userEdit;
    TQLineEdit* m_passEdit;
    TQLabel* m_lblUser;
    TQLabel* m_lblPass;
};

#endif /* RUN_NEW_TASK_DIALOG_H */
