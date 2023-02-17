AC_PREREQ([2.71])
LT_PREREQ([2.4.7])
AC_INIT([libmobile-bgb], [0.2.0])
AM_INIT_AUTOMAKE([1.16.5 -Wall foreign subdir-objects])

AC_PROG_CC
LT_INIT([win32-dll])
PKG_PROG_PKG_CONFIG

EXTRA_LIBS=""
EXTRA_CFLAGS=""
AC_SUBST([EXTRA_LIBS])
AC_SUBST([EXTRA_CFLAGS])

# Enable -ffunction-sections -fdata-sections and -Wl,--gc-sections by default
AC_CACHE_CHECK([whether the linker accepts -Wl,--gc-sections],
    [my_cv_check_ldflags_gc_sections], [dnl
    SAVE_LDFLAGS="$LDFLAGS"
    LDFLAGS="-Wl,--gc-sections $LDFLAGS"
    AC_LINK_IFELSE([AC_LANG_PROGRAM()],
        [my_cv_check_ldflags_gc_sections=yes],
        [my_cv_check_ldflags_gc_sections=no])
    LDFLAGS="$SAVE_LDFLAGS"])
AS_IF([test "$my_cv_check_ldflags_gc_sections" = yes], [dnl
    CFLAGS="-ffunction-sections -fdata-sections $CFLAGS"
    LDFLAGS="-Wl,--gc-sections $LDFLAGS"])

# Use modified flags in subprojects
export CFLAGS LDFLAGS

# Include libmobile library
AC_ARG_WITH([system-libmobile], AS_HELP_STRING([--with-system-libmobile],
    [use a shared version of libmobile rather than the bundled one]))
AS_IF([test "$with_system_libmobile" != no], [dnl
    PKG_CHECK_MODULES_STATIC([LIBMOBILE], [libmobile >= 0.2.0],
        [with_system_libmobile=yes],
        [with_system_libmobile=no]
        AS_IF([test "$with_system_libmobile" = yes],
            AC_MSG_FAILURE([libmobile not found])))])
AS_IF([test "$with_system_libmobile" = no], [dnl
    AC_CONFIG_SUBDIRS([subprojects/libmobile])])
AM_CONDITIONAL([WITH_SYSTEM_LIBMOBILE], [test "$with_system_libmobile" = yes])

# Add threading support
AS_IF([test "$GCC" = yes], [dnl
    EXTRA_CFLAGS="$EXTRA_CFLAGS -pthread"])

# Link windows libraries
AS_CASE([$host_os], [mingw*], [dnl
    AC_CHECK_LIB([ws2_32], [socket], [EXTRA_LIBS="$EXTRA_LIBS -lws2_32"])])

AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_HEADERS([confdefs.h])
AC_CONFIG_FILES([Makefile])

AC_OUTPUT
