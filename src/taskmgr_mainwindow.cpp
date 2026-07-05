/*
 * taskmgr_mainwindow.cpp — Main Window for TQt3 taskmgr port.
 *
 * Implements the multi-tab interface mimicking Windows 10 Task Manager.
 */

#include "taskmgr_mainwindow.h"
#include "preferences_dialog.h"
#include "run_new_task_dialog.h"
#include "about_dialog.h"
#include "performance_tab.h"
#include "cpu_tray_icon.h"
#include "taskmgr_system_tray.h"
#include "app_icons.h"
#include "root_password_dialog.h"
#include "root_credential_vault.h"
#include "tde_icon_loader.h"

#include <ntqapplication.h>
#include <ntqmessagebox.h>
#include <ntqmenubar.h>
#include <ntqpopupmenu.h>
#include <ntqlayout.h>
#include <ntqtabwidget.h>
#include <ntqobjectlist.h>
#include <ntqtooltip.h>
#include <twin.h>
#include <netwm_def.h>

#include <unistd.h>
#include <sys/types.h>

TaskMgrMainWindow::TaskMgrMainWindow(TQWidget* parent, const char* name)
    : TQMainWindow(parent, name),
      m_tray(0),
      m_trayMenu(0),
      m_trayKeepAboveId(-1),
      m_fileMenu(0),
      m_rootModeMenuId(-1),
      m_tabBar(0)
{
    setCaption("Task Manager");
    resize(960, 710);
    setMinimumSize(775, 470);

    /* Central widget and layout */
    TQWidget* central = new TQWidget(this);
    setCentralWidget(central);

    TQVBoxLayout* mainLayout = new TQVBoxLayout(central, 4, 4);

    /* Tab widget */
    m_tabWidget = new TQTabWidget(central);
    mainLayout->addWidget(m_tabWidget, 1);

    // Remove dotted focus rectangle around tabs when clicked
    m_tabWidget->setFocusPolicy(TQWidget::NoFocus);
    const TQObjectList* tabWidgetChildren = m_tabWidget->children();
    if (tabWidgetChildren) {
        TQObjectListIt it(*tabWidgetChildren);
        TQObject* obj;
        while ((obj = it.current())) {
            ++it;
            if (obj->inherits("TQTabBar")) {
                ((TQWidget*)obj)->setFocusPolicy(TQWidget::NoFocus);
            }
        }
    }

    const TQObjectList* tabChildren = m_tabWidget->children();
    if (tabChildren) {
        TQObjectListIt tabIt(*tabChildren);
        TQObject* tabObj;
        while ((tabObj = tabIt.current())) {
            ++tabIt;
            if (tabObj->inherits("TQTabBar"))
                m_tabBar = (TQWidget*)tabObj;
        }
    }

    /* ---- Processes Tab ---- */
    m_processesTab = new TQWidget(m_tabWidget);
    m_processesLayout = new TQVBoxLayout(m_processesTab, 4, 0);
    m_processesGauges = new SystemInfoGauges(m_processesTab);
    m_processesLayout->addWidget(m_processesGauges);
    m_processesTabContent = new ProcessesTab(m_processesTab);
    m_processesHeaderStats = new HeaderExtensionStats(m_processesTabContent->treeView(), m_processesTab);
    m_processesLayout->addWidget(m_processesHeaderStats);
    m_processesLayout->addWidget(m_processesTabContent, 1);

    connect(m_processesTabContent, SIGNAL(detailsToggleRequested()),
            this, SLOT(onProcessesDetailsToggle()));

    /* ---- Performance Tab (No gauges) ---- */
    m_performanceTab = new PerformanceTab(m_tabWidget);
    connect(m_performanceTab, SIGNAL(summaryViewChanged()),
            this, SLOT(onSummaryViewChanged()));

    /* ---- Startup Tab ---- */
    m_startupTab = new TQWidget(m_tabWidget);
    m_startupLayout = new TQVBoxLayout(m_startupTab, 4, 0);
    m_startupGauges = new SystemInfoGauges(m_startupTab);
    m_startupLayout->addWidget(m_startupGauges);
    m_startupTabContent = new StartupTab(m_startupTab);
    m_startupLayout->addWidget(m_startupTabContent, 1);

    /* ---- Users Tab ---- */
    m_usersTab = new TQWidget(m_tabWidget);
    m_usersLayout = new TQVBoxLayout(m_usersTab, 4, 0);
    m_usersGauges = new SystemInfoGauges(m_usersTab);
    m_usersLayout->addWidget(m_usersGauges);
    m_usersTabContent = new UsersTab(m_usersTab);
    m_usersLayout->addWidget(m_usersTabContent, 1);

    /* ---- Services Tab ---- */
    m_servicesTab = new TQWidget(m_tabWidget);
    m_servicesLayout = new TQVBoxLayout(m_servicesTab, 4, 0);
    m_servicesGauges = new SystemInfoGauges(m_servicesTab);
    m_servicesLayout->addWidget(m_servicesGauges);
    m_servicesTabContent = new ServicesTab(m_servicesTab);
    m_servicesLayout->addWidget(m_servicesTabContent, 1);

    m_tabWidget->addTab(m_processesTab,   "  Processes  ");
    m_tabWidget->addTab(m_performanceTab, "  Performance  ");
    m_tabWidget->addTab(m_startupTab,     "  Startup  ");
    m_tabWidget->addTab(m_usersTab,       "  Users  ");
    m_tabWidget->addTab(m_servicesTab,    "  Services  ");

    createMenuBar();

    /* Refresh timer using backend interval */
    m_refreshTimer = new TQTimer(this);
    connect(m_refreshTimer, SIGNAL(timeout()), this, SLOT(onRefreshTimeout()));
    int interval = bridge_get_refresh_interval();
    if (interval < 3600000) {
        m_refreshTimer->start(interval);
    }

    connect(m_tabWidget, SIGNAL(currentChanged(TQWidget*)),
            this,        SLOT(onTabChanged(TQWidget*)));

    setIcon(appNormalWindowIcon());
}

