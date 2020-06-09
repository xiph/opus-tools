/* Copyright 2012 Mozilla Foundation
   Copyright 2012 Xiph.Org Foundation
   Copyright 2012 Gregory Maxwell

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

/* dump opus rtp packets into an ogg file
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>

#ifdef HAVE_SOCKETS
# include <sys/types.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <errno.h>
# include <netdb.h>
# if defined HAVE_MACH_ABSOLUTE_TIME
#  include <mach/mach_time.h>
# elif !(defined HAVE_CLOCK_GETTIME && defined CLOCK_REALTIME && \
         defined HAVE_NANOSLEEP)
#  include <sys/time.h>
# endif
#endif

#ifdef HAVE_PCAP
# include <pcap.h>
#endif
#include <opus.h>
#include <ogg/ogg.h>

#define DYNAMIC_PAYLOAD_TYPE_MIN 96

/* state struct for passing around our handles */
typedef struct {
  ogg_stream_state *stream;
  FILE *out;
  int seq;
  ogg_int64_t granulepos;
  int linktype;
  int dst_port;
  int payload_type;
} state;

/* helper, write a little-endian 32 bit int to memory */
void le32(unsigned char *p, int v)
{
  p[0] = v & 0xff;
  p[1] = (v >> 8) & 0xff;
  p[2] = (v >> 16) & 0xff;
  p[3] = (v >> 24) & 0xff;
}

/* helper, write a little-endian 16 bit int to memory */
void le16(unsigned char *p, int v)
{
  p[0] = v & 0xff;
  p[1] = (v >> 8) & 0xff;
}

/* helper, write a big-endian 32 bit int to memory */
void be32(unsigned char *p, int v)
{
  p[0] = (v >> 24) & 0xff;
  p[1] = (v >> 16) & 0xff;
  p[2] = (v >> 8) & 0xff;
  p[3] = v & 0xff;
}

/* helper, write a big-endian 16 bit int to memory */
void be16(unsigned char *p, int v)
{
  p[0] = (v >> 8) & 0xff;
  p[1] = v & 0xff;
}

/* manufacture a generic OpusHead packet */
ogg_packet *op_opushead(int samplerate, int channels)
{
  int size = 19;
  unsigned char *data = malloc(size);
  ogg_packet *op = malloc(sizeof(*op));

  if (!data) {
    fprintf(stderr, "Couldn't allocate data buffer.\n");
    free(op);
    return NULL;
  }
  if (!op) {
    fprintf(stderr, "Couldn't allocate Ogg packet.\n");
    free(data);
    return NULL;
  }

  memcpy(data, "OpusHead", 8);  /* identifier */
  data[8] = 1;                  /* version */
  data[9] = channels;           /* channels */
  le16(data + 10, 0);           /* pre-skip */
  le32(data + 12, samplerate);  /* original sample rate */
  le16(data + 16, 0);           /* gain */
  data[18] = 0;                 /* channel mapping family */

  op->packet = data;
  op->bytes = size;
  op->b_o_s = 1;
  op->e_o_s = 0;
  op->granulepos = 0;
  op->packetno = 0;

  return op;
}


/* manufacture a generic OpusTags packet */
ogg_packet *op_opustags(void)
{
  char *vendor = "opus rtp packet dump";
  int size = 8 + 4 + strlen(vendor) + 4;
  unsigned char *data = malloc(size);
  ogg_packet *op = malloc(sizeof(*op));

  if (!data) {
    fprintf(stderr, "Couldn't allocate data buffer.\n");
    free(op);
    return NULL;
  }
  if (!op) {
    fprintf(stderr, "Couldn't allocate Ogg packet.\n");
    free(data);
    return NULL;
  }

  memcpy(data, "OpusTags", 8);
  le32(data + 8, strlen(vendor));
  memcpy(data + 12, vendor, strlen(vendor));
  le32(data + 12 + strlen(vendor), 0);

  op->packet = data;
  op->bytes = size;
  op->b_o_s = 0;
  op->e_o_s = 0;
  op->granulepos = 0;
  op->packetno = 1;

  return op;
}

ogg_packet *op_from_pkt(const unsigned char *pkt, int len)
{
  ogg_packet *op = malloc(sizeof(*op));
  if (!op) {
    fprintf(stderr, "Couldn't allocate Ogg packet.\n");
    return NULL;
  }

  op->packet = (unsigned char *)pkt;
  op->bytes = len;
  op->b_o_s = 0;
  op->e_o_s = 0;

  return op;
}

/* free a packet and its contents */
void op_free(ogg_packet *op)
{
  if (op) {
    if (op->packet) {
      free(op->packet);
    }
    free(op);
  }
}

/* check if an ogg page begins an opus stream */
int is_opus(ogg_page *og)
{
  ogg_stream_state os;
  ogg_packet op;

  ogg_stream_init(&os, ogg_page_serialno(og));
  ogg_stream_pagein(&os, og);
  if (ogg_stream_packetout(&os, &op) == 1) {
    if (op.bytes >= 19 && !memcmp(op.packet, "OpusHead", 8)) {
      ogg_stream_clear(&os);
      return 1;
    }
  }
  ogg_stream_clear(&os);
  return 0;
}

