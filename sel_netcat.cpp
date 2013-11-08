#include <stdio.h>
#include <assert.h>
#include <fstream>
#include <iostream>

#include <winsock2.h>
#include <windows.h>

#include "ncatutil.h"

#define DEBUG(fmt, args...)
#define MAX_BUF_CNT 32

class pipling_t {
public:
    pipling_t(void);
    int pipling(int fd1, int fd2);

private:
    int eof;
    int off, len;
    int _io_len;
    char __io_dat[8192];
    int async_send(int fd);
    int async_recv(int fd);

public:
    char *_io_buf;
    int _ready_pipling;
    static HANDLE handle;
};

static long ln_events = 0;
static char _phony_buf[8192];
HANDLE pipling_t::handle = NULL;

struct std_buf {
    int o_use;
    struct std_buf *o_next;

    int o_len;
    char o_buf[8192];
} _free_in_ring[MAX_BUF_CNT], _free_out_ring[MAX_BUF_CNT];

static int _out_error = 0;
static HANDLE _out_handle = 0;
static std_buf *_out_header = 0;
static std_buf *_out_tailer = 0;
static std_buf *_out_freeer = 0;
static int _out_freeer_count = MAX_BUF_CNT;
static CRITICAL_SECTION _out_mutex;

static DWORD WINAPI pipling_stdout(VOID *p)
{
    int eof = 0;
    std_buf *out;
    DWORD transfer;
    HANDLE houtput = GetStdHandle(STD_OUTPUT_HANDLE);

    do {
        EnterCriticalSection(&_out_mutex);
        if (_out_header == NULL) {
            LeaveCriticalSection(&_out_mutex);
			DEBUG("WaitForSingleObject output start\n");
            WaitForSingleObject(_out_handle, INFINITE);
			DEBUG("WaitForSingleObject output end\n");
            continue;
        }

        out = _out_header;
        _out_header = _out_header->o_next;
        out->o_next = NULL;
        LeaveCriticalSection(&_out_mutex);

        assert(out->o_use == 2);
		DEBUG("WriteFile start\n");
        eof = (eof || !WriteFile(houtput, out->o_buf, out->o_len, &transfer, NULL));
        eof = (eof || transfer != out->o_len);
		DEBUG("WriteFile end\n");
		_out_error = (eof? GetLastError(): 0);
        out->o_use = 0;

        EnterCriticalSection(&_out_mutex);
        out->o_next = _out_freeer;
        _out_freeer = out;
        SetEvent(pipling_t::handle);
        LeaveCriticalSection(&_out_mutex);
    } while (eof == 0);

    return 0;
}

static int insert_stdout_buf(std_buf *buf)
{
    buf->o_next = NULL;
    buf->o_use = 2;

    if (_out_header == NULL) {
        _out_header = buf;
    } else {
        _out_tailer->o_next = buf;
    }
	
	_out_tailer = buf;
    SetEvent(_out_handle);
	return 0;
}

