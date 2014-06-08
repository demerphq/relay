#ifndef UTIL_LINUX_SETPROCTITLE_H
#define UTIL_LINUX_SETPROCTITLE_H

extern void initproctitle (int argc, char **argv);
extern void setproctitle (const char *txt);

#define STR_AND_LEN(s,l) (( l= sizeof(s "")), (s ""))

#endif
