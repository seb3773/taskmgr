/*
 * taskmgr_mainwindow.h — Main Window for TQt3 taskmgr port.
 *
 * Implements the multi-tab interface mimicking Windows 10 Task Manager.
 */

#ifndef TASKMGR_MAINWINDOW_H
#define TASKMGR_MAINWINDOW_H

#include <ntqmainwindow.h>
#include <ntqevent.h>
#include <ntqtabwidget.h>
#include <ntqlabel.h>
#include <ntqwidget.h>
#include <ntqtimer.h>
#include <ntqaction.h>
#include <ntqmenubar.h>
#include <ntqpopupmenu.h>
#include <ntqpainter.h>
#include <ntqcolor.h>
#include <ntqlayout.h>
#include <ntqheader.h>
#include <ntqscrollbar.h>
#include <ntqapplication.h>
#include "mvc/tqtmvctreeview.h"
#include "backend_bridge.h"
#include "processes_tab.h"
#include "startup_tab.h"
#include "services_tab.h"
#include "users_tab.h"
#include "taskmgr_system_tray.h"

extern "C" {
extern gint saved_win_x;
extern gint saved_win_y;
}

/* ====================================================================
 * CustomProgressBar — Flat, border, background, solid progress chunk,
 * and overlay text centered. Looks exactly like GTK3/Windows 10 style.
 * ==================================================================== */
class CustomProgressBar : public TQWidget {
    TQ_OBJECT
public:
    CustomProgressBar(TQWidget* parent = 0, const char* name = 0)
        : TQWidget(parent, name), m_progress(0.0), m_color(166, 210, 255) {}

    void setProgress(double fraction) {
        if (fraction < 0.0) fraction = 0.0;
        if (fraction > 1.0) fraction = 1.0;
        if (m_progress != fraction) {
            m_progress = fraction;
            update();
        }
    }

    void setText(const TQString& text) {
        if (m_text != text) {
            m_text = text;
            update();
        }
    }

    void setColor(const TQColor& color) {
        if (m_color != color) {
            m_color = color;
            update();
        }
    }

protected:
    void paintEvent(TQPaintEvent*) {
        TQPainter p(this);
        
        // Background & border
        p.setPen(TQColor(218, 220, 224)); // Light grey border
        p.setBrush(TQt::white);
        p.drawRect(0, 0, width(), height());

        // Progress chunk
        int fillWidth = (int)(m_progress * (width() - 2));
        if (fillWidth > 0) {
            p.setPen(TQt::NoPen);
            p.setBrush(m_color);
            p.drawRect(1, 1, fillWidth, height() - 2);
        }

        // Text centered
        p.setPen(TQColor(0, 0, 0)); // Black text
        p.setFont(font());
        p.drawText(0, 0, width(), height(), TQt::AlignCenter, m_text);
    }

private:
    double m_progress;
    TQString m_text;
    TQColor m_color;
};

/* ====================================================================
 * SystemInfoGauges — Container for the 3 horizontal progress bars.
 * ==================================================================== */
class SystemInfoGauges : public TQWidget {
    TQ_OBJECT
public:
    SystemInfoGauges(TQWidget* parent = 0) : TQWidget(parent) {
        TQHBoxLayout* layout = new TQHBoxLayout(this, 0, 8); // 8px spacing
        
        m_cpuBar = new CustomProgressBar(this);
        m_cpuBar->setColor(TQColor(166, 210, 255)); // Matching blue
        
        m_ramBar = new CustomProgressBar(this);
        m_ramBar->setColor(TQColor(166, 210, 255));
        
        m_swapBar = new CustomProgressBar(this);
        m_swapBar->setColor(TQColor(166, 210, 255));
        
        layout->addWidget(m_cpuBar, 1);
        layout->addWidget(m_ramBar, 1);
        layout->addWidget(m_swapBar, 1);
        
        setFixedHeight(26);
    }
    
