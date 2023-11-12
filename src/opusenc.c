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
#include <stdarg.h>
#include <time.h>
#include <math.h>

#if (!defined WIN32 && !defined _WIN32) || defined(__MINGW32__)
# include <unistd.h>
#else
# include <process.h>
# define getpid _getpid
#endif

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
#include <opusenc.h>

#include "opus_header.h"
#include "encoder.h"
#include "diag_range.h"
#include "cpusupport.h"

/* printf format specifier for opus_int64 */
#if !defined opus_int64 && defined PRId64
# define I64FORMAT PRId64
#elif defined WIN32 || defined _WIN32
# define I64FORMAT "I64d"
#else
# define I64FORMAT "lld"
#endif

#define IMIN(a,b) ((a) < (b) ? (a) : (b))   /**< Minimum int value.   */
#define IMAX(a,b) ((a) > (b) ? (a) : (b))   /**< Maximum int value.   */

#if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 3)
# define FORMAT_PRINTF __attribute__((__format__(printf, 1, 2)))
#else
# define FORMAT_PRINTF
#endif

static void fatal(const char *format, ...) FORMAT_PRINTF;

static void fatal(const char *format, ...)
{
  va_list ap;
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
  exit(1);
}

static void opustoolsversion(const char *opusversion)
{
  printf("opusenc %s %s (using %s)\n",PACKAGE_NAME,PACKAGE_VERSION,opusversion);
  printf("Copyright (C) 2008-2018 Xiph.Org Foundation\n");
}

static void opustoolsversion_short(const char *opusversion)
{
  opustoolsversion(opusversion);
}

static void usage(void)
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
  printf(" --bitrate n.nnn    Set target bitrate in kbit/s (6-256/channel)\n");
  printf(" --vbr              Use variable bitrate encoding (default)\n");
  printf(" --cvbr             Use constrained variable bitrate encoding\n");
  printf(" --hard-cbr         Use hard constant bitrate encoding\n");
  printf(" --music            Tune low bitrates for music (override automatic detection)\n");
  printf(" --speech           Tune low bitrates for speech (override automatic detection)\n");
  printf(" --comp n           Set encoding complexity (0-10, default: 10 (slowest))\n");
  printf(" --framesize n      Set maximum frame size in milliseconds\n");
  printf("                      (2.5, 5, 10, 20, 40, 60, default: 20)\n");
  printf(" --expect-loss n    Set expected packet loss in percent (default: 0)\n");
  printf(" --downmix-mono     Downmix to mono\n");
  printf(" --downmix-stereo   Downmix to stereo (if >2 channels)\n");
#ifdef OPUS_SET_PHASE_INVERSION_DISABLED_REQUEST
  printf(" --no-phase-inv     Disable use of phase inversion for intensity stereo\n");
#endif
  printf(" --max-delay n      Set maximum container delay in milliseconds\n");
  printf("                      (0-1000, default: 1000)\n");
  printf("\nMetadata options:\n");
  printf(" --title title      Set track title\n");
  printf(" --artist artist    Set artist or author, may be used multiple times\n");
  printf(" --album album      Set album or collection\n");
  printf(" --genre genre      Set genre, may be used multiple times\n");
  printf(" --date YYYY-MM-DD  Set date of track (YYYY, YYYY-MM, or YYYY-MM-DD)\n");
  printf(" --tracknumber n    Set track number\n");
  printf(" --comment tag=val  Add the given string as an extra comment\n");
  printf("                      This may be used multiple times\n");
  printf(" --picture file     Attach album art (see --help-picture)\n");
  printf("                      This may be used multiple times\n");
  printf(" --padding n        Reserve n extra bytes for metadata (default: 512)\n");
  printf(" --discard-comments Don't keep metadata when transcoding\n");
  printf(" --discard-pictures Don't keep pictures when transcoding\n");
  printf("\nInput options:\n");
  printf(" --raw              Interpret input as raw PCM data without headers\n");
  printf(" --raw-float        Interpret input as raw float data without headers\n");
  printf(" --raw-bits n       Set bits/sample for raw input (default: 16; 32 for float)\n");
  printf(" --raw-rate n       Set sampling rate for raw input (default: 48000)\n");
  printf(" --raw-chan n       Set number of channels for raw input (default: 2)\n");
  printf(" --raw-endianness n 1 for big endian, 0 for little (default: 0)\n");
  printf(" --ignorelength     Ignore the data length in Wave headers\n");
  printf(" --channels fmt     Override the format of the input channels (ambix, discrete)\n");
  printf("\nDiagnostic options:\n");
  printf(" --serial n         Force use of a specific stream serial number\n");
  printf(" --save-range file  Save check values for every frame to a file\n");
  printf(" --set-ctl-int x=y  Pass the encoder control x with value y (advanced)\n");
  printf("                      Preface with s: to direct the ctl to multistream s\n");
  printf("                      This may be used multiple times\n");
}

static void help_picture(void)
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
  hours=(opus_int64)(seconds/3600);
  seconds-=hours*3600.;
  minutes=(opus_int64)(seconds/60);
  seconds-=minutes*60.;
  if (hours) {
    fprintf(stderr, " %" I64FORMAT " hour%s%s", hours, hours!=1 ? "s" : "",
      minutes && seconds>0 ? "," : minutes || seconds>0 ? " and" : "");
  }
  if (minutes) {
    fprintf(stderr, " %" I64FORMAT " minute%s%s", minutes, minutes!=1 ? "s" : "",
      seconds>0 ? (hours ? ", and" : " and") : "");
  }
  if (seconds>0 || (!hours && !minutes)) {
    fprintf(stderr, " %0.4g second%s", seconds, seconds!=1 ? "s" : "");
  }
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
  opus_int32 nb_streams;
  opus_int32 nb_coupled;
  FILE *frange;
} EncData;

