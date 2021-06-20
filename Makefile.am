ACLOCAL_AMFLAGS = -I m4

#AUTOMAKE_OPTIONS = subdir-objects 1.6 dist-zip
AUTOMAKE_OPTIONS = subdir-objects 1.11 dist-zip dist-xz

SUBDIRS = .

AM_CPPFLAGS = -I$(top_srcdir)/src -I$(top_srcdir)/win32 -D_FORTIFY_SOURCE=2
AM_CFLAGS = $(OPUS_CFLAGS) $(OGG_CFLAGS)

bin_PROGRAMS = opusenc opusdec opusinfo
noinst_PROGRAMS = opusrtp

noinst_HEADERS = src/arch.h \
                 src/diag_range.h \
                 src/flac.h \
                 src/info_opus.h \
                 src/encoder.h \
                 src/opus_header.h \
                 src/opusinfo.h \
                 src/picture.h \
                 src/tagcompare.h \
                 src/resample_sse.h \
                 src/speex_resampler.h \
                 src/stack_alloc.h \
                 win32/unicode_support.h \
                 src/cpusupport.h \
                 src/wave_out.h \
                 src/wav_io.h

EXTRA_DIST = Makefile.unix \
             man/opusrtp.1 \
             include/getopt.h \
             share/getopt.c \
             share/getopt1.c \
             win32/genversion.bat \
             win32/config.h \
             win32/unicode_support.h \
             win32/unicode_support.c \
             win32/VS2015/generate_version.vcxproj \
             win32/VS2015/opusdec.vcxproj \
             win32/VS2015/opusdec.vcxproj.filters \
             win32/VS2015/opusenc.vcxproj \
             win32/VS2015/opusenc.vcxproj.filters \
             win32/VS2015/opusinfo.vcxproj \
             win32/VS2015/opusinfo.vcxproj.filters \
             win32/VS2015/opus-tools.sln \
             win32/VS2015/common.props \
             win32/VS2015/opus-tools.props

dist_man_MANS = man/opusenc.1 man/opusdec.1 man/opusinfo.1

resampler_CPPFLAGS = -DRANDOM_PREFIX=opustools -DOUTSIDE_SPEEX -DRESAMPLE_FULL_SINC_TABLE

opusenc_SOURCES = src/opus_header.c src/opusenc.c src/tagcompare.c src/audio-in.c src/diag_range.c src/flac.c win32/unicode_support.c
opusenc_CPPFLAGS = $(AM_CPPFLAGS)
opusenc_CFLAGS = $(AM_CFLAGS) $(LIBOPUSENC_CFLAGS) $(FLAC_CFLAGS)
opusenc_LDADD = $(LIBOPUSENC_LIBS) $(OPUS_LIBS) $(FLAC_LIBS) $(OGG_LIBS) $(LIBM)
opusenc_MANS = man/opusenc.1

opusdec_SOURCES = src/opus_header.c src/wav_io.c src/wave_out.c src/opusdec.c src/resample.c src/diag_range.c win32/unicode_support.c
opusdec_CPPFLAGS = $(AM_CPPFLAGS) $(resampler_CPPFLAGS)
opusdec_CFLAGS = $(AM_CFLAGS) $(OPUSURL_CFLAGS)
opusdec_LDADD = $(OPUSURL_LIBS) $(OPUS_LIBS) $(LIBM)
opusdec_MANS = man/opusdec.1

opusinfo_SOURCES = src/opus_header.c src/opusinfo.c src/info_opus.c src/picture.c src/tagcompare.c win32/unicode_support.c
opusinfo_CPPFLAGS = $(AM_CPPFLAGS) -DOPUSTOOLS
opusinfo_LDADD = $(OGG_LIBS)
opusinfo_MANS = man/opusinfo.1

opusrtp_SOURCES = src/opusrtp.c
opusrtp_LDADD = $(OPUS_LIBS) $(OGG_LIBS) $(OPUSRTP_LIBS)

#TESTS = FIXME


# We check this every time make is run, with configure.ac being touched to
# trigger an update of the build system files if update_version changes the
# current PACKAGE_VERSION (or if package_version was modified manually by a
# user with either AUTO_UPDATE=no or no update_version script present - the
# latter being the normal case for tarball releases).
#
# We can't just add the package_version file to CONFIGURE_DEPENDENCIES since
# simply running autoconf will not actually regenerate configure for us when
# the content of that file changes (due to autoconf dependency checking not
# knowing about that without us creating yet another file for it to include).
#
# The MAKECMDGOALS check is a gnu-make'ism, but will degrade 'gracefully' for
# makes that don't support it.  The only loss of functionality is not forcing
# an update of package_version for `make dist` if AUTO_UPDATE=no, but that is
# unlikely to be a real problem for any real user.
$(top_srcdir)/configure.ac: force
	@case "$(MAKECMDGOALS)" in \
	    dist-hook)                             exit 0       ;; \
	    dist-* | dist | distcheck | distclean) _arg=release ;; \
	esac; \
	if ! $(top_srcdir)/update_version $$_arg 2> /dev/null; then \
	    if [ ! -e $(top_srcdir)/package_version ]; then \
		echo 'PACKAGE_VERSION="unknown"' > $(top_srcdir)/package_version; \
	    fi; \
	    . $(top_srcdir)/package_version || exit 1; \
	    [ "$(PACKAGE_VERSION)" != "$$PACKAGE_VERSION" ] || exit 0; \
	fi; \
	touch $@

force:

# Create a minimal package_version file when make dist is run.
dist-hook:
	echo 'PACKAGE_VERSION="$(PACKAGE_VERSION)"' > $(top_distdir)/package_version

