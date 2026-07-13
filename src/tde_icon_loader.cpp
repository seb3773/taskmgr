/*
 * tde_icon_loader.cpp — Trinity icon loading (KSysGuard / KIconLoader style).
 *
 * Mirrors KSysGuard ProcessList icon resolution:
 *   aliases + TDEGlobal::iconLoader()->loadIcon(..., userType=true) + cache.
 */

#include "tde_icon_loader.h"

#include <tdeglobal.h>
#include <kiconloader.h>

#include <ntqimage.h>
#include <ntqmap.h>
#include <ntqfile.h>
#include <ntqdir.h>
#include <ntqstringlist.h>
#include <stdlib.h>

static bool s_startupDeferral = false;

void TdeIconLoader::setStartupDeferral(bool defer)
{
    s_startupDeferral = defer;
}

bool TdeIconLoader::startupDeferral()
{
    return s_startupDeferral;
}

TQPixmap TdeIconLoader::placeholderIcon()
{
    return defaultExecutableIcon();
}

static TQMap<TQString, TQString>& processAliases()
{
    static TQMap<TQString, TQString> aliases;
    if (!aliases.isEmpty())
        return aliases;

#ifdef Q_OS_LINUX
    aliases.insert("init", "penguin");
#else
    aliases.insert("init", "system");
#endif

    /* kernel threads */
    aliases.insert("bdflush", "kernel");
    aliases.insert("dhcpcd", "kernel");
    aliases.insert("kapm-idled", "kernel");
    aliases.insert("keventd", "kernel");
    aliases.insert("khubd", "kernel");
    aliases.insert("klogd", "kernel");
    aliases.insert("kreclaimd", "kernel");
    aliases.insert("kreiserfsd", "kernel");
    aliases.insert("ksoftirqd_CPU0", "kernel");
    aliases.insert("ksoftirqd_CPU1", "kernel");
    aliases.insert("ksoftirqd_CPU2", "kernel");
    aliases.insert("ksoftirqd_CPU3", "kernel");
    aliases.insert("ksoftirqd_CPU4", "kernel");
    aliases.insert("ksoftirqd_CPU5", "kernel");
    aliases.insert("ksoftirqd_CPU6", "kernel");
    aliases.insert("ksoftirqd_CPU7", "kernel");
    aliases.insert("kswapd", "kernel");
    aliases.insert("kupdated", "kernel");
    aliases.insert("mdrecoveryd", "kernel");

    /* daemons */
    aliases.insert("artsd", "daemon");
    aliases.insert("atd", "daemon");
    aliases.insert("cron", "daemon");
    aliases.insert("cupsd", "daemon");
    aliases.insert("sshd", "daemon");
    aliases.insert("syslogd", "daemon");

    /* Trinity services */
    aliases.insert("appletproxy", "tdeapp");
    aliases.insert("dcopserver", "tdeapp");
    aliases.insert("kded", "tdeapp");
    aliases.insert("tdeinit", "tdeapp");
    aliases.insert("kdesktop", "tdeapp");
    aliases.insert("tdesud", "tdeapp");
    aliases.insert("tdm", "tdeapp");
    aliases.insert("ksmserver", "tdeapp");
    aliases.insert("tdelauncher", "tdeapp");

    /* shells and tools */
    aliases.insert("bash", "shell");
    aliases.insert("sh", "shell");
    aliases.insert("zsh", "shell");
    aliases.insert("cat", "tools");
    aliases.insert("grep", "tools");
    aliases.insert("find", "tools");
    aliases.insert("sort", "tools");
    aliases.insert("su", "tools");
    aliases.insert("vi", "application-vnd.oasis.opendocument.text");
    aliases.insert("vim", "application-vnd.oasis.opendocument.text");
    aliases.insert("emacs", "application-vnd.oasis.opendocument.text");

    /* common desktop apps */
    aliases.insert("chrome", "google-chrome");
    aliases.insert("chrome_crashpad_handler", "google-chrome");
    aliases.insert("chromium", "chromium-browser");
    aliases.insert("firefox-esr", "firefox");
    aliases.insert("firefox-bin", "firefox");
    aliases.insert("soffice.bin", "libreoffice-startcenter");
    aliases.insert("taskmgr", "qxtask");
    aliases.insert("cursor", "cursor");

    return aliases;
}

TQString TdeIconLoader::normalizeKey(const TQString& name)
{
    TQString key = name.lower();

    int slash = key.findRev('/');
    if (slash >= 0)
        key = key.mid(slash + 1);

    int space = key.find(' ');
    if (space >= 0)
        key = key.left(space);

    return key;
}

TQString TdeIconLoader::resolveProcessAlias(const TQString& key)
{
    TQMap<TQString, TQString>& aliases = processAliases();
    if (aliases.contains(key))
        return aliases[key];
    return key;
}

