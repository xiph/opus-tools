/* Copyright (c) 2002-2010 Jean-Marc Valin
   Copyright (c) 2007-2010 Xiph.Org Foundation
   Copyright (c) 2008-2010 Gregory Maxwell
   File: opusenc.c

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
#include <getopt.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "opus_header.h"
#include "speex_resampler.h"

#ifdef _MSC_VER
#define snprintf _snprintf
#endif

#include "opus.h"
#include "opus_header.h"
#include <ogg/ogg.h>
#include "wav_io.h"

#if defined WIN32 || defined _WIN32
/* We need the following two to set stdout to binary */
#include <io.h>
#include <fcntl.h>
#endif

void comment_init(char **comments, int* length, char *vendor_string);
void comment_add(char **comments, int* length, char *tag, char *val);


/*Write an Ogg page to a file pointer*/
int oe_write_page(ogg_page *page, FILE *fp)
{
   int written;
   written = fwrite(page->header,1,page->header_len, fp);
   written += fwrite(page->body,1,page->body_len, fp);
   
   return written;
}

#define MAX_FRAME_SIZE (960*2*3)
#define MAX_FRAME_BYTES 1276
#define IMIN(a,b) ((a) < (b) ? (a) : (b))   /**< Minimum int value.   */
#define IMAX(a,b) ((a) > (b) ? (a) : (b))   /**< Maximum int value.   */

/* Convert input audio bits, endians and channels */
static int read_samples(FILE *fin,int frame_size, int bits, int channels, int lsb, short * input, char *buff, opus_int32 *size)
{   
   short s[MAX_FRAME_SIZE];
   unsigned char *in = (unsigned char*)s;
   int i;
   int nb_read;

   if (size && *size<=0)
   {
      return 0;
   }
   /*Read input audio*/
   if (size)
      *size -= bits/8*channels*frame_size;
   if (buff)
   {
      for (i=0;i<12;i++)
         in[i]=buff[i];
      nb_read = fread(in+12,1,bits/8*channels*frame_size-12, fin) + 12;
      if (size)
         *size += 12;
   } else {
      nb_read = fread(in,1,bits/8*channels* frame_size, fin);
   }
   nb_read /= bits/8*channels;

   /*fprintf (stderr, "%d\n", nb_read);*/
   if (nb_read==0)
      return 0;

   if(bits==8)
   {
      /* Convert 8->16 bits */
      for(i=frame_size*channels-1;i>=0;i--)
      {
         s[i]=(in[i]<<8)^0x8000;
      }
   } else
   {
      /* convert to our endian format */
      for(i=0;i<frame_size*channels;i++)
      {
         if(lsb) 
            s[i]=le_short(s[i]); 
         else
            s[i]=be_short(s[i]);
      }
   }

   /* FIXME: This is probably redundent now */
   /* copy to float input buffer */
   for (i=0;i<frame_size*channels;i++)
   {
      input[i]=s[i];
   }

   for (i=nb_read*channels;i<frame_size*channels;i++)
   {
      input[i]=0;
   }


   return nb_read;
}

void version(const char *version)
{
   printf ("opusenc (Opus %s encoder)\n",version);
   printf ("Copyright (C) 2008-2011 Xiph.Org Foundation (written by Jean-Marc Valin)\n");
}

void version_short(const char *version)
{
   printf ("opusenc (Opus %s encoder)\n",version);
   printf ("Copyright (C) 2008-2011 Xiph.Org Foundation (written by Jean-Marc Valin)\n");
}

