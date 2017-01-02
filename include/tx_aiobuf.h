#ifndef _TX_AIOBUF_
#define _TX_AIOBUF_

#include <libtx/queue.h>

struct tx_membuf {
	int iob_use;
	void *iob_alloc;
};

struct tx_aiobuf {
	char *iob_buf;
	size_t iob_len;
	tx_membuf *iob_base;
};

#endif

