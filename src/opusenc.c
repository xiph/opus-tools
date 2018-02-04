/* Copyright (C)2002-2011 Jean-Marc Valin
   Copyright (C)2007-2013 Xiph.Org Foundation
   Copyright (C)2008-2013 Gregory Maxwell
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
#if (!defined WIN32 && !defined _WIN32) || defined(__MINGW32__)
# include <unistd.h>
# include <time.h>
#endif
#include <math.h>

#ifdef HAVE_INTTYPES_H
# include <inttypes.h>
#endif

#ifdef _MSC_VER
# define snprintf _snprintf
#endif

#if defined WIN32 || defined _WIN32
# include "unicode_support.h"
/* We need the following two to set stdout to binary */
# include <io.h>
# include <fcntl.h>
#else
# define fopen_utf8(_x,_y) fopen((_x),(_y))
# define argc_utf8 argc
# define argv_utf8 argv
#endif

#include <opus.h>
#include <opus_multistream.h>
#include "wav_io.h"

#include "opus_header.h"
#include "encoder.h"
#include "diag_range.h"
#include "cpusupport.h"
#include <opusenc.h>

/* printf format specifier for opus_int64 */
#if !defined opus_int64 && defined PRId64
# define I64FORMAT PRId64
#elif defined WIN32 || defined _WIN32
# define I64FORMAT "I64d"
#else
# define I64FORMAT "lld"
#endif

#ifdef VALGRIND
# include <valgrind/memcheck.h>
# define VG_UNDEF(x,y) VALGRIND_MAKE_MEM_UNDEFINED((x),(y))
# define VG_CHECK(x,y) VALGRIND_CHECK_MEM_IS_DEFINED((x),(y))
#else
# define VG_UNDEF(x,y)
# define VG_CHECK(x,y)
#endif

#define IMIN(a,b) ((a) < (b) ? (a) : (b))   /**< Minimum int value.   */
#define IMAX(a,b) ((a) > (b) ? (a) : (b))   /**< Maximum int value.   */

void opustoolsversion(const char *opusversion)
{
  printf("opusenc %s %s (using %s)\n",PACKAGE_NAME,PACKAGE_VERSION,opusversion);
  printf("Copyright (C) 2008-2017 Xiph.Org Foundation\n");
}

void opustoolsversion_short(const char *opusversion)
{
  opustoolsversion(opusversion);
}

void usage(void)
{
  printf("Usage: opusenc [options] input_file output_file.opus\n");
  printf("\n");
  printf("Encode audio using Opus.\n");
#if defined(HAVE_LIBFLAC)
  printf("The input format can be Wave, AIFF, FLAC, Ogg/FLAC, or raw PCM.\n");
#else
  printf("The input format can be Wave, AIFF, or raw PCM.\n");
#endif
  printf("\ninput_file can be:\n");
  printf("  filename.wav      file\n");
  printf("  -                 stdin\n");
  printf("\noutput_file can be:\n");
  printf("  filename.opus     compressed file\n");
  printf("  -                 stdout\n");
  printf("\nGeneral options:\n");
  printf(" -h, --help         Show this help\n");
  printf(" -V, --version      Show version information\n");
  printf(" --help-picture     Show help on attaching album art\n");
  printf(" --quiet            Enable quiet mode\n");
  printf("\nEncoding options:\n");
  printf(" --bitrate n.nnn    Set target bitrate in kbit/sec (6-256/channel)\n");
  printf(" --vbr              Use variable bitrate encoding (default)\n");
  printf(" --cvbr             Use constrained variable bitrate encoding\n");
  printf(" --hard-cbr         Use hard constant bitrate encoding\n");
  printf(" --comp n           Set encoding complexity (0-10, default: 10 (slowest))\n");
  printf(" --framesize n      Set maximum frame size in milliseconds\n");
  printf("                      (2.5, 5, 10, 20, 40, 60, default: 20)\n");
  printf(" --expect-loss      Set expected packet loss in percent (default: 0)\n");
  printf(" --downmix-mono     Downmix to mono\n");
  printf(" --downmix-stereo   Downmix to stereo (if >2 channels)\n");
  printf(" --max-delay n      Set maximum container delay in milliseconds\n");
  printf("                      (0-1000, default: 1000)\n");
  printf("\nMetadata options:\n");
  printf(" --title title      Set track title\n");
  printf(" --artist artist    Set artist or author, may be used multiple times\n");
  printf(" --album album      Set album or collection\n");
  printf(" --genre genre      Set genre, may be used multiple times\n");
  printf(" --date YYYY-MM-DD  Set date of track (YYYY, YYYY-MM, or YYYY-MM-DD)\n");
  printf(" --comment tag=val  Add the given string as an extra comment\n");
  printf("                      This may be used multiple times\n");
  printf(" --picture file     Attach album art (see --help-picture)\n");
  printf("                      This may be used multiple times\n");
  printf(" --padding n        Reserve n extra bytes for metadata (default: 512)\n");
  printf(" --discard-comments Don't keep metadata when transcoding\n");
  printf(" --discard-pictures Don't keep pictures when transcoding\n");
  printf("\nInput options:\n");
  printf(" --raw              Interpret input as raw PCM data without headers\n");
  printf(" --raw-bits n       Set bits/sample for raw input (default: 16)\n");
  printf(" --raw-rate n       Set sampling rate for raw input (default: 48000)\n");
  printf(" --raw-chan n       Set number of channels for raw input (default: 2)\n");
  printf(" --raw-endianness n 1 for big endian, 0 for little (default: 0)\n");
  printf(" --ignorelength     Ignore the data length in Wave headers\n");
  printf("\nDiagnostic options:\n");
  printf(" --serial n         Force use of a specific stream serial number\n");
  printf(" --save-range file  Save check values for every frame to a file\n");
  printf(" --set-ctl-int x=y  Pass the encoder control x with value y (advanced)\n");
  printf("                      Preface with s: to direct the ctl to multistream s\n");
  printf("                      This may be used multiple times\n");
}

