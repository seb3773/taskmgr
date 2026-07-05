# Task Manager for Trinity Desktop Environment (TDE)

A high-performance, lightweight, and modern system task manager designed for the Trinity Desktop Environment (TDE), closely mimicking the well known Windows 10 Task Manager interface.

![Task Manager Screenshot](about_taskmgr.png)

## Core Features

- **Processes Tab**: Virtual tree-table view displaying hierarchical processes with grouped mode. Includes real-time indicators for CPU, memory working set, private bytes, virtual size, GPU usage, priority, and PSS memory calculations. Includes quick "End Task" controls and **interactive keyboard prefix-search** (automatically jumps selection to matching processes with a theme-aware overlay popup).
- **Performance Tab**: real-time system resource graphs (CPU usage, RAM allocation, GPU activity, Disk read/write rates, and Network throughput). Supports smooth scrolling, logical processor grids, and double-click **Compact Mode** (displays only the active graph with window decorations and sidebar hidden).
- **Startup Tab**: Lists desktop auto-start applications with quick enable/disable toggle options.
- **Users Tab**: Monitors logged-in users with session resource allocations and disconnect capabilities.
- **Services Tab**: Integrated systemd system services manager with start, stop, restart, enable, and disable controls.
- **Frictionless Privileged Actions**: Action prompts trigger password-authenticated operations (via su/PAM credentials helper) without requiring the entire application to run as root.
- **System Tray Integration**: Interactive tray icon showing dynamic live CPU usage level.

---

## Technical Architecture

The architecture is split into a **C backend** (polling, processing, system calls) and a **C++ frontend** (TQt3 graphics, layout, MVC views). This partitioning is designed to achieve maximum runtime performance and a minimal binary footprint.

```
                  ┌──────────────────────────────┐
                  │      C++ / TQt3 Frontend     │
                  │                              │
                  │   MainWindow / Tabs / Dialogs│
                  │   Custom Graphs  / MVC views │
                  └──────────────┬───────────────┘
                                 │
                     C++ to C Bridge interface
                                 │
                  ┌──────────────▼───────────────┐
                  │           C Backend          │
                  │                              │
                  │  /proc parsing / SIMD PSS    │
                  │  Network, Disk, GPU Polling  │
                  └──────────────────────────────┘
```

### 1. Pure C Performance-Critical Polling Backend
All background system data acquisition modules are written in **pure, optimized C**:
- **Zero C++ overhead**: System calls and filesystem parsing of `/proc` are done without exceptions, RTTI, vtables, or static initializers, maintaining maximum speed.
- **Batch /proc Reading**: Implements batch file processing using `openat` and static thread-local buffers (consolidated buffers) to avoid repeated malloc allocations and reduce system call frequency.
- **SIMD-accelerated PSS Parsing**: Reads and parses `smaps_rollup` dynamically using Intel SSE2 vectors to parse memory values at the byte level with minimum CPU overhead.
- **Fast Formatting Engine**: A custom string formatting implementation (`fast_format.c`) replaces standard `sprintf`/`snprintf` calls inside performance loops, yielding a 20x to 40x speed improvement for common patterns.

### 2. Modern TQt3 C++ GUI
The graphical interface is built using C++ and the TQt3 framework (compatible with the Trinity Desktop Environment):
- **Virtual Tree-Table View**: Flat vector rendering flat list structures mapped to `TQTable` widgets, allowing instant sorting, expansion, and collapse of hundreds of processes without GUI lag.
- **Custom AA Painter**: High-performance anti-aliased graph and shape drawing (`tqtaapainter.cpp`) directly on `TQImage` arrays using Wu's line drawing algorithm.
- **Zero-Allocation Grouping**: Grouping processes uses `const char*` keys wrapped in a lightweight pointer key (`CharPtrKey`) to completely avoid heap allocations and string copies during GUI refreshes.
- **O(N) Pre-parsed Sorting**: Caches parsed sort keys once per node before sorting, avoiding redundant numeric conversion and string formatting inside the comparator loop.

### 3. Binary Size Optimization
The build system is tuned for minimal footprint, yielding a fully-featured desktop binary of only **605 KB**:
- **Link-Time Optimization (LTO)**: Enabled via `-flto` to inline functions across C and C++ translation units.
- **Garbage Collection of Sections**: Strips unused code at link time using `-ffunction-sections -fdata-sections -Wl,--gc-sections`.
- **Visibility Control**: Hides internal library symbols with `-fvisibility=hidden`.

---

## Build and Run Instructions

### Dependencies