    void updateValues(double cpu_pct, double cpu_ghz,
                      double mem_used_kb, double mem_total_gb,
                      double swap_used_kb, double swap_total_gb) {
        // CPU
        TQString cpuText = TQString("CPU usage: %1 % at %2 GHz")
            .arg(cpu_pct, 0, 'f', 0)
            .arg(cpu_ghz, 0, 'f', 2);
        m_cpuBar->setProgress(cpu_pct / 100.0);
        m_cpuBar->setText(cpuText);
        
        // RAM
        TQString ramText = TQString("Memory: %1 GB of %2 GB used")
            .arg(mem_used_kb / (1024.0 * 1024.0), 0, 'f', 1)
            .arg(mem_total_gb, 0, 'f', 1);
        double ram_frac = mem_total_gb > 0 ? (mem_used_kb / (1024.0 * 1024.0)) / mem_total_gb : 0.0;
        m_ramBar->setProgress(ram_frac);
        m_ramBar->setText(ramText);
        
        // Swap
        TQString swapText;
        if (swap_total_gb > 0) {
            swapText = TQString("Swap: %1 GB of %2 GB used")
                .arg(swap_used_kb / (1024.0 * 1024.0), 0, 'f', 1)
                .arg(swap_total_gb, 0, 'f', 1);
            m_swapBar->setProgress((swap_used_kb / (1024.0 * 1024.0)) / swap_total_gb);
        } else {
            swapText = "Swap: 0,0 GB of 0,0 GB used";
            m_swapBar->setProgress(0.0);
        }
        m_swapBar->setText(swapText);
    }

private:
    CustomProgressBar* m_cpuBar;
    CustomProgressBar* m_ramBar;
    CustomProgressBar* m_swapBar;
};

/* ====================================================================
 * HeaderExtensionStats — Minimal extension above column headers.
 * ==================================================================== */
class HeaderExtensionStats : public TQWidget {
    TQ_OBJECT
public:
    HeaderExtensionStats(TQtMvcTreeView* treeView, TQWidget* parent = 0)
        : TQWidget(parent), m_treeView(treeView), m_cpuPct(0.0), m_memPct(0.0), m_gpuPct(0.0)
    {
        setFixedHeight(26); // 22px content + 4px overlap bridge to real header
        if (m_treeView) {
            connect(m_treeView->horizontalHeader(), SIGNAL(sizeChange(int,int,int)), this, SLOT(update()));
            if (m_treeView->horizontalScrollBar()) {
                connect(m_treeView->horizontalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(update()));
            }
        }
    }

    void updateValues(double cpu, double mem, double gpu) {
        m_cpuPct = cpu;
        m_memPct = mem;
        m_gpuPct = gpu;
        update();
    }

protected:
    void paintEvent(TQPaintEvent*) {
        TQPainter p(this);
        int h = height();
        int w = width();
        TQColorGroup cg = colorGroup();
        int bridgeH = 4;
        int contentH = h - bridgeH; // Top 22px: stats content area
        int bridgeY = contentH;     // Bottom 10px: bridge to real header

        // Top zone: background matching window bg from settings
        p.fillRect(0, 0, w, contentH, cg.background());
        // Bottom zone: bridge painted with header button color
        p.fillRect(0, bridgeY, w, bridgeH, cg.button());

        if (!m_treeView) return;

        // Account for the tree view's frame border offset (+1 empirical correction)
        int frameOfs = m_treeView->frameWidth() + 1;
        int cols = m_treeView->numCols();
        for (int col = 0; col < cols; ++col) {
            int colW = m_treeView->columnWidth(col);
            if (colW > 0) {
                int colX = m_treeView->columnPos(col) - m_treeView->contentsX() + frameOfs;

                TQString text;
                TQString label = m_treeView->horizontalHeader()->label(col);
                if (label == "CPU") {
                    text = TQString("%1%").arg((int)m_cpuPct);
                } else if (label == "Memory") {
                    text = TQString("%1%").arg((int)m_memPct);
                } else if (label == "GPU") {
                    text = TQString("%1%").arg((int)m_gpuPct);
                }

                if (!text.isEmpty()) {
                    p.save();
                    p.setPen(cg.text());
                    TQFont f = p.font();
                    f.setPointSize(f.pointSize() + 1);
                    p.setFont(f);
                    // Center text in the content zone only (top 22px)
                    p.drawText(colX, 0, colW, contentH, TQt::AlignCenter, text);
                    p.restore();
                }
            }
        }
    }

    void mousePressEvent(TQMouseEvent* e) {
        propagateMouseEvent(e);
    }

    void mouseReleaseEvent(TQMouseEvent* e) {
        propagateMouseEvent(e);
    }

    void mouseMoveEvent(TQMouseEvent* e) {
        propagateMouseEvent(e);
    }

