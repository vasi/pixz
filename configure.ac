#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([pixz], [1.0.3], [https://github.com/vasi/pixz/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src/pixz.c])
AC_CONFIG_HEADERS([config.h])

# Automake invocation.
AM_INIT_AUTOMAKE([foreign])

# Checks for programs.
AC_PROG_CC_STDC
AC_CHECK_PROG(A2X, a2x, a2x, [])
if test "x$A2X" = x ; then
  AC_MSG_ERROR([AsciiDoc not found.])
fi

# Checks for libraries.
AC_CHECK_LIB([m], [ceil])
AX_PTHREAD
PKG_CHECK_MODULES(LIBARCHIVE, libarchive)
PKG_CHECK_MODULES(LZMA, liblzma)

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stdint.h stdlib.h string.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
# add when travis has autoconf 2.69+ AC_CHECK_HEADER_STDBOOL
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for operating system services or capabilities.
AC_SYS_LARGEFILE

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_FUNC_STRTOD
AC_CHECK_FUNCS([memchr memmove memset strerror strtol])

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 test/Makefile])
AC_OUTPUT
