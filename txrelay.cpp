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

#define SOCKS5_FORWARD


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

#define FLAG_UPLOAD     0x01
#define FLAG_DOWNLOAD   0x02
#define FLAG_CONNECTING 0x04
#define FLAG_HANDSHAKE  0x08

struct channel_context {
	int flags;
	int pxy_stat;
	tx_aiocb file;
	tx_aiocb remote;
	tx_task_t task;

	int port;
	in_addr target;
	char domain[128];
	int (*proxy_handshake)(struct channel_context *up);

	int upl, upo;
	char upbuf[8192];

	int downl, downo;
	char downbuf[8192];
};

static void do_channel_release(struct channel_context *up)
{
	int fd;
	tx_aiocb *cb = &up->file;
	tx_outcb_cancel(cb, 0);
	tx_aincb_stop(cb, 0);

	fd = cb->tx_fd;
	tx_aiocb_fini(cb);
	closesocket(fd);

	cb = &up->remote;
	tx_outcb_cancel(cb, 0);
	tx_aincb_stop(cb, 0);

	fd = cb->tx_fd;
	tx_aiocb_fini(cb);
	closesocket(fd);
	delete up;
}

static int g_v5len = 11; 
static unsigned char g_v5req[] = {0x04, 0x01, 0x75, 0x37, 106, 185, 52, 148, 'H', 'H', 0x0};

void set_socks5_proxy(const char *user, const char *passwd, struct tcpip_info *xyinfo)
{
	int idx = 0;
	int len = 0;
	/*
	   4.发送 05 01 00 01 + 目的地址(4字节） + 目的端口（2字节），目的地址和端口都是16进制码（不是字符串！！）。 例202.103.190.27 -7201 则发送的信息为：05 01 00 01 CA 67 BE 1B 1C 21 (CA=202 67=103 BE=190 1B=27 1C21=7201)
	 */

	g_v5req[idx++] = 0x05; 
	g_v5req[idx++] = 0x01; 
	g_v5req[idx++] = 0x00; 
	g_v5req[idx++] = 0x01; 
	memcpy(g_v5req + idx, &xyinfo->address, 4);
	idx += 4;

	memcpy(g_v5req + idx, &xyinfo->port, 2);
	idx += 2;

	g_v5len = idx;
	return;
}

int socks5_connect(char *buf, const char *name, unsigned short port)
{
	int ln;
	char *np;
	/*
	   4.发送 05 01 00 01 + 目的地址(4字节） + 目的端口（2字节），目的地址和端口都是16进制码（不是字符串！！）。 例202.103.190.27 -7201 则发送的信息为：05 01 00 01 CA 67 BE 1B 1C 21 (CA=202 67=103 BE=190 1B=27 1C21=7201)
	 */

	np = buf;
	*np++ = 0x05;
	*np++ = 0x01;
	*np++ = 0x00;
	*np++ = 0x03; // type = domain
	ln = strlen(name);
	*np++ = ln;
	memcpy(np, name, ln);
	np += ln;
	memcpy(np, &port, sizeof(port));
	np += sizeof(port);

	return (np - buf);
}