/* helper, write out available ogg pages */
int ogg_write(state *params)
{
  ogg_page page;
  size_t written;

  if (!params || !params->stream || !params->out) {
    return -1;
  }

  while (ogg_stream_pageout(params->stream, &page)) {
    written = fwrite(page.header, 1, page.header_len, params->out);
    if (written != (size_t)page.header_len) {
      fprintf(stderr, "Error writing Ogg page header\n");
      return -2;
    }
    written = fwrite(page.body, 1, page.body_len, params->out);
    if (written != (size_t)page.body_len) {
      fprintf(stderr, "Error writing Ogg page body\n");
      return -3;
    }
  }

  return 0;
}

/* helper, flush remaining ogg data */
int ogg_flush(state *params)
{
  ogg_page page;
  size_t written;

  if (!params || !params->stream || !params->out) {
    return -1;
  }

  while (ogg_stream_flush(params->stream, &page)) {
    written = fwrite(page.header, 1, page.header_len, params->out);
    if (written != (size_t)page.header_len) {
      fprintf(stderr, "Error writing Ogg page header\n");
      return -2;
    }
    written = fwrite(page.body, 1, page.body_len, params->out);
    if (written != (size_t)page.body_len) {
      fprintf(stderr, "Error writing Ogg page body\n");
      return -3;
    }
  }

  return 0;
}

#define ETH_HEADER_LEN 14
typedef struct {
  unsigned char src[6], dst[6]; /* ethernet MACs */
  int type;
} eth_header;

#define LOOP_HEADER_LEN 4
typedef struct {
  int family;
} loop_header;

#define IP_HEADER_MIN 20
#define IP6_HEADER_MIN 40
typedef struct {
  int version;
  int header_size;
  int protocol;
  char src[46];
  char dst[46];
} ip_header;

#define UDP_HEADER_LEN 8
typedef struct {
  int src, dst; /* ports */
  int size, checksum;
} udp_header;

#define RTP_HEADER_MIN 12
typedef struct {
  int version;
  int type;
  int pad, ext, cc, mark;
  int seq, time;
  int ssrc;
  int *csrc;
  int header_size;
  int payload_size;
} rtp_header;

/* helper, read a big-endian 16 bit int from memory */
static int rbe16(const unsigned char *p)
{
  int v = p[0] << 8 | p[1];
  return v;
}

/* helper, read a big-endian 32 bit int from memory */
static int rbe32(const unsigned char *p)
{
  int v = p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
  return v;
}

/* helper, read a native-endian 32 bit int from memory */
static int rne32(const unsigned char *p)
{
  /* On x86 we could just cast, but that might not meet
   * arm alignment requirements. */
  int d = 0;
  memcpy(&d, p, 4);
  return d;
}

int parse_eth_header(const unsigned char *packet, int size, eth_header *eth)
{
  int header_size = ETH_HEADER_LEN;
  if (!packet || !eth) {
    return -2;
  }
  if (size < ETH_HEADER_LEN) {
    fprintf(stderr, "Packet too short for eth\n");
    return -1;
  }
  memcpy(eth->src, packet + 0, 6);
  memcpy(eth->dst, packet + 6, 6);
  while (1) {
    eth->type = rbe16(packet + header_size - 2);
    if (eth->type != 0x8100 && eth->type != 0x88a8) break;
    /* 802.1Q */
    header_size += 4;
    if (header_size > size) {
      fprintf(stderr, "Packet too short for eth extension header\n");
      return -1;
    }
  }
  return header_size;
}

/* used by the darwin loopback interface, at least */
int parse_loop_header(const unsigned char *packet, int size, loop_header *loop)
{
  if (!packet || !loop) {
    return -2;
  }
  if (size < LOOP_HEADER_LEN) {
    fprintf(stderr, "Packet too short for loopback\n");
    return -1;
  }
  /* protocol is in host byte order on osx. may be big endian on openbsd? */
  loop->family = rne32(packet);

  return 0;
}

void format_ipv6_addr(const unsigned char *addr, char *buf, size_t bufsize)
{
#ifdef HAVE_INET_NTOP
  if (inet_ntop(AF_INET6, addr, buf, bufsize)) return;
#endif
  snprintf(buf, bufsize, "%x:%x:%x:%x:%x:%x:%x:%x",
    (addr[0]  << 8) | addr[1],  (addr[2]  << 8) | addr[3],
    (addr[4]  << 8) | addr[5],  (addr[6]  << 8) | addr[7],
    (addr[8]  << 8) | addr[9],  (addr[10] << 8) | addr[11],
    (addr[12] << 8) | addr[13], (addr[14] << 8) | addr[15]);
}

