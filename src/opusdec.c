/* Copyright (c) 2002-2007 Jean-Marc Valin
   Copyright (c) 2008 CSIRO
   Copyright (c) 2007-2013 Xiph.Org Foundation
   File: opusdec.c

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
# include "config.h"
#endif

#include <stdio.h>
#if !defined WIN32 && !defined _WIN32
# include <unistd.h>
#endif

#include <getopt.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <ctype.h> /*tolower()*/

#include <opus.h>
#include <opusfile.h>

/*We're using this define to test for libopus 1.1 or later until libopus
  provides a better mechanism.*/
#if defined(OPUS_GET_EXPERT_FRAME_DURATION_REQUEST)
/*Enable soft clipping prevention.*/
# define HAVE_SOFT_CLIP (1)
#endif

#if defined WIN32 || defined _WIN32
# include "unicode_support.h"
# include "wave_out.h"
/* We need the following two to set stdout to binary */
# include <io.h>
# include <fcntl.h>
#else
# define fopen_utf8(_x,_y) fopen((_x),(_y))
# define argc_utf8 argc
# define argv_utf8 argv
#endif

#include <math.h>

#ifdef HAVE_LRINTF
# define float2int(x) lrintf(x)
#else
# define float2int(flt) ((int)(floor(.5+flt)))
#endif

#if defined HAVE_LIBSNDIO
# include <sndio.h>
#elif defined HAVE_SYS_SOUNDCARD_H || defined HAVE_MACHINE_SOUNDCARD_H || defined HAVE_SOUNDCARD_H
# if defined HAVE_SYS_SOUNDCARD_H
#  include <sys/soundcard.h>
# elif defined HAVE_MACHINE_SOUNDCARD_H
#  include <machine/soundcard.h>
# else
#  include <soundcard.h>
# endif
# include <sys/types.h>
# include <sys/stat.h>
# include <fcntl.h>
# include <sys/ioctl.h>
#elif defined HAVE_SYS_AUDIOIO_H
# include <sys/types.h>
# include <fcntl.h>
# include <sys/ioctl.h>
# include <sys/audioio.h>
# ifndef AUDIO_ENCODING_SLINEAR
#  define AUDIO_ENCODING_SLINEAR AUDIO_ENCODING_LINEAR /* Solaris */
# endif
#endif

#include <string.h>
#include "wav_io.h"
#include "opus_header.h"
#include "diag_range.h"
#include "speex_resampler.h"
#include "stack_alloc.h"
#include "cpusupport.h"

/* printf format specifier for opus_int64 */
#if !defined opus_int64 && defined PRId64
# define I64FORMAT PRId64
#elif defined WIN32 || defined _WIN32
# define I64FORMAT "I64d"
#else
# define I64FORMAT "lld"
#endif

#define MINI(_a,_b)      ((_a)<(_b)?(_a):(_b))
#define MAXI(_a,_b)      ((_a)>(_b)?(_a):(_b))
#define CLAMPI(_a,_b,_c) (MAXI(_a,MINI(_b,_c)))

/* 120ms at 48000 */
#define MAX_FRAME_SIZE (960*6)

#ifdef HAVE_LIBSNDIO
struct sio_hdl *hdl;
#endif

typedef struct shapestate shapestate;
struct shapestate {
  float * b_buf;
  float * a_buf;
  int fs;
  int mute;
};

static unsigned int rngseed = 22222;
static inline unsigned int fast_rand(void)
{
  rngseed = (rngseed * 96314165) + 907633515;
  return rngseed;
}

#ifndef HAVE_FMINF
# define fminf(_x,_y) ((_x)<(_y)?(_x):(_y))
#endif

#ifndef HAVE_FMAXF
# define fmaxf(_x,_y) ((_x)>(_y)?(_x):(_y))
#endif

/* This implements a 16 bit quantization with full triangular dither
   and IIR noise shaping. The noise shaping filters were designed by
   Sebastian Gesemann based on the LAME ATH curves with flattening
   to limit their peak gain to 20 dB.
   (Everyone elses' noise shaping filters are mildly crazy)
   The 48kHz version of this filter is just a warped version of the
   44.1kHz filter and probably could be improved by shifting the
   HF shelf up in frequency a little bit since 48k has a bit more
   room and being more conservative against bat-ears is probably
   more important than more noise suppression.
   This process can increase the peak level of the signal (in theory
   by the peak error of 1.5 +20 dB though this much is unobservable rare)
   so to avoid clipping the signal is attenuated by a couple thousandths
   of a dB. Initially the approach taken here was to only attenuate by
   the 99.9th percentile, making clipping rare but not impossible (like
   SoX) but the limited gain of the filter means that the worst case was
   only two thousandths of a dB more, so this just uses the worst case.
   The attenuation is probably also helpful to prevent clipping in the DAC
   reconstruction filters or downstream resampling in any case.*/
