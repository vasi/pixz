ifneq ($(shell gcc -v 2>&1 | grep 'Apple Inc'),)
	APPLE=1
endif

LIBPREFIX = /Library/Fink/sl64 /opt/local
LDFLAGS = $(patsubst %,-L%/lib,$(LIBPREFIX)) -g -Wall
ifdef APPLE
ifeq ($(CC),gcc)
	LDFLAGS += -search_paths_first
endif
endif
CFLAGS = $(patsubst %,-I%/include,$(LIBPREFIX)) -g -O0 -std=c99 \
	-Wall -Wno-unknown-pragmas

CC = gcc
COMPILE = $(CC) $(CFLAGS) -c -o
LD = $(CC) $(LDFLAGS) -o

PROGS = write read list
COMMON = common.o endian.o cpu.o

all: $(PROGS)

%.o: %.c pixz.h
	$(COMPILE) $@ $<

$(PROGS): %: %.o $(COMMON)
	$(LD) $@ $^ -llzma -larchive

clean:
	rm -f *.o $(PROGS)

.PHONY: all clean
