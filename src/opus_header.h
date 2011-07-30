#ifndef OPUS_HEADER_H
#define OPUS_HEADER_H

typedef struct {
   int sample_rate;
   int multi_stream;
   int channels;
   int pregap;
   unsigned char mapping[256][3];
} OpusHeader;

void opus_header_parse(const unsigned char *header, OpusHeader *h);
int opus_header_to_packet(const OpusHeader *h, unsigned char *packet, int len);

#endif