static inline void shape_dither_toshort(shapestate *_ss, short *_o, float *_i, int _n, int _CC)
{
  const float gains[3]={32768.f-15.f,32768.f-15.f,32768.f-3.f};
  const float fcoef[3][8] =
  {
    {2.2374f, -.7339f, -.1251f, -.6033f, 0.9030f, .0116f, -.5853f, -.2571f}, /* 48.0kHz noise shaping filter sd=2.34*/
    {2.2061f, -.4706f, -.2534f, -.6214f, 1.0587f, .0676f, -.6054f, -.2738f}, /* 44.1kHz noise shaping filter sd=2.51*/
    {1.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f,0.0000f, 0.0000f, 0.0000f}, /* lowpass noise shaping filter sd=0.65*/
  };
  int i;
  int rate=_ss->fs==44100?1:(_ss->fs==48000?0:2);
  float gain=gains[rate];
  float *b_buf;
  float *a_buf;
  int mute=_ss->mute;
  b_buf=_ss->b_buf;
  a_buf=_ss->a_buf;
  /*In order to avoid replacing digital silence with quiet dither noise
    we mute if the output has been silent for a while*/
  if(mute>64)
    memset(a_buf,0,sizeof(float)*_CC*4);
  for(i=0;i<_n;i++)
  {
    int c;
    int pos = i*_CC;
    int silent=1;
    for(c=0;c<_CC;c++)
    {
      int j, si;
      float r,s,err=0;
      silent&=_i[pos+c]==0;
      s=_i[pos+c]*gain;
      for(j=0;j<4;j++)
        err += fcoef[rate][j]*b_buf[c*4+j] - fcoef[rate][j+4]*a_buf[c*4+j];
      memmove(&a_buf[c*4+1],&a_buf[c*4],sizeof(float)*3);
      memmove(&b_buf[c*4+1],&b_buf[c*4],sizeof(float)*3);
      a_buf[c*4]=err;
      s = s - err;
      r=(float)fast_rand()*(1/(float)UINT_MAX) - (float)fast_rand()*(1/(float)UINT_MAX);
      if (mute>16)r=0;
      /*Clamp in float out of paranoia that the input will be >96 dBFS and wrap if the
        integer is clamped.*/
      _o[pos+c] = si = float2int(fmaxf(-32768,fminf(s + r,32767)));
      /*Including clipping in the noise shaping is generally disastrous:
        the futile effort to restore the clipped energy results in more clipping.
        However, small amounts-- at the level which could normally be created by
        dither and rounding-- are harmless and can even reduce clipping somewhat
        due to the clipping sometimes reducing the dither+rounding error.*/
      b_buf[c*4] = (mute>16)?0:fmaxf(-1.5f,fminf(si - s,1.5f));
    }
    mute++;
    if(!silent)mute=0;
  }
  _ss->mute=MINI(mute,960);
}

static void print_comments(const OpusTags *_tags)
{
   int i;
   int ncomments;
   fprintf(stderr, "Encoded with %s\n", _tags->vendor);
   ncomments = _tags->comments;
   for (i=0;i<ncomments;i++) {
      char *comment;
      comment=_tags->user_comments[i];
      if (opus_tagncompare("METADATA_BLOCK_PICTURE",22,comment)==0) {
         OpusPictureTag pic;
         int            err;
         err=opus_picture_tag_parse(&pic, comment);
         fprintf(stderr, "%.23s", comment);
         if (err<0) {
            fprintf(stderr, "<error parsing picture tag>\n");
         } else {
            fprintf(stderr, "%u|%s|%s|%ux%ux%u", pic.type, pic.mime_type,
             pic.description, pic.width, pic.height, pic.depth);
            if (pic.colors != 0) {
               fprintf(stderr, "/%u", pic.colors);
            }
            if (pic.format==OP_PIC_FORMAT_URL) {
               fprintf(stderr, "|%s\n", pic.data);
            } else {
               static const char *pic_format_str[4] = {
                  "image", "JPEG", "PNG", "GIF"
               };
               int format_idx;
               format_idx = pic.format < 1 || pic.format >= 4 ? 0 : pic.format;
               fprintf(stderr, "|<%u bytes of %s data>\n", pic.data_length,
                pic_format_str[format_idx]);
            }
            opus_picture_tag_clear(&pic);
         }
      } else {
         fprintf(stderr, "%s\n", comment);
      }
   }
}

