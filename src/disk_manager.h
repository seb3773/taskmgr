#ifndef DISK_MANAGER_H
#define DISK_MANAGER_H

#include <glib.h>
#include "types.h"

#define MAX_DISKS 16
#define DISK_NAME_MAX 32
#define DISK_MODEL_MAX 128

typedef struct {
    // 64-bit fields first
    guint64 capacity_gb;                 // Capacité en GB
    
    // Arrays (largest to smallest for better packing)
    gint activity_samples[PERFORMANCE_SAMPLES_COUNT];  // Activité en %
    gint read_samples[PERFORMANCE_SAMPLES_COUNT];      // Lecture en KB/s
    gint write_samples[PERFORMANCE_SAMPLES_COUNT];     // Écriture en KB/s
    gchar model[DISK_MODEL_MAX];         // ex: "Samsung SSD 980 PRO"
    gchar display_name[DISK_MODEL_MAX];  // ex: "Samsung SSD 980 PRO (sda)"
    gchar name[DISK_NAME_MAX];           // ex: "sda", "nvme0n1"
    
    // Small fields at end
    guint8 flags;                        // DISK_IS_SYSTEM, DISK_HAS_SWAP
} disk_info_t;

typedef struct {
    // Large array first
    disk_info_t disks[MAX_DISKS];
    
    // Group integers together
    gint disk_count;
    gint current_index;
    gint selected_disk_index;  // Index du disque sélectionné pour le graphique principal
    
    // Small fields at end
    guint8 flags;                        // MANAGER_BUFFER_FULL
} disk_manager_t;

extern disk_manager_t disk_manager;

// Fonctions publiques
void init_disk_manager(void);
void cleanup_disk_manager(void);
void update_disk_data(void);
gboolean check_and_refresh_disk_list(void);  // Nouvelle fonction de détection
gint get_disk_count(void);
disk_info_t* get_disk_info(gint index);
disk_info_t* get_system_disk_info(void);
void set_selected_disk(gint index);
gint get_selected_disk_index(void);

// Fonctions utilitaires
gboolean is_physical_disk(const gchar* device_name);
gchar* get_disk_model_name(const gchar* device_name);
guint64 get_disk_capacity_gb_new(const gchar* device_name);
gboolean disk_has_swap(const gchar* device_name);

#endif // DISK_MANAGER_H
