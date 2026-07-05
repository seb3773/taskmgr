#ifndef TQTAAPAINTER_H
#define TQTAAPAINTER_H

#include <ntqcolor.h>
#include <ntqpainter.h>
#include <ntqimage.h>
#include <ntqpoint.h>

class TQtAAPainter {
public:
    static void setAnalyticThresholdPx(float px);
    static float analyticThresholdPx();

    static void fillRoundRectAA(TQImage* img,
                               int x, int y,
                               int w, int h,
                               int r,
                               const TQColor& fg,
                               int segments);

    static void drawRoundRectAA(TQImage* img,
                               int x, int y,
                               int w, int h,
                               int r,
                               const TQColor& fg,
                               int segments,
                               int width);

    static void drawLineAA(TQImage* img,
                           int x0, int y0,
                           int x1, int y1,
                           const TQColor& fg,
                           int width);

    static void drawPolylineAA(TQImage* img,
                               const TQPoint* pts,
                               int n,
                               const TQColor& fg,
                               int width);

    static void drawEllipseAA(TQImage* img,
                              int cx, int cy,
                              int rx, int ry,
                              const TQColor& fg,
                              int segments);

    static void drawCircleAA(TQImage* img,
                             int cx, int cy,
                             int r,
                             const TQColor& fg,
                             int segments);

    static void fillCircleAA(TQImage* img,
                             int cx, int cy,
                             int r,
                             const TQColor& fg);

    static void fillPolygonAA(TQImage* img,
                              const TQPoint* pts,
                              int n,
                              const TQColor& fg);

    static void fillPolygonAA(TQImage* img,
                              const TQPoint* pts,
                              int n,
                              const TQColor& fg,
                              int outlineWidth);

    static void fillAnnularArcAA(TQImage* img,
                                 int cx, int cy,
                                 int rOuter,
                                 int rInner,
                                 double startDeg,
                                 double spanDeg,
                                 const TQColor& fg,
                                 int segments);

    static void drawArcAA(TQImage* img,
                          int cx, int cy,
                          int r,
                          double startDeg,
                          double spanDeg,
                          const TQColor& fg,
                          int segments,
                          int width);

    static void drawLineAA(TQPainter* p,
                           int x0, int y0,
                           int x1, int y1,
                           const TQColor& fg,
                           const TQColor& bg,
                           int width);

    static void drawPolylineAA(TQPainter* p,
                               const TQPoint* pts,
                               int n,
                               const TQColor& fg,
                               const TQColor& bg,
                               int width);

    static void drawEllipseAA(TQPainter* p,
                              int cx, int cy,
                              int rx, int ry,
                              const TQColor& fg,
                              const TQColor& bg,
                              int segments);

    static void drawCircleAA(TQPainter* p,
                             int cx, int cy,
                             int r,
                             const TQColor& fg,
                             const TQColor& bg,
                             int segments);

    static void fillCircleAA(TQPainter* p,
                             int cx, int cy,
                             int r,
                             const TQColor& fg);
};

#endif
