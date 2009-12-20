LDFLAGS = -L/Library/Fink/sl64/lib -llzma
CFLAGS = -I/Library/Fink/sl64/include -g -O0

xz: xz.o
	gcc $(LDFLAGS) -Wall -o $@ $<

%.o: %.c
	gcc $(CFLAGS) -std=c99 -Wall -c -o $@ $<

run: xz
	time ./xz < test.in > test.out
	@md5sum test.in
	@xz -d < test.out | md5sum

clean:
	rm -f *.o xz test.out test.base

.PHONY: run clean
