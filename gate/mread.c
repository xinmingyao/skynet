#include "mread.h"
#include "ringbuffer.h"

/* Test for polling API */
#ifdef __linux__
#define HAVE_EPOLL 1
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined (__NetBSD__)
#define HAVE_KQUEUE 1
#endif

#if !defined(HAVE_EPOLL) && !defined(HAVE_KQUEUE) &&!defined(_WIN32)
#error "system does not support epoll or kqueue API or iocp"
#endif
/* ! Test for polling API */


#ifdef HAVE_EPOLL
#include <sys/epoll.h>
#elif HAVE_KQUEUE
#include <sys/event.h>
#endif

#include <sys/types.h>
#if defined(_WIN32)

#include <windows.h>
#include <WS2tcpip.h>
#pragma comment(lib, "wsock32.lib")
#pragma comment(lib,"winsock.lib")
#pragma comment(lib,"ws2_32.lib")
#include <mswsock.h>
#include <winsock2.h>
/* Mingw's headers don't define LPFN_ACCEPTEX. */
#define WSAID_ACCEPTEX {0xb5367df1,0xcbac,0x11cf,{0x95,0xca,0x00,0x80,0x5f,0x48,0xa1,0x92}}
typedef BOOL (WINAPI *LPFN_ACCEPTEX)(SOCKET, SOCKET, PVOID, DWORD, DWORD, DWORD, LPDWORD, LPOVERLAPPED);
LPFN_ACCEPTEX lpfnAcceptEx = NULL;
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#endif

#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <fcntl.h>

#define BACKLOG 32
#define READQUEUE 32
#define READBLOCKSIZE 2048
#define RINGBUFFER_DEFAULT 1024 * 1024

#define SOCKET_INVALID 0
#define SOCKET_CLOSED 1
#define SOCKET_SUSPEND 2
#define SOCKET_READ 3
#define SOCKET_POLLIN 4

#define SOCKET_ALIVE	SOCKET_SUSPEND

#define LISTENSOCKET (void *)((intptr_t)~0)
static data[1024];
static struct socket *
_add_client(struct mread_pool * self, int fd) ;
struct socket {
	#if defined(WIN32)
	OVERLAPPED ol;
	WSABUF buffer;
	char data[128];
	#endif 
	int fd;
	struct ringbuffer_block * node;
	struct ringbuffer_block * temp;
	int status;
};

struct mread_pool {
	int listen_fd;
#ifdef HAVE_EPOLL
	int epoll_fd;
#elif HAVE_KQUEUE
	int kqueue_fd;
#else
	HANDLE iocp;
	WSAEVENT           events[1]; 
	struct socket * ev[0]; //
#endif
	int max_connection;
	int closed;
	int active;
	int skip;
	struct socket * sockets;
	struct socket * free_socket;
	int queue_len;
	int queue_head;
#ifdef HAVE_EPOLL
	struct epoll_event ev[READQUEUE];
#elif HAVE_KQUEUE
	struct kevent ev[READQUEUE];
#endif
	struct ringbuffer * rb;
};

static struct socket *
_create_sockets(int max) {
	int i;
	struct socket * s = malloc(max * sizeof(struct socket));
	for (i=0;i<max;i++) {
		s[i].fd = i+1;
		s[i].node = NULL;
		s[i].temp = NULL;
		s[i].status = SOCKET_INVALID;
	}
	s[max-1].fd = -1;
	return s;
}

static struct ringbuffer *
_create_rb(int size) {
	size = (size + 3) & ~3;
	if (size < READBLOCKSIZE * 2) {
		size = READBLOCKSIZE * 2;
	}
	struct ringbuffer * rb = ringbuffer_new(size);

	return rb;
}

static void
_release_rb(struct ringbuffer * rb) {
	ringbuffer_delete(rb);
}

