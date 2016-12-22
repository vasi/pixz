bin_PROGRAMS = pixz

pixz_CC = $(PTHREAD_CC)
pixz_CFLAGS = $(PTHREAD_CFLAGS) -Wall -Wno-unknown-pragmas
pixz_CPPFLAGS = $(LIBARCHIVE_CFLAGS) $(LZMA_CFLAGS)
pixz_LDADD = -lm $(LIBARCHIVE_LIBS) $(LZMA_LIBS) $(PTHREAD_LIBS)

pixz_SOURCES = \
	common.c \
	cpu.c \
	endian.c \
	list.c \
	pixz.c \
	pixz.h \
	read.c \
	write.c

if MANPAGE
# TODO remove when possible: This is a hack because a2x is not able to output the man pages to a
# specific directory, only to where the source is.
pixz.1: pixz.1.asciidoc
	$(A2X) -a manversion=$(PACKAGE_VERSION) -f manpage $(top_srcdir)/src/pixz.1.asciidoc
	if ! test -f pixz.1 ; then mv -f $(top_srcdir)/src/pixz.1 . ; fi

man_MANS = pixz.1

CLEANFILES = pixz.1

EXTRA_DIST = $(man_MANS) pixz.1.asciidoc
endif
