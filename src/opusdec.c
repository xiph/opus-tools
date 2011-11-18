/* Copyright (c) 2002-2007 Jean-Marc Valin
   Copyright (c) 2008 CSIRO
   Copyright (c) 2007-2009 Xiph.Org Foundation
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
#include <unistd.h>
#endif
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
/*#ifndef HAVE_GETOPT_LONG
#include "getopt_win.h"
#endif*/
#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include <opus.h>
#include <opus_multistream.h>
#include <ogg/ogg.h>

#if defined WIN32 || defined _WIN32
#include "wave_out.h"
/* We need the following two to set stdout to binary */
#include <io.h>
#include <fcntl.h>
#endif
#include <math.h>

#ifdef __MINGW32__
#include "wave_out.c"
#endif

#ifdef HAVE_SYS_SOUNDCARD_H
#include <sys/soundcard.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#elif defined HAVE_SYS_AUDIOIO_H
#include <sys/types.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/audioio.h>
#ifndef AUDIO_ENCODING_SLINEAR
#define AUDIO_ENCODING_SLINEAR AUDIO_ENCODING_LINEAR /* Solaris */
#endif

#endif

#include <string.h>
#include "wav_io.h"
#include "opus_header.h"
#include "speex_resampler.h"

#define MINI(_a,_b)      ((_a)<(_b)?(_a):(_b))
#define MAXI(_a,_b)      ((_a)>(_b)?(_a):(_b))
#define CLAMPI(_a,_b,_c) (MAXI(_a,MINI(_b,_c)))

/* 120ms at 48000 */
#define MAX_FRAME_SIZE (960*6)

#define readint(buf, base) (((buf[base+3]<<24)&0xff000000)| \
                           ((buf[base+2]<<16)&0xff0000)| \
                           ((buf[base+1]<<8)&0xff00)| \
  	           	    (buf[base]&0xff))

typedef struct shapestate shapestate;
struct shapestate {
  float * b_buf;
  float * a_buf;
  int fs;
  int mute;
};

static unsigned int rngseed = 22222;
static inline unsigned int fast_rand() {
  rngseed = (rngseed * 96314165) + 907633515;
  return rngseed;
}

/* This implements a 16 bit quantization with full triangular dither
   and IIR noise shaping. The noise shaping filters were designed by
   Sebastian Gesemann based on the LAME ATH curves with flattening
   to limit their peak gain to 20dB.
   (Everyone elses' noise shaping filters are mildly crazy)
   The 48kHz version of this filter is just a warped version of the
   44.1kHz filter and probably could be improved by shifting the
   HF shelf up in frequency a little bit since 48k has a bit more
   room and being more conservative against bat-ears is probably
   more important than more noise suppression.
   This process can increase the peak level of the signal (in theory
   by the peak error of 1.5 +20dB though this much is unobservable rare)
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
      /*Clamp in float out of paranoia that the input will be >96dBFS and wrap if the
        integer is clamped.*/
      _o[pos+c] = si = lrintf(fmaxf(-32768,fminf(s + r,32767)));
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

static void print_comments(char *comments, int length)
{
   char *c=comments;
   int len, i, nb_fields;

   if (length<(8+4+4))
   {
      fprintf (stderr, "Invalid/corrupted comments\n");
      return;
   }
   if (strncmp(c, "OpusTags", 8) != 0)
   {
      fprintf (stderr, "Invalid/corrupted comments\n");
      return;
   }
   c += 8;
   fprintf(stderr, "Encoded with ");
   len=readint(c, 0);
   c+=4;
   if (len < 0 || len>(length-16))
   {
      fprintf (stderr, "Invalid/corrupted comments\n");
      return;
   }
   fwrite(c, 1, len, stderr);
   c+=len;
   fprintf (stderr, "\n");
   /*The -16 check above makes sure we can read this.*/
   nb_fields=readint(c, 0);
   c+=4;
   length-=16+len;
   if (nb_fields < 0 || nb_fields>(length>>2))
   {
      fprintf (stderr, "Invalid/corrupted comments\n");
      return;
   }
   for (i=0;i<nb_fields;i++)
   {
      if (length<4)
      {
         fprintf (stderr, "Invalid/corrupted comments\n");
         return;
      }
      len=readint(c, 0);
      c+=4;
      length-=4;
      if (len < 0 || len>length)
      {
         fprintf (stderr, "Invalid/corrupted comments\n");
         return;
      }
      fwrite(c, 1, len, stderr);
      c+=len;
      length-=len;
      fprintf (stderr, "\n");
   }
}