static int
_set_nonblocking(int fd)
{
#if defined(_WIN32)
	unsigned long flag=1;
	int r = ioctlsocket(fd,FIONBIO,&flag); 
	if (r!=0){ 
    	  return -1; 
	}
	return r;
#else
	int flag = fcntl(fd, F_GETFL, 0);
	if ( -1 == flag ) {
		return -1;
	}
	return fcntl(fd, F_SETFL, flag | O_NONBLOCK);
#endif
}

struct mread_pool *
mread_create(uint32_t addr, int port , int max , int buffer_size) {
	int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd == -1) {
		return NULL;
	}
	if ( -1 == _set_nonblocking(listen_fd) ) {
		return NULL;
	}

	int reuse = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int));

	struct sockaddr_in my_addr;
	memset(&my_addr, 0, sizeof(struct sockaddr_in));
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(port);
	my_addr.sin_addr.s_addr = addr;
//	printf("MREAD bind %s:%u\n",inet_ntoa(my_addr.sin_addr),ntohs(my_addr.sin_port));
	if (bind(listen_fd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) == -1) {
		close(listen_fd);
		return NULL;
	}
	if (listen(listen_fd, BACKLOG) == -1) {
		close(listen_fd);
		return NULL;
	}

	struct mread_pool * self = malloc(sizeof(*self));
	self->max_connection = max;
	self->closed = 0;
	self->active = -1;
	self->skip = 0;
	self->sockets = _create_sockets(max);
	self->free_socket = &self->sockets[0];
	self->queue_len = 0;
	self->queue_head = 0;
	if (buffer_size == 0) {
		self->rb = _create_rb(RINGBUFFER_DEFAULT);
	} else {
		self->rb = _create_rb(buffer_size);
	}

#ifdef HAVE_EPOLL
	int epoll_fd = epoll_create(max + 1);
	if (epoll_fd == -1) {
		close(listen_fd);
		return NULL;
	}

	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.ptr = LISTENSOCKET;

	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) == -1) {
		close(listen_fd);
		close(epoll_fd);
		return NULL;
	}
#elif HAVE_KQUEUE
	int kqueue_fd = kqueue();
	if (kqueue_fd == -1) {
		close(listen_fd);
		return NULL;
	}

	struct kevent ke;
	EV_SET(&ke, listen_fd, EVFILT_READ, EV_ADD, 0, 0, LISTENSOCKET);
	if (kevent(kqueue_fd, &ke, 1, NULL, 0, NULL) == -1) {
		close(listen_fd);
		close(kqueue_fd);
		return NULL;
	}
#elif defined(_WIN32)
	HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0,2);//todo fix 2
	if(iocp == NULL){
		printf("error:create iocp:%d \n",WSAGetLastError());
		return NULL;
	}
	CreateIoCompletionPort((HANDLE)listen_fd,iocp,listen_fd, 0);
	GUID guidAcceptEx = WSAID_ACCEPTEX;
	DWORD dwBytes = 0;
        if (!WSAIoctl(listen_fd, SIO_GET_EXTENSION_FUNCTION_POINTER,
        	&guidAcceptEx, sizeof(guidAcceptEx), &lpfnAcceptEx, sizeof(lpfnAcceptEx),
        	&dwBytes, NULL, NULL) == 0)
   	 {
        	printf("WSAIoctl error:%d \n",WSAGetLastError());
        	return NULL;
    	}	

	
	int peer_socket = socket(AF_INET,SOCK_STREAM,0);
	if(peer_socket== INVALID_SOCKET){
        	printf("create socket  error:%d \n",WSAGetLastError());
		return NULL;
	}
	struct socket * s2=_add_client(self,peer_socket);
	BOOL bRet = lpfnAcceptEx(listen_fd,peer_socket,s2->data, 0,
        	sizeof(SOCKADDR_IN)+16, sizeof(SOCKADDR_IN)+16, &dwBytes,
       	 &(s2->ol));
   	 if (bRet == FALSE)
   	 {
        	if (WSAGetLastError() != ERROR_IO_PENDING){
        		printf("WSAIoctl error:%d \n",WSAGetLastError());
			return NULL;
		}
    	}
