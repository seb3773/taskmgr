#include "performance_tab.h"
#include "backend_bridge.h"
#include <ntqlayout.h>
#include <ntqpopupmenu.h>
#include <ntqmessagebox.h>
#include <ntqpainter.h>
#include <ntqmainwindow.h>
#include <ntqmenubar.h>
#include <ntqtabbar.h>
#include <ntqobjectlist.h>

class RightPanelWidget : public TQWidget {
public:
    RightPanelWidget(TQWidget* parent, TQWidget* sidebar = 0)
        : TQWidget(parent), m_sidebar(sidebar) {}
protected:
    void paintEvent(TQPaintEvent* e) {
        TQWidget::paintEvent(e);
        if (m_sidebar && m_sidebar->isVisible()) {
            TQPainter p(this);
            p.setPen(TQPen(TQColor(210, 210, 210), 1));
            p.drawLine(0, 10, 0, height() - 10);
        }
    }
private:
    TQWidget* m_sidebar;
};

double PerformanceTab::s_scrollOffset = 0.0;

double PerformanceTab::scrollOffset()
{
    return s_scrollOffset;
}

bool PerformanceTab::isSummaryViewActive() const
{
    return m_compactMode || m_sidebarSummaryMode;
}

WId PerformanceTab::summaryViewWinId() const
{
    if (m_compactMode && m_graphWidget)
        return m_graphWidget->winId();
    if (m_sidebarSummaryMode && m_sidebarScroll)
        return m_sidebarScroll->winId();
    return 0;
}

