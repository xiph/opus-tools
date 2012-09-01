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
 *
 * compile with: cc -g -Wall -o opusrtc opusrtp.c -lpcap -logg
 *
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#else
# ifndef PACKAGE
#  define PACKAGE "opus-tools"
# endif
# ifndef VERSION
#  define VERSION "unknown"
# endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#ifdef HAVE_PCAP
#include <pcap.h>
#endif
#include <ogg/ogg.h>

/* state struct for passing around our handles */
typedef struct {
  ogg_stream_state *stream;
  FILE *out;
  int seq;
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

/* manufacture a generic OpusHead packet */
ogg_packet *op_opushead(void)
{
  int size = 19;
  unsigned char *data = malloc(size);
  ogg_packet *op = malloc(sizeof(*op));

  if (!data) {
    fprintf(stderr, "Couldn't allocate data buffer.\n");
    return NULL;
  }
  if (!op) {
    fprintf(stderr, "Couldn't allocate Ogg packet.\n");
    return NULL;
  }

  memcpy(data, "OpusHead", 8);  /* identifier */
  data[8] = 1;                  /* version */
  data[9] = 2;                  /* channels */
  le16(data+10, 0);             /* pre-skip */
  le32(data + 12, 48000);       /* original sample rate */
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
  char *identifier = "OpusTags";
  char *vendor = "opus rtp packet dump";
  int size = strlen(identifier) + 4 + strlen(vendor) + 4;
  unsigned char *data = malloc(size);
  ogg_packet *op = malloc(sizeof(*op));

  if (!data) {
    fprintf(stderr, "Couldn't allocate data buffer.\n");
    return NULL;
  }
  if (!op) {
    fprintf(stderr, "Couldn't allocate Ogg packet.\n");
    return NULL;
  }

  memcpy(data, identifier, 8);
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
void op_free(ogg_packet *op) {
  if (op) {
    if (op->packet) {
      free(op->packet);
    }
    free(op);
  }
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

#define IP_HEADER_MIN 20
typedef struct {
  int version;
  int header_size;
  unsigned char src[4], dst[4]; /* ipv4 addrs */
  int protocol;
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

int parse_eth_header(const unsigned char *packet, int size, eth_header *eth)
{
  if (!packet || !eth) {
    return -2;
  }
  if (size < ETH_HEADER_LEN) {
    fprintf(stderr, "Packet too short for eth\n");
    return -1;
  }
  memcpy(eth->src, packet + 0, 6);
  memcpy(eth->dst, packet + 6, 6);
  eth->type = rbe16(packet + 12);

  return 0;
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
  if (ip->version != 4) {
    fprintf(stderr, "unhandled ip version %d\n", ip->version);
    return 1;
  }

  /* ipv4 header */
  ip->header_size = 4 * (packet[0] & 0x0f);
  ip->protocol = packet[9];
  memcpy(ip->src, packet + 12, 4);
  memcpy(ip->dst, packet + 16, 4);

  if (size < ip->header_size) {
    fprintf(stderr, "Packet too short for ipv4 with options\n");
    return -1;
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

#ifdef HAVE_PCAP
/* pcap 'got_packet' callback */
void write_packet(u_char *args, const struct pcap_pkthdr *header,
                  const u_char *data)
{
  state *params = (state *)args;
  const unsigned char *packet;
  int size;
  eth_header eth;
  ip_header ip;
  udp_header udp;
  rtp_header rtp;

  fprintf(stderr, "Got %d byte packet (%d bytes captured)\n",
          header->len, header->caplen);
  packet = data;
  size = header->caplen;

  if (parse_eth_header(packet, size, &eth)) {
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
  packet += ETH_HEADER_LEN;
  size -= ETH_HEADER_LEN;

  if (parse_ip_header(packet, size, &ip)) {
    fprintf(stderr, "error parsing ip header\n");
    return;
  }
  fprintf(stderr, " ipv%d protocol %d", ip.version, ip.protocol);
  fprintf(stderr, " %d.%d.%d.%d ->",
          ip.src[0], ip.src[1], ip.src[2], ip.src[3]);
  fprintf(stderr, " %d.%d.%d.%d",
          ip.dst[0], ip.dst[1], ip.dst[2], ip.dst[3]);
  fprintf(stderr, " header %d bytes\n", ip.header_size);
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

  packet += rtp.header_size;
  size -= rtp.header_size;

  if (size < 0) {
    fprintf(stderr, "skipping short packet\n");
    return;
  }

  if (rtp.seq < params->seq) {
    fprintf(stderr, "skipping out-of-sequence packet\n");
    return;
  }
  params->seq = rtp.seq;

  if (rtp.type != 109) {
    fprintf(stderr, "skipping non-opus packet\n");
    return;
  }

  /* write the payload to our opus file */
  ogg_packet *op = op_from_pkt(packet, size);
  op->granulepos = 960*rtp.seq; // FIXME: get this from the toc byte
  ogg_stream_packetin(params->stream, op);
  free(op);
  ogg_write(params);

  if (size < rtp.payload_size) {
    fprintf(stderr, "!! truncated %d uncaptured bytes\n",
            rtp.payload_size - size);
  }
}

/* use libpcap to capture packets and write them to a file */
int sniff(char *device)
{
  state *params;
  pcap_t *pcap;
  char errbuf[PCAP_ERRBUF_SIZE];
  ogg_packet *op;

  if (!device) {
    device = "lo";
  }

  /* set up */
  pcap = pcap_open_live(device, 9600, 0, 1000, errbuf);
  if (pcap == NULL) {
    fprintf(stderr, "Couldn't open device %s: %s\n", device, errbuf);
    return(2);
  }
  params = malloc(sizeof(state));
  if (!params) {
    fprintf(stderr, "Couldn't allocate param struct.\n");
    return -1;
  }
  params->stream = malloc(sizeof(ogg_stream_state));
  if (!params->stream) {
    fprintf(stderr, "Couldn't allocate stream struct.\n");
    return -1;
  }
  if (ogg_stream_init(params->stream, rand()) < 0) {
    fprintf(stderr, "Couldn't initialize Ogg stream state.\n");
    return -1;
  }
  params->out = fopen("rtpdump.opus", "wb");
  if (!params->out) {
    fprintf(stderr, "Couldn't open output file.\n");
    return -2;
  }
  params->seq = 0;

  /* write stream headers */
  op = op_opushead();
  ogg_stream_packetin(params->stream, op);
  op_free(op);
  op = op_opustags();
  ogg_stream_packetin(params->stream, op);
  op_free(op);
  ogg_flush(params);

  /* start capture loop */
  fprintf(stderr, "Capturing packets\n");
  pcap_loop(pcap, 300, write_packet, (u_char *)params);

  /* write outstanding data */
  ogg_flush(params);

  /* clean up */
  fclose(params->out);
  ogg_stream_destroy(params->stream);
  free(params);
  pcap_close(pcap);

  return 0;
}
#endif /* HAVE_PCAP */

void opustools_version(void)
{
  printf("opusrtp %s %s\n", PACKAGE, VERSION);
  printf("Copyright (C) 2012 Xiph.Org Foundation\n");
}

void usage(char *exe)
{
  printf("Usage: %s [--sniff]\n", exe);
  printf("\n");
  printf("Receives Opus audio RTP streams.\n");
  printf("\nGeneral Options:\n");
  printf(" -h, --help           This help\n");
  printf(" -V, --version        Version information\n");
  printf(" -q, --quiet          Suppress status output\n");
  printf(" --sniff              Sniff and record Opus RTP streams\n");
}

int main(int argc, char *argv[])
{
  int option, i;
  struct option long_options[] = {
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'V'},
    {"quiet", no_argument, NULL, 'q'},
    {"sniff", no_argument, NULL, 0},
    {0, 0, 0, 0}
  };

  /* process command line arguments */
  while ((option = getopt_long(argc, argv, "hVq", long_options, &i)) != -1) {
    switch (option) {
      case 0:
        if (!strcmp(long_options[i].name, "sniff")) {
#ifdef HAVE_PCAP
          sniff("lo");
#else
          fprintf(stderr, "pcap support disabled, sorry.\n");
#endif
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
      case 'h':
        usage(argv[0]);
        return 0;
      case '?':
      default:
        usage(argv[0]);
        return 1;
    }
  }

  return 0;
}
