LDFLAGS = -L/Library/Fink/sl64/lib -llzma -Wall
CFLAGS = -I/Library/Fink/sl64/include -g -O0 -std=c99 -Wall

CC = gcc $(CFLAGS) -c -o
LD = gcc $(LDFLAGS) -o


PIXZ_OBJS = pixz.o encode.o block.o

all: pixz

pixz: $(PIXZ_OBJS)
	$(LD) $@ $^

$(PIXZ_OBJS): %.o: %.c pixz.h
	$(CC) $@ $<


pixzlist: pixzlist.o
	$(LD) $@ $^

pixzlist.o: pixzlist.c
	$(CC) $@ $<


run: pixz
	time ./$< < test.in > test.out
	@md5sum test.in
	@xz -d < test.out | md5sum

clean:
	rm -f *.o pixz test.out

.PHONY: all run clean
