#!/bin/sh

PIXZ=../src/pixz

[ "foo" = "$(echo foo | xz    | xz    -dc)" ] || exit 1
[ "bar" = "$(echo bar | $PIXZ | $PIXZ -dc)" ] || exit 1