static int lock_stdout(char *buf, int len, char **loc)
{
    int err = _out_error;
    std_buf *gc_buf = NULL;

    EnterCriticalSection(&_out_mutex);
    int index = (buf - (char *)_free_out_ring) / sizeof(_free_out_ring[0]);
    if (index >= 0 && index < MAX_BUF_CNT) {
        assert(_free_out_ring[index].o_buf == buf);
        gc_buf = &_free_out_ring[index];
    }

    if (gc_buf == NULL &&
		_out_freeer != NULL &&
		_out_freeer->o_next != NULL) {
        gc_buf = _out_freeer;
        _out_freeer = _out_freeer->o_next;
        gc_buf->o_next = NULL;
        assert(len <= sizeof(gc_buf->o_buf));
        memcpy(gc_buf->o_buf, buf, len);
        gc_buf->o_use = 1;
        _out_freeer_count--;
    }

	if (gc_buf == NULL) {
    	LeaveCriticalSection(&_out_mutex);
        WSASetLastError(WSAEWOULDBLOCK);
		DEBUG("lock failure %p\n", _out_freeer);
		return -1;
	}

	if (_out_freeer != NULL) {
		assert(gc_buf->o_use == 1);
		gc_buf->o_len = len;
		//memcpy(_phony_buf, buf, len);
		insert_stdout_buf(gc_buf);
	}

    if (_out_freeer != NULL) {
        err = sizeof(_out_freeer->o_buf);
        *loc = _out_freeer->o_buf;
        _out_freeer->o_use = 1;
        _out_freeer = _out_freeer->o_next;
        _out_freeer_count--;
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
    buf->o_use = 1;

    if (_in_header == NULL) {
        _in_header = buf;
    } else {
        _in_tailer->o_next = buf;
	}
	_in_tailer = buf;
	SetEvent(pipling_t::handle);
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
            LeaveCriticalSection(&_in_mutex);
            DEBUG("ReadFile WaitForSingleObject start\n");
            WaitForSingleObject(_in_handle, INFINITE);
            DEBUG("ReadFile WaitForSingleObject end\n");
            continue;
        }

        inp = _in_freeer;
        _in_freeer = _in_freeer->o_next;
        inp->o_next = NULL;
        LeaveCriticalSection(&_in_mutex);

        assert(inp->o_use == 0);
        eof = (eof || !ReadFile(hinput, inp->o_buf, sizeof(inp->o_buf), &transfer, NULL));
        eof = (eof || transfer == 0);
        DEBUG("ReadFile end %d, transfer %d\n", eof, transfer);
        inp->o_len = transfer;
        inp->o_use = 1;

        EnterCriticalSection(&_in_mutex);
        inp->o_next = NULL;
        if (_in_header == NULL) {
            _in_header = inp;
        } else {
            _in_tailer->o_next = inp;
        }
        _in_tailer = inp;
        SetEvent(pipling_t::handle);
        LeaveCriticalSection(&_in_mutex);
    } while (!eof);

    DEBUG("ReadFile quited\n");
    return 0;
}

static int lock_stdin(char *buf, char **loc)
{
    int err = _in_error;
    std_buf *gc_buf = NULL;

    EnterCriticalSection(&_in_mutex);
	DEBUG("lock_stdin %d BB\n", err);
    int index = (buf - (char *)_free_in_ring) / sizeof(_free_in_ring[0]);
    if (index >= 0 && index < MAX_BUF_CNT) {
        assert(_free_in_ring[index].o_buf == buf);
        gc_buf = &_free_in_ring[index];
    }

    assert(gc_buf != NULL || buf == NULL);
    if (gc_buf != NULL && _in_header != NULL) {
        assert(gc_buf->o_use == 2);
        gc_buf->o_use = 0;
        gc_buf->o_next = _in_freeer;
        _in_freeer = gc_buf;
		SetEvent(_in_handle);
    }

    if (_in_header != NULL) {
        err = _in_header->o_len;
        assert(err > 0);
        *loc = 0;
        if (err > 0) {
            assert(_in_header->o_use == 1);
            _in_header->o_use = 2;
            *loc = _in_header->o_buf;
            assert(err <= sizeof(_in_header->o_buf));
            _in_header = _in_header->o_next;
			//memcpy(_phony_buf, *loc, err);
        }
    } else if (err == 0) {
        WSASetLastError(WSAEWOULDBLOCK);
        err = -1;
    } else {
        WSASetLastError(err);
        err = -1;
    }
    LeaveCriticalSection(&_in_mutex);

	DEBUG("lock_stdin %d\n", err);
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
        DEBUG("async_recv\n");
        return 0;
    }

    if (fd == -1) {
        DEBUG("lock_stdin start\n");
        err = lock_stdin(_io_buf, &buf);
        if (err == -1 && WSAGetLastError() == WSAEWOULDBLOCK) {
            ln_events = ln_events& ~FD_WRITE;
            WSAEventSelect(fd, pipling_t::handle, ln_events);
            _ready_pipling = 0;
            return 1;
        }
    } else {
		DWORD out;
		WSABUF b;
		DWORD flags = 0;
		b.buf = _io_buf;
		b.len = _io_len;
        err = WSARecv(fd, &b, 1, &out, &flags, NULL, NULL);
        //err = recv(fd, _io_buf, _io_len, 0);
		err = (err == 0? out: err);
        if (err == -1 && WSAGetLastError() == WSAEWOULDBLOCK) {
            ln_events = ln_events| FD_READ;
            WSAEventSelect(fd, pipling_t::handle, ln_events);
            DEBUG("WSAEventSelect: FD_WRITE %x\n", ln_events  & FD_WRITE);
            DEBUG("WSAEventSelect: FD_READ %x\n", ln_events  & FD_READ);
            _ready_pipling = 0;
            return 1;
        }
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
        DEBUG("lock_stdout start %d\n", len);
#if 0
        err = lock_stdout(_io_buf, len, &buf);
        if (err == -1 && WSAGetLastError() == WSAEWOULDBLOCK) {
            ln_events = ln_events& ~FD_READ;
            WSAEventSelect(fd, pipling_t::handle, ln_events);
            _ready_pipling = 0;
            DEBUG("lock_stdout\n");
            return 1;
        }

        _ready_pipling = 1;
        if (err > 0) {
            _io_buf = buf;
            _io_len = err;
            err = len;
        }
#else
        DWORD out;
        if (WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), _io_buf, len, &out, NULL)) {
            err = out;
        } else {
            err = -1;
        }