TQPixmap TdeIconLoader::scaleToListSize(const TQPixmap& pix)
{
    if (pix.isNull())
        return pix;

    if (pix.width() == ListIconSize && pix.height() == ListIconSize)
        return pix;

    TQImage img = pix.convertToImage();
    TQPixmap scaled;
    scaled.convertFromImage(img.smoothScale(ListIconSize, ListIconSize));
    return scaled;
}

TQPixmap TdeIconLoader::defaultExecutableIcon()
{
    static TQPixmap cached;
    if (!cached.isNull())
        return cached;

    TQPixmap pix = TDEGlobal::iconLoader()->loadIcon(
        "application-x-executable", TDEIcon::Small, TDEIcon::SizeSmallMedium,
        TDEIcon::DefaultState, 0, false);

    if (pix.isNull() || !pix.mask()) {
        pix = TDEGlobal::iconLoader()->loadIcon(
            "exec", TDEIcon::Small, TDEIcon::SizeSmallMedium,
            TDEIcon::DefaultState, 0, false);
    }

    cached = scaleToListSize(pix);
    return cached;
}

bool TdeIconLoader::isUndesiredGenericIcon(const TQPixmap& pix)
{
    if (pix.isNull())
        return true;

    static TQPixmap unknownApp;
    if (unknownApp.isNull()) {
        unknownApp = scaleToListSize(TDEGlobal::iconLoader()->loadIcon(
            "unknownapp", TDEIcon::User, TDEIcon::SizeSmallMedium));
    }

    static TQPixmap genericText;
    if (genericText.isNull()) {
        genericText = scaleToListSize(TDEGlobal::iconLoader()->loadIcon(
            "text-x-generic", TDEIcon::Small, TDEIcon::SizeSmallMedium,
            TDEIcon::DefaultState, 0, false));
    }

    return pix.serialNumber() == unknownApp.serialNumber()
        || pix.serialNumber() == genericText.serialNumber()
        || pix.serialNumber() == defaultExecutableIcon().serialNumber();
}

static TQString readDesktopField(const TQString& desktopPath, const TQString& fieldName)
{
    if (desktopPath.isEmpty())
        return TQString();

    TQFile file(desktopPath);
    if (!file.open(IO_ReadOnly))
        return TQString();

    TQString prefix = fieldName + "=";
    TQString content = TQString::fromUtf8(file.readAll());
    file.close();

    TQStringList lines = TQStringList::split('\n', content);
    bool inEntry = false;
    for (TQStringList::ConstIterator it = lines.begin(); it != lines.end(); ++it) {
        TQString line = (*it).stripWhiteSpace();
        if (line.isEmpty() || line[0] == '#')
            continue;
        if (line == "[Desktop Entry]") {
            inEntry = true;
            continue;
        }
        if (line.startsWith("[") && inEntry)
            break;
        if (!inEntry)
            continue;
        if (line.startsWith(prefix))
            return line.mid(prefix.length()).stripWhiteSpace();
    }
    return TQString();
}

TQString TdeIconLoader::findDesktopIconByWmClass(const TQString& wmClass)
{
    static TQMap<TQString, TQString> cache;
    TQString key = wmClass.lower();
    if (key.isEmpty())
        return TQString();
    if (cache.contains(key))
        return cache[key];

    TQStringList searchDirs;
    const char* home = getenv("HOME");
    if (home)
        searchDirs << TQString(home) + "/.local/share/applications";
    searchDirs << "/usr/local/share/applications"
               << "/usr/share/applications"
               << "/opt/trinity/share/applications";

    TQString foundIcon;
    for (TQStringList::ConstIterator dirIt = searchDirs.begin();
         dirIt != searchDirs.end() && foundIcon.isEmpty(); ++dirIt) {
        TQDir dir(*dirIt, "*.desktop");
        const TQFileInfoList* files = dir.entryInfoList();
        if (!files)
            continue;
        for (TQFileInfoList::ConstIterator fit = files->begin(); fit != files->end(); ++fit) {
            TQFileInfo* fi = *fit;
            if (!fi)
                continue;
            TQString wmField = readDesktopField(fi->absFilePath(), "StartupWMClass");
            if (wmField.lower() != key)
                continue;
            foundIcon = readDesktopIconField(fi->absFilePath());
            if (!foundIcon.isEmpty())
                break;
        }
    }

    cache.insert(key, foundIcon);
    return foundIcon;
}

