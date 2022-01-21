/* Copyright 2000-2002, Michael Smith <msmith@xiph.org>
             2010, Monty <monty@xiph.org>
   AIFF/AIFC support from OggSquish, (c) 1994-1996 Monty <xiphmont@xiph.org>
   (From GPL code in oggenc relicensed by permission from Monty and Msmith)
   File: audio-in.c

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#if !defined(_LARGEFILE_SOURCE)
# define _LARGEFILE_SOURCE
#endif
#if !defined(_LARGEFILE64_SOURCE)
# define _LARGEFILE64_SOURCE
#endif
#if !defined(_FILE_OFFSET_BITS)
# define _FILE_OFFSET_BITS 64
#endif

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <math.h>

#include "stack_alloc.h"

#if defined WIN32 || defined _WIN32
# include <windows.h> /*GetFileType()*/
# include <io.h>      /*_get_osfhandle()*/
#endif

#ifdef ENABLE_NLS
# include <libintl.h>
# define _(X) gettext(X)
#else
# define _(X) (X)
# define textdomain(X)
# define bindtextdomain(X, Y)
#endif
#ifdef gettext_noop
# define N_(X) gettext_noop(X)
#else
# define N_(X) (X)
#endif

#include <ogg/ogg.h>
#include "encoder.h"
#include "opus_header.h"
#include "wav_io.h"
#include "flac.h"

/* Macros for handling potentially large file offsets */
#if defined WIN32 || defined _WIN32
# define OFF_T __int64
/* On Windows, fseek() on pipes may return zero even though it doesn't seek. */
# if defined __MINGW32__ || defined __MINGW64__
#  define FSEEK(s,o,w) (((GetFileType((HANDLE)_get_osfhandle(_fileno(s)))&~FILE_TYPE_REMOTE)==FILE_TYPE_DISK)?fseeko64((s),(o),(w)):1)
#  define FTELL ftello64
# else
#  define FSEEK(s,o,w) (((GetFileType((HANDLE)_get_osfhandle(_fileno(s)))&~FILE_TYPE_REMOTE)==FILE_TYPE_DISK)?_fseeki64((s),(o),(w)):1)
#  define FTELL _ftelli64
# endif
#elif defined HAVE_FSEEKO
# define OFF_T off_t
# define FSEEK fseeko
# define FTELL ftello
#else
# define OFF_T long
# define FSEEK fseek
# define FTELL ftell
#endif

/* Macros to read header data */
#define READ_U32_LE(buf) \
    (((unsigned int)(buf)[3]<<24)|((buf)[2]<<16)|((buf)[1]<<8)|((buf)[0]))

#define READ_U16_LE(buf) \
    (((buf)[1]<<8)|((buf)[0]&0xff))

#define READ_U32_BE(buf) \
    (((unsigned int)(buf)[0]<<24)|((buf)[1]<<16)|((buf)[2]<<8)|((buf)[3]))

#define READ_U16_BE(buf) \
    (((buf)[0]<<8)|((buf)[1]&0xff))

/* Define the supported formats here */
input_format formats[] = {
    {wav_id, 12, wav_open, wav_close, "wav", N_("WAV file reader")},
    {aiff_id, 12, aiff_open, wav_close, "aiff", N_("AIFF/AIFC file reader")},
    {flac_id,     0x10000, flac_open, flac_close, "flac", N_("FLAC file reader")},
    {oggflac_id, 33, flac_open, flac_close, "ogg", N_("Ogg FLAC file reader")},
    {NULL, 0, NULL, NULL, NULL, NULL}
};

input_format *open_audio_file(FILE *in, oe_enc_opt *opt)
{
    int j=0;
    unsigned char *buf=NULL;
    int buf_size=0, buf_filled=0;
    int size;

    while (formats[j].id_func)
    {
        size = formats[j].id_data_len;
        if (size >= buf_size)
        {
            buf = realloc(buf, size);
            buf_size = size;
        }

        if (size > buf_filled)
        {
            buf_filled += (int)fread(buf+buf_filled, 1, buf_size-buf_filled, in);
            /* We still check a truncated read aginast the id_func
             * in order to support very small FLAC files but still be able to
             * read past an ID3 header. */
        }

        if (formats[j].id_func(buf, buf_filled))
        {
            /* ok, we now have something that can handle the file */
            if (formats[j].open_func(in, opt, buf, buf_filled)) {
                free(buf);
                return &formats[j];
            }
        }
        j++;
    }

    free(buf);

    return NULL;
}

