// IOCP.cpp : 定义控制台应用程序的入口点。
//
#include <stdio.h>
#include <assert.h>
#include <iostream>

#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#define THREAD_COUNT 2
#define MAX_BUFF_SIZE                8192

using namespace std;

HANDLE g_hIOCP = INVALID_HANDLE_VALUE;
SOCKET g_ServerSocket = INVALID_SOCKET;

enum IO_OPERATION {IO_READ, IO_WRITE};

struct IO_DATA{
	WSAOVERLAPPED   Overlapped;
	char            Buffer[MAX_BUFF_SIZE];
	WSABUF          wsabuf;
	int             nTotalBytes;
	int             nSentBytes;
	IO_OPERATION    opCode;
	SOCKET          activeSocket;
};

static char _dummy[MAX_BUFF_SIZE + 1];
static int do_post_quit(void);
static int do_post_send(IO_DATA *iop, int len);
static int do_post_receive(IO_DATA *iop);

static int _quit_key_;
static OVERLAPPED _quit_olap;

int do_post_quit(void)
{
    ZeroMemory(&_quit_olap, sizeof(_quit_olap));
    for (int i = 0; i < THREAD_COUNT; i++)
        PostQueuedCompletionStatus(g_hIOCP, 0, (ULONG_PTR)&_quit_key_, &_quit_olap);
    return 0;
}

static DWORD WINAPI WorkerThread(LPVOID WorkThreadContext)
{
	int nRet = 0;
	BOOL bSuccess;
	WSABUF buffSend;
	IO_DATA *lpIOContext = NULL; 
	LPWSAOVERLAPPED lpOverlapped = NULL;

	DWORD dwFlags = 0;
	DWORD dwIoSize = 0;
	DWORD dwRecvNumBytes = 0;
	DWORD dwSendNumBytes = 0;

	for ( ; ; ) {
		void *lpCompletionKey = NULL;

		bSuccess = GetQueuedCompletionStatus(g_hIOCP, &dwIoSize, (LPDWORD)&lpCompletionKey, (LPOVERLAPPED *)&lpOverlapped, 10);
		if (!bSuccess && GetLastError() == WAIT_TIMEOUT) {
			//cerr << "GetQueuedCompletionStatus() timeout:"<< GetLastError() << endl;
			continue;
		} else if (!bSuccess) {
			cerr << "GetQueuedCompletionStatus() failed:"<< GetLastError() <<endl;
			break;
		}

        if (lpOverlapped == &_quit_olap) {
            assert(&_quit_key_ == lpCompletionKey);
            break;
        }

		lpIOContext = (IO_DATA *)lpOverlapped;
#if 0
		if (dwIoSize == 0 && lpIOContext->opCode == IO_READ) {
			cerr << "Client disconnect" << endl;
			do_post_quit();
			delete lpIOContext;
			break;
		}
#endif

		if (lpIOContext->opCode == IO_READ) {
			do {
				int error;
				DWORD transfer;
				assert(dwIoSize < sizeof(_dummy));
				/* memcpy(_dummy, lpIOContext->Buffer, dwIoSize); */
				error = recv(g_ServerSocket, lpIOContext->Buffer, sizeof(lpIOContext->Buffer), 0);
				if (error > 0) {
					dwIoSize = error;
					if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), lpIOContext->Buffer, dwIoSize, &transfer, NULL)) {
						do_post_quit();
						break;
					} else if (dwIoSize != transfer) {
						do_post_quit();
						break;
					}
				} else {
					if (error == -1 && WSAGetLastError() == WSAEWOULDBLOCK) {
						do_post_receive(lpIOContext);
					} else {
						do_post_quit();
					}
					break;
				}
			} while ( 1 );
        } else if (lpIOContext->opCode = IO_WRITE) {
            DWORD transfer;
            WSABUF buf = lpIOContext->wsabuf;
            if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), buf.buf, MAX_BUFF_SIZE, &transfer, 0)) {
                do_post_quit();
            } else if (transfer > 0) {
                do_post_send(lpIOContext, transfer);
            } else {
                do_post_quit();
            }
        }
	}

	return 0;
}

static int do_post_send(IO_DATA *iop, int len)
{
	int nRet;
	DWORD dwSendNumBytes;

	ZeroMemory(&iop->Overlapped, sizeof(iop->Overlapped));
	iop->opCode = IO_WRITE;
	iop->nTotalBytes = 0;
	iop->nSentBytes  = 0;
    iop->wsabuf.len  = len;
    iop->wsabuf.buf  = iop->Buffer;

	nRet = WSASend(g_ServerSocket, &iop->wsabuf, 1,
			&dwSendNumBytes, 0, &iop->Overlapped, NULL);

	if (nRet == SOCKET_ERROR  && (ERROR_IO_PENDING != WSAGetLastError())) {
		cerr << "WASend Failed::Reason Code::"<< WSAGetLastError() << endl;
		return -1;
	}

	assert(len < sizeof(_dummy));
	//memcpy(_dummy, iop->Buffer, len);
	return 0;
}

static int do_post_receive(IO_DATA *iop)
{
	int nRet;
	DWORD dwFlags = 0, dwRecvNumBytes;

	ZeroMemory(&iop->Overlapped, sizeof(iop->Overlapped));
	iop->opCode = IO_READ;
	iop->nTotalBytes = 0;
	iop->nSentBytes  = 0;
	iop->wsabuf.buf  = iop->Buffer;
	iop->wsabuf.len  = 0;//sizeof(iop->Buffer);

	nRet = WSARecv(g_ServerSocket, &iop->wsabuf, 1,
			&dwRecvNumBytes, &dwFlags, &iop->Overlapped, NULL);

	if (nRet == SOCKET_ERROR  && (ERROR_IO_PENDING != WSAGetLastError())) {
		cerr << "WASRecv Failed::Reason Code::"<< WSAGetLastError() << endl;
		return -1;
	}

	return 0;
}

int get_netcat_socket(int argc, char *argv[]);

int main (int argc, char * argv[])
{
	DWORD dwTId;
	WSADATA wsad;

	int retVal = -1;
	if ((retVal = WSAStartup(MAKEWORD(2,2), &wsad)) != 0) {
		cerr << "WSAStartup Failed::Reason Code::"<< retVal << endl;
		return 0;
	}

	g_ServerSocket = get_netcat_socket(argc, argv);
	g_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (g_hIOCP == NULL) {
		cerr << "CreateIoCompletionPort() Failed::Reason::"<< GetLastError() << endl;
		return 0;
	}

	unsigned long nblock = 1;
	int error = ioctlsocket(g_ServerSocket, FIONBIO, (unsigned long *)&nblock);

	if (CreateIoCompletionPort((HANDLE)g_ServerSocket, g_hIOCP, 0, 0) == NULL) {
		cerr << "Binding Server Socket to IO Completion Port Failed::Reason Code::"<< GetLastError() << endl;
		return 0;    
	}

    IO_DATA *data = new IO_DATA;
    if (do_post_receive(data)) {
        delete data;
        goto clean;
    }

    data = new IO_DATA;
    if (do_post_send(data, 0)) {
        delete data;
        goto clean;
    }

	for (DWORD dwThread = 1; dwThread < THREAD_COUNT; dwThread++) {
		HANDLE hThread = CreateThread(NULL, 0, WorkerThread, 0, 0, &dwTId);
		CloseHandle(hThread);
	}

	WorkerThread(NULL);

clean:
	closesocket(g_ServerSocket);
	WSACleanup();
	return 0;
} 

