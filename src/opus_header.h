#ifndef OPUS_HEADER_H
#define OPUS_HEADER_H

#include "opus_types.h"

typedef struct {
   int version;
   int channels; /* Number of channels: 1..255 */
   int preskip;
   opus_uint32 input_sample_rate;
   int gain; /* in dB S7.8 should be zero whenever possible */
   int channel_mapping;
   /* The rest is only used if channel_mapping != 0 */
   int nb_streams;
   int nb_coupled;
   unsigned char stream_map[255];
} OpusHeader;

int opus_header_parse(const unsigned char *header, int len, OpusHeader *h);
int opus_header_to_packet(const OpusHeader *h, unsigned char *packet, int len);

#endif
