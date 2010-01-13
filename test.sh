#!/bin/sh

#base=lmnopuz
#file=lmnopuz/CheckPUZ.app/Contents/Resources/script

#base=nicotine
#file=nicotine/museek+-0.1.13/doc/SConscript

base=simbl
file=Users/vasi/Desktop/SIMBL/keywurl/SIMBL.pkg/Contents/Info.plist

echo XZ
time xz -c < $base.tar > $base.txz
time xz -cd < $base.txz | tar xO $file | md5sum

echo; echo; echo PIXZ
time ./write $base.tar $base.tpxz
time ./read $base.tpxz $file | tar xO $file | md5sum

echo; echo; echo CROSS
xz -cd < $base.tpxz | tar xO $file | md5sum

echo; echo
du -sh $base.tar $base.tpxz $base.txz