int parse_ip_header(const unsigned char *packet, int size, ip_header *ip)
{
  if (!packet || !ip) {
    return -2;
  }
  if (size < IP_HEADER_MIN) {
    fprintf(stderr, "Packet too short for ip\n");
    return -1;
  }

  ip->version = (packet[0] >> 4) & 0x0f;
  if (ip->version == 4) {
    /* ipv4 header */
    ip->header_size = 4 * (packet[0] & 0x0f);
    ip->protocol = packet[9];
    snprintf(ip->src, sizeof(ip->src), "%u.%u.%u.%u",
      packet[12], packet[13], packet[14], packet[15]);
    snprintf(ip->dst, sizeof(ip->dst), "%u.%u.%u.%u",
      packet[16], packet[17], packet[18], packet[19]);

    if (size < ip->header_size) {
      fprintf(stderr, "Packet too short for ipv4 with options\n");
      return -1;
    }
  } else if (ip->version == 6) {
    /* ipv6 header */
    if (size < IP6_HEADER_MIN) {
      fprintf(stderr, "Packet too short for IPv6\n");
      return -1;
    }
    ip->header_size = IP6_HEADER_MIN;
    ip->protocol = packet[6];

    format_ipv6_addr(packet + 8, ip->src, sizeof(ip->src));
    format_ipv6_addr(packet + 24, ip->dst, sizeof(ip->dst));

    while (1) {
      int ext_pos;
      int ext_size;

      switch (ip->protocol) {
      case 0:   /* hop-by-hop options header */
      case 43:  /* routing header */
      case 51:  /* authentication header */
      case 60:  /* destination options header */
        break;
      default:
        return 0;
      }
      ext_pos = ip->header_size;
      if (ext_pos + 8 > size ||
          ext_pos + (ext_size = (packet[ext_pos+1]+1)*8) > size) {
        fprintf(stderr, "Packet too short for IPv6 extension headers\n");
        return -1;
      }
      if (ip->protocol == 0 || ip->protocol == 60) {
        /* parse options */
        int opt_pos = ext_pos + 2;
        while (opt_pos + 1 < ext_pos + ext_size) {
          int opt_type = packet[opt_pos];
          if (opt_type == 0) opt_pos += 1;
          else if (opt_type < 0x40) opt_pos += 2 + packet[opt_pos+1];
          else {
            fprintf(stderr, "unsupported IPv6 %s option %#x\n",
              ip->protocol == 0 ? "hop-by-hop" : "destination", opt_type);
            return 1;
          }
        }
      }
      ip->protocol = packet[ext_pos];
      ip->header_size = ext_pos + ext_size;
    }
  } else {
    fprintf(stderr, "unhandled ip version %d\n", ip->version);
    return 1;
  }
  return 0;
}

int parse_udp_header(const unsigned char *packet, int size, udp_header *udp)
{
  if (!packet || !udp) {
    return -2;
  }
  if (size < UDP_HEADER_LEN) {
    fprintf(stderr, "Packet too short for udp\n");
    return -1;
  }

  udp->src = rbe16(packet);
  udp->dst = rbe16(packet + 2);
  udp->size = rbe16(packet + 4);
  udp->checksum = rbe16(packet + 6);

  return 0;
}


int parse_rtp_header(const unsigned char *packet, int size, rtp_header *rtp)
{
  if (!packet || !rtp) {
    return -2;
  }
  if (size < RTP_HEADER_MIN) {
    fprintf(stderr, "Packet too short for rtp\n");
    return -1;
  }
  rtp->version = (packet[0] >> 6) & 3;
  rtp->pad = (packet[0] >> 5) & 1;
  rtp->ext = (packet[0] >> 4) & 1;
  rtp->cc = packet[0] & 7;
  rtp->header_size = 12 + 4 * rtp->cc;
  if (rtp->ext == 1) {
    uint16_t ext_length;
    rtp->header_size += 4;
    ext_length = rbe16(packet + rtp->header_size - 2);
    rtp->header_size += ext_length * 4;
  }
  rtp->payload_size = size - rtp->header_size;

  rtp->mark = (packet[1] >> 7) & 1;
  rtp->type = (packet[1]) & 127;
  rtp->seq  = rbe16(packet + 2);
  rtp->time = rbe32(packet + 4);
  rtp->ssrc = rbe32(packet + 8);
  rtp->csrc = NULL;
  if (size < rtp->header_size) {
    fprintf(stderr, "Packet too short for RTP header\n");
    return -1;
  }

  return 0;
}

int serialize_rtp_header(unsigned char *packet, int size, rtp_header *rtp)
{
  int i;

  if (!packet || !rtp) {
    return -2;
  }
  if (size < RTP_HEADER_MIN) {
    fprintf(stderr, "Packet buffer too short for RTP\n");
    return -1;
  }
  if (size < rtp->header_size) {
    fprintf(stderr, "Packet buffer too short for declared RTP header size\n");
    return -3;
  }
  packet[0] = ((rtp->version & 3) << 6) |
              ((rtp->pad & 1) << 5) |
              ((rtp->ext & 1) << 4) |
              ((rtp->cc & 7));
  packet[1] = ((rtp->mark & 1) << 7) |
              ((rtp->type & 127));
  be16(packet+2, rtp->seq);
  be32(packet+4, rtp->time);
  be32(packet+8, rtp->ssrc);
  if (rtp->cc && rtp->csrc) {
    for (i = 0; i < rtp->cc; i++) {
      be32(packet + 12 + i*4, rtp->csrc[i]);
    }
  }

  return 0;
}

