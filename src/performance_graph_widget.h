#ifndef PERFORMANCE_GRAPH_WIDGET_H
#define PERFORMANCE_GRAPH_WIDGET_H

#include <ntqwidget.h>
#include <ntqstring.h>
#include <ntqcolor.h>
#include <ntqpoint.h>

class PerformanceGraphWidget : public TQWidget {
    TQ_OBJECT
public:
    enum GraphType {
        GraphTypeCPUOverall = 0,
        GraphTypeCPULogical = 1,
        GraphTypeRAM = 2,
        GraphTypeDisk = 3,
        GraphTypeNetwork = 4,
        GraphTypeGPU = 5,
        GraphTypeGPURender = 6,
        GraphTypeGPUVideo = 7
    };

    PerformanceGraphWidget(TQWidget* parent = 0);
    virtual ~PerformanceGraphWidget();

    void setGraphType(GraphType type, int deviceIndex = 0);
    GraphType graphType() const { return m_graphType; }
    int deviceIndex() const { return m_deviceIndex; }

    void refresh();

signals:
    void doubleClicked();

protected:
    void paintEvent(TQPaintEvent* e);
    void mouseMoveEvent(TQMouseEvent* e);
    void leaveEvent(TQEvent* e);
    void mouseDoubleClickEvent(TQMouseEvent* e);

private:
    GraphType m_graphType;
    int m_deviceIndex;

    int m_hoverIndex; // -1 if not hovering
    int m_hoverX;
    int m_hoverCore; // -1 if not hovering a specific core
    int m_hoverSubGraph; // -1 if not hovering a split graph, 0 for top, 1 for bottom

    TQColor m_cpuColor;
    TQColor m_cpuFillColor;
    TQColor m_ramColor;
    TQColor m_ramFillColor;
    TQColor m_swapColor;
    TQColor m_swapFillColor;
    TQColor m_diskColor;
    TQColor m_diskFillColor;
    TQColor m_diskWriteColor;
    TQColor m_diskWriteFillColor;
    TQColor m_netColor;
    TQColor m_netFillColor;
    TQColor m_netTxColor;
    TQColor m_gpuColor;
    TQColor m_gpuFillColor;
    TQColor m_gpuRenderColor;
    TQColor m_gpuRenderFillColor;
    TQColor m_gpuVideoColor;
    TQColor m_gpuVideoFillColor;

    void drawSingleGraph(TQPainter& p, const TQRect& r, GraphType type, int deviceIdx, bool isSubCore = false, int coreIdx = -0);
    void drawGrid(TQPainter& p, const TQRect& r, int hLines, int vLines);
    void drawTooltip(TQPainter& p, const TQRect& r);
};

#endif // PERFORMANCE_GRAPH_WIDGET_H
