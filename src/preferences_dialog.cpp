#include "preferences_dialog.h"
#include "backend_bridge.h"
#include "cpu_tray_icon.h"
#include <ntqlayout.h>
#include <ntqlabel.h>
#include <ntqcolordialog.h>
#include <ntqpainter.h>
#include <ntqsettings.h>
#include <ntqapplication.h>
#include <ntqstyle.h>
#include <ntqobjectlist.h>

static void propagatePalette(TQWidget* w, const TQPalette& pal)
{
    if (!w) return;
    w->setPalette(pal);
    
    const TQObjectList* list = w->children();
    if (list) {
        TQObjectListIt it(*list);
        TQObject* obj;
        while ((obj = it.current())) {
            ++it;
            if (obj->isWidgetType()) {
                propagatePalette((TQWidget*)obj, pal);
            }
        }
    }
}

namespace GraphColors {
    TQColor cpu;
    TQColor ram;
    TQColor netRecv;
    TQColor netSend;
    TQColor diskRead;
    TQColor diskWrite;
    TQColor gpu;
    TQColor gpuRender;
    TQColor gpuVideo;

    TQColor getFillColor(const TQColor& stroke) {
        int h, s, v;
        stroke.getHsv(h, s, v);
        int fillS = s > 0 ? (s / 5) : 0;
        if (fillS > 30) fillS = 30;
        if (fillS < 8 && s > 0) fillS = 8;
        int fillV = 230 + (255 - v) / 10;
        if (fillV > 250) fillV = 250;
        if (fillV < 225) fillV = 225;
        TQColor c;
        c.setHsv(h, fillS, fillV);
        return c;
    }

    void load() {
        TQSettings settings;
        
        bool useCustomCpu = settings.readBoolEntry("/taskmgr/useCustomCpuColor", false);
        cpu = useCustomCpu ? TQColor(settings.readEntry("/taskmgr/cpuColor", "#5A7A8A")) : TQColor(90, 122, 138);

        bool useCustomRam = settings.readBoolEntry("/taskmgr/useCustomRamColor", false);
        ram = useCustomRam ? TQColor(settings.readEntry("/taskmgr/ramColor", "#8B12AE")) : TQColor(139, 18, 174);

        bool useCustomNetRecv = settings.readBoolEntry("/taskmgr/useCustomNetRecvColor", false);
        netRecv = useCustomNetRecv ? TQColor(settings.readEntry("/taskmgr/netRecvColor", "#0C6DA6")) : TQColor(12, 109, 166);

        bool useCustomNetSend = settings.readBoolEntry("/taskmgr/useCustomNetSendColor", false);
        netSend = useCustomNetSend ? TQColor(settings.readEntry("/taskmgr/netSendColor", "#A60C0C")) : TQColor(166, 12, 12);

        bool useCustomDiskRead = settings.readBoolEntry("/taskmgr/useCustomDiskReadColor", false);
        diskRead = useCustomDiskRead ? TQColor(settings.readEntry("/taskmgr/diskReadColor", "#4DA60C")) : TQColor(77, 166, 12);

        bool useCustomDiskWrite = settings.readBoolEntry("/taskmgr/useCustomDiskWriteColor", false);
        diskWrite = useCustomDiskWrite ? TQColor(settings.readEntry("/taskmgr/diskWriteColor", "#FF0000")) : TQColor(255, 0, 0);

        bool useCustomGpu = settings.readBoolEntry("/taskmgr/useCustomGpuColor", false);
        gpu = useCustomGpu ? TQColor(settings.readEntry("/taskmgr/gpuColor", "#2E7D32")) : TQColor(46, 125, 50);

        bool useCustomGpuRender = settings.readBoolEntry("/taskmgr/useCustomGpuRenderColor", false);
        gpuRender = useCustomGpuRender ? TQColor(settings.readEntry("/taskmgr/gpuRenderColor", "#FF5722")) : TQColor(255, 87, 34);

        bool useCustomGpuVideo = settings.readBoolEntry("/taskmgr/useCustomGpuVideoColor", false);
        gpuVideo = useCustomGpuVideo ? TQColor(settings.readEntry("/taskmgr/gpuVideoColor", "#2196F3")) : TQColor(33, 150, 243);
    }
}
namespace GaugeColors {
    TQColor bg;
    TQColor cpu;
    TQColor ram;
    TQColor swap;

    void load() {
        TQSettings settings;
        
        bool useCustomBg = settings.readBoolEntry("/taskmgr/useCustomGaugeBg", false);
        bg = useCustomBg ? TQColor(settings.readEntry("/taskmgr/gaugeBgColor", "#FFFFFF")) : TQColor(255, 255, 255);

        bool useCustomCpu = settings.readBoolEntry("/taskmgr/useCustomGaugeCpu", false);
        cpu = useCustomCpu ? TQColor(settings.readEntry("/taskmgr/gaugeCpuColor", "#A6D2FF")) : TQColor(166, 210, 255);

        bool useCustomRam = settings.readBoolEntry("/taskmgr/useCustomGaugeRam", false);
        ram = useCustomRam ? TQColor(settings.readEntry("/taskmgr/gaugeRamColor", "#A6D2FF")) : TQColor(166, 210, 255);

        bool useCustomSwap = settings.readBoolEntry("/taskmgr/useCustomGaugeSwap", false);
        swap = useCustomSwap ? TQColor(settings.readEntry("/taskmgr/gaugeSwapColor", "#A6D2FF")) : TQColor(166, 210, 255);
    }
}


void applyAppPalette()
{
    TQSettings settings;
    bool useCustomFg = settings.readBoolEntry("/taskmgr/useCustomFg", false);
    bool useCustomBg = settings.readBoolEntry("/taskmgr/useCustomBg", false);
    bool useCustomSelBg = settings.readBoolEntry("/taskmgr/useCustomSelectionBg", false);
    bool useCustomSelFg = settings.readBoolEntry("/taskmgr/useCustomSelectionFg", false);
    TQString fgColorStr = settings.readEntry("/taskmgr/foregroundColor", "#000000");
    TQString bgColorStr = settings.readEntry("/taskmgr/backgroundColor", "#ffffff");
    TQString selBgColorStr = settings.readEntry("/taskmgr/selectionBgColor", "#91C9F7");
    TQString selFgColorStr = settings.readEntry("/taskmgr/selectionFgColor", "#000000");

    TQWidget dummy;
    TQPalette pal = dummy.palette();

    if (useCustomFg) {
        TQColor fg(fgColorStr);
        pal.setColor(TQColorGroup::Foreground, fg);
        pal.setColor(TQColorGroup::Text, fg);
        pal.setColor(TQColorGroup::ButtonText, fg);
    }
    if (useCustomBg) {
        TQColor bg(bgColorStr);
        TQColor light = bg.light(150);
        TQColor midlight = bg.light(115);
        TQColor mid = bg.dark(115);
        TQColor dark = bg.dark(150);

        pal.setColor(TQColorGroup::Background, bg);
        pal.setColor(TQColorGroup::Base, bg);
        pal.setColor(TQColorGroup::Button, bg);
        pal.setColor(TQColorGroup::Light, light);
        pal.setColor(TQColorGroup::Midlight, midlight);
        pal.setColor(TQColorGroup::Mid, mid);
        pal.setColor(TQColorGroup::Dark, dark);
        pal.setColor(TQColorGroup::Shadow, TQt::black);
    }
    if (useCustomSelBg) {
        pal.setColor(TQColorGroup::Highlight, TQColor(selBgColorStr));
    }
    if (useCustomSelFg) {
        pal.setColor(TQColorGroup::HighlightedText, TQColor(selFgColorStr));
    }

    TQApplication::setPalette(pal, true);
}