static void sanitize_fourcc(unsigned char *buf)
{
    int i;
    for (i = 0; i < 4; ++i)
        if (buf[i] < ' ' || buf[i] > '~')
            buf[i] = '?';
}

static int seek_forward(FILE *in, ogg_int64_t length)
{
    ogg_int64_t remaining = length;
    while (remaining > 0)
    {
        /* When OFF_T is 64 bits, only one seek is needed and the comparison
         * will always be false. When OFF_T is not large enough, seek LONG_MAX
         * bytes at a time (the maximum offset that basic fseek() can handle).
         */
        OFF_T seekstep = (OFF_T)remaining;
        if (seekstep != remaining)
            seekstep = LONG_MAX;
        if (FSEEK(in, seekstep, SEEK_CUR))
        {
            /* Failed to seek; do it by reading. */
            unsigned char buf[1024];
            do {
                size_t readstep = remaining > 1024 ? 1024 : (size_t)remaining;
                readstep = fread(buf, 1, readstep, in);
                if (!readstep)
                    return 0; /* Couldn't read more, can't read file */
                remaining -= readstep;
            } while (remaining);
            break;
        }
        remaining -= seekstep;
    }
    return 1;
}

static int find_wav_chunk(FILE *in, char *type, unsigned int *len)
{
    unsigned char buf[8];
    unsigned int chunklen;

    while (1)
    {
        if (fread(buf,1,8,in) < 8) /* Suck down a chunk specifier */
            return 0; /* EOF before reaching the appropriate chunk */

        chunklen = READ_U32_LE(buf+4);

        if (memcmp(buf, type, 4))
        {
            sanitize_fourcc(buf);
            fprintf(stderr, _("Skipping chunk of type \"%.4s\", length %u\n"),
                buf, chunklen);

            if (!seek_forward(in, (ogg_int64_t)chunklen + (chunklen & 1)))
                return 0;
        }
        else
        {
            *len = chunklen;
            return 1;
        }
    }
}

static int find_aiff_chunk(FILE *in, char *type, unsigned int *len)
{
    unsigned char buf[8];
    unsigned int chunklen;
    int restarted = 0;

    while (1)
    {
        if (fread(buf,1,8,in) < 8)
        {
            if (!restarted) {
                /* Handle out of order chunks by seeking back to the start
                 * to retry */
                restarted = 1;
                if (!FSEEK(in, 12, SEEK_SET))
                    continue;
            }
            return 0;
        }

        chunklen = READ_U32_BE(buf+4);

        if (memcmp(buf,type,4))
        {
            if (!seek_forward(in, (ogg_int64_t)chunklen + (chunklen & 1)))
                return 0;
        }
        else
        {
            *len = chunklen;
            return 1;
        }
    }
}

/* Read chunk of size *len and advance the file position to the next chunk.
 * Returns 0 on EOF or read error. Otherwise *len is updated with the number
 * of bytes placed in the buffer (the lesser of the chunk size and buffer
 * size) and 1 is returned.
 */
static int read_chunk(FILE *in, unsigned char *buf, unsigned int bufsize,
        unsigned int *len)
{
    unsigned int chunklen = *len;
    unsigned int readlen = chunklen > bufsize ? bufsize : chunklen;

    if (fread(buf, 1, readlen, in) != readlen)
        return 0;

    if (!seek_forward(in, (ogg_int64_t)(chunklen - readlen) + (chunklen & 1)))
        return 0;

    *len = readlen;
    return 1;
}

static double read_IEEE80(unsigned char *buf)
{
    int e = READ_U16_BE(buf) & 0x7fff;
    double f;
    if (e==32767)
        /* NaNs and infinities -- their format can vary among implementations,
           but for our purposes they can all be treated as infinite. */
        f = HUGE_VAL;
    else
        f = ldexp(READ_U32_BE(buf+2) + READ_U32_BE(buf+6)*ldexp(1.0, -32), e-16383-31);
    return (buf[0]&0x80)?-f:f;
}

