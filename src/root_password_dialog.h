/*
 * root_password_dialog.h — Password prompt for root mode activation.
 */

#ifndef ROOT_PASSWORD_DIALOG_H
#define ROOT_PASSWORD_DIALOG_H

#include <ntqdialog.h>
#include <ntqlineedit.h>
#include <ntqlabel.h>
#include <ntqpushbutton.h>

enum RootPasswordDialogMode {
    RootPasswordFullMode,
    RootPasswordEphemeralMode
};

class RootPasswordDialog : public TQDialog {
    TQ_OBJECT

public:
    RootPasswordDialog(TQWidget* parent = 0,
                       RootPasswordDialogMode mode = RootPasswordFullMode,
                       const TQString& prompt = TQString());

    TQString password() const;

private slots:
    void onOkClicked();
    void onCancelClicked();

private:
    void showError(const TQString& message);
    void clearError();

    RootPasswordDialogMode m_mode;
    TQLineEdit* m_passwordEdit;
    TQLabel* m_errorLabel;
    TQPushButton* m_btnOk;
    TQPushButton* m_btnCancel;
};

#endif /* ROOT_PASSWORD_DIALOG_H */