void usage(void)
{
   printf ("Usage: opusenc [options] input_file output_file.oga\n");
   printf ("\n");
   printf ("Encodes input_file using Opus. It can read the WAV or raw files.\n");
   printf ("\n");
   printf ("input_file can be:\n");
   printf ("  filename.wav      wav file\n");
   printf ("  filename.*        Raw PCM file (any extension other than .wav)\n");
   printf ("  -                 stdin\n");
   printf ("\n");  
   printf ("output_file can be:\n");
   printf ("  filename.oga      compressed file\n");
   printf ("  -                 stdout\n");
   printf ("\n");  
   printf ("Options:\n");
   printf (" --bitrate n        Encoding bit-rate in kbit/sec\n"); 
   printf (" --cbr              Use constant bitrate encoding\n");
   printf (" --comp n           Encoding complexity (0-10)\n");
   printf (" --framesize n      Frame size (Default: 960)\n");
   printf (" --nopf             Do not use the prefilter/postfilter\n");
   printf (" --independent      Encode frames independently (implies nopf)\n");
   printf (" --comment          Add the given string as an extra comment. This may be\n");
   printf ("                     used multiple times\n");
   printf (" --author           Author of this track\n");
   printf (" --title            Title for this track\n");
   printf (" -h, --help         This help\n"); 
   printf (" -v, --version      Version information\n"); 
   printf (" -V                 Verbose mode (show bit-rate)\n"); 
   printf ("Raw input options:\n");
   printf (" --rate n           Sampling rate for raw input\n"); 
   printf (" --mono             Consider raw input as mono\n"); 
   printf (" --stereo           Consider raw input as stereo\n"); 
   printf (" --le               Raw input is little-endian\n"); 
   printf (" --be               Raw input is big-endian\n"); 
   printf (" --8bit             Raw input is 8-bit unsigned\n"); 
   printf (" --16bit            Raw input is 16-bit signed\n"); 
   printf ("Default raw PCM input is 48kHz, 16-bit, little-endian, stereo\n");
}


