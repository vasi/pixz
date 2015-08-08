#!/bin/bash

if which valgrind &> /dev/null ; then
  PIXZ=../src/pixz

  INPUT=$(basename $0)
  OUTPUT=$INPUT.pixz

  MEMCHECK_OUT=$(mktemp)

  trap "rm -f $OUTPUT $MEMCHECK_OUT" EXIT

  # TODO add --show-leak-kinds=all when travis gets newer valgrind
  valgrind --tool=memcheck --leak-check=full --track-origins=yes \
    $PIXZ $INPUT $OUTPUT \
      2> $MEMCHECK_OUT

  cat $MEMCHECK_OUT
  grep -q 'ERROR SUMMARY: 0' $MEMCHECK_OUT
else
  echo "no valgrind, skipping test"
  exit 77
fi