static int write_callback(void *user_data, const unsigned char *ptr, opus_int32 len)
{
  EncData *data = (EncData*)user_data;
  data->bytes_written += len;
  data->pages_out++;
  return fwrite(ptr, 1, len, data->fout) != (size_t)len;
}

static int close_callback(void *user_data)
{
  EncData *obj = (EncData*)user_data;
  return fclose(obj->fout) != 0;
}

static void packet_callback(void *user_data, const unsigned char *packet_ptr, opus_int32 packet_len, opus_uint32 flags)
{
  EncData *data = (EncData*)user_data;
  int nb_samples = opus_packet_get_nb_samples(packet_ptr, packet_len, 48000);
  if (nb_samples <= 0) return;  /* ignore header packets */
  data->total_bytes+=packet_len;
  data->peak_bytes=IMAX(packet_len,data->peak_bytes);
  data->min_bytes=IMIN(packet_len,data->min_bytes);
  data->nb_encoded += nb_samples;
  data->packets_out++;
  data->last_length = packet_len;
  if (data->frange!=NULL) {
    int ret;
    opus_uint32 rngs[256];
    OpusEncoder *oe;
    int s;
    for (s = 0; s < data->nb_streams; ++s) {
      rngs[s] = 0;
      ret = ope_encoder_ctl(data->enc, OPUS_MULTISTREAM_GET_ENCODER_STATE(s, &oe));
      if (ret == OPE_OK && oe != NULL) {
          (void)opus_encoder_ctl(oe, OPUS_GET_FINAL_RANGE(&rngs[s]));
      }
    }
    save_range(data->frange,nb_samples,packet_ptr,packet_len,
               rngs,data->nb_streams);
  }
  (void)flags;
}

static int is_valid_ctl(int request)
{
  /*
   * These encoder ctls can be overridden for testing purposes without any
   * special handling in opusenc.  Some have their own option that should
   * be preferred but can still be overridden on a per-stream basis.  The
   * default settings are tuned to produce the best quality at the chosen
   * bitrate, so in general lower quality should be expected if these
   * settings are overridden.
   */
  switch (request) {
  case OPUS_SET_APPLICATION_REQUEST:
  case OPUS_SET_BITRATE_REQUEST:
  case OPUS_SET_MAX_BANDWIDTH_REQUEST:
  case OPUS_SET_VBR_REQUEST:
  case OPUS_SET_BANDWIDTH_REQUEST:
  case OPUS_SET_COMPLEXITY_REQUEST:
  case OPUS_SET_INBAND_FEC_REQUEST:
  case OPUS_SET_PACKET_LOSS_PERC_REQUEST:
  case OPUS_SET_DTX_REQUEST:
  case OPUS_SET_VBR_CONSTRAINT_REQUEST:
  case OPUS_SET_FORCE_CHANNELS_REQUEST:
  case OPUS_SET_SIGNAL_REQUEST:
  case OPUS_SET_LSB_DEPTH_REQUEST:
  case OPUS_SET_PREDICTION_DISABLED_REQUEST:
#ifdef OPUS_SET_PHASE_INVERSION_DISABLED_REQUEST
  case OPUS_SET_PHASE_INVERSION_DISABLED_REQUEST:
#endif
  case OPE_SET_DECISION_DELAY_REQUEST:
  case OPE_SET_MUXING_DELAY_REQUEST:
  case OPE_SET_COMMENT_PADDING_REQUEST:
  case OPE_SET_SERIALNO_REQUEST:
  case OPE_SET_HEADER_GAIN_REQUEST:
    return 1;
  }
  return 0;
}

static void validate_ambisonics_channel_count(int num_channels)
{
  int order_plus_one;
  int nondiegetic_chs;
  if(num_channels<1||num_channels>227) fatal("Error: the number of channels must not be <1 or >227.\n");
  order_plus_one=sqrt(num_channels);
  nondiegetic_chs=num_channels-order_plus_one*order_plus_one;
  if(nondiegetic_chs!=0&&nondiegetic_chs!=2) fatal("Error: invalid number of ambisonics channels.\n");
}

static const char *channels_format_name(int channels_format, int channels)
{
  static const char *format_name[8] =
  {
    "mono", "stereo", "linear surround", "quadraphonic",
    "5.0 surround", "5.1 surround", "6.1 surround", "7.1 surround"
  };

  if (channels_format == CHANNELS_FORMAT_DEFAULT) {
    if (channels >= 1 && channels <= 8) {
      return format_name[channels-1];
    }
  } else if (channels_format == CHANNELS_FORMAT_AMBIX) {
    return "ambix";
  }
  return "discrete";
}