int main(int argc, char **argv)
{
   int nb_samples, total_samples=0, nb_encoded;
   int c;
   int option_index = 0;
   char *inFile, *outFile;
   FILE *fin, *fout;
   short input[MAX_FRAME_SIZE];
   opus_int32 frame_size = 960;
   int quiet=0;
   int nbBytes;
   void *st;
   unsigned char bits[MAX_FRAME_BYTES];
   int with_cbr = 0;
   int with_cvbr = 0;
   int total_bytes = 0;
   int peak_bytes = 0;
   struct option long_options[] =
   {
      {"bitrate", required_argument, NULL, 0},
      {"cbr",no_argument,NULL, 0},
      {"cvbr",no_argument,NULL, 0},
      {"comp", required_argument, NULL, 0},
      {"nopf", no_argument, NULL, 0},
      {"independent", no_argument, NULL, 0},
      {"framesize", required_argument, NULL, 0},
      {"help", no_argument, NULL, 0},
      {"quiet", no_argument, NULL, 0},
      {"le", no_argument, NULL, 0},
      {"be", no_argument, NULL, 0},
      {"8bit", no_argument, NULL, 0},
      {"16bit", no_argument, NULL, 0},
      {"mono", no_argument, NULL, 0},
      {"stereo", no_argument, NULL, 0},
      {"rate", required_argument, NULL, 0},
      {"version", no_argument, NULL, 0},
      {"version-short", no_argument, NULL, 0},
      {"comment", required_argument, NULL, 0},
      {"author", required_argument, NULL, 0},
      {"title", required_argument, NULL, 0},
      {0, 0, 0, 0}
   };
   int print_bitrate=0;
   opus_int32 rate=48000;
   opus_int32 size;
   int chan=1;
   int fmt=16;
   int lsb=1;
   ogg_stream_state os;
   ogg_page 		 og;
   ogg_packet 		 op;
   int bytes_written=0, ret, result;
   int id=-1;
   OpusHeader header;
   char vendor_string[64];
   char *comments;
   int comments_length;
   int close_in=0, close_out=0;
   int eos=0;
   float bitrate=-1;
   char first_bytes[12];
   int wave_input=0;
   opus_int32 lookahead = 0;
   int bytes_per_packet=-1;
   int complexity=-127;
   const char *opus_version = "none";
   SpeexResamplerState *resampler=NULL;

   /*Process command-line options*/
   while(1)
   {
      c = getopt_long (argc, argv, "hvV",
                       long_options, &option_index);
      if (c==-1)
         break;
      
      switch(c)
      {
      case 0:
         if (strcmp(long_options[option_index].name,"bitrate")==0)
         {
            bitrate = atof (optarg);
         } else if (strcmp(long_options[option_index].name,"cbr")==0)
         {
            with_cbr=1;
         } else if (strcmp(long_options[option_index].name,"cvbr")==0)
         {
            with_cvbr=1;
         } else if (strcmp(long_options[option_index].name,"help")==0)
         {
            usage();
            exit(0);
         } else if (strcmp(long_options[option_index].name,"quiet")==0)
         {
            quiet = 1;
         } else if (strcmp(long_options[option_index].name,"version")==0)
         {
            version(opus_version);
            exit(0);
         } else if (strcmp(long_options[option_index].name,"version-short")==0)
         {
            version_short(opus_version);
            exit(0);
         } else if (strcmp(long_options[option_index].name,"le")==0)
         {
            lsb=1;
         } else if (strcmp(long_options[option_index].name,"be")==0)
         {
            lsb=0;
         } else if (strcmp(long_options[option_index].name,"8bit")==0)
         {
            fmt=8;
         } else if (strcmp(long_options[option_index].name,"16bit")==0)
         {
            fmt=16;
         } else if (strcmp(long_options[option_index].name,"stereo")==0)
         {
            chan=2;
         } else if (strcmp(long_options[option_index].name,"mono")==0)
         {
            chan=1;
         } else if (strcmp(long_options[option_index].name,"rate")==0)
         {
            rate=atoi (optarg);
         } else if (strcmp(long_options[option_index].name,"comp")==0)
         {
            complexity=atoi (optarg);
         } else if (strcmp(long_options[option_index].name,"framesize")==0)
         {
            frame_size=atoi (optarg);
         } else if (strcmp(long_options[option_index].name,"comment")==0)
         {
	   if (!strchr(optarg, '='))
	   {
	     fprintf (stderr, "Invalid comment: %s\n", optarg);
	     fprintf (stderr, "Comments must be of the form name=value\n");
	     exit(1);
	   }
           comment_add(&comments, &comments_length, NULL, optarg); 
         } else if (strcmp(long_options[option_index].name,"author")==0)
         {
           comment_add(&comments, &comments_length, "author=", optarg); 
         } else if (strcmp(long_options[option_index].name,"title")==0)
         {
           comment_add(&comments, &comments_length, "title=", optarg); 
         }

         break;
      case 'h':
         usage();
         exit(0);
         break;
      case 'v':
         version(opus_version);
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
   if (argc-optind!=2)
   {
      usage();
      exit(1);
   }
   inFile=argv[optind];
   outFile=argv[optind+1];

   /*Initialize Ogg stream struct*/
   srand(time(NULL));
   if (ogg_stream_init(&os, rand())==-1)
   {
      fprintf(stderr,"Error: stream init failed\n");
      exit(1);
   }

   if (strcmp(inFile, "-")==0)
   {
#if defined WIN32 || defined _WIN32
         _setmode(_fileno(stdin), _O_BINARY);
#elif defined OS2
         _fsetmode(stdin,"b");
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

   {
      fread(first_bytes, 1, 12, fin);
      if (strncmp(first_bytes,"RIFF",4)==0 && strncmp(first_bytes,"RIFF",4)==0)
      {
         if (read_wav_header(fin, &rate, &chan, &fmt, &size)==-1)
            exit(1);
         wave_input=1;
         lsb=1; /* CHECK: exists big-endian .wav ?? */
      }
   }

   if (rate != 48000)
   {
      int err;
      fprintf(stderr, "Resampling from %d Hz to %d Hz before encoding\n", rate, 48000);
      resampler = speex_resampler_init(chan, rate, 48000, 5, &err);
      if (err!=0)
         fprintf(stderr, "resampler error: %s\n", speex_resampler_strerror(err));
   }
   if (bitrate<=0.005)
     if (chan==1)
       bitrate=64.0;
     else
       bitrate=128.0;
     
   bytes_per_packet = MAX_FRAME_BYTES;
   
   snprintf(vendor_string, sizeof(vendor_string), "Encoded with Opus %s\n",opus_get_version_string());
   comment_init(&comments, &comments_length, vendor_string);

   header.channels = chan;
   header.pregap = 0;
   header.multi_stream = 0;
   header.sample_rate = rate;
   
   {
      char *st_string="mono";
      if (chan==2)
         st_string="stereo";
      if (!quiet)
         if (with_cbr)
           fprintf (stderr, "Encoding %.0f kHz %s audio in %.0fms packets at %0.3fkbit/sec (%d bytes per packet, CBR)\n",
               header.sample_rate/1000., st_string, frame_size/48., bitrate, bytes_per_packet);
         else
           fprintf (stderr, "Encoding %.0f kHz %s audio in %.0fms packets at %0.3fkbit/sec (%d bytes per packet maximum)\n",
               header.sample_rate/1000., st_string, frame_size/48., bitrate, bytes_per_packet);
   }

   /*Initialize OPUS encoder*/
   st = opus_encoder_create(48000, chan, OPUS_APPLICATION_AUDIO);

   {
      int tmp = (bitrate*1000);
      if (opus_encoder_ctl(st, OPUS_SET_BITRATE(tmp)) != OPUS_OK)
      {
         fprintf (stderr, "bitrate request failed\n");
         return 1;
      }
   }
   if (!with_cbr)
   {
     if (opus_encoder_ctl(st, OPUS_SET_VBR_FLAG(1)) != OPUS_OK)
     {
        fprintf (stderr, "VBR request failed\n");
        return 1;
     }
     if (!with_cvbr)
     {
        if (opus_encoder_ctl(st, OPUS_SET_VBR_CONSTRAINT(0)) != OPUS_OK)
        {
           fprintf (stderr, "VBR constraint failed\n");
           return 1;
        }
     }
   }

   if (complexity!=-127) {
     if (opus_encoder_ctl(st, OPUS_SET_COMPLEXITY(complexity)) != OPUS_OK)
     {
        fprintf (stderr, "Only complexity 0 through 10 is supported\n");
        return 1;
     }
   }

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
      close_out=1;
   }

   /*Write header*/
   {
      unsigned char header_data[100];
      int packet_size = opus_header_to_packet(&header, header_data, 100);
      op.packet = header_data;
      op.bytes = packet_size;
      op.b_o_s = 1;
      op.e_o_s = 0;
      op.granulepos = 0;
      op.packetno = 0;
      ogg_stream_packetin(&os, &op);

      while((result = ogg_stream_flush(&os, &og)))
      {
         if(!result) break;
         ret = oe_write_page(&og, fout);
         if(ret != og.header_len + og.body_len)
         {
            fprintf (stderr,"Error: failed writing header to output stream\n");
            exit(1);
         }
         else
            bytes_written += ret;
      }

      op.packet = (unsigned char *)comments;
      op.bytes = comments_length;
      op.b_o_s = 0;
      op.e_o_s = 0;
      op.granulepos = 0;
      op.packetno = 1;
      ogg_stream_packetin(&os, &op);
   }

   /* writing the rest of the opus header packets */
   while((result = ogg_stream_flush(&os, &og)))
   {
      if(!result) break;
      ret = oe_write_page(&og, fout);
      if(ret != og.header_len + og.body_len)
      {
         fprintf (stderr,"Error: failed writing header to output stream\n");
         exit(1);
      }
      else
         bytes_written += ret;
   }

   free(comments);

   if (!wave_input)
   {
      nb_samples = read_samples(fin,frame_size,fmt,chan,lsb,input, first_bytes, NULL);
   } else {
      nb_samples = read_samples(fin,frame_size,fmt,chan,lsb,input, NULL, &size);
   }
   if (nb_samples==0)
      eos=1;
   total_samples += nb_samples;
   nb_encoded = -lookahead;
   /*Main encoding loop (one frame per iteration)*/
   while (!eos || total_samples>nb_encoded)
   {
      id++;
      /*Encode current frame*/

      nbBytes = opus_encode(st, input, frame_size, bits, bytes_per_packet);
      if (nbBytes<0)
      {
         fprintf(stderr, "Got error %d while encoding. Aborting.\n", nbBytes);
         break;
      }
      nb_encoded += frame_size;
      total_bytes += nbBytes;
      peak_bytes=IMAX(nbBytes,peak_bytes);

      if (wave_input)
      {
         nb_samples = read_samples(fin,frame_size,fmt,chan,lsb,input, NULL, &size);
      } else {
         nb_samples = read_samples(fin,frame_size,fmt,chan,lsb,input, NULL, NULL);
      }
      if (nb_samples==0)
      {
         eos=1;
      }
      if (eos && total_samples<=nb_encoded)
         op.e_o_s = 1;
      else
         op.e_o_s = 0;
      total_samples += nb_samples;

      op.packet = (unsigned char *)bits;
      op.bytes = nbBytes;
      op.b_o_s = 0;
      /*Is this redundent?*/
      if (eos && total_samples<=nb_encoded)
         op.e_o_s = 1;
      else
         op.e_o_s = 0;
      op.granulepos = (id+1)*frame_size-lookahead;
      if (op.granulepos>total_samples)
         op.granulepos = total_samples;
      op.packetno = 2+id;
      /*printf ("granulepos: %d %d %d\n", (int)op.granulepos, op.packetno, op.bytes);*/
      ogg_stream_packetin(&os, &op);

      /*Write all new pages (most likely 0 or 1)*/
      while (ogg_stream_pageout(&os,&og))
      {
         ret = oe_write_page(&og, fout);
         if(ret != og.header_len + og.body_len)
         {
            fprintf (stderr,"Error: failed writing header to output stream\n");
            exit(1);
         }
         else
            bytes_written += ret;
      }
   }
   /*Flush all pages left to be written*/
   while (ogg_stream_flush(&os, &og))
   {
      ret = oe_write_page(&og, fout);
      if(ret != og.header_len + og.body_len)
      {
         fprintf (stderr,"Error: failed writing header to output stream\n");
         exit(1);
      }
      else
         bytes_written += ret;
   }

   if (!with_cbr && !quiet)
     fprintf (stderr, "Average rate %0.3fkbit/sec, %d peak bytes per packet\n", (total_bytes*8.0/((float)nb_encoded/header.sample_rate))/1000.0, peak_bytes);

   opus_encoder_destroy(st);
   ogg_stream_clear(&os);

   if (close_in)
      fclose(fin);
   if (close_out)
      fclose(fout);
   return 0;
}

/*                 
 Comments will be stored in the Vorbis style.            
 It is describled in the "Structure" section of
    http://www.xiph.org/ogg/vorbis/doc/v-comment.html

The comment header is decoded as follows:
  1) [vendor_length] = read an unsigned integer of 32 bits
  2) [vendor_string] = read a UTF-8 vector as [vendor_length] octets
  3) [user_comment_list_length] = read an unsigned integer of 32 bits
  4) iterate [user_comment_list_length] times {
     5) [length] = read an unsigned integer of 32 bits
     6) this iteration's user comment = read a UTF-8 vector as [length] octets
     }
  7) [framing_bit] = read a single bit as boolean
  8) if ( [framing_bit]  unset or end of packet ) then ERROR
  9) done.

  If you have troubles, please write to ymnk@jcraft.com.
 */

#define readint(buf, base) (((buf[base+3]<<24)&0xff000000)| \
                           ((buf[base+2]<<16)&0xff0000)| \
                           ((buf[base+1]<<8)&0xff00)| \
  	           	    (buf[base]&0xff))
#define writeint(buf, base, val) do{ buf[base+3]=((val)>>24)&0xff; \
                                     buf[base+2]=((val)>>16)&0xff; \
                                     buf[base+1]=((val)>>8)&0xff; \
                                     buf[base]=(val)&0xff; \
                                 }while(0)