PreferencesDialog::PreferencesDialog(TQWidget* parent)
    : TQDialog(parent, "preferences_dialog", true)
{
    setCaption("Settings");
    resize(780, 520);

    TQVBoxLayout* mainLayout = new TQVBoxLayout(this, 10, 8);

    TQHBoxLayout* colsLayout = new TQHBoxLayout(mainLayout);
    colsLayout->setSpacing(10);

    TQVBoxLayout* col1 = new TQVBoxLayout(colsLayout);
    col1->setSpacing(8);

    TQVBoxLayout* col2 = new TQVBoxLayout(colsLayout);
    col2->setSpacing(8);

    // Group 1: General Settings
    TQGroupBox* grpGeneral = new TQGroupBox("General Settings", this);
    grpGeneral->setColumnLayout(0, TQt::Vertical);
    grpGeneral->layout()->setSpacing(6);
    grpGeneral->layout()->setMargin(10);
    TQVBoxLayout* grpLayout = new TQVBoxLayout(grpGeneral->layout());

    m_chkMinimizeToTray = new TQCheckBox("Minimize to tray", grpGeneral);
    m_chkGauges = new TQCheckBox("Show header system gauges", grpGeneral);
    m_chkSmooth = new TQCheckBox("Enable smooth scrolling", grpGeneral);
    m_chkAntiAlias = new TQCheckBox("Enable anti aliasing", grpGeneral);
    m_chkIndividualFreq = new TQCheckBox("Display individual frequency in logical processor view", grpGeneral);
    m_chkUseTdeRunDialog = new TQCheckBox("Use TDE 'Run Command' dialog for new tasks", grpGeneral);
    m_chkStatusBar = new TQCheckBox("Show status bar", grpGeneral);

    grpLayout->addWidget(m_chkMinimizeToTray);
    grpLayout->addWidget(m_chkGauges);
    grpLayout->addWidget(m_chkSmooth);
    grpLayout->addWidget(m_chkAntiAlias);
    grpLayout->addWidget(m_chkIndividualFreq);
    grpLayout->addWidget(m_chkUseTdeRunDialog);
    grpLayout->addWidget(m_chkStatusBar);

    col1->addWidget(grpGeneral);

    // Group 2: Measurement
    TQGroupBox* grpMeasurement = new TQGroupBox("Measurement", this);
    grpMeasurement->setColumnLayout(0, TQt::Vertical);
    grpMeasurement->layout()->setSpacing(6);
    grpMeasurement->layout()->setMargin(10);
    TQVBoxLayout* measureLayout = new TQVBoxLayout(grpMeasurement->layout());

    m_chkPss = new TQCheckBox("Display real memory usage (PSS)", grpMeasurement);
    m_chkCachedAsFree = new TQCheckBox("Show cached memory as free", grpMeasurement);
    measureLayout->addWidget(m_chkPss);
    measureLayout->addWidget(m_chkCachedAsFree);

    TQHBoxLayout* gpuLayout = new TQHBoxLayout(measureLayout);
    gpuLayout->addWidget(new TQLabel("GPU usage:", grpMeasurement));
    m_cmbGpuMode = new TQComboBox(false, grpMeasurement);
    m_cmbGpuMode->insertItem("Process Time (cumulative)");
    m_cmbGpuMode->insertItem("Process Time average (balanced)");
    m_cmbGpuMode->insertItem("Hardware Approximation (realistic)");
    gpuLayout->addWidget(m_cmbGpuMode, 1);

    col1->addWidget(grpMeasurement);

    // Group 5: Default Applications
    TQGroupBox* grpApps = new TQGroupBox("Default Applications", this);
    grpApps->setColumnLayout(0, TQt::Vertical);
    grpApps->layout()->setSpacing(6);
    grpApps->layout()->setMargin(10);
    TQGridLayout* appGrid = new TQGridLayout(grpApps->layout(), 3, 2);

    appGrid->addWidget(new TQLabel("Default text editor:", grpApps), 0, 0);
    m_cmbEditor = new TQComboBox(false, grpApps);
    m_cmbEditor->insertItem("System default");
    for (int i = 0; i < available_editors_count; i++) {
        m_cmbEditor->insertItem(available_editors[i].name);
    }
    appGrid->addWidget(m_cmbEditor, 0, 1);

    appGrid->addWidget(new TQLabel("Default web browser:", grpApps), 1, 0);
    m_cmbBrowser = new TQComboBox(false, grpApps);
    m_cmbBrowser->insertItem("System default");
    for (int i = 0; i < available_browsers_count; i++) {
        m_cmbBrowser->insertItem(available_browsers[i].name);
    }
    appGrid->addWidget(m_cmbBrowser, 1, 1);

    appGrid->addWidget(new TQLabel("Default terminal emulator:", grpApps), 2, 0);
    m_cmbTerminal = new TQComboBox(false, grpApps);
    m_cmbTerminal->insertItem("System default");
    for (int i = 0; i < available_terminals_count; i++) {
        m_cmbTerminal->insertItem(available_terminals[i].name);
    }
    appGrid->addWidget(m_cmbTerminal, 2, 1);

    col1->addWidget(grpApps);

    // Group 3: UI colors
    // Group 3: UI colors
    TQGroupBox* grpColors = new TQGroupBox("UI colors", this);
    grpColors->setColumnLayout(0, TQt::Vertical);
    grpColors->layout()->setSpacing(6);
    grpColors->layout()->setMargin(10);
    TQGridLayout* grid = new TQGridLayout(grpColors->layout(), 4, 3);

    grid->addWidget(new TQLabel("Foreground:", grpColors), 0, 0);
    m_cmbFg = new TQComboBox(false, grpColors);
    m_cmbFg->insertItem("Default");
    m_cmbFg->insertItem("Custom");
    grid->addWidget(m_cmbFg, 0, 1);
    
    m_btnFgColor = new TQPushButton(grpColors);
    m_btnFgColor->setFixedWidth(50);
    grid->addWidget(m_btnFgColor, 0, 2);

    grid->addWidget(new TQLabel("Background:", grpColors), 1, 0);
    m_cmbBg = new TQComboBox(false, grpColors);
    m_cmbBg->insertItem("Default");
    m_cmbBg->insertItem("Custom");
    grid->addWidget(m_cmbBg, 1, 1);

    m_btnBgColor = new TQPushButton(grpColors);
    m_btnBgColor->setFixedWidth(50);
    grid->addWidget(m_btnBgColor, 1, 2);

    grid->addWidget(new TQLabel("Selection bg:", grpColors), 2, 0);
    m_cmbSelBg = new TQComboBox(false, grpColors);
    m_cmbSelBg->insertItem("Default");
    m_cmbSelBg->insertItem("Custom");
    grid->addWidget(m_cmbSelBg, 2, 1);

    m_btnSelBg = new TQPushButton(grpColors);
    m_btnSelBg->setFixedWidth(50);
    grid->addWidget(m_btnSelBg, 2, 2);

    grid->addWidget(new TQLabel("Selection fg:", grpColors), 3, 0);
    m_cmbSelFg = new TQComboBox(false, grpColors);
    m_cmbSelFg->insertItem("Default");
    m_cmbSelFg->insertItem("Custom");
    grid->addWidget(m_cmbSelFg, 3, 1);

    m_btnSelFg = new TQPushButton(grpColors);
    m_btnSelFg->setFixedWidth(50);
    grid->addWidget(m_btnSelFg, 3, 2);

    col2->addWidget(grpColors);

    // Group 3.5: Graph colors
    TQGroupBox* grpGraphColors = new TQGroupBox("Graph colors", this);
    grpGraphColors->setColumnLayout(0, TQt::Vertical);
    grpGraphColors->layout()->setSpacing(6);
    grpGraphColors->layout()->setMargin(10);
    TQGridLayout* graphGrid = new TQGridLayout(grpGraphColors->layout(), 5, 6);

    // CPU
    graphGrid->addWidget(new TQLabel("CPU:", grpGraphColors), 0, 0);
    m_cmbCpu = new TQComboBox(false, grpGraphColors);
    m_cmbCpu->insertItem("Default");
    m_cmbCpu->insertItem("Custom");
    graphGrid->addWidget(m_cmbCpu, 0, 1);
    m_btnCpu = new TQPushButton(grpGraphColors);
    m_btnCpu->setFixedWidth(40);
    graphGrid->addWidget(m_btnCpu, 0, 2);

    // RAM
    graphGrid->addWidget(new TQLabel("RAM:", grpGraphColors), 0, 3);
    m_cmbRam = new TQComboBox(false, grpGraphColors);
    m_cmbRam->insertItem("Default");
    m_cmbRam->insertItem("Custom");
    graphGrid->addWidget(m_cmbRam, 0, 4);
    m_btnRam = new TQPushButton(grpGraphColors);
    m_btnRam->setFixedWidth(40);
    graphGrid->addWidget(m_btnRam, 0, 5);

    // NET Send
    graphGrid->addWidget(new TQLabel("NET Send:", grpGraphColors), 1, 0);
    m_cmbNetSend = new TQComboBox(false, grpGraphColors);
    m_cmbNetSend->insertItem("Default");
    m_cmbNetSend->insertItem("Custom");
    graphGrid->addWidget(m_cmbNetSend, 1, 1);
    m_btnNetSend = new TQPushButton(grpGraphColors);
    m_btnNetSend->setFixedWidth(40);
    graphGrid->addWidget(m_btnNetSend, 1, 2);

    // NET Recv
    graphGrid->addWidget(new TQLabel("NET Recv:", grpGraphColors), 1, 3);
    m_cmbNetRecv = new TQComboBox(false, grpGraphColors);
    m_cmbNetRecv->insertItem("Default");
    m_cmbNetRecv->insertItem("Custom");
    graphGrid->addWidget(m_cmbNetRecv, 1, 4);
    m_btnNetRecv = new TQPushButton(grpGraphColors);
    m_btnNetRecv->setFixedWidth(40);
    graphGrid->addWidget(m_btnNetRecv, 1, 5);

    // DISK Read
    graphGrid->addWidget(new TQLabel("DISK Read:", grpGraphColors), 2, 0);
    m_cmbDiskRead = new TQComboBox(false, grpGraphColors);
    m_cmbDiskRead->insertItem("Default");
    m_cmbDiskRead->insertItem("Custom");
    graphGrid->addWidget(m_cmbDiskRead, 2, 1);
    m_btnDiskRead = new TQPushButton(grpGraphColors);
    m_btnDiskRead->setFixedWidth(40);
    graphGrid->addWidget(m_btnDiskRead, 2, 2);

    // DISK Write
    graphGrid->addWidget(new TQLabel("DISK Write:", grpGraphColors), 2, 3);
    m_cmbDiskWrite = new TQComboBox(false, grpGraphColors);
    m_cmbDiskWrite->insertItem("Default");
    m_cmbDiskWrite->insertItem("Custom");
    graphGrid->addWidget(m_cmbDiskWrite, 2, 4);
    m_btnDiskWrite = new TQPushButton(grpGraphColors);
    m_btnDiskWrite->setFixedWidth(40);
    graphGrid->addWidget(m_btnDiskWrite, 2, 5);

    // GPU overall
    graphGrid->addWidget(new TQLabel("GPU usage:", grpGraphColors), 3, 0);
    m_cmbGpu = new TQComboBox(false, grpGraphColors);
    m_cmbGpu->insertItem("Default");
    m_cmbGpu->insertItem("Custom");
    graphGrid->addWidget(m_cmbGpu, 3, 1);
    m_btnGpu = new TQPushButton(grpGraphColors);
    m_btnGpu->setFixedWidth(40);
    graphGrid->addWidget(m_btnGpu, 3, 2);

    // GPU render
    graphGrid->addWidget(new TQLabel("GPU render:", grpGraphColors), 3, 3);
    m_cmbGpuRender = new TQComboBox(false, grpGraphColors);
    m_cmbGpuRender->insertItem("Default");
    m_cmbGpuRender->insertItem("Custom");
    graphGrid->addWidget(m_cmbGpuRender, 3, 4);
    m_btnGpuRender = new TQPushButton(grpGraphColors);
    m_btnGpuRender->setFixedWidth(40);
    graphGrid->addWidget(m_btnGpuRender, 3, 5);

    // GPU video
    graphGrid->addWidget(new TQLabel("GPU video:", grpGraphColors), 4, 0);
    m_cmbGpuVideo = new TQComboBox(false, grpGraphColors);
    m_cmbGpuVideo->insertItem("Default");
    m_cmbGpuVideo->insertItem("Custom");
    graphGrid->addWidget(m_cmbGpuVideo, 4, 1);
    m_btnGpuVideo = new TQPushButton(grpGraphColors);
    m_btnGpuVideo->setFixedWidth(40);
    graphGrid->addWidget(m_btnGpuVideo, 4, 2);

    col2->addWidget(grpGraphColors);

    // Group 3.7: Gauge colors
    TQGroupBox* grpGaugeColors = new TQGroupBox("Gauge colors", this);
    grpGaugeColors->setColumnLayout(0, TQt::Vertical);
    grpGaugeColors->layout()->setSpacing(6);
    grpGaugeColors->layout()->setMargin(10);
    TQGridLayout* gaugeGrid = new TQGridLayout(grpGaugeColors->layout(), 2, 6);

    // Row 0: BG, CPU
    gaugeGrid->addWidget(new TQLabel("Background:", grpGaugeColors), 0, 0);
    m_cmbGaugeBg = new TQComboBox(false, grpGaugeColors);
    m_cmbGaugeBg->insertItem("Default");
    m_cmbGaugeBg->insertItem("Custom");
    gaugeGrid->addWidget(m_cmbGaugeBg, 0, 1);
    m_btnGaugeBg = new TQPushButton(grpGaugeColors);
    m_btnGaugeBg->setFixedWidth(40);
    gaugeGrid->addWidget(m_btnGaugeBg, 0, 2);

    gaugeGrid->addWidget(new TQLabel("CPU Gauge:", grpGaugeColors), 0, 3);
    m_cmbGaugeCpu = new TQComboBox(false, grpGaugeColors);
    m_cmbGaugeCpu->insertItem("Default");
    m_cmbGaugeCpu->insertItem("Custom");
    gaugeGrid->addWidget(m_cmbGaugeCpu, 0, 4);
    m_btnGaugeCpu = new TQPushButton(grpGaugeColors);
    m_btnGaugeCpu->setFixedWidth(40);
    gaugeGrid->addWidget(m_btnGaugeCpu, 0, 5);

    // Row 1: RAM, Swap
    gaugeGrid->addWidget(new TQLabel("RAM Gauge:", grpGaugeColors), 1, 0);
    m_cmbGaugeRam = new TQComboBox(false, grpGaugeColors);
    m_cmbGaugeRam->insertItem("Default");
    m_cmbGaugeRam->insertItem("Custom");
    gaugeGrid->addWidget(m_cmbGaugeRam, 1, 1);
    m_btnGaugeRam = new TQPushButton(grpGaugeColors);
    m_btnGaugeRam->setFixedWidth(40);
    gaugeGrid->addWidget(m_btnGaugeRam, 1, 2);

    gaugeGrid->addWidget(new TQLabel("Swap Gauge:", grpGaugeColors), 1, 3);
    m_cmbGaugeSwap = new TQComboBox(false, grpGaugeColors);
    m_cmbGaugeSwap->insertItem("Default");
    m_cmbGaugeSwap->insertItem("Custom");
    gaugeGrid->addWidget(m_cmbGaugeSwap, 1, 4);
    m_btnGaugeSwap = new TQPushButton(grpGaugeColors);
    m_btnGaugeSwap->setFixedWidth(40);
    gaugeGrid->addWidget(m_btnGaugeSwap, 1, 5);

    col2->addWidget(grpGaugeColors);

    // Push second column components up (if height is large enough)
    // col2->addStretch(1); // We don't need stretch anymore as the two columns are perfectly balanced in height now!

    // Group 4: Systray icon
    TQGroupBox* grpSystray = new TQGroupBox("Systray icon", this);
    grpSystray->setColumnLayout(0, TQt::Vertical);
    grpSystray->layout()->setSpacing(6);
    grpSystray->layout()->setMargin(10);
    TQGridLayout* systrayGrid = new TQGridLayout(grpSystray->layout(), 2, 3);

    systrayGrid->addWidget(new TQLabel("% cpu utilization", grpSystray), 0, 0);
    m_cmbSystrayCpu = new TQComboBox(false, grpSystray);
    m_cmbSystrayCpu->insertItem("Default");
    m_cmbSystrayCpu->insertItem("Custom");
    systrayGrid->addWidget(m_cmbSystrayCpu, 0, 1);

    m_btnSystrayCpuColor = new TQPushButton(grpSystray);
    m_btnSystrayCpuColor->setFixedWidth(50);
    systrayGrid->addWidget(m_btnSystrayCpuColor, 0, 2);

    systrayGrid->addWidget(new TQLabel("Background tint", grpSystray), 1, 0);
    m_cmbSystrayBgTint = new TQComboBox(false, grpSystray);
    m_cmbSystrayBgTint->insertItem("Default");
    m_cmbSystrayBgTint->insertItem("Custom");
    systrayGrid->addWidget(m_cmbSystrayBgTint, 1, 1);

    m_btnSystrayBgTintColor = new TQPushButton(grpSystray);
    m_btnSystrayBgTintColor->setFixedWidth(50);
    systrayGrid->addWidget(m_btnSystrayBgTintColor, 1, 2);

    col2->addWidget(grpSystray);

    // OK / Cancel buttons
    TQHBoxLayout* btnLayout = new TQHBoxLayout(mainLayout);
    btnLayout->addSpacing(10);
    btnLayout->addStretch(1);

    m_btnOk = new TQPushButton("OK", this);
    m_btnCancel = new TQPushButton("Cancel", this);
    btnLayout->addWidget(m_btnOk);
    btnLayout->addWidget(m_btnCancel);

    // Connections
    connect(m_cmbFg, SIGNAL(activated(int)), this, SLOT(onColorComboChanged(int)));
    connect(m_cmbBg, SIGNAL(activated(int)), this, SLOT(onColorComboChanged(int)));
    connect(m_cmbSelBg, SIGNAL(activated(int)), this, SLOT(onColorComboChanged(int)));
    connect(m_cmbSelFg, SIGNAL(activated(int)), this, SLOT(onColorComboChanged(int)));
    connect(m_cmbCpu, SIGNAL(activated(int)), this, SLOT(onColorComboChanged(int)));
    connect(m_cmbRam, SIGNAL(activated(int)), this, SLOT(onColorComboChanged(int)));
    connect(m_cmbNetRecv, SIGNAL(activated(int)), this, SLOT(onColorComboChanged(int)));
    connect(m_cmbNetSend, SIGNAL(activated(int)), this, SLOT(onColorComboChanged(int)));
    connect(m_cmbDiskRead, SIGNAL(activated(int)), this, SLOT(onColorComboChanged(int)));
    connect(m_cmbDiskWrite, SIGNAL(activated(int)), this, SLOT(onColorComboChanged(int)));
    connect(m_cmbGpu, SIGNAL(activated(int)), this, SLOT(onColorComboChanged(int)));
    connect(m_cmbGpuRender, SIGNAL(activated(int)), this, SLOT(onColorComboChanged(int)));
    connect(m_cmbGpuVideo, SIGNAL(activated(int)), this, SLOT(onColorComboChanged(int)));
    connect(m_cmbSystrayCpu, SIGNAL(activated(int)), this, SLOT(onColorComboChanged(int)));
    connect(m_cmbSystrayBgTint, SIGNAL(activated(int)), this, SLOT(onColorComboChanged(int)));
    connect(m_cmbGaugeBg, SIGNAL(activated(int)), this, SLOT(onColorComboChanged(int)));
    connect(m_cmbGaugeCpu, SIGNAL(activated(int)), this, SLOT(onColorComboChanged(int)));
    connect(m_cmbGaugeRam, SIGNAL(activated(int)), this, SLOT(onColorComboChanged(int)));
    connect(m_cmbGaugeSwap, SIGNAL(activated(int)), this, SLOT(onColorComboChanged(int)));

    connect(m_btnFgColor, SIGNAL(clicked()), this, SLOT(onColorButtonClicked()));
    connect(m_btnBgColor, SIGNAL(clicked()), this, SLOT(onColorButtonClicked()));
    connect(m_btnSelBg, SIGNAL(clicked()), this, SLOT(onColorButtonClicked()));
    connect(m_btnSelFg, SIGNAL(clicked()), this, SLOT(onColorButtonClicked()));
    connect(m_btnCpu, SIGNAL(clicked()), this, SLOT(onColorButtonClicked()));
    connect(m_btnRam, SIGNAL(clicked()), this, SLOT(onColorButtonClicked()));
    connect(m_btnNetRecv, SIGNAL(clicked()), this, SLOT(onColorButtonClicked()));
    connect(m_btnNetSend, SIGNAL(clicked()), this, SLOT(onColorButtonClicked()));
    connect(m_btnDiskRead, SIGNAL(clicked()), this, SLOT(onColorButtonClicked()));
    connect(m_btnDiskWrite, SIGNAL(clicked()), this, SLOT(onColorButtonClicked()));
    connect(m_btnGpu, SIGNAL(clicked()), this, SLOT(onColorButtonClicked()));
    connect(m_btnGpuRender, SIGNAL(clicked()), this, SLOT(onColorButtonClicked()));
    connect(m_btnGpuVideo, SIGNAL(clicked()), this, SLOT(onColorButtonClicked()));
    connect(m_btnSystrayCpuColor, SIGNAL(clicked()), this, SLOT(onColorButtonClicked()));
    connect(m_btnSystrayBgTintColor, SIGNAL(clicked()), this, SLOT(onColorButtonClicked()));
    connect(m_btnGaugeBg, SIGNAL(clicked()), this, SLOT(onColorButtonClicked()));
    connect(m_btnGaugeCpu, SIGNAL(clicked()), this, SLOT(onColorButtonClicked()));
    connect(m_btnGaugeRam, SIGNAL(clicked()), this, SLOT(onColorButtonClicked()));
    connect(m_btnGaugeSwap, SIGNAL(clicked()), this, SLOT(onColorButtonClicked()));

    m_btnFgColor->installEventFilter(this);
    m_btnBgColor->installEventFilter(this);
    m_btnSelBg->installEventFilter(this);
    m_btnSelFg->installEventFilter(this);
    m_btnCpu->installEventFilter(this);
    m_btnRam->installEventFilter(this);
    m_btnNetRecv->installEventFilter(this);
    m_btnNetSend->installEventFilter(this);
    m_btnDiskRead->installEventFilter(this);
    m_btnDiskWrite->installEventFilter(this);
    m_btnGpu->installEventFilter(this);
    m_btnGpuRender->installEventFilter(this);
    m_btnGpuVideo->installEventFilter(this);
    m_btnSystrayCpuColor->installEventFilter(this);
    m_btnSystrayBgTintColor->installEventFilter(this);
    m_btnGaugeBg->installEventFilter(this);
    m_btnGaugeCpu->installEventFilter(this);
    m_btnGaugeRam->installEventFilter(this);
    m_btnGaugeSwap->installEventFilter(this);

    connect(m_btnOk, SIGNAL(clicked()), this, SLOT(onOkClicked()));
    connect(m_btnCancel, SIGNAL(clicked()), this, SLOT(onCancelClicked()));

    // Load states
    guint16 flags = bridge_get_app_flags();
    m_chkMinimizeToTray->setChecked((flags & APP_FLAG_MINIMIZE_TO_TRAY) != 0);
    m_chkGauges->setChecked((flags & APP_FLAG_SHOW_HEADER_GAUGES) != 0);
    m_chkPss->setChecked((flags & APP_FLAG_DISPLAY_PSS) != 0);
    m_chkCachedAsFree->setChecked((flags & APP_FLAG_SHOW_CACHED_FREE) != 0);
    m_chkSmooth->setChecked((flags & APP_FLAG_SMOOTH_SCROLLING) != 0);
    m_chkAntiAlias->setChecked((flags & APP_FLAG_ENABLE_ANTIALIASING) != 0);
    m_cmbGpuMode->setCurrentItem(bridge_get_gpu_usage_mode());

    guint16 d_flags = bridge_get_display_flags();
    m_chkUseTdeRunDialog->setChecked((d_flags & DISPLAY_FLAG_USE_TDE_RUN_DIALOG) != 0);
    m_chkStatusBar->setChecked((d_flags & DISPLAY_FLAG_SHOW_STATUS_BAR) != 0);

    TQSettings settings;
    bool displayFreq = settings.readBoolEntry("/taskmgr/displayIndividualFrequency", true);
    m_chkIndividualFreq->setChecked(displayFreq);

    // UI Colors loading
    bool useCustomFg = settings.readBoolEntry("/taskmgr/useCustomFg", false);
    m_cmbFg->setCurrentItem(useCustomFg ? 1 : 0);
    m_fgColor = TQColor(settings.readEntry("/taskmgr/foregroundColor", "#000000"));
    updateColorButton(m_btnFgColor, getButtonColor(m_btnFgColor));

    bool useCustomBg = settings.readBoolEntry("/taskmgr/useCustomBg", false);
    m_cmbBg->setCurrentItem(useCustomBg ? 1 : 0);
    m_bgColor = TQColor(settings.readEntry("/taskmgr/backgroundColor", "#ffffff"));
    updateColorButton(m_btnBgColor, getButtonColor(m_btnBgColor));

    bool useCustomSelBg = settings.readBoolEntry("/taskmgr/useCustomSelectionBg", false);
    m_cmbSelBg->setCurrentItem(useCustomSelBg ? 1 : 0);
    m_selBgColor = TQColor(settings.readEntry("/taskmgr/selectionBgColor", "#91C9F7"));
    updateColorButton(m_btnSelBg, getButtonColor(m_btnSelBg));

    bool useCustomSelFg = settings.readBoolEntry("/taskmgr/useCustomSelectionFg", false);
    m_cmbSelFg->setCurrentItem(useCustomSelFg ? 1 : 0);
    m_selFgColor = TQColor(settings.readEntry("/taskmgr/selectionFgColor", "#000000"));
    updateColorButton(m_btnSelFg, getButtonColor(m_btnSelFg));

    // Graph Colors loading
    bool useCustomCpu = settings.readBoolEntry("/taskmgr/useCustomCpuColor", false);
    m_cmbCpu->setCurrentItem(useCustomCpu ? 1 : 0);
    m_cpuColor = TQColor(settings.readEntry("/taskmgr/cpuColor", "#5A7A8A"));
    updateColorButton(m_btnCpu, getButtonColor(m_btnCpu));

    bool useCustomRam = settings.readBoolEntry("/taskmgr/useCustomRamColor", false);
    m_cmbRam->setCurrentItem(useCustomRam ? 1 : 0);
    m_ramColor = TQColor(settings.readEntry("/taskmgr/ramColor", "#8B12AE"));
    updateColorButton(m_btnRam, getButtonColor(m_btnRam));

    bool useCustomNetRecv = settings.readBoolEntry("/taskmgr/useCustomNetRecvColor", false);
    m_cmbNetRecv->setCurrentItem(useCustomNetRecv ? 1 : 0);
    m_netRecvColor = TQColor(settings.readEntry("/taskmgr/netRecvColor", "#0C6DA6"));
    updateColorButton(m_btnNetRecv, getButtonColor(m_btnNetRecv));

    bool useCustomNetSend = settings.readBoolEntry("/taskmgr/useCustomNetSendColor", false);
    m_cmbNetSend->setCurrentItem(useCustomNetSend ? 1 : 0);
    m_netSendColor = TQColor(settings.readEntry("/taskmgr/netSendColor", "#A60C0C"));
    updateColorButton(m_btnNetSend, getButtonColor(m_btnNetSend));

    bool useCustomDiskRead = settings.readBoolEntry("/taskmgr/useCustomDiskReadColor", false);
    m_cmbDiskRead->setCurrentItem(useCustomDiskRead ? 1 : 0);
    m_diskReadColor = TQColor(settings.readEntry("/taskmgr/diskReadColor", "#4DA60C"));
    updateColorButton(m_btnDiskRead, getButtonColor(m_btnDiskRead));

    bool useCustomDiskWrite = settings.readBoolEntry("/taskmgr/useCustomDiskWriteColor", false);
    m_cmbDiskWrite->setCurrentItem(useCustomDiskWrite ? 1 : 0);
    m_diskWriteColor = TQColor(settings.readEntry("/taskmgr/diskWriteColor", "#FF0000"));
    updateColorButton(m_btnDiskWrite, getButtonColor(m_btnDiskWrite));

    bool useCustomGpu = settings.readBoolEntry("/taskmgr/useCustomGpuColor", false);
    m_cmbGpu->setCurrentItem(useCustomGpu ? 1 : 0);
    m_gpuColor = TQColor(settings.readEntry("/taskmgr/gpuColor", "#2E7D32"));
    updateColorButton(m_btnGpu, getButtonColor(m_btnGpu));

    bool useCustomGpuRender = settings.readBoolEntry("/taskmgr/useCustomGpuRenderColor", false);
    m_cmbGpuRender->setCurrentItem(useCustomGpuRender ? 1 : 0);
    m_gpuRenderColor = TQColor(settings.readEntry("/taskmgr/gpuRenderColor", "#FF5722"));
    updateColorButton(m_btnGpuRender, getButtonColor(m_btnGpuRender));

    bool useCustomGpuVideo = settings.readBoolEntry("/taskmgr/useCustomGpuVideoColor", false);
    m_cmbGpuVideo->setCurrentItem(useCustomGpuVideo ? 1 : 0);
    m_gpuVideoColor = TQColor(settings.readEntry("/taskmgr/gpuVideoColor", "#2196F3"));
    updateColorButton(m_btnGpuVideo, getButtonColor(m_btnGpuVideo));

    // Gauge Colors loading
    bool useCustomGaugeBg = settings.readBoolEntry("/taskmgr/useCustomGaugeBg", false);
    m_cmbGaugeBg->setCurrentItem(useCustomGaugeBg ? 1 : 0);
    m_gaugeBgColor = TQColor(settings.readEntry("/taskmgr/gaugeBgColor", "#FFFFFF"));
    updateColorButton(m_btnGaugeBg, getButtonColor(m_btnGaugeBg));

    bool useCustomGaugeCpu = settings.readBoolEntry("/taskmgr/useCustomGaugeCpu", false);
    m_cmbGaugeCpu->setCurrentItem(useCustomGaugeCpu ? 1 : 0);
    m_gaugeCpuColor = TQColor(settings.readEntry("/taskmgr/gaugeCpuColor", "#A6D2FF"));
    updateColorButton(m_btnGaugeCpu, getButtonColor(m_btnGaugeCpu));

    bool useCustomGaugeRam = settings.readBoolEntry("/taskmgr/useCustomGaugeRam", false);
    m_cmbGaugeRam->setCurrentItem(useCustomGaugeRam ? 1 : 0);
    m_gaugeRamColor = TQColor(settings.readEntry("/taskmgr/gaugeRamColor", "#A6D2FF"));
    updateColorButton(m_btnGaugeRam, getButtonColor(m_btnGaugeRam));

    bool useCustomGaugeSwap = settings.readBoolEntry("/taskmgr/useCustomGaugeSwap", false);
    m_cmbGaugeSwap->setCurrentItem(useCustomGaugeSwap ? 1 : 0);
    m_gaugeSwapColor = TQColor(settings.readEntry("/taskmgr/gaugeSwapColor", "#A6D2FF"));
    updateColorButton(m_btnGaugeSwap, getButtonColor(m_btnGaugeSwap));

    // Systray & defaults loading
    bool useCustomSystrayCpu = settings.readBoolEntry("/taskmgr/systrayCustomCpuColor", false);
    m_cmbSystrayCpu->setCurrentItem(useCustomSystrayCpu ? 1 : 0);
    m_systrayCpuColor = TQColor(settings.readEntry("/taskmgr/systrayCpuColor", "#56BFFE"));
    updateColorButton(m_btnSystrayCpuColor, getButtonColor(m_btnSystrayCpuColor));

    bool useCustomSystrayBgTint = settings.readBoolEntry("/taskmgr/systrayCustomBgTint", false);
    m_cmbSystrayBgTint->setCurrentItem(useCustomSystrayBgTint ? 1 : 0);
    m_systrayBgTintColor = TQColor(settings.readEntry("/taskmgr/systrayBgTintColor", "#808080"));
    updateColorButton(m_btnSystrayBgTintColor, getButtonColor(m_btnSystrayBgTintColor));

    m_cmbEditor->setCurrentItem(editor_manager.default_index);
    m_cmbBrowser->setCurrentItem(browser_manager.default_index);
    m_cmbTerminal->setCurrentItem(terminal_manager.default_index);
}