int update_rtp_header(rtp_header *rtp)
{
  rtp->header_size = 12 + 4 * rtp->cc;
  return 0;
}

#ifdef HAVE_SOCKETS
/*
 * Wait for the next time slot, which begins delta nanoseconds after the
 * start of the previous time slot, or in the case of the first call at
 * the time of the call.  delta must be in the range 0..999999999.
 */
void wait_for_time_slot(int delta)
{
# if defined HAVE_MACH_ABSOLUTE_TIME
  /* Apple */
  static mach_timebase_info_data_t tbinfo;
  static uint64_t target;

  if (tbinfo.numer == 0) {
    mach_timebase_info(&tbinfo);
    target = mach_absolute_time();
  } else {
    target += tbinfo.numer == tbinfo.denom
      ? (uint64_t)delta : (uint64_t)delta * tbinfo.denom / tbinfo.numer;
    mach_wait_until(target);
  }
# elif defined HAVE_CLOCK_GETTIME && defined CLOCK_REALTIME && \
       defined HAVE_NANOSLEEP
  /* try to use POSIX monotonic clock */
  static int initialized = 0;
  static clockid_t clock_id;
  static struct timespec target;

  if (!initialized) {
#  if defined CLOCK_MONOTONIC && \
      defined _POSIX_MONOTONIC_CLOCK && _POSIX_MONOTONIC_CLOCK >= 0
    if (
#   if _POSIX_MONOTONIC_CLOCK == 0
        sysconf(_SC_MONOTONIC_CLOCK) > 0 &&
#   endif
        clock_gettime(CLOCK_MONOTONIC, &target) == 0) {
      clock_id = CLOCK_MONOTONIC;
      initialized = 1;
    } else
#  endif
    if (clock_gettime(CLOCK_REALTIME, &target) == 0) {
      clock_id = CLOCK_REALTIME;
      initialized = 1;
    }
  } else {
    target.tv_nsec += delta;
    if (target.tv_nsec >= 1000000000) {
      ++target.tv_sec;
      target.tv_nsec -= 1000000000;
    }
#  if defined HAVE_CLOCK_NANOSLEEP && \
      defined _POSIX_CLOCK_SELECTION && _POSIX_CLOCK_SELECTION > 0
    clock_nanosleep(clock_id, TIMER_ABSTIME, &target, NULL);
#  else
    {
      /* convert to relative time */
      struct timespec rel;
      if (clock_gettime(clock_id, &rel) == 0) {
        rel.tv_sec = target.tv_sec - rel.tv_sec;
        rel.tv_nsec = target.tv_nsec - rel.tv_nsec;
        if (rel.tv_nsec < 0) {
          rel.tv_nsec += 1000000000;
          --rel.tv_sec;
        }
        if (rel.tv_sec >= 0 && (rel.tv_sec > 0 || rel.tv_nsec > 0)) {
          nanosleep(&rel, NULL);
        }
      }
    }
#  endif
  }
# else
  /* fall back to the old non-monotonic gettimeofday() */
  static int initialized = 0;
  static struct timeval target;
  struct timeval now;
  int nap;

  if (!initialized) {
    gettimeofday(&target, NULL);
    initialized = 1;
  } else {
    delta /= 1000;
    target.tv_usec += delta;
    if (target.tv_usec >= 1000000) {
      ++target.tv_sec;
      target.tv_usec -= 1000000;
    }

    gettimeofday(&now, NULL);
    nap = target.tv_usec - now.tv_usec;
    if (now.tv_sec != target.tv_sec) {
      if (now.tv_sec > target.tv_sec) nap = 0;
      else if (target.tv_sec - now.tv_sec == 1) nap += 1000000;
      else nap = 1000000;
    }
    if (nap > delta) nap = delta;
    if (nap > 0) {
#  if defined HAVE_USLEEP
      usleep(nap);
#  else
      struct timeval timeout;
      timeout.tv_sec = 0;
      timeout.tv_usec = nap;
      select(0, NULL, NULL, NULL, &timeout);
#  endif
    }
  }
# endif
}

int send_rtp_packet(int fd, struct sockaddr *addr, socklen_t addrlen,
    rtp_header *rtp, const unsigned char *opus_packet)
{
  unsigned char *packet;
  int ret;

  update_rtp_header(rtp);
  packet = malloc(rtp->header_size + rtp->payload_size);
  if (!packet) {
    fprintf(stderr, "Couldn't allocate packet buffer\n");
    return -1;
  }
  serialize_rtp_header(packet, rtp->header_size, rtp);
  memcpy(packet + rtp->header_size, opus_packet, rtp->payload_size);
  ret = sendto(fd, packet, rtp->header_size + rtp->payload_size, 0,
      addr, addrlen);
  if (ret < 0) {
    fprintf(stderr, "error sending: %s\n", strerror(errno));
  }
  free(packet);

  return ret;
}

