bin_PROGRAMS = pixz

pixz_CC = $(PTHREAD_CC)
pixz_CFLAGS = $(PTHREAD_CFLAGS) -Wall -Wno-unknown-pragmas
pixz_CPPFLAGS = $(LIBARCHIVE_CPPFLAGS) $(LZMA_CPPFLAGS)
pixz_LDADD = $(LIBARCHIVE_LIBS) $(LZMA_LIBS) $(PTHREAD_LIBS)

pixz_SOURCES = \
	common.c \
	cpu.c \
	endian.c \
	list.c \
	pixz.c \
	pixz.h \
	read.c \
	write.c

pixz.1: pixz.1.asciidoc
	$(A2X) -a manversion=$(PACKAGE_VERSION) -f manpage pixz.1.asciidoc

man_MANS = pixz.1

EXTRA_DIST = pixz.1.asciidoc
