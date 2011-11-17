CC=gcc
CFLAGS=-DHAVE_SYS_SOUNDCARD_H -O2 -g -fstack-protector-all -c -Wall -DHAVE_GETOPT_H -DEXPORT= -DRANDOM_PREFIX=opustools -DOUTSIDE_SPEEX -DFLOATING_POINT
INCLUDES=-I../../opus/include

all: opusenc opusdec

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@

opusenc: opus_header.o wav_io.o opusenc.o resample.o
	gcc opus_header.o wav_io.o opusenc.o resample.o -o opusenc ../../opus/.libs/libopus.a -lm -logg

opusdec: opus_header.o wav_io.o wave_out.o opusdec.o resample.o
	gcc wave_out.o opus_header.o wav_io.o opusdec.o resample.o -o opusdec ../../opus/.libs/libopus.a -lm -logg

clean:
	rm -f *.o opusenc opusdec
