/* Copyright (C)2002-2011 Jean-Marc Valin
   Copyright (C)2007-2012 Xiph.Org Foundation
   Copyright (C)2008-2012 Gregory Maxwell
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
#include <sys/time.h>
#include <math.h>

#ifdef _MSC_VER
#define snprintf _snprintf
#endif

#include "opus.h"
#include "opus_multistream.h"
#include "opus_header.h"
#include <ogg/ogg.h>
#include "wav_io.h"

#include "opus_header.h"
#include "opusenc.h"

#if defined WIN32 || defined _WIN32
/* We need the following two to set stdout to binary */
#include <io.h>
#include <fcntl.h>
#endif

#ifdef VALGRIND
#include <valgrind/memcheck.h>
#define VG_UNDEF(x,y) VALGRIND_MAKE_MEM_UNDEFINED((x),(y))
#define VG_CHECK(x,y) VALGRIND_CHECK_MEM_IS_DEFINED((x),(y))
#else
#define VG_UNDEF(x,y)
#define VG_CHECK(x,y)
#endif

void comment_init(char **comments, int* length, char *vendor_string);
void comment_add(char **comments, int* length, char *tag, char *val);

/*Write an Ogg page to a file pointer*/
int oe_write_page(ogg_page *page, FILE *fp)
{
   int written;
   written=fwrite(page->header,1,page->header_len, fp);
   written+=fwrite(page->body,1,page->body_len, fp);
   return written;
}

#define MAX_FRAME_BYTES 61295
#define IMIN(a,b) ((a) < (b) ? (a) : (b))   /**< Minimum int value.   */
#define IMAX(a,b) ((a) > (b) ? (a) : (b))   /**< Maximum int value.   */

void version(const char *version)
{
  printf("opusenc (using %s)\n",version);
  printf("Copyright (C) 2008-2012 Xiph.Org Foundation\n");
}

void version_short(const char *version)
{
  printf("opusenc (using %s)\n",version);
  printf("Copyright (C) 2008-2012 Xiph.Org Foundation\n");
}

void usage(void)
{
  printf("Usage: opusenc [options] input_file output_file.oga\n");
  printf("\n");
  printf("Encodes input_file using Opus. It can read the WAV, AIFF, or raw files.\n");
  printf("\nGeneral options:\n");
  printf(" -h, --help         This help\n");
  printf(" -v, --version      Version information\n");
  printf(" --quiet            Quiet mode\n");
  printf("\n");
  printf("input_file can be:\n");
  printf("  filename.wav      file\n");
  printf("  -                 stdin\n");
  printf("\n");
  printf("output_file can be:\n");
  printf("  filename.oga      compressed file\n");
  printf("  -                 stdout\n");
  printf("\nEncoding options:\n");
  printf(" --speech           Optimize for speech\n");
  printf(" --music            Optimize for music\n");
  printf(" --bitrate n.nnn    Encoding bit-rate in kbit/sec (6-256 per-channel)\n");
  printf(" --vbr              Use variable bitrate encoding (default)\n");
  printf(" --cvbr             Use constrained variable bitrate encoding\n");
  printf(" --hard-cbr         Use hard constant bitrate encoding\n");
  printf(" --comp n           Encoding complexity (0-10, default: 10)\n");
  printf(" --framesize n      Maximum frame size in milliseconds\n");
  printf("                      (2.5, 5, 10, 20, 40, 60, default: 20)\n");
  printf(" --expect-loss      Percentage packet loss to expect (default: 0)\n");
  printf(" --downmix-mono     Downmix to mono\n");
  printf(" --downmix-stereo   Downmix to to stereo (if >2 channels)\n");
  printf(" --max-ogg-delay n  Maximum container delay in milliseconds\n");
  printf("                      (0-1000, default: 1000)\n");
  printf("\nDiagnostic options:\n");
  printf(" --save-range file  Saves check values for every frame to a file\n");
  printf(" --set-ctl-int x=y  Pass the encoder control x with value y (advanced)\n");
  printf("                      This may be used multiple times\n");
  printf(" --uncoupled        Use one mono stream per channel\n");
  printf("\nMetadata options:\n");
  printf(" --comment          Add the given string as an extra comment\n");
  printf("                      This may be used multiple times\n");
  printf(" --author           Author of this track\n");
  printf(" --title            Title for this track\n");
  printf("\nInput options:\n");
  printf(" --raw              Raw input\n");
  printf(" --raw-bits n       Set bits/sample for raw input (default: 16)\n");
  printf(" --raw-rate n       Set sampling rate for raw input (default: 48000)\n");
  printf(" --raw-chan n       Set number of channels for raw input (default: 2)\n");
  printf(" --raw-endianness n 1 for bigendian, 0 for little (defaults to 0)\n");
}

