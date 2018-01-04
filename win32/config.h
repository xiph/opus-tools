#ifndef CONFIG_H
#define CONFIG_H

/* comment to disable flac dependency */
#define HAVE_LIBFLAC          1
/* comment to compile with dynamic flac */
#define FLAC__NO_DLL

#define OUTSIDE_SPEEX         1
#define OPUSTOOLS             1

#define inline __inline
#define alloca _alloca
#define getpid _getpid
#define USE_ALLOCA            1
#define FLOATING_POINT        1

#ifdef HAVE_LIBFLAC
#ifdef FLAC__NO_DLL
    #pragma comment(lib, "libFLAC_static.lib")
#else
    #pragma comment(lib, "libFLAC_dynamic.lib")
#endif
#endif

#define SPX_RESAMPLE_EXPORT
#define __SSE__

#define RANDOM_PREFIX opustools

#define PACKAGE_NAME "opus-tools"
#include "version.h"


#endif /* CONFIG_H */