static int do_socks5_proxy_handshake(struct channel_context *up)
{
	int count;
	int namlen;
	int change = 0;
	char *pbuf = 0;
	tx_aiocb *cb = &up->remote;

#define XYSTAT_SOCKSV5_S1PREPARE 0x01
#define XYSTAT_SOCKSV5_SXPREPARE 0x02
#define XYSTAT_SOCKSV5_S2PREPARE 0x04

#define XYSTAT_SOCKSV5_S1DONE 0x10
#define XYSTAT_SOCKSV5_SXDONE 0x20
#define XYSTAT_SOCKSV5_S2DONE 0x40

	cb = &up->remote;
	/*
	   +----+----+----+----+----+----+----+----+----+----+...+----+
	   | VN | CD | DSTPORT |      DSTIP        | USERID      |NULL|
	   +----+----+----+----+----+----+----+----+----+----+...+----+
	   1    1      2              4           variable       1
	   VN      SOCKS协议版本号，应该是0x04
	   CD      SOCKS命令，可取如下值:
	   0x01    CONNECT
	   0x02    BIND
	   DSTPORT CD相关的端口信息
	   DSTIP   CD相关的地址信息
	   USERID  客户方的USERID
	   NULL    0x00
	 */
	unsigned char v5reqs1[] = {05, 0x01, 0x00};
	unsigned char v5reqs1x[] = {05, 0x02, 0x00, 0x02};

	if (tx_writable(cb) &&
			(up->pxy_stat & XYSTAT_SOCKSV5_S1PREPARE) == 0) {
		memcpy(up->upbuf, v5reqs1, sizeof(v5reqs1));
		up->pxy_stat |= XYSTAT_SOCKSV5_S1PREPARE;
		up->upl = sizeof(v5reqs1);
		up->upo = 0;

		fprintf(stderr, "send socks v5 s1 \n");
	}

next_step:
	/*
	   4.发送 05 01 00 01 + 目的地址(4字节） + 目的端口（2字节），目的地址和端口都是16进制码（不是字符串！！）。 例202.103.190.27 -7201 则发送的信息为：05 01 00 01 CA 67 BE 1B 1C 21 (CA=202 67=103 BE=190 1B=27 1C21=7201)
	 */
	if (tx_writable(cb) &&
			(up->pxy_stat & (XYSTAT_SOCKSV5_S2PREPARE| XYSTAT_SOCKSV5_SXDONE)) == XYSTAT_SOCKSV5_SXDONE) {
		up->pxy_stat |= XYSTAT_SOCKSV5_S2PREPARE;
		if (*up->domain == 0) {
			memcpy(up->upbuf + up->upl, g_v5req, g_v5len);
			up->upl += g_v5len;
		} else {
			namlen = socks5_connect(up->upbuf + up->upl, up->domain, up->port);
			up->upl += namlen;
		}

		fprintf(stderr, "send socks v5 s2 \n");
	}

	pbuf = up->upbuf + up->upo;
	while (tx_writable(cb) && up->upo < up->upl) {
		count = tx_outcb_write(cb, pbuf, up->upl - up->upo);
		if (count > 0) {
			up->upo += count;
			pbuf += count;
		}

		if (!tx_writable(cb)) {
			break;
		}

		if (count == -1 && tx_writable(cb)) {
			fprintf(stderr, "enter handeshake up/out error finish\n");
			return -1;
		}
	}

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

	/* 0x05 0x00 */
	if (!(up->pxy_stat & XYSTAT_SOCKSV5_S1DONE)
			&& up->downl >= 2 && up->downbuf[0] == 0x05) {
		up->pxy_stat |= XYSTAT_SOCKSV5_S1DONE;

		switch (up->downbuf[1]) {
			case 0x00:
				fprintf(stderr, "ok asynmouse supported\n");
				up->pxy_stat |= XYSTAT_SOCKSV5_SXDONE;
				break;

			case 0x02:
				/* prepare user password */
				fprintf(stderr, "failure user password not supported\n");
				return -1;
				break;

			default:
				fprintf(stderr, "failure\n");
				break;
		}

		up->downo += 2;
		goto next_step;
	}

	if ((up->pxy_stat & XYSTAT_SOCKSV5_S2PREPARE)
			&& !(up->pxy_stat & XYSTAT_SOCKSV5_S2DONE)
			&& up->downl - up->downo >= 4 && up->downbuf[up->downo] == 0x05) {
		if (up->downbuf[up->downo + 3] == 0x01
				&& up->downl >= up->downo + 10) {
			switch (up->downbuf[up->downo + 1]) {
				case 0x00:
					fprintf(stderr, "connect socks5 finish\n");
					up->pxy_stat |= XYSTAT_SOCKSV5_S2DONE;
					up->flags &= ~FLAG_HANDSHAKE;
					up->downo += 10;
					return 0;

				default:
					fprintf(stderr, "connect socks5 error: %d\n", up->downbuf[up->downo + 1]);
					return -1;
			}
		}
	}

	if ((up->flags & FLAG_DOWNLOAD) == 0) {
		fprintf(stderr, "socksv4 handshake close unexpected\n");
		return -1;
	}

	if (!tx_readable(cb) && up->downl < sizeof(up->downbuf)) {
		//fprintf(stderr, "download wait read .. %p\n", cb);
		tx_aincb_active(cb, &up->task);
	}

	if (!tx_writable(cb) && up->upo < up->upl) {
		//fprintf(stderr, "upload wait write .. %p\n", cb);
		tx_outcb_prepare(cb, &up->task, 0);
	}

	return 0;
}


static int g_v4len = 11; 
static unsigned char g_v4req[] = {0x04, 0x01, 0x75, 0x37, 106, 185, 52, 148, 'H', 'H', 0x0};

