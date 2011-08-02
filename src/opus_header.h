#ifndef OPUS_HEADER_H
#define OPUS_HEADER_H

#include "opus_types.h"

typedef struct {
   int version;
   opus_uint32 sample_rate;
   int multi_stream;
   int channels;
   int pregap;
   int nb_streams;
   unsigned char mapping[256][3];
} OpusHeader;

int opus_header_parse(const unsigned char *header, int len, OpusHeader *h);
int opus_header_to_packet(const OpusHeader *h, unsigned char *packet, int len);

#endif
