#include "mini_block_widget.h"
#include "performance_tab.h"
#include "backend_bridge.h"
#include <ntqpainter.h>
#include <ntqstyle.h>

bool MiniBlockWidget::s_hideGraphs = false;

void MiniBlockWidget::setHideGraphs(bool hide)
{
    s_hideGraphs = hide;
}

void MiniBlockWidget::updateHeight()
{
    int h = s_hideGraphs ? 52 : 70;
    setMinimumSize(210, h);
    setMaximumSize(2000, h);
    resize(width(), h);
    update();
}

MiniBlockWidget::MiniBlockWidget(Type type, int deviceIndex, TQWidget* parent)
    : TQWidget(parent),
      m_type(type),
      m_deviceIndex(deviceIndex),
      m_selected(false),
      m_hovered(false)
{
    // Fix size for standard sidebar block (width: ~210px, height: ~70px)
    setMinimumSize(210, 70);
    setMaximumSize(2000, 70);

    // Assign theme colors matching Windows 10 Task Manager
    switch (m_type) {
        case TypeCPU:
            m_themeColor = TQColor(90, 122, 138);     // #5A7A8A
            m_themeFillColor = TQColor(229, 235, 243); // #E5EBF3
            m_frameColor = TQColor(195, 210, 223);
            break;
        case TypeRAM:
            m_themeColor = TQColor(139, 18, 174);     // #8B12AE
            m_themeFillColor = TQColor(238, 230, 241); // #EEE6F1
            m_frameColor = TQColor(220, 200, 230);
            break;
        case TypeDisk:
            m_themeColor = TQColor(77, 166, 12);      // #4DA60C
            m_themeFillColor = TQColor(205, 227, 168); // #CDE3A8
            m_frameColor = TQColor(205, 220, 195);
            break;
        case TypeNetwork:
            m_themeColor = TQColor(12, 109, 166);     // #0C6DA6
            m_themeFillColor = TQColor(220, 235, 245); // #DCEBF5
            m_frameColor = TQColor(195, 215, 230);
            break;
        case TypeGPU:
            m_themeColor = TQColor(46, 125, 50);      // #2E7D32
            m_themeFillColor = TQColor(200, 230, 201); // #C8E6C9
            m_frameColor = TQColor(195, 220, 195);
            break;
    }
    updateLabels(m_cachedTitle, m_cachedValue);
}

MiniBlockWidget::~MiniBlockWidget()
{
}

void MiniBlockWidget::refresh()
{
    updateLabels(m_cachedTitle, m_cachedValue);
    update();
}

void MiniBlockWidget::setSelected(bool sel)
{
    if (m_selected != sel) {
        m_selected = sel;
        update();
    }
}

void MiniBlockWidget::mousePressEvent(TQMouseEvent* e)
{
    (void)e;
    emit clicked(this);
}

void MiniBlockWidget::enterEvent(TQEvent* e)
{
    (void)e;
    m_hovered = true;
    update();
}

void MiniBlockWidget::leaveEvent(TQEvent* e)
{
    (void)e;
    m_hovered = false;
    update();
}

