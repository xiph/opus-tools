/* Ogginfo
 *
 * A tool to describe ogg file contents and metadata.
 *
 * Copyright 2002-2005 Michael Smith <msmith@xiph.org>
 * Licensed under the GNU GPL, distributed with this program.
 */

/*No NLS support for now*/
#define _(X) (X)

#ifdef HAVE_INTTYPES_H
# include <inttypes.h>
#endif
#ifndef PRId64
# if defined WIN32 || defined _WIN32
#  define PRId64 "I64d"
# else
#  define PRId64 "lld"
# endif
#endif

typedef struct _stream_processor {
    void (*process_page)(struct _stream_processor *, ogg_page *);
    void (*process_end)(struct _stream_processor *);
    int isillegal;
    int constraint_violated;
    int shownillegal;
    int isnew;
    long seqno;
    int lostseq;
    int seen_file_icons;

    int start;
    int end;

    int num;
    char *type;

    ogg_uint32_t serial; /* must be 32 bit unsigned */
    ogg_stream_state os;
    void *data;
} stream_processor;

typedef struct {
    stream_processor *streams;
    int allocated;
    int used;

    int in_headers;
} stream_set;

#if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 3)
# define OI_FORMAT_PRINTF __attribute__((__format__(printf, 1, 2)))
#else
# define OI_FORMAT_PRINTF
#endif

void oi_info(char *format, ...) OI_FORMAT_PRINTF;
void oi_warn(char *format, ...) OI_FORMAT_PRINTF;
void oi_error(char *format, ...) OI_FORMAT_PRINTF;
void check_xiph_comment(stream_processor *stream, int i, const char *comment, int comment_length);
