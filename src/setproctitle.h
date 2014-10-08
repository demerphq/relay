#ifndef UTIL_LINUX_SETPROCTITLE_H
#define UTIL_LINUX_SETPROCTITLE_H

extern void initproctitle(int argc, char **argv);
extern void setproctitle(const char *txt);
extern void destroy_proctitle(void);

#define STR_AND_LEN(s,l) (( l= sizeof(s "")), (s ""))

#endif
