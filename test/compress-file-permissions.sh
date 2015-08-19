#!/bin/bash

PIXZ=../src/pixz

INPUT=$(mktemp)
trap "rm -f $INPUT $INPUT.xz" EXIT

chmod 600 $INPUT
echo foo > $INPUT

$PIXZ $INPUT

[[ $(stat -c "%a" $INPUT.xz) = 600 ]]
