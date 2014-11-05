#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#ifdef WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#define closesocket close
#endif

#include "txall.h"

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

#define FLAG_UPLOAD     0x01
#define FLAG_DOWNLOAD   0x02
#define FLAG_CONNECTING 0x04

struct channel_context {
	int flags;
	tx_aiocb file;
	tx_aiocb remote;
	tx_task_t task;

	int upl, upo;
	char upbuf[8192];

	int downl, downo;
	char downbuf[8192];
};

static void do_channel_release(struct channel_context *up)
{
	tx_aiocb *cb = &up->file;
	tx_outcb_cancel(cb, 0);
	tx_aincb_stop(cb, 0);
	tx_aiocb_fini(cb);
	closesocket(cb->tx_fd);

	cb = &up->remote;
	tx_outcb_cancel(cb, 0);
	tx_aincb_stop(cb, 0);
	closesocket(cb->tx_fd);
	tx_aiocb_fini(cb);
	delete up;
}

static int do_channel_poll(struct channel_context *up)
{
	int count;
	char *pbuf;
	int change = 0;
	tx_aiocb *cb = &up->file;

	if (!tx_writable(&up->remote) &&
			(up->flags & FLAG_CONNECTING)) {
		fprintf(stderr, "waiting connect...");
		return 0;
	} else {
		up->flags &= ~FLAG_CONNECTING;
	}

	do {
		change = 0;

		cb = &up->file;
		pbuf = up->upbuf + up->upl;
		while (tx_readable(cb) && up->upl < sizeof(up->upbuf)) {
			count = recv(cb->tx_fd, pbuf, sizeof(up->upbuf) - up->upl, 0);
			tx_aincb_update(cb, count);

			if (!tx_readable(cb)) {
				break;
			}

			if (count == 0) {
				up->flags &= ~FLAG_UPLOAD;
				break;
			}

			if (count == -1) {
				fprintf(stderr, "enter up/in error finish\n");
				return -1;
			}

			up->upl += count;
			pbuf += count;
		}

		cb = &up->remote;
		pbuf = up->upbuf + up->upo;
		while (tx_writable(cb) && up->upo < up->upl) {
			count = tx_outcb_write(cb, pbuf, up->upl - up->upo);
			if (count > 0) {
				up->upo += count;
				pbuf += count;
				change = 1;
			}

			if (!tx_writable(cb)) {
				break;
			}

			if (count == -1 && tx_writable(cb)) {
				fprintf(stderr, "enter up/out error finish\n");
				return -1;
			}

			change = 1;
		}

		if (up->upo == up->upl) {
			up->upl = 0;
			up->upo = 0;
		}

	} while (change);

	do {
		change = 0;

		cb = &up->remote;
		pbuf = up->downbuf + up->downl;
		while (tx_readable(cb) && up->downl < sizeof(up->downbuf)) {
			count = recv(cb->tx_fd, pbuf, sizeof(up->downbuf) - up->downl, 0);
			tx_aincb_update(cb, count);

			if (!tx_readable(cb)) {
				break;
			}

			if (count == 0) {
				up->flags &= ~FLAG_DOWNLOAD;
				break;
			}

			if (count == -1) {
				fprintf(stderr, "enter down/in error finish\n");
				return -1;
			}

			up->downl += count;
			pbuf += count;
		}

		cb = &up->file;
		pbuf = up->downbuf + up->downo;
		while (tx_writable(cb) && up->downo < up->downl) {
			count = tx_outcb_write(cb, pbuf, up->downl - up->downo);
			if (count > 0) {
				up->downo += count;
				pbuf += count;
				change = 1;
			}

			if (!tx_writable(cb)) {
				break;
			}

			if (count == -1 && tx_writable(cb)) {
				fprintf(stderr, "enter down/out error finish\n");
				return -1;
			}

			change = 1;
		}

		if (up->downo == up->downl) {
			up->downo = 0;
			up->downl = 0;
		}

	} while (change);

	if (up->downo == up->downl &&
		up->upl == up->upo && 0 == (up->flags & (FLAG_UPLOAD| FLAG_DOWNLOAD))) {
		fprintf(stderr, "nomalize finish\n");
		return -1;
	}

	cb = &up->file;
	if (!tx_readable(cb) && up->upl < sizeof(up->upbuf)) {
		//fprintf(stderr, "upload wait read .. %p\n", cb);
		tx_aincb_active(cb, &up->task);
	}

	if (!tx_writable(cb) && up->downo < up->downl) {
		//fprintf(stderr, "download wait write .. %p\n", cb);
		tx_outcb_prepare(cb, &up->task, 0);
	}

	if ((up->flags & FLAG_DOWNLOAD) == 0 && up->downo == up->downl) {
		//fprintf(stderr, "shutdown download: %d\n", cb->tx_fd);
		shutdown(cb->tx_fd, SHUT_WR);
	}

	cb = &up->remote;
	if (!tx_readable(cb) && up->downl < sizeof(up->downbuf)) {
		//fprintf(stderr, "download wait read .. %p\n", cb);
		tx_aincb_active(cb, &up->task);
	}

	if (!tx_writable(cb) && up->upo < up->upl) {
		//fprintf(stderr, "upload wait write .. %p\n", cb);
		tx_outcb_prepare(cb, &up->task, 0);
	}

	if ((up->flags & FLAG_UPLOAD) == 0 && up->upl == up->upo) {
		//fprintf(stderr, "shutdown upload: %d\n", cb->tx_fd);
		shutdown(cb->tx_fd, SHUT_WR);
	}

#if 0
	fprintf(stderr, "%p upstat %d %d r%x w%x\n",
			&up->remote, up->upo, up->upl, tx_readable(&up->file), tx_writable(&up->remote));
	fprintf(stderr, "%p downstat %d %d r%x w%x\n",
			&up->file, up->downo, up->downl, tx_readable(&up->remote), tx_writable(&up->file));
#endif
	return 0;
}

