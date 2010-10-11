ifneq ($(shell gcc -v 2>&1 | grep 'Apple Inc'),)
	APPLE=1
endif

LIBPREFIX = /Library/Fink/sl64
LDFLAGS = -L$(LIBPREFIX)/lib -g -Wall
ifdef APPLE
ifeq ($(CC),gcc)
	LDFLAGS += -search_paths_first
endif
endif
CFLAGS = -I$(LIBPREFIX)/include -g -O0 -std=c99 -Wall -Wno-unknown-pragmas

CC = gcc
COMPILE = $(CC) $(CFLAGS) -c -o
LD = $(CC) $(LDFLAGS) -o

PROGS = write read list

all: $(PROGS)

%.o: %.c pixz.h
	$(COMPILE) $@ $<

list: list.o common.o endian.o
	$(LD) $@ $^ -llzma

write: write.o common.o endian.o cpu.o
	$(LD) $@ $^ -larchive -llzma

read: read.o common.o endian.o
	$(LD) $@ $^ -llzma

clean:
	rm -f *.o $(PROGS)

.PHONY: all clean