void TaskMgrMainWindow::runInitialRefresh()
{
    updateSystemMetrics();
    updateGaugesVisibility();

    TdeIconLoader::setStartupDeferral(true);
    m_processesTabContent->refreshLight();
    TdeIconLoader::setStartupDeferral(false);

    if (tqApp)
        tqApp->processEvents();

    if (!m_processesTabContent->isCompactMode())
        m_processesTabContent->refreshIconsOnly();

    setupSystemTray();
}

TaskMgrMainWindow::~TaskMgrMainWindow()
{
    root_mode_deactivate();
    freeCpuTrayIconCache();
    save_config();
    delete m_tray;
}

void TaskMgrMainWindow::createMenuBar()
{
    menuBar()->setFrameStyle(TQFrame::NoFrame);

    /* ---- File menu ---- */
    m_fileMenu = new TQPopupMenu(this);
    m_fileMenu->insertItem("&Run new task...", this, SLOT(onMenuRunNewTask()));
    m_fileMenu->insertSeparator();

    if (getuid() != 0) {
        m_rootModeMenuId = m_fileMenu->insertItem("Root &mode", this, SLOT(onMenuRootMode()));
    } else {
        m_rootModeMenuId = -1;
    }
    m_fileMenu->insertItem("&Settings", this, SLOT(onMenuSettings()));
    m_fileMenu->insertItem("&Quit", this, SLOT(onMenuQuit()), TQKeySequence("Ctrl+W"));

    /* ---- View menu ---- */
    m_viewMenu = new TQPopupMenu(this);
    guint16 flags = bridge_get_app_flags();

    // 1. Refresh at the top, renamed to "Refresh"
    m_viewRefreshId = m_viewMenu->insertItem("&Refresh", this, SLOT(onMenuRefreshNow()), TQKeySequence("F5"));

    // 2. Update Speed sub-menu
    m_speedMenu = new TQPopupMenu(this);
    m_speedHighId    = m_speedMenu->insertItem("High (0.5s)", this, SLOT(onSpeedHigh()));
    m_speedNormalId  = m_speedMenu->insertItem("Normal (1.0s)", this, SLOT(onSpeedNormal()));
    m_speedLowId     = m_speedMenu->insertItem("Low (4.0s)", this, SLOT(onSpeedLow()));
    m_speedPausedId  = m_speedMenu->insertItem("Paused", this, SLOT(onSpeedPaused()));

    // Set initial checked state based on current refresh interval
    int currentInterval = bridge_get_refresh_interval();
    if (currentInterval <= 500) {
        m_speedMenu->setItemChecked(m_speedHighId, true);
    } else if (currentInterval <= 1000) {
        m_speedMenu->setItemChecked(m_speedNormalId, true);
    } else if (currentInterval <= 4000) {
        m_speedMenu->setItemChecked(m_speedLowId, true);
    } else {
        m_speedMenu->setItemChecked(m_speedPausedId, true);
    }

    m_viewSpeedItemId = m_viewMenu->insertItem("Update Speed", m_speedMenu);
    m_viewSeparatorAfterSpeedId = m_viewMenu->insertSeparator();

    // 3. User tasks, etc.
    m_viewUserTasksId    = m_viewMenu->insertItem("User tasks",
                            this, SLOT(onToggleUserTasks()));
    m_viewMenu->setItemChecked(m_viewUserTasksId,
                               (flags & APP_FLAG_SHOW_USER_TASKS) != 0);

    m_viewRootTasksId    = m_viewMenu->insertItem("Root tasks",
                            this, SLOT(onToggleRootTasks()));
    m_viewMenu->setItemChecked(m_viewRootTasksId,
                               (flags & APP_FLAG_SHOW_ROOT_TASKS) != 0);

    m_viewOtherTasksId   = m_viewMenu->insertItem("Other tasks",
                            this, SLOT(onToggleOtherTasks()));
    m_viewMenu->setItemChecked(m_viewOtherTasksId,
                               (flags & APP_FLAG_SHOW_OTHER_TASKS) != 0);

    m_viewSeparatorBeforeFullCmdId = m_viewMenu->insertSeparator();

    m_viewFullCmdLineId  = m_viewMenu->insertItem("Full command line",
                            this, SLOT(onToggleFullCmdLine()));
    m_viewMenu->setItemChecked(m_viewFullCmdLineId,
                               (flags & APP_FLAG_SHOW_FULL_PATH) != 0);

    m_viewCachedAsFreeId = m_viewMenu->insertItem("Show cached memory as free",
                            this, SLOT(onToggleCachedAsFree()));
    m_viewMenu->setItemChecked(m_viewCachedAsFreeId,
                               (flags & APP_FLAG_SHOW_CACHED_FREE) != 0);

    m_viewGroupProcsId   = m_viewMenu->insertItem("Group processes",
                            this, SLOT(onToggleGroupProcs()));
    m_viewMenu->setItemChecked(m_viewGroupProcsId,
                               (flags & APP_FLAG_GROUP_PROCS) != 0);

    /* ---- Help menu ---- */
    TQPopupMenu* helpMenu = new TQPopupMenu(this);
    helpMenu->insertItem("&About Task Manager", this, SLOT(onMenuAbout()));

    /* ---- Menu bar ---- */
    menuBar()->insertItem("&File",  m_fileMenu);
    menuBar()->insertItem("&View",  m_viewMenu);
    menuBar()->insertItem("&Help",  helpMenu);
}