PerformanceTab::PerformanceTab(TQWidget* parent)
    : TQWidget(parent),
      m_selectedIndex(0),
      m_animationTimerId(0),
      m_compactMode(false),
      m_sidebarSummaryMode(false),
      m_dragActive(false)
{
    // Enable focus policy for arrow key navigation
    setFocusPolicy(TQWidget::StrongFocus);

    // Main layout
    TQHBoxLayout* mainLayout = new TQHBoxLayout(this, 5, 5);

    // 1. Sidebar Scroll View (direct child of tab widget, fixed width)
    m_sidebarScroll = new TQScrollView(this);
    m_sidebarScroll->setFixedWidth(236); // content width (220px) + vertical scrollbar margin (16px)
    mainLayout->addWidget(m_sidebarScroll);
    m_sidebarScroll->setVScrollBarMode(TQScrollView::Auto);
    m_sidebarScroll->setHScrollBarMode(TQScrollView::AlwaysOff);
    m_sidebarScroll->setFrameStyle(TQFrame::NoFrame);
    m_sidebarScroll->setBackgroundMode(TQt::PaletteBackground);
    m_sidebarScroll->viewport()->setBackgroundMode(TQt::PaletteBackground);

    m_sidebarContent = new TQWidget(m_sidebarScroll->viewport());
    m_sidebarContent->setBackgroundMode(TQt::PaletteBackground);
    m_sidebarLayout = new TQVBoxLayout(m_sidebarContent, 0, 0);

    // Add CPU block
    MiniBlockWidget* cpuBlock = new MiniBlockWidget(MiniBlockWidget::TypeCPU, 0, m_sidebarContent);
    m_sidebarLayout->addWidget(cpuBlock);
    m_blocks.append(cpuBlock);
    connect(cpuBlock, SIGNAL(clicked(MiniBlockWidget*)), this, SLOT(onBlockClicked(MiniBlockWidget*)));

    // Add Memory block
    MiniBlockWidget* ramBlock = new MiniBlockWidget(MiniBlockWidget::TypeRAM, 0, m_sidebarContent);
    m_sidebarLayout->addWidget(ramBlock);
    m_blocks.append(ramBlock);
    connect(ramBlock, SIGNAL(clicked(MiniBlockWidget*)), this, SLOT(onBlockClicked(MiniBlockWidget*)));

    // Add Disk blocks
    int disks = get_disk_count();
    for (int i = 0; i < disks; ++i) {
        MiniBlockWidget* diskBlock = new MiniBlockWidget(MiniBlockWidget::TypeDisk, i, m_sidebarContent);
        m_sidebarLayout->addWidget(diskBlock);
        m_blocks.append(diskBlock);
        connect(diskBlock, SIGNAL(clicked(MiniBlockWidget*)), this, SLOT(onBlockClicked(MiniBlockWidget*)));
    }

    // Add Network blocks
    int nets = get_network_count();
    for (int i = 0; i < nets; ++i) {
        MiniBlockWidget* netBlock = new MiniBlockWidget(MiniBlockWidget::TypeNetwork, i, m_sidebarContent);
        m_sidebarLayout->addWidget(netBlock);
        m_blocks.append(netBlock);
        connect(netBlock, SIGNAL(clicked(MiniBlockWidget*)), this, SLOT(onBlockClicked(MiniBlockWidget*)));
    }

    // Add GPU block if available
    const char* gpu_name = bridge_get_gpu_model_name();
    if (gpu_name && strlen(gpu_name) > 0) {
        MiniBlockWidget* gpuBlock = new MiniBlockWidget(MiniBlockWidget::TypeGPU, 0, m_sidebarContent);
        m_sidebarLayout->addWidget(gpuBlock);
        m_blocks.append(gpuBlock);
        connect(gpuBlock, SIGNAL(clicked(MiniBlockWidget*)), this, SLOT(onBlockClicked(MiniBlockWidget*)));
    }

    // Install event filter on all mini blocks, sidebar content and scroll view for drag and context menu
    for (int i = 0; i < (int)m_blocks.size(); ++i) {
        m_blocks[i]->installEventFilter(this);
    }
    m_sidebarContent->installEventFilter(this);
    m_sidebarScroll->installEventFilter(this);
    m_sidebarScroll->viewport()->installEventFilter(this);

    // Add spacer to keep sidebar items aligned at the top
    m_sidebarLayout->addStretch(1);

    m_sidebarScroll->addChild(m_sidebarContent);

    // Set initial size for sidebar container widget inside scroll view
    int blockHeight = MiniBlockWidget::hideGraphs() ? 52 : 70;
    m_sidebarContent->resize(220, m_blocks.size() * blockHeight + 20);

    // 2. Right Panel Layout container (direct child of tab widget, expands dynamically)
    m_rightPanel = new RightPanelWidget(this, m_sidebarScroll);
    m_rightLayout = new TQVBoxLayout(m_rightPanel, 5, 0);
    mainLayout->addWidget(m_rightPanel, 1); // Spans all remaining horizontal space

    // Large Live Graph
    m_graphWidget = new PerformanceGraphWidget(m_rightPanel);
    m_rightLayout->addWidget(m_graphWidget, 1); // Expand to fill top half

    // Detailed Stats Block
    m_infoWidget = new InfoBlockWidget(m_rightPanel);
    m_rightLayout->addWidget(m_infoWidget, 0); // Fits bottom half

    // Connect double click and event filter for compact mode
    connect(m_graphWidget, SIGNAL(doubleClicked()), this, SLOT(onGraphDoubleClicked()));
    m_graphWidget->installEventFilter(this);

    // Select the first block (CPU Overall)
    selectBlock(0);
}

PerformanceTab::~PerformanceTab()
{
}

