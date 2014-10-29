#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <fstream>
#include <iostream>
#include <winsock2.h>

#include "txall.h"
#include "ncatutil.h"

#define P_WRITE(p, b, l) ((p)->p_ops->p_write)((p)->p_upp, b, l)
#define P_READ(p, b, l)  ((p)->p_ops->p_read)((p)->p_upp, b, l)
#define P_CLOSE(p)       ((p)->p_ops->p_close)((p)->p_upp)
#define P_BREAK(c)		 if (c) break;

using namespace std;

struct tx_pipling_t {
	int eof;
	int off, len;
	char buf[8192 * 16];
	int  pipling(tx_aiocb *f, tx_aiocb *t, tx_task_t *sk);
};

struct tx_netcat_t {
	tx_task_t task;
	tx_aiocb file;
	tx_aiocb file2;
	tx_timer_t timer;

	tx_pipling_t n2s;
	tx_pipling_t s2n;
} netcat;

#define BREAK(c, c2) if (c) { err = 0; if (c2) break; }

int tx_pipling_t::pipling(tx_aiocb *f, tx_aiocb *t, tx_task_t *sk)
{
	int err;
	BOOL writok;
	DWORD wroted;
	HANDLE handle;

	while (!eof || off < len) {
		if (off == len && !tx_readable(f)) {
			tx_aincb_active(f, sk);
			return 1;
		}

		if (off == len) {
			err = recv(f->tx_fd, buf, sizeof(buf), 0);
			eof |= (err == 0);
			tx_aincb_update(f, err);
			BREAK(err == -1, tx_readable(f));
			len = err, off = 0;
		}

		if (off < len && t == NULL) {
			wroted = 0;
			handle = GetStdHandle(STD_OUTPUT_HANDLE);
			writok = WriteFile(handle, buf + off, len - off, &wroted, NULL);
			if (writok == FALSE) break;
			off += wroted;
#if 0
			off = len;
#endif
			continue;
		}

		if (off < len && !tx_writable(t)) {
			tx_outcb_prepare(t, sk, 0);
			return 1;
		}

		if (off < len) {
			err = tx_outcb_write(t, buf + off, len - off);
			BREAK(err == -1, tx_writable(t));
			off += err;
		}
	}

	return 0;
}

static void update_netcat(void *upp)
{
	int d1, d2;
	tx_netcat_t *np = (tx_netcat_t *)upp;

	d1 = np->s2n.pipling(&np->file2, &np->file, &np->task);
	d2 = np->n2s.pipling(&np->file, &np->file2, &np->task);

	if (d1 == 0 || d2 == 0) {
		tx_loop_stop(tx_loop_get(&np->task));
		return;
	}

	return;
}

static DWORD _id_tx_;
static int _stop_tx_ = 0;
static HANDLE handle[2] = {NULL};

struct pipling_ops {
	int (*p_write)(ULONG_PTR upp, const void *buf, size_t len);
	int (*p_read)(ULONG_PTR upp, void *buf, size_t len);
	int (*p_close)(ULONG_PTR upp);
};

int p_handle_read(ULONG_PTR upp, void *buf, size_t len)
{
	BOOL callok;
	DWORD transfer;
	callok = ReadFile((HANDLE)upp, buf, len, &transfer, NULL);
	return callok? transfer: -1;
}

int p_handle_write(ULONG_PTR upp, const void *buf, size_t len)
{
	BOOL callok;
	DWORD transfer;
	callok = WriteFile((HANDLE)upp, buf, len, &transfer, NULL);
	return callok? transfer: -1;
}

int p_handle_close(ULONG_PTR upp)
{
	CloseHandle((HANDLE)upp);
	return 0;
}

static pipling_ops _handle_ops = {
p_write: p_handle_write,
	 p_read: p_handle_read,
	 p_close: p_handle_close
};

int p_network_read(ULONG_PTR upp, void *buf, size_t len)
{
	return recv((int)upp, (char *)buf, len, 0);
}

int p_network_write(ULONG_PTR upp, const void *buf, size_t len)
{
	return send((int)upp, (const char *)buf, len, 0);
}

int p_network_close(ULONG_PTR upp)
{
	shutdown((int)upp, SD_BOTH);
	closesocket((int)upp);
	return 0;
}

