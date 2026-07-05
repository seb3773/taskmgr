cmake_minimum_required(VERSION 3.10)
project(taskmgr C CXX)

set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Aggressive optimizations (inspired by tdeclamui, -O2 instead of -Os).
# Disable with: cmake -DTASKMGR_AGGRESSIVE_FLAGS=OFF ..
option(TASKMGR_AGGRESSIVE_FLAGS "Enable aggressive -O2/LTO compile flags" ON)

option(TASKMGR_ASAN "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(TASKMGR_ASAN)
    set(TASKMGR_AGGRESSIVE_FLAGS OFF CACHE BOOL "Enable aggressive flags" FORCE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set(CMAKE_C_FLAGS "-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer")
    set(CMAKE_CXX_FLAGS "-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -std=c++03 -fno-threadsafe-statics -Wno-deprecated-declarations -fpermissive")
    set(CMAKE_EXE_LINKER_FLAGS "-fsanitize=address,undefined")
    message(STATUS "AddressSanitizer and UndefinedBehaviorSanitizer are ENABLED")
endif()

if(TASKMGR_AGGRESSIVE_FLAGS)
    set(_TASKMGR_C_FLAGS "-O2 -flto=auto -fstrict-aliasing -ffunction-sections -fdata-sections -fno-ident -fno-plt -fomit-frame-pointer -fno-stack-protector -fno-math-errno -fvisibility=hidden -fmerge-all-constants -DNDEBUG")
    set(_TASKMGR_CXX_EXTRA "-std=c++03 -fno-threadsafe-statics -fno-exceptions -fmerge-all-constants -Wno-deprecated-declarations -fpermissive")
    set(CMAKE_C_FLAGS "${_TASKMGR_C_FLAGS}")
    set(CMAKE_CXX_FLAGS "${_TASKMGR_C_FLAGS} ${_TASKMGR_CXX_EXTRA}")
    set(CMAKE_EXE_LINKER_FLAGS "-Wl,-O1 -Wl,--gc-sections -Wl,--as-needed -flto=auto -s")
    set(CMAKE_C_FLAGS_DEBUG "")
    set(CMAKE_C_FLAGS_RELEASE "")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "")
    set(CMAKE_C_FLAGS_MINSIZEREL "")
    set(CMAKE_CXX_FLAGS_DEBUG "")
    set(CMAKE_CXX_FLAGS_RELEASE "")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "")
    set(CMAKE_CXX_FLAGS_MINSIZEREL "")
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()
else()
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-std=c++03>)
endif()

add_compile_definitions(WITHOUT_GTK=1 _GNU_SOURCE=1)

find_package(PkgConfig REQUIRED)
pkg_check_modules(TQT REQUIRED tqt-mt)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(SYSTEMD REQUIRED "libsystemd>=221")

find_program(TQMOC_EXECUTABLE NAMES tqmoc moc-tqt)
if (NOT TQMOC_EXECUTABLE)
    message(FATAL_ERROR "tqmoc (or moc-tqt) not found")
endif()

# We need X11, pci, pthread
find_package(Threads REQUIRED)
find_package(X11 REQUIRED)