void PerformanceTab::selectBlock(int index)
{
    if (index < 0 || index >= (int)m_blocks.size()) return;

    m_selectedIndex = index;

    // Update selection state of all blocks
    for (int i = 0; i < (int)m_blocks.size(); ++i) {
        m_blocks[i]->setSelected(i == m_selectedIndex);
    }

    // Ensure the selected block is visible in the scroll area
    if (m_sidebarScroll && index >= 0 && index < (int)m_blocks.size()) {
        MiniBlockWidget* block = m_blocks[index];
        m_sidebarScroll->ensureVisible(0, block->y() + block->height() / 2, 0, block->height() / 2);
    }

    // Configure main graph and info widgets based on selected block
    MiniBlockWidget* active = m_blocks[m_selectedIndex];
    switch (active->type()) {
        case MiniBlockWidget::TypeCPU:
            if (bridge_get_cpu_graph_type() == 1) {
                m_graphWidget->setGraphType(PerformanceGraphWidget::GraphTypeCPULogical);
            } else {
                m_graphWidget->setGraphType(PerformanceGraphWidget::GraphTypeCPUOverall);
            }
            m_infoWidget->setType(InfoBlockWidget::TypeCPU);
            break;
        case MiniBlockWidget::TypeRAM:
            m_graphWidget->setGraphType(PerformanceGraphWidget::GraphTypeRAM);
            m_infoWidget->setType(InfoBlockWidget::TypeRAM);
            break;
        case MiniBlockWidget::TypeDisk:
            m_graphWidget->setGraphType(PerformanceGraphWidget::GraphTypeDisk, active->deviceIndex());
            m_infoWidget->setType(InfoBlockWidget::TypeDisk, active->deviceIndex());
            break;
        case MiniBlockWidget::TypeNetwork:
            m_graphWidget->setGraphType(PerformanceGraphWidget::GraphTypeNetwork, active->deviceIndex());
            m_infoWidget->setType(InfoBlockWidget::TypeNetwork, active->deviceIndex());
            break;
        case MiniBlockWidget::TypeGPU:
            m_graphWidget->setGraphType(PerformanceGraphWidget::GraphTypeGPU);
            m_infoWidget->setType(InfoBlockWidget::TypeGPU);
            break;
    }
}

void PerformanceTab::onBlockClicked(MiniBlockWidget* widget)
{
    setFocus();
    for (int i = 0; i < (int)m_blocks.size(); ++i) {
        if (m_blocks[i] == widget) {
            selectBlock(i);
            break;
        }
    }
}

void PerformanceTab::refresh()
{
    if (bridge_get_app_flags() & APP_FLAG_SMOOTH_SCROLLING) {
        m_scrollTimer.start();
        s_scrollOffset = 0.0;
        if (m_animationTimerId == 0) {
            m_animationTimerId = startTimer(33); // ~30 FPS
        }
    } else {
        if (m_animationTimerId != 0) {
            killTimer(m_animationTimerId);
            m_animationTimerId = 0;
        }
        s_scrollOffset = 0.0;
    }

    // Refresh sidebar blocks
    for (int i = 0; i < (int)m_blocks.size(); ++i) {
        m_blocks[i]->refresh();
    }

    // Refresh active graph and info widgets
    m_graphWidget->refresh();
    m_infoWidget->refresh();
}

void PerformanceTab::keyPressEvent(TQKeyEvent* e)
{
    if (m_compactMode && e->key() == TQt::Key_Escape) {
        exitCompactMode();
        e->accept();
        return;
    }

    if (m_blocks.isEmpty()) {
        TQWidget::keyPressEvent(e);
        return;
    }

    if (e->key() == TQt::Key_Up || e->key() == TQt::Key_Left) {
        if (m_selectedIndex > 0) {
            selectBlock(m_selectedIndex - 1);
        } else {
            selectBlock((int)m_blocks.size() - 1);
        }
        e->accept();
    } else if (e->key() == TQt::Key_Down || e->key() == TQt::Key_Right) {
        if (m_selectedIndex < (int)m_blocks.size() - 1) {
            selectBlock(m_selectedIndex + 1);
        } else {
            selectBlock(0);
        }
        e->accept();
    } else {
        TQWidget::keyPressEvent(e);
    }
}

void PerformanceTab::contextMenuEvent(TQContextMenuEvent* e)
{
    // Do not handle context menu globally on the tab widget anymore.
    // We only want the context menu to trigger when clicking on the graph widget.
    TQWidget::contextMenuEvent(e);
}

void PerformanceTab::onCPUGraphTypeAction(int id)
{
    bridge_set_cpu_graph_type(id);
    if (id == 0) {
        m_graphWidget->setGraphType(PerformanceGraphWidget::GraphTypeCPUOverall);
    } else {
        m_graphWidget->setGraphType(PerformanceGraphWidget::GraphTypeCPULogical);
    }
}

