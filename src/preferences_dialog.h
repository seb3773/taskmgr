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

protected:
    bool eventFilter(TQObject* watched, TQEvent* e);

private slots:
    void onOkClicked();
    void onCancelClicked();
    void onColorComboChanged(int index);
    void onColorButtonClicked();

private:
    void updateColorButton(TQPushButton* btn, const TQColor& color);
    TQColor getButtonColor(TQPushButton* btn);

    TQCheckBox* m_chkMinimizeToTray;
    TQCheckBox* m_chkGauges;
    TQCheckBox* m_chkPss;
    TQCheckBox* m_chkSmooth;
    TQCheckBox* m_chkAntiAlias;
    TQCheckBox* m_chkCachedAsFree;
    TQCheckBox* m_chkIndividualFreq;
    TQCheckBox* m_chkUseTdeRunDialog;
    TQComboBox* m_cmbGpuMode;
    
    TQComboBox* m_cmbFg;
    TQPushButton* m_btnFgColor;
    TQColor m_fgColor;

    TQComboBox* m_cmbBg;
    TQPushButton* m_btnBgColor;
    TQColor m_bgColor;

    TQComboBox* m_cmbSelBg;
    TQPushButton* m_btnSelBg;
    TQColor m_selBgColor;

    TQComboBox* m_cmbSelFg;
    TQPushButton* m_btnSelFg;
    TQColor m_selFgColor;

    TQComboBox* m_cmbCpu;
    TQPushButton* m_btnCpu;
    TQColor m_cpuColor;

    TQComboBox* m_cmbRam;
    TQPushButton* m_btnRam;
    TQColor m_ramColor;

    TQComboBox* m_cmbNetRecv;
    TQPushButton* m_btnNetRecv;
    TQColor m_netRecvColor;

    TQComboBox* m_cmbNetSend;
    TQPushButton* m_btnNetSend;
    TQColor m_netSendColor;

    TQComboBox* m_cmbDiskRead;
    TQPushButton* m_btnDiskRead;
    TQColor m_diskReadColor;

    TQComboBox* m_cmbDiskWrite;
    TQPushButton* m_btnDiskWrite;
    TQColor m_diskWriteColor;

    TQComboBox* m_cmbGpu;
    TQPushButton* m_btnGpu;
    TQColor m_gpuColor;

    TQComboBox* m_cmbGpuRender;
    TQPushButton* m_btnGpuRender;
    TQColor m_gpuRenderColor;

    TQComboBox* m_cmbGpuVideo;
    TQPushButton* m_btnGpuVideo;
    TQColor m_gpuVideoColor;

    TQComboBox* m_cmbSystrayCpu;
    TQPushButton* m_btnSystrayCpuColor;
    TQColor m_systrayCpuColor;

    TQComboBox* m_cmbSystrayBgTint;
    TQPushButton* m_btnSystrayBgTintColor;
    TQColor m_systrayBgTintColor;

    TQComboBox* m_cmbGaugeBg;
    TQPushButton* m_btnGaugeBg;
    TQColor m_gaugeBgColor;

    TQComboBox* m_cmbGaugeCpu;
    TQPushButton* m_btnGaugeCpu;
    TQColor m_gaugeCpuColor;

    TQComboBox* m_cmbGaugeRam;
    TQPushButton* m_btnGaugeRam;
    TQColor m_gaugeRamColor;

    TQComboBox* m_cmbGaugeSwap;
    TQPushButton* m_btnGaugeSwap;
    TQColor m_gaugeSwapColor;

    TQComboBox* m_cmbEditor;
    TQComboBox* m_cmbBrowser;
    TQComboBox* m_cmbTerminal;

    TQPushButton* m_btnOk;
    TQPushButton* m_btnCancel;
};

void applyAppPalette();

namespace GraphColors {
    extern TQColor cpu;
    extern TQColor ram;
    extern TQColor netRecv;
    extern TQColor netSend;
    extern TQColor diskRead;
    extern TQColor diskWrite;
    extern TQColor gpu;
    extern TQColor gpuRender;
    extern TQColor gpuVideo;

    void load();
    TQColor getFillColor(const TQColor& stroke);
}

namespace GaugeColors {
    extern TQColor bg;
    extern TQColor cpu;
    extern TQColor ram;
    extern TQColor swap;

    void load();
}

#endif // PREFERENCES_DIALOG_H
