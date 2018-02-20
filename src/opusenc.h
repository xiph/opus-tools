#ifndef __OPUSENC_H
#define __OPUSENC_H

#include <opus_types.h>
#include <ogg/ogg.h>

#ifdef ENABLE_NLS
#include <libintl.h>
#define _(X) gettext(X)
#else
#define _(X) (X)
#define textdomain(X)
#define bindtextdomain(X, Y)
#endif
#ifdef gettext_noop
#define N_(X) gettext_noop(X)
#else
#define N_(X) (X)
#endif

typedef long (*audio_read_func)(void *src, float *buffer, int samples);

typedef struct
{
    audio_read_func read_samples;
    void *readdata;
    opus_int64 total_samples_per_channel;
    int rawmode;
    int channels;
    long rate;
    int gain;
    int samplesize;
    int endianness;
    char *infilename;
    int ignorelength;
    int skip;
    int extraout;
    char *comments;
    int comments_length;
    int copy_comments;
    int copy_pictures;
    int no_surround;
} oe_enc_opt;

void setup_scaler(oe_enc_opt *opt, float scale);
void clear_scaler(oe_enc_opt *opt);
void setup_padder(oe_enc_opt *opt, ogg_int64_t *original_samples);
void clear_padder(oe_enc_opt *opt);
int setup_downmix(oe_enc_opt *opt, int out_channels);
void clear_downmix(oe_enc_opt *opt);
void comment_add(char **comments, int* length, char *tag, char *val);

typedef struct
{
    int (*id_func)(unsigned char *buf, int len); /* Returns true if can load file */
    int id_data_len; /* Amount of data needed to id whether this can load the file */
    int (*open_func)(FILE *in, oe_enc_opt *opt, unsigned char *buf, int buflen);
    void (*close_func)(void *);
    char *format;
    char *description;
} input_format;

typedef struct {
    unsigned short format;
    unsigned short channels;
    unsigned int samplerate;
    unsigned int bytespersec;
    unsigned short align;
    unsigned short samplesize;
    unsigned int mask;
} wav_fmt;

typedef struct {
    unsigned short channels;
    short samplesize;
    opus_int64 totalsamples;
    opus_int64 samplesread;
    FILE *f;
    short bigendian;
    short unsigned8bit;
    int *channel_permute;
} wavfile;

typedef struct {
    short channels;
    unsigned int totalframes;
    short samplesize;
    double rate;
    unsigned int offset;
    unsigned int blocksize;
} aiff_fmt;

typedef wavfile aifffile; /* They're the same */

input_format *open_audio_file(FILE *in, oe_enc_opt *opt);

int raw_open(FILE *in, oe_enc_opt *opt, unsigned char *buf, int buflen);
int wav_open(FILE *in, oe_enc_opt *opt, unsigned char *buf, int buflen);
int aiff_open(FILE *in, oe_enc_opt *opt, unsigned char *buf, int buflen);
int wav_id(unsigned char *buf, int len);
int aiff_id(unsigned char *buf, int len);
void wav_close(void *);
void raw_close(void *);
int setup_resample(oe_enc_opt *opt, int complexity, long outfreq);
void clear_resample(oe_enc_opt *opt);

long wav_read(void *, float *buffer, int samples);
long wav_ieee_read(void *, float *buffer, int samples);

#endif /* __OPUSENC_H */
