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
#include <string.h>

#include <opus.h>
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

#define MAX_FRAME_SIZE (2*960*3)

#define readint(buf, base) (((buf[base+3]<<24)&0xff000000)| \
                           ((buf[base+2]<<16)&0xff0000)| \
                           ((buf[base+1]<<8)&0xff00)| \
  	           	    (buf[base]&0xff))

static void print_comments(char *comments, int length)
{
   char *c=comments;
   int len, i, nb_fields;
   char *end;
   
   if (strncmp(c, "OpusTags", 8) != 0)
   {
      fprintf (stderr, "Invalid/corrupted comments\n");
      return;
   }
   c += 8;
   fprintf(stderr, "Encoded with ");
   if (length<8)
   {
      fprintf (stderr, "Invalid/corrupted comments\n");
      return;
   }
   end = c+length;
   len=readint(c, 0);
   c+=4;
   if (len < 0 || c+len>end)
   {
      fprintf (stderr, "Invalid/corrupted comments\n");
      return;
   }
   fwrite(c, 1, len, stderr);
   c+=len;
   fprintf (stderr, "\n");
   if (c+4>end)
   {
      fprintf (stderr, "Invalid/corrupted comments\n");
      return;
   }
   nb_fields=readint(c, 0);
   c+=4;
   for (i=0;i<nb_fields;i++)
   {
      if (c+4>end)
      {
         fprintf (stderr, "Invalid/corrupted comments\n");
         return;
      }
      len=readint(c, 0);
      c+=4;
      if (len < 0 || c+len>end)
      {
         fprintf (stderr, "Invalid/corrupted comments\n");
         return;
      }
      fwrite(c, 1, len, stderr);
      c+=len;
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
   printf (" --packet-loss n       Simulate n %% random packet loss\n");
   printf (" -V                    Verbose mode (show bit-rate)\n"); 
   printf (" -h, --help            This help\n");
   printf (" -v, --version         Version information\n");
   printf ("\n");
}

void version(void)
{
   printf ("opusenc (based on %s)\n",opus_get_version_string());
   printf ("Copyright (C) 2008-2011 Jean-Marc Valin\n");
}

void version_short(void)
{
   printf ("opusenc (based on %s)\n",opus_get_version_string());
   printf ("Copyright (C) 2008-2011 Jean-Marc Valin\n");
}

static OpusDecoder *process_header(ogg_packet *op, opus_int32 *rate, int *channels, int *preskip, float *gain, int quiet)
{
   OpusDecoder *st;
   OpusHeader header;

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
   st = opus_decoder_create(48000, header.channels);
   if (!st)
   {
      fprintf (stderr, "Decoder initialization failed.\n");
      return NULL;
   }

   *gain = pow(10., header.gain/5120.);

   if (header.gain!=0)
      printf("Playback gain: %f (%f dB)\n", *gain, header.gain/256.);
   if (!quiet)
   {
      fprintf (stderr, "Decoding %d Hz audio in", *rate);

      if (*channels==1)
         fprintf (stderr, " (mono");
      else
         fprintf (stderr, " (stereo");
      fprintf(stderr, ")\n");
   }

   return st;
}

void audio_write(float *pcm, int channels, int frame_size, FILE *fout, SpeexResamplerState *resampler, int *skip, int file)
{
   int i,tmp_skip;
   unsigned out_len;
   short out[2048];
   float buf[2048];
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
     /*FIXME: This should dither for integer output*/
     if (file){
       for (i=0;i<out_len*channels;i++)
         out[i]=le_short((short)lrintf(fmax(fmin(output[i]*32768.f+0.5f,32767),-32768)));
     } else {
       for (i=0;i<out_len*channels;i++)
         out[i]=(short)lrintf(fmax(fmin(output[i]*32768.f+0.5f,32767),-32768));
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
   float output[MAX_FRAME_SIZE];
   int frame_size=0;
   void *st=NULL;
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
      {"packet-loss", required_argument, NULL, 0},
      {0, 0, 0, 0}
   };
   ogg_sync_state oy;
   ogg_page       og;
   ogg_packet     op;
   ogg_stream_state os;
   int enh_enabled;
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
   SpeexResamplerState *resampler=NULL;
   float gain=1;
   
   enh_enabled = 1;

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

            } else if (strncmp((char*)op.packet, "OpusTags", 8)==0)
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
                     ret = opus_decode_float(st, (unsigned char*)op.packet, op.bytes, output, MAX_FRAME_SIZE, 0);
                  else
                     ret = opus_decode_float(st, NULL, 0, output, MAX_FRAME_SIZE, 0);

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
                     audio_write(output, channels, new_frame_size, fout, resampler, &preskip, strlen(outFile)==0);
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
         audio_write(zeros, channels, tmp, fout, resampler, NULL, strlen(outFile)==0);
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
      opus_decoder_destroy(st);
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