void set_socks4_proxy(const char *ident, struct tcpip_info *xyinfo)
{
	int idx = 0;
	int len = 0;

	g_v4req[idx++] = 0x04; 
	g_v4req[idx++] = 0x01; 
	memcpy(g_v4req + idx, &xyinfo->port, 2);
	idx += 2;

	memcpy(g_v4req + idx, &xyinfo->address, 4);
	idx += 4;

	len = strlen(ident);
	memcpy(g_v4req + idx, ident, len + 1);
	idx += len;

	g_v4len = idx + 1;
	return;
}

static int do_socks4_proxy_handshake(struct channel_context *up)
{
	int count;
	int change = 0;
	char *pbuf = 0;
	tx_aiocb *cb = &up->remote;

#define XYSTAT_SOCKSV4_REQ_SENT 0x01
#define XYSTAT_PREPARE_REQ_SENT 0x04

	cb = &up->remote;
	/*
	   +----+----+----+----+----+----+----+----+----+----+...+----+
	   | VN | CD | DSTPORT |      DSTIP        | USERID      |NULL|
	   +----+----+----+----+----+----+----+----+----+----+...+----+
	   1    1      2              4           variable       1
	   VN      SOCKS协议版本号，应该是0x04
	   CD      SOCKS命令，可取如下值:
	   0x01    CONNECT
	   0x02    BIND
	   DSTPORT CD相关的端口信息
	   DSTIP   CD相关的地址信息
	   USERID  客户方的USERID
	   NULL    0x00
	 */
	if (tx_writable(cb) &&
			(up->pxy_stat & XYSTAT_PREPARE_REQ_SENT) == 0) {
		memcpy(up->upbuf, g_v4req, g_v4len);
		up->pxy_stat |= XYSTAT_PREPARE_REQ_SENT;
		up->upl = g_v4len;
		up->upo = 0;
	}

	pbuf = up->upbuf + up->upo;
	while (tx_writable(cb) && up->upo < up->upl) {
		count = tx_outcb_write(cb, pbuf, up->upl - up->upo);
		if (count > 0) {
			up->upo += count;
			pbuf += count;
		}

		if (!tx_writable(cb)) {
			break;
		}

		if (count == -1 && tx_writable(cb)) {
			fprintf(stderr, "enter handeshake up/out error finish\n");
			return -1;
		}
	}

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

	/*
	   VN CD PORT IP
	   {0x00, 0x5A, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00}
CD:
0x5A forward allow
0x5B forward rejected
0x5C author down
0x5D author failure
	 */

	if (up->downl > 0 && up->downbuf[0] != 0) {
		fprintf(stderr, "socksv4 handshake error cd %x\n", up->downbuf[0]);
		return -1;
	}

	if (up->downl > 1 && up->downbuf[1] != 0x5A) {
		fprintf(stderr, "socksv4 handshake error vn %x\n", up->downbuf[1]);
		return -1;
	}

	if (up->downl >= 8) {
		fprintf(stderr, "socksv4 handshake finish\n");
		up->flags &= ~FLAG_HANDSHAKE;
		up->downo = 8;
	}

	if (up->downl < 8 &&
			(up->flags & FLAG_DOWNLOAD) == 0) {
		fprintf(stderr, "socksv4 handshake close unexpected\n");
		return -1;
	}

	if (!tx_readable(cb) && up->downl < sizeof(up->downbuf)) {
		//fprintf(stderr, "download wait read .. %p\n", cb);
		tx_aincb_active(cb, &up->task);
	}

	if (!tx_writable(cb) && up->upo < up->upl) {
		//fprintf(stderr, "upload wait write .. %p\n", cb);
		tx_outcb_prepare(cb, &up->task, 0);
	}

	return 0;
}

static int g_https_len = 37; 
static unsigned char g_https_req[512] = {"CONNECT www.baidu.com:80 HTTP/1.0\r\n\r\n"};

void set_https_proxy(const char *user, const char *passwd, struct tcpip_info *xyinfo)
{
	int n;
	struct in_addr ia0;
	char *p = (char *)g_https_req;

	memcpy(&ia0, &xyinfo->address, sizeof(ia0));
	n = snprintf(p, sizeof(g_https_req),
			"CONNECT %s:%d HTTP/1.0\r\n\r\n", inet_ntoa(ia0), htons(xyinfo->port));

	fprintf(stderr, "PROXY BLOCK: %s", p);
	g_https_len = n;
	return;
}

