/*
 * taskmgr_system_tray.h — Custom KSystemTray without TDE injected menu items.
 *
 * Flicker-free icon updates via paintEvent double-buffering (tdeclamui pattern).
 * Live stats tooltip via custom popup + cursor polling (KSystemTray/XEMBED safe).
 */

#ifndef TASKMGR_SYSTEM_TRAY_H
#define TASKMGR_SYSTEM_TRAY_H

#include <ksystemtray.h>
#include <tdepopupmenu.h>
#include <ntqpixmap.h>
#include <ntqstring.h>

class TQLabel;
class TQTimer;

class TaskMgrMainWindow;

class TaskMgrSystemTray : public KSystemTray {
    TQ_OBJECT

public:
    TaskMgrSystemTray(TaskMgrMainWindow* mainWindow);
    ~TaskMgrSystemTray();

    TDEPopupMenu* customMenu() const { return m_customMenu; }

    void setCpuIconLevel(int cpuLevel, const TQPixmap& icon);
    void setTooltipStats(int cpuPercent, int ramPercent, int diskPercent,
                         int networkTotalKbs);

protected:
    void contextMenuAboutToShow(TDEPopupMenu*);
    void mousePressEvent(TQMouseEvent* e);
    void mouseReleaseEvent(TQMouseEvent* e);
    void paintEvent(TQPaintEvent* e);
    void showEvent(TQShowEvent* e);
    void hideEvent(TQHideEvent* e);

private slots:
    void syncTooltipHover();

private:
    TQString formatTooltipText() const;
    bool isPointerOverTrayOrTooltip() const;
    void ensureTooltipPopup();
    void repositionTooltipPopup();
    void showTooltipPopup();
    void hideTooltipPopup();
    void updateTooltipPopupText();

    TaskMgrMainWindow* m_mainWindow;
    TDEPopupMenu* m_customMenu;

    TQPixmap m_currentPixmap;
    int m_currentLevel;

    int m_tooltipCpu;
    int m_tooltipRam;
    int m_tooltipDisk;
    int m_tooltipNetworkKbs;

    TQTimer* m_hoverPollTimer;
    TQLabel* m_tooltipPopup;
    int m_hoverPollTicks;
    bool m_tooltipVisible;
};

#endif
