#ifndef INFO_BLOCK_WIDGET_H
#define INFO_BLOCK_WIDGET_H

#include <ntqwidget.h>
#include <ntqlabel.h>
#include <ntqlayout.h>
#include <ntqvaluevector.h>
#include "backend_bridge.h"

class InfoBlockWidget : public TQWidget {
    TQ_OBJECT
public:
    enum Type {
        TypeCPU = 0,
        TypeRAM = 1,
        TypeDisk = 2,
        TypeNetwork = 3,
        TypeGPU = 4
    };

    InfoBlockWidget(TQWidget* parent = 0);
    virtual ~InfoBlockWidget();

    void setType(Type type, int deviceIndex = 0);

    void refresh();

private:
    Type m_type;
    int m_deviceIndex;

    TQHBoxLayout* m_mainLayout;
    TQWidget* m_leftWidget;
    TQWidget* m_rightWidget;
    TQGridLayout* m_leftLayout;
    TQGridLayout* m_rightLayout;

    struct LeftMetric {
        TQLabel* titleLabel;
        TQLabel* valueLabel;
    };
    TQValueVector<LeftMetric> m_leftMetrics;

    struct RightSpec {
        TQLabel* keyLabel;
        TQLabel* valLabel;
    };
    TQValueVector<RightSpec> m_rightSpecs;

    void clearLayout();
    void addLeftMetric(int row, int col, const TQString& title, const TQString& value);
    void addRightSpec(int row, const TQString& key, const TQString& val);
    void setupCPU();
    void setupRAM();
    void setupDisk();
    void setupNetwork();
    void setupGPU();

    void updateCPU();
    void updateRAM();
    void updateDisk();
    void updateNetwork();
    void updateGPU();
};

#endif // INFO_BLOCK_WIDGET_H
