#ifndef _TX_DEBUG_H_
#define _TX_DEBUG_H_

#include <assert.h>
#define TX_ASSERT assert
#define TX_UNUSED(var) var = var

#define TX_PANIC(cond, msg) __tx_panic__((cond) != 0, msg, __LINE__, __FILE__)
#define TX_CHECK(cond, msg) __tx_check__((cond) != 0, msg, __LINE__, __FILE__)

void __tx_check__(int cond, const char *msg, int line, const char *file);
void __tx_panic__(int cond, const char *msg, int line, const char *file);

enum {TXL_ERROR, TXL_WARN, TXL_MESSAGE, TXL_DEBUG, TXL_VERBOSE};
const char *get_debug_format(const char *append);
#define TX_PRINT(level, format, args...) fprintf (stderr, get_debug_format("<%d>: " format "\n"), level, ##args)

#endif