static inline void print_time(double seconds)
{
  long long hours, minutes;
  hours=seconds/3600;
  seconds-=hours*3600.;
  minutes=seconds/60;
  seconds-=minutes*60.;
  if(hours)fprintf(stderr," %lld hour%s%s",hours,hours>1?"s":"",
                   minutes&&!(seconds>0)?" and":"");
  if(minutes)fprintf(stderr,"%s%lld minute%s%s",hours?", ":" ",minutes,
                     minutes>1?"s":"",!hours&&seconds>0?" and":seconds>0?", and":"");
  if(seconds>0)fprintf(stderr," %0.4g second%s",seconds,seconds!=1?"s":"");
}

int main(int argc, char **argv)
{
  int option_index=0;
  struct option long_options[] =
  {
    {"quiet", no_argument, NULL, 0},
    {"bitrate", required_argument, NULL, 0},
    {"hard-cbr",no_argument,NULL, 0},
    {"vbr",no_argument,NULL, 0},
    {"cvbr",no_argument,NULL, 0},
    {"comp", required_argument, NULL, 0},
    {"framesize", required_argument, NULL, 0},
    {"expect-loss", required_argument, NULL, 0},
    {"downmix-mono",no_argument,NULL, 0},
    {"downmix-stereo",no_argument,NULL, 0},
    {"max-ogg-delay", required_argument, NULL, 0},
    {"save-range", required_argument, NULL, 0},
    {"set-ctl-int", required_argument, NULL, 0},
    {"uncoupled", no_argument, NULL, 0},
    {"help", no_argument, NULL, 0},
    {"raw", no_argument, NULL, 0},
    {"raw-bits", required_argument, NULL, 0},
    {"raw-rate", required_argument, NULL, 0},
    {"raw-chan", required_argument, NULL, 0},
    {"raw-endianness", required_argument, NULL, 0},
    {"rate", required_argument, NULL, 0},
    {"music", no_argument, NULL, 0},
    {"speech", no_argument, NULL, 0},
    {"version", no_argument, NULL, 0},
    {"version-short", no_argument, NULL, 0},
    {"comment", required_argument, NULL, 0},
    {"author", required_argument, NULL, 0},
    {"title", required_argument, NULL, 0},
    {0, 0, 0, 0}
  };
  int i, ret;
  OpusMSEncoder    *st;
  const char       *opus_version;
  unsigned char    *packet;
  float            *input;
  /*I/O*/
  oe_enc_opt       inopt;
  input_format     *in_format;
  char             *inFile;
  char             *outFile;
  char             *range_file;
  FILE             *fin;
  FILE             *fout;
  FILE             *frange;
  ogg_stream_state os;
  ogg_page         og;
  ogg_packet       op;
  ogg_int64_t      last_granulepos=0;
  ogg_int32_t      id=-1;
  int              eos=0;
  OpusHeader       header;
  int              comments_length;
  char             vendor_string[64];
  char             *comments;
  /*Counters*/
  opus_int64       nb_encoded=0;
  opus_int64       bytes_written=0;
  opus_int64       pages_out=0;
  opus_int64       total_bytes=0;
  opus_int64       total_samples=0;
  opus_int32       nbBytes;
  opus_int32       nb_samples;
  opus_int32       peak_bytes=0;
  opus_int32       min_bytes=MAX_FRAME_BYTES;
  struct timeval   start_time;
  struct timeval   stop_time;
  time_t           last_spin=0;
  int              last_spin_len=0;
  /*Settings*/
  int              quiet=0;
  opus_int32       bitrate=-1;
  opus_int32       rate=48000;
  opus_int32       coding_rate=48000;
  opus_int32       frame_size=960;
  int              chan=2;
  int              with_hard_cbr=0;
  int              with_cvbr=0;
  int              signal=OPUS_AUTO;
  int              expect_loss=0;
  int              complexity=10;
  int              downmix=0;
  int              uncoupled=0;
  int              *opt_ctls_ctlval;
  int              opt_ctls=0;
  int              max_ogg_delay=48000; /*@48kHz*/
  opus_int32       lookahead=0;
  unsigned char    mapping[256];
  int              force_narrow=0;

  opt_ctls_ctlval=NULL;
  frange=NULL;
  range_file=NULL;
  in_format=NULL;
  inopt.channels=chan;
  inopt.rate=coding_rate=rate;
  inopt.samplesize=16;
  inopt.endianness=0;
  inopt.rawmode=0;

  for(i=0;i<256;i++)mapping[i]=i;

  opus_version=opus_get_version_string();
  snprintf(vendor_string, sizeof(vendor_string), "%s\n",opus_version);
  comment_init(&comments, &comments_length, vendor_string);

  packet=malloc(sizeof(unsigned char)*MAX_FRAME_BYTES);
  if(packet==NULL){
    fprintf(stderr,"Error allocating packet buffer.\n");
    exit(1);
  }

  /*Process command-line options*/
  while(1){
    int c;
    c=getopt_long(argc, argv, "hv",
                  long_options, &option_index);
    if(c==-1)
       break;

    switch(c){
      case 0:
        if(strcmp(long_options[option_index].name,"quiet")==0){
          quiet=1;
        }else if(strcmp(long_options[option_index].name,"bitrate")==0){
          bitrate=atof(optarg)*1000.;
        }else if(strcmp(long_options[option_index].name,"hard-cbr")==0){
          with_hard_cbr=1;
          with_cvbr=0;
        }else if(strcmp(long_options[option_index].name,"cvbr")==0){
          with_cvbr=1;
          with_hard_cbr=0;
        }else if(strcmp(long_options[option_index].name,"vbr")==0){
          with_cvbr=0;
          with_hard_cbr=0;
        }else if(strcmp(long_options[option_index].name,"help")==0){
          usage();
          exit(0);
        }else if(strcmp(long_options[option_index].name,"version")==0){
          version(opus_version);
          exit(0);
        }else if(strcmp(long_options[option_index].name,"version-short")==0){
          version_short(opus_version);
          exit(0);
        }else if(strcmp(long_options[option_index].name,"raw")==0){
          inopt.rawmode=1;
        }else if(strcmp(long_options[option_index].name,"raw-bits")==0){
          inopt.rawmode=1;
          inopt.samplesize=atoi(optarg);
          if(inopt.samplesize!=8&&inopt.samplesize!=16&&inopt.samplesize!=24){
            fprintf(stderr,"Invalid bit-depth: %s\n",optarg);
            fprintf(stderr,"--raw-bits must be one of 8,16, or 24\n");
            exit(1);
          }
        }else if(strcmp(long_options[option_index].name,"raw-rate")==0){
          inopt.rawmode=1;
          inopt.rate=atoi(optarg);
        }else if(strcmp(long_options[option_index].name,"raw-chan")==0){
          inopt.rawmode=1;
          inopt.channels=atoi(optarg);
        }else if(strcmp(long_options[option_index].name,"raw-endianness")==0){
          inopt.rawmode=1;
          inopt.endianness=atoi(optarg);
        }else if(strcmp(long_options[option_index].name,"music")==0){
          signal=OPUS_SIGNAL_MUSIC;
        }else if(strcmp(long_options[option_index].name,"speech")==0){
          signal=OPUS_SIGNAL_VOICE;
        }else if(strcmp(long_options[option_index].name,"downmix-mono")==0){
          downmix=1;
        }else if(strcmp(long_options[option_index].name,"downmix-stereo")==0){
          downmix=2;
        }else if(strcmp(long_options[option_index].name,"expect-loss")==0){
          expect_loss=atoi(optarg);
          if(expect_loss>100||expect_loss<0){
            fprintf(stderr,"Invalid expect-loss: %s\n",optarg);
            fprintf(stderr,"Expected loss is a percent and must be 0-100.\n");
            exit(1);
          }
        }else if(strcmp(long_options[option_index].name,"comp")==0){
          complexity=atoi(optarg);
          if(complexity>10||complexity<0){
            fprintf(stderr,"Invalid complexity: %s\n",optarg);
            fprintf(stderr,"Complexity must be 0-10.\n");
            exit(1);
          }
        }else if(strcmp(long_options[option_index].name,"framesize")==0){
          if(strcmp(optarg,"2.5")==0)frame_size=120;
          else if(strcmp(optarg,"5")==0)frame_size=240;
          else if(strcmp(optarg,"10")==0)frame_size=480;
          else if(strcmp(optarg,"20")==0)frame_size=960;
          else if(strcmp(optarg,"40")==0)frame_size=1920;
          else if(strcmp(optarg,"60")==0)frame_size=2880;
          else{
            fprintf(stderr,"Invalid framesize: %s\n",optarg);
            fprintf(stderr,"Framesize must be 2.5, 5, 10, 20, 40, or 60.\n");
            exit(1);
          }
        }else if(strcmp(long_options[option_index].name,"max-ogg-delay")==0){
          max_ogg_delay=floor(atof(optarg)*48.);
          if(max_ogg_delay<0||max_ogg_delay>48000){
            fprintf(stderr,"Invalid max-ogg-delay: %s\n",optarg);
            fprintf(stderr,"max-ogg-delay 0-1000 ms.\n");
            exit(1);
          }
        }else if(strcmp(long_options[option_index].name,"set-ctl-int")==0){
          int len=strlen(optarg);
          char *spos;
          spos=strchr(optarg,'=');
          if(len<3||spos==NULL||(spos-optarg)<1||(spos-optarg)>=len){
            fprintf(stderr, "Invalid set-ctl-int: %s\n", optarg);
            fprintf(stderr, "Syntax is --set-ctl-int intX=intY\n");
            exit(1);
          }
          if((atoi(optarg)&1)!=0){
            fprintf(stderr, "Invalid set-ctl-int: %s\n", optarg);
            fprintf(stderr, "libopus set CTL values are even.\n");
            exit(1);
          }
          if(opt_ctls==0)opt_ctls_ctlval=malloc(sizeof(int)*2);
          else opt_ctls_ctlval=realloc(opt_ctls_ctlval,sizeof(int)*(opt_ctls+1)*2);
          opt_ctls_ctlval[opt_ctls<<1]=atoi(optarg);
          opt_ctls_ctlval[(opt_ctls<<1)+1]=atoi(spos+1);
          opt_ctls++;
        }else if(strcmp(long_options[option_index].name,"save-range")==0){
          frange=fopen(optarg,"w");
          if(frange==NULL){
            perror(optarg);
            fprintf(stderr,"Could not open save-range file: %s\n",optarg);
            fprintf(stderr,"Must provide a writable file name.\n");
            exit(1);
          }
          range_file=optarg;
        }else if(strcmp(long_options[option_index].name,"uncoupled")==0){
          uncoupled=1;
        }else if(strcmp(long_options[option_index].name,"comment")==0){
          if(!strchr(optarg,'=')){
            fprintf(stderr, "Invalid comment: %s\n", optarg);
            fprintf(stderr, "Comments must be of the form name=value\n");
            exit(1);
          }
          comment_add(&comments, &comments_length, NULL, optarg);
        }else if(strcmp(long_options[option_index].name,"author")==0){
          comment_add(&comments, &comments_length, "author=", optarg);
        } else if(strcmp(long_options[option_index].name,"title")==0){
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
      case '?':
        usage();
        exit(1);
        break;
    }
  }
  if(argc-optind!=2){
    usage();
    exit(1);
  }
  inFile=argv[optind];
  outFile=argv[optind+1];

  /*Initialize Ogg stream struct*/
  gettimeofday(&start_time,NULL);
  srand(start_time.tv_sec^start_time.tv_usec);
  if(ogg_stream_init(&os, rand())==-1){
    fprintf(stderr,"Error: stream init failed\n");
    exit(1);
  }

  if(strcmp(inFile, "-")==0){
#if defined WIN32 || defined _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#elif defined OS2
    _fsetmode(stdin,"b");
#endif
    fin=stdin;
  }else{
    fin=fopen(inFile, "rb");
    if(!fin){
      perror(inFile);
      exit(1);
    }
  }

  if(inopt.rawmode){
    static input_format raw_format = {NULL, 0, raw_open, wav_close, "raw",N_("RAW file reader")};
    in_format = &raw_format;
    in_format->open_func(fin, &inopt, NULL, 0);
  }else in_format=open_audio_file(fin,&inopt);

  if(!in_format){
    fprintf(stderr,"Error parsing input file: %s\n",inFile);
    exit(1);
  }

  rate=inopt.rate;
  chan=inopt.channels;
  inopt.skip=0;

  /*In order to code the complete length we'll need to do a little padding*/
  setup_padder(&inopt);

  if(rate>24000)coding_rate=48000;
  else if(rate>16000)coding_rate=24000;
  else if(rate>12000)coding_rate=12000;
  else coding_rate=8000;

  frame_size=frame_size/(48000/coding_rate);


  /*Scale the resampler complexity, but only for 48000 output because
    the near-cutoff behavior matters a lot more at lower rates.*/
  if(rate!=coding_rate)setup_resample(&inopt,coding_rate==48000?complexity/2:5,coding_rate);

  /*OggOpus headers*/ /*FIXME: broke forcemono*/
  header.channels=chan;
  header.nb_streams=header.channels;
  header.nb_coupled=0;
  if(header.channels<=8&&!uncoupled){
    static const unsigned char opusenc_streams[8][10]={
      /*Coupled, NB_bitmap, mapping...*/
      /*1*/ {0,   0, 0},
      /*2*/ {1,   0, 0,1},
      /*3*/ {1,   0, 0,2,1},
      /*4*/ {2,   0, 0,1,2,3},
      /*5*/ {2,   0, 0,4,1,2,3},
      /*6*/ {2,1<<5, 0,4,1,2,3,5},
      /*7*/ {2,1<<6, 0,4,1,2,3,5,6},
      /*6*/ {3,1<<7, 0,6,1,2,3,4,5,7}
    };
    for(i=0;i<header.channels;i++)mapping[i]=opusenc_streams[header.channels-1][i+2];
    force_narrow=opusenc_streams[header.channels-1][1];
    header.nb_coupled=opusenc_streams[header.channels-1][0];
    header.nb_streams=header.channels-header.nb_coupled;
  }
  header.channel_mapping=header.channels>8?255:header.nb_streams>1;
  if(header.channel_mapping>0)for(i=0;i<header.channels;i++)header.stream_map[i]=mapping[i];
  /* 0 dB gain is the recommended unless you know what you're doing */
  header.gain=0;
  header.input_sample_rate=rate;

  /*Initialize OPUS encoder*/
  st=opus_multistream_encoder_create(coding_rate, chan, header.nb_streams, header.nb_coupled, mapping, OPUS_APPLICATION_AUDIO, &ret);
  if(ret!=OPUS_OK){
    fprintf(stderr, "Cannot create encoder: %s\n", opus_strerror(ret));
    exit(1);
  }

  ret=opus_multistream_encoder_ctl(st, OPUS_SET_SIGNAL(signal));
  if(ret!=OPUS_OK){
    fprintf(stderr,"OPUS_SET_SIGNAL returned: %s\n",opus_strerror(ret));
    exit(1);
  }

  if(force_narrow!=0){
    for(i=0;i<header.nb_streams;i++){
      if(force_narrow&(1<<i)){
        OpusEncoder *oe;
        opus_multistream_encoder_ctl(st,OPUS_MULTISTREAM_GET_ENCODER_STATE(i,&oe));
        ret=opus_encoder_ctl(oe, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_NARROWBAND));
        if(ret!=OPUS_OK){
          fprintf(stderr,"OPUS_SET_MAX_BANDWIDTH on stream %d returned: %s\n",i,opus_strerror(ret));
          exit(1);
        }
      }
    }
  }

  bitrate=bitrate>0?bitrate:64000*header.nb_streams+32000*header.nb_coupled;

  if(bitrate>2048000||bitrate<500){
    fprintf(stderr,"Error: Bitrate %d bits/sec is insane.\nDid you mistake bits for kilobits?\n",bitrate);
    fprintf(stderr,"--bitrate values from 6-256 kbit/sec per channel are meaningful.\n");
    exit(1);
  }
  bitrate=IMIN(chan*256000,bitrate);

  ret=opus_multistream_encoder_ctl(st, OPUS_SET_BITRATE(bitrate));
  if(ret!=OPUS_OK){
    fprintf(stderr,"OPUS_SET_BITRATE returned: %s\n",opus_strerror(ret));
    exit(1);
  }

  ret=opus_multistream_encoder_ctl(st, OPUS_SET_VBR(!with_hard_cbr));
  if(ret!=OPUS_OK){
    fprintf(stderr,"OPUS_SET_VBR returned: %s\n",opus_strerror(ret));
    exit(1);
  }

  if(!with_hard_cbr){
    ret=opus_multistream_encoder_ctl(st, OPUS_SET_VBR_CONSTRAINT(with_cvbr));
    if(ret!=OPUS_OK){
      fprintf(stderr,"OPUS_SET_VBR_CONSTRAINT returned: %s\n",opus_strerror(ret));
      exit(1);
    }
  }

  ret=opus_multistream_encoder_ctl(st, OPUS_SET_COMPLEXITY(complexity));
  if(ret!=OPUS_OK){
    fprintf(stderr,"OPUS_SET_COMPLEXITY returned: %s\n",opus_strerror(ret));
    exit(1);
  }

  ret=opus_multistream_encoder_ctl(st, OPUS_SET_PACKET_LOSS_PERC(expect_loss));
  if(ret!=OPUS_OK){
    fprintf(stderr,"OPUS_SET_PACKET_LOSS_PERC returned: %s\n",opus_strerror(ret));
    exit(1);
  }

  for(i=0;i<opt_ctls;i++){
    ret=opus_multistream_encoder_ctl(st,opt_ctls_ctlval[i<<1],opt_ctls_ctlval[(i<<1)+1]);
    if(ret!=OPUS_OK){
      fprintf(stderr,"opus_multistream_encoder_ctl(st,%d,%d) returned: %s\n",opt_ctls_ctlval[i<<1],opt_ctls_ctlval[(i<<1)+1],opus_strerror(ret));
      exit(1);
    }
  }

  /*We do the lookahead check late so user CTLs can change it*/
  ret=opus_multistream_encoder_ctl(st, OPUS_GET_LOOKAHEAD(&lookahead));
  if(ret!=OPUS_OK){
    fprintf(stderr,"OPUS_GET_LOOKAHEAD returned: %s\n",opus_strerror(ret));
    exit(1);
  }
  inopt.skip+=lookahead;
  /*Regardless of the rate we're coding at the ogg timestamping/skip is
    always timed at 48000.*/
  header.preskip=inopt.skip*(48000./coding_rate);
  /* Extra samples that need to be read to compensate for the pre-skip */
  inopt.extraout=(int)header.preskip*(rate/48000.);

  if(!quiet){
    int opus_app;
    fprintf(stderr,"Encoding using %s",opus_version);
    opus_multistream_encoder_ctl(st,OPUS_GET_APPLICATION(&opus_app));
    if(opus_app==OPUS_APPLICATION_VOIP)fprintf(stderr," (VoIP)\n");
    else if(opus_app==OPUS_APPLICATION_AUDIO)fprintf(stderr," (audio)\n");
    else if(opus_app==OPUS_APPLICATION_RESTRICTED_LOWDELAY)fprintf(stderr," (low-delay)\n");
    else fprintf(stderr," (unknown)\n");
    fprintf(stderr,"-----------------------------------------------------\n");
    fprintf(stderr,"  Input: %0.6gkHz %d channel%s\n",
            header.input_sample_rate/1000.,chan,chan<2?"":"s");
    fprintf(stderr," Output: %d channel%s (",header.channels,header.channels<2?"":"s");
    if(header.nb_coupled>0)fprintf(stderr,"%d coupled",header.nb_coupled*2);
    if(header.nb_streams-header.nb_coupled>0)fprintf(stderr,
       "%s%d uncoupled",header.nb_coupled>0?", ":"",
       header.nb_streams-header.nb_coupled);
    fprintf(stderr,")\n         %0.2gms packets, %0.6gkbit/sec%s\n",
       frame_size/(coding_rate/1000.), bitrate/1000.,
       with_hard_cbr?" CBR":with_cvbr?" CVBR":" VBR");
    fprintf(stderr," Pregap: %d\n",header.preskip);

    if(frange!=NULL)fprintf(stderr,"         Writing final range file %s\n",range_file);
    fprintf(stderr,"\n");
  }

  if(strcmp(outFile,"-")==0){
#if defined WIN32 || defined _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    fout=stdout;
  }else{
    fout=fopen(outFile, "wb");
    if(!fout){
      perror(outFile);
      exit(1);
    }
  }

  /*Write header*/
  {
    unsigned char header_data[100];
    int packet_size=opus_header_to_packet(&header, header_data, 100);
    op.packet=header_data;
    op.bytes=packet_size;
    op.b_o_s=1;
    op.e_o_s=0;
    op.granulepos=0;
    op.packetno=0;
    ogg_stream_packetin(&os, &op);

    while((ret=ogg_stream_flush(&os, &og))){
      if(!ret)break;
      ret=oe_write_page(&og, fout);
      if(ret!=og.header_len+og.body_len){
        fprintf(stderr,"Error: failed writing header to output stream\n");
        exit(1);
      }
      bytes_written+=ret;
      pages_out++;
    }

    op.packet=(unsigned char *)comments;
    op.bytes=comments_length;
    op.b_o_s=0;
    op.e_o_s=0;
    op.granulepos=0;
    op.packetno=1;
    ogg_stream_packetin(&os, &op);
  }

  /* writing the rest of the opus header packets */
  while((ret=ogg_stream_flush(&os, &og))){
    if(!ret)break;
    ret=oe_write_page(&og, fout);
    if(ret!=og.header_len + og.body_len){
      fprintf(stderr,"Error: failed writing header to output stream\n");
      exit(1);
    }
    bytes_written+=ret;
    pages_out++;
  }

  free(comments);

  input=malloc(sizeof(float)*frame_size*chan);
  if(input==NULL){
    fprintf(stderr,"Error: couldn't allocate sample buffer.\n");
    exit(1);
  }

  nb_samples = inopt.read_samples(inopt.readdata,input,frame_size);

  if(nb_samples==0)eos=1;
  total_samples+=nb_samples;
  nb_encoded=-header.preskip;
  /*Main encoding loop (one frame per iteration)*/
  while(!eos){
    id++;
    /*Encode current frame*/

    if(nb_samples<frame_size){
    /*FIXME*/
      //printf("X: %d %d\n",nb_samples,frame_size);
      for(i=nb_samples*chan;i<frame_size*chan;i++)input[i]=0;
    }

    VG_UNDEF(packet,MAX_FRAME_BYTES);
    VG_CHECK(input,sizeof(float)*chan*frame_size);
    nbBytes=opus_multistream_encode_float(st, input, frame_size, packet, MAX_FRAME_BYTES);
    if(nbBytes<0){
      fprintf(stderr, "Encoding failed: %s. Aborting.\n", opus_strerror(nbBytes));
      break;
    }
    VG_CHECK(packet,nbBytes);
    VG_UNDEF(input,sizeof(float)*chan*frame_size);
    nb_encoded+=frame_size;
    total_bytes+=nbBytes;
    peak_bytes=IMAX(nbBytes,peak_bytes);
    min_bytes=IMIN(nbBytes,min_bytes);

    if(frange!=NULL){
      static const char *bw_strings[5]={"NB","MB","WB","SWB","FB"};
      static const char *mode_strings[3]={"LP","HYB","MDCT"};
      int streams=1; /*FIXME, mode data should be per stream too*/
      OpusEncoder *oe;
      opus_uint32 rng;
      fprintf(frange,"%d %d ",frame_size*(48000/coding_rate),nbBytes);
      fprintf(frange,"%s %s %c %d ",mode_strings[((((packet[0]>>3)+48)&92)+4)>>5],
         bw_strings[opus_packet_get_bandwidth(packet)-OPUS_BANDWIDTH_NARROWBAND],
         packet[0]&4?'S':'M',opus_packet_get_samples_per_frame(packet,48000));
      for(i=0;i<streams;i++){
        ret=opus_multistream_encoder_ctl(st,OPUS_MULTISTREAM_GET_ENCODER_STATE(i,&oe));
        ret=opus_encoder_ctl(oe,OPUS_GET_FINAL_RANGE(&rng));
        fprintf(frange,"%llu%c",(unsigned long long)rng,i+1==streams?'\n':' ');
      }
    }

    nb_samples = inopt.read_samples(inopt.readdata,input,frame_size);
    if(nb_samples==0)eos=1;
    if(eos && total_samples<=nb_encoded)op.e_o_s=1;
    else op.e_o_s=0;

    total_samples+=nb_samples;

    op.packet=(unsigned char *)packet;
    op.bytes=nbBytes;
    op.b_o_s=0;
    /*Is this redundent?*/
    if(eos && total_samples<=nb_encoded)op.e_o_s=1;
    else op.e_o_s=0;
    /*FIXME: this doesn't cope with the frame size changing*/
    op.granulepos=(id+1)*frame_size*(48000/coding_rate);
    if(op.granulepos>total_samples)op.granulepos=total_samples*(48000/coding_rate);
    op.packetno=2+id;
    /*printf("granulepos: %d %d %d\n", (int)op.granulepos, op.packetno, op.bytes);*/
    ogg_stream_packetin(&os, &op);

    /*Write all new pages (most likely 0 or 1)
      Flush if we've buffered >max_ogg_delay second to avoid excessive framing delay. */
    while(eos||(op.granulepos-last_granulepos+(frame_size*(48000/coding_rate))>max_ogg_delay)?
#if 0
    /*Libogg > 1.2.2 allows us to achieve lower overhead by
      producing larger pages. For 20ms frames this is only relevant
      above ~32kbit/sec. We still target somewhat smaller than the
      maximum size in order to avoid continued pages.*/
           ogg_stream_flush_fill(&os, &og,255*255-7*1276):
           ogg_stream_pageout_fill(&os, &og,255*255-7*1276))
#else
#define OLD_LIBOGG
           ogg_stream_flush(&os, &og):
           ogg_stream_pageout(&os, &og))
#endif
    {
      if(ogg_page_packets(&og)!=0)last_granulepos=ogg_page_granulepos(&og);
      ret=oe_write_page(&og, fout);
      if(ret!=og.header_len+og.body_len){
         fprintf(stderr,"Error: failed writing header to output stream\n");
         exit(1);
      }
      bytes_written+=ret;
      pages_out++;
    }

    if(!quiet){
      gettimeofday(&stop_time,NULL);
      if(stop_time.tv_sec>last_spin){
        double estbitrate;
        double coded_seconds=nb_encoded/(double)coding_rate;
        double wall_time=(stop_time.tv_sec-start_time.tv_sec)+
          (stop_time.tv_usec-start_time.tv_usec)*1e-06;
        char sbuf[55];
        static const char spinner[]="|/-\\";
        if(!with_hard_cbr){
          double tweight=1./(1+exp(-((coded_seconds/10.)-3.)));
          estbitrate=(total_bytes*8.0/coded_seconds)*tweight+
                      bitrate*(1.-tweight);
        }else estbitrate=nbBytes*8*((double)coding_rate/frame_size);
        for(i=0;i<last_spin_len;i++)fprintf(stderr," ");
        if(inopt.total_samples_per_channel>0 && inopt.total_samples_per_channel<nb_encoded){
          snprintf(sbuf,54,"\r[%c] %02d%% ",spinner[last_spin&3],
          (int)floor(nb_encoded/(double)(inopt.total_samples_per_channel+inopt.skip)*100.));
        }else{
          snprintf(sbuf,54,"\r[%c] ",spinner[last_spin&3]);
        }
        last_spin_len=strlen(sbuf);
        snprintf(sbuf+last_spin_len,54-last_spin_len,
          "%02d:%02d:%02d.%02d %4.3gx realtime, %5.4gkbit/s\r",
          (int)(coded_seconds/3600),(int)(coded_seconds/60)%60,
          (int)(coded_seconds)%60,(int)(coded_seconds*100)%100,
          coded_seconds/wall_time,
          estbitrate/1000.);
        fprintf(stderr,"%s",sbuf);
        last_spin_len=strlen(sbuf);
        last_spin=stop_time.tv_sec;
      }
    }
  }
  gettimeofday(&stop_time,NULL);

  for(i=0;i<last_spin_len;i++)fprintf(stderr," ");
  if(last_spin_len)fprintf(stderr,"\r");

  /*Flush all pages left to be written*/
  while(ogg_stream_flush(&os, &og)){
    ret=oe_write_page(&og, fout);
    if(ret!=og.header_len+og.body_len){
      fprintf(stderr,"Error: failed writing header to output stream\n");
      exit(1);
    }
    bytes_written+=ret;
    pages_out++;
  }

  if(!quiet){
    double coded_seconds=nb_encoded/(double)coding_rate;
    double wall_time=(stop_time.tv_sec-start_time.tv_sec)+
      (stop_time.tv_usec-start_time.tv_usec)*1e-06;
    fprintf(stderr,"Encoding complete\n");
    fprintf(stderr,"-----------------------------------------------------\n");
    fprintf(stderr,"    Encoded:");
    print_time(coded_seconds);
    fprintf(stderr,"\n    Runtime:");
    print_time(wall_time);
    fprintf(stderr,"\n             (%0.4gx realtime)\n",coded_seconds/wall_time);
    fprintf(stderr,"      Wrote: %lld bytes, %d packets, %lld pages\n",total_bytes,id+1,pages_out);
    fprintf(stderr,"    Bitrate: %0.6gkbit/s (without overhead)\n",
            total_bytes*8.0/(coded_seconds)/1000.0);
    fprintf(stderr," Rate range: %0.6gkbit/s to %0.6gkbit/s\n             (%d to %d bytes per packet)\n",
            min_bytes*8*((double)coding_rate/frame_size/1000.),
            peak_bytes*8*((double)coding_rate/frame_size/1000.),min_bytes,peak_bytes);
    fprintf(stderr,"   Overhead: %0.3g%% (container+metadata)\n",(bytes_written-total_bytes)/(double)total_bytes*100.);
#ifdef OLD_LIBOGG
    if(max_ogg_delay>(frame_size*(48000/coding_rate)*4))fprintf(stderr,"   (use libogg 1.2.2 or later for lower overhead)\n");
#endif
    fprintf(stderr,"\n");
  }

  opus_multistream_encoder_destroy(st);
  ogg_stream_clear(&os);
  free(packet);
  free(input);
  if(opt_ctls)free(opt_ctls_ctlval);

  if(rate!=coding_rate)clear_resample(&inopt);
  clear_padder(&inopt);
  in_format->close_func(inopt.readdata);
  if(fin)fclose(fin);
  if(fout)fclose(fout);
  if(frange)fclose(frange);
  return 0;
}

