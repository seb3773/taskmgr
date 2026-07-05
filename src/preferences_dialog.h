#ifndef PREFERENCES_DIALOG_H
#define PREFERENCES_DIALOG_H

#include <ntqdialog.h>
#include <ntqcheckbox.h>
#include <ntqgroupbox.h>
#include <ntqpushbutton.h>
#include <ntqcombobox.h>
#include <ntqcolor.h>

class PreferencesDialog : public TQDialog
{
    TQ_OBJECT

public:
    PreferencesDialog(TQWidget* parent = 0);
    ~PreferencesDialog();

private slots:
    void onOkClicked();
    void onCancelClicked();
    void onFgModeChanged(int index);
    void onBgModeChanged(int index);
    void onFgColorClicked();
    void onBgColorClicked();
    void onSystrayCpuModeChanged(int index);
    void onSystrayCpuColorClicked();
    void onSystrayBgTintModeChanged(int index);
    void onSystrayBgTintColorClicked();

private:
    void updateColorButton(TQPushButton* btn, const TQColor& color);

    TQCheckBox* m_chkMinimizeToTray;
    TQCheckBox* m_chkGauges;
    TQCheckBox* m_chkPss;
    TQCheckBox* m_chkSmooth;
    TQCheckBox* m_chkAntiAlias;
    TQComboBox* m_cmbGpuMode;
    
    TQComboBox* m_cmbFg;
    TQPushButton* m_btnFgColor;
    TQColor m_fgColor;

    TQComboBox* m_cmbBg;
    TQPushButton* m_btnBgColor;
    TQColor m_bgColor;

    TQComboBox* m_cmbSystrayCpu;
    TQPushButton* m_btnSystrayCpuColor;
    TQColor m_systrayCpuColor;

    TQComboBox* m_cmbSystrayBgTint;
    TQPushButton* m_btnSystrayBgTintColor;
    TQColor m_systrayBgTintColor;

    TQComboBox* m_cmbEditor;
    TQComboBox* m_cmbBrowser;

    TQPushButton* m_btnOk;
    TQPushButton* m_btnCancel;
};

void applyAppPalette();

#endif // PREFERENCES_DIALOG_H
