/*
 * app_icons.cpp — Load embedded qxtask / qxtask_root window icons.
 */

#include "app_icons.h"
#include "taskmgr_icons.h"

#include <ntqimage.h>

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
    return loadEmbeddedIcon(qxtask_root_data, qxtask_root_size);
}

TQPixmap embeddedRunIcon()
{
    return loadEmbeddedIcon(run_data, run_size);
}

TQPixmap embeddedTuxmgrAboutIcon()
{
    return loadEmbeddedIconScaled(tuxmgr_data, tuxmgr_size, 65, 65);
}
