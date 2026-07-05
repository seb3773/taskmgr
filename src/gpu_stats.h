/*
 * GPU Statistics Library
 * ======================
 *
 * Simple library to monitor GPU usage on Linux via DRM statistics.
 * Optimized for frequent polling (1Hz recommended minimum).
 *
 * Author: Extracted and optimized from htop GPU monitoring code
 * License: GPL v2+
 *
 * USAGE EXAMPLE:
 * ==============
 *
 * #include "gpu_stats.h"
 *
 * int main() {
 *     GPUStats stats;
 *
 *     // Initialize - call once at startup
 *     if (gpu_stats_init() != 0) {
 *         printf("GPU monitoring not available\n");
 *         return 1;
 *     }
 *
 *     // CHOICE 1: htop-style values (raw DRM process time, can exceed 100%)
 *     gpu_stats_set_mode(GPU_MODE_PROCESS_TIME);
 *
 *     // CHOICE 2: Averaged process time (balanced, always ≤100%)
 *     gpu_stats_set_mode(GPU_MODE_PROCESS_AVERAGE);
 *
 *     // CHOICE 3: Hardware-approximated values (frequency-corrected, more realistic)
 *     gpu_stats_set_mode(GPU_MODE_HARDWARE_APPROX);  // Default mode
 *
 *     while (running) {
 *         // Update stats - call every 1+ seconds
 *         if (gpu_stats_update(&stats) == 0) {
 *             printf("Total: %.1f%%, Render: %.1f%%, Video: %.1f%%\n",
 *                    stats.total_percent, stats.render_percent, stats.video_percent);
 *         }
 *         sleep(1);
 *     }
 *
 *     // Cleanup - call once at shutdown
 *     gpu_stats_cleanup();
 *     return 0;
 * }
 *
 * DEVELOPER MIGRATION GUIDE:
 * =========================
 *
 * If you previously implemented GPU monitoring and want to migrate to this system:
 *
 * 1. KEEPING OLD BEHAVIOR (htop-compatible):
 *    // Add this line after gpu_stats_init():
 *    gpu_stats_set_mode(GPU_MODE_PROCESS_TIME);
 *
 *    // Your existing code remains unchanged:
 *    gpu_stats_update(&stats);
 *    display_gpu_usage(stats.total_percent, stats.render_percent, stats.video_percent);
 *
 * 2. UPGRADING TO REALISTIC VALUES (Mission Center-compatible):
 *    // Add this line after gpu_stats_init() (or omit - it's the default):
 *    gpu_stats_set_mode(GPU_MODE_HARDWARE_APPROX);
 *
 *    // Same API, but values will be 2-4x lower (more realistic):
 *    gpu_stats_update(&stats);
 *    display_gpu_usage(stats.total_percent, stats.render_percent, stats.video_percent);
 *
 *    // IMPORTANT: Review your alerting thresholds and historical data!
 *
 * 3. DYNAMIC SWITCHING (advanced usage):
 *    bool use_realistic_values = get_user_preference();
 *    gpu_stats_set_mode(use_realistic_values ? GPU_MODE_HARDWARE_APPROX : GPU_MODE_PROCESS_TIME);
 *
 * 4. VALUE DIFFERENCES TO EXPECT:
 *    GPU_MODE_PROCESS_TIME:    Values similar to original htop (can exceed 100% per engine)
 *    GPU_MODE_HARDWARE_APPROX: Values 2-4x lower, frequency-corrected, never exceed 100%
 *
 * 5. WHEN TO USE WHICH MODE:
 *    - Use GPU_MODE_PROCESS_TIME for: htop compatibility, process-level analysis
 *    - Use GPU_MODE_HARDWARE_APPROX for: realistic monitoring, hardware correlation
 *
 * 6. API COMPATIBILITY:
 *    - GPUStats structure: UNCHANGED
 *    - gpu_stats_init/update/cleanup: UNCHANGED
 *    - Only difference: values returned by gpu_stats_update() depend on current mode
 */

#ifndef GPU_STATS_H
#define GPU_STATS_H

#include <sys/types.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * GPU Monitoring Modes
 * Choose between different calculation methods for GPU utilization
 */
typedef enum {
    GPU_MODE_PROCESS_TIME,     // Raw DRM fdinfo time (htop-style, cumulative: render+video)
    GPU_MODE_PROCESS_AVERAGE,  // Average of active engines (process-based but clamped ≤100%)
    GPU_MODE_HARDWARE_APPROX   // Frequency-corrected (Mission Center-style, hardware approximation)
} GPUMonitoringMode;

/**
 * GPU Statistics Structure
 * Contains the latest GPU usage percentages
 */
typedef struct {
    double total_percent;   // Total GPU usage (calculation depends on monitoring mode)
    double render_percent;  // 3D/graphics rendering engine usage
    double video_percent;   // Video decode/encode engine usage
    int available;          // 1 if GPU stats are available, 0 otherwise
} GPUStats;

/**
 * Per-process GPU Statistics Structure
 * Contains GPU usage data for individual processes
 */
