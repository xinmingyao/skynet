#ifndef TCPSERVER_H
#define TCPSERVER_H
#include <stdint.h>
#define LISTENSOCKET (void *)((intptr_t)~0)
#define MAX_ID 0x7fffffff
#define READ_BUFFER 4000
#define STATUS_INVALID 0
#define STATUS_HALFCLOSE 1
#define STATUS_SUSPEND 2


#define BACKLOG 32
#define READBLOCKSIZE 2048
#define RINGBUFFER_DEFAULT 1024 * 1024

#define SOCKET_INVALID 0
#define SOCKET_CLOSED 1
#define SOCKET_SUSPEND 2
#define SOCKET_READ 3
#define SOCKET_POLLIN 4
#define SOCKET_HALFCLOSE 5

#define SOCKET_ALIVE	SOCKET_SUSPEND




#endif
