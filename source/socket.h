// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <errno.h>

#if defined(__unix__)
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#elif defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#error "Unsupported OS"
#endif

#if defined(__unix__)
#define socket_close close
#define socket_geterror() errno
#define socket_seterror(e) errno = (e)
#define SOCKET_EWOULDBLOCK EWOULDBLOCK
#define SOCKET_EINPROGRESS EINPROGRESS
#define SOCKET_EALREADY EALREADY
#define SOCKET_USE_POLL
typedef int SOCKET;
#elif defined(_WIN32)
#define socket_close closesocket
#define socket_geterror() WSAGetLastError()
#define socket_seterror(e) WSASetLastError(e)
#define SOCKET_EWOULDBLOCK WSAEWOULDBLOCK
#define SOCKET_EINPROGRESS WSAEINPROGRESS
#define SOCKET_EALREADY WSAEALREADY
typedef int ssize_t;
#endif

void socket_perror(const char *func);
int socket_straddr(char *res, unsigned res_len, char *res_port, struct sockaddr *addr, socklen_t addrlen);
int socket_hasdata(SOCKET socket);
int socket_isconnected(SOCKET socket);
int socket_wait(SOCKET *sockets, unsigned count, int delay);
int socket_setblocking(SOCKET socket, int flag);
SOCKET socket_connect(const char *host, const char *port);
