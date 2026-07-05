/*
 * taskmgr_system_tray.cpp — Custom KSystemTray without TDE injected menu items.
 *
 * Flicker-free icon updates via paintEvent double-buffering (tdeclamui pattern).
 * Custom popup tooltip with cursor polling: KSystemTray/XEMBED does not deliver
 * reliable enter/leave events, and TQToolTip cannot live-update visible tips.
 */

#include "taskmgr_system_tray.h"
#include "taskmgr_mainwindow.h"

#include <ntqapplication.h>
#include <ntqcursor.h>
#include <ntqdesktopwidget.h>
#include <ntqevent.h>
#include <ntqlabel.h>
#include <ntqpainter.h>
#include <ntqtimer.h>

namespace {

const int kHoverPollMs = 100;
const int kTooltipWakeUpMs = 700;

class TrayStatsTipLabel : public TQLabel {
public:
    TrayStatsTipLabel(TQWidget* parent)
        : TQLabel(parent, "taskmgr_tray_tooltip",
                  WStyle_StaysOnTop | WStyle_Customize | WStyle_NoBorder
                      | WStyle_Tool | WX11BypassWM)
    {
        setMargin(1);
        setFrameStyle(TQFrame::Plain | TQFrame::Box);
        setLineWidth(1);
        setAlignment(AlignAuto | AlignTop);
        setIndent(0);
    }
};

} // namespace

TaskMgrSystemTray::TaskMgrSystemTray(TaskMgrMainWindow* mainWindow)
    : KSystemTray(mainWindow, "taskmgr_systray"),
      m_mainWindow(mainWindow),
      m_customMenu(new TDEPopupMenu(mainWindow)),
      m_currentLevel(-1),
      m_tooltipCpu(-1),
      m_tooltipRam(-1),
      m_tooltipDisk(-1),
      m_tooltipNetworkKbs(-1),
      m_hoverPollTimer(new TQTimer(this)),
      m_tooltipPopup(0),
      m_hoverPollTicks(0),
      m_tooltipVisible(false)
{
    setCaption("Task Manager");
    connect(m_hoverPollTimer, SIGNAL(timeout()), this, SLOT(syncTooltipHover()));
    m_hoverPollTimer->start(kHoverPollMs);
}

TaskMgrSystemTray::~TaskMgrSystemTray()
{
    delete m_tooltipPopup;
    m_tooltipPopup = 0;
}

void TaskMgrSystemTray::setCpuIconLevel(int cpuLevel, const TQPixmap& icon)
{
    if (icon.isNull())
        return;
    if (cpuLevel == m_currentLevel && icon.serialNumber() == m_currentPixmap.serialNumber())
        return;

    m_currentLevel = cpuLevel;
    m_currentPixmap = icon;

    /* setPixmap once per level for XEMBED; visual updates via paintEvent */
    setPixmap(m_currentPixmap);
    repaint(false);
}

TQString TaskMgrSystemTray::formatTooltipText() const
{
    TQString networkSpeed;
    if (m_tooltipNetworkKbs >= 1024)
        networkSpeed = TQString("%1 MB/s").arg(m_tooltipNetworkKbs / 1024.0, 0, 'f', 1);
    else
        networkSpeed = TQString("%1 KB/s").arg(m_tooltipNetworkKbs);

    return TQString("CPU: %1%\nRAM: %2%\nDISK: %3%\nNET: %4")
        .arg(m_tooltipCpu).arg(m_tooltipRam).arg(m_tooltipDisk).arg(networkSpeed);
}

bool TaskMgrSystemTray::isPointerOverTrayOrTooltip() const
{
    if (!isVisible() || width() <= 0 || height() <= 0)
        return false;

    TQPoint gp = TQCursor::pos();
    TQRect trayRect(mapToGlobal(TQPoint(0, 0)), size());
    trayRect.addCoords(-1, -1, 1, 1);

    if (trayRect.contains(gp))
        return true;

    if (m_tooltipPopup && m_tooltipVisible && m_tooltipPopup->isVisible()) {
        TQRect tipRect = m_tooltipPopup->geometry();
        tipRect.addCoords(-2, -2, 2, 2);
        if (tipRect.contains(gp))
            return true;
    }

    return false;
}

void TaskMgrSystemTray::ensureTooltipPopup()
{
    if (m_tooltipPopup)
        return;

    int scr = TQApplication::desktop()->screenNumber(this);
    m_tooltipPopup = new TrayStatsTipLabel(TQApplication::desktop()->screen(scr));
}