#endif
	self->listen_fd = listen_fd;
#ifdef HAVE_EPOLL
	self->epoll_fd = epoll_fd;
#elif HAVE_KQUEUE
	self->kqueue_fd = kqueue_fd;
#elif defined(_WIN32)
	self->iocp= iocp;
#endif
	return self;
}

void
mread_close(struct mread_pool *self) {
	if (self == NULL)
		return;
	int i;
	struct socket * s = self->sockets;
	for (i=0;i<self->max_connection;i++) {
		if (s[i].status >= SOCKET_ALIVE) {
			close(s[i].fd);
		}
	}
	free(s);
	if (self->listen_fd >= 0) {
		close(self->listen_fd);
	}
#ifdef HAVE_EPOLL
	close(self->epoll_fd);
#elif HAVE_KQUEUE
	close(self->kqueue_fd);
#endif
	_release_rb(self->rb);
	free(self);
}

static int
_read_queue(struct mread_pool * self, int timeout) {
	self->queue_head = 0;
#ifdef HAVE_EPOLL
	int n = epoll_wait(self->epoll_fd , self->ev, READQUEUE, timeout);
#elif HAVE_KQUEUE
	struct timespec timeoutspec;
	timeoutspec.tv_sec = timeout / 1000;
	timeoutspec.tv_nsec = (timeout % 1000) * 1000000;
	int n = kevent(self->kqueue_fd, NULL, 0, self->ev, READQUEUE, &timeoutspec);
#elif defined(WIN32)
	 int                n,key,flags=0;
    	 u_long             bytes=0;
	 struct socket * _socket;
	int rc = GetQueuedCompletionStatus(self->iocp, &bytes, (LPDWORD) &key,
                                   (LPOVERLAPPED*)&_socket, (u_long) timeout);

    	if (rc == 0) {
		self->queue_len = 0;
        	return -1;
    	} 
	if(key==self->listen_fd){//accept	
		int peer_socket = socket(AF_INET,SOCK_STREAM,0);
		if(peer_socket== INVALID_SOCKET){
        	printf("create socket  error:%d \n",WSAGetLastError());
		self->queue_len = 0;
		return -1;
		}
		struct socket * s2=_add_client(self,peer_socket);
		BOOL bRet = lpfnAcceptEx(self->listen_fd,peer_socket , s2->data, 0,
        	sizeof(SOCKADDR_IN)+16, sizeof(SOCKADDR_IN)+16, &bytes,
       		 &(s2->ol));
   	 	if (bRet == FALSE)
   		 {
        		if (WSAGetLastError() != ERROR_IO_PENDING){
        			printf("WSAIoctl error:%d \n",WSAGetLastError());
				self->queue_len = 0;
				return -1;
			}
    		}
		if (setsockopt(_socket->fd, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,  
                (char*)&(self->listen_fd), sizeof(self->listen_fd)) == SOCKET_ERROR)
            	{
               		printf("set opt error:%d \n",WSAGetLastError());	
			self->queue_len = 0;
			return -1;
            	}
		//recv zero data for iocp	
		CreateIoCompletionPort(_socket->fd,self->iocp,_socket->fd,0);
		ZeroMemory(&(_socket->ol),sizeof(OVERLAPPED));
    		if (WSARecv(_socket->fd, &(_socket->buffer),1, &bytes, &flags, &(_socket->ol), NULL) == SOCKET_ERROR)  
   		{  
   		DWORD error; 
		error = GetLastError();
		printf("GetLastError is : %d , recvBytes = %d \n",error,bytes);
		}
       		n=-1;	
	}else{ //read
		n=1;
		self->ev[0]=_socket;
	}
        
#endif
	if (n == -1) {
		self->queue_len = 0;
		return -1;
	}
	self->queue_len = n;
	return n;
}

