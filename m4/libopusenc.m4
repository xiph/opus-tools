# Configure paths for libopusenc
# Jean-Marc Valin <jmvalin@jmvalin.ca> 11-12-2017
# Jack Moffitt <jack@icecast.org> 10-21-2000
# Shamelessly stolen from Owen Taylor and Manish Singh

dnl XIPH_PATH_LIBOPUSENC([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for libopusenc, and define LIBOPUSENC_CFLAGS and LIBOPUSENC_LIBS
dnl
AC_DEFUN([XIPH_PATH_LIBOPUSENC],
[dnl
dnl Get the cflags and libraries
dnl
AC_ARG_WITH(libopusenc,AC_HELP_STRING([--with-libopusenc=PFX],[Prefix where libopusenc is installed (optional)]), libopusenc_prefix="$withval", libopusenc_prefix="")
AC_ARG_WITH(libopusenc-libraries,AC_HELP_STRING([--with-libopusenc-libraries=DIR],[Directory where libopusenc library is installed (optional)]), libopusenc_libraries="$withval", libopusenc_libraries="")
AC_ARG_WITH(libopusenc-includes,AC_HELP_STRING([--with-libopusenc-includes=DIR],[Directory where libopusenc header files are installed (optional)]), libopusenc_includes="$withval", libopusenc_includes="")
AC_ARG_ENABLE(libopusenctest,AC_HELP_STRING([--disable-libopusenctest],[Do not try to compile and run a test libopusenc program]),, enable_libopusenctest=yes)

  if test "x$libopusenc_libraries" != "x" ; then
    LIBOPUSENC_LIBS="-L$libopusenc_libraries"
  elif test "x$libopusenc_prefix" = "xno" || test "x$libopusenc_prefix" = "xyes" ; then
    LIBOPUSENC_LIBS=""
  elif test "x$libopusenc_prefix" != "x" ; then
    LIBOPUSENC_LIBS="-L$libopusenc_prefix/lib"
  elif test "x$prefix" != "xNONE" ; then
    LIBOPUSENC_LIBS="-L$prefix/lib"
  fi

  if test "x$libopusenc_prefix" != "xno" ; then
    LIBOPUSENC_LIBS="$LIBOPUSENC_LIBS -lopusenc"
  fi

  if test "x$libopusenc_includes" != "x" ; then
    LIBOPUSENC_CFLAGS="-I$libopusenc_includes"
  elif test "x$libopusenc_prefix" = "xno" || test "x$libopusenc_prefix" = "xyes" ; then
    LIBOPUSENC_CFLAGS=""
  elif test "x$libopusenc_prefix" != "x" ; then
    LIBOPUSENC_CFLAGS="-I$libopusenc_prefix/include/opus"
  elif test "x$prefix" != "xNONE"; then
    LIBOPUSENC_CFLAGS="-I$prefix/include/opus"
  fi

  AC_MSG_CHECKING(for libopusenc)
  if test "x$libopusenc_prefix" = "xno" ; then
    no_libopusenc="disabled"
    enable_libopusenctest="no"
  else
    no_libopusenc=""
  fi


  if test "x$enable_libopusenctest" = "xyes" ; then
    ac_save_CFLAGS="$CFLAGS"
    ac_save_LIBS="$LIBS"
    CFLAGS="$CFLAGS $LIBOPUSENC_CFLAGS $OPUS_CFLAGS"
    LIBS="$LIBS $LIBOPUSENC_LIBS $OPUS_LIBS"
dnl
dnl Now check if the installed libopusenc is sufficiently new.
dnl
      rm -f conf.libopusenctest
      AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <opusenc.h>

int main ()
{
  system("touch conf.libopusenctest");
  return 0;
}

],, no_libopusenc=yes,[echo $ac_n "cross compiling; assumed OK... $ac_c"])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
  fi

  if test "x$no_libopusenc" = "xdisabled" ; then
     AC_MSG_RESULT(no)
     ifelse([$2], , :, [$2])
  elif test "x$no_libopusenc" = "x" ; then
     AC_MSG_RESULT(yes)
     ifelse([$1], , :, [$1])
  else
     AC_MSG_RESULT(no)
     if test -f conf.libopusenctest ; then
       :
     else
       echo "*** Could not run libopusenc test program, checking why..."
       CFLAGS="$CFLAGS $LIBOPUSENC_CFLAGS"
       LIBS="$LIBS $LIBOPUSENC_LIBS"
       AC_TRY_LINK([
#include <stdio.h>
#include <opusenc.h>
],     [ return 0; ],
       [ echo "*** The test program compiled, but did not run. This usually means"
       echo "*** that the run-time linker is not finding libopusenc or finding the wrong"
       echo "*** version of libopusenc. If it is not finding libopusenc, you'll need to set your"
       echo "*** LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
       echo "*** to the installed location  Also, make sure you have run ldconfig if that"
       echo "*** is required on your system"
       echo "***"
       echo "*** If you have an old version installed, it is best to remove it, although"
       echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH"],
       [ echo "*** The test program failed to compile or link. See the file config.log for the"
       echo "*** exact error that occured. This usually means libopusenc was incorrectly installed"
       echo "*** or that you have moved libopusenc since it was installed." ])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
     fi
     LIBOPUSENC_CFLAGS=""
     LIBOPUSENC_LIBS=""
     ifelse([$2], , :, [$2])
  fi
  AC_SUBST(LIBOPUSENC_CFLAGS)
  AC_SUBST(LIBOPUSENC_LIBS)
  rm -f conf.libopusenctest
])