    void mouseDoubleClickEvent(TQMouseEvent* e) {
        propagateMouseEvent(e);
    }

private:
    void propagateMouseEvent(TQMouseEvent* e) {
        if (m_treeView && m_treeView->horizontalHeader()) {
            TQPoint globalPos = mapToGlobal(e->pos());
            TQPoint headerPos = m_treeView->horizontalHeader()->mapFromGlobal(globalPos);
            // Force Y to be inside the header's bounds so QHeader accepts the event as a valid click/drag
            headerPos.setY(m_treeView->horizontalHeader()->height() / 2);
            TQMouseEvent ev(e->type(), headerPos, globalPos, e->button(), e->state());
            TQApplication::sendEvent(m_treeView->horizontalHeader(), &ev);
        }
    }

    TQtMvcTreeView* m_treeView;
    double m_cpuPct;
    double m_memPct;
    double m_gpuPct;
};

class PerformanceTab;

/* ====================================================================
 * TaskMgrMainWindow — Main Window Class
 * ==================================================================== */
class TaskMgrMainWindow : public TQMainWindow {
    TQ_OBJECT

public:
    TaskMgrMainWindow(TQWidget* parent = 0, const char* name = 0);
    virtual ~TaskMgrMainWindow();

    void toggleFromTray();
    void runInitialRefresh();

protected:
    void showEvent(TQShowEvent* e);
    bool event(TQEvent* e);

private slots:
    void onRefreshTimeout();
    void onTabChanged(TQWidget* widget);
    void onMenuQuit();
    void onMenuRunNewTask();
    void onMenuRootMode();
    void onMenuSettings();
    void onMenuAbout();
    void onMenuRefreshNow();
    // Speed slots
    void onSpeedHigh();
    void onSpeedNormal();
    void onSpeedLow();
    void onSpeedPaused();
    // View menu toggle slots
    void onToggleUserTasks();
    void onToggleRootTasks();
    void onToggleOtherTasks();
    void onToggleFullCmdLine();
    void onToggleGroupProcs();
    void onExpandAll();
    void onCollapseAll();
    void onProcessesDetailsToggle();
    void onTrayKeepAboveToggled();
    void onSummaryViewChanged();
    void updateSystemTrayMenu();

private:
    void createMenuBar();
    void applyRootModeUi();
    void updateGaugesVisibility();
    void setupSystemTray();
    void updateSystemTray(double cpuPercent);
    void updateSystemMetrics();
    void applyKeepAbove(bool keepAbove);
    void enterProcessesCompactMode();
    void exitProcessesCompactMode();
    void updateViewMenu();

    TQTabWidget* m_tabWidget;
    TQWidget* m_tabBar;

    // Tabs container widgets and layouts
    TQWidget* m_processesTab;
    TQVBoxLayout* m_processesLayout;
    SystemInfoGauges* m_processesGauges;
    HeaderExtensionStats* m_processesHeaderStats;
    ProcessesTab* m_processesTabContent;

    PerformanceTab* m_performanceTab;

    TQWidget* m_startupTab;
    TQVBoxLayout* m_startupLayout;
    SystemInfoGauges* m_startupGauges;
    StartupTab* m_startupTabContent;

    TQWidget* m_usersTab;
    TQVBoxLayout* m_usersLayout;
    SystemInfoGauges* m_usersGauges;
    UsersTab* m_usersTabContent;

    TQWidget* m_servicesTab;
    TQVBoxLayout* m_servicesLayout;
    SystemInfoGauges* m_servicesGauges;
    ServicesTab* m_servicesTabContent;

    // Refresh timer
    TQTimer* m_refreshTimer;

    // View menu checkable items (need to keep reference for state)
    int m_viewUserTasksId;
    int m_viewRootTasksId;
    int m_viewOtherTasksId;
    int m_viewFullCmdLineId;
    int m_viewGroupProcsId;
    int m_viewSeparatorExpandCollapseId;
    int m_viewExpandAllId;
    int m_viewCollapseAllId;
    int m_viewRefreshId;
    int m_viewSpeedItemId;
    int m_viewSeparatorAfterSpeedId;
    int m_viewSeparatorBeforeFullCmdId;
    TQPopupMenu* m_viewMenu;
    TQPopupMenu* m_fileMenu;
    int m_rootModeMenuId;
    TQPopupMenu* m_speedMenu;
    int m_speedHighId;
    int m_speedNormalId;
    int m_speedLowId;
    int m_speedPausedId;
    system_status_bridge m_systemStatus;

    TaskMgrSystemTray* m_tray;
    TDEPopupMenu* m_trayMenu;
    int m_trayKeepAboveId;

    TQSize m_savedWindowSize;
    TQSize m_savedMinimumSize;
    static const int CompactWidth = 480;
    static const int CompactHeight = 500;
};

#endif // TASKMGR_MAINWINDOW_H
