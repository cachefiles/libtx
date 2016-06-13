#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#include "tx_debug.h"

const char * get_debug_format(const char *format)
{
	struct tm *tmp;
	struct timeval tv;
	static char long_format[8192];

	time_t now;
	gettimeofday(&tv, NULL);

	now = tv.tv_sec;
	tmp = localtime(&now);
	snprintf(long_format, sizeof(long_format),
			"%02d:%02d:%02d.%03d %s",
			tmp->tm_hour, tmp->tm_min, tmp->tm_sec, tv.tv_usec/1000, format);
	return long_format;
}

void __tx_check__(int cond, const char *msg, int line, const char *file)
{
	if (cond == 0) {
		fprintf(stderr, "%s %s:%d\n", msg, file, line);
		/* just an warning */
	}
	return;
}

void __tx_panic__(int cond, const char *msg, int line, const char *file)
{
	if (cond == 0) {
		fprintf(stderr, "%s %s:%d\n", msg, file, line);
		exit(-1);
	}
	return;
}
