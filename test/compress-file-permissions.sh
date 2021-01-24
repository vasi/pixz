#!/bin/sh

PIXZ=../src/pixz

INPUT=$(mktemp)
trap "rm -f $INPUT $INPUT.xz" EXIT

chmod 600 $INPUT
echo foo > $INPUT

$PIXZ $INPUT

[ "$(find $INPUT.xz -perm 0600)" = "$INPUT.xz" ]