/* Returns 1 on success, 0 on error with message displayed on stderr. */
static int out_file_open(const char *outFile, int *wav_format, int rate,
    int mapping_family, int *channels, int fp, FILE **fout)
{
   /* Open output file or audio playback device. */
   if (!outFile)
   {
#if defined HAVE_LIBSNDIO
      struct sio_par par;

      hdl = sio_open(NULL, SIO_PLAY, 0);
      if (!hdl)
      {
         fprintf(stderr, "Cannot open sndio device\n");
         return 0;
      }

      sio_initpar(&par);
      par.sig = 1;
      par.bits = 16;
      par.rate = rate;
      par.pchan = *channels;

      if (!sio_setpar(hdl, &par) || !sio_getpar(hdl, &par) ||
        par.sig != 1 || par.bits != 16 || par.rate != rate) {
         fprintf(stderr, "could not set sndio parameters\n");
         return 0;
      }
      /*We allow the channel count to be forced to stereo, but not anything
        else.*/
      if (*channels!=par.pchan && par.pchan!=2) {
         fprintf(stderr, "could not set sndio channel count\n");
         return 0;
      }
      *channels = par.pchan;
      if (!sio_start(hdl)) {
          fprintf(stderr, "could not start sndio\n");
          return 0;
      }
      *fout=NULL;
#elif defined HAVE_SYS_SOUNDCARD_H || defined HAVE_MACHINE_SOUNDCARD_H || defined HAVE_SOUNDCARD_H
      int audio_fd, format, stereo;
      audio_fd=open("/dev/dsp", O_WRONLY);
      if (audio_fd<0)
      {
         perror("Cannot open /dev/dsp");
         return 0;
      }

      format=AFMT_S16_NE;
      if (ioctl(audio_fd, SNDCTL_DSP_SETFMT, &format)==-1)
      {
         perror("SNDCTL_DSP_SETFMT");
         close(audio_fd);
         return 0;
      }

      if (*channels > 2)
      {
        /* There doesn't seem to be a way to get or set the channel
         * matrix with the sys/soundcard api, so we can't support
         * multichannel. We fall back to stereo downmix.
         */
        fprintf(stderr, "Cannot configure multichannel playback."
                        " Falling back to stereo.\n");
        *channels=2;
      }
      stereo=0;
      if (*channels==2)
         stereo=1;
      if (ioctl(audio_fd, SNDCTL_DSP_STEREO, &stereo)==-1)
      {
         perror("SNDCTL_DSP_STEREO");
         close(audio_fd);
         return 0;
      }
      if (stereo!=0)
      {
         if (*channels==1)
            fprintf(stderr, "Cannot set mono mode, will decode in stereo\n");
         *channels=2;
      }

      if (ioctl(audio_fd, SNDCTL_DSP_SPEED, &rate)==-1)
      {
         perror("SNDCTL_DSP_SPEED");
         close(audio_fd);
         return 0;
      }
      *fout = fdopen(audio_fd, "w");
      if (!*fout)
      {
        perror("Cannot open output");
        close(audio_fd);
        return 0;
      }
#elif defined HAVE_SYS_AUDIOIO_H
      audio_info_t info;
      int audio_fd;

      audio_fd = open("/dev/audio", O_WRONLY);
      if (audio_fd<0)
      {
         perror("Cannot open /dev/audio");
         return 0;
      }

      AUDIO_INITINFO(&info);
# ifdef AUMODE_PLAY    /* NetBSD/OpenBSD */
      info.mode = AUMODE_PLAY;
# endif
      info.play.encoding = AUDIO_ENCODING_SLINEAR;
      info.play.precision = 16;
      info.play.input_sample_rate = rate;
      info.play.channels = *channels;

      if (ioctl(audio_fd, AUDIO_SETINFO, &info) < 0)
      {
         perror("AUDIO_SETINFO");
         close(audio_fd);
         return 0;
      }
      *fout = fdopen(audio_fd, "w");
      if (!*fout)
      {
        perror("Cannot open output");
        close(audio_fd);
        return 0;
      }
#elif defined WIN32 || defined _WIN32
      {
         unsigned int opus_channels = *channels;
         if (Set_WIN_Params(INVALID_FILEDESC, rate, SAMPLE_SIZE, opus_channels))
         {
            fprintf(stderr, "Can't access %s\n", "WAVE OUT");
            return 0;
         }
         *fout=NULL;
      }
#else
      fprintf(stderr, "No soundcard support\n");
      return 0;
#endif
   } else {
      if (strcmp(outFile,"-")==0)
      {
#if defined WIN32 || defined _WIN32
         _setmode(_fileno(stdout), _O_BINARY);
#endif
         *fout=stdout;
      }
      else
      {
         *fout = fopen_utf8(outFile, "wb");
         if (!*fout)
         {
            perror(outFile);
            return 0;
         }
      }
      if (*wav_format)
      {
         *wav_format = write_wav_header(*fout, rate, mapping_family, *channels, fp);
         if (*wav_format < 0)
         {
            fprintf(stderr, "Error writing WAV header.\n");
            fclose(*fout);
            *fout = NULL;
            return 0;
         }
      }
   }
   return 1;
}

