/*
 * cpu_tray_icon.cpp — CPU systray icon generation.
 *
 * Layering: cpu_bg (optional recolor) + cpu0 overlay, cached once per bg setting.
 * Utilization bars drawn once per level per segment color, cached in s_cpuIconCache.
 */

#include "cpu_tray_icon.h"
#include "taskmgr_icons.h"

#include <ntqimage.h>
#include <ntqsettings.h>

static TQPixmap* s_cpuIconCache[CPU_TRAY_MAX_LEVEL + 1];
static bool s_cpuIconCacheValid[CPU_TRAY_MAX_LEVEL + 1];

static TQImage* s_cpuBgSource = 0;
static TQImage* s_cpuOverlaySource = 0;
static bool s_rawImagesLoaded = false;

static TQImage* s_cpuCompositeBase = 0;
static bool s_cpuCompositeBaseValid = false;
static TQString s_cachedBgTintKey;

static TQString s_cachedSegmentColorKey;

static TQColor cpuTraySegmentColor()
{
    TQSettings settings;
    const bool useCustom = settings.readBoolEntry("/taskmgr/systrayCustomCpuColor", false);
    if (useCustom)
        return TQColor(settings.readEntry("/taskmgr/systrayCpuColor", "#56BFFE"));
    return TQColor("#56BFFE");
}

static TQString cpuTraySegmentColorKey()
{
    TQSettings settings;
    const bool useCustom = settings.readBoolEntry("/taskmgr/systrayCustomCpuColor", false);
    if (!useCustom)
        return TQString("default");
    return settings.readEntry("/taskmgr/systrayCpuColor", "#56BFFE");
}

static TQString cpuTrayBgTintKey()
{
    TQSettings settings;
    const bool useCustom = settings.readBoolEntry("/taskmgr/systrayCustomBgTint", false);
    if (!useCustom)
        return TQString("default");
    return settings.readEntry("/taskmgr/systrayBgTintColor", "#808080");
}

static TQColor cpuTrayBgTintColor()
{
    TQSettings settings;
    return TQColor(settings.readEntry("/taskmgr/systrayBgTintColor", "#808080"));
}

static TQImage loadEmbeddedPng(const unsigned char* data, size_t size)
{
    TQImage img;
    if (!img.loadFromData(data, (int)size, "PNG"))
        return TQImage();

    if (img.depth() != 32)
        img = img.convertDepth(32);
    img.setAlphaBuffer(true);
    return img;
}

static bool loadRawImages()
{
    if (s_rawImagesLoaded)
        return s_cpuBgSource && s_cpuOverlaySource
               && !s_cpuBgSource->isNull() && !s_cpuOverlaySource->isNull();

    s_rawImagesLoaded = true;

    TQImage bg = loadEmbeddedPng(cpu_bg_data, cpu_bg_size);
    TQImage overlay = loadEmbeddedPng(cpu0_data, cpu0_size);
    if (bg.isNull() || overlay.isNull())
        return false;

    s_cpuBgSource = new TQImage(bg);
    s_cpuOverlaySource = new TQImage(overlay);
    return true;
}

static TQImage recolorBgLayer(const TQImage& bg, const TQColor& color)
{
    TQImage img = bg.copy();
    const int r = color.red();
    const int g = color.green();
    const int b = color.blue();

    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const uint pixel = img.pixel(x, y);
            const int a = tqAlpha(pixel);
            if (a == 0)
                continue;
            img.setPixel(x, y, tqRgba(r, g, b, a));
        }
    }

    return img;
}

static TQImage compositeImages(const TQImage& bg, const TQImage& overlay)
{
    TQImage out = bg.copy();

    for (int y = 0; y < out.height() && y < overlay.height(); ++y) {
        for (int x = 0; x < out.width() && x < overlay.width(); ++x) {
            const uint overPx = overlay.pixel(x, y);
            const int aOver = tqAlpha(overPx);
            if (aOver == 0)
                continue;
            if (aOver == 255) {
                out.setPixel(x, y, overPx);
                continue;
            }

            const uint underPx = out.pixel(x, y);
            const int aUnder = tqAlpha(underPx);
            const int aOut = aOver + (aUnder * (255 - aOver)) / 255;
            if (aOut == 0)
                continue;

            const int r = (tqRed(overPx) * aOver + tqRed(underPx) * aUnder * (255 - aOver) / 255) / aOut;
            const int g = (tqGreen(overPx) * aOver + tqGreen(underPx) * aUnder * (255 - aOver) / 255) / aOut;
            const int b = (tqBlue(overPx) * aOver + tqBlue(underPx) * aUnder * (255 - aOver) / 255) / aOut;
            out.setPixel(x, y, tqRgba(r, g, b, aOut));
        }
    }

    return out;
}