int rtp_send_file_to_addr(const char *filename, struct sockaddr *addr,
    socklen_t addrlen, int payload_type)
{
  rtp_header rtp;
  int fd;
  int optval = 0;
  int ret;
  FILE *in;
  ogg_sync_state oy;
  ogg_stream_state os;
  ogg_page og;
  ogg_packet op;
  int headers = 0;
  char *in_data;
  const long in_size = 8192;
  size_t in_read;

  fd = socket(addr->sa_family, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0) {
    fprintf(stderr, "Couldn't create socket\n");
    return fd;
  }
  ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));
  if (ret < 0) {
    fprintf(stderr, "Couldn't set socket options\n");
    return ret;
  }

  rtp.version = 2;
  rtp.type = payload_type;
  rtp.pad = 0;
  rtp.ext = 0;
  rtp.cc = 0;
  rtp.mark = 0;
  rtp.seq = rand();
  rtp.time = rand();
  rtp.ssrc = rand();
  rtp.csrc = NULL;
  rtp.header_size = 0;
  rtp.payload_size = 0;

  fprintf(stderr, "Sending %s...\n", filename);
  in = fopen(filename, "rb");

  if (!in) {
    fprintf(stderr, "Couldn't open input file '%s'\n", filename);
    return -1;
  }
  ret = ogg_sync_init(&oy);
  if (ret < 0) {
    fprintf(stderr, "Couldn't initialize Ogg sync state\n");
    fclose(in);
    return ret;
  }
  while (!feof(in)) {
    in_data = ogg_sync_buffer(&oy, in_size);
    if (!in_data) {
      fprintf(stderr, "ogg_sync_buffer failed\n");
      fclose(in);
      return -1;
    }
    in_read = fread(in_data, 1, in_size, in);
    ret = ogg_sync_wrote(&oy, in_read);
    if (ret < 0) {
      fprintf(stderr, "ogg_sync_wrote failed\n");
      fclose(in);
      return ret;
    }
    while (ogg_sync_pageout(&oy, &og) == 1) {
      if (headers == 0) {
        if (is_opus(&og)) {
          /* this is the start of an Opus stream */
          ret = ogg_stream_init(&os, ogg_page_serialno(&og));
          if (ret < 0) {
            fprintf(stderr, "ogg_stream_init failed\n");
            fclose(in);
            return ret;
          }
          headers++;
        } else if (!ogg_page_bos(&og)) {
          /* We're past the header and haven't found an Opus stream.
           * Time to give up. */
          fclose(in);
          return 1;
        } else {
          /* try again */
          continue;
        }
      }
      /* submit the page for packetization */
      ret = ogg_stream_pagein(&os, &og);
      if (ret < 0) {
        fprintf(stderr, "ogg_stream_pagein failed\n");
        fclose(in);
        return ret;
      }
      /* read and process available packets */
      while (ogg_stream_packetout(&os,&op) == 1) {
        int samples;
        /* skip header packets */
        if (headers == 1 && op.bytes >= 19 && !memcmp(op.packet, "OpusHead", 8)) {
          headers++;
          continue;
        }
        if (headers == 2 && op.bytes >= 16 && !memcmp(op.packet, "OpusTags", 8)) {
          headers++;
          continue;
        }
        /* get packet duration */
        samples = opus_packet_get_nb_samples(op.packet, op.bytes, 48000);
        if (samples <= 0) {
          fprintf(stderr, "skipping invalid packet\n");
          continue;
        }
        /* update the rtp header and send */
        rtp.seq++;
        rtp.time += samples;
        rtp.payload_size = op.bytes;
        fprintf(stderr, "rtp %d %d %d %3d ms %5d bytes\n",
            rtp.type, rtp.seq, rtp.time, samples/48, rtp.payload_size);
        send_rtp_packet(fd, addr, addrlen, &rtp, op.packet);
        /* convert number of 48 kHz samples to nanoseconds without overflow */
        wait_for_time_slot(samples*62500/3);
      }
    }
  }

  if (headers > 0)
    ogg_stream_clear(&os);
  ogg_sync_clear(&oy);
  fclose(in);
  return 0;
}

int rtp_send_file(const char *filename, const char *dest, const char *port,
        int payload_type)
{
  int ret;
  struct addrinfo *addrs;
  struct addrinfo hints;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = 0;
  hints.ai_protocol = IPPROTO_UDP;
  ret = getaddrinfo(dest, port, &hints, &addrs);
  if (ret != 0 || !addrs) {
    fprintf(stderr, "Cannot resolve host %s port %s: %s\n",
      dest, port, gai_strerror(ret));
    return -1;
  }
  ret = rtp_send_file_to_addr(filename, addrs->ai_addr, addrs->ai_addrlen,
    payload_type);
  freeaddrinfo(addrs);
  return ret;
}
#else /* !HAVE_SOCKETS */
int rtp_send_file(const char *filename, const char *dest, const char *port,
        int payload_type)
{
  fprintf(stderr, "Cannot send %s to %s:%s'. Socket support not available.\n",
          filename, dest, port);
  (void)payload_type;
  return -2;
}
#endif