void PerformanceTab::timerEvent(TQTimerEvent* e)
{
    if (e->timerId() == m_animationTimerId) {
        if (bridge_get_app_flags() & APP_FLAG_SMOOTH_SCROLLING) {
            int elapsed_ms = m_scrollTimer.elapsed();
            int interval_ms = bridge_get_refresh_interval();
            if (interval_ms <= 0) interval_ms = 1000;
            s_scrollOffset = (double)elapsed_ms / (double)interval_ms;
            if (s_scrollOffset > 1.0) {
                s_scrollOffset = 1.0;
            }

            // Repaint active graph and the selected mini block to keep smooth scroll in sync
            if (m_graphWidget) {
                m_graphWidget->update();
            }
            if (m_selectedIndex >= 0 && m_selectedIndex < (int)m_blocks.size()) {
                if (m_blocks[m_selectedIndex]) {
                    m_blocks[m_selectedIndex]->update();
                }
            }
        } else {
            killTimer(m_animationTimerId);
            m_animationTimerId = 0;
            s_scrollOffset = 0.0;
            if (m_graphWidget) {
                m_graphWidget->update();
            }
            for (size_t i = 0; i < m_blocks.size(); ++i) {
                if (m_blocks[i]) {
                    m_blocks[i]->update();
                }
            }
        }
    } else {
        TQWidget::timerEvent(e);
    }
}

void PerformanceTab::showEvent(TQShowEvent*)
{
    setFocus();
    if (bridge_get_app_flags() & APP_FLAG_SMOOTH_SCROLLING) {
        m_scrollTimer.start();
        s_scrollOffset = 0.0;
        if (m_animationTimerId == 0) {
            m_animationTimerId = startTimer(33);
        }
    }
}

void PerformanceTab::hideEvent(TQHideEvent*)
{
    if (m_animationTimerId != 0) {
        killTimer(m_animationTimerId);
        m_animationTimerId = 0;
    }
}

void PerformanceTab::onGraphDoubleClicked()
{
    if (m_compactMode) {
        exitCompactMode();
    } else {
        enterCompactMode();
    }
}

