#include <stdio.h>
#include <assert.h>
#include <fstream>
#include <iostream>

#include <windows.h>

#if defined(WIN32)
#define ABORTON(cond) if (cond) goto clean
typedef int socklen_t;
static int inet_pton4(const char *src, unsigned char *dst);
int inet_pton(int af, const char* src, void* dst)
{
	switch (af) {
		case AF_INET:
			return inet_pton4(src, (unsigned char *)dst);

#if defined(AF_INET6)
		case AF_INET6:
			/*return inet_pton6(src, (unsigned char *)dst); */
#endif

		default:
			return 0;
	}
	/* NOTREACHED */
}

int inet_pton4(const char *src, unsigned char *dst)
{
	static const char digits[] = "0123456789";
	int saw_digit, octets, ch;
	unsigned char tmp[sizeof(struct in_addr)], *tp;

	saw_digit = 0;
	octets = 0;
	*(tp = tmp) = 0;
	while ((ch = *src++) != '\0') {
		const char *pch;

		if ((pch = strchr(digits, ch)) != NULL) {
			unsigned int nw = *tp * 10 + (pch - digits);

			if (saw_digit && *tp == 0)
				return 0;
			if (nw > 255)
				return 0;
			*tp = nw;
			if (!saw_digit) {
				if (++octets > 4)
					return 0;
				saw_digit = 1;
			}
		} else if (ch == '.' && saw_digit) {
			if (octets == 4)
				return 0;
			*++tp = 0;
			saw_digit = 0;
		} else
			return 0;
	}
	if (octets < 4)
		return 0;
	memcpy(dst, tmp, sizeof(struct in_addr));
	return 1;
}

#endif


class pipling_t {
public:
    pipling_t(void);
    int pipling(int fd1, int fd2);

private:
    int eof;
    int off, len;
    int _io_len;
    char *_io_buf;
    char __io_dat[8192];
    int async_send(int fd);
    int async_recv(int fd);

public:
    int _ready_pipling;
    static HANDLE handle;
};

static long ln_events = 0;
HANDLE pipling_t::handle = NULL;

struct std_buf {
    int o_use;
    struct std_buf *o_next;

    int o_len;
    char o_buf[8192];
} _free_in_ring[4], _free_out_ring[4];

static int _out_wait = 0;
static int _out_error = 0;
static HANDLE _out_handle = 0;
static std_buf *_out_header = 0;
static std_buf *_out_tailer = 0;
static std_buf *_out_freeer = 0;
static CRITICAL_SECTION _out_mutex;

static DWORD WINAPI pipling_stdout(VOID *p)
{
    int eof = 1;
    std_buf *out;
    DWORD transfer;
    HANDLE houtput = GetStdHandle(STD_OUTPUT_HANDLE);

    do {
        EnterCriticalSection(&_out_mutex);
        if (_out_header == NULL) {
            LeaveCriticalSection(&_out_mutex);
            WaitForSingleObject(_out_handle, INFINITE);
            continue;
        }

        out = _out_header;
        _out_header = _out_header->o_next;
        if (_out_header == NULL)
            _out_tailer = _out_header;
        LeaveCriticalSection(&_out_mutex);

        assert(out->o_use == 2);
        eof = !WriteFile(houtput, out->o_buf, out->o_len, &transfer, NULL);
        eof = (eof || transfer != out->o_len);
        out->o_use = 0;

        EnterCriticalSection(&_out_mutex);
        out->o_next = _out_freeer;
        _out_freeer = out;
        SetEvent(_out_handle);
        LeaveCriticalSection(&_out_mutex);
    } while (eof);

    return 0;
}

static int insert_stdout_buf(std_buf *buf)
{
    buf->o_next = NULL;
    buf->o_use = 2;

    if (_out_header == NULL) {
        _out_header = _out_tailer = buf;
    } else {
        _out_tailer->o_next = buf;
    }

    if (_out_wait == 1) {
        SetEvent(_out_handle);
        _out_wait = 0;
    }

    return 0;
}

