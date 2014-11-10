#include "daemonize.h"

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include "log.h"

int daemonize()
{
    pid_t pid;

    /* Fork the first time. */
    pid = fork();
    if (pid == -1) {
	FATAL_ERRNO("Failed the first fork");
	return 0;
    }
    if (pid)
	_exit(0);		/* We are the first parent, bye-bye. */

    /* Become the session leader: detaches from the tty. */
    if (setsid() == -1) {
	FATAL_ERRNO("Failed setsid");
	return 0;
    }

    /* Fork the second time. */
    pid = fork();
    if (pid == -1) {
	FATAL_ERRNO("Failed the second fork");
	return 0;
    }
    if (pid)
	_exit(0);		/* We are the second parent, bye-byte. */

    /* Change directory to root, maybe. */
    if (chdir("/") == -1) {
	FATAL_ERRNO("Failed chdir to root");
	return 0;
    }

    /* Clear file creation mask. */
    umask(0);

    return 1;
}

/* Close the standard file descriptors and re-open them to/from /dev/null. */
int close_std_fds(void)
{
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    if (open("/dev/null", O_RDONLY) == -1) {
	FATAL_ERRNO("Failed to reopen stdin");
	return 0;
    }
    if (open("/dev/null", O_WRONLY) == -1) {
	FATAL_ERRNO("Failed to reopen stdout");
	return 0;
    }
    if (open("/dev/null", O_WRONLY) == -1) {
	FATAL_ERRNO("Failed to reopen stderr");
	return 0;
    }

    return 1;
}