bool PerformanceTab::eventFilter(TQObject* watched, TQEvent* e)
{
    // Check if watched is one of our mini blocks
    bool isMiniBlock = false;
    for (int i = 0; i < (int)m_blocks.size(); ++i) {
        if (m_blocks[i] == watched) {
            isMiniBlock = true;
            break;
        }
    }

    if (isMiniBlock || watched == m_sidebarContent || watched == m_sidebarScroll || watched == m_sidebarScroll->viewport()) {
        if (e->type() == TQEvent::ContextMenu) {
            TQContextMenuEvent* ce = (TQContextMenuEvent*)e;
            TQPopupMenu* menu = new TQPopupMenu(this);

            int summaryId = menu->insertItem("Summary view", this, SLOT(onContextMenuSidebarSummaryView()));
            menu->setItemChecked(summaryId, m_sidebarSummaryMode);

            int hideGraphsId = menu->insertItem("Hide graphs", this, SLOT(onContextMenuHideGraphs()));
            menu->setItemChecked(hideGraphsId, MiniBlockWidget::hideGraphs());

            if (m_sidebarSummaryMode) {
                menu->insertSeparator();
                menu->insertItem("Resize", this, SLOT(onContextMenuResize()));
            }

            menu->exec(ce->globalPos());
            delete menu;
            return true;
        }

        if (m_sidebarSummaryMode) {
            if (e->type() == TQEvent::MouseButtonPress) {
                TQMouseEvent* me = (TQMouseEvent*)e;
                if (me->button() == TQt::LeftButton) {
                    m_dragActive = true;
                    m_dragStartPos = me->globalPos();
                    m_dragWindowStartPos = m_sidebarScroll->pos();
                    // If it's a mini block, let it handle selection, otherwise consume
                    if (!isMiniBlock) return true;
                }
            } else if (e->type() == TQEvent::MouseMove) {
                TQMouseEvent* me = (TQMouseEvent*)e;
                if (m_dragActive && (me->state() & TQt::LeftButton)) {
                    TQPoint delta = me->globalPos() - m_dragStartPos;
                    m_sidebarScroll->move(m_dragWindowStartPos + delta);
                    return true;
                }
            } else if (e->type() == TQEvent::MouseButtonRelease) {
                TQMouseEvent* me = (TQMouseEvent*)e;
                if (me->button() == TQt::LeftButton) {
                    m_dragActive = false;
                    return true;
                }
            } else if (e->type() == TQEvent::MouseButtonDblClick) {
                exitSidebarSummaryMode();
                return true;
            } else if (e->type() == TQEvent::KeyPress) {
                TQKeyEvent* ke = (TQKeyEvent*)e;
                if (ke->key() == TQt::Key_Escape) {
                    exitSidebarSummaryMode();
                    return true;
                }
            }
        }
    }

    if (watched == m_graphWidget) {
        if (e->type() == TQEvent::ContextMenu) {
            TQContextMenuEvent* ce = (TQContextMenuEvent*)e;
            
            TQPopupMenu* menu = new TQPopupMenu(this);
            MiniBlockWidget* active = m_blocks[m_selectedIndex];

            // 1. CPU-specific "Change graphic to" submenu
            if (active->type() == MiniBlockWidget::TypeCPU) {
                TQPopupMenu* changeGraphicMenu = new TQPopupMenu(menu);
                int overallId = changeGraphicMenu->insertItem("Overall utilization", this, SLOT(onCPUGraphTypeAction(int)), 0, 0);
                int logicalId = changeGraphicMenu->insertItem("Logical processors", this, SLOT(onCPUGraphTypeAction(int)), 0, 1);

                if (m_graphWidget->graphType() == PerformanceGraphWidget::GraphTypeCPUOverall) {
                    changeGraphicMenu->setItemChecked(overallId, true);
                } else {
                    changeGraphicMenu->setItemChecked(logicalId, true);
                }

                menu->insertItem("Change graphic to", changeGraphicMenu);
            }

            // 2. Summary View checkable item (always present)
            int summaryId = menu->insertItem("Summary view", this, SLOT(onContextMenuSummaryView()));
            menu->setItemChecked(summaryId, m_compactMode);

            if (m_compactMode) {
                menu->insertItem("Resize", this, SLOT(onContextMenuResize()));
            }

            // 3. Display submenu (always present)
            TQPopupMenu* displayMenu = new TQPopupMenu(menu);
            for (int i = 0; i < (int)m_blocks.size(); ++i) {
                MiniBlockWidget* block = m_blocks[i];
                TQString label;
                switch (block->type()) {
                    case MiniBlockWidget::TypeCPU:
                        label = "CPU";
                        break;
                    case MiniBlockWidget::TypeRAM:
                        label = "RAM";
                        break;
                    case MiniBlockWidget::TypeDisk: {
                        disk_info_t* disk = get_disk_info(block->deviceIndex());
                        label = TQString("DISK %1 (%2)").arg(block->deviceIndex()).arg(disk ? disk->name : "Unknown");
                        break;
                    }
                    case MiniBlockWidget::TypeNetwork: {
                        network_info_t* net = get_network_info(block->deviceIndex());
                        label = TQString("NET (%1)").arg(net ? net->name : "Unknown");
                        break;
                    }
                    case MiniBlockWidget::TypeGPU:
                        label = "GPU";
                        break;
                }
                int itemId = displayMenu->insertItem(label, this, SLOT(onContextMenuDisplayAction(int)), 0, i);
                if (i == m_selectedIndex) {
                    displayMenu->setItemChecked(itemId, true);
                }
            }
            menu->insertItem("Display", displayMenu);

            menu->exec(ce->globalPos());
            delete menu;
            return true;
        }

        if (m_compactMode) {
            if (e->type() == TQEvent::MouseButtonPress) {
                TQMouseEvent* me = (TQMouseEvent*)e;
                if (me->button() == TQt::LeftButton) {
                    m_dragActive = true;
                    m_dragStartPos = me->globalPos();
                    m_dragWindowStartPos = m_graphWidget->pos();
                    return true;
                }
            } else if (e->type() == TQEvent::MouseMove) {
                TQMouseEvent* me = (TQMouseEvent*)e;
                if (m_dragActive && (me->state() & TQt::LeftButton)) {
                    TQPoint delta = me->globalPos() - m_dragStartPos;
                    m_graphWidget->move(m_dragWindowStartPos + delta);
                    return true;
                }
            } else if (e->type() == TQEvent::MouseButtonRelease) {
                TQMouseEvent* me = (TQMouseEvent*)e;
                if (me->button() == TQt::LeftButton) {
                    m_dragActive = false;
                    return true;
                }
            } else if (e->type() == TQEvent::MouseButtonDblClick) {
                exitCompactMode();
                return true;
            } else if (e->type() == TQEvent::KeyPress) {
                TQKeyEvent* ke = (TQKeyEvent*)e;
                if (ke->key() == TQt::Key_Escape) {
                    exitCompactMode();
                    return true;
                }
            }
        }
    }
    return TQWidget::eventFilter(watched, e);
}

