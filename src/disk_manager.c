#include "common.h"
#include <sys/statvfs.h>
#include <mntent.h>
#include <ctype.h>
#include "disk_manager.h"
#include "functions.h"

// Custom function to strip whitespace (replaces g_strstrip)
static char* strip_whitespace(char* str) {
    char *end;
    
    // Trim leading space
    while(isspace((unsigned char)*str)) str++;
    
    if(*str == 0)  // All spaces?
        return str;
    
    // Trim trailing space
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    
    // Write new null terminator character
    end[1] = '\0';
    
    return str;
}

disk_manager_t disk_manager = {0};

// Variables statiques pour le calcul des taux I/O
static struct {
    gchar device[DISK_NAME_MAX];
    long prev_read_sectors;
    long prev_write_sectors;
    long prev_io_time;
    struct timespec prev_time;
    guint8 flags;  // DISK_INITIALIZED
} disk_stats[MAX_DISKS] = {0};

OPTIMIZE_SIZE_BEGIN
OPTIMIZE_SIZE COLD_FUNCTION
void init_disk_manager(void) {
    disk_manager.disk_count = 0;
    disk_manager.current_index = 0;
    disk_manager.flags &= ~MANAGER_BUFFER_FULL;
    disk_manager.selected_disk_index = -1; // Aucun disque sélectionné par défaut
    
    // Scanner /sys/block pour trouver tous les disques physiques
    DIR *dir = opendir("/sys/block");
    if (!dir) {
        g_warning("Cannot open /sys/block directory");
        return;
    }
    
    struct dirent *entry;
    gchar system_disk[64] = {0}; // Must be at least 64 bytes to match DEVICE_MAX in get_root_device
    
    // Détecter le disque système
    get_root_device(system_disk);
    if (system_disk[0] != '\0') {
        strip_partition_suffix(system_disk);
    }
    
    while ((entry = readdir(dir)) != NULL && disk_manager.disk_count < MAX_DISKS) {
        // Ignorer les entrées spéciales
        if (entry->d_name[0] == '.' || 
            strncmp(entry->d_name, "loop", 4) == 0 ||
            strncmp(entry->d_name, "ram", 3) == 0 ||
            strncmp(entry->d_name, "dm-", 3) == 0) {
            continue;
        }
        
        // Vérifier si c'est un disque physique
        if (is_physical_disk(entry->d_name)) {
            disk_info_t *disk = &disk_manager.disks[disk_manager.disk_count];
            
            // Copier le nom du disque
            g_strlcpy(disk->name, entry->d_name, DISK_NAME_MAX);
            
            // Obtenir le modèle
            gchar *model = get_disk_model_name(entry->d_name);
            if (model) {
                g_strlcpy(disk->model, model, DISK_MODEL_MAX);
                g_free(model);
            } else {
                g_snprintf(disk->model, DISK_MODEL_MAX, "Unknown Disk");
            }
            
            // Créer le nom d'affichage
            g_snprintf(disk->display_name, DISK_MODEL_MAX, "%s (%s)", 
                      disk->model, disk->name);
            
            // Obtenir la capacité
            disk->capacity_gb = get_disk_capacity_gb_new(entry->d_name);
            
            // Vérifier si c'est le disque système
            if (strcmp(disk->name, system_disk) == 0) {
                disk->flags |= DISK_IS_SYSTEM;
            } else {
                disk->flags &= ~DISK_IS_SYSTEM;
            }
            // Ne pas sélectionner automatiquement le disque système
            
            // Vérifier si ce disque contient du swap
            if (disk_has_swap(entry->d_name)) {
                disk->flags |= DISK_HAS_SWAP;
            } else {
                disk->flags &= ~DISK_HAS_SWAP;
            }
            
            // Initialiser les buffers de données
            memset(disk->activity_samples, 0, sizeof(disk->activity_samples));
            memset(disk->read_samples, 0, sizeof(disk->read_samples));
            memset(disk->write_samples, 0, sizeof(disk->write_samples));
            
            // Initialiser les statistiques pour ce disque
            g_strlcpy(disk_stats[disk_manager.disk_count].device, entry->d_name, DISK_NAME_MAX);
            disk_stats[disk_manager.disk_count].flags &= ~DISK_INITIALIZED;
            
            disk_manager.disk_count++;
        }
    }
    
    closedir(dir);
    
    // Initialization complete
}
OPTIMIZE_SIZE_END

// Comptage rapide des disques physiques dans /sys/block
OPTIMIZE_SIZE HOT_FUNCTION
static gint count_physical_disks(void) {
    DIR *dir = opendir("/sys/block");
    if (!dir) return -1;
    
    gint count = 0;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL) {
        // Ignorer les entrées spéciales (même logique que init)
        if (entry->d_name[0] == '.' || 
            strncmp(entry->d_name, "loop", 4) == 0 ||
            strncmp(entry->d_name, "ram", 3) == 0 ||
            strncmp(entry->d_name, "dm-", 3) == 0) {
            continue;
        }
        
        // Vérifier si c'est un disque physique
        if (is_physical_disk(entry->d_name)) {
            count++;
        }
    }
    
    closedir(dir);
    return count;
}