void usage(void)
{
#if defined HAVE_LIBSNDIO || defined HAVE_SYS_SOUNDCARD_H || \
    defined HAVE_MACHINE_SOUNDCARD_H || defined HAVE_SOUNDCARD_H || \
    defined HAVE_SYS_AUDIOIO_H || defined WIN32 || defined _WIN32
   printf("Usage: opusdec [options] input [output]\n");
#else
   printf("Usage: opusdec [options] input output\n");
#endif
   printf("\n");
   printf("Decode audio in Opus format to Wave or raw PCM\n");
   printf("\n");
   printf("input can be:\n");
   printf("  file:filename.opus   Opus URL\n");
   printf("  filename.opus        Opus file\n");
   printf("  -                    stdin\n");
   printf("\n");
   printf("output can be:\n");
   printf("  filename.wav         Wave file\n");
   printf("  filename.*           Raw PCM file (any extension other than .wav)\n");
   printf("  -                    stdout (raw; unless --force-wav)\n");
#if defined HAVE_LIBSNDIO || defined HAVE_SYS_SOUNDCARD_H || \
    defined HAVE_MACHINE_SOUNDCARD_H || defined HAVE_SOUNDCARD_H || \
    defined HAVE_SYS_AUDIOIO_H || defined WIN32 || defined _WIN32
   printf("  (default)            Play audio\n");
#endif
   printf("\n");
   printf("Options:\n");
   printf(" -h, --help            Show this help\n");
   printf(" -V, --version         Show version information\n");
   printf(" --quiet               Suppress program output\n");
   printf(" --rate n              Force decoding at sampling rate n Hz\n");
   printf(" --force-stereo        Force decoding to stereo\n");
   printf(" --gain n              Adjust output volume n dB (negative is quieter)\n");
   printf(" --no-dither           Do not dither 16-bit output\n");
   printf(" --float               Output 32-bit floating-point samples\n");
   printf(" --force-wav           Force Wave header on output\n");
   printf(" --packet-loss n       Simulate n %% random packet loss\n");
   printf(" --save-range file     Save check values for every frame to a file\n");
   printf("\n");
}

void version(void)
{
   printf("opusdec %s %s (using %s)\n",PACKAGE_NAME,PACKAGE_VERSION,opus_get_version_string());
   printf("Copyright (C) 2008-2018 Xiph.Org Foundation\n");
}

void version_short(void)
{
   version();
}

opus_int64 audio_write(float *pcm, int channels, int frame_size, FILE *fout,
 SpeexResamplerState *resampler, float *clipmem, shapestate *shapemem,
 int file, int rate, opus_int64 link_read, opus_int64 link_out, int fp)
{
   opus_int64 sampout=0;
   opus_int64 maxout;
   int ret;
   int i;
   unsigned out_len;
   short *out;
   float *buf;
   float *output;
   out=alloca(sizeof(short)*MAX_FRAME_SIZE*channels);
   buf=alloca(sizeof(float)*MAX_FRAME_SIZE*channels);
   maxout=((link_read/48000)*rate + (link_read%48000)*rate/48000) - link_out;
   maxout=maxout<0?0:maxout;
   do {
     if (resampler) {
       unsigned in_len;
       output=buf;
       in_len = frame_size;
       out_len = 1024<maxout?1024:(unsigned)maxout;
       speex_resampler_process_interleaved_float(resampler,
        pcm, &in_len, buf, &out_len);
       pcm += channels*(in_len);
       frame_size -= in_len;
     } else {
       output=pcm;
       out_len=frame_size<maxout?(unsigned)frame_size:(unsigned)maxout;
       frame_size=0;
     }

     if (!file||!fp)
     {
        /*Convert to short and save to output file*/
#if defined(HAVE_SOFT_CLIP)
        opus_pcm_soft_clip(output,out_len,channels,clipmem);
#else
        (void)clipmem;
#endif
        if (shapemem) {
          shape_dither_toshort(shapemem,out,output,out_len,channels);
        } else {
          for (i=0;i<(int)out_len*channels;i++)
            out[i]=(short)float2int(fmaxf(-32768,fminf(output[i]*32768.f,32767)));
        }
        if ((le_short(1)!=(1))&&file) {
          for (i=0;i<(int)out_len*channels;i++)
            out[i]=le_short(out[i]);
        }
     }
     else if (le_short(1)!=(1)) {
       /* ensure the floats are little endian */
       for (i=0;i<(int)out_len*channels;i++)
         put_le_float(buf+i, output[i]);
       output = buf;
     }

     if (maxout>0)
     {
#if defined WIN32 || defined _WIN32
       if (!file) {
         ret=WIN_Play_Samples(out, sizeof(short) * channels * out_len);
         if (ret>0) ret/=sizeof(short)*channels;
         else fprintf(stderr, "Error playing audio.\n");
       } else
#elif defined HAVE_LIBSNDIO
       if (!file) {
         ret=sio_write(hdl, out, sizeof(short) * channels * out_len);
         if (ret>0) ret/=sizeof(short)*channels;
         else fprintf(stderr, "Error playing audio.\n");
       } else
#endif
         ret=fwrite(fp?(char *)output:(char *)out,
          (fp?sizeof(float):sizeof(short))*channels, out_len, fout);
       sampout+=ret;
       maxout-=ret;
     }
   } while (frame_size>0 && maxout>0);
   return sampout;
}