#ifdef HAVE_PCAP
/* pcap 'got_packet' callback */
void write_packet(u_char *args, const struct pcap_pkthdr *header,
                  const u_char *data)
{
  state *params = (state *)(void *)args;
  const unsigned char *packet;
  int size;
  eth_header eth;
  loop_header loop;
  ip_header ip;
  udp_header udp;
  rtp_header rtp;
  ogg_packet *op;
  int samples;
  int header_size;

  fprintf(stderr, "Got %d byte packet (%d bytes captured)\n",
          header->len, header->caplen);
  packet = data;
  size = header->caplen;

  /* parse the link-layer header */
  switch (params->linktype) {
    case DLT_EN10MB:
      header_size = parse_eth_header(packet, size, &eth);
      if (header_size < 0) {
        fprintf(stderr, "error parsing eth header\n");
        return;
      }
      fprintf(stderr, "  eth 0x%04x", eth.type);
      fprintf(stderr, " %02x:%02x:%02x:%02x:%02x:%02x ->",
              eth.src[0], eth.src[1], eth.src[2],
              eth.src[3], eth.src[4], eth.src[5]);
      fprintf(stderr, " %02x:%02x:%02x:%02x:%02x:%02x\n",
              eth.dst[0], eth.dst[1], eth.dst[2],
              eth.dst[3], eth.dst[4], eth.dst[5]);
      if (eth.type != 0x0800 && eth.type != 0x86dd) {
        fprintf(stderr, "skipping packet: not IP\n");
        return;
      }
      packet += header_size;
      size -= header_size;
      break;

    case DLT_NULL:
      if (parse_loop_header(packet, size, &loop)) {
        fprintf(stderr, "error parsing loopback header\n");
        return;
      }
      fprintf(stderr, "  loopback family %d\n", loop.family);
      if (loop.family != PF_INET && loop.family != PF_INET6) {
        fprintf(stderr, "skipping packet: not IP\n");
        return;
      }
      packet += LOOP_HEADER_LEN;
      size -= LOOP_HEADER_LEN;
      break;

# ifdef DLT_LINUX_SLL
    case DLT_LINUX_SLL:
      if (size < 16) {
        fprintf(stderr, "Packet too short for LINUX_SLL\n");
        return;
      } else {
        int packet_type = rbe16(packet);
        int arphrd_type = rbe16(packet + 2);
        int addr_len = rbe16(packet + 4);
        int proto_type = rbe16(packet + 14);
        int i;
        fprintf(stderr, "  LINUX_SLL %d %d %#04x",
                packet_type, arphrd_type, proto_type);
        if (addr_len > 8) addr_len = 8;
        for (i = 0; i < addr_len; ++i) {
          fprintf(stderr, "%c%02x", i ? ':' : ' ', packet[6 + i]);
        }
        fprintf(stderr, "\n");
        if (proto_type != 0x0800 && proto_type != 0x86dd) {
          fprintf(stderr, "skipping packet: not IP\n");
          return;
        }
        packet += 16;
        size -= 16;
      }
      break;
# endif

    default:
      fprintf(stderr, "skipping packet: unrecognized linktype %d\n",
          params->linktype);
      return;
  }

  if (parse_ip_header(packet, size, &ip)) {
    fprintf(stderr, "error parsing ip header\n");
    return;
  }
  fprintf(stderr, "  ipv%d protocol %d", ip.version, ip.protocol);
  fprintf(stderr, " %s -> %s\n", ip.src, ip.dst);
  if (ip.protocol != 17) {
    fprintf(stderr, "skipping packet: not UDP\n");
    return;
  }
  packet += ip.header_size;
  size -= ip.header_size;

  if (parse_udp_header(packet, size, &udp)) {
    fprintf(stderr, "error parsing udp header\n");
    return;
  }
  fprintf(stderr, "  udp %d bytes %d -> %d crc 0x%04x\n",
          udp.size, udp.src, udp.dst, udp.checksum);
  packet += UDP_HEADER_LEN;
  size -= UDP_HEADER_LEN;

  if (parse_rtp_header(packet, size, &rtp)) {
    fprintf(stderr, "error parsing rtp header\n");
    return;
  }
  fprintf(stderr, "  rtp 0x%08x %d %d %d",
          rtp.ssrc, rtp.type, rtp.seq, rtp.time);
  fprintf(stderr, "  v%d %s%s%s CC %d", rtp.version,
          rtp.pad ? "P":".", rtp.ext ? "X":".",
          rtp.mark ? "M":".", rtp.cc);
  fprintf(stderr, " %5d bytes\n", rtp.payload_size);

  if (!params->out) {
    return;
  }

  packet += rtp.header_size;
  size -= rtp.header_size;

  if (size < 0) {
    fprintf(stderr, "skipping short packet\n");
    return;
  }

  if (params->dst_port >= 0 && udp.dst != params->dst_port) {
    fprintf(stderr, "skipping packet with destination port %d\n", udp.dst);
    return;
  }

  if (params->payload_type >= 0
      ? rtp.type != params->payload_type
      : rtp.type < DYNAMIC_PAYLOAD_TYPE_MIN) {
    fprintf(stderr, "skipping packet with payload type %d\n", rtp.type);
    return;
  }

  /* lock onto first plausible port and payload_type if unspecified */
  if (params->dst_port < 0 || params->payload_type < 0) {
    const unsigned char *frames[48];
    opus_int16 fsizes[48];
    if (opus_packet_parse(packet, size, NULL, frames, fsizes, NULL) <= 0) {
      fprintf(stderr, "skipping non-Opus packet\n");
      return;
    }
    /* this could be a valid Opus packet */
    fprintf(stderr, "recording stream with payload type %d\n", rtp.type);
    if (params->dst_port < 0) params->dst_port = udp.dst;
    if (params->payload_type < 0) params->payload_type = rtp.type;
  }

  if (rtp.seq < params->seq) {
    fprintf(stderr, "skipping out-of-sequence packet\n");
    return;
  }
  params->seq = rtp.seq;

  /* write the payload to our opus file */
  op = op_from_pkt(packet, size);
  op->packetno = rtp.seq;
  samples = opus_packet_get_nb_samples(packet, size, 48000);
  if (samples > 0) params->granulepos += samples;
  op->granulepos = params->granulepos;
  ogg_stream_packetin(params->stream, op);
  free(op);
  ogg_write(params);

  if (size < rtp.payload_size) {
    fprintf(stderr, "!! truncated %d uncaptured bytes\n",
            rtp.payload_size - size);
  } else if (samples <= 0) {
    fprintf(stderr, "!! invalid opus packet\n");
  }
}

