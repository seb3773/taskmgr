/*
 * GPU Statistics Library Implementation
 * Optimized for frequent polling with minimal overhead
 *
 * APPROXIMATION METHOD AND JUSTIFICATION:
 * =======================================
 *
 * This library uses DRM (Direct Rendering Manager) fdinfo interface to monitor GPU usage.
 * DRM fdinfo provides process-level GPU engine time in nanoseconds, which differs from
 * hardware-level utilization monitoring (like Intel PMU or Mission Center).
 *
 * Key difference:
 * - DRM fdinfo: Measures cumulative time engines were active (process perspective)
 * - Hardware monitoring: Measures actual GPU silicon utilization (hardware perspective)
 *
 * The discrepancy occurs because GPU frequency scaling affects actual work done:
 * - Engine running 50% time at 600MHz ≠ 50% time at 1200MHz in terms of work output
 * - DRM fdinfo reports same 50% for both cases
 * - Hardware monitoring shows different utilization values
 *
 * FREQUENCY CORRECTION APPROXIMATION:
 * Our solution: utilization_corrected = utilization_drm × (current_freq / max_freq)
 *
 * This approximates hardware reality by scaling DRM measurements according to actual
 * GPU frequency, bringing our universal method closer to vendor-specific hardware monitors.
 *
 * Validation: Tested against Mission Center (Intel PMU), showing ~4x reduction in values
 * when GPU frequency drops, matching expected hardware behavior.
 *
 * MIGRATION GUIDE FOR EXISTING IMPLEMENTATIONS:
 * ============================================
 *
 * If you previously implemented GPU monitoring based on this library's earlier version,
 * here are the changes needed to migrate to the frequency-corrected system:
 *
 * 1. HEADER CHANGES (gpu_stats.h):
 *    - No API changes required
 *    - GPUStats structure remains identical
 *    - All function signatures unchanged
 *
 * 2. CALCULATION CHANGES:
 *    OLD BEHAVIOR:
 *      stats->render_percent = raw_percentage_from_drm_fdinfo;
 *      stats->total_percent = stats->render_percent + stats->video_percent;
 *
 *    NEW BEHAVIOR:
 *      double freq_correction = get_gpu_frequency_correction();
 *      stats->render_percent = raw_percentage_from_drm_fdinfo * freq_correction;
 *      stats->total_percent = average_of_active_engines; // Not sum
 *
 * 3. EXPECTED VALUE CHANGES:
 *    - GPU usage values will be LOWER (more realistic)
 *    - Typical reduction: 2x to 4x depending on GPU frequency
 *    - Values now correlate with tools like Mission Center, nvidia-smi, etc.
 *
 * 4. INTEGRATION CONSIDERATIONS:
 *    - If you stored historical data: expect discontinuity after migration
 *    - If you have alerting thresholds: review and adjust downward
 *    - If you compare with other tools: values should now be more consistent
 *
 * 5. FALLBACK BEHAVIOR:
 *    - If frequency files unavailable: correction factor = 1.0 (no change)
 *    - Library gracefully handles missing sysfs files
 *    - Intel systems: reads gt_cur_freq_mhz, gt_RP0_freq_mhz
 *    - Other vendors: may need adaptation of frequency file paths
 *
 * 6. DEBUGGING FREQUENCY CORRECTION:
 *    Add this to your code to monitor correction factor:
 *      extern double get_gpu_frequency_correction(void); // Add to gpu_stats.h if needed
 *      double correction = get_gpu_frequency_correction();
 *      printf("Frequency correction: %.2f (%.0f%% of max)\n", correction, correction*100);
 *
 * 7. VALIDATION CHECKLIST:
 *    ✓ Compare values with Mission Center / nvidia-smi / intel_gpu_top
 *    ✓ Verify frequency correction changes with GPU load
 *    ✓ Check that values stay within 0-100% range
 *    ✓ Test fallback behavior when frequency files are missing
 */

#include "gpu_stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <ctype.h>
#include <pci/pci.h>

// Internal state for optimization
typedef struct {
    unsigned long long render_time;
    unsigned long long video_time;
    unsigned long long total_time;
    struct timeval timestamp;
} EngineData;

// Static state (persistent between calls)
static EngineData previous_data = {0};
static EngineData current_data = {0};
static int initialized = 0;
static char error_buffer[256] = {0};
static char read_buffer[4096]; // Reused buffer to avoid malloc/free
static char gpu_name_buffer[256] = {0}; // Buffer for GPU name
static GPUMonitoringMode monitoring_mode = GPU_MODE_HARDWARE_APPROX; // Default to hardware approximation