void TaskMgrMainWindow::updateViewMenuForCompactMode(bool compact)
{
    if (!m_viewMenu)
        return;

    m_viewMenu->setItemVisible(m_viewSeparatorAfterSpeedId, !compact);
    m_viewMenu->setItemVisible(m_viewUserTasksId, !compact);
    m_viewMenu->setItemVisible(m_viewRootTasksId, !compact);
    m_viewMenu->setItemVisible(m_viewOtherTasksId, !compact);
    m_viewMenu->setItemVisible(m_viewSeparatorBeforeFullCmdId, !compact);
    m_viewMenu->setItemVisible(m_viewFullCmdLineId, !compact);
    m_viewMenu->setItemVisible(m_viewCachedAsFreeId, !compact);
    m_viewMenu->setItemVisible(m_viewGroupProcsId, !compact);
}

void TaskMgrMainWindow::updateGaugesVisibility()
{
    if (m_processesTabContent && m_processesTabContent->isCompactMode()) {
        m_processesGauges->hide();
        m_processesHeaderStats->hide();
        return;
    }

    bool showGauges = (bridge_get_app_flags() & APP_FLAG_SHOW_HEADER_GAUGES) != 0;
    if (showGauges) {
        m_processesGauges->show();
        m_processesHeaderStats->hide();
        m_startupGauges->show();
        m_usersGauges->show();
        m_servicesGauges->show();
    } else {
        m_processesGauges->hide();
        m_processesHeaderStats->show();
        m_startupGauges->hide();
        m_usersGauges->hide();
        m_servicesGauges->hide();
    }
}