typedef struct decode_cb_ctx decode_cb_ctx;
struct decode_cb_ctx {
   FILE *frange;
   float loss_percent;
};

static int decode_cb(decode_cb_ctx *ctx, OpusMSDecoder *decoder, void *pcm,
 const ogg_packet *op, int nsamples, int nchannels, int format, int li)
{
   int lost;
   int ret;
   (void)nchannels;
   (void)li;
   lost = ctx->loss_percent>0
    && 100*(float)rand()/(float)RAND_MAX<ctx->loss_percent;
   switch (format)
   {
      case OP_DEC_FORMAT_SHORT:
      {
         if (lost)
         {
            ret = opus_multistream_decode(decoder,
             NULL, 0, pcm, nsamples, 0);
         } else {
            ret = opus_multistream_decode(decoder,
             op->packet, op->bytes, pcm, nsamples, 0);
         }
         break;
      }
      case OP_DEC_FORMAT_FLOAT:
      {
         if (lost)
         {
            ret = opus_multistream_decode_float(decoder,
             NULL, 0, pcm, nsamples, 0);
         } else {
            ret = opus_multistream_decode_float(decoder,
             op->packet, op->bytes, pcm, nsamples, 0);
         }
         break;
      }
      default:
      {
         return OPUS_BAD_ARG;
      }
   }
   /*On success, either we got as many samples as we wanted, or something went
     wrong.*/
   if (ret >= 0)
   {
      ret=ret==nsamples?0:OPUS_INTERNAL_ERROR;
      if (ret==0 && ctx->frange!=NULL)
      {
         OpusDecoder *od;
         opus_uint32 rngs[256];
         int err;
         int si;
         /*If we're collecting --save-range debugging data, collect it now.*/
         for (si=0;si<255;si++)
         {
            err=opus_multistream_decoder_ctl(decoder,
             OPUS_MULTISTREAM_GET_DECODER_STATE(si, &od));
            /*This will fail with OPUS_BAD_ARG the first time we ask for a
              stream that isn't there, which is currently the only way to find
              out how many streams there are using the libopus API.*/
            if (err<0) break;
            opus_decoder_ctl(od,OPUS_GET_FINAL_RANGE(&rngs[si]));
         }
         save_range(ctx->frange, nsamples, op->packet, op->bytes, rngs, si);
      }
   }
   return ret;
}

static void drain_resampler(FILE *fout, int file_output,
 SpeexResamplerState *resampler, int channels, int rate,
 opus_int64 link_read, opus_int64 link_out, float *clipmem,
 shapestate *shapemem, opus_int64 *audio_size, int fp)
{
   float *zeros;
   int drain;
   zeros=(float *)calloc(100*channels,sizeof(float));
   drain=speex_resampler_get_input_latency(resampler);
   do
   {
      opus_int64 outsamp;
      int tmp=MINI(drain, 100);
      outsamp=audio_write(zeros, channels, tmp, fout, resampler, clipmem,
       shapemem, file_output, rate, link_read, link_out, fp);
      link_out+=outsamp;
      (*audio_size)+=(fp?sizeof(float):sizeof(short))*outsamp*channels;
      drain-=tmp;
   } while (drain>0);
   free(zeros);
}

