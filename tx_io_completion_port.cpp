#include <time.h>
#include <stdio.h>

#ifdef WIN32
#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>
#endif

#ifdef WIN32
#ifndef WSAID_TRANSMITFILE
#define WSAID_TRANSMITFILE \
{0xb5367df0,0xcbac,0x11cf,{0x95,0xca,0x00,0x80,0x5f,0x48,0xa1,0x92}}
#endif

#ifndef WSAID_ACCEPTEX
#define WSAID_ACCEPTEX \
{0xb5367df1,0xcbac,0x11cf,{0x95,0xca,0x00,0x80,0x5f,0x48,0xa1,0x92}}
typedef BOOL (PASCAL *LPFN_ACCEPTEX)(SOCKET, SOCKET, PVOID, DWORD,
        DWORD, DWORD, LPDWORD, LPOVERLAPPED);
#endif

#ifndef WSAID_CONNECTEX
#define WSAID_CONNECTEX \
{0x25a207b9,0xddf3,0x4660,{0x8e,0xe9,0x76,0xe5,0x8c,0x74,0x06,0x3e}}
typedef BOOL (PASCAL *LPFN_CONNECTEX)(SOCKET, const struct sockaddr *,
        int, PVOID, DWORD, LPDWORD, LPOVERLAPPED);
#endif

static HANDLE  io_completion_port;
static LPFN_ACCEPTEX lpAcceptEx = NULL;
static LPFN_CONNECTEX lpConnectEx = NULL;
#endif

static void poll_init(void)
{
#ifdef WIN32
	WSADATA wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);
	io_completion_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
#endif
}

static void poll_exit(void)
{
#ifdef WIN32
	CloseHandle(io_completion_port);
	WSACleanup();
#endif
}

