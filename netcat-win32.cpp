#include <stdio.h>
#include <assert.h>
#include <iostream>

#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>

#include "ncatutil.h"

#pragma comment(lib, "ws2_32.lib")

#define THREAD_COUNT     2
#define MAX_BUFF_SIZE 8192

/*
 * ENABLE_DATA_COPY 1
 * DISABLE_NONBLOCK 1
 * ENABLE_ZERO_COPY 1
 */

#if defined(DISABLE_NONBLOCK) && defined(ENABLE_ZERO_COPY)
#error "build error"
#endif

using namespace std;

#define IO_ZERO_READ  0x00
#define IO_COOL_READ  0x01
#define IO_BYTE_WRITE 0x02

int get_netcat_socket(int argc, char *argv[]);

struct IO_DATA {
	WSAOVERLAPPED   overlapped;

	char *aiobuf;
	int   action;
	int   catfd;
};

static int do_post_quit(HANDLE hPort);
static int do_post_recv(HANDLE hPort, IO_DATA *iop);
static int do_post_send(HANDLE hPort, IO_DATA *iop, int len);

static int _quit_key_;
static OVERLAPPED _quit_olap;
#if defined(ENABLE_DATA_COPY)
static char _dummy[MAX_BUFF_SIZE];
#endif
static char _recv_buf[MAX_BUFF_SIZE];
static char _send_buf[MAX_BUFF_SIZE];

int do_post_quit(HANDLE hPort)
{
    ZeroMemory(&_quit_olap, sizeof(_quit_olap));

    for (int i = 0; i < THREAD_COUNT; i++)
        PostQueuedCompletionStatus(hPort, 0, (ULONG_PTR)&_quit_key_, &_quit_olap);

    return 0;
}

static void callback(HANDLE hPort, IO_DATA *lpData, DWORD dwTransfer)
{
	int nRet = dwTransfer;
	int catfd = lpData->catfd;

	switch (lpData->action) {
		case IO_ZERO_READ:
			assert(nRet == 0);
			nRet = recv(catfd, lpData->aiobuf, MAX_BUFF_SIZE, 0);
			if (nRet == -1) {
				do_post_quit(hPort);
				break;
			}

		case IO_COOL_READ:
#if defined(ENABLE_DATA_COPY)
			assert(nRet <= sizeof(_dummy));
			memcpy(_dummy, lpData->aiobuf, nRet);
#endif
			while (nRet > 0) {
				DWORD transfer = 0;

				if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), lpData->aiobuf, nRet, &transfer, NULL)) {
					cerr << "WriteFile failed:"<< GetLastError() <<endl;
					break;
				} else if (nRet != transfer) {
					cerr << "WriteFile failed:"<< GetLastError() <<endl;
					break;
				}

#if defined(DISABLE_NONBLOCK)
				do_post_recv(hPort, lpData);
				return;
#endif
				nRet = recv(catfd, lpData->aiobuf, MAX_BUFF_SIZE, 0);
				if (nRet == -1 && WSAGetLastError() == WSAEWOULDBLOCK) {
					do_post_recv(hPort, lpData);
					return;
				}
			}

			do_post_quit(hPort);
			break;

		case IO_BYTE_WRITE:
			if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), lpData->aiobuf, MAX_BUFF_SIZE, &dwTransfer, 0)) {
				do_post_quit(hPort);
			} else if (dwTransfer > 0) {
				do_post_send(hPort, lpData, dwTransfer);
			} else {
				do_post_quit(hPort);
			}

			break;
	}

	return;
}

static DWORD WINAPI WorkerThread(LPVOID lpArg)
{
	int nRet;
	int catfd;
	BOOL bStatus;
	HANDLE hPort = (HANDLE)lpArg;
	IO_DATA *lpData = NULL; 
	LPWSAOVERLAPPED lpOverlapped = NULL;

	DWORD dwFlags = 0;
	DWORD dwTransfer = 0;

	for ( ; ; ) {
		void *lpCompletionKey = NULL;

		bStatus = GetQueuedCompletionStatus(hPort, &dwTransfer, (LPDWORD)&lpCompletionKey, (LPOVERLAPPED *)&lpOverlapped, 10);
		if (!bStatus && GetLastError() == WAIT_TIMEOUT) {
			/* cerr << "GetQueuedCompletionStatus() timeout:"<< GetLastError() << endl; */
			continue;
		} else if (!bStatus) {
			cerr << "GetQueuedCompletionStatus() failed:"<< GetLastError() <<endl;
			do_post_quit(hPort);
			break;
		}

        if (lpOverlapped == &_quit_olap) {
            assert(&_quit_key_ == lpCompletionKey);
            break;
        }

		callback(hPort, (IO_DATA *)lpOverlapped, dwTransfer);
	}

	ExitProcess(0);
	return 0;
}