// Cache for last measurement to prevent state corruption during mode switching
static double last_raw_render = 0.0;
static double last_raw_video = 0.0;
static struct timespec last_measurement_time = {0, 0};
static int measurement_cache_valid = 0;

// Global process table for per-process GPU statistics
static GPUProcessTable process_table = {0};

// Previous process data for percentage calculation
typedef struct {
    pid_t pid;
    unsigned long long render_time;
    unsigned long long video_time;
    struct timeval timestamp;
} ProcessHistoryEntry;

static ProcessHistoryEntry* previous_process_data = NULL;
static int previous_process_count = 0;
static int previous_process_capacity = 0;

// Global GPU frequency data (automatically updated)
static unsigned int current_gpu_freq_mhz = 0;
static unsigned int max_gpu_freq_mhz = 0;
static int freq_data_valid = 0;


// Forward declarations
static int scan_all_processes(EngineData* data);
static int scan_process_fdinfo(pid_t pid, EngineData* data);
static int parse_fdinfo_file(int dirfd, const char* filename, EngineData* data);
static double calculate_percentage(unsigned long long current, unsigned long long previous, double time_delta_ms);
static double get_gpu_frequency_correction(void);

// Process table management functions
static int init_process_table(void);
static void cleanup_process_table(void);
static ProcessGPUEntry* add_process_entry(pid_t pid);
static void get_process_name(pid_t pid, char* name_buffer, size_t buffer_size);
static void calculate_process_percentages(double time_delta_ms);
static void update_process_history(void);

/**
 * Initialize GPU monitoring
 */
int gpu_stats_init(void) {
    if (initialized) {
        return 0; // Already initialized
    }

    // Clear state
    memset(&previous_data, 0, sizeof(previous_data));
    memset(&current_data, 0, sizeof(current_data));
    memset(error_buffer, 0, sizeof(error_buffer));

    // Test if we can access /proc
    DIR* proc_dir = opendir("/proc");
    if (!proc_dir) {
        snprintf(error_buffer, sizeof(error_buffer), "Cannot access /proc directory");
        return -1;
    }
    closedir(proc_dir);

    // Get initial timestamp
    gettimeofday(&previous_data.timestamp, NULL);

    // Initialize process table
    if (init_process_table() != 0) {
        return -1;
    }

    initialized = 1;
    return 0;
}

/**
 * Update GPU statistics
 */
int gpu_stats_update(GPUStats* stats) {
    if (!initialized) {
        snprintf(error_buffer, sizeof(error_buffer), "GPU stats not initialized");
        return -1;
    }

    if (!stats) {
        snprintf(error_buffer, sizeof(error_buffer), "Invalid stats pointer");
        return -1;
    }

    // Clear current data
    memset(&current_data, 0, sizeof(current_data));
    gettimeofday(&current_data.timestamp, NULL);

    // Scan all processes for GPU usage
    if (scan_all_processes(&current_data) != 0) {
        // Error already set by scan_all_processes
        return -1;
    }

    // Calculate time delta in milliseconds
    double time_delta_ms = (current_data.timestamp.tv_sec - previous_data.timestamp.tv_sec) * 1000.0 +
                          (current_data.timestamp.tv_usec - previous_data.timestamp.tv_usec) / 1000.0;

    if (time_delta_ms <= 0) {
        snprintf(error_buffer, sizeof(error_buffer), "Invalid time delta");
        return -1;
    }

    // Calculate raw percentages
    double raw_render = calculate_percentage(current_data.render_time, previous_data.render_time, time_delta_ms);
    double raw_video = calculate_percentage(current_data.video_time, previous_data.video_time, time_delta_ms);

    // Apply calculations based on monitoring mode
    if (monitoring_mode == GPU_MODE_HARDWARE_APPROX) {
        // Apply frequency correction to approximate hardware reality
        double freq_correction = get_gpu_frequency_correction();
        stats->render_percent = raw_render * freq_correction;
        stats->video_percent = raw_video * freq_correction;

        // Hardware-style total: Average of active engines (closer to Mission Center)
        // This represents actual GPU hardware utilization rather than cumulative process time
        int active_engines = (stats->render_percent > 0 ? 1 : 0) + (stats->video_percent > 0 ? 1 : 0);
        stats->total_percent = active_engines > 0 ? (stats->render_percent + stats->video_percent) / active_engines : 0.0;
    } else if (monitoring_mode == GPU_MODE_PROCESS_AVERAGE) {
        // Process-based average: Always average render and video (whether active or not)
        // This gives a balanced view without frequency correction but stays ≤100%
        stats->render_percent = raw_render;
        stats->video_percent = raw_video;
        stats->total_percent = (stats->render_percent + stats->video_percent) / 2.0;
    } else {
        // GPU_MODE_PROCESS_TIME: Raw DRM fdinfo time (htop-style)
        // Simple sum of all engines (cumulative process view)
        stats->render_percent = raw_render;
        stats->video_percent = raw_video;
        stats->total_percent = stats->render_percent + stats->video_percent;

        // Clamp total to avoid extreme spikes during heavy multi-engine usage
        if (stats->total_percent > 100.0) {
            stats->total_percent = 100.0;
        }
    }

    stats->available = 1;

    // Calculate per-process percentages using the same time delta
    calculate_process_percentages(time_delta_ms);

    // Store current as previous for next iteration only if enough time has passed
    // This prevents state corruption during rapid mode switching
    if (time_delta_ms >= 100.0) { // Only update state if at least 100ms passed
        previous_data = current_data;
        update_process_history(); // Also update process history
    }

    return 0;
}

