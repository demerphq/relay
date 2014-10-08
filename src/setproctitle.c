/*
 *  set process title for ps (from sendmail)
 *
 *  Clobbers argv of our main procedure so ps(1) will display the title.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "setproctitle.h"

#ifndef SPT_BUFSIZE
# define SPT_BUFSIZE     2048
#endif

extern char **environ;

static char **argv0 = NULL;
static int argv_lth = 0;

static char *prog_str = NULL;
static int prog_len = 0;

static char *args_str = NULL;
static int args_len = 0;

void initproctitle(int argc, char **argv)
{
    int i;
    char **envp = environ;

    if (argc) {
	prog_len = strlen(argv[0]);
	prog_str = strdup(argv[0]);
	if (argc > 1) {
	    for (i = 1; i < argc; i++) {
		args_len += strlen(argv[i]);
		args_len++;
	    }
	    args_str = (char *) malloc(args_len);
	    args_len = 0;
	    for (i = 1; i < argc; i++) {
		int l = strlen(argv[i]);
		memcpy(args_str + args_len, argv[i], l);
		args_len += l;
		args_str[args_len++] = ' ';
	    }
	    args_len--;
	    args_str[args_len] = 0;
	} else {
	    args_str = "";
	    args_len = 0;
	}
    } else {
	prog_str = STR_AND_LEN("relay", prog_len);
    }
    /*
       printf("prog_str: '%*s' args_str: '%*s'\n", prog_len, prog_str, args_len, args_str);
     */

    /*
     * Move the environment so we can reuse the memory.
     * (Code borrowed from sendmail.)
     * WARNING: ugly assumptions on memory layout here;
     *          if this ever causes problems, #undef DO_PS_FIDDLING
     */
    for (i = 0; envp[i] != NULL; i++)
	continue;

    environ = (char **) malloc(sizeof(char *) * (i + 1));
    if (environ == NULL)
	return;

    for (i = 0; envp[i] != NULL; i++)
	if ((environ[i] = strdup(envp[i])) == NULL)
	    return;
    environ[i] = NULL;

    argv0 = argv;
    if (i > 0)
	argv_lth = envp[i - 1] + strlen(envp[i - 1]) - argv0[0];
    else
	argv_lth = argv0[argc - 1] + strlen(argv0[argc - 1]) - argv0[0];
}

void setproctitle(const char *txt)
{
    int i;
    char buf[SPT_BUFSIZE];

    if (!argv0)
	return;

    if (prog_len + args_len + strlen(txt) + 6 > SPT_BUFSIZE)
	return;

    sprintf(buf, "%s %s -- %s", prog_str, args_str, txt);

    i = strlen(buf);
    if (i > argv_lth - 2) {
	i = argv_lth - 2;
	buf[i] = '\0';
    }
    memset(argv0[0], '\0', argv_lth);	/* clear the memory area */
    strcpy(argv0[0], buf);

    argv0[1] = NULL;
}

void destroy_proctitle(void)
{
    char **tmp;
    free(prog_str);
    free(args_str);
    tmp = environ;
    while (tmp && *tmp) {
	free(*tmp++);
    }
    free(environ);
}
