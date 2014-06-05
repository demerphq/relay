#ifndef _RELAY_H
#define _RELAY_H 

#include "relay_common.h"
#include "relay_threads.h"
#include "util.h"

#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/stat.h>

#if defined(__APPLE__) || defined(__MACH__)
#include <sys/syslimits.h>
# ifndef MSG_NOSIGNAL
#   define MSG_NOSIGNAL SO_NOSIGPIPE
# endif
#endif

#endif