/* AIFF/AIFC support adapted from the old OggSQUISH application */
int aiff_id(unsigned char *buf, int len)
{
    if (len<12) return 0; /* Truncated file, probably */

    if (memcmp(buf, "FORM", 4))
        return 0;

    if (memcmp(buf+8, "AIF",3))
        return 0;

    if (buf[11]!='C' && buf[11]!='F')
        return 0;

    return 1;
}

static int aiff_permute_matrix[6][6] =
{
    {0},            /* 1.0 mono   */
    {0,1},          /* 2.0 stereo */
    {0,2,1},        /* 3.0 channel ('wide') stereo */
    {0,1,2,3},      /* 4.0 discrete quadraphonic (WARN) */
    {0,2,1,3,4},    /* 5.0 surround (WARN) */
    {0,1,2,3,4,5},  /* 5.1 surround (WARN)*/
};

int aiff_open(FILE *in, oe_enc_opt *opt, unsigned char *buf, int buflen)
{
    int aifc; /* AIFC or AIFF? */
    unsigned int len;
    unsigned char buffer[22];
    unsigned char buf2[8];
    int bigendian = 1;
    aiff_fmt format;
    aifffile *aiff;
    int i;
    (void)buflen;/*unused*/

    if (buf[11]=='C')
        aifc=1;
    else
        aifc=0;

    if (!find_aiff_chunk(in, "COMM", &len))
    {
        fprintf(stderr, _("ERROR: No common chunk found in AIFF file\n"));
        return 0; /* EOF before COMM chunk */
    }

    if (len < 18 || !read_chunk(in, buffer, sizeof(buffer), &len))
    {
        fprintf(stderr, _("ERROR: Incomplete common chunk in AIFF header\n"));
        return 0;
    }

    format.channels = (short)READ_U16_BE(buffer);
    format.totalframes = READ_U32_BE(buffer+2);
    format.samplesize = (short)READ_U16_BE(buffer+6);
    format.rate = read_IEEE80(buffer+8);

    if (format.channels <= 0)
    {
        fprintf(stderr, _("ERROR: Invalid channel count in AIFF header\n"));
        return 0;
    }

    if (aifc)
    {
        if (len < 22)
        {
            fprintf(stderr, _("ERROR: AIFF-C header truncated.\n"));
            return 0;
        }

        if (!memcmp(buffer+18, "NONE", 4))
        {
            bigendian = 1;
        }
        else if (!memcmp(buffer+18, "sowt", 4))
        {
            bigendian = 0;
        }
        else
        {
            sanitize_fourcc(buffer+18);
            fprintf(stderr, _("ERROR: Can't handle compressed AIFF-C \"%.4s\"\n"),
                buffer+18);
            return 0; /* Compressed. Can't handle */
        }
    }

    if (!(format.rate >= 1 && format.rate <= INT_MAX))
    {
        fprintf(stderr, _("ERROR: Preposterous sample rate in AIFF header: %g Hz\n"),
            format.rate);
        return 0;
    }

    if (!find_aiff_chunk(in, "SSND", &len))
    {
        fprintf(stderr, _("ERROR: No SSND chunk found in AIFF file\n"));
        return 0; /* No SSND chunk -> no actual audio */
    }

    if (len < 8)
    {
        fprintf(stderr, _("ERROR: Corrupted SSND chunk in AIFF header\n"));
        return 0;
    }

    if (fread(buf2,1,8, in) < 8)
    {
        fprintf(stderr, _("ERROR: Unexpected EOF reading AIFF header\n"));
        return 0;
    }

    format.offset = READ_U32_BE(buf2);
    format.blocksize = READ_U32_BE(buf2+4);

    if (format.blocksize == 0 &&
        (format.samplesize == 16 || format.samplesize == 8))
    {
        /* From here on, this is very similar to the wav code. Oh well. */

        opt->rate = (int)(format.rate + 0.5);  /* round to nearest integer */
        opt->channels = format.channels;
        opt->samplesize = format.samplesize;
        opt->read_samples = wav_read; /* Similar enough, so we use the same */
        opt->total_samples_per_channel = format.totalframes;

        aiff = malloc(sizeof(aifffile));
        aiff->f = in;
        aiff->samplesread = 0;
        aiff->channels = format.channels;
        aiff->samplesize = format.samplesize;
        aiff->totalsamples = format.totalframes;
        aiff->bigendian = bigendian;
        aiff->unsigned8bit = 0;

        if (opt->channels_format==CHANNELS_FORMAT_DEFAULT && aiff->channels>3)
          fprintf(stderr, _("WARNING: AIFF[-C] files with more than three channels use\n"
                  "speaker locations incompatible with Vorbis surround definitions.\n"
                  "Not performing channel location mapping.\n"));

        opt->readdata = (void *)aiff;

        aiff->channel_permute = malloc(aiff->channels * sizeof(int));
        if (opt->channels_format == CHANNELS_FORMAT_DEFAULT && aiff->channels <= 6)
            /* Where we know the mappings, use them. */
            memcpy(aiff->channel_permute, aiff_permute_matrix[aiff->channels-1],
                    sizeof(int) * aiff->channels);
        else
            /* Use a default 1-1 mapping */
            for (i=0; i < aiff->channels; i++)
                aiff->channel_permute[i] = i;

        seek_forward(in, format.offset); /* Swallow some data */
        return 1;
    }
    else
    {
        fprintf(stderr, _("ERROR: Unsupported AIFF/AIFC file.\n"
                "Must be 8 or 16 bit PCM.\n"));
        return 0;
    }
}