include_directories(
    ${TQT_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
    ${SYSTEMD_INCLUDE_DIRS}
    ${X11_INCLUDE_DIRS}
    /usr/include/tqt
    /opt/trinity/include
    /opt/trinity/include/tde
    ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/mvc
)

# MOC generation helper macro
macro(TQT_MOC_HEADER_FILE infile outfile)
    add_custom_command(
        OUTPUT ${outfile}
        COMMAND ${TQMOC_EXECUTABLE} ${infile} -o ${outfile}
        DEPENDS ${infile}
    )
endmacro()

# List of headers to moc
tqt_moc_header_file(${CMAKE_SOURCE_DIR}/src/mvc/tqtabstractitemmodel.h ${CMAKE_BINARY_DIR}/tqtabstractitemmodel.moc)
tqt_moc_header_file(${CMAKE_SOURCE_DIR}/src/mvc/tqtabstractlistmodel.h ${CMAKE_BINARY_DIR}/tqtabstractlistmodel.moc)
tqt_moc_header_file(${CMAKE_SOURCE_DIR}/src/mvc/tqtliststore.h ${CMAKE_BINARY_DIR}/tqtliststore.moc)
tqt_moc_header_file(${CMAKE_SOURCE_DIR}/src/mvc/tqtmvctableview.h ${CMAKE_BINARY_DIR}/tqtmvctableview.moc)
tqt_moc_header_file(${CMAKE_SOURCE_DIR}/src/mvc/tqtmvctreeview.h ${CMAKE_BINARY_DIR}/tqtmvctreeview.moc)
tqt_moc_header_file(${CMAKE_SOURCE_DIR}/src/mvc/tqttreestore.h ${CMAKE_BINARY_DIR}/tqttreestore.moc)

# Main window, processes tab and main app to moc
tqt_moc_header_file(${CMAKE_SOURCE_DIR}/src/taskmgr_mainwindow.h ${CMAKE_BINARY_DIR}/taskmgr_mainwindow.moc)
tqt_moc_header_file(${CMAKE_SOURCE_DIR}/src/processes_tab.h ${CMAKE_BINARY_DIR}/processes_tab.moc)
tqt_moc_header_file(${CMAKE_SOURCE_DIR}/src/preferences_dialog.h ${CMAKE_BINARY_DIR}/preferences_dialog.moc)
tqt_moc_header_file(${CMAKE_SOURCE_DIR}/src/main.cpp ${CMAKE_BINARY_DIR}/main.moc)
tqt_moc_header_file(${CMAKE_SOURCE_DIR}/src/mini_block_widget.h ${CMAKE_BINARY_DIR}/mini_block_widget.moc)
tqt_moc_header_file(${CMAKE_SOURCE_DIR}/src/performance_graph_widget.h ${CMAKE_BINARY_DIR}/performance_graph_widget.moc)
tqt_moc_header_file(${CMAKE_SOURCE_DIR}/src/info_block_widget.h ${CMAKE_BINARY_DIR}/info_block_widget.moc)
tqt_moc_header_file(${CMAKE_SOURCE_DIR}/src/performance_tab.h ${CMAKE_BINARY_DIR}/performance_tab.moc)
tqt_moc_header_file(${CMAKE_SOURCE_DIR}/src/startup_tab.h ${CMAKE_BINARY_DIR}/startup_tab.moc)
tqt_moc_header_file(${CMAKE_SOURCE_DIR}/src/services_tab.h ${CMAKE_BINARY_DIR}/services_tab.moc)
tqt_moc_header_file(${CMAKE_SOURCE_DIR}/src/users_tab.h ${CMAKE_BINARY_DIR}/users_tab.moc)
tqt_moc_header_file(${CMAKE_SOURCE_DIR}/src/taskmgr_system_tray.h ${CMAKE_BINARY_DIR}/taskmgr_system_tray.moc)
tqt_moc_header_file(${CMAKE_SOURCE_DIR}/src/service_details_dialog.h ${CMAKE_BINARY_DIR}/service_details_dialog.moc)
tqt_moc_header_file(${CMAKE_SOURCE_DIR}/src/process_details_dialog.h ${CMAKE_BINARY_DIR}/process_details_dialog.moc)
tqt_moc_header_file(${CMAKE_SOURCE_DIR}/src/root_password_dialog.h ${CMAKE_BINARY_DIR}/root_password_dialog.moc)
tqt_moc_header_file(${CMAKE_SOURCE_DIR}/src/run_new_task_dialog.h ${CMAKE_BINARY_DIR}/run_new_task_dialog.moc)

set(MVC_SOURCES
    src/mvc/tqtabstractitemmodel.cpp
    src/mvc/tqtabstractlistmodel.cpp
    src/mvc/tqtliststore.cpp
    src/mvc/tqtmvctableview.cpp
    src/mvc/tqtmvctreeview.cpp
    src/mvc/tqttreestore.cpp
)

set(BACKEND_SOURCES
    src/app_manager.c
    src/autostart_manager.c
    src/disk_manager.c
    src/fast_format.c
    src/functions.c
    src/gpu_stats.c
    src/manager_common.c
    src/manager_registry.c
    src/network_manager.c
    src/pss_thread_simd.c
    src/service_manager.c
    src/process_details.c
    src/root_credential_vault.c
    src/privileged_exec.c
    src/privileged_policy.c
    src/user_disconnect.c
    src/taskmgr-linux.c
    src/utils.c
    src/process_launcher.c
    src/backend_bridge.c
)

set(FRONTEND_SOURCES
    src/main.cpp
    src/taskmgr_mainwindow.cpp
    src/processes_tab.cpp
    src/preferences_dialog.cpp
    src/mini_block_widget.cpp
    src/performance_graph_widget.cpp
    src/info_block_widget.cpp
    src/performance_tab.cpp
    src/startup_tab.cpp
    src/services_tab.cpp
    src/service_details_dialog.cpp
    src/process_details_dialog.cpp
    src/root_password_dialog.cpp
    src/run_new_task_dialog.cpp
    src/about_dialog.cpp
    src/taskmgr_privileged_ops.cpp
    src/app_icons.cpp
    src/users_tab.cpp
    src/cpu_tray_icon.cpp
    src/taskmgr_system_tray.cpp
    src/tde_icon_loader.cpp
    src/compact_app_list.cpp
    src/tqtaapainter.cpp
)

if(TASKMGR_AGGRESSIVE_FLAGS)
    set_source_files_properties(
        src/main.cpp
        src/taskmgr_mainwindow.cpp
        src/preferences_dialog.cpp
        src/mini_block_widget.cpp
        src/info_block_widget.cpp
        src/startup_tab.cpp
        src/services_tab.cpp
        src/service_details_dialog.cpp
        src/process_details_dialog.cpp
        src/root_password_dialog.cpp
        src/run_new_task_dialog.cpp
        src/about_dialog.cpp
        src/taskmgr_privileged_ops.cpp
        src/app_icons.cpp
        src/users_tab.cpp
        src/cpu_tray_icon.cpp
        src/taskmgr_system_tray.cpp
        src/tde_icon_loader.cpp
        src/compact_app_list.cpp
        PROPERTIES COMPILE_FLAGS "-Os"
    )
endif()

set(MOC_OUTPUTS
    ${CMAKE_BINARY_DIR}/tqtabstractitemmodel.moc
    ${CMAKE_BINARY_DIR}/tqtabstractlistmodel.moc
    ${CMAKE_BINARY_DIR}/tqtliststore.moc
    ${CMAKE_BINARY_DIR}/tqtmvctableview.moc
    ${CMAKE_BINARY_DIR}/tqtmvctreeview.moc
    ${CMAKE_BINARY_DIR}/tqttreestore.moc
    ${CMAKE_BINARY_DIR}/taskmgr_mainwindow.moc
    ${CMAKE_BINARY_DIR}/processes_tab.moc
    ${CMAKE_BINARY_DIR}/preferences_dialog.moc
    ${CMAKE_BINARY_DIR}/main.moc
    ${CMAKE_BINARY_DIR}/mini_block_widget.moc
    ${CMAKE_BINARY_DIR}/performance_graph_widget.moc
    ${CMAKE_BINARY_DIR}/info_block_widget.moc
    ${CMAKE_BINARY_DIR}/performance_tab.moc
    ${CMAKE_BINARY_DIR}/startup_tab.moc
    ${CMAKE_BINARY_DIR}/services_tab.moc
    ${CMAKE_BINARY_DIR}/users_tab.moc
    ${CMAKE_BINARY_DIR}/taskmgr_system_tray.moc
    ${CMAKE_BINARY_DIR}/service_details_dialog.moc
    ${CMAKE_BINARY_DIR}/process_details_dialog.moc
    ${CMAKE_BINARY_DIR}/root_password_dialog.moc
    ${CMAKE_BINARY_DIR}/run_new_task_dialog.moc
)

add_executable(taskmgr
    ${MVC_SOURCES}
    ${BACKEND_SOURCES}
    ${FRONTEND_SOURCES}
    ${MOC_OUTPUTS}
)

# SIMD optimization flag for PSS threads
set_source_files_properties(src/pss_thread_simd.c PROPERTIES COMPILE_FLAGS "-msse2")

target_compile_options(taskmgr PRIVATE ${TQT_CFLAGS_OTHER} ${GLIB_CFLAGS_OTHER} ${SYSTEMD_CFLAGS_OTHER})
target_link_libraries(taskmgr
    ${TQT_LIBRARIES}
    ${GLIB_LIBRARIES}
    ${SYSTEMD_LIBRARIES}
    ${X11_LIBRARIES}
    pci
    Threads::Threads
    /opt/trinity/lib/libtdecore.so
    /opt/trinity/lib/libtdeui.so
)
