#include "opus_header.h"
#include <string.h>

/* Header contents:
  - "OpusHead" (64 bits)
  - Sampling rate (32 bits, max 192)
  - Channel mapping (bool in byte)
  - Channels (8 bits)
  - Pre-gap (16 bits)
*/

typedef struct {
   unsigned char *data;
   int maxlen;
   int pos;
} Packet;

typedef struct {
   const unsigned char *data;
   int maxlen;
   int pos;
} ROPacket;

static int write_uint32(Packet *p, opus_uint32 val)
{
   if (p->pos>p->maxlen-4)
      return 0;
   p->data[p->pos  ] = (val>>24) & 0xFF;
   p->data[p->pos+1] = (val>>16) & 0xFF;
   p->data[p->pos+2] = (val>> 8) & 0xFF;
   p->data[p->pos+3] = (val    ) & 0xFF;
   p->pos += 4;
   return 1;
}

static int write_uint16(Packet *p, opus_uint16 val)
{
   if (p->pos>p->maxlen-4)
      return 0;
   p->data[p->pos  ] = (val>> 8) & 0xFF;
   p->data[p->pos+1] = (val    ) & 0xFF;
   p->pos += 2;
   return 1;
}

static int write_chars(Packet *p, const unsigned char *str, int nb_chars)
{
   int i;
   if (p->pos>p->maxlen-nb_chars)
      return 0;
   for (i=0;i<nb_chars;i++)
      p->data[p->pos++] = str[i];
   return 1;
}

static int read_uint32(ROPacket *p, opus_uint32 *val)
{
   if (p->pos>p->maxlen-4)
      return 0;
   *val =  (opus_uint32)p->data[p->pos  ]<<24;
   *val |= (opus_uint32)p->data[p->pos+1]<<16;
   *val |= (opus_uint32)p->data[p->pos+2]<< 8;
   *val |= (opus_uint32)p->data[p->pos+3];
   p->pos += 4;
   return 1;
}

static int read_uint16(ROPacket *p, opus_uint16 *val)
{
   if (p->pos>p->maxlen-4)
      return 0;
   *val =  (opus_uint16)p->data[p->pos  ]<<8;
   *val |= (opus_uint16)p->data[p->pos+1];
   p->pos += 4;
   return 1;
}

static int read_chars(ROPacket *p, unsigned char *str, int nb_chars)
{
   int i;
   if (p->pos>p->maxlen-nb_chars)
      return 0;
   for (i=0;i<nb_chars;i++)
      str[i] = p->data[p->pos++];
   return 1;
}

int opus_header_parse(const unsigned char *packet, int len, OpusHeader *h)
{
   char str[9];
   ROPacket p;
   unsigned char ch;
   opus_uint16 shortval;
   
   p.data = packet;
   p.maxlen = len;
   p.pos = 0;
   str[8] = 0;
   read_chars(&p, (unsigned char*)str, 8);
   if (strcmp(str, "OpusHead")!=0)
      return 0;
   if (!read_uint32(&p, &h->sample_rate))
      return 0;
   if (!read_chars(&p, &ch, 1))
      return 0;
   h->multi_stream = ch;
   if (!read_chars(&p, &ch, 1))
      return 0;
   h->channels = ch;
   if (!read_uint16(&p, &shortval))
      return 0;
   h->pregap = shortval;
   
   return 1;
}

int opus_header_to_packet(const OpusHeader *h, unsigned char *packet, int len)
{
   Packet p;
   unsigned char ch;
   
   p.data = packet;
   p.maxlen = len;
   p.pos = 0;
   if (!write_chars(&p, (const unsigned char*)"OpusHead", 8))
      return 0;
   if (!write_uint32(&p, h->sample_rate))
      return 0;
   ch = h->multi_stream;
   if (!write_chars(&p, &ch, 1))
      return 0;
   ch = h->channels;
   if (!write_chars(&p, &ch, 1))
      return 0;
   if (!write_uint16(&p, h->pregap))
      return 0;
   return p.pos;
}