void help_picture(void)
{
  printf("  The --picture option can be used with a FILENAME, naming a JPEG,\n");
  printf("  PNG, or GIF image file, or a more complete SPECIFICATION. The\n");
  printf("  SPECIFICATION is a string whose parts are separated by | (pipe)\n");
  printf("  characters. Some parts may be left empty to invoke default values.\n");
  printf("  A plain FILENAME is just shorthand for \"||||FILENAME\".\n");
  printf("\n");
  printf("  The format of SPECIFICATION is:\n");
  printf("  [TYPE]|[MEDIA-TYPE]|[DESCRIPTION]|[WIDTHxHEIGHTxDEPTH[/COLORS]]|FILENAME\n");
  printf("\n");
  printf("  TYPE is an optional number from one of:\n");
  printf("     0: Other\n");
  printf("     1: 32x32 pixel 'file icon' (PNG only)\n");
  printf("     2: Other file icon\n");
  printf("     3: Cover (front)\n");
  printf("     4: Cover (back)\n");
  printf("     5: Leaflet page\n");
  printf("     6: Media (e.g., label side of a CD)\n");
  printf("     7: Lead artist/lead performer/soloist\n");
  printf("     8: Artist/performer\n");
  printf("     9: Conductor\n");
  printf("    10: Band/Orchestra\n");
  printf("    11: Composer\n");
  printf("    12: Lyricist/text writer\n");
  printf("    13: Recording location\n");
  printf("    14: During recording\n");
  printf("    15: During performance\n");
  printf("    16: Movie/video screen capture\n");
  printf("    17: A bright colored fish\n");
  printf("    18: Illustration\n");
  printf("    19: Band/artist logotype\n");
  printf("    20: Publisher/studio logotype\n");
  printf("\n");
  printf("  The default is 3 (front cover). More than one --picture option can\n");
  printf("  be specified to attach multiple pictures. There may only be one\n");
  printf("  picture each of type 1 and 2 in a file.\n");
  printf("\n");
  printf("  MEDIA-TYPE is optional and is now ignored.\n");
  printf("\n");
  printf("  DESCRIPTION is optional. The default is an empty string.\n");
  printf("\n");
  printf("  The next part specifies the resolution and color information, but\n");
  printf("  is now ignored.\n");
  printf("\n");
  printf("  FILENAME is the path to the picture file to be imported.\n");
}

static inline void print_time(double seconds)
{
  opus_int64 hours, minutes;
  hours=seconds/3600;
  seconds-=hours*3600.;
  minutes=seconds/60;
  seconds-=minutes*60.;
  if(hours)fprintf(stderr," %" I64FORMAT " hour%s%s",hours,hours>1?"s":"",
                   minutes&&!(seconds>0)?" and":"");
  if(minutes)fprintf(stderr,"%s%" I64FORMAT " minute%s%s",hours?", ":" ",minutes,
                     minutes>1?"s":"",!hours&&seconds>0?" and":seconds>0?", and":"");
  if(seconds>0)fprintf(stderr," %0.4g second%s",seconds,seconds!=1?"s":"");
}

typedef struct {
  OggOpusEnc *enc;
  FILE *fout;
  opus_int64 total_bytes;
  opus_int64 bytes_written;
  opus_int64 nb_encoded;
  opus_int64 pages_out;
  opus_int64 packets_out;
  opus_int32 peak_bytes;
  opus_int32 min_bytes;
  opus_int32 last_length;
  FILE *frange;
} EncData;

int write_callback(void *user_data, const unsigned char *ptr, opus_int32 len) {
  EncData *data = (EncData*)user_data;
  data->bytes_written += len;
  data->pages_out++;
  return fwrite(ptr, 1, len, data->fout) != (size_t)len;
}

int close_callback(void *user_data) {
  EncData *obj = (EncData*)user_data;
  int ret = 0;
  if (obj->fout) ret = fclose(obj->fout);
  return ret;
}

int packet_callback(void *user_data, const unsigned char *packet_ptr, opus_int32 packet_len, opus_uint32 flags) {
  EncData *data = (EncData*)user_data;
  int nb_samples = opus_packet_get_nb_samples(packet_ptr, packet_len, 48000);
  if (nb_samples <= 0) return 0;  /* ignore header packets */
  data->total_bytes+=packet_len;
  data->peak_bytes=IMAX(packet_len,data->peak_bytes);
  data->min_bytes=IMIN(packet_len,data->min_bytes);
  data->nb_encoded += nb_samples;
  data->packets_out++;
  data->last_length = packet_len;
  if(data->frange!=NULL){
    int ret;
    opus_uint32 rngs[256];
    OpusEncoder *oe;
    int nb_streams;
    for(nb_streams=0;;nb_streams++){
      ret=ope_encoder_ctl(data->enc,OPUS_MULTISTREAM_GET_ENCODER_STATE(nb_streams,&oe));
      if (ret != 0 || oe == NULL) break;
      ret=opus_encoder_ctl(oe,OPUS_GET_FINAL_RANGE(&rngs[nb_streams]));
    }
    save_range(data->frange,nb_samples,packet_ptr,packet_len,
               rngs,nb_streams);
  }
  (void)flags;
  return 0;
}

