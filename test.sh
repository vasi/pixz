#!/bin/bash

set -e

tarball=$1
sample=$2

[ ! -f "$sample" ] && (echo "[E] test file '$sample' does not exists!"; exit 1)
[ ! -f "$tarball" ] && (echo "[E] test file '$tarball' does not exists!"; exit 1)

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
