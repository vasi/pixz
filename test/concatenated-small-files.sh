#!/bin/sh

PIXZ=../src/pixz

F1=$(mktemp)
F2=$(mktemp)
EXPECTED=$(mktemp)
ACTUAL=$(mktemp)
trap "rm -f $F1 $F2 $EXPECTED $ACTUAL" EXIT

echo foo >> $EXPECTED
echo foo | $PIXZ > $F1
echo bar >> $EXPECTED
echo bar | $PIXZ > $F2

cat $F1 $F2 | $PIXZ -d > $ACTUAL

cmp $ACTUAL $EXPECTED
