SUBDIRS = lib
ACLOCAL_AMFLAGS = -I m4
AM_CFLAGS = $(WERROR_CFLAGS) $(WARN_CFLAGS)
AM_CPPFLAGS = $(ISYSTEM)$(srcdir)/lib

bin_PROGRAMS = popcheck
popcheck_SOURCES = popcheck.c
popcheck_LDADD = lib/lib$(PACKAGE).a $(CURSES_LIB)

man_MANS = popcheck.1
EXTRA_DIST = m4/gnulib-cache.m4
