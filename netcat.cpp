#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <fstream>
#include <iostream>
#include <winsock2.h>

#include "txall.h"

#define P_WRITE(p, b, l) ((p)->p_ops->p_write)((p)->p_upp, b, l)
#define P_READ(p, b, l)  ((p)->p_ops->p_read)((p)->p_upp, b, l)
#define P_CLOSE(p)       ((p)->p_ops->p_close)((p)->p_upp)
#define P_BREAK(c)		 if (c) break;

using namespace std;

typedef struct _netcat {
	int l_mode;
	const char *sai_port;
	const char *sai_addr;
	const char *dai_port;
	const char *dai_addr;
} netcat_t;

static int _use_poll = 0;

static void error_check(int exited, const char *str)
{
	if (exited) {
		fprintf(stderr, "%s\n", str);
		exit(-1);
	}

	return;
}

static char* memdup(const void *buf, size_t len)
{
	char *p = (char *)malloc(len);
	if (p != NULL)
		memcpy(p, buf, len);
	return p;
}

static int get_cat_socket(netcat_t *upp)
{
	int serv = socket(AF_INET, SOCK_STREAM, 0);

	sockaddr_in my_addr;
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(upp->sai_port? atoi(upp->sai_port): 0);
	if (upp->sai_addr == NULL) {
		my_addr.sin_addr.s_addr = INADDR_ANY;
	} else if (inet_pton(AF_INET, upp->sai_addr, &my_addr.sin_addr) <= 0) {
		cerr << "incorrect network address.\n";
		return -1;
	}

	if ((upp->sai_addr != NULL || upp->sai_port != NULL) &&
			(-1 == bind(serv, (sockaddr*)&my_addr, sizeof(my_addr)))) {
		cerr << "bind network address.\n";
		return -1;
	}

	if (upp->l_mode) {
		int ret;
		struct sockaddr their_addr;
		socklen_t namlen = sizeof(their_addr);

		ret = listen(serv, 5);
		error_check(ret == -1, "listen");
		fprintf(stderr, "server is ready at port: %s\n", upp->sai_port);

		ret = accept(serv, &their_addr, &namlen);
		error_check(ret == -1, "recvfrom failure");

		closesocket(serv);
		return ret;

	} else {
		sockaddr_in their_addr;
		their_addr.sin_family = AF_INET;
		their_addr.sin_port = htons(short(atoi(upp->dai_port)));
		if (inet_pton(AF_INET, upp->dai_addr, &their_addr.sin_addr) <= 0) {
			cerr << "incorrect network address.\n";
			closesocket(serv);
			return -1;
		}

		if (-1 == connect(serv, (sockaddr*)&their_addr, sizeof(their_addr))) {
			cerr << "connect: " << endl;
			closesocket(serv);
			return -1;
		}

		return serv;
	}

	return -1;
}

static netcat_t* get_cat_context(netcat_t *upp, int argc, char **argv)
{
	int i;
	int opt_pidx = 0;
	int opt_listen = 0;
	char *parts[2] = {0};
	const char *domain = 0, *port = 0;
	const char *s_domain = 0, *sai_port = 0;

	for (i = 1; i < argc; i++) {
		if (!strcmp("-l", argv[i])) {
			opt_listen = 1;
		} else if (!strcmp("--poll", argv[i])) {
			_use_poll = 1;
		} else if (!strcmp("-s", argv[i])) {
			error_check(++i == argc, "-s need an argument");
			s_domain = argv[i];
		} else if (!strcmp("-p", argv[i])) {
			error_check(++i == argc, "-p need an argument");
			sai_port = argv[i];
		} else if (opt_pidx < 2) {
			parts[opt_pidx++] = argv[i];
		} else {
			fprintf(stderr, "too many argument");
			return 0;
		}
	}

	if (opt_pidx == 1) {
		port = parts[0];
		for (i = 0; port[i]; i++) {
			if (!isdigit(port[i])) {
				domain = port;
				port = NULL;
				break;
			}
		}
	} else if (opt_pidx == 2) {
		port = parts[1];
		domain = parts[0];
		for (i = 0; domain[i]; i++) {
			if (!isdigit(domain[i])) {
				break;
			}
		}

		error_check(domain[i] == 0, "should give one port only");
	}

	if (opt_listen) {
		if (s_domain != NULL)
			error_check(domain != NULL, "domain repeat twice");
		else
			s_domain = domain;

		if (sai_port != NULL)
			error_check(port != NULL, "port repeat twice");
		else
			sai_port = port;
	} else {
		u_long f4wardai_addr = 0;
		error_check(domain == NULL, "hostname is request");
		f4wardai_addr = inet_addr(domain);
		error_check(f4wardai_addr == INADDR_ANY, "bad hostname");
		error_check(f4wardai_addr == INADDR_NONE, "bad hostname");
	}

	upp->l_mode = opt_listen;
	upp->sai_addr = s_domain;
	upp->sai_port = sai_port;
	upp->dai_addr = domain;
	upp->dai_port = port;
	return upp;
}

