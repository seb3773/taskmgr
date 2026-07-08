/*
 * process_details_dialog.h — Process Details Dialog for taskmgr TQt3 port.
 *
 * Same layout/style as ServiceDetailsDialog; data from process_details.c.
 */

#ifndef PROCESS_DETAILS_DIALOG_H
#define PROCESS_DETAILS_DIALOG_H

#include <ntqdialog.h>
#include <ntqpushbutton.h>
#include <ntqtextedit.h>
#include <ntqlayout.h>
#include <sys/types.h>
#include "process_details.h"

class ProcessDetailsDialog : public TQDialog {
    TQ_OBJECT

public:
    ProcessDetailsDialog(pid_t pid, TQWidget* parent = 0);
    virtual ~ProcessDetailsDialog();

private slots:
    void onMoreLessDetailsClicked();
    void onCopyClicked();
    void onRefreshClicked();
    void onKillClicked();
    void onOkClicked();

private:
    void loadDetails();
    void loadMoreDetails();
    void updateLayout();
    void formatPanels();

    pid_t m_pid;
    ProcessDetailsBasic m_basic;
    ProcessDetailsMore m_more;
    bool m_expanded;
    bool m_moreLoaded;
    bool m_firstLayout;

    TQHBoxLayout* m_mainSplitLayout;
    TQWidget* m_leftPanel;
    TQWidget* m_rightPanel;
    TQTextEdit* m_leftTextEdit;
    TQTextEdit* m_rightTextEdit;

    TQPushButton* m_btnMoreLess;
    TQPushButton* m_btnCopy;
    TQPushButton* m_btnRefresh;
    TQPushButton* m_btnKill;
    TQPushButton* m_btnOk;
};

#endif /* PROCESS_DETAILS_DIALOG_H */