void PerformanceTab::enterCompactMode()
{
    TQWidget* mainWin = topLevelWidget();
    if (!mainWin) return;

    TQPoint graphGlobalPos = m_graphWidget->mapToGlobal(TQPoint(0, 0));
    TQSize graphSize = m_graphWidget->size();

    m_savedWindowPos = mainWin->pos();
    m_savedWindowSize = mainWin->size();
    m_frameOffset = mainWin->frameGeometry().topLeft() - mainWin->geometry().topLeft();

    // Remove from layout before reparenting to avoid layout conflicts
    m_rightLayout->remove(m_graphWidget);

    m_graphWidget->reparent(0, WStyle_Customize | WStyle_NoBorder, graphGlobalPos, false);
    m_graphWidget->resize(graphSize);

    mainWin->hide();

    m_graphWidget->show();
    m_graphWidget->setFocus();

    m_compactMode = true;
}

void PerformanceTab::exitCompactMode()
{
    m_dragActive = false;
    TQWidget* mainWin = topLevelWidget();
    if (!mainWin) return;

    TQPoint graphGlobalPos = m_graphWidget->mapToGlobal(TQPoint(0, 0));

    m_graphWidget->hide();

    m_graphWidget->reparent(m_rightPanel, 0, TQPoint(0, 0), false);
    m_rightLayout->insertWidget(0, m_graphWidget, 1);
    m_graphWidget->show();

    mainWin->resize(m_savedWindowSize);

    // Call selectBlock to force complete recreation and visibility update of InfoBlockWidget
    selectBlock(m_selectedIndex);

    // Refresh and activate layouts
    if (m_rightLayout) m_rightLayout->activate();
    if (mainWin->layout()) mainWin->layout()->activate();
    if (this->layout()) this->layout()->activate();

    TQPoint graphInNormal = m_graphWidget->mapTo(mainWin, TQPoint(0, 0));
    TQPoint newWindowPos = graphGlobalPos - graphInNormal + m_frameOffset;

    mainWin->move(newWindowPos);
    mainWin->show();

    m_compactMode = false;
    setFocus();
    emit summaryViewChanged();
}

void PerformanceTab::onContextMenuSummaryView()
{
    if (m_compactMode) {
        exitCompactMode();
    } else {
        enterCompactMode();
    }
}

void PerformanceTab::enterSidebarSummaryMode()
{
    TQWidget* mainWin = topLevelWidget();
    if (!mainWin) return;

    TQPoint sidebarGlobalPos = m_sidebarScroll->mapToGlobal(TQPoint(0, 0));
    TQSize sidebarSize = m_sidebarScroll->size();

    m_savedWindowPos = mainWin->pos();
    m_savedWindowSize = mainWin->size();
    m_frameOffset = mainWin->frameGeometry().topLeft() - mainWin->geometry().topLeft();

    // Remove from main layout
    this->layout()->remove(m_sidebarScroll);

    m_sidebarScroll->reparent(0, WStyle_Customize | WStyle_NoBorder, sidebarGlobalPos, false);
    m_sidebarScroll->resize(sidebarSize);

    mainWin->hide();

    m_sidebarScroll->show();
    m_sidebarScroll->setFocus();

    m_sidebarSummaryMode = true;
    emit summaryViewChanged();
}