PreferencesDialog::~PreferencesDialog()
{
}

bool PreferencesDialog::eventFilter(TQObject* watched, TQEvent* e)
{
    if (watched && watched->inherits("TQPushButton")) {
        TQPushButton* btn = (TQPushButton*)watched;
        TQComboBox* cmb = NULL;
        if (btn == m_btnFgColor) cmb = m_cmbFg;
        else if (btn == m_btnBgColor) cmb = m_cmbBg;
        else if (btn == m_btnSelBg) cmb = m_cmbSelBg;
        else if (btn == m_btnSelFg) cmb = m_cmbSelFg;
        else if (btn == m_btnCpu) cmb = m_cmbCpu;
        else if (btn == m_btnRam) cmb = m_cmbRam;
        else if (btn == m_btnNetRecv) cmb = m_cmbNetRecv;
        else if (btn == m_btnNetSend) cmb = m_cmbNetSend;
        else if (btn == m_btnDiskRead) cmb = m_cmbDiskRead;
        else if (btn == m_btnDiskWrite) cmb = m_cmbDiskWrite;
        else if (btn == m_btnGpu) cmb = m_cmbGpu;
        else if (btn == m_btnGpuRender) cmb = m_cmbGpuRender;
        else if (btn == m_btnGpuVideo) cmb = m_cmbGpuVideo;
        else if (btn == m_btnSystrayCpuColor) cmb = m_cmbSystrayCpu;
        else if (btn == m_btnSystrayBgTintColor) cmb = m_cmbSystrayBgTint;
        else if (btn == m_btnGaugeBg) cmb = m_cmbGaugeBg;
        else if (btn == m_btnGaugeCpu) cmb = m_cmbGaugeCpu;
        else if (btn == m_btnGaugeRam) cmb = m_cmbGaugeRam;
        else if (btn == m_btnGaugeSwap) cmb = m_cmbGaugeSwap;

        if (cmb && cmb->currentItem() == 0) {
            // It is in "Default" / "System" mode! Ignore hover and click events.
            if (e->type() == TQEvent::MouseButtonPress ||
                e->type() == TQEvent::MouseButtonRelease ||
                e->type() == TQEvent::MouseButtonDblClick ||
                e->type() == TQEvent::Enter ||
                e->type() == TQEvent::Leave) {
                return true; // Discard event
            }
        }
    }
    return TQDialog::eventFilter(watched, e);
}