static int do_post_send(HANDLE hPort, IO_DATA *iop, int len)
{
	int nRet;
	WSABUF wbuf;
	DWORD dwSentBytes;

    wbuf.len = len;
    wbuf.buf = iop->aiobuf;

	iop->action = IO_BYTE_WRITE;
	ZeroMemory(&iop->overlapped, sizeof(iop->overlapped));

#if defined(ENABLE_DATA_COPY)
	assert(len <= sizeof(_dummy));
	memcpy(_dummy, iop->aiobuf, len);
#endif

	nRet = WSASend(iop->catfd, &wbuf, 1, &dwSentBytes, 0, &iop->overlapped, NULL);
	if (nRet == SOCKET_ERROR  && (ERROR_IO_PENDING != WSAGetLastError())) {
		cerr << "WASend Failed::Reason Code::"<< WSAGetLastError() << endl;
		return -1;
	}

	return 0;
}

static int do_post_recv(HANDLE hPort, IO_DATA *iop)
{
	int nRet;
	WSABUF wbuf;
	DWORD  dwFlags = 0, dwRecvBytes;

	wbuf.buf = iop->aiobuf;
	wbuf.len = MAX_BUFF_SIZE;
	iop->action = IO_COOL_READ;

#if defined(ENABLE_ZERO_COPY)
	wbuf.len = 0;
	iop->action = IO_ZERO_READ;
#endif
	ZeroMemory(&iop->overlapped, sizeof(iop->overlapped));

	nRet = WSARecv(iop->catfd, &wbuf, 1, &dwRecvBytes, &dwFlags, &iop->overlapped, NULL);

	if (nRet == SOCKET_ERROR  && (ERROR_IO_PENDING != WSAGetLastError())) {
		cerr << "WASRecv Failed::Reason Code::"<< WSAGetLastError() << endl;
		return -1;
	}

	return 0;
}

int main (int argc, char * argv[])
{
	int catfd;
	int retVal;
	DWORD dwTId;
	HANDLE hPort;
	WSADATA wData;
	unsigned long nonBlock = 1;

	if ((retVal = WSAStartup(MAKEWORD(2,2), &wData)) != 0) {
		cerr << "WSAStartup Failed::Reason Code::"<< retVal << endl;
		return 0;
	}

	catfd = get_netcat_socket(argc, argv);
	if (catfd == -1) {
		/* XXX */
		return 0;
	}

	retVal = ioctlsocket(catfd, FIONBIO, (unsigned long *)&nonBlock);
	if (retVal != 0) {
		/* XXX */
		return 0;
	}

	hPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (hPort == NULL) {
		cerr << "CreateIoCompletionPort() Failed::Reason::"<< GetLastError() <<endl;
		return 0;
	}

	if (CreateIoCompletionPort((HANDLE)catfd, hPort, 0, 0) == NULL) {
		cerr << "Binding Server Socket to IO Completion Port Failed::Reason Code::"<< GetLastError() <<endl;
		return 0;
	}

    IO_DATA *data = new IO_DATA;
    data->aiobuf = _recv_buf;
    data->catfd = catfd;
    if (do_post_recv(hPort, data)) {
        delete data;
        goto clean;
    }

    data = new IO_DATA;
    data->aiobuf = _send_buf;
    data->catfd = catfd;
    if (do_post_send(hPort, data, 0)) {
        delete data;
        goto clean;
    }

	for (DWORD dwThread = 1; dwThread < THREAD_COUNT; dwThread++) {
		HANDLE hThread = CreateThread(NULL, 0, WorkerThread, hPort, 0, &dwTId);
		CloseHandle(hThread);
	}

	WorkerThread(hPort);

clean:
	closesocket(catfd);
	WSACleanup();
	return 0;
} 