/*
 Comments will be stored in the Vorbis style.
 It is describled in the "Structure" section of
    http://www.xiph.org/ogg/vorbis/doc/v-comment.html

 However, Opus and other non-vorbis formats omit the "framing_bit".

The comment header is decoded as follows:
  1) [vendor_length] = read an unsigned integer of 32 bits
  2) [vendor_string] = read a UTF-8 vector as [vendor_length] octets
  3) [user_comment_list_length] = read an unsigned integer of 32 bits
  4) iterate [user_comment_list_length] times {
     5) [length] = read an unsigned integer of 32 bits
     6) this iteration's user comment = read a UTF-8 vector as [length] octets
     }
  7) done.
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
  int len=8+4+vendor_length+4;
  char *p=(char*)malloc(len);
  if(p==NULL){
    fprintf(stderr, "malloc failed in comment_init()\n");
    exit(1);
  }
  memcpy(p, "OpusTags", 8);
  writeint(p, 8, vendor_length);
  memcpy(p+12, vendor_string, vendor_length);
  writeint(p, 12+vendor_length, user_comment_list_length);
  *length=len;
  *comments=p;
}

void comment_add(char **comments, int* length, char *tag, char *val)
{
  char* p=*comments;
  int vendor_length=readint(p, 8);
  int user_comment_list_length=readint(p, 8+4+vendor_length);
  int tag_len=(tag?strlen(tag):0);
  int val_len=strlen(val);
  int len=(*length)+4+tag_len+val_len;

  p=(char*)realloc(p, len);
  if(p==NULL){
    fprintf(stderr, "realloc failed in comment_add()\n");
    exit(1);
  }

  writeint(p, *length, tag_len+val_len);      /* length of comment */
  if(tag) memcpy(p+*length+4, tag, tag_len);  /* comment */
  memcpy(p+*length+4+tag_len, val, val_len);  /* comment */
  writeint(p, 8+4+vendor_length, user_comment_list_length+1);
  *comments=p;
  *length=len;
}
#undef readint
#undef writeint