void PreferencesDialog::onColorComboChanged(int index)
{
    (void)index;
    TQComboBox* cmb = (TQComboBox*)sender();
    if (!cmb) return;

    TQPushButton* btn = NULL;
    if (cmb == m_cmbFg) btn = m_btnFgColor;
    else if (cmb == m_cmbBg) btn = m_btnBgColor;
    else if (cmb == m_cmbSelBg) btn = m_btnSelBg;
    else if (cmb == m_cmbSelFg) btn = m_btnSelFg;
    else if (cmb == m_cmbCpu) btn = m_btnCpu;
    else if (cmb == m_cmbRam) btn = m_btnRam;
    else if (cmb == m_cmbNetRecv) btn = m_btnNetRecv;
    else if (cmb == m_cmbNetSend) btn = m_btnNetSend;
    else if (cmb == m_cmbDiskRead) btn = m_btnDiskRead;
    else if (cmb == m_cmbDiskWrite) btn = m_btnDiskWrite;
    else if (cmb == m_cmbGpu) btn = m_btnGpu;
    else if (cmb == m_cmbGpuRender) btn = m_btnGpuRender;
    else if (cmb == m_cmbGpuVideo) btn = m_btnGpuVideo;
    else if (cmb == m_cmbSystrayCpu) btn = m_btnSystrayCpuColor;
    else if (cmb == m_cmbSystrayBgTint) btn = m_btnSystrayBgTintColor;
    else if (cmb == m_cmbGaugeBg) btn = m_btnGaugeBg;
    else if (cmb == m_cmbGaugeCpu) btn = m_btnGaugeCpu;
    else if (cmb == m_cmbGaugeRam) btn = m_btnGaugeRam;
    else if (cmb == m_cmbGaugeSwap) btn = m_btnGaugeSwap;

    if (btn) {
        updateColorButton(btn, getButtonColor(btn));
    }
}

