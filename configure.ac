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

AC_DEFUN([MY_CHECK_FLAG], [dnl
AS_VAR_PUSHDEF([CHECK], [my_cv_check_m4_$1_$2])dnl
AC_CACHE_CHECK([whether the compiler accepts $2 in $1], CHECK, [dnl
    SAVE_FLAGS="$$1"; $1="$$1 $2"
    AC_LINK_IFELSE([AC_LANG_PROGRAM()],
        [AS_VAR_SET(CHECK, [yes])], [AS_VAR_SET(CHECK, [no])])
    $1="$SAVE_FLAGS"])
AS_IF([test "$my_cv_check_$1_$2" = yes],
    [m4_default([$3])], [m4_default([$4])])
AS_VAR_POPDEF([CHECK])dnl
])dnl

# Enable -ffunction-sections -fdata-sections and -Wl,--gc-sections by default
MY_CHECK_FLAG([LDFLAGS], [-Wl,--gc-sections], [dnl
    CFLAGS="-ffunction-sections -fdata-sections $CFLAGS"
    LDFLAGS="-Wl,--gc-sections $LDFLAGS"])

# Use modified flags in subprojects
export CFLAGS LDFLAGS

# Include libmobile library
AC_ARG_WITH([system-libmobile], AS_HELP_STRING([--with-system-libmobile],
    [use a shared copy of libmobile rather than the bundled one]))
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
MY_CHECK_FLAG([CFLAGS], [-pthread], [dnl
    EXTRA_CFLAGS="$EXTRA_CFLAGS -pthread"])

# Link windows libraries
AS_CASE([$host_os], [mingw*], [dnl
    AC_CHECK_LIB([ws2_32], [main], [EXTRA_LIBS="$EXTRA_LIBS -lws2_32"])])

AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_HEADERS([confdefs.h])
AC_CONFIG_FILES([Makefile])

AC_OUTPUT
