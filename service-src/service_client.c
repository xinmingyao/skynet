#include "skynet.h"
#if defined(_WIN32)
#include <windows.h>
#include <WS2tcpip.h>
#pragma comment(lib, "wsock32.lib")
#pragma comment(lib,"winsock.lib")
#pragma comment(lib,"ws2_32.lib")
#include <winsock2.h>
#else
#include <sys/uio.h>
#endif
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int
_cb(struct skynet_context * context, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	assert(sz <= 65535);
	int fd = (int)(intptr_t)ud;
	#if defined(WIN32)
	//todo fixme,use iocp WSASend
	uint8_t head[2] = { sz >> 8 & 0xff , sz & 0xff };
	send(fd,head,2,0);
	send(fd,msg,sz,0);
	return 0;
	#else
	struct iovec buffer[2];
	// send big-endian header
	uint8_t head[2] = { sz >> 8 & 0xff , sz & 0xff };
	buffer[0].iov_base = head;
	buffer[0].iov_len = 2;
	buffer[1].iov_base = (void *)msg;
	buffer[1].iov_len = sz;

	for (;;) {
		int err = writev(fd, buffer, 2);
		if (err < 0) {
			switch (errno) {
			case EAGAIN:
			case EINTR:
				continue;
			}
		}
		if (err < 0) {
			skynet_error(context, "Client socket error : Drop message from %x session = %d", source, session);
			return 0;
		}
		assert(err == sz +2);
		return 0;
	}
	#endif
}

#if defined(_WIN32)
__declspec(dllexport)
#endif  
int
client_init(void * dummy, struct skynet_context *ctx, const char * args) {
	int fd = strtol(args, NULL, 10);
	skynet_callback(ctx, (void*)(intptr_t)fd, _cb);

	return 0;
}
