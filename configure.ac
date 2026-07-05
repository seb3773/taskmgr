fAC_PREREQ([2.57])
AC_INIT(taskmgr,[0.2], [],taskmgr)
AM_INIT_AUTOMAKE([foreign no-dist-gzip dist-xz])

AM_MAINTAINER_MODE([enable])
AC_CONFIG_MACRO_DIR([.])

AC_CONFIG_HEADERS([config.h])

dnl Support silent build rules. Disable by either passing --disable-silent-rules
dnl to configure or passing V=1 to make
AM_SILENT_RULES([yes])

AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_CC_C_O

# Set optimized flags if not in debug mode
if test "$enable_debug" != "yes"; then
    CFLAGS="-g0 -O2 -fstrict-aliasing -flto=auto -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-unwind-tables -fomit-frame-pointer -fvisibility=hidden -fno-stack-protector -fno-ident -fmerge-all-constants -fno-exceptions -fno-plt -fno-semantic-interposition -fno-stack-check -fno-trapping-math -fno-math-errno -DNDEBUG -DNO_DEBUG"
    CPPFLAGS="$CPPFLAGS -DG_DISABLE_ASSERT -DG_DISABLE_CHECKS -DG_DISABLE_CAST_CHECKS -DNDEBUG -DNO_DEBUG"
    LDFLAGS="-fuse-ld=gold -Wl,--gc-sections,--build-id=none,-O1,--strip-all,--hash-style=gnu,--as-needed,--no-undefined,--no-allow-shlib-undefined,--no-undefined-version,--no-keep-memory,-z,nodlopen,-z,nodump,-z,noexecstack,-z,now,-z,relro,-z,combreloc -s"
fi

dnl Check for UNIX variants
AC_AIX()
AC_ISC_POSIX()
#AC_MINIX()

AC_HEADER_STDC()
AC_CHECK_HEADERS([dirent.h pwd.h sys/types.h sys/stat.h sys/param.h \
                  stdio.h stdlib.h string.h unistd.h stdlib.h signal.h])

AC_ARG_ENABLE(gtk3,
[  --enable-gtk3        enable to use gtk-3.0 instead of gtk-2.0 ],
[case "${enableval}" in
  yes)  enable_gtk3=yes ;;
  *)   enable_gtk3=no ;;
esac],[])

echo "x$enable_gtk3"
if test "x$enable_gtk3" = "xyes" ; then
  CFLAGS="$CFLAGS -Wno-deprecated-declarations"
  pkg_modules="gtk+-3.0 >= 3.0.0"
else
  pkg_modules="gtk+-2.0 >= 2.6.0"
fi

PKG_CHECK_MODULES(GTK, [$pkg_modules])
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)

# Check for systemd
PKG_CHECK_MODULES(SYSTEMD, [libsystemd >= 221])
AC_SUBST(SYSTEMD_CFLAGS)
AC_SUBST(SYSTEMD_LIBS)

# Localization support removed

AC_ARG_ENABLE(debug,
    [AC_HELP_STRING([--enable-debug],
        [build qxtask with debug support @<:@default=no@:>@])],
    [enable_debug="${enableval}"],
    [enable_debug=no]
)

if test "$enable_debug" = "yes"; then
    # turn on debug and disable optimization
    CPPFLAGS="$CPPFLAGS -DG_ENABLE_DEBUG -O0 -g"
    case "$CC" in
    gcc*)
        CPPFLAGS="$CPPFLAGS -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers"
        ;;
    *)
        ;;
    esac
    dnl Be more strict on portability
    CPPFLAGS="$CPPFLAGS -D_POSIX_C_SOURCE=200112L -D_XOPEN_SOURCE=700"
else
    # turn off glib debug checks (already set above for non-debug mode)
    :
fi

AC_OUTPUT([
  Makefile
  src/Makefile
  data/taskmgr.1
])

echo
echo TaskMgr...................................... : Version $VERSION
echo
echo Prefix....................................... : $prefix
echo
echo The binary will be installed in $prefix/bin
echo
echo http://lxde.org/
echo
echo Have fun!
echo