void PreferencesDialog::onColorButtonClicked()
{
    TQPushButton* btn = (TQPushButton*)sender();
    if (!btn) return;

    TQColor* col = NULL;
    TQString title;
    TQComboBox* cmb = NULL;
    if (btn == m_btnFgColor) { col = &m_fgColor; title = "Select Foreground Color"; cmb = m_cmbFg; }
    else if (btn == m_btnBgColor) { col = &m_bgColor; title = "Select Background Color"; cmb = m_cmbBg; }
    else if (btn == m_btnSelBg) { col = &m_selBgColor; title = "Select Selection Background"; cmb = m_cmbSelBg; }
    else if (btn == m_btnSelFg) { col = &m_selFgColor; title = "Select Selection Foreground"; cmb = m_cmbSelFg; }
    else if (btn == m_btnCpu) { col = &m_cpuColor; title = "Select CPU Graph Color"; cmb = m_cmbCpu; }
    else if (btn == m_btnRam) { col = &m_ramColor; title = "Select RAM Graph Color"; cmb = m_cmbRam; }
    else if (btn == m_btnNetRecv) { col = &m_netRecvColor; title = "Select Network Rx Color"; cmb = m_cmbNetRecv; }
    else if (btn == m_btnNetSend) { col = &m_netSendColor; title = "Select Network Tx Color"; cmb = m_cmbNetSend; }
    else if (btn == m_btnDiskRead) { col = &m_diskReadColor; title = "Select Disk Read Color"; cmb = m_cmbDiskRead; }
    else if (btn == m_btnDiskWrite) { col = &m_diskWriteColor; title = "Select Disk Write Color"; cmb = m_cmbDiskWrite; }
    else if (btn == m_btnGpu) { col = &m_gpuColor; title = "Select GPU Color"; cmb = m_cmbGpu; }
    else if (btn == m_btnGpuRender) { col = &m_gpuRenderColor; title = "Select GPU Render Color"; cmb = m_cmbGpuRender; }
    else if (btn == m_btnGpuVideo) { col = &m_gpuVideoColor; title = "Select GPU Video Color"; cmb = m_cmbGpuVideo; }
    else if (btn == m_btnSystrayCpuColor) { col = &m_systrayCpuColor; title = "Select Systray CPU Color"; cmb = m_cmbSystrayCpu; }
    else if (btn == m_btnSystrayBgTintColor) { col = &m_systrayBgTintColor; title = "Select Systray Tint Color"; cmb = m_cmbSystrayBgTint; }
    else if (btn == m_btnGaugeBg) { col = &m_gaugeBgColor; title = "Select Gauge Background"; cmb = m_cmbGaugeBg; }
    else if (btn == m_btnGaugeCpu) { col = &m_gaugeCpuColor; title = "Select CPU Gauge Color"; cmb = m_cmbGaugeCpu; }
    else if (btn == m_btnGaugeRam) { col = &m_gaugeRamColor; title = "Select RAM Gauge Color"; cmb = m_cmbGaugeRam; }
    else if (btn == m_btnGaugeSwap) { col = &m_gaugeSwapColor; title = "Select Swap Gauge Color"; cmb = m_cmbGaugeSwap; }

    if (col) {
        TQColor c = TQColorDialog::getColor(*col, this, "color_dialog");
        if (c.isValid()) {
            *col = c;
            updateColorButton(btn, c);
            if (cmb) {
                cmb->setCurrentItem(1);
            }
        }
    }
}