void PerformanceTab::exitSidebarSummaryMode()
{
    m_dragActive = false;
    TQWidget* mainWin = topLevelWidget();
    if (!mainWin) return;

    TQPoint sidebarGlobalPos = m_sidebarScroll->mapToGlobal(TQPoint(0, 0));

    m_sidebarScroll->hide();

    // Put it back in the main layout (which is a TQHBoxLayout)
    m_sidebarScroll->reparent(this, 0, TQPoint(0, 0), false);
    ((TQHBoxLayout*)this->layout())->insertWidget(0, m_sidebarScroll);
    m_sidebarScroll->show();

    mainWin->resize(m_savedWindowSize);

    // Refresh and activate layouts
    if (this->layout()) this->layout()->activate();
    if (mainWin->layout()) mainWin->layout()->activate();

    TQPoint sidebarInNormal = m_sidebarScroll->mapTo(mainWin, TQPoint(0, 0));
    TQPoint newWindowPos = sidebarGlobalPos - sidebarInNormal + m_frameOffset;

    mainWin->move(newWindowPos);
    mainWin->show();

    m_sidebarSummaryMode = false;
    setFocus();
    emit summaryViewChanged();
}

void PerformanceTab::onContextMenuSidebarSummaryView()
{
    if (m_sidebarSummaryMode) {
        exitSidebarSummaryMode();
    } else {
        enterSidebarSummaryMode();
    }
}

void PerformanceTab::onContextMenuHideGraphs()
{
    bool hide = !MiniBlockWidget::hideGraphs();
    MiniBlockWidget::setHideGraphs(hide);

    for (int i = 0; i < (int)m_blocks.size(); ++i) {
        m_blocks[i]->updateHeight();
    }

    // Recalculate sidebar content height
    int blockHeight = hide ? 52 : 70;
    m_sidebarContent->resize(220, m_blocks.size() * blockHeight + 20);

    if (m_sidebarLayout) m_sidebarLayout->activate();
    m_sidebarContent->update();
    m_sidebarScroll->update();
}

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <ntqcursor.h>

void PerformanceTab::onContextMenuResize()
{
    TQWidget* topLevel = 0;
    if (m_compactMode) {
        topLevel = m_graphWidget;
    } else if (m_sidebarSummaryMode) {
        topLevel = m_sidebarScroll;
    }

    if (!topLevel) return;

    // 1. Move the mouse cursor to the bottom-right corner of the window
    TQPoint globalBottomRight = topLevel->mapToGlobal(TQPoint(topLevel->width(), topLevel->height()));
    TQCursor::setPos(globalBottomRight);

    // 2. Send the EWMH _NET_WM_MOVERESIZE message to the root window
    Display* dpy = tqt_xdisplay();
    Window root = tqt_xrootwin();
    Window win = topLevel->winId();

    XEvent xev;
    memset(&xev, 0, sizeof(xev));
    xev.xclient.type = ClientMessage;
    xev.xclient.message_type = XInternAtom(dpy, "_NET_WM_MOVERESIZE", False);
    xev.xclient.display = dpy;
    xev.xclient.window = win;
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = globalBottomRight.x();
    xev.xclient.data.l[1] = globalBottomRight.y();
    xev.xclient.data.l[2] = 4; // _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT
    xev.xclient.data.l[3] = 1; // Left button
    xev.xclient.data.l[4] = 1; // Source indication (normal application)

    // Release pointer grabs before sending EWMH message
    XUngrabPointer(dpy, CurrentTime);

    XSendEvent(dpy, root, False,
               SubstructureRedirectMask | SubstructureNotifyMask,
               &xev);
    XFlush(dpy);
}

void PerformanceTab::onContextMenuDisplayAction(int id)
{
    selectBlock(id);
}

#include "performance_tab.moc"