/* display available devices if possible */
void show_devices(void)
{
#ifdef PCAP_IF_LOOPBACK  /* pcap version >= 0.7 */
  char errbuf[PCAP_ERRBUF_SIZE];
  pcap_if_t *alldevs;

  if (pcap_findalldevs(&alldevs, errbuf) == 0) {
    if (!alldevs) {
      fprintf(stderr, "No devices available\n");
    } else {
      size_t col = 80;
      pcap_if_t *curdev;
      fprintf(stderr, "Available devices:");
      for (curdev = alldevs; curdev; curdev = curdev->next) {
        size_t len = 1 + strlen(curdev->name);
        if (col + len > 78) {
          col = 3;
          fprintf(stderr, "\n   ");
        }
        col += len;
        fprintf(stderr, " %s", curdev->name);
      }
      fprintf(stderr, "\n");
      pcap_freealldevs(alldevs);
    }
  }
#endif
}

/* use libpcap to capture packets and write them to a file */
int sniff(const char *input_file, const char *device, const char *output_file,
        int dst_port, int payload_type, int samplerate, int channels)
{
  state *params;
  pcap_t *pcap;
  char errbuf[PCAP_ERRBUF_SIZE];
  ogg_packet *op;

  /* set up */
  if (input_file) {
    pcap = pcap_open_offline(input_file, errbuf);
    if (pcap == NULL) {
      fprintf(stderr,"Cannot open pcap file: %s\n%s\n", input_file, errbuf);
      return 1;
    }
  } else {
    pcap = pcap_open_live(device, 9600, 0, 1000, errbuf);
    if (pcap == NULL) {
      fprintf(stderr, "Cannot open device %s\n%s\n", device, errbuf);
      show_devices();
      return 1;
    }
    fprintf(stderr, "Capturing packets from %s\n", device);
  }

  params = malloc(sizeof(state));
  if (!params) {
    fprintf(stderr, "Couldn't allocate param struct.\n");
    pcap_close(pcap);
    return 1;
  }
  params->linktype = pcap_datalink(pcap);
  params->stream = malloc(sizeof(ogg_stream_state));
  if (!params->stream) {
    fprintf(stderr, "Couldn't allocate stream struct.\n");
    free(params);
    pcap_close(pcap);
    return 1;
  }
  if (ogg_stream_init(params->stream, rand()) < 0) {
    fprintf(stderr, "Couldn't initialize Ogg stream state.\n");
    free(params->stream);
    free(params);
    pcap_close(pcap);
    return 1;
  }
  params->out = NULL;
  params->seq = 0;
  params->granulepos = 0;
  params->dst_port = dst_port;
  params->payload_type = payload_type;

  if (output_file) {
    if (strcmp(output_file, "-") == 0) {
      params->out = stdout;
    } else {
      params->out = fopen(output_file, "wb");
    }
    if (!params->out) {
      fprintf(stderr, "Couldn't open output file.\n");
      free(params->stream);
      free(params);
      pcap_close(pcap);
      return 1;
    }
    /* write stream headers */
    op = op_opushead(samplerate, channels);
    ogg_stream_packetin(params->stream, op);
    op_free(op);
    op = op_opustags();
    ogg_stream_packetin(params->stream, op);
    op_free(op);
    ogg_flush(params);
  }

  /* start capture loop */
  /* if reading from an input file, continue until EOF */
  pcap_loop(pcap, input_file ? 0 : 300, write_packet, (u_char *)params);

  /* write outstanding data */
  if (params->out) {
    ogg_flush(params);
    if (params->out == stdout) {
      fflush(stdout);
    } else {
      fclose(params->out);
    }
    params->out = NULL;
  }

  /* clean up */
  ogg_stream_destroy(params->stream);
  free(params);
  pcap_close(pcap);
  return 0;
}
#endif /* HAVE_PCAP */

