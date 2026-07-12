#ifndef MINI_BLOCK_WIDGET_H
#define MINI_BLOCK_WIDGET_H

#include <ntqwidget.h>
#include <ntqstring.h>
#include <ntqcolor.h>
#include <ntqpoint.h>

class MiniBlockWidget : public TQWidget {
    TQ_OBJECT
public:
    enum Type {
        TypeCPU = 0,
        TypeRAM = 1,
        TypeDisk = 2,
        TypeNetwork = 3,
        TypeGPU = 4
    };

    MiniBlockWidget(Type type, int deviceIndex = 0, TQWidget* parent = 0);
    virtual ~MiniBlockWidget();

    Type type() const { return m_type; }
    int deviceIndex() const { return m_deviceIndex; }

    bool isSelected() const { return m_selected; }
    void setSelected(bool sel);

    static bool hideGraphs() { return s_hideGraphs; }
    static void setHideGraphs(bool hide);
    void updateHeight();

    void refresh();

signals:
    void clicked(MiniBlockWidget* widget);

protected:
    void paintEvent(TQPaintEvent* e);
    void mousePressEvent(TQMouseEvent* e);
    void enterEvent(TQEvent* e);
    void leaveEvent(TQEvent* e);

private:
    Type m_type;
    int m_deviceIndex;
    bool m_selected;
    bool m_hovered;

    TQColor m_themeColor;
    TQColor m_themeFillColor;
    TQColor m_frameColor;

    TQString m_cachedTitle;
    TQString m_cachedValue;

    TQString m_line1;
    TQString m_line2;
    TQString m_line3;

    static bool s_hideGraphs;

    void updateLabels(TQString &title, TQString &value);
    void drawMiniGraph(TQPainter& p, const TQRect& r);
    void updateThemeColors();
};

#endif // MINI_BLOCK_WIDGET_H