void TaskMgrMainWindow::updateSystemMetrics()
{
    if (!get_system_status(&m_systemStatus))
        return;

    double cpu_usage = get_cpu_usage(&m_systemStatus);
    double cpu_pct = cpu_usage * 100.0;
    if (cpu_pct < 0) cpu_pct = 0;
    if (cpu_pct > 100) cpu_pct = 100;

    guint64 memory_used = m_systemStatus.mem_total - m_systemStatus.mem_free;
    if (bridge_get_app_flags() & APP_FLAG_SHOW_CACHED_FREE) {
        memory_used -= (m_systemStatus.mem_cached + m_systemStatus.mem_buffered);
    }

    double cpu_ghz = get_cpu_speed();
    double mem_total_gb = (double)m_systemStatus.mem_total / (1024.0 * 1024.0);

    guint64 swap_total = 0, swap_free = 0;
    get_swap_info(&swap_total, &swap_free);
    guint64 swap_used = swap_total - swap_free;
    double swap_total_gb = (double)swap_total / (1024.0 * 1024.0);

    double ram_usage = (double)memory_used / (double)m_systemStatus.mem_total;
    double swap_usage = 0.0;
    if (swap_total > 0)
        swap_usage = (double)swap_used / (double)swap_total;
    bridge_update_performance_samples(cpu_usage, ram_usage, swap_usage);

    if (!m_processesTabContent->isCompactMode()) {
        m_processesGauges->updateValues(cpu_pct, cpu_ghz, (double)memory_used, mem_total_gb,
                                        (double)swap_used, swap_total_gb);

        double gpu_pct = 0.0;
        performance_data_bridge* perf = bridge_get_performance_data();
        if (perf && perf->gpu_total_samples) {
            int last_idx = perf->current_index - 1;
            if (last_idx < 0) last_idx = 119;
            gpu_pct = perf->gpu_total_samples[last_idx];
        }
        m_processesHeaderStats->updateValues(cpu_pct, ram_usage * 100.0, gpu_pct);
    }

    m_startupGauges->updateValues(cpu_pct, cpu_ghz, (double)memory_used, mem_total_gb,
                                  (double)swap_used, swap_total_gb);
    m_usersGauges->updateValues(cpu_pct, cpu_ghz, (double)memory_used, mem_total_gb,
                                (double)swap_used, swap_total_gb);
    m_servicesGauges->updateValues(cpu_pct, cpu_ghz, (double)memory_used, mem_total_gb,
                                   (double)swap_used, swap_total_gb);

    updateSystemTray(cpu_pct);
}

void TaskMgrMainWindow::onRefreshTimeout()
{
    updateSystemMetrics();

    /* Refresh the active tab content */
    if (m_tabWidget->currentPage() == m_processesTab) {
        m_processesTabContent->refresh();
    } else if (m_tabWidget->currentPage() == m_performanceTab) {
        m_performanceTab->refresh();
    } else if (m_tabWidget->currentPage() == m_usersTab) {
        m_usersTabContent->refresh();
    }

    updateGaugesVisibility();
}

void TaskMgrMainWindow::onTabChanged(TQWidget* widget)
{
    if (widget == m_processesTab) {
        m_processesTabContent->refresh();
    } else if (widget == m_performanceTab) {
        m_performanceTab->refresh();
        m_performanceTab->setFocus();
    } else if (widget == m_startupTab) {
        m_startupTabContent->refresh();
    } else if (widget == m_usersTab) {
        m_usersTabContent->refresh();
    } else if (widget == m_servicesTab) {
        m_servicesTabContent->refresh();
    }
}

void TaskMgrMainWindow::onMenuQuit()
{
    close();
}

void TaskMgrMainWindow::onMenuRunNewTask()
{
    RunNewTaskDialog dlg(this);
    dlg.exec();
}

void TaskMgrMainWindow::applyRootModeUi()
{
    if (root_mode_is_active()) {
        setIcon(appRootWindowIcon());
        setCaption("Task Manager - root");
        if (m_fileMenu && m_rootModeMenuId >= 0)
            m_fileMenu->changeItem(m_rootModeMenuId, "Exit root &mode");
    } else {
        setIcon(appNormalWindowIcon());
        setCaption("Task Manager");
        if (m_fileMenu && m_rootModeMenuId >= 0)
            m_fileMenu->changeItem(m_rootModeMenuId, "Root &mode");
    }
}