static void do_channel_wrapper(void *up)
{
	int err;
	struct channel_context *upp;

	upp = (struct channel_context *)up;
	err = do_channel_poll(upp);

	if (err != 0) {
		do_channel_release(upp);
		return;
	}

	return;
}

static int target_port = 30007;
static const char *target_host = "127.0.0.1";

static void do_channel_prepare(struct channel_context *up, int newfd)
{
	int peerfd, error;
	struct sockaddr sa0;
	struct sockaddr_in sin0;
	tx_loop_t *loop = tx_loop_default();

	tx_aiocb_init(&up->file, loop, newfd);
	tx_task_init(&up->task, loop, do_channel_wrapper, up);
	tx_task_active(&up->task);

	tx_setblockopt(newfd, 0);

	peerfd = socket(AF_INET, SOCK_STREAM, 0);

	memset(&sa0, 0, sizeof(sa0));
	sa0.sa_family = AF_INET;
	error = bind(peerfd, &sa0, sizeof(sa0));

	tx_setblockopt(peerfd, 0);
	tx_aiocb_init(&up->remote, loop, peerfd);
	sin0.sin_family = AF_INET;
	sin0.sin_port   = htons(target_port);
	sin0.sin_addr.s_addr = inet_addr(target_host);
	tx_aiocb_connect(&up->remote, (struct sockaddr *)&sin0, &up->task);

	up->upl = up->upo = 0;
	up->downl = up->downo = 0;
	up->flags = (FLAG_UPLOAD| FLAG_DOWNLOAD| FLAG_CONNECTING);

	fprintf(stderr, "newfd: %d to here\n", newfd);
	return;
}

struct listen_context {
	tx_aiocb file;
	tx_task_t task;
};

static void do_listen_accepted(void *up)
{
	struct listen_context *lp0;
	struct channel_context *cc0;
	lp0 = (struct listen_context *)up;

	int newfd = tx_listen_accept(&lp0->file, NULL, NULL);
	fprintf(stderr, "new fd: %d\n", newfd);
	tx_listen_active(&lp0->file, &lp0->task);

	if (newfd != -1) {
		cc0 = new channel_context;
		if (cc0 == NULL) {
			fprintf(stderr, "failure\n");
			closesocket(newfd);
			return;
		}

		do_channel_prepare(cc0, newfd);
	}

	return;
}

static void listen_init(struct listen_context *up)
{
	int fd;
	int err;
	int option = 1;
	tx_loop_t *loop;
	struct sockaddr_in sa0;

	fd = socket(AF_INET, SOCK_STREAM, 0);

	tx_setblockopt(fd, 0);

	setsockopt(fd,SOL_SOCKET, SO_REUSEADDR, (char*)&option,sizeof(option));

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

#ifndef WIN32
	signal(SIGPIPE, SIG_IGN);
#endif
	if (argc > 1) target_host = argv[1];
	if (argc > 2) target_port = atoi(argv[2]);

	unsigned int last_tick = 0;
	tx_loop_t *loop = tx_loop_default();
	tx_poll_t *poll = tx_epoll_init(loop);
	tx_poll_t *poll1 = tx_completion_port_init(loop);
	tx_timer_ring *provider = tx_timer_ring_get(loop);

	uptick.ticks = 0;
	uptick.last_ticks = tx_getticks();
	tx_task_init(&uptick.task, loop, update_tick, &uptick);
	tx_task_active(&uptick.task);

	tx_timer_init(&tmtask.timer, loop, &tmtask.task);
	tx_task_init(&tmtask.task, loop, update_timer, &tmtask);
	tx_timer_reset(&tmtask.timer, 500);

	listen_init(&listen0);

	tx_loop_main(loop);

	tx_timer_stop(&tmtask.timer);
	tx_loop_delete(loop);

	TX_UNUSED(last_tick);

	return 0;
}