int main(int argc, char **argv)
{
  static const input_format raw_format = {NULL, 0, raw_open, wav_close, "raw",N_("RAW file reader")};
  int option_index=0;
  struct option long_options[] =
  {
    {"quiet", no_argument, NULL, 0},
    {"bitrate", required_argument, NULL, 0},
    {"hard-cbr",no_argument,NULL, 0},
    {"vbr",no_argument,NULL, 0},
    {"cvbr",no_argument,NULL, 0},
    {"comp", required_argument, NULL, 0},
    {"complexity", required_argument, NULL, 0},
    {"framesize", required_argument, NULL, 0},
    {"expect-loss", required_argument, NULL, 0},
    {"downmix-mono",no_argument,NULL, 0},
    {"downmix-stereo",no_argument,NULL, 0},
    {"no-downmix",no_argument,NULL, 0},
    {"max-delay", required_argument, NULL, 0},
    {"serial", required_argument, NULL, 0},
    {"save-range", required_argument, NULL, 0},
    {"set-ctl-int", required_argument, NULL, 0},
    {"help", no_argument, NULL, 0},
    {"help-picture", no_argument, NULL, 0},
    {"raw", no_argument, NULL, 0},
    {"raw-bits", required_argument, NULL, 0},
    {"raw-rate", required_argument, NULL, 0},
    {"raw-chan", required_argument, NULL, 0},
    {"raw-endianness", required_argument, NULL, 0},
    {"ignorelength", no_argument, NULL, 0},
    {"rate", required_argument, NULL, 0},
    {"version", no_argument, NULL, 0},
    {"version-short", no_argument, NULL, 0},
    {"comment", required_argument, NULL, 0},
    {"artist", required_argument, NULL, 0},
    {"title", required_argument, NULL, 0},
    {"album", required_argument, NULL, 0},
    {"date", required_argument, NULL, 0},
    {"genre", required_argument, NULL, 0},
    {"picture", required_argument, NULL, 0},
    {"padding", required_argument, NULL, 0},
    {"discard-comments", no_argument, NULL, 0},
    {"discard-pictures", no_argument, NULL, 0},
    {0, 0, 0, 0}
  };
  int i, ret;
  int                cline_size;
  OpusEncCallbacks   callbacks = {write_callback, close_callback};
  OggOpusEnc         *enc;
  EncData            data;
  const char         *opus_version;
  float              *input;
  /*I/O*/
  oe_enc_opt         inopt;
  const input_format *in_format;
  char               *inFile;
  char               *outFile;
  char               *range_file;
  FILE               *fin;
  char               ENCODER_string[1024];
  /*Counters*/
  opus_int64         total_samples=0;
  opus_int32         nb_samples;
  time_t             start_time;
  time_t             stop_time;
  time_t             last_spin=0;
  int                last_spin_len=0;
  /*Settings*/
  int                quiet=0;
  opus_int32         bitrate=-1;
  opus_int32         rate=48000;
  opus_int32         frame_size=960;
  opus_int32         opus_frame_param = OPUS_FRAMESIZE_20_MS;
  int                chan=2;
  int                with_hard_cbr=0;
  int                with_cvbr=0;
  int                expect_loss=0;
  int                complexity=10;
  int                downmix=0;
  int                *opt_ctls_ctlval;
  int                opt_ctls=0;
  int                max_ogg_delay=48000; /*48kHz samples*/
  int                seen_file_icons=0;
  int                comment_padding=512;
  int                serialno;
  opus_int32         lookahead=0;
  int                nb_streams;
  int                nb_coupled;
#ifdef WIN_UNICODE
  int argc_utf8;
  char **argv_utf8;
#endif

  if(query_cpu_support()){
    fprintf(stderr,"\n\n** WARNING: This program was compiled with SSE%s\n",query_cpu_support()>1?"2":"");
    fprintf(stderr,"            but this CPU claims to lack these instructions. **\n\n");
  }

#ifdef WIN_UNICODE
  (void)argc;
  (void)argv;

  init_commandline_arguments_utf8(&argc_utf8, &argv_utf8);
#endif

  opt_ctls_ctlval=NULL;
  range_file=NULL;
  in_format=NULL;
  inopt.channels=chan;
  inopt.rate=rate;
  /* 0 dB gain is recommended unless you know what you're doing */
  inopt.gain=0;
  inopt.samplesize=16;
  inopt.endianness=0;
  inopt.rawmode=0;
  inopt.ignorelength=0;
  inopt.copy_comments=1;
  inopt.copy_pictures=1;

  start_time = time(NULL);
  srand(((getpid()&65535)<<15)^start_time);
  serialno=rand();

  inopt.comments = ope_comments_create();
  opus_version=opus_get_version_string();
  /*Vendor string should just be the encoder library,
    the ENCODER comment specifies the tool used.*/
  snprintf(ENCODER_string, sizeof(ENCODER_string), "opusenc from %s %s",PACKAGE_NAME,PACKAGE_VERSION);
  ope_comments_add(inopt.comments, "ENCODER", ENCODER_string);

  /*Process command-line options*/
  cline_size=0;
  data.frange = NULL;
  while(1){
    int c;
    int save_cmd=1;
    c=getopt_long(argc_utf8, argv_utf8, "hV",
                  long_options, &option_index);
    if(c==-1)
       break;

    switch(c){
      case 0:
        if(strcmp(long_options[option_index].name,"quiet")==0){
          quiet=1;
          save_cmd=0;
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
        }else if(strcmp(long_options[option_index].name,"help-picture")==0){
          help_picture();
          exit(0);
        }else if(strcmp(long_options[option_index].name,"version")==0){
          opustoolsversion(opus_version);
          exit(0);
        }else if(strcmp(long_options[option_index].name,"version-short")==0){
          opustoolsversion_short(opus_version);
          exit(0);
        }else if(strcmp(long_options[option_index].name,"ignorelength")==0){
          inopt.ignorelength=1;
          save_cmd=0;
        }else if(strcmp(long_options[option_index].name,"raw")==0){
          inopt.rawmode=1;
          save_cmd=0;
        }else if(strcmp(long_options[option_index].name,"raw-bits")==0){
          inopt.rawmode=1;
          inopt.samplesize=atoi(optarg);
          save_cmd=0;
          if(inopt.samplesize!=8&&inopt.samplesize!=16&&inopt.samplesize!=24){
            fprintf(stderr,"Invalid bit-depth: %s\n",optarg);
            fprintf(stderr,"--raw-bits must be one of 8,16, or 24\n");
            exit(1);
          }
        }else if(strcmp(long_options[option_index].name,"raw-rate")==0){
          inopt.rawmode=1;
          inopt.rate=atoi(optarg);
          save_cmd=0;
        }else if(strcmp(long_options[option_index].name,"raw-chan")==0){
          inopt.rawmode=1;
          inopt.channels=atoi(optarg);
          save_cmd=0;
        }else if(strcmp(long_options[option_index].name,"raw-endianness")==0){
          inopt.rawmode=1;
          inopt.endianness=atoi(optarg);
          save_cmd=0;
        }else if(strcmp(long_options[option_index].name,"downmix-mono")==0){
          downmix=1;
        }else if(strcmp(long_options[option_index].name,"downmix-stereo")==0){
          downmix=2;
        }else if(strcmp(long_options[option_index].name,"no-downmix")==0){
          downmix=-1;
        }else if(strcmp(long_options[option_index].name,"expect-loss")==0){
          expect_loss=atoi(optarg);
          if(expect_loss>100||expect_loss<0){
            fprintf(stderr,"Invalid expect-loss: %s\n",optarg);
            fprintf(stderr,"Expected loss is a percent and must be 0-100.\n");
            exit(1);
          }
        }else if(strcmp(long_options[option_index].name,"comp")==0 ||
                 strcmp(long_options[option_index].name,"complexity")==0){
          complexity=atoi(optarg);
          if(complexity>10||complexity<0){
            fprintf(stderr,"Invalid complexity: %s\n",optarg);
            fprintf(stderr,"Complexity must be 0-10.\n");
            exit(1);
          }
        }else if(strcmp(long_options[option_index].name,"framesize")==0){
          if(strcmp(optarg,"2.5")==0)opus_frame_param=OPUS_FRAMESIZE_2_5_MS;
          else if(strcmp(optarg,"5")==0)opus_frame_param=OPUS_FRAMESIZE_5_MS;
          else if(strcmp(optarg,"10")==0)opus_frame_param=OPUS_FRAMESIZE_10_MS;
          else if(strcmp(optarg,"20")==0)opus_frame_param=OPUS_FRAMESIZE_20_MS;
          else if(strcmp(optarg,"40")==0)opus_frame_param=OPUS_FRAMESIZE_40_MS;
          else if(strcmp(optarg,"60")==0)opus_frame_param=OPUS_FRAMESIZE_60_MS;
          else{
            fprintf(stderr,"Invalid framesize: %s\n",optarg);
            fprintf(stderr,"Framesize must be 2.5, 5, 10, 20, 40, or 60.\n");
            exit(1);
          }

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
        }else if(strcmp(long_options[option_index].name,"max-delay")==0){
          max_ogg_delay=floor(atof(optarg)*48.);
          if(max_ogg_delay<0||max_ogg_delay>48000){
            fprintf(stderr,"Invalid max-delay: %s\n",optarg);
            fprintf(stderr,"max-delay 0-1000 ms.\n");
            exit(1);
          }
        }else if(strcmp(long_options[option_index].name,"serial")==0){
          serialno=atoi(optarg);
        }else if(strcmp(long_options[option_index].name,"set-ctl-int")==0){
          int len=strlen(optarg),target;
          char *spos,*tpos;
          spos=strchr(optarg,'=');
          if(len<3||spos==NULL||(spos-optarg)<1||(spos-optarg)>=len){
            fprintf(stderr, "Invalid set-ctl-int: %s\n", optarg);
            fprintf(stderr, "Syntax is --set-ctl-int intX=intY or\n");
            fprintf(stderr, "Syntax is --set-ctl-int intS:intX=intY\n");
            exit(1);
          }
          tpos=strchr(optarg,':');
          if(tpos==NULL){
            target=-1;
            tpos=optarg-1;
          }else target=atoi(optarg);
          if((atoi(tpos+1)&1)!=0){
            fprintf(stderr, "Invalid set-ctl-int: %s\n", optarg);
            fprintf(stderr, "libopus set CTL values are even.\n");
            exit(1);
          }
          if(opt_ctls==0)opt_ctls_ctlval=malloc(sizeof(int)*3);
          else opt_ctls_ctlval=realloc(opt_ctls_ctlval,sizeof(int)*(opt_ctls+1)*3);
          if(!opt_ctls_ctlval)
          {
            fprintf(stderr, "Memory allocation failure.\n");
            exit(1);
          }
          opt_ctls_ctlval[opt_ctls*3]=target;
          opt_ctls_ctlval[opt_ctls*3+1]=atoi(tpos+1);
          opt_ctls_ctlval[opt_ctls*3+2]=atoi(spos+1);
          opt_ctls++;
        }else if(strcmp(long_options[option_index].name,"save-range")==0){
          data.frange=fopen_utf8(optarg,"w");
          save_cmd=0;
          if(data.frange==NULL){
            perror(optarg);
            fprintf(stderr,"Could not open save-range file: %s\n",optarg);
            fprintf(stderr,"Must provide a writable file name.\n");
            exit(1);
          }
          range_file=optarg;
        }else if(strcmp(long_options[option_index].name,"comment")==0){
          save_cmd=0;
          if(!strchr(optarg,'=')){
            fprintf(stderr, "Invalid comment: %s\n", optarg);
            fprintf(stderr, "Comments must be of the form name=value\n");
            exit(1);
          }
          ope_comments_add_string(inopt.comments, optarg);
        }else if(strcmp(long_options[option_index].name,"artist")==0){
          save_cmd=0;
          ope_comments_add(inopt.comments, "artist", optarg);
        } else if(strcmp(long_options[option_index].name,"title")==0){
          save_cmd=0;
          ope_comments_add(inopt.comments, "title", optarg);
        } else if(strcmp(long_options[option_index].name,"album")==0){
          save_cmd=0;
          ope_comments_add(inopt.comments, "album", optarg);
        } else if(strcmp(long_options[option_index].name,"date")==0){
          save_cmd=0;
          ope_comments_add(inopt.comments, "date", optarg);
        } else if(strcmp(long_options[option_index].name,"genre")==0){
          save_cmd=0;
          ope_comments_add(inopt.comments, "genre", optarg);
        } else if(strcmp(long_options[option_index].name,"picture")==0){
          const char    *media_type;
          const char    *media_type_end;
          const char    *description;
          const char    *description_end;
          const char    *filename;
          const char    *spec;
          char *description_copy;
          FILE *picture_file;
          int picture_type;
          save_cmd=0;
          spec = optarg;
          picture_type=3;
          media_type=media_type_end=description=description_end=filename=spec;
          picture_file=fopen_utf8(filename,"rb");
          description_copy=NULL;
          if(picture_file==NULL&&strchr(spec,'|')){
            const char *p;
            char       *q;
            unsigned long val;
            /*We don't have a plain file, and there is a pipe character: assume it's
              the full form of the specification.*/
            val=strtoul(spec,&q,10);
            if(*q!='|'||val>20){
              fprintf(stderr, "Invalid picture type: %.*s\n",
                (int)strcspn(spec,"|"), spec);
              exit(1);
            }
            /*An empty field implies a default of 'Cover (front)'.*/
            if(spec!=q)picture_type=val;
            media_type=q+1;
            media_type_end=media_type+strcspn(media_type,"|");
            if(*media_type_end=='|'){
              description=media_type_end+1;
              description_end=description+strcspn(description,"|");
              if(*description_end=='|'){
                p=description_end+1;
                /*Ignore WIDTHxHEIGHTxDEPTH/COLORS.*/
                p+=strcspn(p,"|");
                if(*p=='|'){
                  filename=p+1;
                }
              }
            }
            if (filename==spec) {
              fprintf(stderr, "Not enough fields in picture specification: %s\n", spec);
              exit(1);
            }
            if (media_type_end-media_type==3 && strncmp("-->",media_type,3)==0) {
              fprintf(stderr, "Picture URLs are no longer supported.\n");
              exit(1);
            }
            if (picture_type>=1&&picture_type<=2&&(seen_file_icons&picture_type)) {
              fprintf(stderr, "Only one picture of type %d (%s) is allowed.\n",
                picture_type, picture_type==1 ? "32x32 icon" : "icon");
              exit(1);
            }
          }
          if (picture_file) fclose(picture_file);
          if (description_end-description != 0) {
            size_t len = description_end-description;
            description_copy = malloc(len+1);
            memcpy(description_copy, description, len);
            description_copy[len]=0;
          }
          ret = ope_comments_add_picture(inopt.comments, filename, picture_type, description_copy);
          if (ret != OPE_OK) {
            fprintf(stderr, "Error: %s: %s\n", ope_strerror(ret), filename);
            exit(1);
          }
          if (description_copy) free(description_copy);
          if (picture_type>=1&&picture_type<=2) seen_file_icons|=picture_type;
        } else if(strcmp(long_options[option_index].name,"padding")==0){
          comment_padding=atoi(optarg);
        } else if(strcmp(long_options[option_index].name,"discard-comments")==0){
          inopt.copy_comments=0;
          inopt.copy_pictures=0;
        } else if(strcmp(long_options[option_index].name,"discard-pictures")==0){
          inopt.copy_pictures=0;
        }
        /*Options whose arguments would leak file paths or just end up as
           metadata, or that relate only to input file handling or console
           output, should have save_cmd=0; to prevent them from being saved
           in the ENCODER_OPTIONS tag.*/
        break;
      case 'h':
        usage();
        exit(0);
        break;
      case 'V':
        opustoolsversion(opus_version);
        exit(0);
        break;
      case '?':
        usage();
        exit(1);
        break;
    }
    if(save_cmd && cline_size<(int)sizeof(ENCODER_string)){
      ret=snprintf(&ENCODER_string[cline_size], sizeof(ENCODER_string)-cline_size, "%s--%s",cline_size==0?"":" ",long_options[option_index].name);
      if(ret<0||ret>=((int)sizeof(ENCODER_string)-cline_size)){
        cline_size=sizeof(ENCODER_string);
      } else {
        cline_size+=ret;
        if(optarg){
          ret=snprintf(&ENCODER_string[cline_size], sizeof(ENCODER_string)-cline_size, " %s",optarg);
          if(ret<0||ret>=((int)sizeof(ENCODER_string)-cline_size)){
            cline_size=sizeof(ENCODER_string);
          } else {
            cline_size+=ret;
          }
        }
      }
    }
  }
  if(argc_utf8-optind!=2){
    usage();
    exit(1);
  }
  inFile=argv_utf8[optind];
  outFile=argv_utf8[optind+1];

  if(cline_size>0)ope_comments_add(inopt.comments, "ENCODER_OPTIONS", ENCODER_string);

  if(strcmp(inFile, "-")==0){
#if defined WIN32 || defined _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#elif defined OS2
    _fsetmode(stdin,"b");
#endif
    fin=stdin;
  }else{
    fin=fopen_utf8(inFile, "rb");
    if(!fin){
      perror(inFile);
      exit(1);
    }
  }

  if(inopt.rawmode){
    in_format = &raw_format;
    in_format->open_func(fin, &inopt, NULL, 0);
  }else in_format=open_audio_file(fin,&inopt);

  if(!in_format){
    fprintf(stderr,"Error parsing input file: %s\n",inFile);
    exit(1);
  }

  if(inopt.rate<100||inopt.rate>768000){
    /*Crazy rates excluded to avoid excessive memory usage for padding/resampling.*/
    fprintf(stderr,"Error parsing input file: %s unhandled sampling rate: %ld Hz\n",inFile,inopt.rate);
    exit(1);
  }

  if(inopt.channels>255||inopt.channels<1){
    fprintf(stderr,"Error parsing input file: %s unhandled number of channels: %d\n",inFile,inopt.channels);
    exit(1);
  }

  if(downmix==0&&inopt.channels>2&&bitrate>0&&bitrate<(16000*inopt.channels)){
    if(!quiet)fprintf(stderr,"Notice: Surround bitrate less than 16kbit/sec/channel, downmixing.\n");
    downmix=inopt.channels>8?1:2;
  }

  if(downmix>0&&downmix<inopt.channels)downmix=setup_downmix(&inopt,downmix);
  else downmix=0;

  rate=inopt.rate;
  chan=inopt.channels;

  if(inopt.total_samples_per_channel && rate!=48000)
    inopt.total_samples_per_channel = (opus_int64)
      ((double)inopt.total_samples_per_channel * (48000./(double)rate));

  /*Initialize Opus encoder*/
  enc = ope_encoder_create_callbacks(&callbacks, &data, inopt.comments, rate, chan, chan>8?255:chan>2, NULL);
  ope_encoder_ctl(enc, OPE_SET_MUXING_DELAY(max_ogg_delay));
  ope_encoder_ctl(enc, OPE_SET_SERIALNO(serialno));
  ope_encoder_ctl(enc, OPE_SET_PACKET_CALLBACK(packet_callback, &data));
  ope_encoder_ctl(enc, OPUS_SET_EXPERT_FRAME_DURATION(opus_frame_param));
  ope_encoder_ctl(enc, OPE_SET_COMMENT_PADDING(comment_padding));
  for(nb_streams=0;;nb_streams++){
    OpusEncoder *oe;
    ret=ope_encoder_ctl(enc,OPUS_MULTISTREAM_GET_ENCODER_STATE(nb_streams,&oe));
    if (ret != 0 || oe == NULL) break;
  }
  nb_coupled = chan - nb_streams;

  if(bitrate<0){
    /*Lower default rate for sampling rates [8000-44100) by a factor of (rate+16k)/(64k)*/
    bitrate=((64000*nb_streams+32000*nb_coupled)*
             (IMIN(48,IMAX(8,((rate<44100?rate:48000)+1000)/1000))+16)+32)>>6;
  }

  if(bitrate>(1024000*chan)||bitrate<500){
    fprintf(stderr,"Error: Bitrate %d bits/sec is insane.\nDid you mistake bits for kilobits?\n",bitrate);
    fprintf(stderr,"--bitrate values from 6-256 kbit/sec per channel are meaningful.\n");
    exit(1);
  }
  bitrate=IMIN(chan*256000,bitrate);

  ret = ope_encoder_ctl(enc, OPUS_SET_BITRATE(bitrate));
  if(ret!=OPUS_OK){
    fprintf(stderr,"Error OPUS_SET_BITRATE returned: %s\n",opus_strerror(ret));
    exit(1);
  }

  ret = ope_encoder_ctl(enc, OPUS_SET_VBR(!with_hard_cbr));
  if(ret!=OPUS_OK){
    fprintf(stderr,"Error OPUS_SET_VBR returned: %s\n",opus_strerror(ret));
    exit(1);
  }

  if(!with_hard_cbr){
    ret = ope_encoder_ctl(enc, OPUS_SET_VBR_CONSTRAINT(with_cvbr));
    if(ret!=OPUS_OK){
      fprintf(stderr,"Error OPUS_SET_VBR_CONSTRAINT returned: %s\n",opus_strerror(ret));
      exit(1);
    }
  }

  ret = ope_encoder_ctl(enc, OPUS_SET_COMPLEXITY(complexity));
  if(ret!=OPUS_OK){
    fprintf(stderr,"Error OPUS_SET_COMPLEXITY returned: %s\n",opus_strerror(ret));
    exit(1);
  }

  ret = ope_encoder_ctl(enc, OPUS_SET_PACKET_LOSS_PERC(expect_loss));
  if(ret!=OPUS_OK){
    fprintf(stderr,"Error OPUS_SET_PACKET_LOSS_PERC returned: %s\n",opus_strerror(ret));
    exit(1);
  }

#ifdef OPUS_SET_LSB_DEPTH
  ret = ope_encoder_ctl(enc, OPUS_SET_LSB_DEPTH(IMAX(8,IMIN(24,inopt.samplesize))));
  if(ret!=OPUS_OK){
    fprintf(stderr,"Warning OPUS_SET_LSB_DEPTH returned: %s\n",opus_strerror(ret));
  }
#endif

  /*This should be the last set of CTLs, except the lookahead get, so it can override the defaults.*/
  for(i=0;i<opt_ctls;i++){
    int target=opt_ctls_ctlval[i*3];
    if(target==-1){
      ret = ope_encoder_ctl(enc, opt_ctls_ctlval[i*3+1],opt_ctls_ctlval[i*3+2]);
      if(ret!=OPUS_OK){
        fprintf(stderr,"Error opus_multistream_encoder_ctl(st,%d,%d) returned: %s\n",opt_ctls_ctlval[i*3+1],opt_ctls_ctlval[i*3+2],opus_strerror(ret));
        exit(1);
      }
    }else if(target<nb_streams){
      OpusEncoder *oe;
      ope_encoder_ctl(enc, OPUS_MULTISTREAM_GET_ENCODER_STATE(target,&oe));
      ret=opus_encoder_ctl(oe, opt_ctls_ctlval[i*3+1],opt_ctls_ctlval[i*3+2]);
      if(ret!=OPUS_OK){
        fprintf(stderr,"Error opus_encoder_ctl(st[%d],%d,%d) returned: %s\n",target,opt_ctls_ctlval[i*3+1],opt_ctls_ctlval[i*3+2],opus_strerror(ret));
        exit(1);
      }
    }else{
      fprintf(stderr,"Error --set-ctl-int target stream %d is higher than the maximum stream number %d.\n",target,nb_streams-1);
      exit(1);
    }
  }

  /*We do the lookahead check late so user CTLs can change it*/
  ret = ope_encoder_ctl(enc, OPUS_GET_LOOKAHEAD(&lookahead));
  if(ret!=OPUS_OK){
    fprintf(stderr,"Error OPUS_GET_LOOKAHEAD returned: %s\n",opus_strerror(ret));
    exit(1);
  }

  if(!quiet){
    int opus_app;
    fprintf(stderr,"Encoding using %s",opus_version);
    ope_encoder_ctl(enc, OPUS_GET_APPLICATION(&opus_app));
    if(opus_app==OPUS_APPLICATION_VOIP)fprintf(stderr," (VoIP)\n");
    else if(opus_app==OPUS_APPLICATION_AUDIO)fprintf(stderr," (audio)\n");
    else if(opus_app==OPUS_APPLICATION_RESTRICTED_LOWDELAY)fprintf(stderr," (low-delay)\n");
    else fprintf(stderr," (unknown)\n");
    fprintf(stderr,"-----------------------------------------------------\n");
    fprintf(stderr,"   Input: %0.6gkHz %d channel%s\n",
            rate/1000.,chan,chan<2?"":"s");
    fprintf(stderr,"  Output: %d channel%s (",chan,chan<2?"":"s");
    if(nb_coupled>0)fprintf(stderr,"%d coupled",nb_coupled*2);
    if(nb_streams-nb_coupled>0)fprintf(stderr,
       "%s%d uncoupled",nb_coupled>0?", ":"",
       nb_streams-nb_coupled);
    fprintf(stderr,")\n          %0.2gms packets, %0.6gkbit/sec%s\n",
       frame_size/(48000/1000.), bitrate/1000.,
       with_hard_cbr?" CBR":with_cvbr?" CVBR":" VBR");
    fprintf(stderr," Preskip: %d\n",lookahead);

    if(data.frange!=NULL)fprintf(stderr,"         Writing final range file %s\n",range_file);
    fprintf(stderr,"\n");
  }

  if(strcmp(outFile,"-")==0){
#if defined WIN32 || defined _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    data.fout=stdout;
  }else{
    data.fout=fopen_utf8(outFile, "wb");
    if(!data.fout){
      perror(outFile);
      exit(1);
    }
  }
  data.enc = enc;
  data.total_bytes = 0;
  data.bytes_written = 0;
  data.nb_encoded = 0;
  data.packets_out = 0;
  data.peak_bytes = 0;
  data.min_bytes = 256*1275*6;
  data.pages_out = 0;
  data.last_length = 0;

  input=malloc(sizeof(float)*frame_size*chan);
  if(input==NULL){
    fprintf(stderr,"Error: couldn't allocate sample buffer.\n");
    exit(1);
  }

  /*Main encoding loop (one frame per iteration)*/
  while(1){
    nb_samples = inopt.read_samples(inopt.readdata,input,frame_size);
    total_samples+=nb_samples;
    ope_encoder_write_float(enc, input, nb_samples);
    if (nb_samples < frame_size) break;

    if(!quiet){
      stop_time = time(NULL);
      if(stop_time>last_spin){
        double estbitrate;
        double coded_seconds=data.nb_encoded/48000.;
        double wall_time=(stop_time-start_time)+1e-6;
        char sbuf[55];
        static const char spinner[]="|/-\\";
        if(with_hard_cbr){
          estbitrate=data.last_length*(8*48000./frame_size);
        }else if(data.nb_encoded<=0){
          estbitrate=0;
        }else{
          double tweight=1./(1+exp(-((coded_seconds/10.)-3.)));
          estbitrate=(data.total_bytes*8.0/coded_seconds)*tweight+
                      bitrate*(1.-tweight);
        }
        fprintf(stderr,"\r");
        for(i=0;i<last_spin_len;i++)fprintf(stderr," ");
        if(inopt.total_samples_per_channel>0 && data.nb_encoded<inopt.total_samples_per_channel){
          snprintf(sbuf,54,"\r[%c] %2d%% ",spinner[last_spin&3],
          (int)floor(data.nb_encoded/(double)(inopt.total_samples_per_channel+lookahead)*100.));
        }else{
          snprintf(sbuf,54,"\r[%c] ",spinner[last_spin&3]);
        }
        last_spin_len=strlen(sbuf);
        snprintf(sbuf+last_spin_len,54-last_spin_len,
          "%02d:%02d:%02d.%02d %4.3gx realtime, %5.4gkbit/s",
          (int)(coded_seconds/3600),(int)(coded_seconds/60)%60,
          (int)(coded_seconds)%60,(int)(coded_seconds*100)%100,
          coded_seconds/wall_time,
          estbitrate/1000.);
        fprintf(stderr,"%s",sbuf);
        fflush(stderr);
        last_spin_len=strlen(sbuf);
        last_spin=stop_time;
      }
    }
  }

  ope_encoder_drain(enc);
  stop_time = time(NULL);

  if(last_spin_len)fprintf(stderr,"\r");
  for(i=0;i<last_spin_len;i++)fprintf(stderr," ");
  if(last_spin_len)fprintf(stderr,"\r");

  if(!quiet){
    double coded_seconds=data.nb_encoded/48000.;
    double wall_time=(stop_time-start_time)+1e-6;
    fprintf(stderr,"Encoding complete\n");
    fprintf(stderr,"-----------------------------------------------------\n");
    fprintf(stderr,"       Encoded:");
    print_time(coded_seconds);
    fprintf(stderr,"\n       Runtime:");
    print_time(wall_time);
    fprintf(stderr,"\n                (%0.4gx realtime)\n",coded_seconds/wall_time);
    fprintf(stderr,"         Wrote: %" I64FORMAT " bytes, %" I64FORMAT " packets, %" I64FORMAT " pages\n",data.bytes_written,data.packets_out,data.pages_out);
    if(data.nb_encoded>0){
      fprintf(stderr,"       Bitrate: %0.6gkbit/s (without overhead)\n",
              data.total_bytes*8.0/(coded_seconds)/1000.0);
      fprintf(stderr," Instant rates: %0.6gkbit/s to %0.6gkbit/s\n                (%d to %d bytes per packet)\n",
              data.min_bytes*(8*48000./frame_size/1000.),
              data.peak_bytes*(8*48000./frame_size/1000.),data.min_bytes,data.peak_bytes);
    }
    if(data.bytes_written>0){
      fprintf(stderr,"      Overhead: %0.3g%% (container+metadata)\n",(data.bytes_written-data.total_bytes)/(double)data.bytes_written*100.);
    }
    fprintf(stderr,"\n");
  }

  ope_encoder_destroy(enc);
  ope_comments_destroy(inopt.comments);
  free(input);
  if(opt_ctls)free(opt_ctls_ctlval);

  if(downmix)clear_downmix(&inopt);
  in_format->close_func(inopt.readdata);
  if(fin)fclose(fin);
  if(data.frange)fclose(data.frange);
#ifdef WIN_UNICODE
  free_commandline_arguments_utf8(&argc_utf8, &argv_utf8);
#endif
  return 0;
}
