#include <stdio.h>
#include <stdlib.h>

#include "tx_debug.h"

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
