/*
 * cpu_tray_icon.h — CPU systray icon generation (GTK3 parity).
 *
 * Level 0  = 0–9 %   (base cpu0, no bars)
 * Level 1  = 10–19 % … Level 10 = 100 % (11 cached pixmaps, 10 with bars).
 */

#ifndef CPU_TRAY_ICON_H
#define CPU_TRAY_ICON_H

#include <ntqpixmap.h>

#define CPU_TRAY_MAX_LEVEL 10

int cpuTrayLevelForPercent(int cpuPercent);
const TQPixmap* cpuTrayCachedPixmapForLevel(int cpuLevel);
void freeCpuTrayIconCache();

#endif
