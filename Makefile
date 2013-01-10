VERSION = 1.0.2
DISTNAME = pixz-$(VERSION)
TARBALL = $(DISTNAME).tgz

ifneq ($(shell gcc -v 2>&1 | grep 'Apple Inc'),)
	APPLE=1
endif

OPT = -g -O0
MYCFLAGS = $(patsubst %,-I%/include,$(LIBPREFIX)) $(OPT) -std=gnu99 \
	-Wall -Wno-unknown-pragmas -DPIXZ_VERSION='"$(VERSION)"'
MYLDFLAGS = $(patsubst %,-L%/lib,$(LIBPREFIX)) $(OPT) -Wall

THREADS = -lpthread
LIBADD = $(THREADS) -lm -llzma -larchive

CC = gcc
COMPILE = $(CC) $(MYCFLAGS) $(CFLAGS) -c -o
LD = $(CC) $(MYLDFLAGS) $(LDFLAGS) -o

ifdef APPLE
ifeq ($(CC),gcc)
	MYLDFLAGS += -search_paths_first
endif
endif

PROGS = pixz
MANPAGE = pixz.1
COMMON = common.o endian.o cpu.o read.o write.o list.o

all: $(PROGS)

%.o: %.c pixz.h
	$(COMPILE) $@ $<

$(PROGS): %: %.o $(COMMON)
	$(LD) $@ $^ $(LIBADD)

clean:
	rm -rf *.o $(PROGS) $(MANPAGE) $(TARBALL) dist

$(MANPAGE): pixz.1.asciidoc
	a2x -a manversion=$(VERSION) -f manpage $<

dist:
	rm -rf dist
	mkdir -p dist
	git archive --prefix=$(DISTNAME)/ --format=tar HEAD | tar -x -C dist

$(TARBALL): $(MANPAGE) dist
	cp pixz.1 dist/$(DISTNAME)/
	tar -czf $(TARBALL) -C dist $(DISTNAME)

tarball: $(TARBALL)

.PHONY: all clean tarball dist
