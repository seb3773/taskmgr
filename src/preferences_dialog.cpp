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

void applyAppPalette()
{
    TQSettings settings;
    bool useCustomFg = settings.readBoolEntry("/taskmgr/useCustomFg", false);
    bool useCustomBg = settings.readBoolEntry("/taskmgr/useCustomBg", false);
    TQString fgColorStr = settings.readEntry("/taskmgr/foregroundColor", "#000000");
    TQString bgColorStr = settings.readEntry("/taskmgr/backgroundColor", "#ffffff");

    TQWidget dummy;
    TQPalette pal = dummy.palette();

    if (!useCustomFg && !useCustomBg) {
        TQApplication::setPalette(pal, true);
        return;
    }

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

    TQApplication::setPalette(pal, true);
}

PreferencesDialog::PreferencesDialog(TQWidget* parent)
    : TQDialog(parent, "preferences_dialog", true)
{
    setCaption("Settings");
    resize(560, 580);

    TQVBoxLayout* mainLayout = new TQVBoxLayout(this, 10, 8);

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

    grpLayout->addWidget(m_chkMinimizeToTray);
    grpLayout->addWidget(m_chkGauges);
    grpLayout->addWidget(m_chkSmooth);
    grpLayout->addWidget(m_chkAntiAlias);
    grpLayout->addWidget(m_chkIndividualFreq);

    mainLayout->addWidget(grpGeneral);

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

    mainLayout->addWidget(grpMeasurement);

    // Group 3: UI colors
    TQGroupBox* grpColors = new TQGroupBox("UI colors", this);
    grpColors->setColumnLayout(0, TQt::Vertical);
    grpColors->layout()->setSpacing(6);
    grpColors->layout()->setMargin(10);
    TQGridLayout* grid = new TQGridLayout(grpColors->layout(), 2, 3);

    grid->addWidget(new TQLabel("Foreground:", grpColors), 0, 0);
    m_cmbFg = new TQComboBox(false, grpColors);
    m_cmbFg->insertItem("System");
    m_cmbFg->insertItem("Custom");
    grid->addWidget(m_cmbFg, 0, 1);
    
    m_btnFgColor = new TQPushButton(grpColors);
    m_btnFgColor->setFixedWidth(50);
    grid->addWidget(m_btnFgColor, 0, 2);

    grid->addWidget(new TQLabel("Background:", grpColors), 1, 0);
    m_cmbBg = new TQComboBox(false, grpColors);
    m_cmbBg->insertItem("System");
    m_cmbBg->insertItem("Custom");
    grid->addWidget(m_cmbBg, 1, 1);

    m_btnBgColor = new TQPushButton(grpColors);
    m_btnBgColor->setFixedWidth(50);
    grid->addWidget(m_btnBgColor, 1, 2);

    mainLayout->addWidget(grpColors);

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

    mainLayout->addWidget(grpSystray);

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

    mainLayout->addWidget(grpApps);

    // OK / Cancel buttons
    TQHBoxLayout* btnLayout = new TQHBoxLayout(mainLayout);
    btnLayout->addSpacing(10);
    btnLayout->addStretch(1);

    m_btnOk = new TQPushButton("OK", this);
    m_btnCancel = new TQPushButton("Cancel", this);
    btnLayout->addWidget(m_btnOk);
    btnLayout->addWidget(m_btnCancel);

    // Connections
    connect(m_cmbFg, SIGNAL(activated(int)), this, SLOT(onFgModeChanged(int)));
    connect(m_cmbBg, SIGNAL(activated(int)), this, SLOT(onBgModeChanged(int)));
    connect(m_btnFgColor, SIGNAL(clicked()), this, SLOT(onFgColorClicked()));
    connect(m_btnBgColor, SIGNAL(clicked()), this, SLOT(onBgColorClicked()));
    connect(m_cmbSystrayCpu, SIGNAL(activated(int)), this, SLOT(onSystrayCpuModeChanged(int)));
    connect(m_btnSystrayCpuColor, SIGNAL(clicked()), this, SLOT(onSystrayCpuColorClicked()));
    connect(m_cmbSystrayBgTint, SIGNAL(activated(int)), this, SLOT(onSystrayBgTintModeChanged(int)));
    connect(m_btnSystrayBgTintColor, SIGNAL(clicked()), this, SLOT(onSystrayBgTintColorClicked()));
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

    TQSettings settings;
    bool displayFreq = settings.readBoolEntry("/taskmgr/displayIndividualFrequency", true);
    m_chkIndividualFreq->setChecked(displayFreq);
    bool useCustomFg = settings.readBoolEntry("/taskmgr/useCustomFg", false);
    m_cmbFg->setCurrentItem(useCustomFg ? 1 : 0);
    m_fgColor = TQColor(settings.readEntry("/taskmgr/foregroundColor", "#000000"));
    updateColorButton(m_btnFgColor, m_fgColor);
    m_btnFgColor->setEnabled(useCustomFg);

    bool useCustomBg = settings.readBoolEntry("/taskmgr/useCustomBg", false);
    m_cmbBg->setCurrentItem(useCustomBg ? 1 : 0);
    m_bgColor = TQColor(settings.readEntry("/taskmgr/backgroundColor", "#ffffff"));
    updateColorButton(m_btnBgColor, m_bgColor);
    m_btnBgColor->setEnabled(useCustomBg);

    bool useCustomSystrayCpu = settings.readBoolEntry("/taskmgr/systrayCustomCpuColor", false);
    m_cmbSystrayCpu->setCurrentItem(useCustomSystrayCpu ? 1 : 0);
    m_systrayCpuColor = TQColor(settings.readEntry("/taskmgr/systrayCpuColor", "#56BFFE"));
    updateColorButton(m_btnSystrayCpuColor,
        useCustomSystrayCpu ? m_systrayCpuColor : TQColor("#56BFFE"));
    m_btnSystrayCpuColor->setEnabled(useCustomSystrayCpu);

    bool useCustomSystrayBgTint = settings.readBoolEntry("/taskmgr/systrayCustomBgTint", false);
    m_cmbSystrayBgTint->setCurrentItem(useCustomSystrayBgTint ? 1 : 0);
    m_systrayBgTintColor = TQColor(settings.readEntry("/taskmgr/systrayBgTintColor", "#808080"));
    updateColorButton(m_btnSystrayBgTintColor,
        useCustomSystrayBgTint ? m_systrayBgTintColor : paletteBackgroundColor());
    m_btnSystrayBgTintColor->setEnabled(useCustomSystrayBgTint);

    m_cmbEditor->setCurrentItem(editor_manager.default_index);
    m_cmbBrowser->setCurrentItem(browser_manager.default_index);
    m_cmbTerminal->setCurrentItem(terminal_manager.default_index);
}

