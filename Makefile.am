ACLOCAL_AMFLAGS = -I m4

SUBDIRS =

AM_CPPFLAGS = $(EXTRA_CPPFLAGS)
AM_CFLAGS = $(EXTRA_CFLAGS)

mobile_CPPFLAGS = $(AM_CPPFLAGS)
mobile_CFLAGS = $(AM_CFLAGS)
mobile_LDFLAGS = -static
mobile_LDADD = $(EXTRA_LIBS)

if WITH_SYSTEM_LIBMOBILE
mobile_CFLAGS += $(LIBMOBILE_CFLAGS)
mobile_LDADD += $(LIBMOBILE_LIBS)
else
SUBDIRS += subprojects/libmobile
mobile_CPPFLAGS += -I subprojects/libmobile -I $(srcdir)/subprojects/libmobile
mobile_LDADD += subprojects/libmobile/libmobile.la
endif

DIST_SUBDIRS = $(SUBDIRS)
AM_DISTCHECK_CONFIGURE_FLAGS = --without-system-libmobile

bin_PROGRAMS = mobile

mobile_SOURCES = \
	source/bgblink.c \
	source/bgblink.h \
	source/main.c \
	source/socket.c \
	source/socket.h \
	source/socket_impl.c \
	source/socket_impl.h

EXTRA_DIST = \
	meson.build \
	CMakeLists.txt
