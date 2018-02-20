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
#include <unistd.h>
#include <time.h>
#endif
#include <math.h>

#ifdef _MSC_VER
#define snprintf _snprintf
#endif

#if defined WIN32 || defined _WIN32 || defined WIN64 || defined _WIN64
# include "unicode_support.h"
/* We need the following two to set stdout to binary */
# include <io.h>
# include <fcntl.h>
# define I64FORMAT "I64d"
#else
# define I64FORMAT "lld"
# define fopen_utf8(_x,_y) fopen((_x),(_y))
# define argc_utf8 argc
# define argv_utf8 argv
#endif

#include <opus.h>
#include <opus_multistream.h>
#include <ogg/ogg.h>
#include "wav_io.h"

#include "picture.h"
#include "opus_header.h"
#include "opusenc.h"
#include "diag_range.h"
#include "cpusupport.h"

#ifdef VALGRIND
#include <valgrind/memcheck.h>
#define VG_UNDEF(x,y) VALGRIND_MAKE_MEM_UNDEFINED((x),(y))
#define VG_CHECK(x,y) VALGRIND_CHECK_MEM_IS_DEFINED((x),(y))
#else
#define VG_UNDEF(x,y)
#define VG_CHECK(x,y)
#endif

static void comment_init(char **comments, int* length, const char *vendor_string);
static void comment_pad(char **comments, int* length, int amount);

/*Write an Ogg page to a file pointer*/
static inline int oe_write_page(ogg_page *page, FILE *fp)
{
   int written;
   written=fwrite(page->header,1,page->header_len, fp);
   written+=fwrite(page->body,1,page->body_len, fp);
   return written;
}