TQColor PreferencesDialog::getButtonColor(TQPushButton* btn)
{
    if (btn == m_btnFgColor) return (m_cmbFg->currentItem() == 1) ? m_fgColor : TQColor("#000000");
    if (btn == m_btnBgColor) return (m_cmbBg->currentItem() == 1) ? m_bgColor : TQColor("#ffffff");
    if (btn == m_btnSelBg) return (m_cmbSelBg->currentItem() == 1) ? m_selBgColor : TQColor("#91C9F7");
    if (btn == m_btnSelFg) return (m_cmbSelFg->currentItem() == 1) ? m_selFgColor : TQColor("#000000");
    if (btn == m_btnCpu) return (m_cmbCpu->currentItem() == 1) ? m_cpuColor : TQColor("#5A7A8A");
    if (btn == m_btnRam) return (m_cmbRam->currentItem() == 1) ? m_ramColor : TQColor("#8B12AE");
    if (btn == m_btnNetRecv) return (m_cmbNetRecv->currentItem() == 1) ? m_netRecvColor : TQColor("#0C6DA6");
    if (btn == m_btnNetSend) return (m_cmbNetSend->currentItem() == 1) ? m_netSendColor : TQColor("#A60C0C");
    if (btn == m_btnDiskRead) return (m_cmbDiskRead->currentItem() == 1) ? m_diskReadColor : TQColor("#4DA60C");
    if (btn == m_btnDiskWrite) return (m_cmbDiskWrite->currentItem() == 1) ? m_diskWriteColor : TQColor("#FF0000");
    if (btn == m_btnGpu) return (m_cmbGpu->currentItem() == 1) ? m_gpuColor : TQColor("#2E7D32");
    if (btn == m_btnGpuRender) return (m_cmbGpuRender->currentItem() == 1) ? m_gpuRenderColor : TQColor("#FF5722");
    if (btn == m_btnGpuVideo) return (m_cmbGpuVideo->currentItem() == 1) ? m_gpuVideoColor : TQColor("#2196F3");
    if (btn == m_btnSystrayCpuColor) return (m_cmbSystrayCpu->currentItem() == 1) ? m_systrayCpuColor : TQColor("#56BFFE");
    if (btn == m_btnSystrayBgTintColor) return (m_cmbSystrayBgTint->currentItem() == 1) ? m_systrayBgTintColor : paletteBackgroundColor();
    if (btn == m_btnGaugeBg) return (m_cmbGaugeBg->currentItem() == 1) ? m_gaugeBgColor : TQColor("#FFFFFF");
    if (btn == m_btnGaugeCpu) return (m_cmbGaugeCpu->currentItem() == 1) ? m_gaugeCpuColor : TQColor("#A6D2FF");
    if (btn == m_btnGaugeRam) return (m_cmbGaugeRam->currentItem() == 1) ? m_gaugeRamColor : TQColor("#A6D2FF");
    if (btn == m_btnGaugeSwap) return (m_cmbGaugeSwap->currentItem() == 1) ? m_gaugeSwapColor : TQColor("#A6D2FF");
    return TQColor();
}