static void invalidateLevelCaches()
{
    for (int i = 0; i <= CPU_TRAY_MAX_LEVEL; ++i) {
        delete s_cpuIconCache[i];
        s_cpuIconCache[i] = 0;
        s_cpuIconCacheValid[i] = false;
    }
    s_cachedSegmentColorKey = TQString::null;
}

static void invalidateCompositeBase()
{
    delete s_cpuCompositeBase;
    s_cpuCompositeBase = 0;
    s_cpuCompositeBaseValid = false;
    s_cachedBgTintKey = TQString::null;
    invalidateLevelCaches();
}

static const TQImage* ensureCompositeBase()
{
    const TQString bgKey = cpuTrayBgTintKey();
    if (s_cpuCompositeBaseValid && s_cpuCompositeBase && s_cachedBgTintKey == bgKey)
        return s_cpuCompositeBase;

    invalidateCompositeBase();

    if (!loadRawImages())
        return 0;

    TQImage bgLayer;
    if (bgKey == "default")
        bgLayer = s_cpuBgSource->copy();
    else
        bgLayer = recolorBgLayer(*s_cpuBgSource, cpuTrayBgTintColor());

    TQImage* composite = new TQImage(compositeImages(bgLayer, *s_cpuOverlaySource));
    s_cpuCompositeBase = composite;
    s_cpuCompositeBaseValid = true;
    s_cachedBgTintKey = bgKey;
    return s_cpuCompositeBase;
}

static void syncSegmentColorCache()
{
    const TQString segKey = cpuTraySegmentColorKey();
    if (s_cachedSegmentColorKey == segKey)
        return;
    invalidateLevelCaches();
    s_cachedSegmentColorKey = segKey;
}

static void drawCpuLevelBars(TQImage& img, int cpuLevel)
{
    if (cpuLevel <= 0)
        return;

    int numLines = (18 * cpuLevel + 9) / 10;
    if (numLines > 18) numLines = 18;

    const int startY = 20;
    const int xStart = 5;
    const int barWidth = 14;
    const TQColor barColor = cpuTraySegmentColor();
    const uint barPixel = tqRgb(barColor.red(), barColor.green(), barColor.blue());

    for (int line = 0; line < numLines; ++line) {
        int y = startY - line;
        if (y < 0 || y >= img.height())
            continue;
        for (int x = xStart; x < xStart + barWidth; ++x) {
            if (x < 0 || x >= img.width())
                continue;
            img.setPixel(x, y, barPixel);
        }
    }
}

static TQPixmap* ensureCachedLevel(int cpuLevel)
{
    if (cpuLevel < 0) cpuLevel = 0;
    if (cpuLevel > CPU_TRAY_MAX_LEVEL) cpuLevel = CPU_TRAY_MAX_LEVEL;

    syncSegmentColorCache();

    if (s_cpuIconCacheValid[cpuLevel] && s_cpuIconCache[cpuLevel])
        return s_cpuIconCache[cpuLevel];

    const TQImage* base = ensureCompositeBase();
    if (!base)
        return 0;

    TQImage img = base->copy();
    if (cpuLevel > 0)
        drawCpuLevelBars(img, cpuLevel);

    if (!s_cpuIconCache[cpuLevel])
        s_cpuIconCache[cpuLevel] = new TQPixmap(img);
    else
        *s_cpuIconCache[cpuLevel] = TQPixmap(img);

    s_cpuIconCacheValid[cpuLevel] = true;
    return s_cpuIconCache[cpuLevel];
}

int cpuTrayLevelForPercent(int cpuPercent)
{
    int cpuLevel = cpuPercent / 10;
    if (cpuLevel > CPU_TRAY_MAX_LEVEL) cpuLevel = CPU_TRAY_MAX_LEVEL;
    if (cpuLevel < 0) cpuLevel = 0;
    return cpuLevel;
}

const TQPixmap* cpuTrayCachedPixmapForLevel(int cpuLevel)
{
    return ensureCachedLevel(cpuLevel);
}

void freeCpuTrayIconCache()
{
    invalidateCompositeBase();

    delete s_cpuBgSource;
    s_cpuBgSource = 0;
    delete s_cpuOverlaySource;
    s_cpuOverlaySource = 0;
    s_rawImagesLoaded = false;
}
