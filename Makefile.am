ACLOCAL_AMFLAGS = -I .

CC = gcc
CCDEPMODE = depmode=gcc3
CFLAGS = -O2 -fstrict-aliasing -flto=auto -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-unwind-tables -fomit-frame-pointer -ffast-math -fvisibility=hidden -fno-stack-protector -fno-ident -fmerge-all-constants -fuse-ld=gold -Wl,--gc-sections,--build-id=none,-O1,--strip-all,--hash-style=gnu -s
CPPFLAGS = -DG_DISABLE_ASSERT -DG_DISABLE_CHECKS -DG_DISABLE_CAST_CHECKS

SUBDIRS =	src

desktopdir = $(datadir)/applications
desktop_DATA = data/taskmgr.desktop

man_MANS = data/taskmgr.1

distclean-local:
	rm -rf *.cache *~

rpm: dist
	rpmbuild -ta $(PACKAGE)-$(VERSION).tar.gz
	@rm -f $(PACKAGE)-$(VERSION).tar.gz

EXTRA_DIST =								\
	data/taskmgr.desktop

DISTCLEANFILES =
