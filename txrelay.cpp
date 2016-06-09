#include <ctype.h>
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
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#define closesocket close
#endif

#include "txall.h"
#include "txconfig.h"

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
	//fprintf(stderr, "update_timer %d\n", tx_ticks);
	return;
}

int load_config(const char *path);
int txdns_create(struct tcpip_info *, struct tcpip_info *);

int main(int argc, char *argv[])
{
	int err;
	struct timer_task tmtask;
	struct uptick_task uptick;

	struct tcpip_info relay_address = {0};
	struct tcpip_info listen_address = {0};

#ifndef WIN32
	signal(SIGPIPE, SIG_IGN);
#endif

	unsigned int last_tick = 0;
	tx_loop_t *loop = tx_loop_default();
	tx_poll_t *poll = tx_epoll_init(loop);
	tx_poll_t *poll1 = tx_kqueue_init(loop);
	tx_poll_t *poll2 = tx_completion_port_init(loop);
	tx_timer_ring *provider = tx_timer_ring_get(loop);

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0) {
			fprintf(stderr, "%s [options] <PROXY-ADDRESS>!\n", argv[0]);
			fprintf(stderr, "-h print this help!\n");
			fprintf(stderr, "-s <RELAY-PROXY> socks4 proxy address!\n");
			fprintf(stderr, "-d <BIND> <REMOTE> socks4 proxy address!\n");
			fprintf(stderr, "-l <LISTEN-ADDRESS> listening tcp address!\n");
			fprintf(stderr, "-f path to config file!\n");
			fprintf(stderr, "all ADDRESS should use this format <HOST:PORT> OR <PORT>\n");
			fprintf(stderr, "\n");
			return 0;
		} else if (strcmp(argv[i], "-d") == 0 && i + 2 < argc) {
			struct tcpip_info local = {0};
			struct tcpip_info fake = {0};
			struct tcpip_info remote = {0};
			get_target_address(&local, argv[i + 1]);
			i++;
			get_target_address(&remote, argv[i + 1]);
			i++;
			txdns_create(&local, &remote);
		} else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
			load_config(argv[i + 1]);
			i++;
		} else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
			get_target_address(&relay_address, argv[i + 1]);
			i++;
		}
	}

	uptick.ticks = 0;
	uptick.last_ticks = tx_getticks();
	tx_task_init(&uptick.task, loop, update_tick, &uptick);
	tx_task_active(&uptick.task);

	tx_timer_init(&tmtask.timer, loop, &tmtask.task);
	tx_task_init(&tmtask.task, loop, update_timer, &tmtask);
	tx_timer_reset(&tmtask.timer, 500);

#if 0
	set_socks4_proxy("hello", &relay_address);
	set_socks5_proxy("user", "password", &relay_address);
	set_https_proxy("user", "password", &relay_address);
#endif

	tx_loop_main(loop);

	tx_timer_stop(&tmtask.timer);
	tx_loop_delete(loop);

	TX_UNUSED(last_tick);

	return 0;
}