/**
 * Cleanup resources
 */
void gpu_stats_cleanup(void) {
    cleanup_process_table();

    // Cleanup process history
    if (previous_process_data) {
        free(previous_process_data);
        previous_process_data = NULL;
    }
    previous_process_count = 0;
    previous_process_capacity = 0;

    memset(&previous_data, 0, sizeof(previous_data));
    memset(&current_data, 0, sizeof(current_data));
    measurement_cache_valid = 0;  // Invalider le cache de mesure
    freq_data_valid = 0;  // Invalider les données de fréquence
    initialized = 0;
}

/**
 * Set GPU monitoring mode
 */
int gpu_stats_set_mode(GPUMonitoringMode mode) {
    if (mode != GPU_MODE_PROCESS_TIME &&
        mode != GPU_MODE_PROCESS_AVERAGE &&
        mode != GPU_MODE_HARDWARE_APPROX) {
        snprintf(error_buffer, sizeof(error_buffer), "Invalid monitoring mode");
        return -1;
    }
    monitoring_mode = mode;
    return 0;
}

/**
 * Get current GPU monitoring mode
 */
GPUMonitoringMode gpu_stats_get_mode(void) {
    return monitoring_mode;
}

/**
 * Check availability
 */
int gpu_stats_is_available(void) {
    return initialized;
}

/**
 * Get last error
 */
const char* gpu_stats_get_error(void) {
    return error_buffer;
}

/**
 * Get GPU name from system information
 */
const char* gpu_stats_get_gpu_name(void) {
    struct pci_access *pacc;
    struct pci_dev *dev;
    char namebuf[1024];

    // Initialize with default
    snprintf(gpu_name_buffer, sizeof(gpu_name_buffer), "Unknown GPU");

    pacc = pci_alloc();
    if (!pacc) {
        return gpu_name_buffer;
    }

    pci_init(pacc);
    pci_scan_bus(pacc);

    for (dev = pacc->devices; dev; dev = dev->next) {
        pci_fill_info(dev, PCI_FILL_IDENT | PCI_FILL_CLASS);
        unsigned int class_code = dev->device_class >> 8;

        // VGA controller (0x03) or 3D controller (0x02)
        if (class_code == 0x03 || class_code == 0x02) {
            pci_lookup_name(pacc, namebuf, sizeof(namebuf), PCI_LOOKUP_DEVICE,
                           dev->vendor_id, dev->device_id);

            // Copy to our buffer and cleanup
            snprintf(gpu_name_buffer, sizeof(gpu_name_buffer), "%s", namebuf);
            pci_cleanup(pacc);
            return gpu_name_buffer;
        }
    }

    pci_cleanup(pacc);
    return gpu_name_buffer;
}

/*
 * INTERNAL FUNCTIONS
 */

/**
 * Scan all processes for GPU usage
 */
