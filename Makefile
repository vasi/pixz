#LDFLAGS = -Llibs -search_paths_first -g -Wall
LDFLAGS = -L/Library/Fink/sl64/lib -search_paths_first -g -Wall
CFLAGS = -I/Library/Fink/sl64/include -g -O0 -std=c99 -Wall

CC = gcc $(CFLAGS) -c -o
LD = gcc $(LDFLAGS) -o

PROGS = write read list

all: $(PROGS)

%.o: %.c pixz.h
	$(CC) $@ $<

list: list.o common.o
	$(LD) $@ $^ -llzma

write: write.o common.o
	$(LD) $@ $^ -larchive -llzma

read: read.o common.o
	$(LD) $@ $^ -llzma

clean:
	rm -f *.o $(PROGS)

.PHONY: all clean
