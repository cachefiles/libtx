#ifndef _TX_DEBUG_H_
#define _TX_DEBUG_H_

#include <assert.h>

#define TX_PANIC(cond, msg)
#define TX_CHECK(cond, msg)
#define TX_PRINT(level, msg)

enum {TXL_ERROR, TXL_WARN, TXL_MESSAGE, TXL_DEBUG, TXL_VERBOSE};

#define TX_ASSERT assert
#endif