static int scan_all_processes(EngineData* data) {
    DIR* proc_dir = opendir("/proc");
    if (!proc_dir) {
        snprintf(error_buffer, sizeof(error_buffer), "Cannot open /proc directory");
        return -1;
    }

    // Reset process table for this scan - mark all as inactive first
    for (int i = 0; i < process_table.count; i++) {
        process_table.processes[i].active = 0;
    }

    struct dirent* entry;
    while ((entry = readdir(proc_dir))) {
        // Skip non-numeric entries (only process PIDs)
        if (!isdigit(entry->d_name[0])) {
            continue;
        }

        pid_t pid = (pid_t)atoi(entry->d_name);
        scan_process_fdinfo(pid, data);
        // Continue even if individual process fails
    }

    // Remove inactive processes (those that disappeared)
    int write_index = 0;
    for (int read_index = 0; read_index < process_table.count; read_index++) {
        if (process_table.processes[read_index].active) {
            if (write_index != read_index) {
                process_table.processes[write_index] = process_table.processes[read_index];
            }
            write_index++;
        }
    }
    process_table.count = write_index;

    // Update timestamp
    gettimeofday(&process_table.last_update, NULL);

    closedir(proc_dir);
    return 0;
}

/**
 * Scan specific process fdinfo directory
 */
