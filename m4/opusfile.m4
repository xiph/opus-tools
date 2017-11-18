# Configure paths for libopusfile
# Timothy B. Terriberry <tterribe@xiph.org> 17-11-2017
# Shamelessly stolen from Gregory Maxwell <greg@xiph.org> 08-30-2012 who
# Shamelessly stole from Jack Moffitt (libogg) who
# Shamelessly stole from Owen Taylor and Manish Singh

dnl XIPH_PATH_OPUSFILE([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for libopusfile OPUSFILE_CFLAGS and OPUSFILE_LIBS
dnl
AC_DEFUN([XIPH_PATH_OPUSFILE],
[dnl
dnl Get the cflags and libraries
dnl
AC_ARG_WITH(opusfile,AC_HELP_STRING([--with-opusfile=PFX],[Prefix where opusfile is installed (optional)]), opusfile_prefix="$withval", opusfile_prefix="")
AC_ARG_WITH(opusfile-libraries,AC_HELP_STRING([--with-opusfile-libraries=DIR],[Directory where the opusfile library is installed (optional)]), opusfile_libraries="$withval", opusfile_libraries="")
AC_ARG_WITH(opusfile-includes,AC_HELP_STRING([--with-opusfile-includes=DIR],[Directory where the opusfile header files are installed (optional)]), opusfile_includes="$withval", opusfile_includes="")
AC_ARG_ENABLE(opusfiletest,AC_HELP_STRING([--disable-opusfiletest],[Do not try to compile and run a test opusfile program]),, enable_opusfiletest=yes)

  if test "x$opusfile_libraries" != "x" ; then
    OPUSFILE_LIBS="-L$opusfile_libraries"
  elif test "x$opusfile_prefix" = "xno" || test "x$opusfile_prefix" = "xyes" ; then
    OPUSFILE_LIBS=""
  elif test "x$opusfile_prefix" != "x" ; then
    OPUSFILE_LIBS="-L$opusfile_prefix/lib"
  elif test "x$prefix" != "xNONE" ; then
    OPUSFILE_LIBS="-L$prefix/lib"
  fi

  if test "x$opusfile_prefix" != "xno" ; then
    OPUSFILE_LIBS="$OPUSFILE_LIBS -lopusfile"
  fi

  if test "x$opusfile_includes" != "x" ; then
    OPUSFILE_CFLAGS="-I$opusfile_includes"
  elif test "x$opusfile_prefix" = "xno" || test "x$opusfile_prefix" = "xyes" ; then
    OPUSFILE_CFLAGS=""
  elif test "x$opusfile_prefix" != "x" ; then
    OPUSFILE_CFLAGS="-I$opusfile_prefix/include/opus"
  elif test "x$prefix" != "xNONE"; then
    OPUSFILE_CFLAGS="-I$prefix/include/opus"
  fi

  AC_MSG_CHECKING(for libopusfile)
  if test "x$opusfile_prefix" = "xno" ; then
    no_opusfile="disabled"
    enable_opusfiletest="no"
  else
    no_opusfile=""
  fi


  if test "x$enable_opusfiletest" = "xyes" ; then
    ac_save_CFLAGS="$CFLAGS"
    ac_save_LIBS="$LIBS"
    CFLAGS="$CFLAGS $OPUSFILE_CFLAGS $OGG_CFLAGS $OPUS_CFLAGS"
    LIBS="$LIBS $OPUSFILE_LIBS $OGG_LIBS $OPUS_LIBS"
dnl
dnl Now check if the installed libopusfile is sufficiently new.
dnl
      rm -f conf.opusfiletest
      AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <opusfile.h>

int main ()
{
  system("touch conf.opusfiletest");
  return 0;
}

],, no_opusfile=yes,[echo $ac_n "cross compiling; assumed OK... $ac_c"])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
  fi

  if test "x$no_opusfile" = "xdisabled" ; then
     AC_MSG_RESULT(no)
     ifelse([$2], , :, [$2])
  elif test "x$no_opusfile" = "x" ; then
     AC_MSG_RESULT(yes)
     ifelse([$1], , :, [$1])
  else
     AC_MSG_RESULT(no)
     if test -f conf.opusfiletest ; then
       :
     else
       echo "*** Could not run libopusfile test program, checking why..."
       CFLAGS="$CFLAGS $OPUSFILE_CFLAGS"
       LIBS="$LIBS $OPUSFILE_LIBS"
       AC_TRY_LINK([
#include <stdio.h>
#include <opusfile.h>
],     [ return 0; ],
       [ echo "*** The test program compiled, but did not run. This usually means"
       echo "*** that the run-time linker is not finding libopusfile or finding the wrong"
       echo "*** version of libopusfile. If it is not finding libopusfile, you'll need to set"
       echo "*** your LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
       echo "*** to the installed location  Also, make sure you have run ldconfig if that"
       echo "*** is required on your system"
       echo "***"
       echo "*** If you have an old version installed, it is best to remove it, although"
       echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH"],
       [ echo "*** The test program failed to compile or link. See the file config.log for the"
       echo "*** exact error that occured. This usually means libopusfile was incorrectly"
       echo "*** installed or that you have moved libopusfile since it was installed." ])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
     fi
     OPUSFILE_CFLAGS=""
     OPUSFILE_LIBS=""
     ifelse([$2], , :, [$2])
  fi
  AC_SUBST(OPUSFILE_CFLAGS)
  AC_SUBST(OPUSFILE_LIBS)
  rm -f conf.opusfiletest
])
