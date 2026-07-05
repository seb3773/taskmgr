/*
 * root_password_dialog.cpp — Password prompt for root mode activation.
 */

#include "root_password_dialog.h"
#include "root_credential_vault.h"

#include <ntqlayout.h>
#include <ntqlabel.h>

RootPasswordDialog::RootPasswordDialog(TQWidget* parent,
                                       RootPasswordDialogMode mode,
                                       const TQString& prompt)
    : TQDialog(parent, "root_password_dialog", true)
    , m_mode(mode)
{
    setCaption(mode == RootPasswordEphemeralMode ? "Administrator password" : "Root mode");
    setBackgroundColor(TQt::white);
    resize(360, 140);

    TQVBoxLayout* layout = new TQVBoxLayout(this, 12, 8);

    TQString labelText = prompt.isEmpty()
        ? TQString("Enter the root password:")
        : prompt;
    layout->addWidget(new TQLabel(labelText, this));

    m_passwordEdit = new TQLineEdit(this);
    m_passwordEdit->setEchoMode(TQLineEdit::Password);
    layout->addWidget(m_passwordEdit);

    m_errorLabel = new TQLabel(this);
    m_errorLabel->setPaletteForegroundColor(TQColor(180, 0, 0));
    m_errorLabel->hide();
    layout->addWidget(m_errorLabel);

    TQHBoxLayout* btnRow = new TQHBoxLayout(0, 0, 8);
    btnRow->addStretch(1);
    m_btnOk = new TQPushButton("OK", this);
    m_btnCancel = new TQPushButton("Cancel", this);
    btnRow->addWidget(m_btnOk);
    btnRow->addWidget(m_btnCancel);
    layout->addLayout(btnRow);

    connect(m_btnOk, SIGNAL(clicked()), this, SLOT(onOkClicked()));
    connect(m_btnCancel, SIGNAL(clicked()), this, SLOT(onCancelClicked()));
    connect(m_passwordEdit, SIGNAL(returnPressed()), this, SLOT(onOkClicked()));

    m_passwordEdit->setFocus();
}

TQString RootPasswordDialog::password() const
{
    return m_passwordEdit->text();
}

void RootPasswordDialog::showError(const TQString& message)
{
    m_errorLabel->setText(message);
    m_errorLabel->show();
}

void RootPasswordDialog::clearError()
{
    m_errorLabel->hide();
}

void RootPasswordDialog::onOkClicked()
{
    clearError();

    TQString pass = m_passwordEdit->text();
    char errmsg[128];
    errmsg[0] = '\0';

    if (m_mode == RootPasswordEphemeralMode) {
        if (root_validate_password(pass.latin1(), errmsg, sizeof(errmsg))) {
            accept();
            return;
        }
    } else if (root_mode_activate_with_password(pass.latin1(), errmsg, sizeof(errmsg))) {
        accept();
        return;
    }

    showError(TQString::fromLocal8Bit(errmsg));
    m_passwordEdit->selectAll();
    m_passwordEdit->setFocus();
}

void RootPasswordDialog::onCancelClicked()
{
    reject();
}

#include "root_password_dialog.moc"
