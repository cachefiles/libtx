#include <stdio.h>
#include <errno.h>
#include <string.h>

#ifdef WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#define closesocket close
#endif

#include "txall.h"

#define STDIN_FILE_FD 0

struct uptick_task {
	int ticks;
	tx_task_t task;
	unsigned int last_ticks;
};

static void update_tick(void *up)
{
	struct uptick_task *uptick;
	unsigned int ticks = tx_ticks;

	uptick = (struct uptick_task *)up;

	if (ticks != uptick->last_ticks) {
		//fprintf(stderr, "tx_getticks: %u %d\n", ticks, uptick->ticks);
		uptick->last_ticks = ticks;
	}

	if (uptick->ticks < 100) {
		tx_task_active(&uptick->task);
		uptick->ticks++;
		return;
	}

	fprintf(stderr, "all update_tick finish\n");
#if 0
	tx_loop_stop(tx_loop_get(&uptick->task));
	fprintf(stderr, "stop the loop\n");
#endif
	return;
}

struct timer_task {
	tx_task_t task;
	tx_timer_t timer;
};

static void update_timer(void *up)
{
	struct timer_task *ttp;
	ttp = (struct timer_task*)up;

	tx_timer_reset(&ttp->timer, 50000);
	fprintf(stderr, "update_timer %d\n", tx_ticks);
	return;
}

struct stdio_task {
	int fd;
	int sent;
	tx_aiocb file;
	tx_task_t task;
};

static void update_stdio(void *up)
{
	int len;
	char buf[8192 * 4];
	struct stdio_task *tp;
	tp = (struct stdio_task *)up;

	if (tp->sent == 0) {
		fprintf(stderr, "send http request\n");
		strcpy(buf, "GET / HTTP/1.0\r\nHost: www.baidu.com\r\n\r\n");
		len = send(tp->fd, buf, strlen(buf), 0);
		tp->sent = 1;

		if (!tx_readable(&tp->file)) {
			tx_aincb_active(&tp->file, &tp->task);
			return;
		}
	}

	for ( ; ; ) {
#ifndef WIN32
		len = read(tp->fd, buf, sizeof(buf));
#else
		len = recv(tp->fd, buf, sizeof(buf), 0);
#endif
		tx_aincb_update(&tp->file, len);
		if (!tx_readable(&tp->file)) {
			tx_aincb_active(&tp->file, &tp->task);
			break;
		}

		if (len <= 0) {
			fprintf(stderr, "reach end of file, stop the loop\n");
			tx_loop_stop(tx_loop_get(&tp->task));
			break;
		}

		fwrite(buf, len, 1, stdout);
	}

	return;
}

int get_url_socket(const char *url)
{
	int fd;
	int flags;
	int error;
	struct sockaddr_in sa;

	sa.sin_family = AF_INET;
	sa.sin_port	  = htons(80);
	sa.sin_addr.s_addr = inet_addr("115.239.210.27");

	fd = socket(AF_INET, SOCK_STREAM, 0);
#ifndef WIN32
	flags = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
#endif

	error = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
	fprintf(stderr, "connect error %d:%d\n", error, errno);

	return fd;
}

struct listen_context {
	tx_aiocb file;
	tx_task_t task;
};

static void do_listen_accepted(void *up)
{
	struct listen_context *lp0;
	lp0 = (struct listen_context *)up;

	int newfd = tx_listen_accept(&lp0->file, NULL, NULL);
	fprintf(stderr, "new fd: %d\n", newfd);
	closesocket(newfd);

	tx_listen_active(&lp0->file, &lp0->task);
	return;
}

static void init_listen(struct listen_context *up)
{
	int fd;
	int err;
	tx_loop_t *loop;
	struct sockaddr_in sa0;

	fd = socket(AF_INET, SOCK_STREAM, 0);

	sa0.sin_family = AF_INET;
	sa0.sin_port   = htons(30008);
	sa0.sin_addr.s_addr = htonl(0);

	err = bind(fd, (struct sockaddr *)&sa0, sizeof(sa0));
	assert(err == 0);

	err = listen(fd, 5);
	assert(err == 0);

	loop = tx_loop_default();
	tx_listen_init(&up->file, loop, fd);
	tx_task_init(&up->task, loop, do_listen_accepted, up);
	tx_listen_active(&up->file, &up->task);

	return;
}

int main(int argc, char *argv[])
{
	int err;
	struct timer_task tmtask;
	struct uptick_task uptick;
	struct listen_context listen0;

	unsigned int last_tick = 0;
	tx_loop_t *loop = tx_loop_default();
	tx_poll_t *poll = tx_epoll_init(loop);
	tx_poll_t *poll1 = tx_completion_port_init(loop);
	tx_timer_ring *provider = tx_timer_ring_get(loop);
	tx_timer_ring *provider1 = tx_timer_ring_get(loop);
	tx_timer_ring *provider2 = tx_timer_ring_get(loop);

	TX_CHECK(provider1 == provider, "timer provider not equal");
	TX_CHECK(provider2 == provider, "timer provider not equal");

	uptick.ticks = 0;
	uptick.last_ticks = tx_getticks();
	tx_task_init(&uptick.task, loop, update_tick, &uptick);
	tx_task_active(&uptick.task);

	tx_timer_init(&tmtask.timer, loop, &tmtask.task);
	tx_task_init(&tmtask.task, loop, update_timer, &tmtask);
	tx_timer_reset(&tmtask.timer, 500);

	init_listen(&listen0);

	tx_loop_main(loop);

	tx_timer_stop(&tmtask.timer);
#ifdef WIN32
	closesocket(STDIN_FILE_FD);
#else
	close(STDIN_FILE_FD);
#endif
	tx_loop_delete(loop);

	TX_UNUSED(last_tick);
	TX_UNUSED(provider2);
	TX_UNUSED(provider1);

	return 0;
}

