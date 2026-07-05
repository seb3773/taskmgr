#include "performance_graph_widget.h"
#include "performance_tab.h"
#include "backend_bridge.h"
#include "tqtaapainter.h"
#include <ntqpainter.h>
#include <ntqstyle.h>
#include <fstream>

static bool isDiskIostatsEnabled(const TQString& diskName)
{
    TQString path = TQString("/sys/block/%1/queue/iostats").arg(diskName);
    std::ifstream file(path.latin1());
    if (!file.is_open()) return true;
    int val = 1;
    if (file >> val) {
        return val != 0;
    }
    return true;
}

PerformanceGraphWidget::PerformanceGraphWidget(TQWidget* parent)
    : TQWidget(parent),
      m_graphType(GraphTypeCPUOverall),
      m_deviceIndex(0),
      m_hoverIndex(-1),
      m_hoverX(-1),
      m_hoverCore(-1),
      m_hoverSubGraph(-1)
{
    setMouseTracking(true);
    setBackgroundMode(TQt::NoBackground);

    // Color definitions matching the GTK version
    m_cpuColor = TQColor(90, 122, 138);       // #5A7A8A
    m_cpuFillColor = TQColor(229, 235, 243);   // #E5EBF3

    m_ramColor = TQColor(139, 18, 174);       // #8B12AE
    m_ramFillColor = TQColor(238, 230, 241);   // #EEE6F1

    m_swapColor = TQColor(170, 70, 220);      // Purple-magenta
    m_swapFillColor = TQColor(245, 230, 250);

    m_diskColor = TQColor(77, 166, 12);        // #4DA60C
    m_diskFillColor = TQColor(205, 227, 168);  // #CDE3A8

    m_diskWriteColor = TQColor(255, 0, 0);     // Red
    m_diskWriteFillColor = TQColor(255, 200, 200);

    m_netColor = TQColor(12, 109, 166);       // #0C6DA6 (Rx - Download)
    m_netFillColor = TQColor(220, 235, 245);   // #DCEBF5

    m_netTxColor = TQColor(166, 12, 12);       // Dark Red (Tx - Upload)

    m_gpuColor = TQColor(46, 125, 50);         // #2E7D32
    m_gpuFillColor = TQColor(200, 230, 201);   // #C8E6C9
    m_gpuRenderColor = TQColor(255, 87, 34);   // #FF5722
    m_gpuRenderFillColor = TQColor(255, 235, 230);
    m_gpuVideoColor = TQColor(33, 150, 243);   // #2196F3
    m_gpuVideoFillColor = TQColor(225, 240, 255);
}

PerformanceGraphWidget::~PerformanceGraphWidget()
{
}

void PerformanceGraphWidget::setGraphType(GraphType type, int deviceIndex)
{
    if (m_graphType != type || m_deviceIndex != deviceIndex) {
        m_graphType = type;
        m_deviceIndex = deviceIndex;
        m_hoverIndex = -1;
        m_hoverCore = -1;
        m_hoverSubGraph = -1;
        update();
    }
}

void PerformanceGraphWidget::refresh()
{
    update();
}

