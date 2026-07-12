/*
 * app_icons.cpp — Load embedded qxtask / qxtask_root window icons.
 */

#include "app_icons.h"
#include "taskmgr_icons.h"

#include <ntqimage.h>
#include <ntqpainter.h>

static TQPixmap loadEmbeddedIcon(const unsigned char *data, size_t size)
{
    TQImage img;
    if (!img.loadFromData(data, (int)size, "PNG"))
        return TQPixmap();
    return TQPixmap(img);
}

static TQPixmap loadEmbeddedIconScaled(const unsigned char *data, size_t size,
                                       int width, int height)
{
    TQImage img;
    if (!img.loadFromData(data, (int)size, "PNG"))
        return TQPixmap();
    return TQPixmap(img.smoothScale(width, height));
}

TQPixmap appNormalWindowIcon()
{
    return loadEmbeddedIcon(qxtask_data, qxtask_size);
}

TQPixmap appRootWindowIcon()
{
    TQImage baseImg;
    if (!baseImg.loadFromData(qxtask_data, (int)qxtask_size, "PNG"))
        return TQPixmap();

    TQImage overlayImg;
    if (!overlayImg.loadFromData(root_data, (int)root_size, "PNG"))
        return TQPixmap(baseImg);

    // Convert both to 32-bit depth to allow direct ARGB pixel blending
    if (baseImg.depth() != 32)
        baseImg = baseImg.convertDepth(32);
    if (overlayImg.depth() != 32)
        overlayImg = overlayImg.convertDepth(32);

    int w = baseImg.width();
    int h = baseImg.height();
    int ow = overlayImg.width();
    int oh = overlayImg.height();

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (x < ow && y < oh) {
                TQRgb pixelOverlay = overlayImg.pixel(x, y);
                int aOverlay = tqAlpha(pixelOverlay);
                if (aOverlay == 255) {
                    baseImg.setPixel(x, y, pixelOverlay);
                } else if (aOverlay > 0) {
                    TQRgb pixelBase = baseImg.pixel(x, y);
                    int aBase = tqAlpha(pixelBase);
                    int rBase = tqRed(pixelBase);
                    int gBase = tqGreen(pixelBase);
                    int bBase = tqBlue(pixelBase);

                    int rOverlay = tqRed(pixelOverlay);
                    int gOverlay = tqGreen(pixelOverlay);
                    int bOverlay = tqBlue(pixelOverlay);

                    int outA = aOverlay + aBase * (255 - aOverlay) / 255;
                    if (outA > 0) {
                        int outR = (rOverlay * aOverlay + rBase * aBase * (255 - aOverlay) / 255) / outA;
                        int outG = (gOverlay * aOverlay + gBase * aBase * (255 - aOverlay) / 255) / outA;
                        int outB = (bOverlay * aOverlay + bBase * aBase * (255 - aOverlay) / 255) / outA;
                        baseImg.setPixel(x, y, tqRgba(outR, outG, outB, outA));
                    }
                }
            }
        }
    }

    return TQPixmap(baseImg);
}

TQPixmap embeddedRunIcon()
{
    return loadEmbeddedIcon(run_data, run_size);
}

TQPixmap embeddedTuxmgrAboutIcon()
{
    return loadEmbeddedIconScaled(tuxmgr_data, tuxmgr_size, 65, 65);
}
