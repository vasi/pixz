#!/bin/bash

if which valgrind &> /dev/null ; then
  PIXZ=../src/pixz

  INPUT=$(basename $0)
  OUTPUT=$INPUT.pixz
  UNCOMPRESSED=$INPUT.uncompressed

  MEMCHECK_OUT=$(mktemp)

  trap "rm -f $OUTPUT $MEMCHECK_OUT $UNCOMPRESSED" EXIT

  $PIXZ $INPUT $OUTPUT

  valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes \
    $PIXZ -d $OUTPUT $UNCOMPRESSED \
      2> $MEMCHECK_OUT

  cat $MEMCHECK_OUT
  grep -q 'ERROR SUMMARY: 0' $MEMCHECK_OUT
else
  echo "no valgrind, skipping test"
  exit 77
fi