void MiniBlockWidget::updateLabels(TQString &title, TQString &value)
{
    performance_data_bridge* perf = bridge_get_performance_data();
    int current_index = perf->current_index;
    int last_idx = (current_index - 1 + PERFORMANCE_SAMPLES_COUNT) % PERFORMANCE_SAMPLES_COUNT;

    switch (m_type) {
        case TypeCPU: {
            title = "CPU";
            int pct = perf->cpu_samples[last_idx];
            double speed = get_cpu_speed();
            value = TQString("%1% %2 GHz").arg(pct).arg(speed, 0, 'f', 2);

            m_line1 = "CPU";
            m_line2 = "";
            m_line3 = value;
            break;
        }
        case TypeRAM: {
            title = "RAM";
            RAMInfoData* ram = get_ram_info_data();
            int pct = perf->ram_samples[last_idx];
            value = TQString("%1/%2 GB (%3%)")
                .arg(ram->ram_in_use_gb, 0, 'f', 1)
                .arg(ram->ram_available_gb, 0, 'f', 1)
                .arg(pct);

            m_line1 = "RAM";
            m_line2 = "";
            m_line3 = value;
            break;
        }
        case TypeDisk: {
            disk_info_t* disk = get_disk_info(m_deviceIndex);
            if (disk) {
                title = TQString("DISK %1").arg(m_deviceIndex);
                int pct = disk->activity_samples[last_idx];
                value = TQString("%1% (%2)").arg(pct).arg(disk->name);

                m_line1 = title;
                m_line2 = disk->name;
                m_line3 = TQString("%1%").arg(pct);
            } else {
                title = "DISK";
                value = "0%";

                m_line1 = "DISK";
                m_line2 = "";
                m_line3 = "0%";
            }
            break;
        }
        case TypeNetwork: {
            network_info_t* net = get_network_info(m_deviceIndex);
            if (net) {
                title = "NET";
                double rx_kbs = net->rx_samples[last_idx];
                double tx_kbs = net->tx_samples[last_idx];
                double total_kbs = rx_kbs + tx_kbs;
                TQString speedStr = total_kbs > 1024 ? TQString("%1 Mbps").arg(total_kbs / 1024.0 * 8, 0, 'f', 1) : TQString("%1 Kbps").arg(total_kbs * 8, 0, 'f', 0);
                value = TQString("%1 (%2)").arg(speedStr).arg(net->name);

                m_line1 = "NET";
                m_line2 = net->name;
                m_line3 = speedStr;
            } else {
                title = "NET";
                value = "0 Kbps";

                m_line1 = "NET";
                m_line2 = "";
                m_line3 = "0 Kbps";
            }
            break;
        }
        case TypeGPU: {
            title = "GPU";
            int pct = perf->gpu_total_samples[last_idx];
            value = TQString("%1%").arg(pct);

            m_line1 = "GPU";
            m_line2 = "";
            m_line3 = value;
            break;
        }
    }
}

void MiniBlockWidget::drawMiniGraph(TQPainter& p, const TQRect& r)
{
    performance_data_bridge* perf = bridge_get_performance_data();
    int current_index = perf->current_index;
    bool buffer_full = (perf->perf_flags & 0x01) != 0; // PERF_DATA_BUFFER_FULL = 0x01
    int samples_count = buffer_full ? PERFORMANCE_SAMPLES_COUNT : current_index;

    // Draw graph frame with soft pastel border color
    p.setPen(m_frameColor);
    p.setBrush(TQColor(255, 255, 255));
    p.drawRect(r);

    if (samples_count <= 1) return;

    // Get the correct samples array based on type
    const int* data_int = NULL;
    const gint16* data_gint16 = NULL;
    int max_val = 100;

    switch (m_type) {
        case TypeCPU:
            data_int = (const int*)perf->cpu_samples;
            break;
        case TypeRAM:
            data_int = (const int*)perf->ram_samples;
            break;
        case TypeDisk: {
            disk_info_t* disk = get_disk_info(m_deviceIndex);
            if (disk) {
                data_int = (const int*)disk->activity_samples;
            }
            break;
        }
        case TypeNetwork: {
            network_info_t* net = get_network_info(m_deviceIndex);
            if (net) {
                data_int = (const int*)net->activity_samples;
            }
            break;
        }
        case TypeGPU:
            data_gint16 = (const gint16*)perf->gpu_total_samples;
            break;
    }

    if (!data_int && !data_gint16) return;

    // Calculate grid / coordinates
    double width_scale = (double)r.width() / (double)(PERFORMANCE_SAMPLES_COUNT - 1);
    double height_scale = (double)r.height() / (double)max_val;

    double x_offset = 0.0;
    if (bridge_get_app_flags() & APP_FLAG_SMOOTH_SCROLLING) {
        x_offset = PerformanceTab::scrollOffset();
    }

    TQPointArray pts(samples_count);
    for (int i = 0; i < samples_count; i++) {
        int idx = buffer_full ? (current_index + i) % PERFORMANCE_SAMPLES_COUNT : i;
        int val = data_int ? data_int[idx] : data_gint16[idx];
        if (val > max_val) val = max_val;
        if (val < 0) val = 0;

        // X flows left-to-right: newest is on the right
        int x = r.left() + (int)(width_scale * (double)(i + (PERFORMANCE_SAMPLES_COUNT - samples_count) - x_offset));
        int y = r.bottom() - (int)(height_scale * (double)val);
        pts.setPoint(i, x, y);
    }

    // Clip painting to graph rectangle so fill does not bleed outside
    p.save();
    p.setClipRect(r);

    // Draw Fill
    p.setPen(TQt::NoPen);
    p.setBrush(m_themeFillColor);
    TQPointArray fillPts(samples_count + 2);
    for (int i = 0; i < samples_count; i++) {
        fillPts.setPoint(i, pts[i].x(), pts[i].y());
    }
    fillPts.setPoint(samples_count, pts[samples_count - 1].x(), r.bottom());
    fillPts.setPoint(samples_count + 1, pts[0].x(), r.bottom());
    p.drawPolygon(fillPts);

    // Draw Line
    p.setPen(TQPen(m_themeColor, 1));
    p.setBrush(TQt::NoBrush);
    for (int i = 0; i < samples_count - 1; i++) {
        p.drawLine(pts[i], pts[i+1]);
    }

    p.restore();
}

