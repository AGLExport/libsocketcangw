bin_PROGRAMS = gwtest gwunittest parsertest 

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


gwunittest_SOURCES = gw-unit-test.c

gwunittest_LDADD = \
	@CMOCKA_LIBS@

gwunittest_CFLAGS = $(gwtest_CFLAGS)


parsertest_SOURCES = libsocketcangw_config_test.c

parsertest_LDADD = \
	@CMOCKA_LIBS@ @LIBCJSON_LIBS@

parsertest_CFLAGS = \
	-g \
	-fsanitize=address -coverage \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/include \
	@CMOCKA_CFLAGS@ @LIBCJSON_CFLAGS@ \
	-D_GNU_SOURCE

CLEANFILES = *.gcda *.gcno