#endif
    } else {
		DWORD out;
		WSABUF b;
		b.buf = buf + off;
		b.len = len - len;
        //err = send(fd, buf + off, len - off, 0);
        err = WSASend(fd, &b, 1, &out, 0, NULL, NULL);
        DEBUG("send start off %d, len %d, %d err\n", off, len, err);
		err = (err == 0? out: err);
        if (err == -1 && WSAGetLastError() == WSAEWOULDBLOCK) {
            ln_events = ln_events| FD_WRITE;
            WSAEventSelect(fd, pipling_t::handle, ln_events);
            DEBUG("WSAEventSelect: FD_WRITE %x\n", ln_events  & FD_WRITE);
            DEBUG("WSAEventSelect: FD_READ %x\n", ln_events  & FD_READ);
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

	DEBUG("pipling %d %d\n", f, t);
    while (!eof || off < len) {
		DEBUG("pipling async_recv %d %d\n", f, t);
        if (off == len && async_recv(f)) {
			DEBUG("AA\n");
            return 1;
        }

		DEBUG("pipling async_send %d %d\n", f, t);
        if (off < len && async_send(t)) {
			DEBUG("BB\n");
            return 1;
        }
    }

    return 0;
}

int main(int argc, char *argv[])
{
	WSADATA data;
    DWORD ignore;
    pipling_t s2f, f2s;
    HANDLE std_thread[2];

	WSAStartup(0x202, &data);

	int nfd = get_netcat_socket(argc, argv);
	if (nfd < 0) {
		perror("net_create");
		return 1;
	}

    _in_handle = CreateEvent(NULL, FALSE, FALSE, NULL);
    _out_handle = CreateEvent(NULL, FALSE, FALSE, NULL);
    pipling_t::handle = CreateEvent(NULL, FALSE, FALSE, NULL);
    InitializeCriticalSection(&_in_mutex);
    InitializeCriticalSection(&_out_mutex);

	_in_freeer = NULL;
	_out_freeer = NULL;
	for (int i = 0; i < MAX_BUF_CNT; i++) {
		_free_in_ring[i].o_use = 0;
		_free_in_ring[i].o_len = 0;
		_free_in_ring[i].o_next = _in_freeer;
		_in_freeer = &_free_in_ring[i];

		_free_out_ring[i].o_use = 0;
		_free_out_ring[i].o_len = 0;
		_free_out_ring[i].o_next = 0;

		_free_out_ring[i].o_next = _out_freeer;
		_out_freeer = &_free_out_ring[i];
	}

	s2f._io_buf = 0;
	WSAEventSelect(nfd, pipling_t::handle, 0);
    std_thread[0] = CreateThread(NULL, 0, pipling_stdout, 0, 0, &ignore);
    std_thread[1] = CreateThread(NULL, 0, pipling_stdin, 0, 0, &ignore);
    do {
        if (!s2f.pipling(-1, nfd)) {
            break;
        }

        if (!f2s.pipling(nfd, -1)) {
            break;
        }

		DEBUG("WaitForSingleObject thread start\n");
        WaitForSingleObject(pipling_t::handle, INFINITE);
		DEBUG("WaitForSingleObject thread end\n");
    } while (1);
    CloseHandle(std_thread[1]);
    CloseHandle(std_thread[0]);

    DeleteCriticalSection(&_out_mutex);
    DeleteCriticalSection(&_in_mutex);

    CloseHandle(pipling_t::handle);
    CloseHandle(_out_handle);
    CloseHandle(_in_handle);
	shutdown(nfd, SD_BOTH);
	closesocket(nfd);
    return 0;
}

