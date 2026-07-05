/*
 * service_details_dialog.h — Service Details Dialog for taskmgr TQt3 port.
 *
 * Implements the collapsible/expandable split details panel matching GTK3 original.
 */

#ifndef SERVICE_DETAILS_DIALOG_H
#define SERVICE_DETAILS_DIALOG_H

#include <ntqdialog.h>
#include <ntqlabel.h>
#include <ntqpushbutton.h>
#include <ntqtextedit.h>
#include <ntqlayout.h>
#include "service_manager.h"

class ServiceDetailsDialog : public TQDialog {
    TQ_OBJECT

public:
    ServiceDetailsDialog(const TQString& serviceName, TQWidget* parent = 0);
    virtual ~ServiceDetailsDialog();

private slots:
    void onMoreLessDetailsClicked();
    void onCopyClicked();
    void onStopClicked();
    void onStartClicked();
    void onOkClicked();

private:
    void loadDetails();
    void updateLayout();

    TQString m_serviceName;
    SystemdServiceDetails m_details;

    TQHBoxLayout* m_mainSplitLayout;
    TQWidget* m_leftPanel;
    TQWidget* m_rightPanel;

    // Left Panel Widgets
    TQTextEdit* m_leftTextEdit;
    
    // Right Panel Widgets
    TQTextEdit* m_rightTextEdit;

    // Bottom Buttons
    TQPushButton* m_btnMoreLess;
    TQPushButton* m_btnCopy;
    TQPushButton* m_btnStop;
    TQPushButton* m_btnStart;
    TQPushButton* m_btnOk;

    bool m_expanded;
};

#endif // SERVICE_DETAILS_DIALOG_H
