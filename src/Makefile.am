AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

lib_LTLIBRARIES = libsocketcangw.la

libsocketcangw_la_SOURCES = \
	libsocketcangw.c

libsocketcangw_la_LBSADD = 

libsocketcangw_la_CFLAGS = \
	-g \
	-I$(top_srcdir)/include \
	-D_GNU_SOURCE 

libsocketcangw_la_LDFLAGS = \
	-export-symbols $(top_srcdir)/src/libsocketcangw.sym -version-info 0:0:0

# configure option 
if ENABLE_ADDRESS_SANITIZER
CFLAGS   += -fsanitize=address
endif

if ENABLE_GCOV
CFLAGS   += -coverage
endif

CLEANFILES = *.gcda *.gcno