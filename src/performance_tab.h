#ifndef PERFORMANCE_TAB_H
#define PERFORMANCE_TAB_H

#include <ntqwidget.h>
#include <ntqsplitter.h>
#include <ntqscrollview.h>
#include <ntqvaluevector.h>
#include <ntqdatetime.h>
#include "mini_block_widget.h"
#include "performance_graph_widget.h"
#include "info_block_widget.h"

class PerformanceTab : public TQWidget {
    TQ_OBJECT
public:
    PerformanceTab(TQWidget* parent = 0);
    virtual ~PerformanceTab();

    void refresh();

    bool isSummaryViewActive() const;
    WId summaryViewWinId() const;

    static double scrollOffset();

signals:
    void summaryViewChanged();

protected:
    void keyPressEvent(TQKeyEvent* e);
    void contextMenuEvent(TQContextMenuEvent* e);
    void timerEvent(TQTimerEvent* e) override;
    void showEvent(TQShowEvent* e) override;
    void hideEvent(TQHideEvent* e) override;
    bool eventFilter(TQObject* watched, TQEvent* e) override;

private slots:
    void onBlockClicked(MiniBlockWidget* widget);
    void onCPUGraphTypeAction(int id);
    void onGraphDoubleClicked();
    void onContextMenuSummaryView();
    void onContextMenuSidebarSummaryView();
    void onContextMenuHideGraphs();
    void onContextMenuResize();
    void onContextMenuDisplayAction(int id);

private:
    TQScrollView* m_sidebarScroll;
    TQWidget* m_sidebarContent;
    TQVBoxLayout* m_sidebarLayout;

    PerformanceGraphWidget* m_graphWidget;
    InfoBlockWidget* m_infoWidget;
    TQWidget* m_rightPanel;
    TQVBoxLayout* m_rightLayout;

    TQValueVector<MiniBlockWidget*> m_blocks;
    int m_selectedIndex;
    int m_animationTimerId;
    TQTime m_scrollTimer;
    static double s_scrollOffset;

    bool m_compactMode;
    bool m_sidebarSummaryMode;
    TQPoint m_savedWindowPos;
    TQSize m_savedWindowSize;
    TQPoint m_frameOffset;

    // Drag-to-move in compact mode
    bool m_dragActive;
    TQPoint m_dragStartPos;
    TQPoint m_dragWindowStartPos;

    void enterCompactMode();
    void exitCompactMode();
    void enterSidebarSummaryMode();
    void exitSidebarSummaryMode();
    void selectBlock(int index);
};

#endif // PERFORMANCE_TAB_H
