#!/bin/sh

PIXZ=../src/pixz

INPUT=$(basename $0)

COMPRESSED=$INPUT.xz
UNCOMPRESSED=$INPUT.extracted
trap "rm -f $COMPRESSED $UNCOMPRESSED" EXIT

$PIXZ $INPUT $COMPRESSED
$PIXZ -d $COMPRESSED $UNCOMPRESSED

[ "$(cat $INPUT | md5sum)" = "$(cat $UNCOMPRESSED | md5sum)" ] || exit 1