int main(int argc, char **argv)
{
   unsigned char channel_map[OPUS_CHANNEL_COUNT_MAX];
   float clipmem[8]={0};
   int c;
   int option_index = 0;
   int exit_code = 0;
   const char *inFile, *outFile, *rangeFile=NULL;
   FILE *fout=NULL, *frange=NULL;
   float *output;
   float *permuted_output;
   OggOpusFile *st=NULL;
   const OpusHead *head;
   decode_cb_ctx cb_ctx;
   int file_output;
   int old_li=-1;
   int li;
   int quiet = 0;
   int forcewav = 0;
   ogg_int64_t nb_read_total=0;
   ogg_int64_t link_read=0;
   ogg_int64_t link_out=0;
   struct option long_options[] =
   {
      {"help", no_argument, NULL, 0},
      {"quiet", no_argument, NULL, 0},
      {"version", no_argument, NULL, 0},
      {"version-short", no_argument, NULL, 0},
      {"rate", required_argument, NULL, 0},
      {"force-stereo", no_argument, NULL, 0},
      {"gain", required_argument, NULL, 0},
      {"no-dither", no_argument, NULL, 0},
      {"float", no_argument, NULL, 0},
      {"force-wav", no_argument, NULL, 0},
      {"packet-loss", required_argument, NULL, 0},
      {"save-range", required_argument, NULL, 0},
      {0, 0, 0, 0}
   };
   opus_int64 audio_size=0;
   opus_int64 last_coded_seconds=-1;
   float loss_percent=-1;
   float manual_gain=0;
   int force_rate=0;
   int force_stereo=0;
   int requested_channels=-1;
   int channels=-1;
   int rate=0;
   int wav_format=0;
   int dither=1;
   int fp=0;
   shapestate shapemem;
   SpeexResamplerState *resampler=NULL;
   size_t last_spin=0;
#ifdef WIN_UNICODE
   int argc_utf8;
   char **argv_utf8;
#endif

   if (query_cpu_support()) {
     fprintf(stderr,"\n\n** WARNING: This program with compiled with SSE%s\n",query_cpu_support()>1?"2":"");
     fprintf(stderr,"            but this CPU claims to lack these instructions. **\n\n");
   }

#ifdef WIN_UNICODE
   (void)argc;
   (void)argv;

   init_console_utf8();
   init_commandline_arguments_utf8(&argc_utf8, &argv_utf8);
#endif

   /*Process options*/
   while (1)
   {
      c = getopt_long(argc_utf8, argv_utf8, "hV",
                       long_options, &option_index);
      if (c==-1)
         break;

      switch (c)
      {
      case 0:
         if (strcmp(long_options[option_index].name,"help")==0)
         {
            usage();
            goto done;
         } else if (strcmp(long_options[option_index].name,"quiet")==0)
         {
            quiet = 1;
         } else if (strcmp(long_options[option_index].name,"version")==0)
         {
            version();
            goto done;
         } else if (strcmp(long_options[option_index].name,"version-short")==0)
         {
            version_short();
            goto done;
         } else if (strcmp(long_options[option_index].name,"no-dither")==0)
         {
            dither=0;
         } else if (strcmp(long_options[option_index].name,"float")==0)
         {
            fp=1;
         } else if (strcmp(long_options[option_index].name,"force-wav")==0)
         {
            forcewav=1;
         } else if (strcmp(long_options[option_index].name,"rate")==0)
         {
            rate=atoi(optarg);
         } else if (strcmp(long_options[option_index].name,"force-stereo")==0)
         {
            force_stereo=1;
         } else if (strcmp(long_options[option_index].name,"gain")==0)
         {
            manual_gain=atof(optarg);
         } else if (strcmp(long_options[option_index].name,"save-range")==0)
         {
            rangeFile=optarg;
         } else if (strcmp(long_options[option_index].name,"packet-loss")==0)
         {
            loss_percent = atof(optarg);
         }
         break;
      case 'h':
         usage();
         goto done;
      case 'V':
         version();
         goto done;
      case '?':
         usage();
         exit_code=1;
         goto done;
      }
   }
   if (argc_utf8-optind!=2 && argc_utf8-optind!=1)
   {
      usage();
      exit_code=1;
      goto done;
   }
   inFile=argv_utf8[optind];

   /*Output to a file or playback?*/
   file_output=argc_utf8-optind==2;
   if (file_output) {
     /*If we're outputting to a file, should we apply a wav header?*/
     int i;
     char *ext;
     outFile=argv_utf8[optind+1];
     ext=".wav";
     i=strlen(outFile)-4;
     wav_format=i>=0;
     while (wav_format&&ext&&outFile[i]) {
       wav_format&=tolower(outFile[i++])==*ext++;
     }
     wav_format|=forcewav;
   } else {
     outFile=NULL;
     wav_format=0;
     /*If playing to audio out, default the rate to 48000
       instead of the original rate. The original rate is
       only important for minimizing surprise about the rate
       of output files and preserving length, which aren't
       relevant for playback. Many audio devices sound
       better at 48kHz and not resampling also saves CPU.*/
     if (rate==0) rate=48000;
     /*Playback is 16-bit only.*/
     fp=0;
   }
   /*If the output is floating point, don't dither.*/
   if (fp) dither=0;

   /*Open input file*/
   if (strcmp(inFile, "-")==0)
   {
      OpusFileCallbacks cb={NULL,NULL,NULL,NULL};
      int fd;
#if defined WIN32 || defined _WIN32
      fd = _fileno(stdin);
      _setmode(fd, _O_BINARY);
#else
      fd = fileno(stdin);
#endif
      st=op_open_callbacks(op_fdopen(&cb, fd, "rb"), &cb, NULL, 0, NULL);
   }
   else
   {
      st=op_open_url(inFile,NULL,NULL);
      if (st==NULL)
      {
         st=op_open_file(inFile,NULL);
      }
   }
   if (st==NULL)
   {
      fprintf(stderr, "Failed to open '%s'.\n", inFile);
      exit_code=1;
      goto done;
   }

   if (manual_gain != 0.F)
   {
       op_set_gain_offset(st, OP_HEADER_GAIN, float2int(manual_gain*256.F));
   }

   head = op_head(st, 0);
   if (op_seekable(st))
   {
      int nlinks;
      /*If we have a seekable file, we can make some intelligent decisions
        about how to decode.*/
      nlinks = op_link_count(st);
      if (rate==0)
      {
         opus_uint32 initial_rate;
         initial_rate=head->input_sample_rate;
         /*We decode unknown rates at 48 kHz, so don't complain about a
           mismatch between 48 kHz and "unknown".*/
         if (initial_rate==0)
         {
            initial_rate=48000;
         }
         for (li=1;li<nlinks;li++) {
            opus_uint32 cur_rate;
            cur_rate = op_head(st, li)->input_sample_rate;
            if (cur_rate==0)
            {
               cur_rate=48000;
            }
            if (initial_rate!=cur_rate)
            {
               fprintf(stderr,
                "Warning: Chained stream with multiple input sample rates: "
                "forcing decode to 48 kHz.\n");
               rate=48000;
               break;
            }
         }
      }
      if (!force_stereo)
      {
         int initial_channels;
         initial_channels = head->channel_count;
         for (li=1;li<nlinks;li++) {
            int cur_channels;
            cur_channels = op_head(st, li)->channel_count;
            if (initial_channels!=cur_channels)
            {
               fprintf(stderr,
                "Warning: Chained stream with multiple channel counts: "
                "forcing decode to stereo.\n");
               force_stereo=1;
               break;
            }
         }
      }
   }

   if (rate==0)
   {
      rate=head->input_sample_rate;
      /*If the rate is unspecified, we decode to 48000.*/
      if (rate==0)
      {
         rate=48000;
      }
   } else {
      /*Remember that we forced the rate, so we don't complain if it changes in
        an unseekable chained stream.*/
      force_rate=1;
   }
   if (rate<8000||rate>192000)
   {
      fprintf(stderr,
       "Warning: Crazy input_rate %d, decoding to 48000 instead.\n", rate);
      rate=48000;
      force_rate=1;
   }

   if (rangeFile)
   {
      frange=fopen_utf8(rangeFile,"w");
      if (!frange)
      {
         perror(rangeFile);
         fprintf(stderr,"Could not open save-range file: %s\n",rangeFile);
         fprintf(stderr,"Must provide a writable file name.\n");
         exit_code=1;
         goto done;
      }
   }

   requested_channels=force_stereo?2:head->channel_count;
   /*TODO: For seekable sources, write the output length in the WAV header.*/
   channels=requested_channels;
   if (!out_file_open(outFile, &wav_format, rate, head->mapping_family,
        &channels, fp, &fout))
   {
      exit_code=1;
      goto done;
   }
   if (channels!=requested_channels) force_stereo=1;

   /*Setup the memory for the dithered output*/
   shapemem.a_buf=calloc(channels,sizeof(float)*4);
   shapemem.b_buf=calloc(channels,sizeof(float)*4);
   shapemem.mute=960;
   shapemem.fs=rate;

   output=malloc(sizeof(float)*MAX_FRAME_SIZE*channels);
   permuted_output=NULL;
   if (!shapemem.a_buf || !shapemem.b_buf || !output)
   {
      fprintf(stderr, "Memory allocation failure.\n");
      exit_code=1;
      goto cleanup;
   }

   if (wav_format&&(channels==3||channels>4))
   {
      int ci;
      for (ci=0;ci<channels;ci++)
      {
         channel_map[ci]=ci;
      }
      adjust_wav_mapping(head->mapping_family, channels, channel_map);
      permuted_output=malloc(sizeof(float)*MAX_FRAME_SIZE*channels);
      if (!permuted_output)
      {
         fprintf(stderr, "Memory allocation failure.\n");
         exit_code=1;
         goto cleanup;
      }
   }

   /*If we're simulating packet loss or saving range data, then we need to
     install a decoder callback.*/
   if (loss_percent>0 || frange!=NULL)
   {
      cb_ctx.loss_percent=loss_percent;
      cb_ctx.frange=frange;
      op_set_decode_callback(st, (op_decode_cb_func)decode_cb, &cb_ctx);
   }

   /*Main decoding loop*/
   while (1)
   {
      opus_int64 outsamp;
      int nb_read;
      int i;
      if (force_stereo)
      {
         nb_read=op_read_float_stereo(st,
          output, MAX_FRAME_SIZE*channels);
         li = op_current_link(st);
      } else {
         nb_read=op_read_float(st,
          output, MAX_FRAME_SIZE*channels, &li);
      }
      if (nb_read<0) {
         if (nb_read==OP_HOLE) {
            /*TODO: At...?*/
            fprintf(stderr, "Warning: Hole in data.\n");
            continue;
         } else {
            fprintf(stderr, "Decoding error.\n");
            exit_code=1;
            break;
         }
      }
      if (nb_read==0)
      {
         if (!quiet)
         {
            fprintf(stderr, "\rDecoding complete.        \n");
            fflush(stderr);
         }
         break;
      }
      if (li!=old_li)
      {
         /*Drain and reset the resampler to be sure we get an accurate number
           of output samples.*/
         if (resampler!=NULL)
         {
            drain_resampler(fout, file_output, resampler, channels, rate,
             link_read, link_out, clipmem, dither?&shapemem:NULL, &audio_size,
             fp);
            /*Neither speex_resampler_reset_mem() nor
              speex_resampler_skip_zeros() clear the number of fractional
              samples properly, so we just destroy it. It will get re-created
              below.*/
            speex_resampler_destroy(resampler);
            resampler=NULL;
         }
         /*We've encountered a new link.*/
         link_read=link_out=0;
         head=op_head(st, li);
         if (!force_stereo && channels!=head->channel_count)
         {
            /*In theory if the first link was stereo, we could downmix the
              remaining links, but we've already decoded the first packet, and
              this stream is unseekable, so we'd have to write our own downmix
              code. That's more trouble than it's worth.*/
            fprintf(stderr,
             "Error: channel count changed in a chained stream: "
             "aborting.\n");
            exit_code=1;
            break;
         }
         if (!force_rate
          && (opus_uint32)rate!=
          (head->input_sample_rate==0?48000:head->input_sample_rate))
         {
            fprintf(stderr,
             "Warning: input sampling rate changed in a chained stream: "
             "resampling remaining links to %d. Use --rate to override.\n",
             rate);
         }
         if (!quiet)
         {
            if (old_li >= 0)
            {
               /*Clear the progress indicator from the previous link.*/
               fprintf(stderr, "\r");
            }
            fprintf(stderr, "Decoding to %d Hz (%d %s)", rate,
              channels, channels>1?"channels":"channel");
            if (head->version!=1)
            {
               fprintf(stderr, ", Header v%d",head->version);
            }
            fprintf(stderr, "\n");
            if (head->output_gain!=0)
            {
               fprintf(stderr,"Playback gain: %f dB\n", head->output_gain/256.);
            }
            if (manual_gain!=0)
            {
               fprintf(stderr,"Manual gain: %f dB\n", manual_gain);
            }
            print_comments(op_tags(st, li));
         }
      }
      nb_read_total+=nb_read;
      link_read+=nb_read;
      if (!quiet)
      {
         /*Display a progress spinner while decoding.*/
         static const char spinner[]="|/-\\";
         opus_int64 coded_seconds = nb_read_total/48000;
         if (coded_seconds > last_coded_seconds || li != old_li)
         {
            if (coded_seconds > last_coded_seconds)
            {
               last_spin++;
               last_coded_seconds = coded_seconds;
            }
            fprintf(stderr,"\r[%c] %02" I64FORMAT ":%02d:%02d",
             spinner[last_spin&3], coded_seconds/3600,
             (int)((coded_seconds/60)%60), (int)(coded_seconds%60));
            fflush(stderr);
         }
      }
      old_li=li;
      if (permuted_output!=NULL)
      {
         int ci;
         for(i=0;i<nb_read;i++)
         {
            for(ci=0;ci<channels;ci++)
            {
               permuted_output[i*channels+ci]=
                output[i*channels+channel_map[ci]];
            }
         }
      }
      /*Normal players should just play at 48000 or their maximum rate,
        as described in the OggOpus spec.  But for commandline tools
        like opusdec it can be desirable to exactly preserve the original
        sampling rate and duration, so we have a resampler here.*/
      if (rate!=48000 && resampler==NULL)
      {
         int err;
         resampler = speex_resampler_init(channels, 48000, rate, 5, &err);
         if (err!=0)
         {
            fprintf(stderr, "resampler error: %s\n",
             speex_resampler_strerror(err));
         }
         speex_resampler_skip_zeros(resampler);
      }
      outsamp=audio_write(permuted_output?permuted_output:output, channels,
       nb_read, fout, resampler, clipmem, dither?&shapemem:0, file_output,
       rate, link_read, link_out, fp);
      link_out+=outsamp;
      audio_size+=(fp?sizeof(float):sizeof(short))*outsamp*channels;
   }

   if (resampler!=NULL)
   {
      drain_resampler(fout, file_output, resampler, channels, rate,
       link_read, link_out, clipmem, dither?&shapemem:NULL, &audio_size, fp);
      speex_resampler_destroy(resampler);
   }

   /*If we were writing wav, go set the duration.*/
   if (fout && wav_format>0 && update_wav_header(fout, wav_format, audio_size)<0)
   {
      fprintf(stderr, "Warning: Cannot update audio size in output file;"
         " size will be incorrect.\n");
   }

cleanup:
#if defined WIN32 || defined _WIN32
   if (!file_output)
      WIN_Audio_close();
#endif
   if (shapemem.a_buf) free(shapemem.a_buf);
   if (shapemem.b_buf) free(shapemem.b_buf);
   if (output) free(output);
   if (permuted_output) free(permuted_output);
   if (fout) fclose(fout);

done:
   if (frange) fclose(frange);
   if (st) op_free(st);
#ifdef WIN_UNICODE
   free_commandline_arguments_utf8(&argc_utf8, &argv_utf8);
   uninit_console_utf8();
#endif
   return exit_code;
}
