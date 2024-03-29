AC_PREREQ([2.71])
LT_PREREQ([2.4.6])
AC_INIT([libmobile-bgb], [0.2.0])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIRS([m4])
AM_INIT_AUTOMAKE([1.16.5 -Wall foreign subdir-objects])
AM_SILENT_RULES([yes])

AC_PROG_CC
LT_INIT([win32-dll])
PKG_PROG_PKG_CONFIG

EXTRA_CPPFLAGS=""
EXTRA_CFLAGS=""
EXTRA_LIBS=""
AC_SUBST([EXTRA_CPPFLAGS])
AC_SUBST([EXTRA_CFLAGS])
AC_SUBST([EXTRA_LIBS])

AC_DEFUN([MY_CHECK_FLAG], [dnl
AS_VAR_PUSHDEF([CHECK], [my_cv_check_flag_$1_$2])dnl
AC_CACHE_CHECK([whether the compiler accepts $2 in $1], CHECK, [dnl
    SAVE_FLAGS="$$1"; $1="$$1 $2"
    AC_LINK_IFELSE([AC_LANG_PROGRAM()],
        [AS_VAR_SET(CHECK, [yes])], [AS_VAR_SET(CHECK, [no])])
    $1="$SAVE_FLAGS"])
AS_VAR_IF(CHECK, [yes], [m4_default([$3])], [m4_default([$4])])
AS_VAR_POPDEF([CHECK])dnl
])dnl

# Default cflags
AS_IF([test "$GCC" = yes], [dnl
    EXTRA_CFLAGS="$EXTRA_CFLAGS -std=gnu11 -Wall -Wextra"])

# Enable -ffunction-sections -fdata-sections and -Wl,--gc-sections by default
MY_CHECK_FLAG([LDFLAGS], [-Wl,--gc-sections], [dnl
    CFLAGS="-ffunction-sections -fdata-sections $CFLAGS"
    LDFLAGS="-Wl,--gc-sections $LDFLAGS"])

# Use modified flags in subprojects
export CFLAGS LDFLAGS

# Include libmobile library
AC_ARG_WITH([system-libmobile], AS_HELP_STRING([--with-system-libmobile],
    [use a system-wide copy of libmobile]))
found_libmobile=no
AS_IF([test "$with_system_libmobile" != no], [dnl
    PKG_CHECK_MODULES([LIBMOBILE], [libmobile >= 0.2.0],
        [found_libmobile=yes], [:])])
AS_IF([test "$with_system_libmobile" = yes -a "$found_libmobile" != yes], [dnl
    AC_MSG_FAILURE([libmobile not found])])
AS_IF([test "$found_libmobile" = no], [dnl
    AC_CONFIG_SUBDIRS([subprojects/libmobile])])
AM_CONDITIONAL([WITH_SYSTEM_LIBMOBILE], [test "$found_libmobile" = yes])

# Link windows libraries
AS_CASE([$host_os], [mingw*], [dnl
    EXTRA_CPPFLAGS="$EXTRA_CPPFLAGS -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0501"
    EXTRA_CPPFLAGS="$EXTRA_CPPFLAGS -D_CRT_SECURE_NO_WARNINGS"
    AC_CHECK_LIB([ws2_32], [exit], [EXTRA_LIBS="$EXTRA_LIBS -lws2_32"],
        [AC_MSG_FAILURE([ws2_32 not found])])])

AC_CONFIG_HEADERS([autoconf.h])
AC_CONFIG_FILES([Makefile])

AC_OUTPUT