void PerformanceGraphWidget::mouseMoveEvent(TQMouseEvent* e)
{
    m_hoverX = e->x();
    
    int paddingLeft = 20;
    int paddingRight = 20;
    int paddingTop = 50;
    int paddingBottom = 2;
    TQRect graphRect(paddingLeft, paddingTop, width() - paddingLeft - paddingRight, height() - paddingTop - paddingBottom);

    if (m_graphType == GraphTypeCPULogical) {
        performance_data_bridge* perf = bridge_get_performance_data();
        int cores = perf->cpu_core_count;
        if (cores <= 0) cores = 1;

        int rows = 1, cols = 1;
        if (cores <= 2) { rows = 1; cols = cores; }
        else if (cores <= 4) { rows = 2; cols = 2; }
        else if (cores <= 8) { rows = 2; cols = 4; }
        else if (cores <= 12) { rows = 3; cols = 4; }
        else if (cores <= 16) { rows = 4; cols = 4; }
        else if (cores <= 32) { rows = 4; cols = 8; }
        else { rows = 8; cols = 8; }

        int coreW = graphRect.width() / cols;
        int coreH = graphRect.height() / rows;

        int cIdx = (e->x() - graphRect.left()) / coreW;
        int rIdx = (e->y() - graphRect.top()) / coreH;

        if (cIdx >= 0 && cIdx < cols && rIdx >= 0 && rIdx < rows) {
            int core = rIdx * cols + cIdx;
            if (core >= 0 && core < cores) {
                m_hoverCore = core;
                // Calculate local x position relative to the core's graph area
                int coreLeft = graphRect.left() + cIdx * coreW + 2;
                int coreWidth = coreW - 4;
                double pct = (double)(e->x() - coreLeft) / (double)coreWidth;
                m_hoverIndex = (int)(pct * 119.0);
                if (m_hoverIndex < 0) m_hoverIndex = 0;
                if (m_hoverIndex > 119) m_hoverIndex = 119;
            } else {
                m_hoverCore = -1;
                m_hoverIndex = -1;
            }
        } else {
            m_hoverCore = -1;
            m_hoverIndex = -1;
        }
    } else if (m_graphType == GraphTypeGPU) {
        m_hoverCore = -1;
        int vertical_margin = 20;
        int available_height = graphRect.height() - vertical_margin;
        int activity_graph_height = (available_height * 65) / 100;

        int topBottom = graphRect.top() + activity_graph_height;
        int bottomTop = topBottom + vertical_margin;

        int mouseY = e->y();
        int mouseX = e->x();
        if (mouseY >= graphRect.top() && mouseY <= topBottom) {
            m_hoverSubGraph = 0; // Top graph (Overall)
        } else if (mouseY >= bottomTop && mouseY <= graphRect.bottom()) {
            int half_width = (graphRect.width() - 10) / 2;
            int render_right = graphRect.left() + half_width;
            int video_left = render_right + 10;
            if (mouseX >= graphRect.left() && mouseX <= render_right) {
                m_hoverSubGraph = 1; // Bottom left (Render)
            } else if (mouseX >= video_left && mouseX <= graphRect.right()) {
                m_hoverSubGraph = 2; // Bottom right (Video)
            } else {
                m_hoverSubGraph = -1;
            }
        } else {
            m_hoverSubGraph = -1;
        }

        int startX = graphRect.left();
        int endX = graphRect.right();
        if (m_hoverSubGraph == 1) {
            int half_width = (graphRect.width() - 10) / 2;
            startX = graphRect.left();
            endX = graphRect.left() + half_width;
        } else if (m_hoverSubGraph == 2) {
            int half_width = (graphRect.width() - 10) / 2;
            startX = graphRect.left() + half_width + 10;
            endX = graphRect.right();
        }

        if (m_hoverSubGraph >= 0 && m_hoverX >= startX && m_hoverX <= endX) {
            double pct = (double)(m_hoverX - startX) / (double)(endX - startX);
            m_hoverIndex = (int)(pct * 119.0);
            if (m_hoverIndex < 0) m_hoverIndex = 0;
            if (m_hoverIndex > 119) m_hoverIndex = 119;
        } else {
            m_hoverIndex = -1;
            m_hoverSubGraph = -1;
        }
    } else if (m_graphType == GraphTypeRAM || m_graphType == GraphTypeDisk) {
        m_hoverCore = -1;
        int topBottom = 0;
        int bottomTop = 0;
        if (m_graphType == GraphTypeDisk) {
            disk_info_t* disk = get_disk_info(m_deviceIndex);
            if (disk && !isDiskIostatsEnabled(TQString(disk->name))) {
                m_hoverIndex = -1;
                m_hoverSubGraph = -1;
                update();
                return;
            }
            int totalH = graphRect.height() - 24;
            int topH = (totalH * 2) / 3;
            topBottom = graphRect.top() + topH;
            bottomTop = graphRect.top() + topH + 24;
        } else {
            int splitH = graphRect.height() / 2 - 12;
            topBottom = graphRect.top() + splitH;
            bottomTop = graphRect.top() + splitH + 24;
        }

        int mouseY = e->y();
        if (mouseY >= graphRect.top() && mouseY <= topBottom) {
            m_hoverSubGraph = 0; // Top graph
        } else if (mouseY >= bottomTop && mouseY <= graphRect.bottom()) {
            m_hoverSubGraph = 1; // Bottom graph
        } else {
            m_hoverSubGraph = -1;
        }

        int startX = graphRect.left();
        int endX = graphRect.right();
        if (m_hoverSubGraph >= 0 && m_hoverX >= startX && m_hoverX <= endX) {
            double pct = (double)(m_hoverX - startX) / (double)(endX - startX);
            m_hoverIndex = (int)(pct * 119.0);
            if (m_hoverIndex < 0) m_hoverIndex = 0;
            if (m_hoverIndex > 119) m_hoverIndex = 119;
        } else {
            m_hoverIndex = -1;
            m_hoverSubGraph = -1;
        }
    } else {
        m_hoverCore = -1;
        m_hoverSubGraph = -1;
        int startX = graphRect.left();
        int endX = graphRect.right();
        if (m_hoverX >= startX && m_hoverX <= endX) {
            double pct = (double)(m_hoverX - startX) / (double)(endX - startX);
            m_hoverIndex = (int)(pct * 119.0);
            if (m_hoverIndex < 0) m_hoverIndex = 0;
            if (m_hoverIndex > 119) m_hoverIndex = 119;
        } else {
            m_hoverIndex = -1;
        }
    }
    update();
}

void PerformanceGraphWidget::leaveEvent(TQEvent* e)
{
    (void)e;
    m_hoverIndex = -1;
    m_hoverCore = -1;
    m_hoverSubGraph = -1;
    update();
}

void PerformanceGraphWidget::mouseDoubleClickEvent(TQMouseEvent* e)
{
    if (e->button() == TQt::LeftButton) {
        emit doubleClicked();
        e->accept();
    } else {
        TQWidget::mouseDoubleClickEvent(e);
    }
}