void comment_init(char **comments, int* length, char *vendor_string)
{
  int vendor_length=strlen(vendor_string);
  int user_comment_list_length=0;
  int len=4+vendor_length+4;
  char *p=(char*)malloc(len);
  if(p==NULL){
     fprintf (stderr, "malloc failed in comment_init()\n");
     exit(1);
  }
  writeint(p, 0, vendor_length);
  memcpy(p+4, vendor_string, vendor_length);
  writeint(p, 4+vendor_length, user_comment_list_length);
  *length=len;
  *comments=p;
}
void comment_add(char **comments, int* length, char *tag, char *val)
{
  char* p=*comments;
  int vendor_length=readint(p, 0);
  int user_comment_list_length=readint(p, 4+vendor_length);
  int tag_len=(tag?strlen(tag):0);
  int val_len=strlen(val);
  int len=(*length)+4+tag_len+val_len;

  p=(char*)realloc(p, len);
  if(p==NULL){
     fprintf (stderr, "realloc failed in comment_add()\n");
     exit(1);
  }

  writeint(p, *length, tag_len+val_len);      /* length of comment */
  if(tag) memcpy(p+*length+4, tag, tag_len);  /* comment */
  memcpy(p+*length+4+tag_len, val, val_len);  /* comment */
  writeint(p, 4+vendor_length, user_comment_list_length+1);

  *comments=p;
  *length=len;
}
#undef readint
#undef writeint