struct tx_pipling_t {
	int eof;
	int off, len;
	char buf[8192 * 16];
	int  pipling(tx_file_t *f, tx_file_t *t, tx_task_t *sk);
};

struct tx_netcat_t {
	tx_task_t task;
	tx_file_t file;
	tx_file_t file2;
	tx_timer_t timer;

	tx_pipling_t n2s;
	tx_pipling_t s2n;
} netcat;

#define BREAK(c, c2) if (c) { err = 0; if (c2) break; }

int tx_pipling_t::pipling(tx_file_t *f, tx_file_t *t, tx_task_t *sk)
{
	int err;
	BOOL writok;
	DWORD wroted;
	HANDLE handle;

	while (!eof || off < len) {
		if (off == len && !tx_readable(f)) {
			tx_active_in(f, sk);
			return 1;
		}

		if (off == len) {
			err = tx_recv(f, buf, sizeof(buf), 0);
			eof |= (err == 0);
			BREAK(err == -1, tx_readable(f));
			len = err, off = 0;
		}

		if (off < len && t == NULL) {
			wroted = 0;
			handle = GetStdHandle(STD_OUTPUT_HANDLE);
			writok = WriteFile(handle, buf + off, len - off, &wroted, NULL);
			if (writok == FALSE) break;
			off += wroted;
			continue;
		}

		if (off < len && !tx_writable(t)) {
			tx_active_out(t, sk);
			return 1;
		}

		if (off < len) {
			err = tx_send(t, buf + off, len - off, 0);
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
	d2 = np->n2s.pipling(&np->file, NULL, &np->task);

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

void tx_stdio_start(int fd)
{
	_i.p_upp = (ULONG_PTR)GetStdHandle(STD_INPUT_HANDLE);
	_i.p_ops = &_handle_ops;

	_n.p_upp = fd;
	_n.p_ops = &_network_ops;

	_s2n[0]= &_i, _s2n[1] = &_n;
	handle[0] = CreateThread(NULL, 0, pipling, (LPVOID)_s2n, 0, &_id_tx_);

	if (_use_poll == 0) {
		_o.p_upp = (ULONG_PTR)GetStdHandle(STD_OUTPUT_HANDLE);
		_o.p_ops = &_handle_ops;

		_n2s[0]= &_n, _n2s[1] = &_o;
		handle[1] = CreateThread(NULL, 0, pipling, (LPVOID)_n2s, 0, &_id_tx_);
	}
}

void tx_stdio_stop(void)
{
	_stop_tx_ = 1;
	CloseHandle(handle[0]);
	if (_use_poll == 0)
		CloseHandle(handle[1]);
	return;
}

int main(int argc, char* argv[])
{
	int filds[2];
	WSADATA data;
	netcat_t netcat_context = {0};

	WSAStartup(0x202, &data);

	netcat_t* upp = get_cat_context(&netcat_context, argc, argv);
	if (upp == NULL) {
		perror("get_cat_context");
		return 1;
	}

	int net_fd = get_cat_socket(upp);
	if (net_fd < 0) {
		perror("net_create");
		return 1;
	}
	
	if (_use_poll == 0) {
		tx_stdio_start(net_fd);
		WaitForMultipleObjects(2, handle, FALSE, INFINITE);
		tx_stdio_stop();
		return 0;
	}

	int error = pipe(filds);
	if (error == -1) {
		perror("pipe");
		return 1;
	}

	tx_loop_t *loop = tx_loop_default();
	tx_poll_t *poll = tx_completion_port_init(loop);
	tx_timer_ring *provider = tx_timer_ring_get(loop);

	tx_task_init(&netcat.task, loop, update_netcat, &netcat);

	tx_file_init(&netcat.file, loop, net_fd);
	tx_file_init(&netcat.file2, loop, filds[0]);
	tx_timer_init(&netcat.timer, provider, &netcat.task);

	tx_stdio_start(filds[1]);
	tx_active_in(&netcat.file2, &netcat.task);
	tx_active_in(&netcat.file, &netcat.task);
	tx_loop_main(loop);
	tx_cancel_in(&netcat.file2, &netcat.task);
	tx_cancel_in(&netcat.file, &netcat.task);
	tx_stdio_stop();

	tx_timer_stop(&netcat.timer);
	tx_close(&netcat.file2);
	tx_close(&netcat.file);
	tx_loop_delete(loop);

	shutdown(net_fd, SD_BOTH);
	closesocket(net_fd);
	WSACleanup();
	return 1;
}