int wav_id(unsigned char *buf, int len)
{
    if (len<12) return 0; /* Something screwed up */

    if (memcmp(buf, "RIFF", 4))
        return 0; /* Not wave */

    /*flen = READ_U32_LE(buf+4);*/ /* We don't use this */

    if (memcmp(buf+8, "WAVE",4))
        return 0; /* RIFF, but not wave */

    return 1;
}

int wav_open(FILE *in, oe_enc_opt *opt, unsigned char *oldbuf, int buflen)
{
    unsigned char buf[40];
    unsigned int len;
    int samplesize;
    int validbits;
    wav_fmt format;
    wavfile *wav;
    int i;
    (void)buflen;/*unused*/
    (void)oldbuf;/*unused*/

    /* Ok. At this point, we know we have a WAV file. Now we have to detect
     * whether we support the subtype, and we have to find the actual data
     * We don't (for the wav reader) need to use the buffer we used to id this
     * as a wav file (oldbuf)
     */

    if (!find_wav_chunk(in, "fmt ", &len))
    {
        fprintf(stderr, _("ERROR: No format chunk found in WAV file\n"));
        return 0;
    }

    if (len < 16)
    {
        fprintf(stderr, _("ERROR: Unrecognised format chunk in WAV header\n"));
        return 0; /* Weird format chunk */
    }

    /* A common error is to have a format chunk that is not 16, 18 or
     * 40 bytes in size.  This is incorrect, but not fatal, so we only
     * warn about it instead of refusing to work with the file.
     * Please, if you have a program that's creating format chunks of
     * sizes other than 16 or 18 bytes in size, report a bug to the
     * author.
     */
    if (len!=16 && len!=18 && len!=40)
        fprintf(stderr,
                _("Warning: INVALID format chunk in wav header.\n"
                " Trying to read anyway (may not work)...\n"));

    if (!read_chunk(in, buf, sizeof(buf), &len))
    {
        fprintf(stderr, _("ERROR: Incomplete format chunk in WAV header\n"));
        return 0;
    }

    format.format =      READ_U16_LE(buf);
    format.channels =    READ_U16_LE(buf+2);
    format.samplerate =  READ_U32_LE(buf+4);
    format.bytespersec = READ_U32_LE(buf+8);
    format.align =       READ_U16_LE(buf+12);
    format.samplesize =  READ_U16_LE(buf+14);

    if (format.channels == 0)
    {
        fprintf(stderr, _("ERROR: Zero channels in WAV header\n"));
        return 0;
    }

    if (format.format == 0xfffe) /* WAVE_FORMAT_EXTENSIBLE */
    {
        if (len<40)
        {
            fprintf(stderr, _("ERROR: Extended WAV format header invalid (too small)\n"));
            return 0;
        }

        validbits = READ_U16_LE(buf+18);
        if (validbits < 1 || validbits > format.samplesize)
            validbits = format.samplesize;

        if (opt->channels_format == CHANNELS_FORMAT_DEFAULT)
        {
            format.mask = READ_U32_LE(buf+20);
            /* warn the user if the format mask is not a supported/expected type */
            switch (format.mask) {
            case 1539: /* 4.0 using side surround instead of back */
                fprintf(stderr, _("WARNING: WAV file uses side surround instead of rear for quadraphonic;\n"
                    "remapping side speakers to rear in encoding.\n"));
                break;
            case 1551: /* 5.1 using side instead of rear */
                fprintf(stderr, _("WARNING: WAV file uses side surround instead of rear for 5.1;\n"
                    "remapping side speakers to rear in encoding.\n"));
                break;
            case 319:  /* 6.1 using rear instead of side */
                fprintf(stderr, _("WARNING: WAV file uses rear surround instead of side for 6.1;\n"
                    "remapping rear speakers to side in encoding.\n"));
                break;
            case 255:  /* 7.1 'Widescreen' */
                fprintf(stderr, _("WARNING: WAV file is a 7.1 'Widescreen' channel mapping;\n"
                    "remapping speakers to Vorbis 7.1 format.\n"));
                break;
            case 0:    /* default/undeclared */
            case 1:    /* mono (left only) */
            case 4:    /* mono (center only) */
            case 3:    /* stereo */
            case 51:   /* quad */
            case 55:   /* 5.0 */
            case 63:   /* 5.1 */
            case 1807: /* 6.1 */
            case 1599: /* 7.1 */
                break;
            default:
                fprintf(stderr, _("WARNING: Unknown WAV surround channel mask: %u\n"
                        "Blindly mapping speakers using default SMPTE/ITU ordering.\n"),
                        format.mask);
                break;
            }
        }
        format.format = READ_U16_LE(buf+24);
    }
    else
    {
        validbits = format.samplesize;
    }

    if (format.format == 1)
    {
        samplesize = format.samplesize/8;
        opt->read_samples = wav_read;
    }
    else if (format.format == 3)
    {
        validbits = 24;
        samplesize = 4;
        opt->read_samples = wav_ieee_read;
    }
    else
    {
        fprintf(stderr, _("ERROR: Unsupported WAV file type.\n"
                "Must be standard PCM or type 3 floating point PCM.\n"));
        return 0;
    }

    if (format.samplerate > INT_MAX)
    {
        fprintf(stderr, _("ERROR: Preposterous sample rate in WAV header: %u Hz\n"),
            format.samplerate);
        return 0;
    }

    if (!find_wav_chunk(in, "data", &len))
    {
        fprintf(stderr, _("ERROR: No data chunk found in WAV file\n"));
        return 0;
    }

    if (format.align != format.channels * samplesize) {
        /* This is incorrect according to the spec. Warn loudly, then ignore
         * this value.
         */
        fprintf(stderr, _("Warning: WAV 'block alignment' value is incorrect, "
                    "ignoring.\n"
                    "The software that created this file is incorrect.\n"));
    }

    if (format.samplesize == samplesize*8 &&
            (format.samplesize == 24 || format.samplesize == 16 ||
             format.samplesize == 8 ||
         (format.samplesize == 32 && format.format == 3)))
    {
        /* OK, good - we have the one supported format,
           now we want to find the size of the file */
        opt->rate = format.samplerate;
        opt->channels = format.channels;
        opt->samplesize = validbits;
        opt->total_samples_per_channel = 0;

        wav = malloc(sizeof(wavfile));
        wav->f = in;
        wav->samplesread = 0;
        wav->bigendian = 0;
        wav->unsigned8bit = format.samplesize == 8;
        wav->channels = format.channels; /* This is in several places. The price
                                            of trying to abstract stuff. */
        wav->samplesize = format.samplesize;
        wav->totalsamples = 0;

        if (opt->ignorelength)
        {
            /* Assume audio data continues until EOF.
               No percent progress will be reported. */
        }
        else if (len>(format.channels*samplesize*4U) && len<((1U<<31)-65536))
        {
            /* Chunk length is plausible.  Limit the audio data read to
               this length so that we do not misinterpret any additional
               chunks after this as audio.  Also use this length to report
               percent progress. */
            wav->totalsamples = opt->total_samples_per_channel =
                len/(format.channels*samplesize);
        }
        else
        {
            /* Assume audio data continues until EOF.
               If the stream is seekable and the current file size can
               be determined, use that to estimate percent progress
               (opt->total_samples_per_channel), but not to limit the
               number of samples read (wav->totalsamples).  Continue to
               read until EOF even if the file grows while reading. */
            OFF_T pos[2];
            pos[0] = FTELL(in);
            if (pos[0] >= 0 && !FSEEK(in, 0, SEEK_END))
            {
                pos[1] = FTELL(in);
                FSEEK(in, pos[0], SEEK_SET);
                if (pos[1] > pos[0])
                    opt->total_samples_per_channel = (pos[1]-pos[0])/(format.channels*samplesize);
            }
        }

        opt->readdata = (void *)wav;

        wav->channel_permute = malloc(wav->channels * sizeof(int));
        if (opt->channels_format == CHANNELS_FORMAT_DEFAULT && wav->channels <= 8)
            /* Where we know the mappings, use them. */
            memcpy(wav->channel_permute, wav_permute_matrix[wav->channels-1],
                    sizeof(int) * wav->channels);
        else
            /* Use a default 1-1 mapping */
            for (i=0; i < wav->channels; i++)
                wav->channel_permute[i] = i;

        return 1;
    }
    else
    {
        fprintf(stderr, _("ERROR: Unsupported WAV sample size.\n"
                "Must be 8, 16, or 24 bit PCM or 32 bit floating point PCM.\n"));
        return 0;
    }
}

