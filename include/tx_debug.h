#ifndef _TX_DEBUG_H_
#define _TX_DEBUG_H_

#include <assert.h>
#include <stdarg.h>
#define TX_ASSERT assert
#define TX_UNUSED(var) var = var

#define TX_PANIC(cond, msg) __tx_panic__((cond) != 0, msg, __LINE__, __FILE__)
#define TX_CHECK(cond, msg) __tx_check__((cond) != 0, msg, __LINE__, __FILE__)

void __tx_check__(int cond, const char *msg, int line, const char *file);
void __tx_panic__(int cond, const char *msg, int line, const char *file);

#define LOG_TAG_PUTLOG            log_tag_putlog
int log_tag_putlog(const char *tag, const char *fmt, ...);
int log_tag_vputlog(const char *tag, const char *fmt, va_list args);

#define LOG_VERBOSE(fmt, args...) LOG_TAG_PUTLOG("V", fmt, ##args)
#define LOG_DEBUG(fmt, args...)   LOG_TAG_PUTLOG("D", fmt, ##args)
#define LOG_INFO(fmt, args...)    LOG_TAG_PUTLOG("I", fmt, ##args)
#define LOG_WARNING(fmt, args...) LOG_TAG_PUTLOG("W", fmt, ##args)
#define LOG_ERROR(fmt, args...)   LOG_TAG_PUTLOG("E", fmt, ##args)
#define LOG_FATAL(fmt, args...)   LOG_TAG_PUTLOG("F", fmt, ##args)

#endif