void TaskMgrMainWindow::onMenuRootMode()
{
    if (root_mode_is_active()) {
        root_mode_deactivate();
        applyRootModeUi();
        return;
    }

    RootPasswordDialog dlg(this);
    if (dlg.exec() == TQDialog::Accepted)
        applyRootModeUi();
}

void TaskMgrMainWindow::onMenuSettings()
{
    PreferencesDialog dlg(this);
    if (dlg.exec() == TQDialog::Accepted) {
        setPalette(TQApplication::palette());
        updateGaugesVisibility();
        if (m_processesTabContent) {
            m_processesTabContent->refresh();
        }
        if (m_performanceTab) {
            m_performanceTab->refresh();
        }
        if (m_startupTabContent) {
            m_startupTabContent->refresh();
        }
        if (m_usersTabContent) {
            m_usersTabContent->refresh();
        }
        if (m_servicesTabContent) {
            m_servicesTabContent->refresh();
        }
        onRefreshTimeout();
    }
}

void TaskMgrMainWindow::onMenuAbout()
{
    showAboutDialog(this);
}

void TaskMgrMainWindow::onSpeedHigh()
{
    m_speedMenu->setItemChecked(m_speedHighId, true);
    m_speedMenu->setItemChecked(m_speedNormalId, false);
    m_speedMenu->setItemChecked(m_speedLowId, false);
    m_speedMenu->setItemChecked(m_speedPausedId, false);

    bridge_set_refresh_interval(500);
    m_refreshTimer->start(500);
}

void TaskMgrMainWindow::onSpeedNormal()
{
    m_speedMenu->setItemChecked(m_speedHighId, false);
    m_speedMenu->setItemChecked(m_speedNormalId, true);
    m_speedMenu->setItemChecked(m_speedLowId, false);
    m_speedMenu->setItemChecked(m_speedPausedId, false);

    bridge_set_refresh_interval(1000);
    m_refreshTimer->start(1000);
}

void TaskMgrMainWindow::onSpeedLow()
{
    m_speedMenu->setItemChecked(m_speedHighId, false);
    m_speedMenu->setItemChecked(m_speedNormalId, false);
    m_speedMenu->setItemChecked(m_speedLowId, true);
    m_speedMenu->setItemChecked(m_speedPausedId, false);

    bridge_set_refresh_interval(4000);
    m_refreshTimer->start(4000);
}

void TaskMgrMainWindow::onSpeedPaused()
{
    m_speedMenu->setItemChecked(m_speedHighId, false);
    m_speedMenu->setItemChecked(m_speedNormalId, false);
    m_speedMenu->setItemChecked(m_speedLowId, false);
    m_speedMenu->setItemChecked(m_speedPausedId, true);

    bridge_set_refresh_interval(3600000);
    m_refreshTimer->stop();
}

void TaskMgrMainWindow::onMenuRefreshNow()
{
    onRefreshTimeout();
}

static void toggleAppFlag(TQPopupMenu* menu, int itemId, guint16 flag)
{
    guint16 flags = bridge_get_app_flags();
    bool nowOn = !(flags & flag);
    if (nowOn)
        flags |= flag;
    else
        flags &= ~flag;
    bridge_set_app_flags(flags);
    menu->setItemChecked(itemId, nowOn);
}

void TaskMgrMainWindow::onToggleUserTasks()
{
    toggleAppFlag(m_viewMenu, m_viewUserTasksId, APP_FLAG_SHOW_USER_TASKS);
    onRefreshTimeout();
}

void TaskMgrMainWindow::onToggleRootTasks()
{
    toggleAppFlag(m_viewMenu, m_viewRootTasksId, APP_FLAG_SHOW_ROOT_TASKS);
    onRefreshTimeout();
}

void TaskMgrMainWindow::onToggleOtherTasks()
{
    toggleAppFlag(m_viewMenu, m_viewOtherTasksId, APP_FLAG_SHOW_OTHER_TASKS);
    onRefreshTimeout();
}

void TaskMgrMainWindow::onToggleFullCmdLine()
{
    toggleAppFlag(m_viewMenu, m_viewFullCmdLineId, APP_FLAG_SHOW_FULL_PATH);
    onRefreshTimeout();
}

void TaskMgrMainWindow::onToggleCachedAsFree()
{
    toggleAppFlag(m_viewMenu, m_viewCachedAsFreeId, APP_FLAG_SHOW_CACHED_FREE);
    onRefreshTimeout();
}