static int lock_stdout(char *buf, int len, char **loc)
{
    int err = _out_error;
    std_buf *gc_buf = NULL;

    EnterCriticalSection(&_out_mutex);
    int index = (buf - (char *)_free_out_ring) / sizeof(_free_out_ring[0]);
    if (index >= 0 && index < 4) {
        assert(_free_out_ring[index].o_buf == buf);
        gc_buf = &_free_out_ring[index];
    }

    if (gc_buf == NULL && _out_freeer != NULL) {
        gc_buf = _out_freeer;
        _out_freeer = _out_freeer->o_next;
        assert(len <= sizeof(gc_buf->o_buf));
        memcpy(gc_buf->o_buf, buf, len);
        gc_buf->o_use = 1;
    }

    if (gc_buf != NULL) {
        assert(gc_buf->o_use == 1);
        gc_buf->o_len = len;
        insert_stdout_buf(gc_buf);
    }

    _out_block = (_out_freeer == NULL);
    if (_out_freeer != NULL) {
        err = sizeof(_out_freeer->o_buf);
        *loc = _out_freeer->o_buf;
        _out_freeer->o_use = 1;
        _out_freeer = _out_freeer->o_next;
    } else if (err == 0) {
        WSASetLastError(WSAEWOULDBLOCK);
        err = -1;
    } else {
        WSASetLastError(err);
        err = -1;
    }
    LeaveCriticalSection(&_out_mutex);

    return err;
}

static int _in_wait = 0;
static int _in_block = 0;
static int _in_error = 0;
static HANDLE _in_handle = 0;
static std_buf *_in_header = 0;
static std_buf *_in_tailer = 0;
static std_buf *_in_freeer = 0;
static CRITICAL_SECTION _in_mutex;

static int insert_stdin_buf(std_buf *buf)
{
    buf->o_next = NULL;
    buf->o_use = 2;

    if (_in_header == NULL) {
        _in_header = buf;
        _in_tailer = buf;
    } else {
        _in_tailer->o_next = buf;
        _in_tailer = buf;
    }

    if (_in_block == 1) {
        SetEvent(pipling_t::handle);
        _in_block = 0;
    }

    return 0;
}

static DWORD WINAPI pipling_stdin(VOID *p)
{
    int eof = 0;
    std_buf *inp;
    DWORD transfer;
    HANDLE hinput = GetStdHandle(STD_INPUT_HANDLE);

    do {
        EnterCriticalSection(&_in_mutex);
        if (_in_freeer == NULL) {
            _in_block = 1;
            LeaveCriticalSection(&_in_mutex);
            WaitForSingleObject(_in_handle, INFINITE);
            continue;
        }

        inp = _out_freeer;
        _out_freeer = _out_freeer->o_next;
        LeaveCriticalSection(&_out_mutex);

        assert(inp->o_use == 0);
        eof = (eof || !ReadFile(hinput, inp->o_buf, inp->o_len, &transfer, NULL));
        eof = (transfer == 0 || eof);
        inp->o_len = transfer;
        inp->o_use = 1;

        EnterCriticalSection(&_in_mutex);
        inp->o_next = NULL;
        if (_in_header == NULL) {
            _in_header = _in_tailer = inp;
        } else {
            _in_tailer->o_next = inp;
        }

        if (_in_block) {
            SetEvent(_in_handle);
            _in_block = 1;
        }
        LeaveCriticalSection(&_in_mutex);
    } while (eof);

    return 0;
}

static int lock_stdin(char *buf, char **loc)
{
    int err = _in_error;
    std_buf *gc_buf = NULL;

    EnterCriticalSection(&_in_mutex);
    int index = (buf - (char *)_free_in_ring) / sizeof(_free_in_ring[0]);
    if (index >= 0 && index < 4) {
        assert(_free_in_ring[index].o_buf == buf);
        gc_buf = &_free_in_ring[index];
    }

    assert(gc_buf != NULL || buf == NULL);
    if (gc_buf != NULL) {
        assert(gc_buf->o_use == 1);
        gc_buf->o_use = 0;
        gc_buf->o_next = _free_in_ring;
        _free_in_ring->o_next = gc_buf;
        if (_in_wait == 1) {
            SetEvent(_in_handle);
            _in_wait = 0;
        }
    }

    _in_block = (_out_header == NULL);
    if (_out_header != NULL) {
        err = _out_header->o_len;
        if (err > 0) {
            *loc = _out_header->o_buf;
            _out_header->o_use = 1;
            _out_header = _out_header->o_next;
            _out_tailer = _out_header? _out_tailer: NULL;
        }
    } else if (err == 0) {
        WSASetLastError(WSAEWOULDBLOCK);
        err = -1;
    } else {
        WSASetLastError(err);
        err = -1;
    }
    LeaveCriticalSection(&_in_mutex);

    return err;
}

pipling_t::pipling_t(void)
{
    _ready_pipling = 1;
    _io_len = sizeof(__io_dat);
    _io_buf = __io_dat;
    off = len = 0;
    eof = 0;
    return;
}