TQPixmap TdeIconLoader::windowIcon(const TQString& wmClass, const TQString& wmInstance,
                                   const TQString& processName)
{
    if (s_startupDeferral)
        return defaultExecutableIcon();

    static TQMap<TQString, TQPixmap> cache;
    TQString cacheKey = wmClass.lower() + "|" + wmInstance.lower() + "|" + normalizeKey(processName);
    if (cache.contains(cacheKey))
        return cache[cacheKey];

    TQPixmap pix = processIcon(processName);
    if (!isUndesiredGenericIcon(pix)) {
        cache.insert(cacheKey, pix);
        return pix;
    }

    if (!wmInstance.isEmpty()) {
        pix = processIcon(wmInstance);
        if (!isUndesiredGenericIcon(pix)) {
            cache.insert(cacheKey, pix);
            return pix;
        }
    }

    if (!wmClass.isEmpty()) {
        pix = processIcon(wmClass);
        if (!isUndesiredGenericIcon(pix)) {
            cache.insert(cacheKey, pix);
            return pix;
        }

        TQString desktopIcon = findDesktopIconByWmClass(wmClass);
        if (!desktopIcon.isEmpty()) {
            pix = loadSmallIcon(desktopIcon, true);
            if (!isUndesiredGenericIcon(pix)) {
                cache.insert(cacheKey, pix);
                return pix;
            }
        }
    }

    pix = processIcon(processName);
    cache.insert(cacheKey, pix);
    return pix;
}

TQPixmap TdeIconLoader::loadSmallIcon(const TQString& iconName, bool userType)
{
    static TQMap<TQString, TQPixmap> cache;
    if (iconName.isEmpty())
        return TQPixmap();

    TQString cacheKey = iconName.lower() + (userType ? TQString(":u") : TQString(":s"));
    if (cache.contains(cacheKey))
        return cache[cacheKey];

    TQPixmap pix = TDEGlobal::iconLoader()->loadIcon(
        iconName, TDEIcon::Small, TDEIcon::SizeSmallMedium,
        TDEIcon::DefaultState, 0, userType);

    if (pix.isNull() || !pix.mask())
        pix = defaultExecutableIcon();
    else
        pix = scaleToListSize(pix);

    cache.insert(cacheKey, pix);
    return pix;
}

bool TdeIconLoader::hasApplicationIcon(const TQString& processName)
{
    TQPixmap pix = processIcon(processName);
    if (pix.isNull())
        return false;
    return !isUndesiredGenericIcon(pix);
}

TQPixmap TdeIconLoader::processIcon(const TQString& processName)
{
    if (s_startupDeferral)
        return defaultExecutableIcon();

    static TQMap<TQString, TQPixmap> cache;

    TQString key = normalizeKey(processName);
    if (key.isEmpty())
        return TQPixmap();

    if (cache.contains(key))
        return cache[key];

    TQString iconName = resolveProcessAlias(key);
    TQPixmap pix = loadSmallIcon(iconName, true);

    if ((pix.isNull() || isUndesiredGenericIcon(pix)) && iconName != key)
        pix = loadSmallIcon(key, true);

    if (pix.isNull() || isUndesiredGenericIcon(pix))
        pix = defaultExecutableIcon();

    cache.insert(key, pix);
    return pix;
}

TQPixmap TdeIconLoader::autostartIcon(const TQString& execName, const TQString& desktopPath)
{
    static TQMap<TQString, TQPixmap> cache;

    TQString key = normalizeKey(execName.isEmpty() ? desktopPath : execName);
    TQString cacheKey = desktopPath + "|" + key;
    if (cache.contains(cacheKey))
        return cache[cacheKey];

    TQPixmap pix;
    if (!desktopPath.isEmpty()) {
        TQString desktopIcon = readDesktopIconField(desktopPath);
        if (!desktopIcon.isEmpty())
            pix = loadSmallIcon(desktopIcon, true);
    }

    if (pix.isNull() || isUndesiredGenericIcon(pix))
        pix = processIcon(execName.isEmpty() ? key : execName);

    cache.insert(cacheKey, pix);
    return pix;
}

TQString TdeIconLoader::readDesktopIconField(const TQString& desktopPath)
{
    if (desktopPath.isEmpty())
        return TQString();

    TQFile file(desktopPath);
    if (!file.open(IO_ReadOnly))
        return TQString();

    TQString content = TQString::fromUtf8(file.readAll());
    file.close();

    TQStringList lines = TQStringList::split('\n', content);
    bool inEntry = false;
    for (TQStringList::ConstIterator it = lines.begin(); it != lines.end(); ++it) {
        TQString line = (*it).stripWhiteSpace();
        if (line.isEmpty() || line[0] == '#')
            continue;
        if (line == "[Desktop Entry]") {
            inEntry = true;
            continue;
        }
        if (line.startsWith("[") && inEntry)
            break;
        if (!inEntry)
            continue;
        if (line.startsWith("Icon="))
            return line.mid(5).stripWhiteSpace();
    }
    return TQString();
}

TQPixmap TdeIconLoader::namedIcon(const TQString& iconName, const TQString& fallback)
{
    TQPixmap pix = loadSmallIcon(iconName, true);

    if ((pix.isNull() || isUndesiredGenericIcon(pix)) && !fallback.isNull())
        pix = loadSmallIcon(fallback, true);

    if (pix.isNull() || isUndesiredGenericIcon(pix))
        pix = defaultExecutableIcon();

    return pix;
}
