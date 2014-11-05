#ifndef RELAY_DAEMONIZE_H
#define RELAY_DAEMONIZE_H

/* Become a daemon, detaching from the controlling terminal.
 * Does not close the standard file descriptors, see below.
 * Returns true if successful, false if not (e.g. fork failed).
 * Returns only to the new child process, the parents never return but inestead _exit(0). */
int daemonize(int do_chroot);

/* Close the standard file descriptors and re-open them to/from /dev/null.
 * Separated from daemonize so that the process can send its final messages. */
int close_std_fds(void);

#endif				/* #ifndef RELAY_DAEMONIZE_H */
