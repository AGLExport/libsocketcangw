bin_PROGRAMS = gwtest

gwtest_SOURCES = gwtest.c

gwtest_LDADD = \
	@CMOCKA_LIBS@

gwtest_CFLAGS = \
	-g \
	-fsanitize=address -coverage \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/include \
	@CMOCKA_CFLAGS@ \
	-D_GNU_SOURCE

CLEANFILES = *.gcda *.gcno