long wav_read(void *in, float *buffer, int samples)
{
    wavfile *f = (wavfile *)in;
    int sampbyte = f->samplesize / 8;
    int realsamples = f->totalsamples > 0 && samples > (f->totalsamples - f->samplesread)
        ? (int)(f->totalsamples - f->samplesread) : samples;
    signed char *buf = alloca(realsamples*sampbyte*f->channels);
    int i,j;
    int *ch_permute = f->channel_permute;

    realsamples = (int)fread(buf, sampbyte*f->channels, realsamples, f->f);
    f->samplesread += realsamples;

    if (f->samplesize==8)
    {
        if (f->unsigned8bit)
        {
            unsigned char *bufu = (unsigned char *)buf;
            for (i = 0; i < realsamples; i++)
            {
                for (j=0; j < f->channels; j++)
                {
                    buffer[i*f->channels+j]=((int)(bufu[i*f->channels + ch_permute[j]])-128)/128.0f;
                }
            }
        }
        else
        {
            for (i = 0; i < realsamples; i++)
            {
                for (j=0; j < f->channels; j++)
                {
                    buffer[i*f->channels+j]=buf[i*f->channels + ch_permute[j]]/128.0f;
                }
            }
        }
    }
    else if (f->samplesize==16)
    {
        if (!f->bigendian)
        {
            for (i = 0; i < realsamples; i++)
            {
                for (j=0; j < f->channels; j++)
                {
                    buffer[i*f->channels+j] = ((buf[i*2*f->channels + 2*ch_permute[j] + 1]<<8) |
                                    (buf[i*2*f->channels + 2*ch_permute[j]] & 0xff))/32768.0f;
                }
            }
        }
        else
        {
            for (i = 0; i < realsamples; i++)
            {
                for (j=0; j < f->channels; j++)
                {
                    buffer[i*f->channels+j]=((buf[i*2*f->channels + 2*ch_permute[j]]<<8) |
                                  (buf[i*2*f->channels + 2*ch_permute[j] + 1] & 0xff))/32768.0f;
                }
            }
        }
    }
    else if (f->samplesize==24)
    {
        if (!f->bigendian) {
            for (i = 0; i < realsamples; i++)
            {
                for (j=0; j < f->channels; j++)
                {
                    buffer[i*f->channels+j] = ((buf[i*3*f->channels + 3*ch_permute[j] + 2] << 16) |
                      (((unsigned char *)buf)[i*3*f->channels + 3*ch_permute[j] + 1] << 8) |
                      (((unsigned char *)buf)[i*3*f->channels + 3*ch_permute[j]] & 0xff))
                        / 8388608.0f;

                }
            }
        }
        else {
            for (i = 0; i < realsamples; i++)
            {
                for (j=0; j < f->channels; j++)
                {
                    buffer[i*f->channels+j] = ((buf[i*3*f->channels + 3*ch_permute[j]] << 16) |
                      (((unsigned char *)buf)[i*3*f->channels + 3*ch_permute[j] + 1] << 8) |
                      (((unsigned char *)buf)[i*3*f->channels + 3*ch_permute[j] + 2] & 0xff))
                        / 8388608.0f;

                }
            }
        }
    }
    else {
        fprintf(stderr, _("Internal error: attempt to read unsupported "
                          "bitdepth %d\n"), f->samplesize);
        return 0;
    }

    return realsamples;
}