void PreferencesDialog::updateColorButton(TQPushButton* btn, const TQColor& color)
{
    TQPixmap px(24, 16);
    TQPainter p(&px);
    p.fillRect(0, 0, 24, 16, color);
    p.setPen(TQt::black);
    p.drawRect(0, 0, 24, 16);
    p.end();
    btn->setPixmap(px);
}

void PreferencesDialog::onOkClicked()
{
    // Save backend flags
    guint16 flags = bridge_get_app_flags();
    if (m_chkMinimizeToTray->isChecked()) {
        flags |= APP_FLAG_MINIMIZE_TO_TRAY;
    } else {
        flags &= ~APP_FLAG_MINIMIZE_TO_TRAY;
    }

    if (m_chkGauges->isChecked()) {
        flags |= APP_FLAG_SHOW_HEADER_GAUGES;
    } else {
        flags &= ~APP_FLAG_SHOW_HEADER_GAUGES;
    }

    if (m_chkPss->isChecked()) {
        flags |= APP_FLAG_DISPLAY_PSS;
        set_optimization_flag(OPTIMIZATION_FLAG_PSS_LOADING, TRUE);
    } else {
        flags &= ~APP_FLAG_DISPLAY_PSS;
        set_optimization_flag(OPTIMIZATION_FLAG_PSS_LOADING, FALSE);
    }

    if (m_chkCachedAsFree->isChecked()) {
        flags |= APP_FLAG_SHOW_CACHED_FREE;
    } else {
        flags &= ~APP_FLAG_SHOW_CACHED_FREE;
    }

    if (m_chkSmooth->isChecked()) {
        flags |= APP_FLAG_SMOOTH_SCROLLING;
    } else {
        flags &= ~APP_FLAG_SMOOTH_SCROLLING;
    }

    if (m_chkAntiAlias->isChecked()) {
        flags |= APP_FLAG_ENABLE_ANTIALIASING;
    } else {
        flags &= ~APP_FLAG_ENABLE_ANTIALIASING;
    }

    bridge_set_app_flags(flags);
    bridge_set_gpu_usage_mode(m_cmbGpuMode->currentItem());
    set_default_app(&editor_manager, m_cmbEditor->currentItem());
    set_default_app(&browser_manager, m_cmbBrowser->currentItem());
    set_default_app(&terminal_manager, m_cmbTerminal->currentItem());

    guint16 d_flags = bridge_get_display_flags();
    if (m_chkUseTdeRunDialog->isChecked()) {
        d_flags |= DISPLAY_FLAG_USE_TDE_RUN_DIALOG;
    } else {
        d_flags &= ~DISPLAY_FLAG_USE_TDE_RUN_DIALOG;
    }

    if (m_chkStatusBar->isChecked()) {
        d_flags |= DISPLAY_FLAG_SHOW_STATUS_BAR;
    } else {
        d_flags &= ~DISPLAY_FLAG_SHOW_STATUS_BAR;
    }
    bridge_set_display_flags(d_flags);

    save_config();

    // Save custom colors
    bool useCustomFg = (m_cmbFg->currentItem() == 1);
    bool useCustomBg = (m_cmbBg->currentItem() == 1);
    bool useCustomSelBg = (m_cmbSelBg->currentItem() == 1);
    bool useCustomSelFg = (m_cmbSelFg->currentItem() == 1);
    bool useCustomCpu = (m_cmbCpu->currentItem() == 1);
    bool useCustomRam = (m_cmbRam->currentItem() == 1);
    bool useCustomNetRecv = (m_cmbNetRecv->currentItem() == 1);
    bool useCustomNetSend = (m_cmbNetSend->currentItem() == 1);
    bool useCustomDiskRead = (m_cmbDiskRead->currentItem() == 1);
    bool useCustomDiskWrite = (m_cmbDiskWrite->currentItem() == 1);
    bool useCustomGpu = (m_cmbGpu->currentItem() == 1);
    bool useCustomGpuRender = (m_cmbGpuRender->currentItem() == 1);
    bool useCustomGpuVideo = (m_cmbGpuVideo->currentItem() == 1);
    bool useCustomSystrayCpu = (m_cmbSystrayCpu->currentItem() == 1);
    bool useCustomSystrayBgTint = (m_cmbSystrayBgTint->currentItem() == 1);

    bool useCustomGaugeBg = (m_cmbGaugeBg->currentItem() == 1);
    bool useCustomGaugeCpu = (m_cmbGaugeCpu->currentItem() == 1);
    bool useCustomGaugeRam = (m_cmbGaugeRam->currentItem() == 1);
    bool useCustomGaugeSwap = (m_cmbGaugeSwap->currentItem() == 1);

    {
        TQSettings settings;
        settings.writeEntry("/taskmgr/useCustomFg", useCustomFg);
        settings.writeEntry("/taskmgr/foregroundColor", m_fgColor.name());
        settings.writeEntry("/taskmgr/useCustomBg", useCustomBg);
        settings.writeEntry("/taskmgr/backgroundColor", m_bgColor.name());
        
        settings.writeEntry("/taskmgr/useCustomSelectionBg", useCustomSelBg);
        settings.writeEntry("/taskmgr/selectionBgColor", m_selBgColor.name());
        settings.writeEntry("/taskmgr/useCustomSelectionFg", useCustomSelFg);
        settings.writeEntry("/taskmgr/selectionFgColor", m_selFgColor.name());

        settings.writeEntry("/taskmgr/useCustomCpuColor", useCustomCpu);
        settings.writeEntry("/taskmgr/cpuColor", m_cpuColor.name());
        settings.writeEntry("/taskmgr/useCustomRamColor", useCustomRam);
        settings.writeEntry("/taskmgr/ramColor", m_ramColor.name());
        settings.writeEntry("/taskmgr/useCustomNetRecvColor", useCustomNetRecv);
        settings.writeEntry("/taskmgr/netRecvColor", m_netRecvColor.name());
        settings.writeEntry("/taskmgr/useCustomNetSendColor", useCustomNetSend);
        settings.writeEntry("/taskmgr/netSendColor", m_netSendColor.name());
        settings.writeEntry("/taskmgr/useCustomDiskReadColor", useCustomDiskRead);
        settings.writeEntry("/taskmgr/diskReadColor", m_diskReadColor.name());
        settings.writeEntry("/taskmgr/useCustomDiskWriteColor", useCustomDiskWrite);
        settings.writeEntry("/taskmgr/diskWriteColor", m_diskWriteColor.name());
        settings.writeEntry("/taskmgr/useCustomGpuColor", useCustomGpu);
        settings.writeEntry("/taskmgr/gpuColor", m_gpuColor.name());
        settings.writeEntry("/taskmgr/useCustomGpuRenderColor", useCustomGpuRender);
        settings.writeEntry("/taskmgr/gpuRenderColor", m_gpuRenderColor.name());
        settings.writeEntry("/taskmgr/useCustomGpuVideoColor", useCustomGpuVideo);
        settings.writeEntry("/taskmgr/gpuVideoColor", m_gpuVideoColor.name());

        settings.writeEntry("/taskmgr/useCustomGaugeBg", useCustomGaugeBg);
        settings.writeEntry("/taskmgr/gaugeBgColor", m_gaugeBgColor.name());
        settings.writeEntry("/taskmgr/useCustomGaugeCpu", useCustomGaugeCpu);
        settings.writeEntry("/taskmgr/gaugeCpuColor", m_gaugeCpuColor.name());
        settings.writeEntry("/taskmgr/useCustomGaugeRam", useCustomGaugeRam);
        settings.writeEntry("/taskmgr/gaugeRamColor", m_gaugeRamColor.name());
        settings.writeEntry("/taskmgr/useCustomGaugeSwap", useCustomGaugeSwap);
        settings.writeEntry("/taskmgr/gaugeSwapColor", m_gaugeSwapColor.name());

        settings.writeEntry("/taskmgr/systrayCustomCpuColor", useCustomSystrayCpu);
        settings.writeEntry("/taskmgr/systrayCpuColor", m_systrayCpuColor.name());
        settings.writeEntry("/taskmgr/systrayCustomBgTint", useCustomSystrayBgTint);
        settings.writeEntry("/taskmgr/systrayBgTintColor", m_systrayBgTintColor.name());
        settings.writeEntry("/taskmgr/displayIndividualFrequency", m_chkIndividualFreq->isChecked());
    }

    freeCpuTrayIconCache();

    // Reload graph and gauge colors instantly in memory
    GraphColors::load();
    GaugeColors::load();

    // Apply palette immediately from dialog's values
    TQWidget dummy;
    TQPalette pal = dummy.palette();
    if (useCustomFg) {
        pal.setColor(TQColorGroup::Foreground, m_fgColor);
        pal.setColor(TQColorGroup::Text, m_fgColor);
        pal.setColor(TQColorGroup::ButtonText, m_fgColor);
    }
    if (useCustomBg) {
        TQColor light = m_bgColor.light(150);
        TQColor midlight = m_bgColor.light(115);
        TQColor mid = m_bgColor.dark(115);
        TQColor dark = m_bgColor.dark(150);

        pal.setColor(TQColorGroup::Background, m_bgColor);
        pal.setColor(TQColorGroup::Base, m_bgColor);
        pal.setColor(TQColorGroup::Button, m_bgColor);
        pal.setColor(TQColorGroup::Light, light);
        pal.setColor(TQColorGroup::Midlight, midlight);
        pal.setColor(TQColorGroup::Mid, mid);
        pal.setColor(TQColorGroup::Dark, dark);
        pal.setColor(TQColorGroup::Shadow, TQt::black);
    }
    if (useCustomSelBg) {
        pal.setColor(TQColorGroup::Highlight, m_selBgColor);
    }
    if (useCustomSelFg) {
        pal.setColor(TQColorGroup::HighlightedText, m_selFgColor);
    }

    TQApplication::setPalette(pal, true);

    // Propagate recursively starting from the parent window
    propagatePalette(parentWidget(), TQApplication::palette());

    accept();
}

void PreferencesDialog::onCancelClicked()
{
    reject();
}

#include "preferences_dialog.moc"