FILE *out_file_open(char *outFile, int rate, int *channels)
{
   FILE *fout=NULL;
   /*Open output file*/
   if (strlen(outFile)==0)
   {
#if defined HAVE_SYS_SOUNDCARD_H
      int audio_fd, format, stereo;
      audio_fd=open("/dev/dsp", O_WRONLY);
      if (audio_fd<0)
      {
         perror("Cannot open /dev/dsp");
         exit(1);
      }

      format=AFMT_S16_NE;
      if (ioctl(audio_fd, SNDCTL_DSP_SETFMT, &format)==-1)
      {
         perror("SNDCTL_DSP_SETFMT");
         close(audio_fd);
         exit(1);
      }

      stereo=0;
      if (*channels==2)
         stereo=1;
      if (ioctl(audio_fd, SNDCTL_DSP_STEREO, &stereo)==-1)
      {
         perror("SNDCTL_DSP_STEREO");
         close(audio_fd);
         exit(1);
      }
      if (stereo!=0)
      {
         if (*channels==1)
            fprintf (stderr, "Cannot set mono mode, will decode in stereo\n");
         *channels=2;
      }

      if (ioctl(audio_fd, SNDCTL_DSP_SPEED, &rate)==-1)
      {
         perror("SNDCTL_DSP_SPEED");
         close(audio_fd);
         exit(1);
      }
      fout = fdopen(audio_fd, "w");
#elif defined HAVE_SYS_AUDIOIO_H
      audio_info_t info;
      int audio_fd;

      audio_fd = open("/dev/audio", O_WRONLY);
      if (audio_fd<0)
      {
         perror("Cannot open /dev/audio");
         exit(1);
      }

      AUDIO_INITINFO(&info);
#ifdef AUMODE_PLAY    /* NetBSD/OpenBSD */
      info.mode = AUMODE_PLAY;
#endif
      info.play.encoding = AUDIO_ENCODING_SLINEAR;
      info.play.precision = 16;
      info.play.input_sample_rate = rate;
      info.play.channels = *channels;

      if (ioctl(audio_fd, AUDIO_SETINFO, &info) < 0)
      {
         perror ("AUDIO_SETINFO");
         exit(1);
      }
      fout = fdopen(audio_fd, "w");
#elif defined WIN32 || defined _WIN32
      {
         unsigned int opus_channels = *channels;
         if (Set_WIN_Params (INVALID_FILEDESC, rate, SAMPLE_SIZE, opus_channels))
         {
            fprintf (stderr, "Can't access %s\n", "WAVE OUT");
            exit(1);
         }
      }
#else
      fprintf (stderr, "No soundcard support\n");
      exit(1);
#endif
   } else {
      if (strcmp(outFile,"-")==0)
      {
#if defined WIN32 || defined _WIN32
         _setmode(_fileno(stdout), _O_BINARY);
#endif
         fout=stdout;
      }
      else
      {
         fout = fopen(outFile, "wb");
         if (!fout)
         {
            perror(outFile);
            exit(1);
         }
         if (strcmp(outFile+strlen(outFile)-4,".wav")==0 || strcmp(outFile+strlen(outFile)-4,".WAV")==0)
            write_wav_header(fout, rate, *channels, 0, 0);
      }
   }
   return fout;
}

void usage(void)
{
   printf ("Usage: opusdec [options] input_file.oga [output_file]\n");
   printf ("\n");
   printf ("Decodes a Opus file and produce a WAV file or raw file\n");
   printf ("\n");
   printf ("input_file can be:\n");
   printf ("  filename.oga         regular Opus file\n");
   printf ("  -                    stdin\n");
   printf ("\n");
   printf ("output_file can be:\n");
   printf ("  filename.wav         Wav file\n");
   printf ("  filename.*           Raw PCM file (any extension other that .wav)\n");
   printf ("  -                    stdout\n");
   printf ("  (nothing)            Will be played to soundcard\n");
   printf ("\n");
   printf ("Options:\n");
   printf (" --mono                Force decoding in mono\n");
   printf (" --stereo              Force decoding in stereo\n");
   printf (" --rate n              Force decoding at sampling rate n Hz\n");
   printf (" --no-dither           Do not dither 16-bit output\n");
   printf (" --packet-loss n       Simulate n %% random packet loss\n");
   printf (" -V                    Verbose mode (show bit-rate)\n");
   printf (" -h, --help            This help\n");
   printf (" -v, --version         Version information\n");
   printf ("\n");
}