static int do_https_proxy_handshake(struct channel_context *up)
{
	int count;
	int change = 0;
	char *pbuf = 0;
	tx_aiocb *cb = &up->remote;

#define XYSTAT_HTTPS_REQ_PREPARE 0x01

	cb = &up->remote;
	if (tx_writable(cb) &&
			(up->pxy_stat & XYSTAT_HTTPS_REQ_PREPARE) == 0) {
		memcpy(up->upbuf, g_https_req, g_https_len);
		up->pxy_stat |= XYSTAT_HTTPS_REQ_PREPARE;
		up->upl = g_https_len;
		up->upo = 0;
	}

	pbuf = up->upbuf + up->upo;
	while (tx_writable(cb) && up->upo < up->upl) {
		count = tx_outcb_write(cb, pbuf, up->upl - up->upo);
		if (count > 0) {
			up->upo += count;
			pbuf += count;
		}

		if (!tx_writable(cb)) {
			break;
		}

		if (count == -1 && tx_writable(cb)) {
			fprintf(stderr, "enter handeshake up/out error finish\n");
			return -1;
		}
	}

	pbuf = up->downbuf + up->downl;
	while (tx_readable(cb) && up->downl < sizeof(up->downbuf) - 1) {
		count = recv(cb->tx_fd, pbuf, sizeof(up->downbuf) - up->downl - 1, 0);
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

	up->downbuf[up->downl] = 0;
	pbuf = strstr(up->downbuf, "\r\n\r\n");

	if (pbuf != NULL) {
		/* should be "HTTP/1.x 200 OK" */
		if (strncasecmp(up->downbuf, "HTTP/1.x", 5) || strncasecmp(up->downbuf + 8, " 200 ", 5)) {
			fprintf(stderr, "https handshake finish: %s\n", up->downbuf);
			return -1;
		}

		up->downo = (pbuf + 4 - up->downbuf);
		up->flags &= ~FLAG_HANDSHAKE;
		return 0;
	}

	if ((up->flags & FLAG_DOWNLOAD) == 0) {
		fprintf(stderr, "https handshake close unexpected\n");
		return -1;
	}

	if (!tx_readable(cb) && up->downl < sizeof(up->downbuf)) {
		//fprintf(stderr, "download wait read .. %p\n", cb);
		tx_aincb_active(cb, &up->task);
	}

	if (!tx_writable(cb) && up->upo < up->upl) {
		//fprintf(stderr, "upload wait write .. %p\n", cb);
		tx_outcb_prepare(cb, &up->task, 0);
	}

	return 0;
}

static int do_proxy_handshake(struct channel_context *up)
{

	if (up->proxy_handshake != NULL)
		return up->proxy_handshake(up);

	fprintf(stderr, "incorrect proxy proto config\n");
	return -1;
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

	if (up->flags & FLAG_HANDSHAKE) {
		int ret = do_proxy_handshake(up);
		if (up->flags & FLAG_HANDSHAKE) return ret;
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

static struct tcpip_info g_target = {0};
static int (*_g_proxy_handshake)(struct channel_context *up) = NULL;

int set_default_relay(const char *relay, const char *user, const char *password)
{

    /* socks5://127.0.0.1:8087 */
    if (strncmp(relay, "socks5://", 9) == 0) {
        get_target_address(&g_target, relay + 9);
        _g_proxy_handshake = do_socks5_proxy_handshake;
        return 0;
    }

    /* socks4://127.0.0.1:8087 */
    if (strncmp(relay, "socks4://", 9) == 0) {
        get_target_address(&g_target, relay + 9);
        _g_proxy_handshake = do_socks4_proxy_handshake;
        return 0;
    }

    /* socks://127.0.0.1:8087 */
    if (strncmp(relay, "socks://", 8) == 0) {
        get_target_address(&g_target, relay + 8);
        _g_proxy_handshake = do_socks5_proxy_handshake;
        return 0;
    }

    /* https://127.0.0.1:8087 */
    if (strncmp(relay, "https://", 8) == 0) {
        get_target_address(&g_target, relay + 8);
        _g_proxy_handshake = do_https_proxy_handshake;
        return 0;
    }

    if (strstr(relay, "://") == NULL) {
        get_target_address(&g_target, relay);
        _g_proxy_handshake = do_https_proxy_handshake;
        return 0;
    }

    fprintf(stderr, "unkown relay: %s\n", relay);
    return 0;
}

static void do_channel_prepare(struct channel_context *up, int newfd, const char *name, unsigned short port)
{
	int peerfd, error;
	struct sockaddr sa0;
	struct sockaddr_in sin0;
	tx_loop_t *loop = tx_loop_default();

	tx_aiocb_init(&up->file, loop, newfd);
	tx_task_init(&up->task, loop, do_channel_wrapper, up);
	tx_task_active(&up->task);

	up->port = port;
	up->domain[0] = 0;
	if (name != NULL)
		strcpy(up->domain, name);
	tx_setblockopt(newfd, 0);

	peerfd = socket(AF_INET, SOCK_STREAM, 0);

	memset(&sa0, 0, sizeof(sa0));
	sa0.sa_family = AF_INET;
	error = bind(peerfd, &sa0, sizeof(sa0));

	tx_setblockopt(peerfd, 0);
	tx_aiocb_init(&up->remote, loop, peerfd);

    
	sin0.sin_family = AF_INET;
	sin0.sin_port   = g_target.port;
	sin0.sin_addr.s_addr = g_target.address;
	tx_aiocb_connect(&up->remote, (struct sockaddr *)&sin0, &up->task);

	up->upl = up->upo = 0;
	up->downl = up->downo = 0;
	up->pxy_stat = 0;
	up->proxy_handshake = NULL;
	up->flags = (FLAG_UPLOAD| FLAG_DOWNLOAD| FLAG_CONNECTING);

    if (_g_proxy_handshake != NULL) {
        up->proxy_handshake = _g_proxy_handshake;
        up->flags |= FLAG_HANDSHAKE;
    }

	fprintf(stderr, "newfd: %d to here\n", newfd);
	return;
}

struct listen_context {
    int flags;
	unsigned int port;

	tx_aiocb file;
	tx_task_t task;
};

const char *get_unwrap_name(unsigned int addr);

static void do_listen_accepted(void *up)
{
	const char *name;
	struct listen_context *lp0;
	struct channel_context *cc0;
	union { struct sockaddr sa; struct sockaddr_in si; } local;

	lp0 = (struct listen_context *)up;

	int newfd = tx_listen_accept(&lp0->file, NULL, NULL);
	TX_PRINT(TXL_DEBUG, "new fd: %d\n", newfd);
	tx_listen_active(&lp0->file, &lp0->task);

	if (newfd != -1) {
		cc0 = new channel_context;
		if (cc0 == NULL) {
			TX_CHECK(cc0 != NULL, "new channel_context failure\n");
			closesocket(newfd);
			return;
		}

		int error;
		socklen_t salen = sizeof(local);
		error = getsockname(newfd, &local.sa, &salen);
		if (error == 0 && (lp0->flags & DYNAMIC_TRANSLATE)) {
			name = get_unwrap_name(local.si.sin_addr.s_addr);
			TX_PRINT(TXL_DEBUG, "client connect to %s\n", name);
			do_channel_prepare(cc0, newfd, name, lp0->port);
			return;
		}

		do_channel_prepare(cc0, newfd, NULL, lp0->port);
	}

	return;
}

void * txlisten_create(struct tcpip_info *info)
{
	int fd;
	int err;
	int option = 1;
	tx_loop_t *loop;
	struct sockaddr_in sa0;
	struct listen_context *up;

	fd = socket(AF_INET, SOCK_STREAM, 0);

	tx_setblockopt(fd, 0);

	setsockopt(fd,SOL_SOCKET, SO_REUSEADDR, (char*)&option,sizeof(option));

	sa0.sin_family = AF_INET;
	sa0.sin_port   = info->port;
	sa0.sin_addr.s_addr = info->address;

	err = bind(fd, (struct sockaddr *)&sa0, sizeof(sa0));
	assert(err == 0);

	err = listen(fd, 5);
	assert(err == 0);

	loop = tx_loop_default();
	up = new listen_context();
	up->port = info->port;
	tx_listen_init(&up->file, loop, fd);
	tx_task_init(&up->task, loop, do_listen_accepted, up);
	tx_listen_active(&up->file, &up->task);

	return up;
}

void * txlisten_addflags(void *up, int flags)
{
	struct listen_context *upp;
    upp = (struct listen_context *)up;
    upp->flags |= flags;
    return up;
}

void * txlisten_addredir(void *up, char const * redir)
{
	struct listen_context *upp;
    upp = (struct listen_context *)up;
    fprintf(stderr, "redir currently not supported.\n");
    return up;
}

void * txlisten_setport(void *up, int port)
{
	struct listen_context *upp;
    upp = (struct listen_context *)up;
    upp->port = port;
    return up;
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
		} else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
			get_target_address(&listen_address, argv[i + 1]);
			txlisten_create(&listen_address);
			i++;
		} else {
			get_target_address(&g_target, argv[i]);
			continue;
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

