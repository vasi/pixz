ifneq ($(shell gcc -v 2>&1 | grep 'Apple Inc'),)
	APPLE=1
endif

OPT = -g -O0
MYCFLAGS = $(patsubst %,-I%/include,$(LIBPREFIX)) $(OPT) -std=gnu99 \
	-Wall -Wno-unknown-pragmas
MYLDFLAGS = $(patsubst %,-L%/lib,$(LIBPREFIX)) $(OPT) -Wall

THREADS = -lpthread
LIBADD = $(THREADS) -llzma -larchive

CC = gcc
COMPILE = $(CC) $(MYCFLAGS) $(CFLAGS) -c -o
LD = $(CC) $(MYLDFLAGS) $(LDFLAGS) -o

ifdef APPLE
ifeq ($(CC),gcc)
	MYLDFLAGS += -search_paths_first
endif
endif

PROGS = pixz
COMMON = common.o endian.o cpu.o read.o write.o list.o

all: $(PROGS)

%.o: %.c pixz.h
	$(COMPILE) $@ $<

$(PROGS): %: %.o $(COMMON)
	$(LD) $@ $^ $(LIBADD)

clean:
	rm -f *.o $(PROGS)

.PHONY: all clean
