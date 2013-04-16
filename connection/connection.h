#ifndef SKYNET_CONNECTION_H
#define SKYNET_CONNECTION_H

#include <stddef.h>
#if defined(_WIN32)
#include <windows.h>
#include <WS2tcpip.h>
#pragma comment(lib, "wsock32.lib")
#pragma comment(lib,"winsock.lib")
#pragma comment(lib,"ws2_32.lib")
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

struct connection_pool;
struct connection {
	#if defined(WIN32)
	OVERLAPPED ol;
	WSABUF buffer;
	char data[128];
	#endif 
	int fd;
	uint32_t address;
	int close;
};
struct connection_pool;

struct connection_pool * connection_newpool(int max);
void connection_deletepool(struct connection_pool *);

int connection_add(struct connection_pool *, int fd, void *ud);
void connection_del(struct connection_pool *, int fd);

void * connection_poll(struct connection_pool *, int timeout);

#endif
