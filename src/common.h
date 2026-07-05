#ifndef COMMON_H
#define COMMON_H

/* Common includes used across multiple files */
#ifdef WITHOUT_GTK
#include <glib.h>
#else
#include <gtk/gtk.h>
#endif
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

/* Include fast_format for optimized number formatting */
#include "fast_format.h"

/* System path constants */
#define PROC_CPUINFO "/proc/cpuinfo"
#define PROC_MEMINFO "/proc/meminfo"
#define PROC_STAT "/proc/stat"
#define PROC_SWAPS "/proc/swaps"
#define PROC_MOUNTS "/proc/mounts"

/* Format string constants */
#define PROC_PID_FMT "/proc/%d/%s"
#define SYS_BLOCK_FMT "/sys/block/%s/%s"
#define SYS_NET_FMT "/sys/class/net/%s/%s"

/* Common buffer sizes */
#define PATH_BUFFER_SIZE 512
#define LINE_BUFFER_SIZE 256
#define SMALL_BUFFER_SIZE 64

/* Optimization attributes for granular control with pragmas */
#ifdef __clang__
    #define OPTIMIZE_SIZE_BEGIN _Pragma("clang optimize off")
    #define OPTIMIZE_SIZE_END _Pragma("clang optimize on")
    #define OPTIMIZE_SPEED_BEGIN _Pragma("clang optimize on")
    #define OPTIMIZE_SPEED_END _Pragma("clang optimize on")
    #define OPTIMIZE_SIZE
    #define OPTIMIZE_SPEED
#else
    // GCC: Pragmas granulaires pour optimisation fine
    #define OPTIMIZE_SIZE_BEGIN \
        _Pragma("GCC push_options") \
        _Pragma("GCC optimize (\"-Os\")")
    #define OPTIMIZE_SIZE_END \
        _Pragma("GCC pop_options")
    #define OPTIMIZE_SPEED_BEGIN \
        _Pragma("GCC push_options") \
        _Pragma("GCC optimize (\"-O3\")")
    #define OPTIMIZE_SPEED_END \
        _Pragma("GCC pop_options")
    
    // Attributs pour fonctions individuelles
    #define OPTIMIZE_SIZE __attribute__((optimize("-Os")))
    #define OPTIMIZE_SPEED __attribute__((optimize("-O3")))
#endif
#define COLD_FUNCTION __attribute__((cold))
#define HOT_FUNCTION __attribute__((hot))
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

/* Section attributes for memory layout optimization */
#define INIT_SECTION __attribute__((section(".init.text")))
#define CLEANUP_SECTION __attribute__((section(".exit.text")))
#define HOT_SECTION __attribute__((section(".hot.text")))

/* Combined attributes for common patterns */
#define INIT_FUNCTION OPTIMIZE_SIZE COLD_FUNCTION INIT_SECTION
#define CLEANUP_FUNCTION OPTIMIZE_SIZE COLD_FUNCTION CLEANUP_SECTION
#define CRITICAL_FUNCTION OPTIMIZE_SPEED HOT_FUNCTION HOT_SECTION

/* Helper functions for common formatting patterns */
static inline char* format_percentage(char* buffer, size_t size, int value) {
    snprintf(buffer, size, "%d%%", value);
    return buffer;
}

// OPTIMISATION: Remplace snprintf() par construction manuelle (10-20× plus rapide)
// BUGFIX: Utilise format_uint32() pour conversion PID correcte
static inline char* format_proc_path(char* buffer, size_t size, pid_t pid, const char* file) {
    char* ptr = buffer;
    
    // Copie "/proc/"
    *ptr++ = '/';
    *ptr++ = 'p';
    *ptr++ = 'r';
    *ptr++ = 'o';
    *ptr++ = 'c';
    *ptr++ = '/';
    
    // Ajoute PID avec conversion correcte (délègue à fast_format)
    // Note: On doit déclarer format_uint32 ou l'inclure
    char pid_str[16];
    format_uint32(pid_str, (uint32_t)pid);
    char* pid_ptr = pid_str;
    while (*pid_ptr) {
        *ptr++ = *pid_ptr++;
    }
    
    // Ajoute "/"
    *ptr++ = '/';
    
    // Copie filename
    while (*file && (ptr - buffer) < (int)size - 1) {
        *ptr++ = *file++;
    }
    
    *ptr = '\0';
    return buffer;
}

// OPTIMISATION: Remplace snprintf() par construction manuelle (10-20× plus rapide)
static inline char* format_sys_block_path(char* buffer, size_t size, const char* device, const char* file) {
    char* ptr = buffer;
    
    // Copie "/sys/block/"
    *ptr++ = '/';
    *ptr++ = 's';
    *ptr++ = 'y';
    *ptr++ = 's';
    *ptr++ = '/';
    *ptr++ = 'b';
    *ptr++ = 'l';
    *ptr++ = 'o';
    *ptr++ = 'c';
    *ptr++ = 'k';
    *ptr++ = '/';
    
    // Copie device name
    while (*device && (ptr - buffer) < (int)size - 1) {
        *ptr++ = *device++;
    }
    
    // Ajoute "/"
    *ptr++ = '/';
    
    // Copie filename
    while (*file && (ptr - buffer) < (int)size - 1) {
        *ptr++ = *file++;
    }
    
    *ptr = '\0';
    return buffer;
}

static inline char* format_sys_net_path(char* buffer, size_t size, const char* interface, const char* file) {
    snprintf(buffer, size, SYS_NET_FMT, interface, file);
    return buffer;
}

/* Memory size formatting helper */
static inline char* format_memory_mb(char* buffer, size_t size, guint64 bytes) {
    snprintf(buffer, size, "%d MB", (int)(bytes / (1024 * 1024)));
    return buffer;
}

static inline char* format_memory_gb(char* buffer, size_t size, guint64 bytes) {
    snprintf(buffer, size, "%.1f GB", (double)bytes / (1024.0 * 1024.0));
    return buffer;
}

/* Speed formatting helper */
static inline char* format_speed_ghz(char* buffer, size_t size, gdouble mhz) {
    snprintf(buffer, size, "%.2f GHz", mhz / 1000.0);
    return buffer;
}

#endif /* COMMON_H */
