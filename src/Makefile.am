taskmgr_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-DG_LOG_DOMAIN=\"taskmgr\" \
	-DG_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED \
	-DGDK_PIXBUF_DISABLE_DEPRECATED -DGLIB_DISABLE_DEPRECATION_WARNINGS \
	-D_GNU_SOURCE -DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_32 \
	-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_32 \
	$(DEBUG_FLAGS)

bin_PROGRAMS = taskmgr

taskmgr_SOURCES = \
	main.c \
	interface.c \
	callbacks.c \
	functions.c \
	disk_manager.c \
	network_manager.c \
	autostart_manager.c \
	app_manager.c \
	app_dialog.c \
	editor_dialog.c \
	manager_common.c \
	service_manager.c \
	user_disconnect.c \
	responsive_layout.c \
	keyboard_navigation.c \
	taskmgr-linux.c \
	utils.c \
	manager_registry.c \
	dialog_pool.c \
	fast_format.c \
	gpu_stats.c \
	pss_thread_simd.c

taskmgr_CFLAGS =	$(GTK_CFLAGS) $(SYSTEMD_CFLAGS) -msse2

taskmgr_LDADD = $(GTK_LIBS) $(SYSTEMD_LIBS) -lX11 -lpci -lpthread
