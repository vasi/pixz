ifneq ($(shell gcc -v 2>&1 | grep 'Apple Inc'),)
	APPLE=1
endif

LIBPREFIX = /Library/Fink/sl64 /opt/local
ifdef APPLE
ifeq ($(CC),gcc)
	LDFLAGS += -search_paths_first
endif
endif
OPT = -g -O0
CFLAGS = $(patsubst %,-I%/include,$(LIBPREFIX)) $(OPT) -std=gnu99 \
	-Wall -Wno-unknown-pragmas
    LDFLAGS = $(patsubst %,-L%/lib,$(LIBPREFIX)) $(OPT) -Wall

CC = gcc
COMPILE = $(CC) $(CFLAGS) -c -o
LD = $(CC) $(LDFLAGS) -o

PROGS = pixz
COMMON = common.o endian.o cpu.o read.o write.o list.o

all: $(PROGS)

%.o: %.c pixz.h
	$(COMPILE) $@ $<

$(PROGS): %: %.o $(COMMON)
	$(LD) $@ $^ -llzma -larchive

clean:
	rm -f *.o $(PROGS)

.PHONY: all clean