inline static struct socket *
_read_one(struct mread_pool * self) {
	if (self->queue_head >= self->queue_len) {
		return NULL;
	}
#ifdef HAVE_EPOLL
	return self->ev[self->queue_head ++].data.ptr;
#elif HAVE_KQUEUE
	return self->ev[self->queue_head ++].udata;
#else
	return self->ev[self->queue_head ++];
#endif
}

static struct socket *
_alloc_socket(struct mread_pool * self) {
	if (self->free_socket == NULL) {
		return NULL;
	}
	struct socket * s = self->free_socket;
	int next_free = s->fd;
	if (next_free < 0 ) {
		self->free_socket = NULL;
	} else {
		self->free_socket = &self->sockets[next_free];
	}
#if defined(_WIN32) //win32 zero overlap
	ZeroMemory(&s->ol,sizeof(OVERLAPPED));
        s->buffer.len=0; //zero data recv use iocp like epoll
#endif 
	return s;
}

static struct socket *
_add_client(struct mread_pool * self, int fd) {
	struct socket * s = _alloc_socket(self);
	if (s == NULL) {
		close(fd);
		return NULL;
	}
#ifdef HAVE_EPOLL
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.ptr = s;
	if (epoll_ctl(self->epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		close(fd);
		return NULL;
	}
#elif HAVE_KQUEUE
	struct kevent ke;
	EV_SET(&ke, fd, EVFILT_READ, EV_ADD, 0, 0, s);
	if (kevent(self->kqueue_fd, &ke, 1, NULL, 0, NULL) == -1) {
		close(fd);
		return NULL;
	}
     
#endif

	s->fd = fd;
	s->node = NULL;
	s->status = SOCKET_SUSPEND;

	return s;
}

static int
_report_closed(struct mread_pool * self) {
	int i;
	for (i=0;i<self->max_connection;i++) {
		if (self->sockets[i].status == SOCKET_CLOSED) {
			self->active = i;
			return i;
		}
	}
	assert(0);
	return -1;
}

int
mread_poll(struct mread_pool * self , int timeout) {
	self->skip = 0;
	if (self->active >= 0) {
		struct socket * s = &self->sockets[self->active];
		if (s->status == SOCKET_READ) {
			return self->active;
		}
	}
	if (self->closed > 0 ) {
		return _report_closed(self);
	}
	if (self->queue_head >= self->queue_len) {
		if (_read_queue(self, timeout) == -1) {
			self->active = -1;
			return -1;
		}
	}
/*
#if defined(_WIN32)
	for(;;){//accept socket
		int n = WSAWaitForMultipleEvents(1, self->events,
                                     0, timeout, 0);
		WSANETWORKEVENTS  ne;
		if (n == WAIT_FAILED) {     
            	break;
       		 }
        	if (n == WAIT_TIMEOUT) {
            	break;
       		 }

        	n -= WSA_WAIT_EVENT_0;
		if (WSAEnumNetworkEvents(self->listen_fd, self->events[n], &ne) == -1) {
            		break;
       		 }
	        if (ne.lNetworkEvents & FD_ACCEPT) {
		   
        	    SOCKET fd = accept(self->listen_fd, NULL, NULL);
		    struct socket * s = _add_client(self, fd);
		    if (CreateIoCompletionPort((HANDLE)fd,self->iocp,s, 0) == NULL) {
        		 break;
    		    }	 
			if (CreateIoCompletionPort((HANDLE)fd,self->iocp,1, 0) == NULL) {
			printf("add connection GetLastError is : %d  %d \n",GetLastError(),fd);
        		break;
       			 }
        		DWORD   flags = 0;       
    			DWORD   recvBytes =0;  
        		ZeroMemory(&s->ol,sizeof(OVERLAPPED));
    			if (WSARecv(fd, NULL,0, &recvBytes, &flags, &(s->ol), NULL) == SOCKET_ERROR)  
   			{  
   				DWORD error; 
				error = GetLastError();
				printf("GetLastError is : %d , recvBytes = %d \n",error,recvBytes);
       		
    			}		
        	   continue;
       		}
	}
#endif
*/
	for (;;) {
		struct socket * s = _read_one(self);
		if (s == NULL) {
			self->active = -1;
			return -1;
		}
		if (s == LISTENSOCKET) {
			struct sockaddr_in remote_addr;
			socklen_t len = sizeof(struct sockaddr_in);
			int client_fd = accept(self->listen_fd , (struct sockaddr *)&remote_addr ,  &len);
			if (client_fd >= 0) {
//				printf("MREAD connect %s:%u (fd=%d)\n",inet_ntoa(remote_addr.sin_addr),ntohs(remote_addr.sin_port), client_fd);
				struct socket * s = _add_client(self, client_fd);
				if (s) {
					self->active = -1;
					return s - self->sockets;
				}
			}
		} else {
			int index = s - self->sockets;
			assert(index >=0 && index < self->max_connection);
			self->active = index;
			s->status = SOCKET_POLLIN;
			return index;
		}
	}
}

int
mread_socket(struct mread_pool * self, int index) {
	return self->sockets[index].fd;
}

static void
_link_node(struct ringbuffer * rb, int id, struct socket * s , struct ringbuffer_block * blk) {
	if (s->node) {
		ringbuffer_link(rb, s->node , blk);
	} else {
		blk->id = id;
		s->node = blk;
	}
}

void
mread_close_client(struct mread_pool * self, int id) {
	struct socket * s = &self->sockets[id];
	s->status = SOCKET_CLOSED;
	s->node = NULL;
	s->temp = NULL;
	close(s->fd);
//	printf("MREAD close %d (fd=%d)\n",id,s->fd);

#ifdef HAVE_EPOLL
	epoll_ctl(self->epoll_fd, EPOLL_CTL_DEL, s->fd , NULL);
#elif HAVE_KQUEUE
	struct kevent ke;
	EV_SET(&ke, s->fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
	kevent(self->kqueue_fd, &ke, 1, NULL, 0, NULL);
#endif

	++self->closed;
}

static void
_close_active(struct mread_pool * self) {
	int id = self->active;
	struct socket * s = &self->sockets[id];
	ringbuffer_free(self->rb, s->temp);
	ringbuffer_free(self->rb, s->node);
	mread_close_client(self, id);
}

static char *
_ringbuffer_read(struct mread_pool * self, int *size) {
	struct socket * s = &self->sockets[self->active];
	if (s->node == NULL) {
		*size = 0;
		return NULL;
	}
	int sz = *size;
	void * ret;
	*size = ringbuffer_data(self->rb, s->node, sz , self->skip, &ret);
	return ret;
}

void *
mread_pull(struct mread_pool * self , int size) {
	if (self->active == -1) {
		return NULL;
	}
	struct socket *s = &self->sockets[self->active];
	int rd_size = size;
	char * buffer = _ringbuffer_read(self, &rd_size);
	if (buffer) {
		self->skip += size;
		return buffer;
	}
	switch (s->status) {
	case SOCKET_READ:
		s->status = SOCKET_SUSPEND;
	case SOCKET_CLOSED:
	case SOCKET_SUSPEND:
		return NULL;
	default:
		assert(s->status == SOCKET_POLLIN);
		break;
	}

	int sz = size - rd_size;
	int rd = READBLOCKSIZE;
	if (rd < sz) {
		rd = sz;
	}

	int id = self->active;
	struct ringbuffer * rb = self->rb;

	struct ringbuffer_block * blk = ringbuffer_alloc(rb , rd);
	while (blk == NULL) {
		int collect_id = ringbuffer_collect(rb);
		mread_close_client(self , collect_id);
		if (id == collect_id) {
			return NULL;
		}
		blk = ringbuffer_alloc(rb , rd);
	}

	buffer = (char *)(blk + 1);

	for (;;) {
		#if defined(WIN32)
 		int flags=0;
		int recv_bytes;
		int bytes = recv(s->fd, buffer, rd, 0);
		//recv zero data for iocp	
		CreateIoCompletionPort(s->fd,self->iocp,s->fd,0);
		ZeroMemory(&(s->ol),sizeof(OVERLAPPED));
    		if (WSARecv(s->fd, &(s->buffer),1, &recv_bytes, &flags, &(s->ol), NULL) == SOCKET_ERROR)  
   		{
			if(GetLastError() !=WSA_IO_PENDING){  
				ringbuffer_shrink(rb, blk, 0);
				_close_active(self);
				printf("GetLastError is : %d  \n",GetLastError());
				return NULL;
			}
		}
		#else
		int bytes = recv(s->fd, buffer, rd, MSG_DONTWAIT);
		#endif
		if (bytes > 0) {
			ringbuffer_shrink(rb, blk , bytes);
			if (bytes < sz) {
				_link_node(rb, self->active, s , blk);
				s->status = SOCKET_SUSPEND;
				return NULL;
			}
			s->status = SOCKET_READ;
			break;
		}
		if (bytes == 0) {
			ringbuffer_shrink(rb, blk, 0);
			_close_active(self);
			return NULL;
		}
		#if !defined(WIN32)
		if (bytes == -1) {
			switch(errno) {
			case EWOULDBLOCK:
				ringbuffer_shrink(rb, blk, 0);
				s->status = SOCKET_SUSPEND;
				return NULL;
			case EINTR:
				continue;
			default:
				ringbuffer_shrink(rb, blk, 0);
				_close_active(self);
				return NULL;
			}
		}
		#else
		if(bytes == -1){
			ringbuffer_shrink(rb, blk, 0);
			_close_active(self);
			return NULL;
		}	
		#endif
	}
	_link_node(rb, self->active , s , blk);
	void * ret;
	int real_rd = ringbuffer_data(rb, s->node , size , self->skip, &ret);
	if (ret) {
		self->skip += size;
		return ret;
	}
	assert(real_rd == size);
	struct ringbuffer_block * temp = ringbuffer_alloc(rb, size);
	while (temp == NULL) {
		int collect_id = ringbuffer_collect(rb);
		mread_close_client(self , collect_id);
		if (id == collect_id) {
			return NULL;
		}
		temp = ringbuffer_alloc(rb , size);
	}
	temp->id = id;
	if (s->temp) {
		ringbuffer_link(rb, temp, s->temp);
	}
	s->temp = temp;
	ret = ringbuffer_copy(rb, s->node, self->skip, temp);
	assert(ret);
	self->skip += size;

	return ret;
}

void
mread_yield(struct mread_pool * self) {
	if (self->active == -1) {
		return;
	}
	struct socket *s = &self->sockets[self->active];
	ringbuffer_free(self->rb , s->temp);
	s->temp = NULL;
	if (s->status == SOCKET_CLOSED && s->node == NULL) {
		--self->closed;
		s->status = SOCKET_INVALID;
		s->fd = self->free_socket - self->sockets;
		self->free_socket = s;
		self->skip = 0;
		self->active = -1;
	} else {
		if (s->node) {
			s->node = ringbuffer_yield(self->rb, s->node, self->skip);
		}
		self->skip = 0;
		if (s->node == NULL) {
			self->active = -1;
		}
	}
}

int
mread_closed(struct mread_pool * self) {
	if (self->active == -1) {
		return 0;
	}
	struct socket * s = &self->sockets[self->active];
	if (s->status == SOCKET_CLOSED && s->node == NULL) {
		mread_yield(self);
		return 1;
	}
	return 0;
}