void opustools_version(void)
{
  printf("opusrtp %s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
  printf("Copyright (C) 2012-2018 Xiph.Org Foundation\n");
}

void usage(void)
{
  printf("Transmit Opus RTP stream:\n");
  printf("  opusrtp [transmit-options] in.opus ...\n");
  printf("    -d, --destination addr Set destination IP address (default 127.0.0.1)\n");
  printf("    -p, --port n           Set destination port (default 1234)\n");
  printf("    -t, --type n           Set RTP payload type (default 120)\n");
  printf("\n");
  printf("Receive Opus RTP stream:\n");
  printf("  opusrtp [receive-options]  (specify one of --sniff or --extract)\n");
  printf("    --sniff device         Sniff device for Opus RTP streams\n");
  printf("    -e, --extract in.pcap  Extract from input pcap file\n");
  printf("    -p, --port n           Set destination port to capture\n");
  printf("    -t, --type n           Set RTP payload type to capture\n");
  printf("    -o, --output out.opus  Write Ogg Opus output file\n");
  printf("    -r, --rate n           Set original sample rate (default 48000)\n");
  printf("    -c, --channels n       Set channel count (default 2)\n");
  printf("\n");
  printf("Display help or version information:\n");
  printf("  opusrtp -h|--help\n");
  printf("  opusrtp -V|--version\n");
  printf("\n");
}

int main(int argc, char *argv[])
{
  int option, i;
  const char *dest = "127.0.0.1";
  const char *device = NULL;
  const char *input_pcap = NULL;
  const char *output_file = NULL;
  int pcap_mode = 0;
  const char *port = NULL;
  int payload_type = -1;
  int samplerate = 48000;
  int channels = 2;
  struct option long_options[] = {
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'V'},
    {"quiet", no_argument, NULL, 'q'},
    {"output", required_argument, NULL, 'o'},
    {"destination", required_argument, NULL, 'd'},
    {"port", required_argument, NULL, 'p'},
    {"rate", required_argument, NULL, 'r'},
    {"channels", required_argument, NULL, 'c'},
    {"type", required_argument, NULL, 't'},
    {"sniff", required_argument, NULL, 0},
    {"extract", required_argument, NULL, 'e'},
    {0, 0, 0, 0}
  };

  /* process command line arguments */
  while ((option = getopt_long(argc, argv, "hVqo:d:p:r:c:t:e:",
            long_options, &i)) != -1) {
    switch (option) {
      case 0:
        if (!strcmp(long_options[i].name, "sniff")) {
          device = optarg;
          pcap_mode = 1;
        } else {
          fprintf(stderr, "Unknown option - try %s --help.\n", argv[0]);
          return -1;
        }
        break;
      case 'V':
        opustools_version();
        return 0;
      case 'q':
        break;
      case 'o':
        if (optarg)
            output_file = optarg;
        break;
      case 'd':
        if (optarg)
            dest = optarg;
        break;
      case 'e':
        if (optarg) {
            input_pcap = optarg;
            pcap_mode = 1;
        }
        break;
      case 'p':
        if (optarg)
            port = optarg;
        break;
      case 'r':
        if (optarg)
            samplerate = atoi(optarg);
        break;
      case 'c':
        if (optarg)
            channels = atoi(optarg);
        break;
      case 't':
        if (optarg)
            payload_type = atoi(optarg);
        break;
      case 'h':
        usage();
        return 0;
      case '?':
      default:
        usage();
        return 1;
    }
  }

  if (optind < argc) {
    /* files to transmit were specified */
    if (pcap_mode) {
      fprintf(stderr, "Ogg Opus input files cannot be used with %s.\n",
        input_pcap ? "--extract" : "--sniff");
      return 1;
    }
    if (!port) port = "1234";
    if (payload_type < 0) payload_type = 120;
    for (i = optind; i < argc; i++) {
      rtp_send_file(argv[i], dest, port, payload_type);
    }
    return 0;
  }

  if (pcap_mode) {
#ifdef HAVE_PCAP
    int port_num = -1;
    if (port && ((port_num = atoi(port)) <= 0 || port_num > 65535)) {
      fprintf(stderr, "Invalid port number %s\n", port);
      return 1;
    }
    return sniff(input_pcap, device, output_file, port_num, payload_type,
      samplerate, channels);
#else
    (void)input_pcap;
    (void)device;
    (void)output_file;
    (void)samplerate;
    (void)channels;
    fprintf(stderr, "Sorry, pcap support is disabled.\n");
    return 1;
#endif
  }

  usage();
  return 1;
}
