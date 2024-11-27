#!/bin/sh

if which cppcheck &> /dev/null ; then
  cppcheck --error-exitcode=1 --check-level=exhaustive $srcdir/../src
else
  echo "no cppcheck, skipping test"
  exit 77
fi