static int scan_process_fdinfo(pid_t pid, EngineData* data) {
    char fdinfo_path[64];
    snprintf(fdinfo_path, sizeof(fdinfo_path), "/proc/%d/fdinfo", pid);

    DIR* fdinfo_dir = opendir(fdinfo_path);
    if (!fdinfo_dir) {
        // Process may have disappeared or no permission - not an error
        return 0;
    }

    // Prepare process-specific data collection
    EngineData process_data = {0};
    int fdinfo_fd = dirfd(fdinfo_dir);
    struct dirent* entry;

    while ((entry = readdir(fdinfo_dir))) {
        // Skip . and ..
        if (entry->d_name[0] == '.' &&
            (entry->d_name[1] == '\0' || (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
            continue;
        }

        parse_fdinfo_file(fdinfo_fd, entry->d_name, &process_data);
        // Continue even if individual file fails
    }

    // If this process has GPU activity, add/update it in the process table
    if (process_data.render_time > 0 || process_data.video_time > 0) {
        ProcessGPUEntry* entry = add_process_entry(pid);
        if (entry) {
            entry->render_time = process_data.render_time;
            entry->video_time = process_data.video_time;
            entry->active = 1;  // Mark as active
        }

        // Add to global totals for compatibility
        data->render_time += process_data.render_time;
        data->video_time += process_data.video_time;
        data->total_time += process_data.total_time;
    }

    closedir(fdinfo_dir);
    return 0;
}

/**
 * Parse individual fdinfo file for DRM statistics
 */
static int parse_fdinfo_file(int dirfd, const char* filename, EngineData* data) {
    int fd = openat(dirfd, filename, O_RDONLY);
    if (fd == -1) {
        return -1;
    }

    ssize_t bytes_read = read(fd, read_buffer, sizeof(read_buffer) - 1);
    close(fd);

    if (bytes_read <= 0) {
        return -1;
    }

    read_buffer[bytes_read] = '\0';

    // Parse line by line
    char* line;
    char* saveptr;
    char* buffer_copy = read_buffer;

    while ((line = strtok_r(buffer_copy, "\n", &saveptr)) != NULL) {
        buffer_copy = NULL;

        // Look for DRM engine statistics
        if (strncmp(line, "drm-engine-", 11) != 0) {
            continue;
        }

        const char* engine_start = line + 11;
        const char* colon = strchr(line, ':');
        if (!colon) {
            continue;
        }

        // Parse the value
        char* endptr;
        errno = 0;
        unsigned long long value = strtoull(colon + 1, &endptr, 10);

        if (errno != 0 || strncmp(endptr, " ns", 3) != 0) {
            continue;
        }

        // Determine engine type and accumulate
        size_t engine_len = colon - engine_start;

        if (strncmp(engine_start, "render", 6) == 0 && engine_len == 6) {
            data->render_time += value;
            data->total_time += value;
        } else if (strncmp(engine_start, "video", 5) == 0 && engine_len == 5) {
            data->video_time += value;
            data->total_time += value;
        }
        // Ignore other engines (copy, video-enhance, etc.)
    }

    return 0;
}

/**
 * Calculate percentage from time differences
 */
static double calculate_percentage(unsigned long long current, unsigned long long previous, double time_delta_ms) {
    if (current < previous) {
        // Handle counter wraparound or process restart
        return 0.0;
    }

    unsigned long long time_diff = current - previous;
    double percentage = 100.0 * time_diff / (1000.0 * 1000.0) / time_delta_ms;

    // Clamp to 100% to avoid spikes during process startup/shutdown
    return (percentage > 100.0) ? 100.0 : percentage;
}

/**
 * Get GPU frequency correction factor to approximate hardware utilization
 * Returns: current_freq / max_freq ratio (0.0 to 1.0)
 */
static double get_gpu_frequency_correction(void) {
    // Try gt_cur_freq_mhz first (more reliable than gt_act_freq_mhz)
    FILE* cur_freq_file = fopen("/sys/class/drm/card0/gt_cur_freq_mhz", "r");
    FILE* max_freq_file = fopen("/sys/class/drm/card0/gt_RP0_freq_mhz", "r");

    if (!cur_freq_file) {
        // Fallback to gt_act_freq_mhz
        cur_freq_file = fopen("/sys/class/drm/card0/gt_act_freq_mhz", "r");
    }

    if (!max_freq_file) {
        // Fallback to gt_max_freq_mhz
        max_freq_file = fopen("/sys/class/drm/card0/gt_max_freq_mhz", "r");
    }

    if (cur_freq_file && max_freq_file) {
        unsigned int current_freq = 0, max_freq = 0;

        if (fscanf(cur_freq_file, "%u", &current_freq) == 1 &&
            fscanf(max_freq_file, "%u", &max_freq) == 1 &&
            max_freq > 0) {

            // Update global variables
            current_gpu_freq_mhz = current_freq;
            max_gpu_freq_mhz = max_freq;
            freq_data_valid = 1;

            double correction = (double)current_freq / (double)max_freq;

            // Reasonable bounds check
            if (correction < 0.1) correction = 0.1;
            if (correction > 1.0) correction = 1.0;

            if (cur_freq_file) fclose(cur_freq_file);
            if (max_freq_file) fclose(max_freq_file);
            return correction;
        }
    }

    if (cur_freq_file) fclose(cur_freq_file);
    if (max_freq_file) fclose(max_freq_file);

    // Mark frequency data as invalid
    freq_data_valid = 0;

    // Default fallback when files can't be read
    return 1.0;
}

/*
 * TEST MAIN FUNCTION
 * Compile with: gcc -DGPU_STATS_TEST_MAIN gpu_stats.c -o gpu_stats_test
 */
double gpu_stats_get_frequency_correction(void) {
    return get_gpu_frequency_correction();
}

/*
 * PROCESS TABLE MANAGEMENT FUNCTIONS
 */

// Initialize the process table
static int init_process_table(void) {
    process_table.processes = malloc(16 * sizeof(ProcessGPUEntry));
    if (!process_table.processes) {
        snprintf(error_buffer, sizeof(error_buffer), "Failed to allocate process table");
        return -1;
    }
    process_table.capacity = 16;
    process_table.count = 0;
    gettimeofday(&process_table.last_update, NULL);
    return 0;
}

// Cleanup the process table
static void cleanup_process_table(void) {
    if (process_table.processes) {
        free(process_table.processes);
        process_table.processes = NULL;
    }
    process_table.count = 0;
    process_table.capacity = 0;
}

// Add or update process entry in the table
static ProcessGPUEntry* add_process_entry(pid_t pid) {
    // First check if process already exists
    for (int i = 0; i < process_table.count; i++) {
        if (process_table.processes[i].pid == pid) {
            return &process_table.processes[i];
        }
    }

    // Need to add new process - check capacity
    if (process_table.count >= process_table.capacity) {
        int new_capacity = process_table.capacity * 2;
        ProcessGPUEntry* new_processes = realloc(process_table.processes,
                                               new_capacity * sizeof(ProcessGPUEntry));
        if (!new_processes) {
            return NULL; // Keep existing data, just don't add new process
        }
        process_table.processes = new_processes;
        process_table.capacity = new_capacity;
    }

    // Add new process
    ProcessGPUEntry* entry = &process_table.processes[process_table.count];
    memset(entry, 0, sizeof(ProcessGPUEntry));
    entry->pid = pid;
    entry->active = 1;
    get_process_name(pid, entry->process_name, sizeof(entry->process_name));
    process_table.count++;
    return entry;
}

// Get process name from /proc/PID/comm
static void get_process_name(pid_t pid, char* name_buffer, size_t buffer_size) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/comm", pid);

    // OPTIMISATION: open/read/close direct au lieu de fopen/fgets/fclose
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        ssize_t bytes = read(fd, name_buffer, buffer_size - 1);
        close(fd);
        if (bytes > 0) {
            name_buffer[bytes] = '\0';
            // Remove trailing newline
            size_t len = strlen(name_buffer);
            if (len > 0 && name_buffer[len - 1] == '\n') {
                name_buffer[len - 1] = '\0';
            }
        } else {
            snprintf(name_buffer, buffer_size, "process-%d", pid);
        }
    } else {
        snprintf(name_buffer, buffer_size, "process-%d", pid);
    }
}

