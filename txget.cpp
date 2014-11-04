#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#ifdef WIN32
#include <windows.h>
#else
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include "txall.h"

#define STDIN_FILE_FD 0
#define FAILURE_SAFEEXIT(cond, fmt, args...) do { if ((cond) == 0) break; fprintf(stderr, fmt, args); exit(0); } while ( 0 )

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
		TX_PRINT(TXL_VERBOSE, "tx_getticks: %u %d", ticks, uptick->ticks);
		uptick->last_ticks = ticks;
	}

	if (uptick->ticks < 100) {
		tx_task_active(&uptick->task);
		uptick->ticks++;
		return;
	}

	TX_PRINT(TXL_VERBOSE, "all update_tick finish");
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
	TX_PRINT(TXL_VERBOSE, "update_timer %d", tx_ticks);
	return;
}

struct stdio_task {
	int fd;
	int sent;
	tx_aiocb file;
	tx_task_t task;
};

static char _g_host[256];
static char _g_path[256];

static void update_stdio(void *up)
{
	int len;
	char buf[8192 * 4];
	struct stdio_task *tp;
	tp = (struct stdio_task *)up;

	if (tp->sent == 0) {
		sprintf(buf, "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", _g_path, _g_host);
		TX_PRINT(TXL_VERBOSE, "send http request:\n%s", buf);
		len = tx_outcb_write(&tp->file, buf, strlen(buf));
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
			TX_PRINT(TXL_VERBOSE, "reach end of file, stop the loop");
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
	int port;
	int flags;
	int error;
	char domain[256];
	const char *p, *line;
	struct hostent *host;
	struct sockaddr_in sa;

	if (strncmp(url, "http://", 7) != 0) {
		TX_PRINT(TXL_VERBOSE, "get_url_socket failure");
		return -1;
	}

	port = 80;
	line = &url[7];
	p = strchr(line, '/');
	if (p != NULL && (p - line) < sizeof(domain)) {
		memcpy(domain, line, p - line);
		domain[p - line] =  0;
		strcpy(_g_path, p);
	} else {
		strncpy(domain, line, sizeof(domain));
		domain[sizeof(domain) - 1] =  0;
		strcpy(_g_path, "/");
	}

	strcpy(_g_host, domain);

	p = strchr(domain, ':');
	if (p != NULL) {
		port = atoi(p + 1);
		*(char *)p = 0;
	}

	sa.sin_family = AF_INET;
	sa.sin_port	  = htons(port);
	sa.sin_addr.s_addr = INADDR_ANY;

	if (inet_aton(domain, &sa.sin_addr) == 0) {
		host = gethostbyname(domain);
		FAILURE_SAFEEXIT(host == NULL, "HostName Error:%s\n", hstrerror(h_errno));
		sa.sin_addr = *(struct in_addr *)(host->h_addr_list[0]);
	}

	fd = socket(AF_INET, SOCK_STREAM, 0);
	error = tx_setblockopt(fd, 0);

	error = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
	TX_PRINT(TXL_VERBOSE, "connect error %d:%d", error, errno);

	return fd;
}

int main(int argc, char *argv[])
{
	struct timer_task tmtask;
	struct stdio_task iotest;
	struct uptick_task uptick;
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

	int fd = get_url_socket(argv[1]);

	iotest.fd = fd;
	iotest.sent = 0;
	tx_aiocb_init(&iotest.file, loop, fd);
	tx_task_init(&iotest.task, loop, update_stdio, &iotest);
	tx_outcb_prepare(&iotest.file, &iotest.task, 0);
	tx_aincb_active(&iotest.file, &iotest.task);

	tx_loop_main(loop);

	tx_aincb_stop(&iotest.file, &iotest.task);
	tx_outcb_cancel(&iotest.file, &iotest.task);
	tx_timer_stop(&tmtask.timer);
	tx_aiocb_fini(&iotest.file);
#ifdef WIN32
	closesocket(STDIN_FILE_FD);
#else
	close(STDIN_FILE_FD);
#endif
	tx_loop_delete(loop);

	TX_UNUSED(last_tick);
	TX_UNUSED(provider2);
	TX_UNUSED(provider1);

	close(fd);

	return 0;
}

