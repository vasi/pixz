#!/bin/bash

tarball=$1
sample=$2

echo XZ
time xz -c < "$tarball" > test.txz
time xz -cd < test.txz | tar xO "$sample" | md5sum

echo; echo; echo PIXZ
time ./pixz < "$tarball" > test.tpxz
time ./pixz -x "$sample" < test.tpxz | tar xO "$sample" | md5sum

echo; echo; echo CROSS
xz -cd < test.tpxz | tar xO "$sample" | md5sum

echo; echo
du -sh "$tarball" test.tpxz test.txz
rm test.tpxz test.txz

echo; echo " * test: keep input"
./pixz -k < "$sample" > test.xz
[ -f "$sample" ] && echo " + compression: ok" || (echo " - compression: file removed"; exit 1)
./pixz -dk < test.xz > test.original
[ -f "$sample" ] && echo " + decompression: ok" || (echo " - decompression: file removed"; exit 1)
rm -f test.xz test.original