long wav_ieee_read(void *in, float *buffer, int samples)
{
    wavfile *f = (wavfile *)in;
    int realsamples = f->totalsamples > 0 && samples > (f->totalsamples - f->samplesread)
        ? (int)(f->totalsamples - f->samplesread) : samples;
    float *buf = alloca(realsamples*4*f->channels); /* de-interleave buffer */
    int i,j;

    realsamples = (int)fread(buf, 4*f->channels, realsamples, f->f);
    f->samplesread += realsamples;

    if (!f->bigendian) {
        for (i=0; i < realsamples; i++)
            for (j=0; j < f->channels; j++)
                buffer[i*f->channels+j] =
                    get_le_float(buf + i*f->channels + f->channel_permute[j]);
    }
    else {
        for (i=0; i < realsamples; i++)
            for (j=0; j < f->channels; j++)
                buffer[i*f->channels+j] =
                    get_be_float(buf + i*f->channels + f->channel_permute[j]);
    }

    return realsamples;
}

void wav_close(void *info)
{
    wavfile *f = (wavfile *)info;
    free(f->channel_permute);

    free(f);
}

int raw_open(FILE *in, oe_enc_opt *opt, unsigned char *buf, int buflen)
{
    wavfile *wav = malloc(sizeof(wavfile));
    int i;
    (void)buf;/*unused*/
    (void)buflen;/*unused*/

    wav->f =             in;
    wav->samplesread =   0;
    wav->bigendian =     opt->endianness;
    wav->unsigned8bit =  opt->samplesize == 8;
    wav->channels =      opt->channels;
    wav->samplesize =    opt->samplesize;
    wav->totalsamples =  0;
    wav->channel_permute = malloc(wav->channels * sizeof(int));
    for (i=0; i < wav->channels; i++)
      wav->channel_permute[i] = i;

    if (opt->rawmode_f)
        opt->read_samples = wav_ieee_read;
    else
        opt->read_samples = wav_read;
    opt->readdata = (void *)wav;
    opt->total_samples_per_channel = 0; /* raw mode, don't bother */
    return 1;
}

