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

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__cplusplus)
# define ATTR_NORETURN _Noreturn
#elif defined(__GNUC__) && defined(__GNUC_MINOR__) \
       && __GNUC__ + (__GNUC_MINOR__ >= 8) > 2  /* GCC 2.8+ */
# define ATTR_NORETURN __attribute__((noreturn))
#else
# define ATTR_NORETURN
#endif

#if defined(__GNUC__) && defined(__GNUC_MINOR__) \
     && __GNUC__ + (__GNUC_MINOR__ >= 3) > 3  /* GCC 3.3+ */
# define ATTR_FORMAT_PRINTF __attribute__((__format__(printf, 1, 2)))
#else
# define ATTR_FORMAT_PRINTF
#endif

void oi_info(char *format, ...) ATTR_FORMAT_PRINTF;
void oi_warn(char *format, ...) ATTR_FORMAT_PRINTF;
void oi_error(char *format, ...) ATTR_FORMAT_PRINTF;
ATTR_NORETURN void oi_fatal(char *format, ...) ATTR_FORMAT_PRINTF;
void check_xiph_comment(stream_processor *stream, int i, const char *comment, int comment_length);
