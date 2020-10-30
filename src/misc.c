#include "misc.h"
#include <errno.h> 
#include <stdarg.h> 

/*codes from APUE */

#define MAXLINE 1000

void daemonize(const char *cmd)
{
	int i, fd0, fd1, fd2;
	pid_t pid;
	struct rlimit rl;
	struct sigaction sa;
	/*
	 * * Clear file creation mask.
	 * */
	umask(0);
	/*
	 * * Get maximum number of file descriptors.
	 * */
	if (getrlimit(RLIMIT_NOFILE, &rl) < 0)
		err_quit("%s: can’t get file limit", cmd);
	/*
	 * * Become a session leader to lose controlling TTY.
	 * */
	if ((pid = fork()) < 0)
		err_quit("%s: can’t fork", cmd);
	else if (pid != 0) /* parent */
		exit(0);
	setsid();
	/*
	 * * Ensure future opens won’t allocate controlling TTYs.
	 * */
	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);

	sa.sa_flags = 0;
	if (sigaction(SIGHUP, &sa, NULL) < 0)
		err_quit("%s: can’t ignore SIGHUP", cmd);
	if ((pid = fork()) < 0)
		err_quit("%s: can’t fork", cmd);
	else if (pid != 0) /* parent */
		exit(0);
	/*
	 * * Change the current working directory to the root so
	 * * we won’t prevent file systems from being unmounted.
	 * */
	if (chdir("/") < 0)
		err_quit("%s: can’t change directory to /", cmd);
	/*
	 * * Close all open file descriptors.
	 * */
	if (rl.rlim_max == RLIM_INFINITY)
		rl.rlim_max = 1024;
	for (i = 0; i < rl.rlim_max; i++)
		close(i);
	/*
	 * * Attach file descriptors 0, 1, and 2 to /dev/null.
	 * */
	fd0 = open("/dev/null", O_RDWR);
	fd1 = dup(0);
	fd2 = dup(0);
	/*
	 * * Initialize the log file.
	 * */
	openlog(cmd, LOG_CONS, LOG_DAEMON);
	if (fd0 != 0 || fd1 != 1 || fd2 != 2) {
		syslog(LOG_ERR, "unexpected file descriptors %d %d %d",
				fd0, fd1, fd2);
		exit(1);
	}

}

/*
 * * Print a message and return to caller.
 * * Caller specifies "errnoflag".
 * */

static void err_doit(int errnoflag, int error, const char *fmt, va_list ap)
{
	char buf[MAXLINE];
	vsnprintf(buf, MAXLINE-1, fmt, ap);
	if (errnoflag)
		snprintf(buf+strlen(buf), MAXLINE-strlen(buf)-1, ": %s",
				strerror(error));
	strcat(buf, "\n");
	fflush(stdout); /* in case stdout and stderr are the same */
	fputs(buf, stderr);
	fflush(NULL); /* flushes all stdio output streams */
}

/*
 * * Nonfatal error related to a system call.
 * * Print a message and return.
 * */

void err_ret(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	err_doit(1, errno, fmt, ap);
	va_end(ap);
}

/*
 * * Fatal error related to a system call.
 * * Print a message and terminate.
 * */

void err_sys(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	err_doit(1, errno, fmt, ap);

	va_end(ap);
	exit(1);
}

/*
 * * Nonfatal error unrelated to a system call.
 * * Error code passed as explict parameter.
 * * Print a message and return.
 * */

void err_cont(int error, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	err_doit(1, error, fmt, ap);
	va_end(ap);
}

/*
 * * Fatal error unrelated to a system call.
 * * Error code passed as explict parameter.
 * * Print a message and terminate.
 * */

void err_exit(int error, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	err_doit(1, error, fmt, ap);
	va_end(ap);
	exit(1);
}

/*
 * * Fatal error related to a system call.
 * * Print a message, dump core, and terminate.
 * */

void err_dump(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	err_doit(1, errno, fmt, ap);
	va_end(ap);
	abort(); /* dump core and terminate */
	exit(1); /* shouldn’t get here */
}

void err_msg(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	err_doit(0, 0, fmt, ap);
	va_end(ap);
}

/*
 * * Fatal error unrelated to a system call.
 * * Print a message and terminate.
 * */

void err_quit(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	err_doit(0, 0, fmt, ap);
	va_end(ap);
	exit(1);
}