void TaskMgrSystemTray::repositionTooltipPopup()
{
    if (!m_tooltipPopup)
        return;

    int scr = TQApplication::desktop()->screenNumber(this);
    TQRect screen = TQApplication::desktop()->screenGeometry(scr);
    TQPoint anchor = mapToGlobal(TQPoint(width() / 2, height()));
    TQPoint p(anchor.x() + 2, anchor.y() + 16);

    if (p.x() + m_tooltipPopup->width() > screen.x() + screen.width())
        p.rx() -= 4 + m_tooltipPopup->width();
    if (p.y() + m_tooltipPopup->height() > screen.y() + screen.height())
        p.ry() -= 24 + m_tooltipPopup->height();
    if (p.y() < screen.y())
        p.setY(screen.y());
    if (p.x() + m_tooltipPopup->width() > screen.x() + screen.width())
        p.setX(screen.x() + screen.width() - m_tooltipPopup->width());
    if (p.x() < screen.x())
        p.setX(screen.x());
    if (p.y() + m_tooltipPopup->height() > screen.y() + screen.height())
        p.setY(screen.y() + screen.height() - m_tooltipPopup->height());

    m_tooltipPopup->move(p);
}

void TaskMgrSystemTray::updateTooltipPopupText()
{
    if (!m_tooltipPopup || !m_tooltipVisible)
        return;

    TQString text = formatTooltipText();
    if (text == m_tooltipPopup->text())
        return;

    m_tooltipPopup->setText(text);
    m_tooltipPopup->adjustSize();
    repositionTooltipPopup();
}

void TaskMgrSystemTray::showTooltipPopup()
{
    if (m_tooltipCpu < 0)
        return;

    ensureTooltipPopup();
    m_tooltipPopup->setText(formatTooltipText());
    m_tooltipPopup->adjustSize();
    repositionTooltipPopup();
    m_tooltipPopup->show();
    m_tooltipPopup->raise();
    m_tooltipVisible = true;
}

void TaskMgrSystemTray::hideTooltipPopup()
{
    if (m_tooltipPopup && m_tooltipVisible)
        m_tooltipPopup->hide();
    m_tooltipVisible = false;
}

void TaskMgrSystemTray::syncTooltipHover()
{
    if (!isPointerOverTrayOrTooltip()) {
        m_hoverPollTicks = 0;
        hideTooltipPopup();
        return;
    }

    ++m_hoverPollTicks;

    if (!m_tooltipVisible) {
        if (m_hoverPollTicks * kHoverPollMs >= kTooltipWakeUpMs)
            showTooltipPopup();
        return;
    }

    updateTooltipPopupText();
}

void TaskMgrSystemTray::setTooltipStats(int cpuPercent, int ramPercent, int diskPercent,
                                        int networkTotalKbs)
{
    m_tooltipCpu = cpuPercent;
    m_tooltipRam = ramPercent;
    m_tooltipDisk = diskPercent;
    m_tooltipNetworkKbs = networkTotalKbs;

    if (m_tooltipVisible)
        updateTooltipPopupText();
    else
        syncTooltipHover();
}

void TaskMgrSystemTray::paintEvent(TQPaintEvent*)
{
    if (m_currentPixmap.isNull())
        return;

    TQPainter p(this);
    p.drawPixmap(0, 0, m_currentPixmap);
}

void TaskMgrSystemTray::showEvent(TQShowEvent* e)
{
    if (!m_hoverPollTimer->isActive())
        m_hoverPollTimer->start(kHoverPollMs);
    KSystemTray::showEvent(e);
}

void TaskMgrSystemTray::hideEvent(TQHideEvent* e)
{
    m_hoverPollTimer->stop();
    m_hoverPollTicks = 0;
    hideTooltipPopup();
    KSystemTray::hideEvent(e);
}

void TaskMgrSystemTray::contextMenuAboutToShow(TDEPopupMenu*)
{
    /* Suppress KSystemTray default menu population. */
}

void TaskMgrSystemTray::mousePressEvent(TQMouseEvent* e)
{
    m_hoverPollTicks = 0;
    hideTooltipPopup();

    if (e->button() == TQt::RightButton)
        return;
    if (e->button() == TQt::LeftButton)
        return;
    KSystemTray::mousePressEvent(e);
}

void TaskMgrSystemTray::mouseReleaseEvent(TQMouseEvent* e)
{
    if (e->button() == TQt::RightButton) {
        if (m_customMenu)
            m_customMenu->popup(e->globalPos());
        return;
    }
    if (e->button() == TQt::LeftButton) {
        if (m_mainWindow)
            m_mainWindow->toggleFromTray();
        return;
    }
    KSystemTray::mouseReleaseEvent(e);
}

#include "taskmgr_system_tray.moc"
