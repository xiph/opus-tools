#ifndef __FLAC_H
# define __FLAC_H
# include <stdio.h>
# include "os_support.h"
# include "opusenc.h"
# if defined(HAVE_LIBFLAC)
#  include <FLAC/stream_decoder.h>
#  include <FLAC/metadata.h>

typedef struct flacfile flacfile;

struct flacfile{
  FLAC__StreamDecoder *decoder;
  oe_enc_opt *inopt;
  short channels;
  FILE *f;
  const int *channel_permute;
  unsigned char *oldbuf;
  int bufpos;
  int buflen;
  float *block_buf;
  opus_int32 block_buf_pos;
  opus_int32 block_buf_len;
  opus_int32 max_blocksize;
};

# endif

int flac_id(unsigned char *buf,int len);
int oggflac_id(unsigned char *buf,int len);
int flac_open(FILE *in,oe_enc_opt *opt,unsigned char *oldbuf,int buflen);
void flac_close(void *client_data);

#endif
