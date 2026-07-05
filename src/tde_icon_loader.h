/*
 * tde_icon_loader.h — Trinity icon loading (KSysGuard / KIconLoader style).
 *
 * Requires TDEApplication to be initialized before use.
 */

#ifndef TDE_ICON_LOADER_H
#define TDE_ICON_LOADER_H

#include <ntqpixmap.h>
#include <ntqstring.h>

class TdeIconLoader {
public:
    static const int ListIconSize = 22;

    static TQPixmap loadSmallIcon(const TQString& iconName, bool userType = true);
    static TQPixmap processIcon(const TQString& processName);
    static TQPixmap windowIcon(const TQString& wmClass, const TQString& wmInstance,
                               const TQString& processName);
    static bool hasApplicationIcon(const TQString& processName);
    static TQPixmap autostartIcon(const TQString& execName, const TQString& desktopPath = TQString::null);
    static TQPixmap namedIcon(const TQString& iconName, const TQString& fallback = TQString::null);

    /* Skip per-process KIconLoader lookups during startup light refresh */
    static void setStartupDeferral(bool defer);
    static bool startupDeferral();
    static TQPixmap placeholderIcon();

private:
    static TQString normalizeKey(const TQString& name);
    static TQString resolveProcessAlias(const TQString& key);
    static TQString readDesktopIconField(const TQString& desktopPath);
    static TQString findDesktopIconByWmClass(const TQString& wmClass);
    static TQPixmap scaleToListSize(const TQPixmap& pix);
    static TQPixmap defaultExecutableIcon();
    static bool isUndesiredGenericIcon(const TQPixmap& pix);
};

#endif
