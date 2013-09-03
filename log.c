#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <syslog.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

#include "log.h"

int do_syslog;

void mylog_open(int sl, char *logname) {
	do_syslog = sl;
	if (sl) {
		setlogmask(LOG_UPTO (LOG_NOTICE));
		openlog(logname, LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);
	}
}

void mylog(char *str, ...) {
        va_list arg;
        va_start(arg,str);
	if (do_syslog) {
                vsyslog(LOG_NOTICE, str, arg);
	} else {
                vfprintf(stderr, str, arg);
		fputc('\n', stderr);
	}
        va_end(arg);
}
