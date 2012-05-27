AUTOMAKE_OPTIONS = subdir-objects

SUBDIRS = .

INCLUDES = -I$(top_srcdir)/src

AM_CFLAGS = $(OGG_CFLAGS) $(Opus_CFLAGS) -DOPUSTOOLS -D_FORTIFY_SOURCE=2 -DHAVE_GETOPT_H -DSPX_RESAMPLE_EXPORT= -DRANDOM_PREFIX=opustools -DOUTSIDE_SPEEX -DFLOATING_POINT

bin_PROGRAMS = opusenc opusdec opusinfo

opusenc_SOURCES = src/opus_header.c src/opusenc.c src/resample.c src/audio-in.c src/diag_range.c
opusenc_LDADD = $(OGG_LIBS) $(Opus_LIBS) -lm

opusdec_SOURCES = src/opus_header.c src/wav_io.c src/wave_out.c src/opusdec.c src/resample.c src/diag_range.c
opusdec_LDADD = $(OGG_LIBS) $(Opus_LIBS) $(OSS_LIBS) -lm

opusinfo_SOURCES = src/opus_header.c src/opusinfo.c src/info_opus.c
opusinfo_LDADD = $(OGG_LIBS)

#TESTS = FIXME