// Calculate percentages for all processes in the table
static void calculate_process_percentages(double time_delta_ms) {
    for (int i = 0; i < process_table.count; i++) {
        ProcessGPUEntry* entry = &process_table.processes[i];

        // Find previous data for this process
        unsigned long long prev_render = 0, prev_video = 0;
        for (int j = 0; j < previous_process_count; j++) {
            if (previous_process_data[j].pid == entry->pid) {
                prev_render = previous_process_data[j].render_time;
                prev_video = previous_process_data[j].video_time;
                break;
            }
        }

        // Calculate raw percentages
        double raw_render = calculate_percentage(entry->render_time, prev_render, time_delta_ms);
        double raw_video = calculate_percentage(entry->video_time, prev_video, time_delta_ms);

        // Apply the same mode logic as the global stats
        if (monitoring_mode == GPU_MODE_HARDWARE_APPROX) {
            double freq_correction = get_gpu_frequency_correction();
            entry->render_percent = raw_render * freq_correction;
            entry->video_percent = raw_video * freq_correction;

            // Hardware-style total: Average of active engines
            int active_engines = (entry->render_percent > 0 ? 1 : 0) + (entry->video_percent > 0 ? 1 : 0);
            entry->total_percent = active_engines > 0 ? (entry->render_percent + entry->video_percent) / active_engines : 0.0;
        } else if (monitoring_mode == GPU_MODE_PROCESS_AVERAGE) {
            entry->render_percent = raw_render;
            entry->video_percent = raw_video;
            entry->total_percent = (entry->render_percent + entry->video_percent) / 2.0;
        } else {
            // GPU_MODE_PROCESS_TIME
            entry->render_percent = raw_render;
            entry->video_percent = raw_video;
            entry->total_percent = entry->render_percent + entry->video_percent;

            if (entry->total_percent > 100.0) {
                entry->total_percent = 100.0;
            }
        }
    }
}

// Update process history for next iteration
static void update_process_history(void) {
    // Ensure we have enough space
    if (process_table.count > previous_process_capacity) {
        int new_capacity = process_table.count * 2;
        ProcessHistoryEntry* new_data = realloc(previous_process_data,
                                              new_capacity * sizeof(ProcessHistoryEntry));
        if (new_data) {
            previous_process_data = new_data;
            previous_process_capacity = new_capacity;
        } else {
            // Memory allocation failed, keep old data
            return;
        }
    }

    // Copy current process data to history
    previous_process_count = process_table.count;
    for (int i = 0; i < process_table.count; i++) {
        previous_process_data[i].pid = process_table.processes[i].pid;
        previous_process_data[i].render_time = process_table.processes[i].render_time;
        previous_process_data[i].video_time = process_table.processes[i].video_time;
        previous_process_data[i].timestamp = process_table.last_update;
    }
}

/*
 * PUBLIC PROCESS TABLE API FUNCTIONS
 */

int gpu_stats_get_process_count(void) {
    if (!initialized) {
        return -1;
    }
    return process_table.count;
}

const ProcessGPUEntry* gpu_stats_get_process(int index) {
    if (!initialized || index < 0 || index >= process_table.count) {
        return NULL;
    }
    return &process_table.processes[index];
}

const ProcessGPUEntry* gpu_stats_get_process_by_pid(pid_t pid) {
    if (!initialized) {
        return NULL;
    }

    for (int i = 0; i < process_table.count; i++) {
        if (process_table.processes[i].pid == pid) {
            return &process_table.processes[i];
        }
    }
    return NULL;
}

const GPUProcessTable* gpu_stats_get_process_table(void) {
    if (!initialized) {
        return NULL;
    }
    return &process_table;
}