PreferencesDialog::~PreferencesDialog()
{
}

void PreferencesDialog::onFgModeChanged(int index)
{
    m_btnFgColor->setEnabled(index == 1);
}

void PreferencesDialog::onBgModeChanged(int index)
{
    m_btnBgColor->setEnabled(index == 1);
}

void PreferencesDialog::onFgColorClicked()
{
    TQColor color = TQColorDialog::getColor(m_fgColor, this);
    if (color.isValid()) {
        m_fgColor = color;
        updateColorButton(m_btnFgColor, m_fgColor);
    }
}

void PreferencesDialog::onBgColorClicked()
{
    TQColor color = TQColorDialog::getColor(m_bgColor, this);
    if (color.isValid()) {
        m_bgColor = color;
        updateColorButton(m_btnBgColor, m_bgColor);
    }
}

void PreferencesDialog::onSystrayCpuModeChanged(int index)
{
    const bool custom = (index == 1);
    m_btnSystrayCpuColor->setEnabled(custom);
    updateColorButton(m_btnSystrayCpuColor, custom ? m_systrayCpuColor : TQColor("#56BFFE"));
}

void PreferencesDialog::onSystrayCpuColorClicked()
{
    TQColor color = TQColorDialog::getColor(m_systrayCpuColor, this);
    if (color.isValid()) {
        m_systrayCpuColor = color;
        updateColorButton(m_btnSystrayCpuColor, m_systrayCpuColor);
    }
}

void PreferencesDialog::onSystrayBgTintModeChanged(int index)
{
    const bool custom = (index == 1);
    m_btnSystrayBgTintColor->setEnabled(custom);
    updateColorButton(m_btnSystrayBgTintColor,
        custom ? m_systrayBgTintColor : paletteBackgroundColor());
}

void PreferencesDialog::onSystrayBgTintColorClicked()
{
    TQColor color = TQColorDialog::getColor(m_systrayBgTintColor, this);
    if (color.isValid()) {
        m_systrayBgTintColor = color;
        updateColorButton(m_btnSystrayBgTintColor, m_systrayBgTintColor);
    }
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
    save_config();

    // Save custom colors
    bool useCustomFg = (m_cmbFg->currentItem() == 1);
    bool useCustomBg = (m_cmbBg->currentItem() == 1);
    bool useCustomSystrayCpu = (m_cmbSystrayCpu->currentItem() == 1);
    bool useCustomSystrayBgTint = (m_cmbSystrayBgTint->currentItem() == 1);
    {
        TQSettings settings;
        settings.writeEntry("/taskmgr/useCustomFg", useCustomFg);
        settings.writeEntry("/taskmgr/useCustomBg", useCustomBg);
        settings.writeEntry("/taskmgr/foregroundColor", m_fgColor.name());
        settings.writeEntry("/taskmgr/backgroundColor", m_bgColor.name());
        settings.writeEntry("/taskmgr/systrayCustomCpuColor", useCustomSystrayCpu);
        settings.writeEntry("/taskmgr/systrayCpuColor", m_systrayCpuColor.name());
        settings.writeEntry("/taskmgr/systrayCustomBgTint", useCustomSystrayBgTint);
        settings.writeEntry("/taskmgr/systrayBgTintColor", m_systrayBgTintColor.name());
        settings.writeEntry("/taskmgr/displayIndividualFrequency", m_chkIndividualFreq->isChecked());
    }

    freeCpuTrayIconCache();

    // Apply palette immediately from dialog's values (avoiding TQSettings caching delays)
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
