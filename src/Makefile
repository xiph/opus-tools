CC=gcc
CFLAGS=-DHAVE_SYS_SOUNDCARD_H -DVALGRIND -O2 -g -fstack-protector-all -c -Wall -Wextra -DHAVE_GETOPT_H -DEXPORT= -DRANDOM_PREFIX=opustools -DOUTSIDE_SPEEX -DFLOATING_POINT
INCLUDES=-I../../opus/include

all: opusenc opusdec

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@

opusenc: opus_header.o opusenc.o resample.o audio-in.o diag_range.o
	gcc opus_header.o audio-in.o diag_range.o opusenc.o resample.o -o opusenc ../../opus/.libs/libopus.a -lm -logg

opusdec: opus_header.o wav_io.o wave_out.o opusdec.o resample.o diag_range.o
	gcc wave_out.o opus_header.o wav_io.o diag_range.o opusdec.o resample.o -o opusdec ../../opus/.libs/libopus.a -lm -logg

clean:
	rm -f *.o opusenc opusdec