int main(int argc, char **argv)
{
  static const input_format raw_format =
  {
    NULL, 0, raw_open, wav_close, "Raw", N_("Raw file reader")
  };
  struct option long_options[] =
  {
    {"quiet", no_argument, NULL, 0},
    {"bitrate", required_argument, NULL, 0},
    {"hard-cbr",no_argument,NULL, 0},
    {"vbr",no_argument,NULL, 0},
    {"cvbr",no_argument,NULL, 0},
    {"music", no_argument, NULL, 0},
    {"speech", no_argument, NULL, 0},
    {"comp", required_argument, NULL, 0},
    {"complexity", required_argument, NULL, 0},
    {"framesize", required_argument, NULL, 0},
    {"expect-loss", required_argument, NULL, 0},
    {"downmix-mono",no_argument,NULL, 0},
    {"downmix-stereo",no_argument,NULL, 0},
    {"no-downmix",no_argument,NULL, 0},
    {"no-phase-inv", no_argument, NULL, 0},
    {"max-delay", required_argument, NULL, 0},
    {"serial", required_argument, NULL, 0},
    {"save-range", required_argument, NULL, 0},
    {"set-ctl-int", required_argument, NULL, 0},
    {"help", no_argument, NULL, 0},
    {"help-picture", no_argument, NULL, 0},
    {"channels", required_argument, NULL, 0},
    {"raw", no_argument, NULL, 0},
    {"raw-bits", required_argument, NULL, 0},
    {"raw-rate", required_argument, NULL, 0},
    {"raw-chan", required_argument, NULL, 0},
    {"raw-endianness", required_argument, NULL, 0},
    {"raw-float", no_argument, NULL, 0},
    {"ignorelength", no_argument, NULL, 0},
    {"version", no_argument, NULL, 0},
    {"version-short", no_argument, NULL, 0},
    {"comment", required_argument, NULL, 0},
    {"artist", required_argument, NULL, 0},
    {"title", required_argument, NULL, 0},
    {"album", required_argument, NULL, 0},
    {"tracknumber", required_argument, NULL, 0},
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
  int                signal_type=OPUS_AUTO;
  int                expect_loss=0;
  int                complexity=10;
  int                downmix=0;
  int                no_phase_inv=0;
  int                *opt_ctls_ctlval;
  int                opt_ctls=0;
  int                max_ogg_delay=48000; /*48kHz samples*/
  int                seen_file_icons=0;
  int                comment_padding=512;
  int                serialno;
  opus_int32         lookahead=0;
  int                mapping_family;
  int                orig_channels;
  int                orig_channels_format;
#ifdef WIN_UNICODE
  int argc_utf8;
  char **argv_utf8;
#endif

  if (query_cpu_support()) {
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
  inopt.channels_format=CHANNELS_FORMAT_DEFAULT;
  inopt.rate=rate;
  /* 0 dB gain is recommended unless you know what you're doing */
  inopt.gain=0;
  inopt.samplesize=16;
  inopt.endianness=0;
  inopt.rawmode=0;
  inopt.rawmode_f=0;
  inopt.ignorelength=0;
  inopt.copy_comments=1;
  inopt.copy_pictures=1;

  start_time = time(NULL);
  srand((((unsigned)getpid()&65535)<<15)^(unsigned)start_time);
  serialno=rand();

  inopt.comments = ope_comments_create();
  if (inopt.comments == NULL) fatal("Error: failed to allocate memory for comments\n");
  opus_version=opus_get_version_string();
  /*Vendor string should just be the encoder library,
    the ENCODER comment specifies the tool used.*/
  snprintf(ENCODER_string, sizeof(ENCODER_string), "opusenc from %s %s",PACKAGE_NAME,PACKAGE_VERSION);
  ret = ope_comments_add(inopt.comments, "ENCODER", ENCODER_string);
  if (ret != OPE_OK) {
    fatal("Error: failed to add ENCODER comment: %s\n", ope_strerror(ret));
  }

  data.enc = NULL;
  data.fout = NULL;
  data.total_bytes = 0;
  data.bytes_written = 0;
  data.nb_encoded = 0;
  data.pages_out = 0;
  data.packets_out = 0;
  data.peak_bytes = 0;
  data.min_bytes = 256*1275*6;
  data.last_length = 0;
  data.nb_streams = 1;
  data.nb_coupled = 0;
  data.frange = NULL;

  /*Process command-line options*/
  cline_size=0;
  while (1) {
    int c;
    int save_cmd;
    int option_index;
    const char *optname;

    c=getopt_long(argc_utf8, argv_utf8, "hV", long_options, &option_index);
    if (c==-1)
       break;

    switch (c) {
      case 0:
        optname = long_options[option_index].name;
        save_cmd = 1;
        if (strcmp(optname, "quiet")==0) {
          quiet=1;
          save_cmd=0;
        } else if (strcmp(optname, "bitrate")==0) {
          bitrate=(opus_int32)(atof(optarg)*1000.);
        } else if (strcmp(optname, "hard-cbr")==0) {
          with_hard_cbr=1;
          with_cvbr=0;
        } else if (strcmp(optname, "cvbr")==0) {
          with_cvbr=1;
          with_hard_cbr=0;
        } else if (strcmp(optname, "vbr")==0) {
          with_cvbr=0;
          with_hard_cbr=0;
        } else if (strcmp(optname, "help")==0) {
          usage();
          exit(0);
        } else if (strcmp(optname, "help-picture")==0) {
          help_picture();
          exit(0);
        } else if (strcmp(optname, "version")==0) {
          opustoolsversion(opus_version);
          exit(0);
        } else if (strcmp(optname, "version-short")==0) {
          opustoolsversion_short(opus_version);
          exit(0);
        } else if (strcmp(optname, "ignorelength")==0) {
          inopt.ignorelength=1;
          save_cmd=0;
        } else if (strcmp(optname, "raw")==0) {
          inopt.rawmode=1;
          save_cmd=0;
        } else if (strcmp(optname, "raw-bits")==0) {
          inopt.rawmode=1;
          inopt.samplesize=atoi(optarg);
          save_cmd=0;
          if (inopt.samplesize!=8&&inopt.samplesize!=16&&inopt.samplesize!=24&&inopt.samplesize!=32) {
            fatal("Invalid bit-depth: %s\n"
              "--raw-bits must be one of 8, 16, 24, or 32\n", optarg);
          }
        } else if (strcmp(optname, "raw-rate")==0) {
          inopt.rawmode=1;
          inopt.rate=atoi(optarg);
          save_cmd=0;
        } else if (strcmp(optname, "raw-chan")==0) {
          inopt.rawmode=1;
          inopt.channels=atoi(optarg);
          save_cmd=0;
        } else if (strcmp(optname, "raw-endianness")==0) {
          inopt.rawmode=1;
          inopt.endianness=atoi(optarg);
          save_cmd=0;
        } else if (strcmp(optname, "raw-float")==0) {
          inopt.rawmode=1;
          inopt.rawmode_f=1;
          inopt.samplesize=32;
        } else if (strcmp(optname, "downmix-mono")==0) {
          downmix=1;
        } else if (strcmp(optname, "downmix-stereo")==0) {
          downmix=2;
        } else if (strcmp(optname, "no-downmix")==0) {
          downmix=-1;
        } else if (strcmp(optname, "no-phase-inv")==0) {
          no_phase_inv=1;
        } else if (strcmp(optname, "music")==0) {
          signal_type=OPUS_SIGNAL_MUSIC;
        } else if (strcmp(optname, "speech")==0) {
          signal_type=OPUS_SIGNAL_VOICE;
        } else if (strcmp(optname, "expect-loss")==0) {
          expect_loss=atoi(optarg);
          if (expect_loss>100||expect_loss<0) {
            fatal("Invalid expect-loss: %s\n"
              "Expected loss is a percentage in the range 0 to 100.\n", optarg);
          }
        } else if (strcmp(optname, "comp")==0 ||
                   strcmp(optname, "complexity")==0) {
          complexity=atoi(optarg);
          if (complexity>10||complexity<0) {
            fatal("Invalid complexity: %s\n"
              "Complexity must be in the range 0 to 10.\n", optarg);
          }
        } else if (strcmp(optname, "framesize")==0) {
          if (strcmp(optarg,"2.5")==0) opus_frame_param=OPUS_FRAMESIZE_2_5_MS;
          else if (strcmp(optarg,"5")==0) opus_frame_param=OPUS_FRAMESIZE_5_MS;
          else if (strcmp(optarg,"10")==0) opus_frame_param=OPUS_FRAMESIZE_10_MS;
          else if (strcmp(optarg,"20")==0) opus_frame_param=OPUS_FRAMESIZE_20_MS;
          else if (strcmp(optarg,"40")==0) opus_frame_param=OPUS_FRAMESIZE_40_MS;
          else if (strcmp(optarg,"60")==0) opus_frame_param=OPUS_FRAMESIZE_60_MS;
          else {
            fatal("Invalid framesize: %s\n"
              "Value is in milliseconds and must be 2.5, 5, 10, 20, 40, or 60.\n",
              optarg);
          }
          frame_size = opus_frame_param <= OPUS_FRAMESIZE_40_MS
            ? 120 << (opus_frame_param - OPUS_FRAMESIZE_2_5_MS)
            : (opus_frame_param - OPUS_FRAMESIZE_20_MS + 1) * 960;
        } else if (strcmp(optname, "max-delay")==0) {
          double val=atof(optarg);
          if (val<0.||val>1000.) {
            fatal("Invalid max-delay: %s\n"
              "Value is in milliseconds and must be in the range 0 to 1000.\n",
              optarg);
          }
          max_ogg_delay=(int)floor(val*48.);
        } else if (strcmp(optname, "channels")==0) {
          if (strcmp(optarg, "ambix")==0) {
            inopt.channels_format=CHANNELS_FORMAT_AMBIX;
          } else if (strcmp(optarg, "discrete")==0) {
            inopt.channels_format=CHANNELS_FORMAT_DISCRETE;
          } else {
            fatal("Invalid input format: %s\n"
              "--channels only supports 'ambix' or 'discrete'\n",
              optarg);
          }
        } else if (strcmp(optname, "serial")==0) {
          serialno=atoi(optarg);
        } else if (strcmp(optname, "set-ctl-int")==0) {
          int target,request;
          char *spos,*tpos;
          size_t len=strlen(optarg);
          spos=strchr(optarg,'=');
          if (len<3||spos==NULL||(spos-optarg)<1||(size_t)(spos-optarg)>=len) {
            fatal("Invalid set-ctl-int: %s\n"
              "Syntax is --set-ctl-int intX=intY\n"
              "       or --set-ctl-int intS:intX=intY\n", optarg);
          }
          tpos=strchr(optarg,':');
          if (tpos==NULL) {
            target=-1;
            tpos=optarg-1;
          } else target=atoi(optarg);
          request=atoi(tpos+1);
          if (!is_valid_ctl(request)) {
            fatal("Invalid set-ctl-int: %s\n", optarg);
          }
          if (opt_ctls==0) opt_ctls_ctlval=malloc(sizeof(int)*3);
          else opt_ctls_ctlval=realloc(opt_ctls_ctlval,sizeof(int)*(opt_ctls+1)*3);
          if (!opt_ctls_ctlval) fatal("Error: failed to allocate memory for ctls\n");
          opt_ctls_ctlval[opt_ctls*3]=target;
          opt_ctls_ctlval[opt_ctls*3+1]=request;
          opt_ctls_ctlval[opt_ctls*3+2]=atoi(spos+1);
          opt_ctls++;
        } else if (strcmp(optname, "save-range")==0) {
          if (data.frange) fclose(data.frange);
          data.frange=fopen_utf8(optarg,"w");
          save_cmd=0;
          if (data.frange==NULL) {
            perror(optarg);
            fatal("Error: cannot open save-range file: %s\n"
              "Must provide a writable file name.\n", optarg);
          }
          range_file=optarg;
        } else if (strcmp(optname, "comment")==0) {
          save_cmd=0;
          if (!strchr(optarg,'=')) {
            fatal("Invalid comment: %s\n"
              "Comments must be of the form name=value\n", optarg);
          }
          ret = ope_comments_add_string(inopt.comments, optarg);
          if (ret != OPE_OK) {
            fatal("Error: failed to add comment: %s\n", ope_strerror(ret));
          }
        } else if (strcmp(optname, "artist") == 0 ||
                   strcmp(optname, "title") == 0 ||
                   strcmp(optname, "album") == 0 ||
                   strcmp(optname, "tracknumber") == 0 ||
                   strcmp(optname, "date") == 0 ||
                   strcmp(optname, "genre") == 0) {
          save_cmd=0;
          ret = ope_comments_add(inopt.comments, optname, optarg);
          if (ret != OPE_OK) {
            fatal("Error: failed to add %s comment: %s\n", optname, ope_strerror(ret));
          }
        } else if (strcmp(optname, "picture")==0) {
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
          if (picture_file==NULL&&strchr(spec,'|')) {
            const char *p;
            char       *q;
            unsigned long val;
            /*We don't have a plain file, and there is a pipe character: assume it's
              the full form of the specification.*/
            val=strtoul(spec,&q,10);
            if (*q!='|'||val>20) {
              fatal("Invalid picture type: %.*s\n"
                "Picture type must be in the range 0 to 20; see --help-picture.\n",
                (int)strcspn(spec,"|"), spec);
            }
            /*An empty field implies a default of 'Cover (front)'.*/
            if (spec!=q) picture_type=(int)val;
            media_type=q+1;
            media_type_end=media_type+strcspn(media_type,"|");
            if (*media_type_end=='|') {
              description=media_type_end+1;
              description_end=description+strcspn(description,"|");
              if (*description_end=='|') {
                p=description_end+1;
                /*Ignore WIDTHxHEIGHTxDEPTH/COLORS.*/
                p+=strcspn(p,"|");
                if (*p=='|') {
                  filename=p+1;
                }
              }
            }
            if (filename==spec) {
              fatal("Not enough fields in picture specification:\n  %s\n"
                "The format of a picture specification is:\n"
                "  [TYPE]|[MEDIA-TYPE]|[DESCRIPTION]|[WIDTHxHEIGHTxDEPTH[/COLORS]]"
                "|FILENAME\nSee --help-picture.\n", spec);
            }
            if (media_type_end-media_type==3 && strncmp("-->",media_type,3)==0) {
              fatal("Picture URLs are no longer supported.\n"
                "See --help-picture.\n");
            }
            if (picture_type>=1&&picture_type<=2&&(seen_file_icons&picture_type)) {
              fatal("Error: only one picture of type %d (%s) is allowed\n",
                picture_type, picture_type==1 ? "32x32 icon" : "icon");
            }
          }
          if (picture_file) fclose(picture_file);
          if (description_end-description != 0) {
            size_t len = description_end-description;
            description_copy = malloc(len+1);
            memcpy(description_copy, description, len);
            description_copy[len]=0;
          }
          ret = ope_comments_add_picture(inopt.comments, filename,
            picture_type, description_copy);
          if (ret != OPE_OK) {
            fatal("Error: %s: %s\n", ope_strerror(ret), filename);
          }
          if (description_copy) free(description_copy);
          if (picture_type>=1&&picture_type<=2) seen_file_icons|=picture_type;
        } else if (strcmp(optname, "padding")==0) {
          comment_padding=atoi(optarg);
        } else if (strcmp(optname, "discard-comments")==0) {
          inopt.copy_comments=0;
          inopt.copy_pictures=0;
        } else if (strcmp(optname, "discard-pictures")==0) {
          inopt.copy_pictures=0;
        }
        /*Options whose arguments would leak file paths or just end up as
           metadata, or that relate only to input file handling or console
           output, should have save_cmd=0; to prevent them from being saved
           in the ENCODER_OPTIONS tag.*/
        if (save_cmd && cline_size<(int)sizeof(ENCODER_string)) {
          ret=snprintf(&ENCODER_string[cline_size], sizeof(ENCODER_string)-cline_size,
            "%s--%s", cline_size==0?"":" ", optname);
          if (ret<0||ret>=((int)sizeof(ENCODER_string)-cline_size)) {
            cline_size=sizeof(ENCODER_string);
          } else {
            cline_size+=ret;
            if (optarg) {
              ret=snprintf(&ENCODER_string[cline_size],
                sizeof(ENCODER_string)-cline_size, " %s",optarg);
              if (ret<0||ret>=((int)sizeof(ENCODER_string)-cline_size)) {
                cline_size=sizeof(ENCODER_string);
              } else {
                cline_size+=ret;
              }
            }
          }
        }
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
  }
  if (inopt.samplesize==32&&(!inopt.rawmode_f)) {
    fatal("Invalid bit-depth:\n"
      "--raw-bits can only be 32 for float sample format\n");
  }
  if (inopt.samplesize!=32&&(inopt.rawmode_f)) {
    fatal("Invalid bit-depth:\n"
      "--raw-bits must be 32 for float sample format\n");
  }
  if (argc_utf8-optind!=2) {
    usage();
    exit(1);
  }
  inFile=argv_utf8[optind];
  outFile=argv_utf8[optind+1];

  if (cline_size > 0) {
    ret = ope_comments_add(inopt.comments, "ENCODER_OPTIONS", ENCODER_string);
    if (ret != OPE_OK) {
      fatal("Error: failed to add ENCODER_OPTIONS comment: %s\n", ope_strerror(ret));
    }
  }

  if (strcmp(inFile, "-")==0) {
#if defined WIN32 || defined _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#elif defined OS2
    _fsetmode(stdin,"b");
#endif
    fin=stdin;
  } else {
    fin=fopen_utf8(inFile, "rb");
    if (!fin) {
      perror(inFile);
      exit(1);
    }
  }

  if (inopt.rawmode) {
    in_format = &raw_format;
    in_format->open_func(fin, &inopt, NULL, 0);
  } else in_format=open_audio_file(fin,&inopt);

  if (!in_format) {
    fatal("Error: unsupported input file: %s\n", inFile);
  }

  if (inopt.rate<100||inopt.rate>768000) {
    /*Crazy rates excluded to avoid excessive memory usage for padding/resampling.*/
    fatal("Error: unsupported sample rate in input file: %ld Hz\n", inopt.rate);
  }

  if (inopt.channels>255||inopt.channels<1) {
    fatal("Error: unsupported channel count in input file: %d\n"
      "Channel count must be in the range 1 to 255.\n", inopt.channels);
  }

  if (inopt.channels_format==CHANNELS_FORMAT_DEFAULT) {
    if (downmix==0&&inopt.channels>2&&bitrate>0&&bitrate<(16000*inopt.channels)) {
      if (!quiet) fprintf(stderr,"Notice: Surround bitrate less than 16 kbit/s per channel, downmixing.\n");
      downmix=inopt.channels>8?1:2;
    }
  } else if (inopt.channels_format==CHANNELS_FORMAT_AMBIX) {
    validate_ambisonics_channel_count(inopt.channels);
  }

  orig_channels = inopt.channels;
  orig_channels_format = inopt.channels_format;

  if (downmix>0) downmix=setup_downmix(&inopt, downmix);
  else downmix=0;

  rate=inopt.rate;
  chan=inopt.channels;

  if (inopt.total_samples_per_channel && rate!=48000)
    inopt.total_samples_per_channel = (opus_int64)
      ((double)inopt.total_samples_per_channel * (48000./(double)rate));

  if (inopt.channels_format==CHANNELS_FORMAT_AMBIX) {
    /*Use channel mapping 3 for orders {1, 2, 3} with 4 to 18 channels
      (including the non-diegetic stereo track). For other orders with no
      demixing matrices currently available, use channel mapping 2.*/
    mapping_family=(chan>=4&&chan<=18)?3:2;
  } else if (inopt.channels_format==CHANNELS_FORMAT_DISCRETE) {
    mapping_family=255;
  } else {
    mapping_family=chan>8?255:chan>2;
  }

  /*Initialize Opus encoder*/
  enc = ope_encoder_create_callbacks(&callbacks, &data, inopt.comments, rate,
    chan, mapping_family, &ret);
  if (enc == NULL) fatal("Error: failed to create encoder: %s\n", ope_strerror(ret));
  data.enc = enc;

  ret = ope_encoder_ctl(enc, OPUS_SET_EXPERT_FRAME_DURATION(opus_frame_param));
  if (ret != OPE_OK) {
    fatal("Error: OPUS_SET_EXPERT_FRAME_DURATION failed: %s\n", ope_strerror(ret));
  }
  ret = ope_encoder_ctl(enc, OPE_SET_MUXING_DELAY(max_ogg_delay));
  if (ret != OPE_OK) {
    fatal("Error: OPE_SET_MUXING_DELAY failed: %s\n", ope_strerror(ret));
  }
  ret = ope_encoder_ctl(enc, OPE_SET_SERIALNO(serialno));
  if (ret != OPE_OK) {
    fatal("Error: OPE_SET_SERIALNO failed: %s\n", ope_strerror(ret));
  }
  ret = ope_encoder_ctl(enc, OPE_SET_HEADER_GAIN(inopt.gain));
  if (ret != OPE_OK) {
    fatal("Error: OPE_SET_HEADER_GAIN failed: %s\n", ope_strerror(ret));
  }
  ret = ope_encoder_ctl(enc, OPE_SET_PACKET_CALLBACK(packet_callback, &data));
  if (ret != OPE_OK) {
    fatal("Error: OPE_SET_PACKET_CALLBACK failed: %s\n", ope_strerror(ret));
  }
  ret = ope_encoder_ctl(enc, OPE_SET_COMMENT_PADDING(comment_padding));
  if (ret != OPE_OK) {
    fatal("Error: OPE_SET_COMMENT_PADDING failed: %s\n", ope_strerror(ret));
  }

  ret = ope_encoder_ctl(enc, OPE_GET_NB_STREAMS(&data.nb_streams));
  if (ret != OPE_OK) {
    fatal("Error: OPE_GET_NB_STREAMS failed: %s\n", ope_strerror(ret));
  }
  ret = ope_encoder_ctl(enc, OPE_GET_NB_COUPLED_STREAMS(&data.nb_coupled));
  if (ret != OPE_OK) {
    fatal("Error: OPE_GET_NB_COUPLED_STREAMS failed: %s\n", ope_strerror(ret));
  }

  if (bitrate<0) {
    /*Lower default rate for sampling rates [8000-44100) by a factor of (rate+16k)/(64k)*/
    bitrate=((64000*data.nb_streams+32000*data.nb_coupled)*
             (IMIN(48,IMAX(8,((rate<44100?rate:48000)+1000)/1000))+16)+32)>>6;
  }

  if (bitrate>(1024000*chan)||bitrate<500) {
    fatal("Error: bitrate %d bits/sec is insane\n%s"
      "--bitrate values from 6 to 256 kbit/s per channel are meaningful.\n",
      bitrate, bitrate>=1000000 ? "Did you mistake bits for kilobits?\n" : "");
  }
  bitrate=IMIN(chan*256000,bitrate);

  ret = ope_encoder_ctl(enc, OPUS_SET_BITRATE(bitrate));
  if (ret != OPE_OK) {
    fatal("Error: OPUS_SET_BITRATE %d failed: %s\n", bitrate, ope_strerror(ret));
  }
  ret = ope_encoder_ctl(enc, OPUS_SET_VBR(!with_hard_cbr));
  if (ret != OPE_OK) {
    fatal("Error: OPUS_SET_VBR %d failed: %s\n", !with_hard_cbr, ope_strerror(ret));
  }
  if (!with_hard_cbr) {
    ret = ope_encoder_ctl(enc, OPUS_SET_VBR_CONSTRAINT(with_cvbr));
    if (ret != OPE_OK) {
      fatal("Error: OPUS_SET_VBR_CONSTRAINT %d failed: %s\n",
        with_cvbr, ope_strerror(ret));
    }
  }
  ret = ope_encoder_ctl(enc, OPUS_SET_SIGNAL(signal_type));
  if (ret != OPE_OK) {
    fatal("Error: OPUS_SET_SIGNAL failed: %s\n", ope_strerror(ret));
  }
  ret = ope_encoder_ctl(enc, OPUS_SET_COMPLEXITY(complexity));
  if (ret != OPE_OK) {
    fatal("Error: OPUS_SET_COMPLEXITY %d failed: %s\n", complexity, ope_strerror(ret));
  }
  ret = ope_encoder_ctl(enc, OPUS_SET_PACKET_LOSS_PERC(expect_loss));
  if (ret != OPE_OK) {
    fatal("Error: OPUS_SET_PACKET_LOSS_PERC %d failed: %s\n",
      expect_loss, ope_strerror(ret));
  }
#ifdef OPUS_SET_LSB_DEPTH
  ret = ope_encoder_ctl(enc, OPUS_SET_LSB_DEPTH(IMAX(8,IMIN(24,inopt.samplesize))));
  if (ret != OPE_OK) {
    fprintf(stderr, "Warning: OPUS_SET_LSB_DEPTH failed: %s\n", ope_strerror(ret));
  }
#endif
  if (no_phase_inv) {
#ifdef OPUS_SET_PHASE_INVERSION_DISABLED_REQUEST
    ret = ope_encoder_ctl(enc, OPUS_SET_PHASE_INVERSION_DISABLED(1));
    if (ret != OPE_OK) {
      fprintf(stderr, "Warning: OPUS_SET_PHASE_INVERSION_DISABLED failed: %s\n",
        ope_strerror(ret));
    }
#else
    fprintf(stderr,"Warning: Disabling phase inversion is not supported.\n");
#endif
  }

  /*This should be the last set of SET ctls, so it can override the defaults.*/
  for (i=0;i<opt_ctls;i++) {
    int target=opt_ctls_ctlval[i*3];
    if (target==-1) {
      ret = ope_encoder_ctl(enc, opt_ctls_ctlval[i*3+1],opt_ctls_ctlval[i*3+2]);
      if (ret != OPE_OK) {
        fatal("Error: failed to set encoder ctl %d=%d: %s\n",
          opt_ctls_ctlval[i*3+1], opt_ctls_ctlval[i*3+2], ope_strerror(ret));
      }
    } else if (target<data.nb_streams) {
      OpusEncoder *oe;
      ret = ope_encoder_ctl(enc, OPUS_MULTISTREAM_GET_ENCODER_STATE(target,&oe));
      if (ret != OPE_OK) {
        fatal("Error: OPUS_MULTISTREAM_GET_ENCODER_STATE %d failed: %s\n",
          target, ope_strerror(ret));
      }
      ret = opus_encoder_ctl(oe, opt_ctls_ctlval[i*3+1],opt_ctls_ctlval[i*3+2]);
      if (ret!=OPUS_OK) {
        fatal("Error: failed to set stream %d encoder ctl %d=%d: %s\n",
          target, opt_ctls_ctlval[i*3+1], opt_ctls_ctlval[i*3+2], opus_strerror(ret));
      }
    } else {
      fatal("Error: --set-ctl-int stream %d is higher than the highest "
        "stream number %d\n", target, data.nb_streams-1);
    }
  }

  /*We do the lookahead check late so user ctls can change it*/
  ret = ope_encoder_ctl(enc, OPUS_GET_LOOKAHEAD(&lookahead));
  if (ret != OPE_OK) {
    fatal("Error: OPUS_GET_LOOKAHEAD failed: %s\n", ope_strerror(ret));
  }

  if (!quiet) {
    int opus_app;
    fprintf(stderr, "Encoding using %s", opus_version);
    ret = ope_encoder_ctl(enc, OPUS_GET_APPLICATION(&opus_app));
    if (ret != OPE_OK) fprintf(stderr, "\n");
    else if (opus_app==OPUS_APPLICATION_VOIP) fprintf(stderr, " (VoIP)\n");
    else if (opus_app==OPUS_APPLICATION_AUDIO) fprintf(stderr, " (audio)\n");
    else if (opus_app==OPUS_APPLICATION_RESTRICTED_LOWDELAY) fprintf(stderr, " (low-delay)\n");
    else fprintf(stderr, " (unknown application)\n");
    fprintf(stderr, "-----------------------------------------------------\n");
    fprintf(stderr, "   Input: %s, %0.6g kHz, %d channel%s, %s\n",
            in_format->format, rate/1000.,
            orig_channels, orig_channels==1?"":"s",
            channels_format_name(orig_channels_format, orig_channels));
    fprintf(stderr, "  Output: Opus, %d channel%s (", chan, chan==1?"":"s");
    if (data.nb_coupled>0) fprintf(stderr, "%d coupled", data.nb_coupled*2);
    if (data.nb_streams-data.nb_coupled>0) fprintf(stderr,
       "%s%d uncoupled", data.nb_coupled>0?", ":"",
       data.nb_streams-data.nb_coupled);
    fprintf(stderr, "), %s\n          %0.2gms packets, %0.6g kbit/s%s\n",
       channels_format_name(inopt.channels_format, chan),
       frame_size/(48000/1000.), bitrate/1000.,
       with_hard_cbr?" CBR":with_cvbr?" CVBR":" VBR");
    fprintf(stderr, " Preskip: %d\n", lookahead);
    if (data.frange!=NULL) {
      fprintf(stderr, "          Writing final range file %s\n", range_file);
    }
    fprintf(stderr, "\n");
  }

  if (strcmp(outFile, "-")==0) {
#if defined WIN32 || defined _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    data.fout=stdout;
  } else {
    data.fout=fopen_utf8(outFile, "wb");
    if (!data.fout) {
      perror(outFile);
      exit(1);
    }
  }

  input=malloc(sizeof(float)*frame_size*chan);
  if (input==NULL) {
    fatal("Error: failed to allocate sample buffer\n");
  }

  /*Main encoding loop (one frame per iteration)*/
  while (1) {
    nb_samples = inopt.read_samples(inopt.readdata,input,frame_size);
    ret = ope_encoder_write_float(enc, input, nb_samples);
    if (ret != OPE_OK || nb_samples < frame_size) break;

    if (!quiet) {
      stop_time = time(NULL);
      if (stop_time>last_spin) {
        double estbitrate;
        double coded_seconds=data.nb_encoded/48000.;
        double wall_time=(double)(stop_time-start_time);
        char sbuf[55];
        static const char spinner[]="|/-\\";
        if (with_hard_cbr) {
          estbitrate=data.last_length*(8*48000./frame_size);
        } else if (data.nb_encoded<=0) {
          estbitrate=0;
        } else {
          double tweight=1./(1+exp(-((coded_seconds/10.)-3.)));
          estbitrate=(data.total_bytes*8.0/coded_seconds)*tweight+
                      bitrate*(1.-tweight);
        }
        fprintf(stderr,"\r");
        for (i=0;i<last_spin_len;i++) fprintf(stderr," ");
        if (inopt.total_samples_per_channel>0 &&
            data.nb_encoded<inopt.total_samples_per_channel+lookahead) {
          snprintf(sbuf,54,"\r[%c] %2d%% ",spinner[last_spin&3],
            (int)floor(data.nb_encoded
              /(double)(inopt.total_samples_per_channel+lookahead)*100.));
        } else {
          snprintf(sbuf,54,"\r[%c] ",spinner[last_spin&3]);
        }
        last_spin_len=(int)strlen(sbuf);
        snprintf(sbuf+last_spin_len,54-last_spin_len,
          "%02d:%02d:%02d.%02d %4.3gx realtime, %5.4g kbit/s",
          (int)(coded_seconds/3600),(int)(coded_seconds/60)%60,
          (int)(coded_seconds)%60,(int)(coded_seconds*100)%100,
          coded_seconds/(wall_time>0?wall_time:1e-6),
          estbitrate/1000.);
        fprintf(stderr,"%s",sbuf);
        fflush(stderr);
        last_spin_len=(int)strlen(sbuf);
        last_spin=stop_time;
      }
    }
  }

  if (last_spin_len) {
    fprintf(stderr,"\r");
    for (i=0;i<last_spin_len;i++) fprintf(stderr," ");
    fprintf(stderr,"\r");
  }

  if (ret == OPE_OK) ret = ope_encoder_drain(enc);
  if (ret != OPE_OK) fatal("Encoding aborted: %s\n", ope_strerror(ret));
  stop_time = time(NULL);

  if (!quiet) {
    double coded_seconds=data.nb_encoded/48000.;
    double wall_time=(double)(stop_time-start_time);
    fprintf(stderr,"Encoding complete\n");
    fprintf(stderr,"-----------------------------------------------------\n");
    fprintf(stderr,"       Encoded:");
    print_time(coded_seconds);
    fprintf(stderr,"\n       Runtime:");
    print_time(wall_time);
    fprintf(stderr,"\n");
    if (wall_time>0) {
      fprintf(stderr,"                (%0.4gx realtime)\n",coded_seconds/wall_time);
    }
    fprintf(stderr,"         Wrote: %" I64FORMAT " bytes, %" I64FORMAT " packets, "
      "%" I64FORMAT " pages\n",data.bytes_written,data.packets_out,data.pages_out);
    if (data.nb_encoded>0) {
      fprintf(stderr,"       Bitrate: %0.6g kbit/s (without overhead)\n",
              data.total_bytes*8.0/(coded_seconds)/1000.0);
      fprintf(stderr," Instant rates: %0.6g to %0.6g kbit/s\n"
                     "                (%d to %d bytes per packet)\n",
              data.min_bytes*(8*48000./frame_size/1000.),
              data.peak_bytes*(8*48000./frame_size/1000.),data.min_bytes,data.peak_bytes);
    }
    if (data.bytes_written>0) {
      fprintf(stderr,"      Overhead: %0.3g%% (container+metadata)\n",
        (data.bytes_written-data.total_bytes)/(double)data.bytes_written*100.);
    }
    fprintf(stderr,"\n");
  }

  ope_encoder_destroy(enc);
  ope_comments_destroy(inopt.comments);
  free(input);
  if (opt_ctls) free(opt_ctls_ctlval);

  if (downmix) clear_downmix(&inopt);
  in_format->close_func(inopt.readdata);
  if (fin) fclose(fin);
  if (data.frange) fclose(data.frange);
#ifdef WIN_UNICODE
  free_commandline_arguments_utf8(&argc_utf8, &argv_utf8);
#endif
  return 0;
}
