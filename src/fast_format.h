#ifndef FAST_FORMAT_H
#define FAST_FORMAT_H

#include <stdint.h>

/**
 * Formatage optimisé manuel - remplace sprintf/snprintf pour les cas fréquents
 * Ces fonctions sont 20-40x plus rapides que sprintf pour les patterns simples
 * Utilisées dans les boucles de refresh pour améliorer les performances
 */

// Formatage PID : "12345"
void format_pid(char* buffer, uint32_t pid);

// Formatage pourcentage CPU : "XX%" ou "X%"  
void format_cpu_percentage(char* buffer, uint32_t percentage);

// Formatage priorité : "-20" à "19"
void format_priority(char* buffer, int32_t priority);

// Formatage nom avec compteur : "firefox (3)"
void format_name_with_count(char* buffer, const char* name, uint32_t count);

// Formatage icône CPU : "cpu0" à "cpu10"
void format_cpu_icon_name(char* buffer, uint32_t cpu_level);

// Formatage temps CPU : "H:MM:SS"
void format_cpu_time_optimized(char* buffer, uint64_t hours, uint64_t minutes, uint64_t seconds);

// Formatage taille mémoire : "1.2 GB", "345 MB", "123 KB", "456 B"
void format_memory_size(char* buffer, uint64_t bytes);

// Formatage entier 32-bit : "12345"
void format_uint32(char* buffer, uint32_t value);

// Helpers pour patterns fréquents (remplace g_strdup_printf)
// Formatage float + unité : "12.3 GB", "4.5 MB/s", etc.
void format_float_gb(char* buffer, double value);          // "%.1f GB"
void format_float_mb(char* buffer, double value);          // "%.1f MB"
void format_float_ghz(char* buffer, double value);         // "%.2f GHz"
void format_int_kb(char* buffer, int value);               // "%d KB"
void format_int_kb_s(char* buffer, int value);             // "%d KB/s"
void format_float_mb_s(char* buffer, double value);        // "%.1f MB/s"
void format_int_mhz(char* buffer, int value);              // "%d MHz"
void format_percentage_simple(char* buffer, int value);    // "%d%%"
void format_ulong_gb(char* buffer, unsigned long value);   // "%lu GB"

// Lookup table partagée pour optimisation digits (exposée pour réutilisation)
extern const char digit_pairs[200];

// Utilitaires internes
static inline void reverse_string(char* start, char* end);
static inline char* uint_to_string(char* buffer, uint32_t value);
static inline char* int_to_string(char* buffer, int32_t value);

#endif // FAST_FORMAT_H
