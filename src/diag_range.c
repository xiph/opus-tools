/* Copyright (C)2012 Xiph.Org Foundation
   Copyright (C)2012 Gregory Maxwell
   Copyright (C)2012 Jean-Marc Valin
   File: diag_range.c

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
#include <opus.h>
#include "diag_range.h"

/*This is some non-exported code copied wholesale from libopus.
 *Normal programs shouldn't need these functions, but we use them here
 *to parse deep inside multichannel packets in order to get diagnostic
 *data for save-range. If you're thinking about copying it and you aren't
 *making an opus stream diagnostic tool, you're probably doing something
 *wrong.*/
static int parse_size(const unsigned char *data, opus_int32 len, opus_int16 *size)
{
   if (len<1)
   {
      *size = -1;
      return -1;
   } else if (data[0]<252)
   {
      *size = data[0];
      return 1;
   } else if (len<2)
   {
      *size = -1;
      return -1;
   } else {
      *size = 4*data[1] + data[0];
      return 2;
   }
}

static int opus_packet_parse_impl(const unsigned char *data, opus_int32 len,
      int self_delimited, opus_int16 size[48], int *payload_offset,
      opus_int32 *packet_offset)
{
   int i, bytes;
   int count;
   int cbr;
   unsigned char ch, toc;
   int framesize;
   opus_int32 last_size;
   opus_int32 pad = 0;
   const unsigned char *data0 = data;

   if (size==NULL || len<0)
      return OPUS_BAD_ARG;
   if (len==0)
      return OPUS_INVALID_PACKET;

   framesize = opus_packet_get_samples_per_frame(data, 48000);

   cbr = 0;
   toc = *data++;
   len--;
   last_size = len;
   switch (toc&0x3)
   {
   /* One frame */
   case 0:
      count=1;
      break;
   /* Two CBR frames */
   case 1:
      count=2;
      cbr = 1;
      if (!self_delimited)
      {
         if (len&0x1)
            return OPUS_INVALID_PACKET;
         last_size = len/2;
         /* If last_size doesn't fit in size[0], we'll catch it later */
         size[0] = (opus_int16)last_size;
      }
      break;
   /* Two VBR frames */
   case 2:
      count = 2;
      bytes = parse_size(data, len, size);
      len -= bytes;
      if (size[0]<0 || size[0] > len)
         return OPUS_INVALID_PACKET;
      data += bytes;
      last_size = len-size[0];
      break;
   /* Multiple CBR/VBR frames (from 0 to 120 ms) */
   default: /*case 3:*/
      if (len<1)
         return OPUS_INVALID_PACKET;
      /* Number of frames encoded in bits 0 to 5 */
      ch = *data++;
      count = ch&0x3F;
      if (count <= 0 || framesize*count > 5760)
         return OPUS_INVALID_PACKET;
      len--;
      /* Padding flag is bit 6 */
      if (ch&0x40)
      {
         int p;
         do {
            int tmp;
            if (len<=0)
               return OPUS_INVALID_PACKET;
            p = *data++;
            len--;
            tmp = p==255 ? 254: p;
            len -= tmp;
            pad += tmp;
         } while (p==255);
      }
      if (len<0)
         return OPUS_INVALID_PACKET;
      /* VBR flag is bit 7 */
      cbr = !(ch&0x80);
      if (!cbr)
      {
         /* VBR case */
         last_size = len;
         for (i=0;i<count-1;i++)
         {
            bytes = parse_size(data, len, size+i);
            len -= bytes;
            if (size[i]<0 || size[i] > len)
               return OPUS_INVALID_PACKET;
            data += bytes;
            last_size -= bytes+size[i];
         }
         if (last_size<0)
            return OPUS_INVALID_PACKET;
      } else if (!self_delimited)
      {
         /* CBR case */
         last_size = len/count;
         if (last_size*count!=len)
            return OPUS_INVALID_PACKET;
         for (i=0;i<count-1;i++)
            size[i] = (opus_int16)last_size;
      }
      break;
   }
   /* Self-delimited framing has an extra size for the last frame. */
   if (self_delimited)
   {
      bytes = parse_size(data, len, size+count-1);
      len -= bytes;
      if (size[count-1]<0 || size[count-1] > len)
         return OPUS_INVALID_PACKET;
      data += bytes;
      /* For CBR packets, apply the size to all the frames. */
      if (cbr)
      {
         if (size[count-1]*count > len)
            return OPUS_INVALID_PACKET;
         for (i=0;i<count-1;i++)
            size[i] = size[count-1];
      } else if (bytes+size[count-1] > last_size)
         return OPUS_INVALID_PACKET;
   } else
   {
      /* Because it's not encoded explicitly, it's possible the size of the
         last packet (or all the packets, for the CBR case) is larger than
         1275. Reject them here.*/
      if (last_size > 1275)
         return OPUS_INVALID_PACKET;
      size[count-1] = (opus_int16)last_size;
   }

   if (payload_offset)
      *payload_offset = (int)(data-data0);

   for (i=0;i<count;i++)
      data += size[i];

   if (packet_offset)
      *packet_offset = pad+(opus_int32)(data-data0);

   return count;
}

void save_range(FILE *frange, int frame_size, const unsigned char *packet, opus_int32 nbBytes, opus_uint32 *rngs, int nb_streams)
{
  int i;
  opus_int32 parsed_size;
  const unsigned char *subpkt;
  static const char *bw_strings[5]={"NB","MB","WB","SWB","FB"};
  static const char *mode_strings[3]={"LP","HYB","MDCT"};
  fprintf(frange,"%d, %ld, ",frame_size,(long)nbBytes);
  subpkt=packet;
  parsed_size=nbBytes;
  for (i=0;i<nb_streams;i++) {
    int j,payload_offset,nf;
    opus_int32 packet_offset;
    opus_int16 size[48];
    payload_offset=0;
    packet_offset=0;
    nf=opus_packet_parse_impl(subpkt,parsed_size,i+1!=nb_streams,
      size,&payload_offset,&packet_offset);
    fprintf(frange,"[[%d",payload_offset);
    for(j=0;j<nf;j++)fprintf(frange,", %d",(int)size[j]);
    fprintf(frange,"], %s, %s, %c, %d",
       mode_strings[((((subpkt[0]>>3)+48)&92)+4)>>5],
       bw_strings[opus_packet_get_bandwidth(subpkt)-OPUS_BANDWIDTH_NARROWBAND],
       subpkt[0]&4?'S':'M',opus_packet_get_samples_per_frame(subpkt,48000));
    fprintf(frange,", %lu]%s",(unsigned long)rngs[i],i+1==nb_streams?"\n":", ");
    parsed_size-=packet_offset;
    subpkt+=packet_offset;
  }
}
