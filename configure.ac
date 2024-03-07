#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([pixz], [1.0.7], [https://github.com/vasi/pixz/issues])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src/pixz.c])
AC_CONFIG_HEADERS([config.h])

# Automake invocation.
AM_INIT_AUTOMAKE([foreign])

# Checks for programs.
AC_PROG_CC_STDC

# Check for a2x only if the man page is missing, i.e. we are building from git. The release tarballs
# are set up to include the man pages. This way, only people creating tarballs via `make dist` and
# people building from git need a2x as a dependency.
AS_IF(
  [test -f src/pixz.1],
  [],
  [
    AC_ARG_WITH(
      [manpage],
      [  --without-manpage       don't build man page],
      [case ${withval} in
        yes) manpage=true ;;
        no)  manpage=false ;;
        *)   AC_MSG_ERROR([bad value ${withval} for --with-manpage]) ;;
      esac],
      [manpage=true]
    )
  ]
)

AM_CONDITIONAL([MANPAGE], [test x$manpage = xtrue])

if test x$manpage = xtrue ; then
  AC_CHECK_PROG(A2X, a2x, a2x, [])
  if test "x$A2X" = x ; then
    AC_MSG_ERROR([AsciiDoc not found, not able to generate the man page.])
  fi
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
AC_CHECK_FUNCS([memchr memmove memset strerror strtol sched_getaffinity sysconf \
  GetSystemInfo _setmode _get_osfhandle])
AC_CHECK_HEADER([sys/endian.h],
               [
                 AC_CHECK_DECLS([htole64, le64toh], [], [], [
                  #define _GNU_SOURCE 1
                  #include <sys/endian.h>
                 ]
               )
               ],
               [], [])

AC_CHECK_HEADER([endian.h],
               [
                 AC_CHECK_DECLS([htole64, le64toh], [], [], [
                  #define _GNU_SOURCE 1
                  #include <endian.h>
                 ])
               ],
               [], [])

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 test/Makefile])
AC_OUTPUT