void TaskMgrMainWindow::onToggleGroupProcs()
{
    toggleAppFlag(m_viewMenu, m_viewGroupProcsId, APP_FLAG_GROUP_PROCS);
    onRefreshTimeout();
}

void TaskMgrMainWindow::onProcessesDetailsToggle()
{
    if (m_processesTabContent->isCompactMode())
        exitProcessesCompactMode();
    else
        enterProcessesCompactMode();
}

void TaskMgrMainWindow::enterProcessesCompactMode()
{
    m_savedWindowSize = size();
    m_savedMinimumSize = minimumSize();

    guint16 flags = bridge_get_app_flags();
    bridge_set_app_flags(flags & ~APP_FLAG_FULL_VIEW);

    if (m_tabBar)
        m_tabBar->hide();

    m_processesGauges->hide();
    m_processesHeaderStats->hide();

    setMinimumSize(CompactWidth, CompactHeight);
    resize(CompactWidth, CompactHeight);

    m_tabWidget->setCurrentPage(0);
    m_processesTabContent->setCompactMode(true);
    updateViewMenuForCompactMode(true);
}

void TaskMgrMainWindow::exitProcessesCompactMode()
{
    m_processesTabContent->setCompactMode(false);
    updateViewMenuForCompactMode(false);

    guint16 flags = bridge_get_app_flags();
    bridge_set_app_flags(flags | APP_FLAG_FULL_VIEW);

    if (m_tabBar)
        m_tabBar->show();

    updateGaugesVisibility();

    if (m_savedMinimumSize.width() > 0 && m_savedMinimumSize.height() > 0)
        setMinimumSize(m_savedMinimumSize);
    else
        setMinimumSize(775, 470);

    if (m_savedWindowSize.width() > 0 && m_savedWindowSize.height() > 0)
        resize(m_savedWindowSize);
}

void TaskMgrMainWindow::setupSystemTray()
{
    m_tray = new TaskMgrSystemTray(this);
    m_trayMenu = m_tray->customMenu();

    connect(m_trayMenu, SIGNAL(aboutToShow()), this, SLOT(updateSystemTrayMenu()));
    updateSystemTrayMenu();

    const TQPixmap* icon = cpuTrayCachedPixmapForLevel(0);
    if (icon && !icon->isNull())
        m_tray->setCpuIconLevel(0, *icon);

    m_tray->show();
}

void TaskMgrMainWindow::updateSystemTrayMenu()
{
    if (!m_trayMenu) return;

    m_trayMenu->clear();
    m_trayKeepAboveId = m_trayMenu->insertItem("Keep above", this, SLOT(onTrayKeepAboveToggled()));
    m_trayMenu->setItemChecked(m_trayKeepAboveId,
        (bridge_get_app_flags() & APP_FLAG_KEEP_ABOVE) != 0);
    m_trayMenu->insertSeparator();
    m_trayMenu->insertItem("Quit", this, SLOT(onMenuQuit()));
}

void TaskMgrMainWindow::onTrayKeepAboveToggled()
{
    toggleAppFlag(m_trayMenu, m_trayKeepAboveId, APP_FLAG_KEEP_ABOVE);
    applyKeepAbove((bridge_get_app_flags() & APP_FLAG_KEEP_ABOVE) != 0);
    save_config();
}

static void setWindowKeepAbove(WId wid, bool keepAbove)
{
    if (!wid)
        return;

    if (keepAbove)
        KWin::setState(wid, NET::KeepAbove);
    else
        KWin::clearState(wid, NET::KeepAbove);
}

void TaskMgrMainWindow::applyKeepAbove(bool keepAbove)
{
    if (m_performanceTab && m_performanceTab->isSummaryViewActive()) {
        setWindowKeepAbove(m_performanceTab->summaryViewWinId(), keepAbove);
        setWindowKeepAbove(winId(), false);
        return;
    }

    setWindowKeepAbove(winId(), keepAbove);
}

void TaskMgrMainWindow::onSummaryViewChanged()
{
    applyKeepAbove((bridge_get_app_flags() & APP_FLAG_KEEP_ABOVE) != 0);
}

void TaskMgrMainWindow::showEvent(TQShowEvent* e)
{
    TQMainWindow::showEvent(e);
    if (bridge_get_app_flags() & APP_FLAG_KEEP_ABOVE)
        applyKeepAbove(true);
}