// Détection légère des changements de disques + re-scan si nécessaire
OPTIMIZE_SIZE HOT_FUNCTION
gboolean check_and_refresh_disk_list(void) {
    static gint last_check_count = -1;
    
    // Comptage rapide des disques actuels
    gint current_count = count_physical_disks();
    
    // Première exécution ou erreur
    if (UNLIKELY(last_check_count == -1)) {
        last_check_count = current_count;
        return FALSE;  // Pas de changement détecté
    }
    
    // Vérifier s'il y a eu un changement
    if (LIKELY(current_count == last_check_count)) {
        return FALSE;  // Pas de changement
    }
    
    // Changement détecté ! Re-scanner complètement
#ifdef DEBUG
    g_message("Disk change detected: %d -> %d disks", last_check_count, current_count);
#endif
    
    // Sauvegarder le disque sélectionné actuel
    gint old_selected = disk_manager.selected_disk_index;
    gchar old_selected_name[DISK_NAME_MAX] = {0};
    if (old_selected >= 0 && old_selected < disk_manager.disk_count) {
        g_strlcpy(old_selected_name, disk_manager.disks[old_selected].name, DISK_NAME_MAX);
    }
    
    // Réinitialiser complètement le gestionnaire de disques
    init_disk_manager();
    
    // Essayer de restaurer la sélection précédente
    if (old_selected_name[0] != '\0') {
        for (gint i = 0; i < disk_manager.disk_count; i++) {
            if (strcmp(disk_manager.disks[i].name, old_selected_name) == 0) {
                disk_manager.selected_disk_index = i;
                break;
            }
        }
    }
    
    // Si le disque sélectionné n'existe plus, sélectionner le disque système
    if (disk_manager.selected_disk_index == -1) {
        for (gint i = 0; i < disk_manager.disk_count; i++) {
            if (disk_manager.disks[i].flags & DISK_IS_SYSTEM) {
                disk_manager.selected_disk_index = i;
                break;
            }
        }
    }
    
    last_check_count = current_count;
    return TRUE;  // Changement détecté et traité
}

void cleanup_disk_manager(void) {
    // Rien à nettoyer pour l'instant
}

gboolean is_physical_disk(const gchar* device_name) {
    gchar path[256];
    struct stat st;
    
    // Vérifier si le périphérique a un répertoire device (disques physiques)
    g_snprintf(path, sizeof(path), "/sys/block/%s/device", device_name);
    if (stat(path, &st) != 0) {
        return FALSE;
    }
    
    // Exclure les partitions (qui ont des numéros à la fin)
    const gchar *p = device_name + strlen(device_name) - 1;
    while (p >= device_name && *p >= '0' && *p <= '9') {
        p--;
    }
    
    // Si on a trouvé des chiffres à la fin, c'est probablement une partition
    if (p < device_name + strlen(device_name) - 1) {
        // Exception pour nvme (nvme0n1 est un disque, nvme0n1p1 est une partition)
        if (strncmp(device_name, "nvme", 4) == 0) {
            // Vérifier si ça se termine par 'n' suivi d'un chiffre (ex: nvme0n1)
            if (p >= device_name && *p == 'n') {
                return TRUE;
            }
        }
        return FALSE;
    }
    
    return TRUE;
}

gchar* get_disk_model_name(const gchar* device_name) {
    gchar path[256];
    FILE *fp;
    gchar buffer[DISK_MODEL_MAX];
    
    // Essayer /sys/block/device/model
    g_snprintf(path, sizeof(path), "/sys/block/%s/device/model", device_name);
    if (read_sys_file_line(path, buffer, sizeof(buffer))) {
        if (strlen(buffer) > 0) {
            return g_strdup(buffer);
        }
    }
    
    // Fallback: essayer /sys/block/device/vendor + model
    g_snprintf(path, sizeof(path), "/sys/block/%s/device/vendor", device_name);
    gchar vendor[64] = {0};
    if (read_sys_file_line(path, vendor, sizeof(vendor))) {
        
        // Maintenant essayer le modèle
        g_snprintf(path, sizeof(path), "/sys/block/%s/device/model", device_name);
        gchar model[64] = {0};
        if (read_sys_file_line(path, model, sizeof(model))) {
            
            if (strlen(vendor) > 0 && strlen(model) > 0) {
                return g_strdup_printf("%s %s", vendor, model);
            } else if (strlen(model) > 0) {
                return g_strdup(model);
            }
        }
    }
    
    return NULL;
}

guint64 get_disk_capacity_gb_new(const gchar* device_name) {
    gchar path[256];
    guint64 sectors = 0;
    guint64 capacity_gb = 0;
    
    g_snprintf(path, sizeof(path), "/sys/block/%s/size", device_name);
    long result = read_sys_file_long(path);
    if (result != -1) {
        sectors = (guint64)result;
        // Les secteurs sont généralement de 512 octets
        // Utiliser les unités décimales comme les fabricants (1000^3 au lieu de 1024^3)
        capacity_gb = (sectors * 512) / (1000 * 1000 * 1000);
    }
    
    return capacity_gb;
}

