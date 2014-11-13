#ifndef UTIL_LINUX_SETPROCTITLE_H
#define UTIL_LINUX_SETPROCTITLE_H

void initproctitle(int argc, char **argv);
void setproctitle(const char *txt);
void destroy_proctitle(void);

#define STR_AND_LEN(s,l) (( l= sizeof(s "")), (s ""))

#endif				/* #ifndef UTIL_LINUX_SETPROCTITLE_H */