typedef struct {
    audio_read_func real_reader;
    void *real_readdata;
    int channels;
    float scale_factor;
} scaler;

static long read_scaler(void *data, float *buffer, int samples)
{
    scaler *d = data;
    long in_samples = d->real_reader(d->real_readdata, buffer, samples);
    int i;

    for (i=0; i < d->channels*in_samples; i++) {
       buffer[i] *= d->scale_factor;
    }

    return in_samples;
}

void setup_scaler(oe_enc_opt *opt, float scale)
{
    scaler *d = calloc(1, sizeof(scaler));

    d->real_reader = opt->read_samples;
    d->real_readdata = opt->readdata;

    opt->read_samples = read_scaler;
    opt->readdata = d;
    d->channels = opt->channels;
    d->scale_factor = scale;
}

typedef struct {
    audio_read_func real_reader;
    void *real_readdata;
    float *bufs;
    float *matrix;
    int in_channels;
    int out_channels;
} downmix;

static long read_downmix(void *data, float *buffer, int samples)
{
    downmix *d = data;
    long in_samples = d->real_reader(d->real_readdata, d->bufs, samples);
    int i,j,k,in_ch,out_ch;

    in_ch=d->in_channels;
    out_ch=d->out_channels;

    for (i=0;i<in_samples;i++) {
      for (j=0;j<out_ch;j++) {
        float *samp;
        samp=&buffer[i*out_ch+j];
        *samp=0;
        for (k=0;k<in_ch;k++) {
          *samp+=d->bufs[i*in_ch+k]*d->matrix[in_ch*j+k];
        }
      }
    }
    return in_samples;
}

