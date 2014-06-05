#ifndef _RELAY_COMMON_H
#define _RELAY_COMMON_H

#ifndef NO_INLINE
#define INLINE inline
#else
#define INLINE
#endif

#define FORMAT(fmt,arg...) fmt " [%s():%s:%d @ %u]\n",##arg,__func__,__FILE__,__LINE__,(unsigned int) time(NULL)
#define _E(fmt,arg...) fprintf(stderr,FORMAT(fmt,##arg))

#define SAYX(rc,fmt,arg...) do {    \
    _E(fmt,##arg);                  \
    exit(rc);                       \
} while(0)

#endif