bool TaskMgrMainWindow::event(TQEvent* e)
{
    if (e->type() == TQEvent::WindowStateChange &&
        (bridge_get_app_flags() & APP_FLAG_MINIMIZE_TO_TRAY) &&
        isMinimized()) {
        saved_win_x = x();
        saved_win_y = y();
        showNormal();
        hide();
        guint16 flags = bridge_get_app_flags();
        bridge_set_app_flags(flags | APP_FLAG_WINDOW_HIDDEN);
        return true;
    }
    return TQMainWindow::event(e);
}

void TaskMgrMainWindow::toggleFromTray()
{
    guint16 flags = bridge_get_app_flags();

    if (flags & APP_FLAG_MINIMIZE_TO_TRAY) {
        if (flags & APP_FLAG_WINDOW_HIDDEN) {
            bridge_set_app_flags(flags & ~APP_FLAG_WINDOW_HIDDEN);
            if (saved_win_x >= 0 && saved_win_y >= 0)
                move(saved_win_x, saved_win_y);
            show();
            raise();
            setActiveWindow();
        } else {
            saved_win_x = x();
            saved_win_y = y();
            hide();
            bridge_set_app_flags(flags | APP_FLAG_WINDOW_HIDDEN);
        }
        return;
    }

    if (isMinimized() || !isShown()) {
        showNormal();
        raise();
        setActiveWindow();
    } else if (isVisible()) {
        showMinimized();
    } else {
        show();
        raise();
        setActiveWindow();
    }
}

void TaskMgrMainWindow::updateSystemTray(double cpuPercent)
{
    if (!m_tray) return;

    int cpuLevel = cpuTrayLevelForPercent((int)cpuPercent);
    const TQPixmap* icon = cpuTrayCachedPixmapForLevel(cpuLevel);
    if (icon && !icon->isNull())
        m_tray->setCpuIconLevel(cpuLevel, *icon);

    performance_data_bridge* perf = bridge_get_performance_data();
    if (!perf) return;

    int cpuSample = 0;
    if (perf->current_index > 0)
        cpuSample = perf->cpu_samples[perf->current_index - 1];
    else if (perf->perf_flags & PERF_DATA_BUFFER_FULL_B)
        cpuSample = perf->cpu_samples[PERFORMANCE_SAMPLES_COUNT - 1];
    if (cpuSample > 100) cpuSample = 100;

    int ramPercent = 0;
    if (perf->current_index > 0)
        ramPercent = perf->ram_samples[perf->current_index - 1];
    else if (perf->perf_flags & PERF_DATA_BUFFER_FULL_B)
        ramPercent = perf->ram_samples[PERFORMANCE_SAMPLES_COUNT - 1];
    if (ramPercent > 100) ramPercent = 100;

    int diskPercent = 0;
    for (int i = 0; i < disk_manager.disk_count; ++i) {
        disk_info_t* disk = &disk_manager.disks[i];
        if (disk_manager.current_index > 0)
            diskPercent += disk->activity_samples[disk_manager.current_index - 1];
        else if (disk_manager.flags & MANAGER_BUFFER_FULL_B)
            diskPercent += disk->activity_samples[PERFORMANCE_SAMPLES_COUNT - 1];
    }
    if (diskPercent > 100) diskPercent = 100;

    int networkRxTotal = 0;
    int networkTxTotal = 0;
    for (int i = 0; i < network_manager.interface_count; ++i) {
        network_info_t* iface = &network_manager.interfaces[i];
        int rxKbs = 0;
        int txKbs = 0;
        if (network_manager.current_index > 0) {
            rxKbs = iface->rx_samples[network_manager.current_index - 1];
            txKbs = iface->tx_samples[network_manager.current_index - 1];
        } else if (network_manager.flags & MANAGER_BUFFER_FULL_B) {
            rxKbs = iface->rx_samples[PERFORMANCE_SAMPLES_COUNT - 1];
            txKbs = iface->tx_samples[PERFORMANCE_SAMPLES_COUNT - 1];
        }
        networkRxTotal += rxKbs;
        networkTxTotal += txKbs;
    }

    m_tray->setTooltipStats(cpuSample, ramPercent, diskPercent,
                            networkRxTotal + networkTxTotal);
}

#include "taskmgr_mainwindow.moc"