void PerformanceGraphWidget::drawGrid(TQPainter& p, const TQRect& r, int hLines, int vLines)
{
    p.setPen(TQColor(230, 232, 236));
    
    // Horizontal lines
    double hSpacing = (double)r.height() / (double)(hLines + 1);
    for (int i = 1; i <= hLines; ++i) {
        int y = r.top() + (int)(hSpacing * (double)i);
        p.drawLine(r.left(), y, r.right(), y);
    }

    // Vertical lines
    double vSpacing = (double)r.width() / (double)(vLines + 1);
    for (int i = 1; i <= vLines; ++i) {
        int x = r.left() + (int)(vSpacing * (double)i);
        p.drawLine(x, r.top(), x, r.bottom());
    }
}

void PerformanceGraphWidget::drawSingleGraph(TQPainter& p, const TQRect& r, GraphType type, int deviceIdx, bool isSubCore, int coreIdx)
{
    performance_data_bridge* perf = bridge_get_performance_data();
    int current_index = perf->current_index;
    bool buffer_full = (perf->perf_flags & 0x01) != 0; // PERF_DATA_BUFFER_FULL = 0x01
    int samples_count = buffer_full ? PERFORMANCE_SAMPLES_COUNT : current_index;

    bool useAA = (bridge_get_app_flags() & APP_FLAG_ENABLE_ANTIALIASING) != 0;

    // Draw border around the graph area
    if (!useAA) {
        p.setPen(TQColor(180, 180, 180));
        p.setBrush(TQt::NoBrush);
        p.drawRect(r);

        if (samples_count <= 1) {
            drawGrid(p, r, 9, 9);
            return;
        }
    } else {
        if (samples_count <= 1) {
            p.setPen(TQColor(180, 180, 180));
            p.setBrush(TQt::NoBrush);
            p.drawRect(r);
            drawGrid(p, r, 9, 9);
            return;
        }
    }

    // Determine colors and samples
    TQColor strokeColor = m_cpuColor;
    TQColor fillColor = m_cpuFillColor;
    int max_val = 100;
    bool autoScale = false;

    const int* data_int1 = NULL;
    const int* data_int2 = NULL; // Optional second curve
    const gint16* data_gint16 = NULL;
    const gint16* data_gint16_sub = NULL;

    switch (type) {
        case GraphTypeCPUOverall:
            data_int1 = (const int*)perf->cpu_samples;
            strokeColor = m_cpuColor;
            fillColor = m_cpuFillColor;
            break;
        case GraphTypeCPULogical:
            if (isSubCore && perf->cpu_core_samples && coreIdx < perf->cpu_core_count) {
                data_gint16 = (const gint16*)perf->cpu_core_samples[coreIdx];
            } else {
                data_int1 = (const int*)perf->cpu_samples;
            }
            strokeColor = m_cpuColor;
            fillColor = m_cpuFillColor;
            break;
        case GraphTypeRAM:
            data_int1 = (const int*)perf->ram_samples;
            strokeColor = m_ramColor;
            fillColor = m_ramFillColor;
            break;
        case GraphTypeDisk: {
            disk_info_t* disk = get_disk_info(deviceIdx);
            if (disk) {
                // If it is the disk main graph, we show active time
                // If it is the disk read/write graph (subgraph), we show throughput
                if (isSubCore) { // Let's use isSubCore to mean Read/Write throughput graph
                    data_int1 = (const int*)disk->read_samples;
                    data_int2 = (const int*)disk->write_samples;
                    autoScale = true;
                    strokeColor = m_netColor; // Blue for read
                    fillColor = TQColor(230, 240, 255);
                } else {
                    data_int1 = (const int*)disk->activity_samples;
                    strokeColor = m_diskColor;
                    fillColor = m_diskFillColor;
                }
            }
            break;
        }
        case GraphTypeNetwork: {
            network_info_t* net = get_network_info(deviceIdx);
            if (net) {
                data_int1 = (const int*)net->rx_samples;
                data_int2 = (const int*)net->tx_samples;
                autoScale = true;
                strokeColor = m_netColor; // Blue for Rx
                fillColor = m_netFillColor;
            }
            break;
        }
        case GraphTypeGPU:
            data_gint16 = (const gint16*)perf->gpu_total_samples;
            strokeColor = m_gpuColor;
            fillColor = m_gpuFillColor;
            break;
        case GraphTypeGPURender:
            data_gint16 = (const gint16*)perf->gpu_render_samples;
            strokeColor = m_gpuRenderColor;
            fillColor = m_gpuRenderFillColor;
            break;
        case GraphTypeGPUVideo:
            data_gint16 = (const gint16*)perf->gpu_video_samples;
            strokeColor = m_gpuVideoColor;
            fillColor = m_gpuVideoFillColor;
            break;
    }

    if (!data_int1 && !data_gint16) {
        drawGrid(p, r, 9, 9);
        return;
    }

    // Calculate auto-scale for throughput graphs
    if (autoScale) {
        int max_found = 100; // Minimum scale: 100 KB/s
        for (int i = 0; i < samples_count; i++) {
            int idx = buffer_full ? (current_index + i) % PERFORMANCE_SAMPLES_COUNT : i;
            int val1 = data_int1 ? data_int1[idx] : 0;
            int val2 = data_int2 ? data_int2[idx] : 0;
            if (val1 > max_found) max_found = val1;
            if (val2 > max_found) max_found = val2;
        }
        // Round to power of 2 for nice visual step increments
        max_val = 100;
        while (max_val < max_found) max_val *= 2;
    }

    // Draw Grid
    if (!useAA) {
        drawGrid(p, r, 9, 9);
    }

    double width_scale = (double)r.width() / (double)(PERFORMANCE_SAMPLES_COUNT - 1);
    double height_scale = (double)r.height() / (double)max_val;

    double x_offset = 0.0;
    if (bridge_get_app_flags() & APP_FLAG_SMOOTH_SCROLLING) {
        x_offset = PerformanceTab::scrollOffset();
    }

    // Draw Curve 1 (Fill + Stroke)
    TQPointArray pts1(samples_count);
    for (int i = 0; i < samples_count; i++) {
        int idx = buffer_full ? (current_index + i) % PERFORMANCE_SAMPLES_COUNT : i;
        int val = data_int1 ? data_int1[idx] : data_gint16[idx];
        if (val > max_val) val = max_val;
        if (val < 0) val = 0;

        int x = r.left() + (int)(width_scale * (double)(i + (PERFORMANCE_SAMPLES_COUNT - samples_count) - x_offset));
        int y = r.bottom() - (int)(height_scale * (double)val);
        pts1.setPoint(i, x, y);
    }

    p.save();
    p.setClipRect(r);

    if (useAA && r.width() > 0 && r.height() > 0) {
        // Offscreen pixmap rendering for standard TQPainter operations
        TQPixmap pix(r.width(), r.height());
        pix.fill(palette().color(TQPalette::Active, TQColorGroup::Background));

        TQPainter pixPainter(&pix);
        pixPainter.translate(-r.left(), -r.top());

        // 1. Draw Grid on the pixmap
        drawGrid(pixPainter, r, 9, 9);

        // 2. Draw Fill 1 on the pixmap
        pixPainter.setPen(TQt::NoPen);
        pixPainter.setBrush(fillColor);
        TQPointArray fillPts1(samples_count + 2);
        for (int i = 0; i < samples_count; i++) {
            fillPts1.setPoint(i, pts1[i].x(), pts1[i].y());
        }
        fillPts1.setPoint(samples_count, pts1[samples_count - 1].x(), r.bottom());
        fillPts1.setPoint(samples_count + 1, pts1[0].x(), r.bottom());
        pixPainter.drawPolygon(fillPts1);

        pixPainter.end();

        // 3. Convert to TQImage for pixel-level AA blending
        TQImage img = pix.convertToImage();

        // 4. Draw antialiased stroke 1 onto the image
        TQPointArray pts1_aa(samples_count);
        for (int i = 0; i < samples_count; i++) {
            pts1_aa.setPoint(i, pts1[i].x() - r.left(), pts1[i].y() - r.top());
        }
        TQtAAPainter::drawPolylineAA(&img, pts1_aa.data(), samples_count, strokeColor, 2);

        // 5. Draw antialiased stroke 2 if present
        if (data_int2 || data_gint16_sub) {
            TQPointArray pts2(samples_count);
            for (int i = 0; i < samples_count; i++) {
                int idx = buffer_full ? (current_index + i) % PERFORMANCE_SAMPLES_COUNT : i;
                int val = data_int2 ? data_int2[idx] : data_gint16_sub[idx];
                if (val > max_val) val = max_val;
                if (val < 0) val = 0;

                int x = r.left() + (int)(width_scale * (double)(i + (PERFORMANCE_SAMPLES_COUNT - samples_count) - x_offset));
                int y = r.bottom() - (int)(height_scale * (double)val);
                pts2.setPoint(i, x, y);
            }

            TQPointArray pts2_aa(samples_count);
            for (int i = 0; i < samples_count; i++) {
                pts2_aa.setPoint(i, pts2[i].x() - r.left(), pts2[i].y() - r.top());
            }
            TQtAAPainter::drawPolylineAA(&img, pts2_aa.data(), samples_count,
                                         data_int2 ? m_diskWriteColor : m_gpuVideoColor, 2);
        }

        // 6. Draw the offscreen image onto the main painter
        p.drawImage(r.left(), r.top(), img);

        // 7. Draw border around the graph area on p
        p.setPen(TQColor(180, 180, 180));
        p.setBrush(TQt::NoBrush);
        p.drawRect(r);
    } else {
        // Draw Fill 1 (legacy)
        p.setPen(TQt::NoPen);
        p.setBrush(fillColor);
        TQPointArray fillPts1(samples_count + 2);
        for (int i = 0; i < samples_count; i++) {
            fillPts1.setPoint(i, pts1[i].x(), pts1[i].y());
        }
        fillPts1.setPoint(samples_count, pts1[samples_count - 1].x(), r.bottom());
        fillPts1.setPoint(samples_count + 1, pts1[0].x(), r.bottom());
        p.drawPolygon(fillPts1);

        // Draw Line 1 (legacy)
        p.setPen(TQPen(strokeColor, 2));
        p.setBrush(TQt::NoBrush);
        for (int i = 0; i < samples_count - 1; i++) {
            p.drawLine(pts1[i], pts1[i+1]);
        }

        // Draw Line 2 (legacy)
        if (data_int2 || data_gint16_sub) {
            TQPointArray pts2(samples_count);
            for (int i = 0; i < samples_count; i++) {
                int idx = buffer_full ? (current_index + i) % PERFORMANCE_SAMPLES_COUNT : i;
                int val = data_int2 ? data_int2[idx] : data_gint16_sub[idx];
                if (val > max_val) val = max_val;
                if (val < 0) val = 0;

                int x = r.left() + (int)(width_scale * (double)(i + (PERFORMANCE_SAMPLES_COUNT - samples_count) - x_offset));
                int y = r.bottom() - (int)(height_scale * (double)val);
                pts2.setPoint(i, x, y);
            }

            p.setPen(TQPen(data_int2 ? m_diskWriteColor : m_gpuVideoColor, 2));
            for (int i = 0; i < samples_count - 1; i++) {
                p.drawLine(pts2[i], pts2[i+1]);
            }
        }
    }

    p.restore();

    // Draw Axis Labels outside the graph boundaries
    if (type != GraphTypeCPULogical && type != GraphTypeGPURender && type != GraphTypeGPUVideo) {
        p.setPen(TQColor(130, 130, 130));
        p.save();
        TQFont f = p.font();
        f.setPointSize(f.pointSize() - 1);
        p.setFont(f);

        TQString maxLabel, minLabel;
        if (autoScale) {
            if (max_val >= 1024) {
                maxLabel = TQString("%1 MB/s").arg((double)max_val / 1024.0, 0, 'f', 1);
            } else {
                maxLabel = TQString("%1 KB/s").arg(max_val);
            }
            minLabel = "0 KB/s";
        } else {
            maxLabel = TQString("%1%").arg(max_val);
            minLabel = "0%";
        }

        int maxW = p.fontMetrics().width(maxLabel);
        int minW = p.fontMetrics().width(minLabel);

        // Draw maxLabel above the top-right corner of the graph (y=32 on overall graph)
        p.drawText(r.right() - maxW, r.top() - 8, maxLabel);
        // Draw minLabel inside the bottom-right corner of the graph
        p.drawText(r.right() - minW - 4, r.bottom() - 4, minLabel);
        p.restore();
    }
}

