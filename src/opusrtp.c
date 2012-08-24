/* dump opus rtp packets into an ogg file
 *
 * compile with: gcc -g -Wall -o opusrtc opusrtp.c -lpcap -logg
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pcap.h>
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

ogg_packet *op_from_pkt(unsigned char *pkt, int len)
{
  ogg_packet *op = malloc(sizeof(*op));
  if (!op) {
    fprintf(stderr, "Couldn't allocate Ogg packet.\n");
    return NULL;
  }

  op->packet = pkt;
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
    if (written != page.header_len) {
      fprintf(stderr, "Error writing Ogg page header\n");
      return -2;
    }
    written = fwrite(page.body, 1, page.body_len, params->out);
    if (written != page.body_len) {
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
    if (written != page.header_len) {
      fprintf(stderr, "Error writing Ogg page header\n");
      return -2;
    }
    written = fwrite(page.body, 1, page.body_len, params->out);
    if (written != page.body_len) {
      fprintf(stderr, "Error writing Ogg page body\n");
      return -3;
    }
  }

  return 0;
}

typedef struct {
  unsigned char src[6], dst[6]; /* ethernet MACs */
  int type;
} eth_header;

typedef struct {
  int version;
  int header_size;
  unsigned char src[4], dst[4]; /* ipv4 addrs */
  int protocol;
} ip_header;

typedef struct {
  int src, dst; /* ports */
  int size, checksum;
} udp_header;

typedef struct {
  int version;
  int type;
  int pad, ext, cc, mark;
  int seq, time;
  int ssrc;
  int header_size;
  int payload_size;
} rtp_header;

/* pcap 'got_packet' callback */
void write_packet(u_char *args, const struct pcap_pkthdr *header,
                  const u_char *packet)
{
  state *params = (state *)args;
  eth_header eth;
  ip_header ip;
  udp_header udp;
  rtp_header rtp;

  fprintf(stderr, "Got %d byte packet (%d bytes captured)\n",
          header->len, header->caplen);

  /* eth header is always 14 bytes */
  if (header->caplen < 14) {
    fprintf(stderr, "Packet too short for eth\n");
    return;
  }
  memcpy(eth.src, packet, 6);
  memcpy(eth.dst, packet + 6, 6);
  eth.type = packet[12] << 8 | packet[13];

  fprintf(stderr, "  eth 0x%04x", eth.type);
  fprintf(stderr, " %02x:%02x:%02x:%02x:%02x:%02x ->",
          eth.src[0], eth.src[1], eth.src[2],
          eth.src[3], eth.src[4], eth.src[5]);
  fprintf(stderr, " %02x:%02x:%02x:%02x:%02x:%02x\n",
          eth.dst[0], eth.dst[1], eth.dst[2],
          eth.dst[3], eth.dst[4], eth.dst[5]);

  /* ipv4 header */
  if (header->caplen < 14 + 20) {
    fprintf(stderr, "Packet too short for ipv4\n");
    return;
  }
  ip.version = (packet[14+0] >> 4) & 0x0f;
  ip.header_size = 4 * (packet[14+0] & 0x0f);
  ip.protocol = packet[14 + 9];
  memcpy(ip.src, packet + 14 + 12, 4);
  memcpy(ip.dst, packet + 14 + 16, 4);

  fprintf(stderr, " ipv%d protocol %d", ip.version, ip.protocol);
  fprintf(stderr, " %d.%d.%d.%d ->",
          ip.src[0], ip.src[1], ip.src[2], ip.src[3]);
  fprintf(stderr, " %d.%d.%d.%d",
          ip.dst[0], ip.dst[1], ip.dst[2], ip.dst[3]);
  fprintf(stderr, " header %d bytes\n", ip.header_size);
  if (header->caplen < 14 + ip.header_size) {
    fprintf(stderr, "Packet too short for ipv4 with options\n");
    return;
  }

  if (header->caplen < 14 + ip.header_size + 8) {
    fprintf(stderr, "Packet too short for udp\n");
    return;
  }
  udp.src = packet[14+ip.header_size] << 8 |
            packet[14+ip.header_size + 1];
  udp.dst = packet[14+ip.header_size + 2] << 8 |
            packet[14+ip.header_size + 3];
  udp.size = packet[14+ip.header_size + 4] << 8 |
             packet[14+ip.header_size + 5];
  udp.checksum = packet[14+ip.header_size + 6] << 8 |
                 packet[14+ip.header_size + 7];
  fprintf(stderr, "  udp %d bytes %d -> %d crc 0x%04x\n",
          udp.size, udp.src, udp.dst, udp.checksum);
  if (header->caplen < 14 + ip.header_size + 8 + 12) {
    fprintf(stderr, "Packet too short for rtp\n");
    return;
  }
  rtp.version = (packet[14+ip.header_size+8 + 0] >> 6) & 3;
  rtp.pad = (packet[14+ip.header_size+8] >> 5) & 1;
  rtp.ext = (packet[14+ip.header_size+8] >> 4) & 1;
  rtp.cc = packet[14+ip.header_size+8] & 7;
  rtp.header_size = 12 + 4 * rtp.cc;
  rtp.payload_size = udp.size - 8 - rtp.header_size;

  rtp.mark = (packet[14+ip.header_size+8 + 1] >> 7) & 1;
  rtp.type = (packet[14+ip.header_size+8 + 1]) & 127;
  rtp.seq = packet[14+ip.header_size+8 + 2] << 8 |
            packet[14+ip.header_size+8 + 3];
  rtp.time = packet[14+ip.header_size+8 + 4] << 24 |
             packet[14+ip.header_size+8 + 5] << 16 |
             packet[14+ip.header_size+8 + 6] << 8 |
             packet[14+ip.header_size+8 + 7];
  rtp.ssrc = packet[14+ip.header_size+8 + 8] << 24 |
             packet[14+ip.header_size+8 + 9] << 16 |
             packet[14+ip.header_size+8 + 10] << 8 |
             packet[14+ip.header_size+8 + 11];

  fprintf(stderr, "  rtp 0x%08x %d %d %d",
          rtp.ssrc, rtp.type, rtp.seq, rtp.time);
  fprintf(stderr, "  v%d %s%s%s CC %d", rtp.version,
          rtp.pad ? "P":".", rtp.ext ? "X":".",
          rtp.mark ? "M":".", rtp.cc);
  fprintf(stderr, " %5d bytes\n", rtp.payload_size);

  if (header->caplen < 14 + ip.header_size + 8 + rtp.header_size) {
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
  int size = header->caplen - 14 - ip.header_size - 8 - rtp.header_size;
  unsigned char *data = packet + 14+ip.header_size+8 + rtp.header_size;
  ogg_packet *op = op_from_pkt(data, size);
  op->granulepos = 960*rtp.seq;
  ogg_stream_packetin(params->stream, op);
  free(op);
  ogg_write(params);

  if (size < rtp.payload_size) {
    fprintf(stderr, "!! truncated %d uncaptured bytes\n",
            rtp.payload_size - size);
  }
}

int main(int argc, char *argv[])
{
  state *params;
  pcap_t *pcap;
  char *dev = "lo";
  int port = 55555;
  char errbuf[PCAP_ERRBUF_SIZE];
  ogg_packet *op;

  /* set up */
  pcap = pcap_open_live(dev, 9600, 0, 1000, errbuf);
  if (pcap == NULL) {
    fprintf(stderr, "Couldn't open device %s: %s\n", dev, errbuf);
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
