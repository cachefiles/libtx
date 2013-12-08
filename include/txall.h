#ifndef _TXALL_H_
#define _TXALL_H_

#include <tx_loop.h>
#include <tx_poll.h>

#include <tx_aiocb.h>
#include <tx_timer.h>
#include <tx_platform.h>

#include <tx_debug.h>

struct module_stub {
	void (* init)(void);
	void (* clean)(void);
};

void initialize_modules(struct module_stub *list[]);
void cleanup_modules(struct module_stub *list[]);

void init_stub(void);
void clean_stub(void);

#endif