static pipling_ops _network_ops = {
p_write: p_network_write,
	 p_read: p_network_read,
	 p_close: p_network_close
};

struct pipling_file {
	ULONG_PTR p_upp;
	struct pipling_ops *p_ops;
};

DWORD WINAPI pipling(LPVOID args)
{
	int off, eof;
	int len, err;
	char buf[8192];
	pipling_file *plu[2];
	memcpy(plu, args, sizeof(pipling_file*) * 2);

	off = len = eof = err = 0;
	while (!eof || off < len) {
		if (off == len) {
			err = P_READ(plu[0], buf, sizeof(buf));
			P_BREAK(err == -1);
			eof |= (err == 0);
			len = err, off = 0;
		}

		if (off < len) {
			err = P_WRITE(plu[1], buf + off, len - off);
			P_BREAK(err == -1);
			off += err;
		}
	}

	P_CLOSE(plu[1]);
	return 0;
}

static pipling_file *_s2n[2];
static pipling_file *_n2s[2];
static pipling_file _n, _i, _o;

void tx_stdio_start(int fd, int threadout)
{
	_i.p_upp = (ULONG_PTR)GetStdHandle(STD_INPUT_HANDLE);
	_i.p_ops = &_handle_ops;

	_n.p_upp = fd;
	_n.p_ops = &_network_ops;

	_s2n[0]= &_i, _s2n[1] = &_n;
	handle[0] = CreateThread(NULL, 0, pipling, (LPVOID)_s2n, 0, &_id_tx_);

	if (threadout == 0)  {
		return;
	}

	_o.p_upp = (ULONG_PTR)GetStdHandle(STD_OUTPUT_HANDLE);
	_o.p_ops = &_handle_ops;

	_n2s[0]= &_n, _n2s[1] = &_o;
	handle[1] = CreateThread(NULL, 0, pipling, (LPVOID)_n2s, 0, &_id_tx_);
}

void tx_stdio_stop(void)
{
	_stop_tx_ = 1;
	CloseHandle(handle[0]);
	CloseHandle(handle[1]);
	return;
}

int main(int argc, char* argv[])
{
	int filds[2];
	WSADATA data;
	WSAStartup(0x202, &data);

	netcat_t* upp = get_cat_context(argc, argv);
	if (upp == NULL) {
		perror("get_cat_context");
		return 1;
	}

	int error = pipe(filds);
	if (error == -1) {
		perror("pipe");
		return 1;
	}
	fprintf(stderr, "filds: %d\n", filds[0]);

	int net_fd = get_cat_socket(upp);
	if (net_fd < 0) {
		perror("net_create");
		return 1;
	}

	const char *blkio = get_cat_options(upp, "blkio");
	if (blkio != 0 && *blkio != 0) {
		tx_stdio_start(net_fd, 1);
		WaitForMultipleObjects(2, handle, FALSE, INFINITE);
		tx_stdio_stop();
		return 0;
	}

	tx_loop_t *loop = tx_loop_default();
	tx_poll_t *poll = tx_completion_port_init(loop);
	tx_timer_ring *provider = tx_timer_ring_get(loop);

	tx_task_init(&netcat.task, loop, update_netcat, &netcat);

	tx_aiocb_init(&netcat.file, loop, net_fd);
	tx_aiocb_init(&netcat.file2, loop, filds[0]);
	tx_timer_init(&netcat.timer, provider, &netcat.task);

	const char *off = get_cat_options(upp, "write");
	tx_stdio_start(filds[1], off == NULL || strcmp("off", off));
	tx_aincb_active(&netcat.file2, &netcat.task);
	tx_aincb_active(&netcat.file, &netcat.task);
	tx_loop_main(loop);
	tx_aincb_stop(&netcat.file2, &netcat.task);
	tx_aincb_stop(&netcat.file, &netcat.task);
	tx_stdio_stop();

	tx_timer_stop(&netcat.timer);
	tx_aiocb_fini(&netcat.file2);
	tx_aiocb_fini(&netcat.file);
	tx_loop_delete(loop);

	shutdown(net_fd, SD_BOTH);
	closesocket(net_fd);
	WSACleanup();
	return 1;
}
