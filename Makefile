LDFLAGS = -L./libs -g -Wall
CFLAGS = -I/Library/Fink/sl64/include -g -O0 -std=c99 -Wall

CC = gcc $(CFLAGS) -c -o
LD = gcc $(LDFLAGS) -o

all: pixz pixzlist pixztar write


%.o: %.c pixz.h
	$(CC) $@ $<

pixz: pixz.o encode.o block.o util.o
	$(LD) $@ $^ -llzma

pixzlist: pixzlist.o
	$(LD) $@ $^ -llzma
	
pixztar: tar.o util.o index.o encode.o block.o
	$(LD) $@ $^ ./libs/libarchive.a -llzma

write: write.o
	$(LD) $@ $^ ./libs/libarchive.a -llzma

run: pixz
	time ./$< < test.in > test.out
	@md5sum test.in
	@xz -d < test.out | md5sum

clean:
	rm -f *.o pixz pixzlist pixztar test.out

.PHONY: all run clean
