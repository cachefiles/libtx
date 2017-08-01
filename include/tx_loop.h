#ifndef _TX_LOOP_H_
#define _TX_LOOP_H_

#if defined(WIN32) && defined(SLIST_ENTRY)
#include <libtx/queue.h>
#else
#include <libtx/queue.h>
#endif


#define TASK_IDLE 0x1
#define TASK_BUSY 0x2
#define TASK_PENDING 0x4
#define TASK_USER_MARK 0x8

struct tx_poll_t;

struct tx_task_t {
	int tx_flags;
	const void *tx_reason;
	void *tx_data;
	void (*tx_call)(void *ctx);
	struct tx_loop_t *tx_loop;
	LIST_ENTRY(tx_task_t) entries;
};

#define MAX_STACK_DEPTH 10

#define STACK_WAIT_VALUE 0
#define STACK_NONE_VALUE 1
#define STACK_CODE_VALUE 2

struct tx_task_ball_t {
	int tx_uflag;
	void *tx_data;
	void (*tx_call)(void *ctx, struct tx_task_stack_t *ts);
};

struct tx_task_stack_t {
	int tx_top;
	int tx_flag;
	int tx_code;
	int tx_uflag;
	tx_task_t tx_sched;
	tx_task_ball_t tx_balls[MAX_STACK_DEPTH];
};

LIST_HEAD(tx_task_q, tx_task_t);

struct tx_loop_t {
	int tx_busy;
	int tx_stop;
	int tx_break;
	int tx_actives;
	int tx_upcount;
	void *tx_holder;
	tx_poll_t *tx_poller;
	tx_task_q tx_taskq;
	tx_task_t tx_tailer;
	tx_task_t *tx_current;
};

struct tx_loop_t *tx_loop_new(void);
struct tx_loop_t *tx_loop_default(void);
struct tx_loop_t *tx_loop_get(tx_task_t *task);

int  tx_loop_timeout(tx_loop_t *up, const void *verify);
void tx_loop_delete(tx_loop_t *up);
void tx_loop_break(tx_loop_t *up);
void tx_loop_main(tx_loop_t *up);
void tx_loop_stop(tx_loop_t *up);

void tx_task_init(tx_task_t *task, tx_loop_t *loop, void (*call)(void *), void *ctx);
void tx_task_active(tx_task_t *task, const void *reason);
void tx_task_drop(tx_task_t *task);
void tx_task_mark(tx_task_t *task);

#define tx_task_idle(t) ((t)->tx_flags & TASK_IDLE)
#define tx_task_ismark(t) ((t)->tx_flags & TASK_USER_MARK)

void tx_task_record(tx_task_q *taskq, tx_task_t *task);
void tx_task_wakeup(tx_task_q *taskq, const void *byevent);

void tx_task_stack_init(tx_task_stack_t *stack, tx_loop_t *loop);
void tx_task_stack_push(tx_task_stack_t *stack, void (*call)(void *, tx_task_stack_t *), void *ctx);
void tx_task_stack_raise(tx_task_stack_t *stack, const void *reason);

void tx_task_stack_pop1(tx_task_stack_t *stack, int code);
void tx_task_stack_pop0(tx_task_stack_t *stack);
void tx_task_stack_drop(tx_task_stack_t *stack);

#define tx_task_stack_active(s, r) tx_task_active(&(s)->tx_sched, r)

struct tx_iocb_t;
struct tx_wait_t {
	int tx_flag;
	tx_task_t *tx_task;
	tx_iocb_t *tx_iocb;
	LIST_ENTRY(tx_wait_t) entries;
};

#define WAIT_IDLE 0x1
typedef LIST_HEAD(tx_wait_q, tx_wait_t) tx_wait_q;

struct tx_iocb_t {
	int tx_flag;
	tx_wait_q tx_waitq;
};

int  tx_wait_init(tx_wait_t *wcbp, tx_iocb_t *iocbp, tx_task_t *task);
int  tx_wait_active(tx_wait_t *wcbp);
int  tx_wait_cancel(tx_wait_t *wcbp);

#define tx_taskq_init(q) LIST_INIT(q)
#define tx_taskq_empty(q) LIST_EMPTY(q)

#endif
