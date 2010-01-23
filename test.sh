#!/bin/bash

#base=lmnopuz
#file=lmnopuz/CheckPUZ.app/Contents/Resources/script

base=nicotine
file=nicotine/museek+-0.1.13/doc/SConscript

#base=simbl
#file=Users/vasi/Desktop/SIMBL/keywurl/SIMBL.pkg/Contents/Info.plist

echo XZ
time xz -c < tars/$base.tar > tars/$base.txz
time xz -cd < tars/$base.txz | tar xO $file | md5sum

echo; echo; echo PIXZ
time ./write tars/$base.tar tars/$base.tpxz
time ./read tars/$base.tpxz $file | tar xO $file | md5sum

echo; echo; echo CROSS
xz -cd < tars/$base.tpxz | tar xO $file | md5sum

echo; echo
du -sh tars/$base.tar tars/$base.tpxz tars/$base.txz