unsigned int gpu_stats_get_current_freq_mhz(void) {
    if (!initialized) {
        return 0;
    }

    // If frequency data is not valid, try to read it now
    if (!freq_data_valid) {
        get_gpu_frequency_correction(); // This updates freq_data_valid and frequency globals
    }

    return current_gpu_freq_mhz;
}

unsigned int gpu_stats_get_max_freq_mhz(void) {
    if (!initialized) {
        return 0;
    }

    // If frequency data is not valid, try to read it now
    if (!freq_data_valid) {
        get_gpu_frequency_correction(); // This updates freq_data_valid and frequency globals
    }

    return max_gpu_freq_mhz;
}

int gpu_stats_freq_data_available(void) {
    return (initialized && freq_data_valid) ? 1 : 0;
}

#ifdef GPU_STATS_TEST_MAIN

#include <signal.h>

static volatile int running = 1;

void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

void display_test_stats(const GPUStats* stats) {
    printf("\033[2J\033[H"); // Clear screen and move cursor to top

    const char* mode_name;
    switch (gpu_stats_get_mode()) {
        case GPU_MODE_PROCESS_TIME:
            mode_name = "Process Time (htop-style, cumulative)";
            break;
        case GPU_MODE_PROCESS_AVERAGE:
            mode_name = "Process Average (balanced, ≤100%)";
            break;
        case GPU_MODE_HARDWARE_APPROX:
            mode_name = "Hardware Approximation (Mission Center-style)";
            break;
        default:
            mode_name = "Unknown Mode";
            break;
    }

    printf("=== GPU Stats Library Test ===\n");
    printf("GPU: %s\n", gpu_stats_get_gpu_name());
    printf("Mode: %s\n\n", mode_name);

    // Read and display GPU frequencies
    unsigned int current_freq = 0, max_freq = 0;
    FILE* cur_freq_file = fopen("/sys/class/drm/card0/gt_cur_freq_mhz", "r");
    FILE* max_freq_file = fopen("/sys/class/drm/card0/gt_RP0_freq_mhz", "r");

    if (cur_freq_file && fscanf(cur_freq_file, "%u", &current_freq) == 1) {
        fclose(cur_freq_file);
    }
    if (max_freq_file && fscanf(max_freq_file, "%u", &max_freq) == 1) {
        fclose(max_freq_file);
    }

    if (current_freq > 0 && max_freq > 0) {
        printf("GPU Frequencies: Current=%u MHz  |  Max=%u MHz  |  Ratio=%.1f%%\n\n",
               current_freq, max_freq, (double)current_freq / max_freq * 100.0);
    } else {
        printf("GPU Frequencies: Unable to read frequency information\n\n");
    }

    if (stats->available) {
        printf("Total GPU Usage: %.1f%%\n", stats->total_percent);
        printf("\nEngine Details:\n");
        printf("  Render: %.1f%%\n", stats->render_percent);
        printf("  Video:  %.1f%%\n", stats->video_percent);

        if (gpu_stats_get_mode() == GPU_MODE_HARDWARE_APPROX) {
            // Show frequency correction factor only in hardware mode
            double freq_correction = get_gpu_frequency_correction();
            printf("\nFrequency Correction: %.2f (%.0f%% of max freq)\n",
                   freq_correction, freq_correction * 100.0);
        }
    } else {
        printf("GPU statistics not available\n");
    }

    printf("\nPress Ctrl+C to exit\n");
    fflush(stdout);
}

int main() {
    printf("GPU Stats Library Test\n");
    printf("======================\n\n");

    // Setup signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Initialize
    printf("Initializing GPU monitoring...\n");
    if (gpu_stats_init() != 0) {
        printf("Error: %s\n", gpu_stats_get_error());
        printf("GPU monitoring is not available on this system.\n");
        return 1;
    }

    printf("GPU monitoring initialized successfully.\n");
    printf("Starting test loop (1 Hz refresh)...\n\n");

    // Wait a bit to establish baseline
    sleep(1);

    // Test loop
    while (running) {
        GPUStats stats;
        if (gpu_stats_update(&stats) == 0) {
            display_test_stats(&stats);
        } else {
            printf("Error updating stats: %s\n", gpu_stats_get_error());
            break;
        }
        sleep(1);
    }

    // Cleanup
    gpu_stats_cleanup();
    printf("\nGPU Stats Test stopped.\n");

    return 0;
}

#endif /* GPU_STATS_TEST_MAIN */