void MiniBlockWidget::paintEvent(TQPaintEvent* e)
{
    (void)e;
    TQPainter p(this);

    // Background selection highlight
    if (m_selected) {
        p.fillRect(rect(), TQColor(179, 217, 255)); // Windows active blue #B3D9FF
    } else if (m_hovered) {
        p.fillRect(rect(), TQColor(242, 246, 252)); // Hover background
    } else {
        p.fillRect(rect(), TQColor(255, 255, 255)); // Clean white background
    }

    if (s_hideGraphs) {
        p.setPen(TQColor(0, 0, 0));
        p.setFont(font());

        if (m_type == TypeDisk || m_type == TypeNetwork) {
            // 3 lines layout
            // Line 1: Element Name (bold)
            p.save();
            TQFont f = p.font();
            f.setBold(true);
            p.setFont(f);
            p.drawText(12, 16, m_line1);
            p.restore();

            // Line 2: Designation (smaller, grey)
            p.save();
            TQFont f2 = p.font();
            f2.setPointSize(f2.pointSize() - 1);
            p.setFont(f2);
            p.setPen(TQColor(120, 120, 120));
            p.drawText(12, 31, m_line2);
            p.restore();

            // Line 3: Value/Usage (smaller, dark grey)
            p.save();
            TQFont f3 = p.font();
            f3.setPointSize(f3.pointSize() - 1);
            p.setFont(f3);
            p.setPen(TQColor(80, 80, 80));
            p.drawText(12, 46, m_line3);
            p.restore();
        } else {
            // 2 lines layout
            // Line 1: Element Name (bold)
            p.save();
            TQFont f = p.font();
            f.setBold(true);
            p.setFont(f);
            p.drawText(12, 22, m_line1);
            p.restore();

            // Line 2: Value/Usage (smaller, dark grey)
            p.save();
            TQFont f2 = p.font();
            f2.setPointSize(f2.pointSize() - 1);
            p.setFont(f2);
            p.setPen(TQColor(80, 80, 80));
            p.drawText(12, 40, m_line3);
            p.restore();
        }
    } else {
        // Draw Mini-Graph on the left (68x50 at x=12, y=10)
        TQRect graphRect(12, 10, 68, 50);
        drawMiniGraph(p, graphRect);

        p.setPen(TQColor(0, 0, 0));
        p.setFont(font());

        // Title label (bold)
        p.save();
        TQFont f = p.font();
        f.setBold(true);
        p.setFont(f);
        p.drawText(88, 28, m_cachedTitle);
        p.restore();

        // Value label (smaller, grey/dark-grey text)
        p.save();
        TQFont f2 = p.font();
        f2.setPointSize(f2.pointSize() - 1);
        p.setFont(f2);
        p.setPen(TQColor(80, 80, 80));
        p.drawText(88, 48, m_cachedValue);
        p.restore();
    }
}

#include "mini_block_widget.moc"