#define MAX_FRAME_BYTES 61295
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
  printf(" --no-surround      Disable surround sound encoding\n");
  printf(" --coupled 1:2,4:5  Specify coupled input channel pairs (only for --no-surround)\n");
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
  printf("  MEDIA-TYPE is optional. If left blank, it will be detected from the\n");
  printf("  file. For best compatibility with players, use pictures with a\n");
  printf("  MEDIA-TYPE of image/jpeg or image/png. The MEDIA-TYPE can also be\n");
  printf("  \"-->\" to mean that FILENAME is actually a URL to an image, though\n");
  printf("  this use is discouraged. The file at the URL will not be fetched.\n");
  printf("  The URL itself is stored in the metadata.\n");
  printf("\n");
  printf("  DESCRIPTION is optional. The default is an empty string.\n");
  printf("\n");
  printf("  The next part specifies the resolution and color information. If\n");
  printf("  the MEDIA-TYPE is image/jpeg, image/png, or image/gif, this can\n");
  printf("  usually be left empty and the information will be read from the\n");
  printf("  file.  Otherwise, you must specify the width in pixels, height in\n");
  printf("  pixels, and color depth in bits-per-pixel. If the image has indexed\n");
  printf("  colors you should also specify the number of colors used. If possible,\n");
  printf("  these are checked against the file for accuracy.\n");
  printf("\n");
  printf("  FILENAME is the path to the picture file to be imported, or the URL\n");
  printf("  if the MEDIA-TYPE is \"-->\".\n");
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
    {"no-surround", no_argument, NULL, 0},
    {"coupled", required_argument, NULL, 0},
    {0, 0, 0, 0}
  };
  int i, ret;
  int                cline_size;
  OpusMSEncoder      *st;
  const char         *opus_version;
  unsigned char      *packet;
  float              *input;
  /*I/O*/
  oe_enc_opt         inopt;
  const input_format *in_format;
  char               *inFile;
  char               *outFile;
  char               *range_file;
  FILE               *fin;
  FILE               *fout;
  FILE               *frange;
  ogg_stream_state   os;
  ogg_page           og;
  ogg_packet         op;
  ogg_int64_t        last_granulepos=0;
  ogg_int64_t        enc_granulepos=0;
  ogg_int64_t        original_samples=0;
  ogg_int32_t        id=-1;
  int                last_segments=0;
  OpusHeader         header;
  char               ENCODER_string[1024];
  /*Counters*/
  opus_int64         nb_encoded=0;
  opus_int64         bytes_written=0;
  opus_int64         pages_out=0;
  opus_int64         total_bytes=0;
  opus_int64         total_samples=0;
  opus_int32         nbBytes;
  opus_int32         nb_samples;
  opus_int32         peak_bytes=0;
  opus_int32         min_bytes;
  time_t             start_time;
  time_t             stop_time;
  time_t             last_spin=0;
  int                last_spin_len=0;
  /*Settings*/
  int                quiet=0;
  int                max_frame_bytes;
  opus_int32         bitrate=-1;
  opus_int32         rate=48000;
  opus_int32         coding_rate=48000;
  opus_int32         frame_size=960;
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
  char               *coupling=NULL;

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
  frange=NULL;
  range_file=NULL;
  in_format=NULL;
  inopt.channels=chan;
  inopt.rate=coding_rate=rate;
  /* 0 dB gain is recommended unless you know what you're doing */
  inopt.gain=0;
  inopt.samplesize=16;
  inopt.endianness=0;
  inopt.rawmode=0;
  inopt.ignorelength=0;
  inopt.copy_comments=1;
  inopt.copy_pictures=1;
  inopt.no_surround=0;

  start_time = time(NULL);
  srand(((getpid()&65535)<<15)^start_time);
  serialno=rand();

  opus_version=opus_get_version_string();
  /*Vendor string should just be the encoder library,
    the ENCODER comment specifies the tool used.*/
  comment_init(&inopt.comments, &inopt.comments_length, opus_version);
  snprintf(ENCODER_string, sizeof(ENCODER_string), "opusenc from %s %s",PACKAGE_NAME,PACKAGE_VERSION);
  comment_add(&inopt.comments, &inopt.comments_length, "ENCODER", ENCODER_string);

  /*Process command-line options*/
  cline_size=0;
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
        }else if(strcmp(long_options[option_index].name,"no-surround")==0){
          inopt.no_surround=1;
        }else if(strcmp(long_options[option_index].name,"coupled")==0){
          /* store for later so we can directly parse it into `header.stream_map` */
          coupling=optarg;
        }else if(strcmp(long_options[option_index].name,"comp")==0 ||
                 strcmp(long_options[option_index].name,"complexity")==0){
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
          frange=fopen_utf8(optarg,"w");
          save_cmd=0;
          if(frange==NULL){
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
          comment_add(&inopt.comments, &inopt.comments_length, NULL, optarg);
        }else if(strcmp(long_options[option_index].name,"artist")==0){
          save_cmd=0;
          comment_add(&inopt.comments, &inopt.comments_length, "artist", optarg);
        } else if(strcmp(long_options[option_index].name,"title")==0){
          save_cmd=0;
          comment_add(&inopt.comments, &inopt.comments_length, "title", optarg);
        } else if(strcmp(long_options[option_index].name,"album")==0){
          save_cmd=0;
          comment_add(&inopt.comments, &inopt.comments_length, "album", optarg);
        } else if(strcmp(long_options[option_index].name,"date")==0){
          save_cmd=0;
          comment_add(&inopt.comments, &inopt.comments_length, "date", optarg);
        } else if(strcmp(long_options[option_index].name,"genre")==0){
          save_cmd=0;
          comment_add(&inopt.comments, &inopt.comments_length, "genre", optarg);
        } else if(strcmp(long_options[option_index].name,"picture")==0){
          const char *error_message;
          char       *picture_data;
          save_cmd=0;
          picture_data=parse_picture_specification(optarg,&error_message,
                                                   &seen_file_icons);
          if(picture_data==NULL){
            fprintf(stderr,"Error parsing picture option: %s\n",error_message);
            exit(1);
          }
          comment_add(&inopt.comments,&inopt.comments_length,
                      "METADATA_BLOCK_PICTURE",picture_data);
          free(picture_data);
        } else if(strcmp(long_options[option_index].name,"padding")==0){
          comment_padding=atoi(optarg);
        } else if(strcmp(long_options[option_index].name,"discard-comments")==0){
          inopt.copy_comments=0;
          inopt.copy_pictures=0;
        } else if(strcmp(long_options[option_index].name,"discard-pictures")==0){
          inopt.copy_pictures=0;
        }
        /*Commands whose arguments would leak file paths or just end up as metadata
           should have save_cmd=0; to prevent them from being saved in the
           command-line tag.*/
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

  if(cline_size>0)comment_add(&inopt.comments, &inopt.comments_length, "ENCODER_OPTIONS", ENCODER_string);

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
  inopt.skip=0;

  /*In order to code the complete length we'll need to do a little padding*/
  setup_padder(&inopt,&original_samples);

  if(rate>24000)coding_rate=48000;
  else if(rate>16000)coding_rate=24000;
  else if(rate>12000)coding_rate=16000;
  else if(rate>8000)coding_rate=12000;
  else coding_rate=8000;

  frame_size=frame_size/(48000/coding_rate);

  /*Scale the resampler complexity, but only for 48000 output because
    the near-cutoff behavior matters a lot more at lower rates.*/
  if(rate!=coding_rate)setup_resample(&inopt,coding_rate==48000?(complexity+1)/2:5,coding_rate);

  if(rate!=coding_rate&&complexity!=10&&!quiet){
    fprintf(stderr,"Notice: Using resampling with complexity<10.\n");
    fprintf(stderr,"Opusenc is fastest with 48, 24, 16, 12, or 8kHz input.\n\n");
  }

  /*OggOpus headers*/ /*FIXME: broke forcemono*/
  header.channels=chan;
  header.channel_mapping=header.channels>8?255:chan>2;
  header.input_sample_rate=rate;
  header.gain=inopt.gain;

  /*Initialize Opus encoder*/
  /*Frame sizes <10ms can only use the MDCT modes, so we switch on RESTRICTED_LOWDELAY
    to save the extra 4ms of codec lookahead when we'll be using only small frames.*/
  if (inopt.no_surround) {
    header.channel_mapping=255;

    /* parse --coupled option if available */
    header.nb_coupled = 0;
    if (coupling) {
      typedef int map_entry[2];
      const char *delim = ",";
      char *tmp = coupling;
      char *last_delim = NULL;
      map_entry *mapping = NULL;
      map_entry *mapping_dst = NULL;
      int j,last_mapped;

      /* count number of coupled channels: */
      /* e.g. "1:2,4:5" means 1/2 are paired and 4/5 are paired */
      while (*tmp) {
        if (delim[0] == *tmp) {
          header.nb_coupled++;
          last_delim = tmp;
        }
        tmp++;
      }

      /* count one more for trailing value */
      header.nb_coupled += last_delim < (coupling + strlen(coupling) - 1);

      /* parse integer values into new array */
      mapping = (map_entry *)malloc(sizeof(map_entry) * header.nb_coupled);
      if (mapping == NULL) {
        fprintf(stderr, "Error allocating mapping buffer.\n");
        exit(1);
      }

      tmp = strtok(coupling, delim);
      mapping_dst = mapping;
      while (tmp) {
        /* separate left vs right channel numbers by colon: */
        char *colon = strchr(tmp, ':');
        if (colon == NULL) {
          fprintf(stderr, "Error parsing --coupled argument; must be '1:2,3:4' format");
          exit(1);
        }
        *colon = 0;
        (*mapping_dst)[0] = atoi(tmp);
        (*mapping_dst)[1] = atoi(colon+1);
        mapping_dst++;
        tmp = strtok(0, delim);
      }

      /* coupled streams must be mapped in first */
      for (i = 0; i < header.nb_coupled; i++) {
        header.stream_map[i*2+0] = mapping[i][0] - 1;
        header.stream_map[i*2+1] = mapping[i][1] - 1;
      }

      /* map the remaining channels in */
      last_mapped = header.nb_coupled * 2;
      for (i = 0; i < header.channels; i++) {
        int is_mapped = 0;
        for (j = 0; j < header.nb_coupled * 2; j++) {
          if (header.stream_map[j] == i) {
            is_mapped = 1;
            break;
          }
        }

        if (!is_mapped) {
          header.stream_map[last_mapped] = i;
          last_mapped++;
        }
      }
    } else {
      /* no coupling so direct map of channels: */
      for (i = 0; i < header.channels; i++) {
        header.stream_map[i] = i;
      }
      for (; i < 255; i++) {
        header.stream_map[i] = 255;
      }
    }

    header.nb_streams = header.channels - header.nb_coupled;
    st=opus_multistream_encoder_create(
      coding_rate,
      header.channels,
      header.nb_streams,
      header.nb_coupled,
      header.stream_map,
      frame_size<480/(48000/coding_rate)?OPUS_APPLICATION_RESTRICTED_LOWDELAY:OPUS_APPLICATION_AUDIO,
      &ret
    );
  } else {
    st=opus_multistream_surround_encoder_create(coding_rate, chan, header.channel_mapping, &header.nb_streams, &header.nb_coupled,
      header.stream_map, frame_size<480/(48000/coding_rate)?OPUS_APPLICATION_RESTRICTED_LOWDELAY:OPUS_APPLICATION_AUDIO, &ret);
  }
  if(ret!=OPUS_OK){
    fprintf(stderr, "Error cannot create encoder: %s\n", opus_strerror(ret));
    exit(1);
  }

  min_bytes=max_frame_bytes=(1275*3+7)*header.nb_streams;
  packet=malloc(sizeof(unsigned char)*max_frame_bytes);
  if(packet==NULL){
    fprintf(stderr,"Error allocating packet buffer.\n");
    exit(1);
  }

  if(bitrate<0){
    /*Lower default rate for sampling rates [8000-44100) by a factor of (rate+16k)/(64k)*/
    bitrate=((64000*header.nb_streams+32000*header.nb_coupled)*
             (IMIN(48,IMAX(8,((rate<44100?rate:48000)+1000)/1000))+16)+32)>>6;
  }

  if(bitrate>(1024000*chan)||bitrate<500){
    fprintf(stderr,"Error: Bitrate %d bits/sec is insane.\nDid you mistake bits for kilobits?\n",bitrate);
    fprintf(stderr,"--bitrate values from 6-256 kbit/sec per channel are meaningful.\n");
    exit(1);
  }
  bitrate=IMIN(chan*256000,bitrate);

  ret=opus_multistream_encoder_ctl(st, OPUS_SET_BITRATE(bitrate));
  if(ret!=OPUS_OK){
    fprintf(stderr,"Error OPUS_SET_BITRATE returned: %s\n",opus_strerror(ret));
    exit(1);
  }

  ret=opus_multistream_encoder_ctl(st, OPUS_SET_VBR(!with_hard_cbr));
  if(ret!=OPUS_OK){
    fprintf(stderr,"Error OPUS_SET_VBR returned: %s\n",opus_strerror(ret));
    exit(1);
  }

  if(!with_hard_cbr){
    ret=opus_multistream_encoder_ctl(st, OPUS_SET_VBR_CONSTRAINT(with_cvbr));
    if(ret!=OPUS_OK){
      fprintf(stderr,"Error OPUS_SET_VBR_CONSTRAINT returned: %s\n",opus_strerror(ret));
      exit(1);
    }
  }

  ret=opus_multistream_encoder_ctl(st, OPUS_SET_COMPLEXITY(complexity));
  if(ret!=OPUS_OK){
    fprintf(stderr,"Error OPUS_SET_COMPLEXITY returned: %s\n",opus_strerror(ret));
    exit(1);
  }

  ret=opus_multistream_encoder_ctl(st, OPUS_SET_PACKET_LOSS_PERC(expect_loss));
  if(ret!=OPUS_OK){
    fprintf(stderr,"Error OPUS_SET_PACKET_LOSS_PERC returned: %s\n",opus_strerror(ret));
    exit(1);
  }

#ifdef OPUS_SET_LSB_DEPTH
  ret=opus_multistream_encoder_ctl(st, OPUS_SET_LSB_DEPTH(IMAX(8,IMIN(24,inopt.samplesize))));
  if(ret!=OPUS_OK){
    fprintf(stderr,"Warning OPUS_SET_LSB_DEPTH returned: %s\n",opus_strerror(ret));
  }
#endif

  /*This should be the last set of CTLs, except the lookahead get, so it can override the defaults.*/
  for(i=0;i<opt_ctls;i++){
    int target=opt_ctls_ctlval[i*3];
    if(target==-1){
      ret=opus_multistream_encoder_ctl(st,opt_ctls_ctlval[i*3+1],opt_ctls_ctlval[i*3+2]);
      if(ret!=OPUS_OK){
        fprintf(stderr,"Error opus_multistream_encoder_ctl(st,%d,%d) returned: %s\n",opt_ctls_ctlval[i*3+1],opt_ctls_ctlval[i*3+2],opus_strerror(ret));
        exit(1);
      }
    }else if(target<header.nb_streams){
      OpusEncoder *oe;
      opus_multistream_encoder_ctl(st,OPUS_MULTISTREAM_GET_ENCODER_STATE(target,&oe));
      ret=opus_encoder_ctl(oe, opt_ctls_ctlval[i*3+1],opt_ctls_ctlval[i*3+2]);
      if(ret!=OPUS_OK){
        fprintf(stderr,"Error opus_encoder_ctl(st[%d],%d,%d) returned: %s\n",target,opt_ctls_ctlval[i*3+1],opt_ctls_ctlval[i*3+2],opus_strerror(ret));
        exit(1);
      }
    }else{
      fprintf(stderr,"Error --set-ctl-int target stream %d is higher than the maximum stream number %d.\n",target,header.nb_streams-1);
      exit(1);
    }
  }

  /*We do the lookahead check late so user CTLs can change it*/
  ret=opus_multistream_encoder_ctl(st, OPUS_GET_LOOKAHEAD(&lookahead));
  if(ret!=OPUS_OK){
    fprintf(stderr,"Error OPUS_GET_LOOKAHEAD returned: %s\n",opus_strerror(ret));
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
    fprintf(stderr,"   Input: %0.6gkHz %d channel%s\n",
            header.input_sample_rate/1000.,chan,chan<2?"":"s");
    fprintf(stderr,"  Output: %d channel%s (",header.channels,header.channels<2?"":"s");
    if(header.nb_coupled>0)fprintf(stderr,"%d coupled",header.nb_coupled*2);
    if(header.nb_streams-header.nb_coupled>0)fprintf(stderr,
       "%s%d uncoupled",header.nb_coupled>0?", ":"",
       header.nb_streams-header.nb_coupled);
    fprintf(stderr,")\n          %0.2gms packets, %0.6gkbit/sec%s\n",
       frame_size/(coding_rate/1000.), bitrate/1000.,
       with_hard_cbr?" CBR":with_cvbr?" CVBR":" VBR");
    fprintf(stderr," Mapping: family=%d, map: [", header.channel_mapping);
    for (i = 0; i < header.channels; i++) {
      fprintf(stderr,"%d", header.stream_map[i]);
      if (i < header.channels - 1) fprintf(stderr,",");
    }
    fprintf(stderr,"]\n Preskip: %d\n",header.preskip);

    if(frange!=NULL)fprintf(stderr,"         Writing final range file %s\n",range_file);
    fprintf(stderr,"\n");
  }

  if(strcmp(outFile,"-")==0){
#if defined WIN32 || defined _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    fout=stdout;
  }else{
    fout=fopen_utf8(outFile, "wb");
    if(!fout){
      perror(outFile);
      exit(1);
    }
  }

  /*Initialize Ogg stream struct*/
  if(ogg_stream_init(&os, serialno)==-1){
    fprintf(stderr,"Error: stream init failed\n");
    exit(1);
  }

  /*Write header*/
  {
    /*The Identification Header is 19 bytes, plus a Channel Mapping Table for
      mapping families other than 0. The Channel Mapping Table is 2 bytes +
      1 byte per channel. Because the maximum number of channels is 255, the
      maximum size of this header is 19 + 2 + 255 = 276 bytes.*/
    unsigned char header_data[276];
    int packet_size=opus_header_to_packet(&header, header_data, sizeof(header_data));
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

    comment_pad(&inopt.comments, &inopt.comments_length, comment_padding);
    op.packet=(unsigned char *)inopt.comments;
    op.bytes=inopt.comments_length;
    op.b_o_s=0;
    op.e_o_s=0;
    op.granulepos=0;
    op.packetno=1;
    ogg_stream_packetin(&os, &op);
  }

  /* writing the rest of the Opus header packets */
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

  free(inopt.comments);

  input=malloc(sizeof(float)*frame_size*chan);
  if(input==NULL){
    fprintf(stderr,"Error: couldn't allocate sample buffer.\n");
    exit(1);
  }

  /*Main encoding loop (one frame per iteration)*/
  nb_samples=-1;
  while(!op.e_o_s){
    int size_segments,cur_frame_size;
    id++;

    if(nb_samples<0){
      nb_samples = inopt.read_samples(inopt.readdata,input,frame_size);
      total_samples+=nb_samples;
    }

    if(start_time==0){
      start_time = time(NULL);
    }

    cur_frame_size=frame_size;

    if(nb_samples<cur_frame_size){
      op.e_o_s=1;
      /*Avoid making the final packet 20ms or more longer than needed.*/
      cur_frame_size-=((cur_frame_size-(nb_samples>0?nb_samples:1))
        /(coding_rate/50))*(coding_rate/50);
      /*No fancy end padding, just fill with zeros for now.*/
      for(i=nb_samples*chan;i<cur_frame_size*chan;i++)input[i]=0;
    }

    /*Encode current frame*/
    VG_UNDEF(packet,max_frame_bytes);
    VG_CHECK(input,sizeof(float)*chan*cur_frame_size);
    nbBytes=opus_multistream_encode_float(st, input, cur_frame_size, packet, max_frame_bytes);
    if(nbBytes<0){
      fprintf(stderr, "Encoding failed: %s. Aborting.\n", opus_strerror(nbBytes));
      break;
    }
    VG_CHECK(packet,nbBytes);
    VG_UNDEF(input,sizeof(float)*chan*cur_frame_size);
    nb_encoded+=cur_frame_size;
    enc_granulepos+=cur_frame_size*48000/coding_rate;
    total_bytes+=nbBytes;
    size_segments=(nbBytes+255)/255;
    peak_bytes=IMAX(nbBytes,peak_bytes);
    min_bytes=IMIN(nbBytes,min_bytes);

    if(frange!=NULL){
      opus_uint32 rngs[256];
      OpusEncoder *oe;
      for(i=0;i<header.nb_streams;i++){
        ret=opus_multistream_encoder_ctl(st,OPUS_MULTISTREAM_GET_ENCODER_STATE(i,&oe));
        ret=opus_encoder_ctl(oe,OPUS_GET_FINAL_RANGE(&rngs[i]));
      }
      save_range(frange,cur_frame_size*(48000/coding_rate),packet,nbBytes,
                 rngs,header.nb_streams);
    }

    /*Flush early if adding this packet would make us end up with a
      continued page which we wouldn't have otherwise.*/
    while((((size_segments<=255)&&(last_segments+size_segments>255))||
           (enc_granulepos-last_granulepos>max_ogg_delay))&&
#ifdef OLD_LIBOGG
           ogg_stream_flush(&os, &og)){
#else
           ogg_stream_flush_fill(&os, &og,255*255)){
#endif
      if(ogg_page_packets(&og)!=0)last_granulepos=ogg_page_granulepos(&og);
      last_segments-=og.header[26];
      ret=oe_write_page(&og, fout);
      if(ret!=og.header_len+og.body_len){
         fprintf(stderr,"Error: failed writing data to output stream\n");
         exit(1);
      }
      bytes_written+=ret;
      pages_out++;
    }

    /*The downside of early reading is if the input is an exact
      multiple of the frame_size you'll get an extra frame that needs
      to get cropped off. The downside of late reading is added delay.
      If your ogg_delay is 120ms or less we'll assume you want the
      low delay behavior.*/
    if((!op.e_o_s)&&max_ogg_delay>5760){
      nb_samples = inopt.read_samples(inopt.readdata,input,frame_size);
      total_samples+=nb_samples;
      if(nb_samples==0)op.e_o_s=1;
    } else nb_samples=-1;

    op.packet=(unsigned char *)packet;
    op.bytes=nbBytes;
    op.b_o_s=0;
    op.granulepos=enc_granulepos;
    if(op.e_o_s){
      /*We compute the final GP as ceil(len*48k/input_rate)+preskip. When a
        resampling decoder does the matching floor((len-preskip)*input_rate/48k)
        conversion, the resulting output length will exactly equal the original
        input length when 0<input_rate<=48000.*/
      op.granulepos=((original_samples*48000+rate-1)/rate)+header.preskip;
    }
    op.packetno=2+id;
    ogg_stream_packetin(&os, &op);
    last_segments+=size_segments;

    /*If the stream is over or we're sure that the delayed flush will fire,
      go ahead and flush now to avoid adding delay.*/
    while((op.e_o_s||(enc_granulepos+(frame_size*48000/coding_rate)-last_granulepos>max_ogg_delay)||
           (last_segments>=255))?
#ifdef OLD_LIBOGG
    /*Libogg > 1.2.2 allows us to achieve lower overhead by
      producing larger pages. For 20ms frames this is only relevant
      above ~32kbit/sec.*/
           ogg_stream_flush(&os, &og):
           ogg_stream_pageout(&os, &og)){
#else
           ogg_stream_flush_fill(&os, &og,255*255):
           ogg_stream_pageout_fill(&os, &og,255*255)){
#endif
      if(ogg_page_packets(&og)!=0)last_granulepos=ogg_page_granulepos(&og);
      last_segments-=og.header[26];
      ret=oe_write_page(&og, fout);
      if(ret!=og.header_len+og.body_len){
         fprintf(stderr,"Error: failed writing data to output stream\n");
         exit(1);
      }
      bytes_written+=ret;
      pages_out++;
    }

    if(!quiet){
      stop_time = time(NULL);
      if(stop_time>last_spin){
        double estbitrate;
        double coded_seconds=nb_encoded/(double)coding_rate;
        double wall_time=(stop_time-start_time)+1e-6;
        char sbuf[55];
        static const char spinner[]="|/-\\";
        if(!with_hard_cbr){
          double tweight=1./(1+exp(-((coded_seconds/10.)-3.)));
          estbitrate=(total_bytes*8.0/coded_seconds)*tweight+
                      bitrate*(1.-tweight);
        }else estbitrate=nbBytes*8*((double)coding_rate/frame_size);
        fprintf(stderr,"\r");
        for(i=0;i<last_spin_len;i++)fprintf(stderr," ");
        if(inopt.total_samples_per_channel>0 && nb_encoded<inopt.total_samples_per_channel){
          snprintf(sbuf,54,"\r[%c] %2d%% ",spinner[last_spin&3],
          (int)floor(nb_encoded/(double)(inopt.total_samples_per_channel+inopt.skip)*100.));
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
  stop_time = time(NULL);

  if(last_spin_len)fprintf(stderr,"\r");
  for(i=0;i<last_spin_len;i++)fprintf(stderr," ");
  if(last_spin_len)fprintf(stderr,"\r");

  if(!quiet){
    double coded_seconds=nb_encoded/(double)coding_rate;
    double wall_time=(stop_time-start_time)+1e-6;
    fprintf(stderr,"Encoding complete\n");
    fprintf(stderr,"-----------------------------------------------------\n");
    fprintf(stderr,"       Encoded:");
    print_time(coded_seconds);
    fprintf(stderr,"\n       Runtime:");
    print_time(wall_time);
    fprintf(stderr,"\n                (%0.4gx realtime)\n",coded_seconds/wall_time);
    fprintf(stderr,"         Wrote: %" I64FORMAT " bytes, %d packets, %" I64FORMAT " pages\n",bytes_written,id+1,pages_out);
    fprintf(stderr,"       Bitrate: %0.6gkbit/s (without overhead)\n",
            total_bytes*8.0/(coded_seconds)/1000.0);
    fprintf(stderr," Instant rates: %0.6gkbit/s to %0.6gkbit/s\n                (%d to %d bytes per packet)\n",
            min_bytes*8*((double)coding_rate/frame_size/1000.),
            peak_bytes*8*((double)coding_rate/frame_size/1000.),min_bytes,peak_bytes);
    fprintf(stderr,"      Overhead: %0.3g%% (container+metadata)\n",(bytes_written-total_bytes)/(double)bytes_written*100.);
#ifdef OLD_LIBOGG
    if(max_ogg_delay>(frame_size*(48000/coding_rate)*4))fprintf(stderr,"    (use libogg 1.3 or later for lower overhead)\n");
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
  if(downmix)clear_downmix(&inopt);
  in_format->close_func(inopt.readdata);
  if(fin)fclose(fin);
  if(fout)fclose(fout);
  if(frange)fclose(frange);
#ifdef WIN_UNICODE
   free_commandline_arguments_utf8(&argc_utf8, &argv_utf8);
#endif
  return 0;
}

/*
 Comments will be stored in the Vorbis style.
 It is described in the "Structure" section of
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

static void comment_init(char **comments, int* length, const char *vendor_string)
{
  /*The 'vendor' field should be the actual encoding library used.*/
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
  int tag_len=(tag?strlen(tag)+1:0);
  int val_len=strlen(val);
  int len=(*length)+4+tag_len+val_len;

  p=(char*)realloc(p, len);
  if(p==NULL){
    fprintf(stderr, "realloc failed in comment_add()\n");
    exit(1);
  }

  writeint(p, *length, tag_len+val_len);      /* length of comment */
  if(tag){
    memcpy(p+*length+4, tag, tag_len);        /* comment tag */
    (p+*length+4)[tag_len-1] = '=';           /* separator */
  }
  memcpy(p+*length+4+tag_len, val, val_len);  /* comment */
  writeint(p, 8+4+vendor_length, user_comment_list_length+1);
  *comments=p;
  *length=len;
}

static void comment_pad(char **comments, int* length, int amount)
{
  if(amount>0){
    int i;
    int newlen;
    char* p=*comments;
    /*Make sure there is at least amount worth of padding free, and
       round up to the maximum that fits in the current ogg segments.*/
    newlen=(*length+amount+255)/255*255-1;
    p=realloc(p,newlen);
    if(p==NULL){
      fprintf(stderr,"realloc failed in comment_pad()\n");
      exit(1);
    }
    for(i=*length;i<newlen;i++)p[i]=0;
    *comments=p;
    *length=newlen;
  }
}
#undef readint
#undef writeint