Ensure the following libraries are installed on your Linux system:
- **TQt3** (`tqt-mt` development package)
- **GLib 2.0** (`glib-2.0`)
- **libsystemd** (version >= 221)
- **X11** and **libpci** development libraries
- **CMake** (version >= 3.10)

On Debian-based systems:
```bash
sudo apt-get install cmake build-essential libtqt3-mt-dev libglib2.0-dev libsystemd-dev libx11-dev libpci-dev
```

### Building the Project

You can compile the project either manually using CMake or automatically via the provided build scripts.

#### Option A: Quick Build (Recommended)
We provide a helper script that automates the build process in Release mode and automatically applies size optimizations (using `sstrip` if available on the system):
```bash
./build.sh
```
The optimized executable will be generated at `build/taskmgr`.

#### Option B: Manual CMake Build
If you prefer to compile manually:
1. Create a build directory:
   ```bash
   mkdir -p build
   cd build
   ```
2. Generate the build files and compile:
   ```bash
   cmake -DCMAKE_BUILD_TYPE=Release ..
   make -j$(nproc)
   ```
The compiled binary will be generated at `build/taskmgr`.

### Building the Debian Package (.deb)
To generate a Debian package (`taskmgr_1.0_<arch>.deb`) containing the binary, the desktop entry, and all application icons:
```bash
./build_deb.sh
```
This script will stage the package filesystem, strip the binary, generate dependencies metadata, and build the final `.deb` package at the root directory.

### Running

To launch the task manager:
```bash
./build/taskmgr
```

---

## Screenshots

| | | |
| :---: | :---: | :---: |
| <a href="screenshots/screenshot_taskmgr_1.jpg"><img src="screenshots/screenshot_taskmgr_1.jpg" width="230" alt="screenshot 1"></a> | <a href="screenshots/screenshot_taskmgr_2.jpg"><img src="screenshots/screenshot_taskmgr_2.jpg" width="230" alt="screenshot 2"></a> | <a href="screenshots/screenshot_taskmgr_3.jpg"><img src="screenshots/screenshot_taskmgr_3.jpg" width="230" alt="screenshot 3"></a> |
| <a href="screenshots/screenshot_taskmgr_4.jpg"><img src="screenshots/screenshot_taskmgr_4.jpg" width="230" alt="screenshot 4"></a> | <a href="screenshots/screenshot_taskmgr_5.jpg"><img src="screenshots/screenshot_taskmgr_5.jpg" width="230" alt="screenshot 5"></a> | <a href="screenshots/screenshot_taskmgr_6.jpg"><img src="screenshots/screenshot_taskmgr_6.jpg" width="115" alt="screenshot 6"></a> |
| <a href="screenshots/screenshot_taskmgr_7.jpg"><img src="screenshots/screenshot_taskmgr_7.jpg" width="230" alt="screenshot 7"></a> | <a href="screenshots/screenshot_taskmgr_8.jpg"><img src="screenshots/screenshot_taskmgr_8.jpg" width="230" alt="screenshot 8"></a> | <a href="screenshots/screenshot_taskmgr_9.jpg"><img src="screenshots/screenshot_taskmgr_9.jpg" width="230" alt="screenshot 9"></a> |
| <a href="screenshots/screenshot_taskmgr_10.jpg"><img src="screenshots/screenshot_taskmgr_10.jpg" width="230" alt="screenshot 10"></a> | <a href="screenshots/screenshot_taskmgr_11.jpg"><img src="screenshots/screenshot_taskmgr_11.jpg" width="230" alt="screenshot 11"></a> | <a href="screenshots/screenshot_taskmgr_12.jpg"><img src="screenshots/screenshot_taskmgr_12.jpg" width="230" alt="screenshot 12"></a> |
| <a href="screenshots/screenshot_taskmgr_13.jpg"><img src="screenshots/screenshot_taskmgr_13.jpg" width="230" alt="screenshot 13"></a> | <a href="screenshots/screenshot_taskmgr_14.jpg"><img src="screenshots/screenshot_taskmgr_14.jpg" width="230" alt="screenshot 14"></a> | <a href="screenshots/screenshot_taskmgr_15.jpg"><img src="screenshots/screenshot_taskmgr_15.jpg" width="230" alt="screenshot 15"></a> |
| <a href="screenshots/screenshot_taskmgr_16.jpg"><img src="screenshots/screenshot_taskmgr_16.jpg" width="230" alt="screenshot 16"></a> | <a href="screenshots/screenshot_taskmgr_17.jpg"><img src="screenshots/screenshot_taskmgr_17.jpg" width="230" alt="screenshot 17"></a> | |