void PerformanceGraphWidget::drawTooltip(TQPainter& p, const TQRect& r)
{
    if (m_hoverIndex < 0) return;

    if (m_graphType == GraphTypeDisk) {
        disk_info_t* disk = get_disk_info(m_deviceIndex);
        if (disk && !isDiskIostatsEnabled(TQString(disk->name))) {
            return;
        }
    }

    performance_data_bridge* perf = bridge_get_performance_data();
    int current_index = perf->current_index;
    bool buffer_full = (perf->perf_flags & 0x01) != 0;
    int samples_count = buffer_full ? PERFORMANCE_SAMPLES_COUNT : current_index;

    // Check if hoverIndex is within active samples
    int active_samples = samples_count;
    int pad = PERFORMANCE_SAMPLES_COUNT - active_samples;
    if (m_hoverIndex < pad) return;

    int idx = buffer_full ? (current_index + m_hoverIndex) % PERFORMANCE_SAMPLES_COUNT : m_hoverIndex - pad;

    // Calculate coreRect/tooltipRect if m_graphType == GraphTypeCPULogical or split graphs
    TQRect tooltipRect = r;
    if (m_graphType == GraphTypeCPULogical && m_hoverCore >= 0) {
        int cores = perf->cpu_core_count;
        if (cores <= 0) cores = 1;

        int rows = 1, cols = 1;
        if (cores <= 2) { rows = 1; cols = cores; }
        else if (cores <= 4) { rows = 2; cols = 2; }
        else if (cores <= 8) { rows = 2; cols = 4; }
        else if (cores <= 12) { rows = 3; cols = 4; }
        else if (cores <= 16) { rows = 4; cols = 4; }
        else if (cores <= 32) { rows = 4; cols = 8; }
        else { rows = 8; cols = 8; }

        int coreW = r.width() / cols;
        int coreH = r.height() / rows;

        int rIdx = m_hoverCore / cols;
        int cIdx = m_hoverCore % cols;
        tooltipRect = TQRect(r.left() + cIdx * coreW + 2,
                             r.top() + rIdx * coreH + 2,
                             coreW - 4, coreH - 4);
    } else if (m_graphType == GraphTypeGPU && m_hoverSubGraph >= 0) {
        int vertical_margin = 20;
        int available_height = r.height() - vertical_margin;
        int activity_graph_height = (available_height * 65) / 100;
        int render_video_graph_height = available_height - activity_graph_height;

        int topBottom = r.top() + activity_graph_height;
        int bottomTop = topBottom + vertical_margin;
        int half_width = (r.width() - 10) / 2;

        if (m_hoverSubGraph == 0) {
            tooltipRect = TQRect(r.left(), r.top(), r.width(), activity_graph_height);
        } else if (m_hoverSubGraph == 1) {
            tooltipRect = TQRect(r.left(), bottomTop, half_width, render_video_graph_height);
        } else if (m_hoverSubGraph == 2) {
            tooltipRect = TQRect(r.left() + half_width + 10, bottomTop, half_width, render_video_graph_height);
        }
    } else if ((m_graphType == GraphTypeRAM || m_graphType == GraphTypeDisk) && m_hoverSubGraph >= 0) {
        if (m_graphType == GraphTypeDisk) {
            int totalH = r.height() - 24;
            int topH = (totalH * 2) / 3;
            int bottomH = totalH - topH;
            if (m_hoverSubGraph == 0) {
                tooltipRect = TQRect(r.left(), r.top(), r.width(), topH);
            } else {
                tooltipRect = TQRect(r.left(), r.top() + topH + 24, r.width(), bottomH);
            }
        } else {
            int splitH = r.height() / 2 - 12;
            if (m_hoverSubGraph == 0) {
                tooltipRect = TQRect(r.left(), r.top(), r.width(), splitH);
            } else {
                tooltipRect = TQRect(r.left(), r.top() + splitH + 24, r.width(), splitH);
            }
        }
    }

    // Retrieve values
    TQString text;
    switch (m_graphType) {
        case GraphTypeCPUOverall:
        case GraphTypeCPULogical: {
            if (m_graphType == GraphTypeCPULogical && m_hoverCore >= 0 && m_hoverCore < perf->cpu_core_count) {
                if (perf->cpu_core_samples && perf->cpu_core_samples[m_hoverCore]) {
                    int pct = perf->cpu_core_samples[m_hoverCore][idx];
                    text = TQString("CPU %1: %2%").arg(m_hoverCore).arg(pct);
                } else {
                    int pct = perf->cpu_samples[idx];
                    text = TQString("CPU: %1%").arg(pct);
                }
            } else {
                int pct = perf->cpu_samples[idx];
                text = TQString("CPU: %1%").arg(pct);
            }
            break;
        }
        case GraphTypeRAM: {
            if (m_hoverSubGraph == 1) {
                int pct = perf->swap_samples[idx];
                text = TQString("Swap: %1%").arg(pct);
            } else {
                int pct = perf->ram_samples[idx];
                text = TQString("RAM: %1%").arg(pct);
            }
            break;
        }
        case GraphTypeDisk: {
            disk_info_t* disk = get_disk_info(m_deviceIndex);
            if (disk) {
                if (m_hoverSubGraph == 0) {
                    int pct = disk->activity_samples[idx];
                    text = TQString("Disk Active: %1%").arg(pct);
                } else {
                    int read_kbs = disk->read_samples[idx];
                    int write_kbs = disk->write_samples[idx];
                    text = TQString("R: %1 KB/s\nW: %2 KB/s").arg(read_kbs).arg(write_kbs);
                }
            }
            break;
        }
        case GraphTypeNetwork: {
            network_info_t* net = get_network_info(m_deviceIndex);
            if (net) {
                int rx = net->rx_samples[idx];
                int tx = net->tx_samples[idx];
                text = TQString("Rx: %1 KB/s\nTx: %2 KB/s").arg(rx).arg(tx);
            }
            break;
        }
        case GraphTypeGPU: {
            if (m_hoverSubGraph == 0) {
                int pct = perf->gpu_total_samples[idx];
                text = TQString("GPU: %1%").arg(pct);
            } else if (m_hoverSubGraph == 1) {
                int render = perf->gpu_render_samples[idx];
                text = TQString("Render: %1%").arg(render);
            } else if (m_hoverSubGraph == 2) {
                int video = perf->gpu_video_samples[idx];
                text = TQString("Video: %1%").arg(video);
            }
            break;
        }
    }

    if (text.isEmpty()) return;

    // Draw vertical dotted line at hover position
    p.setPen(TQPen(TQColor(128, 128, 128), 1, TQt::DashLine));
    int lineX = m_hoverX;
    if (lineX < tooltipRect.left()) lineX = tooltipRect.left();
    if (lineX > tooltipRect.right()) lineX = tooltipRect.right();
    p.drawLine(lineX, tooltipRect.top(), lineX, tooltipRect.bottom());

    // Draw floating tooltip card
    p.setFont(font());
    int cardW = 120;
    int cardH = text.contains("\n") ? 38 : 22;
    int cardX = lineX + 10;
    int cardY = tooltipRect.top() + 10;

    // Shift card to left if it would go outside graph boundary
    if (cardX + cardW > tooltipRect.right()) {
        cardX = lineX - cardW - 10;
    }
    if (cardY + cardH > r.bottom()) {
        cardY = r.bottom() - cardH - 5;
    }
    if (cardY < r.top()) {
        cardY = r.top();
    }

    p.setPen(TQColor(100, 100, 100));
    p.setBrush(TQColor(255, 255, 225)); // Light yellow tooltip bg
    p.drawRect(cardX, cardY, cardW, cardH);

    p.setPen(TQColor(0, 0, 0));
    if (text.contains("\n")) {
        TQStringList lines = TQStringList::split("\n", text);
        p.drawText(cardX + 8, cardY + 14, lines[0]);
        p.drawText(cardX + 8, cardY + 30, lines[1]);
    } else {
        p.drawText(cardX + 8, cardY + 15, text);
    }
}