typedef struct {
    pid_t pid;                    // Process ID
    char process_name[64];        // Process name (truncated from /proc/PID/comm)
    double render_percent;        // 3D/graphics rendering engine usage for this process
    double video_percent;         // Video decode/encode engine usage for this process
    double total_percent;         // Total GPU usage for this process (calculation depends on monitoring mode)
    unsigned long long render_time; // Raw render time in nanoseconds (for advanced usage)
    unsigned long long video_time;  // Raw video time in nanoseconds (for advanced usage)
    int active;                   // 1 if process is still active, 0 if terminated
} ProcessGPUEntry;

/**
 * GPU Process Table Structure
 * Contains all per-process GPU data from last scan
 */
typedef struct {
    ProcessGPUEntry* processes;   // Array of process entries
    int count;                    // Number of active processes with GPU usage
    int capacity;                 // Allocated capacity of processes array
    struct timeval last_update;   // Timestamp of last update
} GPUProcessTable;

/**
 * Initialize GPU monitoring
 *
 * Call this once at application startup.
 *
 * @return 0 on success, -1 if GPU monitoring is not available
 */
int gpu_stats_init(void);

/**
 * Set GPU monitoring mode
 *
 * Changes how GPU utilization is calculated:
 * - GPU_MODE_PROCESS_TIME: Raw DRM time percentages (htop-style)
 * - GPU_MODE_HARDWARE_APPROX: Frequency-corrected approximation (Mission Center-style)
 *
 * @param mode Monitoring mode to use
 * @return 0 on success, -1 on error
 */
int gpu_stats_set_mode(GPUMonitoringMode mode);

/**
 * Get current GPU monitoring mode
 *
 * @return Current monitoring mode
 */
GPUMonitoringMode gpu_stats_get_mode(void);

/**
 * Update GPU statistics
 *
 * Call this function at regular intervals (recommended: every 1+ seconds).
 * First call after init() will return 0% values (baseline measurement).
 *
 * @param stats Pointer to GPUStats structure to fill
 * @return 0 on success, -1 on error
 *
 * NOTE: Function is optimized for frequent calls:
 * - Minimal memory allocation (reuses buffers)
 * - Fast process scanning (skips non-GPU processes)
 * - Efficient file I/O (single read per fdinfo file)
 * - Calculation method depends on current monitoring mode
 */
int gpu_stats_update(GPUStats* stats);

/**
 * Cleanup GPU monitoring resources
 *
 * Call this once at application shutdown.
 * Frees internal buffers and resources.
 */
void gpu_stats_cleanup(void);

/**
 * Check if GPU monitoring is available
 *
 * @return 1 if GPU monitoring is available, 0 otherwise
 */
int gpu_stats_is_available(void);

/**
 * Get human-readable error string for last operation
 *
 * @return Error description string (valid until next gpu_stats_* call)
 */
const char* gpu_stats_get_error(void);

/**
 * Get GPU name/model for display purposes
 *
 * @return GPU name string (e.g., "Intel UHD Graphics 620"), or "Unknown GPU" if unavailable
 *         String is valid until next call to this function
 */
const char* gpu_stats_get_gpu_name(void);

/**
 * Get GPU frequency correction factor (for debugging)
 *
 * @return Current frequency / max frequency ratio (0.1 to 1.0)
 */
double gpu_stats_get_frequency_correction(void);

/**
 * Get number of processes currently using GPU
 *
 * @return Number of processes with GPU activity, -1 on error
 */
int gpu_stats_get_process_count(void);

/**
 * Get GPU statistics for a specific process by index
 *
 * @param index Process index (0 to gpu_stats_get_process_count()-1)
 * @return Pointer to ProcessGPUEntry or NULL if index invalid
 */
const ProcessGPUEntry* gpu_stats_get_process(int index);

/**
 * Get GPU statistics for a specific process by PID
 *
 * @param pid Process ID to search for
 * @return Pointer to ProcessGPUEntry or NULL if PID not found
 */
const ProcessGPUEntry* gpu_stats_get_process_by_pid(pid_t pid);

/**
 * Get the complete GPU process table
 *
 * @return Pointer to GPUProcessTable with all process data, NULL on error
 */
const GPUProcessTable* gpu_stats_get_process_table(void);

/**
 * Get current GPU frequency in MHz
 *
 * @return Current GPU frequency in MHz, 0 if not available
 * @note Automatically updated when gpu_stats_update() is called
 */
unsigned int gpu_stats_get_current_freq_mhz(void);

/**
 * Get maximum GPU frequency in MHz
 *
 * @return Maximum GPU frequency in MHz, 0 if not available
 * @note Automatically updated when gpu_stats_update() is called
 */
unsigned int gpu_stats_get_max_freq_mhz(void);

/**
 * Check if GPU frequency data is available
 *
 * @return 1 if frequency data is valid, 0 otherwise
 */
int gpu_stats_freq_data_available(void);

#ifdef __cplusplus
}
#endif

#endif /* GPU_STATS_H */