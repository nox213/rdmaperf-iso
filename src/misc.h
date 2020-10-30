#ifndef __MISC__H

#define __MISC__H

/*codes from APUE */

#include <sys/types.h> /* some systems still require this */
#include <sys/stat.h>
#include <sys/resource.h>
#include <stdio.h> /* for convenience */
#include <stdlib.h> /* for convenience */
#include <stddef.h> /* for offsetof */
#include <string.h> /* for convenience */
#include <unistd.h> /* for convenience */
#include <signal.h> /* for SIG_ERR */
#include <syslog.h>
#include <fcntl.h>

void daemonize(const char *cmd);

void err_msg(const char *, ...); 
void err_dump(const char *, ...) __attribute__((noreturn));
void err_quit(const char *, ...) __attribute__((noreturn));
void err_cont(int, const char *, ...);
void err_exit(int, const char *, ...) __attribute__((noreturn));
void err_ret(const char *, ...);
void err_sys(const char *, ...) __attribute__((noreturn));

#endif
