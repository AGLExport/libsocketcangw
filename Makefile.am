SUBDIRS = src

if ENABLE_TEST
SUBDIRS += test
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libsocketcangw.pc

CLEANFILES = libsocketcangw.pc

include_HEADERS = \
    include/libsocketcangw.h