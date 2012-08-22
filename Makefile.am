
#AUTOMAKE_OPTIONS = subdir-objects 1.6 dist-zip
AUTOMAKE_OPTIONS = subdir-objects 1.11 dist-zip dist-xz

SUBDIRS = .

INCLUDES = -I$(top_srcdir)/src -I$(top_srcdir)/win32

AM_CFLAGS = $(OGG_CFLAGS) $(Opus_CFLAGS) -DOPUSTOOLS -D_FORTIFY_SOURCE=2 -DHAVE_GETOPT_H -DSPX_RESAMPLE_EXPORT= -DRANDOM_PREFIX=opustools -DOUTSIDE_SPEEX -DFLOATING_POINT

bin_PROGRAMS = opusenc opusdec opusinfo opusrtp

noinst_HEADERS = src/arch.h \
                 src/diag_range.h \
                 src/info_opus.h \
                 src/lpc.h \
                 src/opusenc.h \
                 src/opus_header.h \
                 src/opusinfo.h \
                 src/os_support.h \
                 src/resample_sse.h \
                 src/speex_resampler.h \
                 src/stack_alloc.h \
                 win32/unicode_support.h \
                 src/cpusupport.h \
                 src/wave_out.h \
                 src/wav_io.h

EXTRA_DIST = Makefile.unix \
             opus-tools.sln \
             src/opusdec.vcxproj.filters \
             src/opusinfo.vcxproj.filters \
             src/opusenc.vcxproj.filters \
             src/opusinfo.vcxproj \
             src/opusenc.vcxproj \
             src/opusdec.vcxproj \
             win32/genversion.bat \
             win32/unicode_support.h \
             win32/config.h \
             win32/unicode_support.c

dist_man_MANS = man/opusenc.1 man/opusdec.1 man/opusinfo.1

opusenc_SOURCES = src/opus_header.c src/opusenc.c src/resample.c src/audio-in.c src/diag_range.c src/lpc.c win32/unicode_support.c
opusenc_LDADD = $(OGG_LIBS) $(Opus_LIBS) -lm
opusenc_MANS = man/opusenc.1

opusdec_SOURCES = src/opus_header.c src/wav_io.c src/wave_out.c src/opusdec.c src/resample.c src/diag_range.c win32/unicode_support.c
opusdec_LDADD = $(OGG_LIBS) $(Opus_LIBS) -lm
opusdec_MANS = man/opusdec.1

opusinfo_SOURCES = src/opus_header.c src/opusinfo.c src/info_opus.c win32/unicode_support.c
opusinfo_LDADD = $(OGG_LIBS)
opusinfo_MANS = man/opusinfo.1

opusrtp_SOURCES = src/opusrtp.c
opusrtp_LDADD = @LIBPCAP@ $(OGG_LIBS)

#TESTS = FIXME