int setup_downmix(oe_enc_opt *opt, int out_channels)
{
    static const float stupid_matrix[7][8][2] = {
      /*2*/  {{1,0},{0,1}},
      /*3*/  {{1,0},{0.7071f,0.7071f},{0,1}},
      /*4*/  {{1,0},{0,1},{0.866f,0.5f},{0.5f,0.866f}},
      /*5*/  {{1,0},{0.7071f,0.7071f},{0,1},{0.866f,0.5f},{0.5f,0.866f}},
      /*6*/  {{1,0},{0.7071f,0.7071f},{0,1},{0.866f,0.5f},{0.5f,0.866f},{0.7071f,0.7071f}},
      /*7*/  {{1,0},{0.7071f,0.7071f},{0,1},{0.866f,0.5f},{0.5f,0.866f},{0.6123f,0.6123f},{0.7071f,0.7071f}},
      /*8*/  {{1,0},{0.7071f,0.7071f},{0,1},{0.866f,0.5f},{0.5f,0.866f},{0.866f,0.5f},{0.5f,0.866f},{0.7071f,0.7071f}},
    };
    float sum;
    downmix *d;
    int i,j;

    if (opt->channels<=out_channels || out_channels>2 || opt->channels<=0 || out_channels<=0) {
        fprintf(stderr, _("Downmix must actually downmix and only knows mono/stereo out.\n"));
        return 0;
    }

    if (out_channels==2 && opt->channels>8) {
        fprintf(stderr, _("Downmix only knows how to mix >8ch to mono.\n"));
        return 0;
    }

    d = calloc(1, sizeof(downmix));
    d->bufs = malloc(sizeof(float)*opt->channels*4096);
    d->matrix = malloc(sizeof(float)*opt->channels*out_channels);
    d->real_reader = opt->read_samples;
    d->real_readdata = opt->readdata;
    d->in_channels=opt->channels;
    d->out_channels=out_channels;

    if (out_channels==1&&d->in_channels>8) {
      for (i=0;i<d->in_channels;i++)d->matrix[i]=1.0f/d->in_channels;
    } else if (out_channels==2) {
      for (j=0;j<d->out_channels;j++)
        for (i=0;i<d->in_channels;i++)d->matrix[d->in_channels*j+i]=
          stupid_matrix[opt->channels-2][i][j];
    } else {
      for (i=0;i<d->in_channels;i++)d->matrix[i]=
        (stupid_matrix[opt->channels-2][i][0])+
        (stupid_matrix[opt->channels-2][i][1]);
    }
    sum=0;
    for (i=0;i<d->in_channels*d->out_channels;i++)sum+=d->matrix[i];
    sum=(float)out_channels/sum;
    for (i=0;i<d->in_channels*d->out_channels;i++)d->matrix[i]*=sum;
    opt->read_samples = read_downmix;
    opt->readdata = d;

    opt->channels = out_channels;
    return out_channels;
}

void clear_downmix(oe_enc_opt *opt)
{
    downmix *d = opt->readdata;

    opt->read_samples = d->real_reader;
    opt->readdata = d->real_readdata;
    opt->channels = d->in_channels; /* other things in cleanup rely on this */

    free(d->bufs);
    free(d->matrix);
    free(d);
}