void version(void)
{
   printf ("opusdec (based on %s)\n",opus_get_version_string());
   printf ("Copyright (C) 2008-2011 Jean-Marc Valin\n");
}

void version_short(void)
{
   printf ("opusdec (based on %s)\n",opus_get_version_string());
   printf ("Copyright (C) 2008-2011 Jean-Marc Valin\n");
}

static OpusMSDecoder *process_header(ogg_packet *op, opus_int32 *rate, int *channels, int *preskip, float *gain, int quiet)
{
   int err;
   OpusMSDecoder *st;
   OpusHeader header;
   unsigned char mapping[256] = {0,1};

   if (opus_header_parse(op->packet, op->bytes, &header)==0)
   {
      fprintf(stderr, "Cannot parse header\n");
      return NULL;
   }

   if (header.channels>2 || header.channels<1)
   {
      fprintf (stderr, "Unsupported number of channels: %d\n", header.channels);
      return NULL;
   }

   *channels = header.channels;

   if (!*rate)
      *rate = header.input_sample_rate;
   *preskip = header.preskip;
   st = opus_multistream_decoder_create(48000, header.channels, 1, header.channels==2 ? 1 : 0, mapping, &err);
   if (err != OPUS_OK)
   {
     fprintf(stderr, "Cannot create encoder: %s\n", opus_strerror(err));
     return NULL;
   }
   if (!st)
   {
      fprintf (stderr, "Decoder initialization failed: %s\n", opus_strerror(err));
      return NULL;
   }

   *gain = pow(10., header.gain/5120.);

   if (header.gain!=0)
      printf("Playback gain: %f (%f dB)\n", *gain, header.gain/256.);
   if (!quiet)
   {
      fprintf (stderr, "Decoding %d Hz audio", *rate);

      if (*channels==1)
         fprintf (stderr, " (mono");
      else
         fprintf (stderr, " (stereo");
      fprintf(stderr, ")\n");
   }

   return st;
}

void audio_write(float *pcm, int channels, int frame_size, FILE *fout, SpeexResamplerState *resampler, int *skip, shapestate *shapemem, int file)
{
   int i,tmp_skip;
   unsigned out_len;
   short out[MAX_FRAME_SIZE*2];
   float buf[MAX_FRAME_SIZE*2];
   float *output;

   do {
     if (resampler){
       output=buf;
       unsigned in_len;
       in_len = frame_size;
       out_len = 1024;
       speex_resampler_process_interleaved_float(resampler, pcm, &in_len, buf, &out_len);
       pcm += channels*in_len;
       frame_size -= in_len;
     } else {
       output=pcm;
       out_len=frame_size;
       frame_size=0;
     }

     if (skip){
       tmp_skip = (*skip>out_len) ? out_len : *skip;
       *skip -= tmp_skip;
     } else {
       tmp_skip = 0;
     }

     /*Convert to short and save to output file*/
     if (shapemem){
       shape_dither_toshort(shapemem,out,output,out_len,channels);
     }else{
       for (i=0;i<out_len*channels;i++)
         out[i]=(short)lrintf(fmax(-32768,fmin(output[i]*32768.f,32767)));
     }
     if ((le_short(1)!=1)&&file){
       for (i=0;i<out_len*channels;i++)
         out[i]=le_short(out[i]);
     }

     fwrite(out+tmp_skip*channels, 2, (out_len-tmp_skip)*channels, fout);
   } while (frame_size != 0);
}