int pipling_t::async_recv(int fd)
{
    int err;
    char *buf = _io_buf;

    if (eof != 0) {
        return 0;
    }

    if (fd == -1) {
        err = lock_stdin(_io_buf, &buf);
        if (err == -1 && WSAGetLastError() == WSAEWOULDBLOCK) {
            _ready_pipling = 0;
            return 1;
        }
    } else {
        err = recv(fd, _io_buf, _io_len, 0);
        if (err == -1 && WSAGetLastError() == WSAEWOULDBLOCK) {
            ln_events = ln_events| FD_READ;
            WSAEventSelect(fd, pipling_t::handle, ln_events);
            _ready_pipling = 0;
            return 1;
        }
        ln_events &= ~FD_READ;
    }

    _ready_pipling = 1;
    _io_buf = buf;
    if (err > 0) {
        len = err;
        off = 0;
        return 0;
    }

    eof = 1;
    return 0;
}

int pipling_t::async_send(int fd)
{
    int err;
    char *buf = _io_buf;

    assert(off < len);

    if (fd == -1) {
        err = lock_stdout(_io_buf, len, &buf);
        if (err == -1 && WSAGetLastError() == WSAEWOULDBLOCK) {
            _ready_pipling = 0;
            return 1;
        }

        _ready_pipling = 1;
        if (err > 0) {
            _io_buf = buf;
            _io_len = err;
            err = len;
        }
    } else {
        err = send(fd, buf + off, len - off, 0);
        if (err == -1 && WSAGetLastError() == WSAEWOULDBLOCK) {
            ln_events = ln_events| FD_WRITE;
            WSAEventSelect(fd, pipling_t::handle, ln_events);
            _ready_pipling = 0;
            return 1;
        }
        ln_events &= ~FD_WRITE;
    }

    _ready_pipling = 1;
    if (err > 0) {
        off += err;
        return 0;
    }

    off = len;
    eof = 1;
    return 0;
}

int pipling_t::pipling(int f, int t)
{
#if 0
    if (_ready_pipling) {
        return 1;
    }
#endif

    while (!eof || off < len) {
        if (off == len && async_recv(f)) {
            return 1;
        }

        if (off < len && async_send(t)) {
            return 1;
        }
    }

    return 0;
}

typedef struct _netcat {
    int l_mode;
    const char *sai_port;
    const char *sai_addr;
    const char *dai_port;
    const char *dai_addr;
} netcat_t;

static int get_cat_socket(netcat_t *upp);
static netcat_t* get_cat_context(netcat_t *upp, int argc, char **argv);

int main(int argc, char *argv[])
{
	WSADATA data;
    DWORD ignore;
    pipling_t s2f, f2s;
    HANDLE std_thread[2];
    pipling_t::handle = WSACreateEvent();
	netcat_t netcat_context = {0};

	WSAStartup(0x202, &data);

	netcat_t* upp = get_cat_context(&netcat_context, argc, argv);
	if (upp == NULL) {
		perror("get_cat_context");
		return 1;
	}

	int nfd = get_cat_socket(upp);
	if (nfd < 0) {
		perror("net_create");
		return 1;
	}

    _in_handle = WSACreateEvent();
    _out_handle = WSACreateEvent();
    InitializeCriticalSection(&_in_mutex);
    InitializeCriticalSection(&_out_mutex);

    std_thread[0] = CreateThread(NULL, 0, pipling_stdout, 0, 0, &ignore);
    std_thread[1] = CreateThread(NULL, 0, pipling_stdin, 0, 0, &ignore);
    do {
        if (!s2f.pipling(-1, nfd)) {
            break;
        }

        if (!f2s.pipling(nfd, -1)) {
            break;
        }

        WaitForSingleObject(pipling_t::handle, INFINITE);
    } while (1);
    CloseHandle(std_thread[1]);
    CloseHandle(std_thread[0]);

    DeleteCriticalSection(&_out_mutex);
    DeleteCriticalSection(&_in_mutex);

    CloseHandle(pipling_t::handle);
    CloseHandle(_out_handle);
    CloseHandle(_in_handle);
    return 0;
}

/* ---------------------------------------------------------------------------- */

using namespace std;
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
#if 0
    } else if (inet_pton(AF_INET, upp->sai_addr, &my_addr.sin_addr) <= 0) {
        cerr << "incorrect network address.\n";
        return -1;
#endif
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