gboolean disk_has_swap(const gchar* device_name) {
    if (!device_name || strlen(device_name) == 0) return FALSE;
    
    FILE *fp = fopen(PROC_SWAPS, "r");
    if (!fp) return FALSE;
    
    char line[256];
    gboolean has_swap = FALSE;
    
    // Ignorer la ligne d'en-tête
    if (fgets(line, sizeof(line), fp)) {
        while (fgets(line, sizeof(line), fp)) {
            char swap_device[128];
            if (sscanf(line, "%127s", swap_device) == 1) {
                // Vérifier si le swap commence par le device (ex: nvme0n1p2 contient nvme0n1)
                if (strncmp(swap_device, "/dev/", 5) == 0) {
                    char *swap_name = swap_device + 5; // Retirer "/dev/"
                    if (strstr(swap_name, device_name) != NULL) {
                        has_swap = TRUE;
                        break;
                    }
                }
            }
        }
    }
    
    fclose(fp);
    return has_swap;
}

void update_disk_data(void) {
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    
    for (int i = 0; i < disk_manager.disk_count; i++) {
        disk_info_t *disk = &disk_manager.disks[i];
        
        // Lire les statistiques actuelles (version optimisée par bloc)
        long read_sectors, write_sectors, io_time;
        get_disk_stats_batch(disk->name, &read_sectors, &write_sectors, &io_time);
        
        if (!(disk_stats[i].flags & DISK_INITIALIZED)) {
            // Premier échantillon - juste sauvegarder les valeurs
            disk_stats[i].prev_read_sectors = read_sectors;
            disk_stats[i].prev_write_sectors = write_sectors;
            disk_stats[i].prev_io_time = io_time;
            disk_stats[i].prev_time = current_time;
            disk_stats[i].flags |= DISK_INITIALIZED;
            
            // Valeurs par défaut
            disk->activity_samples[disk_manager.current_index] = 0;
            disk->read_samples[disk_manager.current_index] = 0;
            disk->write_samples[disk_manager.current_index] = 0;
        } else {
            // Calculer les différences
            long read_diff = read_sectors - disk_stats[i].prev_read_sectors;
            long write_diff = write_sectors - disk_stats[i].prev_write_sectors;
            long io_time_diff = io_time - disk_stats[i].prev_io_time;
            
            double time_diff = (current_time.tv_sec - disk_stats[i].prev_time.tv_sec) +
                              (current_time.tv_nsec - disk_stats[i].prev_time.tv_nsec) / 1e9;
            
            // Calculer l'activité (pourcentage d'utilisation)
            gint activity_percent = 0;
            if (time_diff > 0) {
                // io_time_diff est en ms, time_diff en secondes
                // Pour obtenir un pourcentage : (io_time_diff / (time_diff * 1000)) * 100
                activity_percent = (gint)((io_time_diff * 100.0) / (time_diff * 1000.0));
                if (activity_percent > 100) activity_percent = 100;
                if (activity_percent < 0) activity_percent = 0;
            }
            
            
            // Calculer les taux de lecture/écriture en KB/s
            gint read_kbs = 0, write_kbs = 0;
            if (time_diff > 0) {
                read_kbs = (gint)((read_diff * 512) / (time_diff * 1024)); // secteurs -> KB/s
                write_kbs = (gint)((write_diff * 512) / (time_diff * 1024));
                if (read_kbs < 0) read_kbs = 0;
                if (write_kbs < 0) write_kbs = 0;
            }
            
            // Stocker les valeurs
            disk->activity_samples[disk_manager.current_index] = activity_percent;
            disk->read_samples[disk_manager.current_index] = read_kbs;
            disk->write_samples[disk_manager.current_index] = write_kbs;
            
            // Sauvegarder pour le prochain calcul
            disk_stats[i].prev_read_sectors = read_sectors;
            disk_stats[i].prev_write_sectors = write_sectors;
            disk_stats[i].prev_io_time = io_time;
            disk_stats[i].prev_time = current_time;
        }
    }
    
    // Avancer l'index circulaire
    disk_manager.current_index = (disk_manager.current_index + 1) % PERFORMANCE_SAMPLES_COUNT;
    if (disk_manager.current_index == 0) {
        disk_manager.flags |= MANAGER_BUFFER_FULL;
    }
}

gint get_disk_count(void) {
    return disk_manager.disk_count;
}

disk_info_t* get_disk_info(gint index) {
    if (index < 0 || index >= disk_manager.disk_count) {
        return NULL;
    }
    return &disk_manager.disks[index];
}

disk_info_t* get_system_disk_info(void) {
    for (int i = 0; i < disk_manager.disk_count; i++) {
        if (disk_manager.disks[i].flags & DISK_IS_SYSTEM) {
            return &disk_manager.disks[i];
        }
    }
    return disk_manager.disk_count > 0 ? &disk_manager.disks[0] : NULL;
}

void set_selected_disk(gint index) {
    if (index >= 0 && index < disk_manager.disk_count) {
        disk_manager.selected_disk_index = index;
    }
}

gint get_selected_disk_index(void) {
    return disk_manager.selected_disk_index;
}