int main(int argc, char **argv)
{
   int c;
   int option_index = 0;
   char *inFile, *outFile;
   FILE *fin, *fout=NULL;
   float output[MAX_FRAME_SIZE*2];
   int frame_size=0;
   OpusMSDecoder *st=NULL;
   int packet_count=0;
   int stream_init = 0;
   int quiet = 0;
   ogg_int64_t page_granule=0;
   ogg_int64_t decoded=0;
   struct option long_options[] =
   {
      {"help", no_argument, NULL, 0},
      {"quiet", no_argument, NULL, 0},
      {"version", no_argument, NULL, 0},
      {"version-short", no_argument, NULL, 0},
      {"rate", required_argument, NULL, 0},
      {"mono", no_argument, NULL, 0},
      {"stereo", no_argument, NULL, 0},
      {"no-dither", no_argument, NULL, 0},
      {"packet-loss", required_argument, NULL, 0},
      {0, 0, 0, 0}
   };
   ogg_sync_state oy;
   ogg_page       og;
   ogg_packet     op;
   ogg_stream_state os;
   int print_bitrate=0;
   int close_in=0;
   int eos=0;
   int audio_size=0;
   float loss_percent=-1;
   int channels=-1;
   int rate=0;
   int wav_format=0;
   int preskip=0;
   int opus_serialno = -1;
   int dither=1;
   shapestate shapemem;
   SpeexResamplerState *resampler=NULL;
   float gain=1;

   shapemem.a_buf=0;
   shapemem.b_buf=0;
   shapemem.mute=960;
   shapemem.fs=0;

   /*Process options*/
   while(1)
   {
      c = getopt_long (argc, argv, "hvV",
                       long_options, &option_index);
      if (c==-1)
         break;

      switch(c)
      {
      case 0:
         if (strcmp(long_options[option_index].name,"help")==0)
         {
            usage();
            exit(0);
         } else if (strcmp(long_options[option_index].name,"quiet")==0)
         {
            quiet = 1;
         } else if (strcmp(long_options[option_index].name,"version")==0)
         {
            version();
            exit(0);
         } else if (strcmp(long_options[option_index].name,"version-short")==0)
         {
            version_short();
            exit(0);
         } else if (strcmp(long_options[option_index].name,"mono")==0)
         {
            channels=1;
         } else if (strcmp(long_options[option_index].name,"stereo")==0)
         {
            channels=2;
         } else if (strcmp(long_options[option_index].name,"no-dither")==0)
         {
            dither=0;
         } else if (strcmp(long_options[option_index].name,"rate")==0)
         {
            rate=atoi (optarg);
         } else if (strcmp(long_options[option_index].name,"packet-loss")==0)
         {
            loss_percent = atof(optarg);
         }
         break;
      case 'h':
         usage();
         exit(0);
         break;
      case 'v':
         version();
         exit(0);
         break;
      case 'V':
         print_bitrate=1;
         break;
      case '?':
         usage();
         exit(1);
         break;
      }
   }
   if (argc-optind!=2 && argc-optind!=1)
   {
      usage();
      exit(1);
   }
   inFile=argv[optind];

   if (argc-optind==2)
      outFile=argv[optind+1];
   else
      outFile = "";
   wav_format = strlen(outFile)>=4 && (
                                       strcmp(outFile+strlen(outFile)-4,".wav")==0
                                       || strcmp(outFile+strlen(outFile)-4,".WAV")==0);
   /*Open input file*/
   if (strcmp(inFile, "-")==0)
   {
#if defined WIN32 || defined _WIN32
      _setmode(_fileno(stdin), _O_BINARY);
#endif
      fin=stdin;
   }
   else
   {
      fin = fopen(inFile, "rb");
      if (!fin)
      {
         perror(inFile);
         exit(1);
      }
      close_in=1;
   }


   /*Init Ogg data struct*/
   ogg_sync_init(&oy);

   /*Main decoding loop*/

   while (1)
   {
      char *data;
      int i, nb_read;
      /*Get the ogg buffer for writing*/
      data = ogg_sync_buffer(&oy, 200);
      /*Read bitstream from input file*/
      nb_read = fread(data, sizeof(char), 200, fin);
      ogg_sync_wrote(&oy, nb_read);

      /*Loop for all complete pages we got (most likely only one)*/
      while (ogg_sync_pageout(&oy, &og)==1)
      {
         if (stream_init == 0) {
            ogg_stream_init(&os, ogg_page_serialno(&og));
            stream_init = 1;
         }
	 if (ogg_page_serialno(&og) != os.serialno) {
	    /* so all streams are read. */
	    ogg_stream_reset_serialno(&os, ogg_page_serialno(&og));
	 }
         /*Add page to the bitstream*/
         ogg_stream_pagein(&os, &og);
         page_granule = ogg_page_granulepos(&og);
         /*Extract all available packets*/
         while (!eos && ogg_stream_packetout(&os, &op) == 1)
         {
	    if (op.bytes>=8 && !memcmp(op.packet, "OpusHead", 8)) {
	       opus_serialno = os.serialno;
	    }
	    if (opus_serialno == -1 || os.serialno != opus_serialno)
	       break;
            /*If first packet, process as OPUS header*/
            if (packet_count==0)
            {
               st = process_header(&op, &rate, &channels, &preskip, &gain, quiet);
               if(shapemem.a_buf)
                 free(shapemem.a_buf);
               if(shapemem.b_buf)
                 free(shapemem.b_buf);
               shapemem.a_buf=calloc(channels,sizeof(float)*4);
               shapemem.b_buf=calloc(channels,sizeof(float)*4);
               shapemem.fs=rate;
               /* Converting preskip to output sampling rate */
               preskip = preskip*(rate/48000.);
               if (!st)
                  exit(1);
               if (rate != 48000)
               {
                  int err;
                  resampler = speex_resampler_init(channels, 48000, rate, 5, &err);
                  if (err!=0)
                     fprintf(stderr, "resampler error: %s\n", speex_resampler_strerror(err));
                  speex_resampler_skip_zeros(resampler);
               }
               fout = out_file_open(outFile, rate, &channels);

            } else if (packet_count==1)
            {
               if (!quiet)
                  print_comments((char*)op.packet, op.bytes);
            } else {
               int lost=0;
               if (loss_percent>0 && 100*((float)rand())/RAND_MAX<loss_percent)
                  lost=1;

               /*End of stream condition*/
               if (op.e_o_s && os.serialno == opus_serialno) /* don't care for anything except opus eos */
                  eos=1;

               {
                  int truncate;
                  int ret;
                  /*Decode frame*/
                  if (!lost)
                     ret = opus_multistream_decode_float(st, (unsigned char*)op.packet, op.bytes, output, MAX_FRAME_SIZE, 0);
                  else
                     ret = opus_multistream_decode_float(st, NULL, 0, output, MAX_FRAME_SIZE, 0);

                  /*for (i=0;i<frame_size*channels;i++)
                    printf ("%d\n", (int)output[i]);*/

                  if (ret<0)
                  {
                     fprintf (stderr, "Decoding error: %s\n", opus_strerror(ret));
                     break;
                  }
                  frame_size = ret;

                  /* Apply header gain */
                  for (i=0;i<frame_size*channels;i++)
                     output[i] *= gain;

                  if (print_bitrate) {
                     opus_int32 tmp=op.bytes;
                     char ch=13;
                     fputc (ch, stderr);
                     fprintf (stderr, "Bitrate in use: %d bytes/packet     ", tmp);
                  }
                  decoded += frame_size;
                  if (decoded > page_granule)
                     truncate = decoded-page_granule;
                  else
                     truncate = 0;
                  {
                     int new_frame_size;
                     if (truncate > frame_size)
                        truncate = frame_size;
                     new_frame_size = frame_size - truncate;
                     audio_write(output, channels, new_frame_size, fout, resampler, &preskip, dither?&shapemem:0, strlen(outFile)==0);
                     audio_size+=sizeof(short)*new_frame_size*channels;
                  }
               }
            }
            packet_count++;
         }
      }
      if (feof(fin))
         break;

   }

   /* Drain the resampler */
   if (resampler)
   {
      int i;
      float zeros[200];
      int drain;

      for (i=0;i<200;i++)
         zeros[i] = 200;
      drain = speex_resampler_get_input_latency(resampler);
      do {
         int tmp = drain;
         if (tmp > 100)
            tmp = 100;
         audio_write(zeros, channels, tmp, fout, resampler, NULL, &shapemem, strlen(outFile)==0);
         drain -= tmp;
      } while (drain>0);
   }

   if (fout && wav_format)
   {
      if (fseek(fout,4,SEEK_SET)==0)
      {
         int tmp;
         tmp = le_int(audio_size+36);
         fwrite(&tmp,4,1,fout);
         if (fseek(fout,32,SEEK_CUR)==0)
         {
            tmp = le_int(audio_size);
            fwrite(&tmp,4,1,fout);
         } else
         {
            fprintf (stderr, "First seek worked, second didn't\n");
         }
      } else {
         fprintf (stderr, "Cannot seek on wave file, size will be incorrect\n");
      }
   }

   if (st)
   {
      opus_multistream_decoder_destroy(st);
   } else {
      fprintf (stderr, "This doesn't look like a Opus file\n");
   }
   if (stream_init)
      ogg_stream_clear(&os);
   ogg_sync_clear(&oy);

#if defined WIN32 || defined _WIN32
   if (strlen(outFile)==0)
      WIN_Audio_close ();
#endif

   if (close_in)
      fclose(fin);
   if (fout != NULL)
      fclose(fout);

   return 0;
}