void PerformanceGraphWidget::paintEvent(TQPaintEvent* e)
{
    (void)e;
    if (width() <= 0 || height() <= 0) return;

    // Create an offscreen double-buffer pixmap for the entire widget
    TQPixmap buffer(width(), height());
    
    // Draw everything onto the buffer pixmap
    TQPainter p(&buffer);

    // Clear whole area with window background color
    p.fillRect(rect(), palette().color(TQPalette::Active, TQColorGroup::Background));

    // Margins - paddingTop is now 40px to make space for top titles
    int paddingLeft = 20; // 20px is enough since labels are inside the graph now!
    int paddingRight = 20;
    int paddingTop = 50;
    int paddingBottom = 2;

    TQRect graphRect(paddingLeft, paddingTop, width() - paddingLeft - paddingRight, height() - paddingTop - paddingBottom);

    // Draw Top-Left Title and Subtitle, and Top-Right Details
    TQString titleText, subtitleText, detailsText;
    
    switch (m_graphType) {
        case GraphTypeCPUOverall:
        case GraphTypeCPULogical: {
            titleText = "CPU";
            subtitleText = "% Utilization";
            detailsText = TQString(bridge_get_cpu_model_name());
            break;
        }
        case GraphTypeRAM: {
            titleText = "RAM";
            subtitleText = "Memory usage";
            detailsText = TQString("%1 GB").arg(bridge_get_installed_ram_gib(), 0, 'f', 1);
            break;
        }
        case GraphTypeDisk: {
            disk_info_t* disk = get_disk_info(m_deviceIndex);
            if (disk) {
                titleText = TQString("Disk %1").arg(m_deviceIndex);
                subtitleText = "Active time";
                detailsText = TQString(disk->display_name);
            } else {
                titleText = "Disk";
                subtitleText = "Active time";
            }
            break;
        }
        case GraphTypeNetwork: {
            network_info_t* net = get_network_info(m_deviceIndex);
            if (net) {
                interface_details_t details;
                memset(&details, 0, sizeof(details));
                get_interface_details(net->name, &details);
                titleText = details.type == INTERFACE_TYPE_WIFI ? "Wi-Fi" : "Ethernet";
                subtitleText = "Throughput";
                detailsText = TQString(net->description);
            } else {
                titleText = "Network";
                subtitleText = "Throughput";
            }
            break;
        }
        case GraphTypeGPU: {
            titleText = "GPU";
            subtitleText = "GPU utilization";
            detailsText = TQString(bridge_get_gpu_model_name());
            break;
        }
    }

    // Draw Top Title
    p.save();
    p.setPen(TQColor(0, 0, 0));
    TQFont fTitle = p.font();
    fTitle.setPointSize(fTitle.pointSize() + 3);
    fTitle.setBold(true);
    p.setFont(fTitle);
    p.drawText(paddingLeft, 18, titleText);
    p.restore();

    // Draw Top Subtitle (stacked directly below the title)
    p.save();
    p.setPen(TQColor(100, 100, 100));
    TQFont fSub = p.font();
    fSub.setPointSize(fSub.pointSize() - 1);
    p.setFont(fSub);
    p.drawText(paddingLeft, 42, subtitleText);
    p.restore();

    // Draw Top Right Details (bold and slightly larger CPU Model)
    if (!detailsText.isEmpty()) {
        p.save();
        p.setPen(TQColor(0, 0, 0)); // Black text matching GTK3
        TQFont fDetails = p.font();
        fDetails.setPointSize(fDetails.pointSize() + 2); // Larger CPU model text
        fDetails.setBold(true); // Bold
        p.setFont(fDetails);
        int detW = p.fontMetrics().width(detailsText);
        p.drawText(width() - paddingRight - detW, 18, detailsText); // Aligned with the title!
        p.restore();
    }

    if (m_graphType == GraphTypeCPULogical) {
        // Draw grid of logical cores
        performance_data_bridge* perf = bridge_get_performance_data();
        int cores = perf->cpu_core_count;
        if (cores <= 0) cores = 1;

        int rows = 1, cols = 1;
        if (cores <= 2) { rows = 1; cols = cores; }
        else if (cores <= 4) { rows = 2; cols = 2; }
        else if (cores <= 8) { rows = 2; cols = 4; }
        else if (cores <= 12) { rows = 3; cols = 4; }
        else if (cores <= 16) { rows = 4; cols = 4; }
        else if (cores <= 32) { rows = 4; cols = 8; }
        else { rows = 8; cols = 8; }

        int coreW = graphRect.width() / cols;
        int coreH = graphRect.height() / rows;

        for (int core = 0; core < cores; ++core) {
            int rIdx = core / cols;
            int cIdx = core % cols;
            TQRect coreRect(graphRect.left() + cIdx * coreW + 2,
                            graphRect.top() + rIdx * coreH + 2,
                            coreW - 4, coreH - 4);
            drawSingleGraph(p, coreRect, GraphTypeCPULogical, 0, true, core);
        }
    } else if (m_graphType == GraphTypeRAM) {
        // Split vertically: RAM overall top, Swap bottom
        int splitH = graphRect.height() / 2 - 12;
        
        TQRect ramRect(graphRect.left(), graphRect.top(), graphRect.width(), splitH);
        TQRect swapRect(graphRect.left(), graphRect.top() + splitH + 24, graphRect.width(), splitH);

        drawSingleGraph(p, ramRect, GraphTypeRAM, 0, false);

        p.setPen(TQColor(0, 0, 0));
        p.setFont(font());
        p.drawText(graphRect.left(), swapRect.top() - 4, "Swap Usage");
        drawSingleGraph(p, swapRect, GraphTypeRAM, 0, true);
    } else if (m_graphType == GraphTypeDisk) {
        disk_info_t* disk = get_disk_info(m_deviceIndex);
        if (disk && !isDiskIostatsEnabled(TQString(disk->name))) {
            // Draw placeholder frame for disabled disk performance counters
            p.save();
            p.setPen(TQColor(220, 222, 226));
            p.setBrush(TQColor(250, 250, 250)); // Light grey/white placeholder bg
            p.drawRect(graphRect);

            p.setPen(TQColor(60, 60, 60));
            TQFont fBold = p.font();
            fBold.setBold(true);
            fBold.setPointSize(fBold.pointSize() + 1);
            p.setFont(fBold);
            TQString msgTitle = "Disk performance counters are disabled";
            int wTitle = p.fontMetrics().width(msgTitle);
            p.drawText(graphRect.left() + (graphRect.width() - wTitle) / 2,
                       graphRect.top() + graphRect.height() / 2 - 10,
                       msgTitle);

            p.setPen(TQColor(120, 120, 120));
            TQFont fReg = p.font();
            fReg.setBold(false);
            p.setFont(fReg);
            TQString msgSub = TQString("To enable, run: echo 1 | sudo tee /sys/block/%1/queue/iostats").arg(disk->name);
            int wSub = p.fontMetrics().width(msgSub);
            p.drawText(graphRect.left() + (graphRect.width() - wSub) / 2,
                       graphRect.top() + graphRect.height() / 2 + 15,
                       msgSub);
            p.restore();
        } else {
            // Split vertically: Disk active top, Disk read/write bottom (2:1 split ratio)
            int totalH = graphRect.height() - 24;
            int topH = (totalH * 2) / 3;
            int bottomH = totalH - topH;

            TQRect actRect(graphRect.left(), graphRect.top(), graphRect.width(), topH);
            TQRect ioRect(graphRect.left(), graphRect.top() + topH + 24, graphRect.width(), bottomH);

            drawSingleGraph(p, actRect, GraphTypeDisk, m_deviceIndex, false);

            p.setPen(TQColor(0, 0, 0));
            p.setFont(font());
            p.drawText(graphRect.left(), ioRect.top() - 4, "Disk Transfer Rate (Read/Write)");
            drawSingleGraph(p, ioRect, GraphTypeDisk, m_deviceIndex, true);
        }
    } else if (m_graphType == GraphTypeGPU) {
        // Split vertically: GPU overall top (65% height), GPU Render & Video bottom (35% height) side-by-side (50% width each)
        int vertical_margin = 20;
        int available_height = graphRect.height() - vertical_margin;
        int activity_graph_height = (available_height * 65) / 100;
        int render_video_graph_height = available_height - activity_graph_height;

        TQRect actRect(graphRect.left(), graphRect.top(), graphRect.width(), activity_graph_height);
        
        int render_graph_y = graphRect.top() + activity_graph_height + vertical_margin;
        int half_width = (graphRect.width() - 10) / 2;
        
        TQRect renderRect(graphRect.left(), render_graph_y, half_width, render_video_graph_height);
        TQRect videoRect(graphRect.left() + half_width + 10, render_graph_y, half_width, render_video_graph_height);

        // Draw top graph (Overall GPU Activity)
        drawSingleGraph(p, actRect, GraphTypeGPU, 0, false);

        // Draw labels for bottom graphs
        p.save();
        p.setPen(TQColor(100, 100, 100));
        p.setFont(font());
        p.drawText(renderRect.left(), renderRect.top() - 4, "Render");
        p.drawText(videoRect.left(), videoRect.top() - 4, "Video");
        p.restore();

        // Draw bottom graphs (Render and Video)
        drawSingleGraph(p, renderRect, GraphTypeGPURender, 0, false);
        drawSingleGraph(p, videoRect, GraphTypeGPUVideo, 0, false);
    } else {
        // Single large overall graph (CPU overall, Network rx/tx)
        drawSingleGraph(p, graphRect, m_graphType, m_deviceIndex, false);
    }

    // Draw hover tooltip over the entire graph rect
    drawTooltip(p, graphRect);

    p.end();

    // Now blit the buffer to the screen in a single call
    TQPainter screenPainter(this);
    screenPainter.drawPixmap(0, 0, buffer);
}

#include "performance_graph_widget.moc"
