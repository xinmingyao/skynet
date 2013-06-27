#include "skynet.h"
#include "event.h"
#include "tcpserver.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#define TCP_OPEN 1
#define TCP_CLOSED 2
struct write_buffer {
	struct write_buffer * next;
	char *ptr;
	size_t sz;
	void *buffer;
};

struct socket {
	int fd;
	int id;
	int status;
    struct sockaddr_in remote_addr;
	struct write_buffer * head;
	struct write_buffer * tail;
};


struct tcpserver_pool {
        uint32_t service;
        int session;
	int cap;
	int listen_fd;
	int efd;
	int max_connection;
	int closed;
	int active;
	int skip;
        int count;
        int id;
	struct socket * sockets;
	struct event ev[MAX_EVENT];
	
};


struct socket * add_client(struct tcpserver_pool * self, int fd);
void  try_close(struct tcpserver_pool * self, struct socket * s) ;
int tcpserver_pool_create(struct tcpserver_pool * pool,uint32_t addr, int port , int max , int buffer);
void tcpserver_poll_close(struct tcpserver_pool *m);




static int
_set_nonblocking(int fd)
{
	int flag = fcntl(fd, F_GETFL, 0);
	if ( -1 == flag ) {
		return -1;
	}

	return fcntl(fd, F_SETFL, flag | O_NONBLOCK);
}


static int
new_socket(struct tcpserver_pool *p, int sock, struct sockaddr_in addr) {
	int i;
	if (p->count >= p->cap) {
		goto _error;
	}
	for (i=0;i<p->cap;i++) {
		int id = p->id + i;
		int n = id % p->cap;
		struct socket * s = &p->sockets[n];
		if (s->status == STATUS_INVALID) {
			if (event_add(p->efd, sock, s)) {
				goto _error;
			}
			s->status = STATUS_SUSPEND;
			_set_nonblocking(sock);
			int keepalive = 1; 
			setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepalive , sizeof(keepalive));
			s->fd = sock;
			s->id = id;
			s->remote_addr = addr;
			p->count++;
			if (++p->id > MAX_ID) {
				p->id = 1;
			}
			assert(s->head == NULL && s->tail == NULL);
			return id;
		}
	}
_error:
	close(sock);
	return -1;
}


static void
force_close(struct socket *s, struct tcpserver_pool *p) {
	struct write_buffer *wb = s->head;
	while (wb) {
		struct write_buffer *tmp = wb;
		wb = wb->next;
		free(tmp->buffer);
		free(tmp);
	}
	s->head = s->tail = NULL;
	s->status = STATUS_INVALID;
	event_del(p->efd, s->fd);
	close(s->fd);
       
}



struct tcpserver_pool *
tcpserver_create(void) {
	struct tcpserver_pool * g = malloc(sizeof(*g));
	memset(g,0,sizeof(*g));
	return g;
}


static void
_parm(char *msg, int sz, int command_sz) {
	while (command_sz < sz) {
		if (msg[command_sz] != ' ')
			break;
		++command_sz;
	}
	int i;
	for (i=command_sz;i<sz;i++) {
		msg[i-command_sz] = msg[i];
	}
	msg[i-command_sz] = '\0';
}

static void
_ctrl(struct skynet_context * ctx, struct tcpserver_pool * g, const void * msg, int sz) {
	char tmp[sz+1];
	memcpy(tmp, msg, sz);
	tmp[sz] = '\0';
	char * command = tmp;
	int i;
	if (sz == 0)
		return;
	for (i=0;i<sz;i++) {
		if (command[i]==' ') {
			break;
		}
	}
	if (memcmp(command,"service",i)==0) {
		_parm(tmp, sz, i);
		g->service = skynet_queryname(ctx, command);
		return;
	}
	if (memcmp(command,"start",i) == 0) {
		skynet_command(ctx,"TIMEOUT","0");
		return;
	}
	skynet_error(ctx, "[tcp server] Unkown command : %s", command);
}

//////////////////////////

static void
forward(struct skynet_context * context, struct socket *s, struct tcpserver_pool *p) {
	int * buffer = malloc(READ_BUFFER + sizeof(int));
	*buffer = s->id;	// convert endian ?
	int r = 0;
	for (;;) {
		r = read(s->fd, buffer+1, READ_BUFFER);
		if (r == -1) {
			switch(errno) {
			case EWOULDBLOCK:
				free(buffer);
				return;
			case EINTR:
				continue;
			}
			r = 0;
			break;
		}
		break;
	}
	if (r == 0) {
		force_close(s,p);
	}

	if (s->status == STATUS_HALFCLOSE) {
	  free(buffer);
	} else {
	  //skynet_send(context, 0, p->service, PTYPE_CLIENT,  p->session, (char *)buffer, r + 4);
	  skynet_send(context, 0, p->service, PTYPE_CLIENT|PTYPE_TAG_DONTCOPY, 0, (char *)buffer, r + 4);
	  
	}
}

static void
sendout(struct tcpserver_pool *p, struct socket *s) {
  
	while (s->head) {
		struct write_buffer * tmp = s->head;
		for (;;) {
			int sz = write(s->fd, tmp->ptr, tmp->sz);
			if (sz < 0) {
				switch(errno) {
				case EINTR:
					continue;
				case EAGAIN:
					return;
				}
				force_close(s,p);
				return;
			}
			if (sz != tmp->sz) {
				tmp->ptr += sz;
				tmp->sz -= sz;
				return;
			}
			break;
		}
		s->head = tmp->next;
		free(tmp->buffer);
		free(tmp);
	}
	s->tail = NULL;
	event_write(p->efd, s->fd, s, false);
}

static int
try_send(struct skynet_context *ctx, struct tcpserver_pool *p, uint32_t source, const int * msg, size_t sz) {
	if (sz < 4) {
		skynet_error(ctx, "%x invalid message", source);
		return 0;
	}
	sz-=4;
	int id = *msg;
	struct socket * s = &p->sockets[id % p->cap];
	if (id != s->id) {
		skynet_error(ctx, "%x write to invalid socket %d", p->service, id);
		return 0;
	}

	if (s->status != STATUS_SUSPEND) {
		skynet_error(ctx, "%x write to closed socket %d", p->service, id);
		return 0;
	}
	if (s->head) {
		struct write_buffer * buf = malloc(sizeof(*buf));
		buf->ptr = (char *)(msg+1);
		buf->buffer = (void *)msg;
		buf->sz = sz;
		assert(s->tail != NULL);
		assert(s->tail->next == NULL);
		buf->next = s->tail->next;
		s->tail->next = buf;
		s->tail = buf;
		return 1;
	}

	char * ptr = (char *)(msg+1);

	for (;;) {
		int wt = write(s->fd, ptr, sz);
		if (wt < 0) {
			switch(errno) {
			case EINTR:
				continue;
			}
			break;
		}
		if (wt == sz) {
			return 0;
		}
		sz-=wt;
		ptr+=wt;

		break;
	}

	struct write_buffer * buf = malloc(sizeof(*buf));
	buf->next = NULL;
	buf->ptr = ptr;
	buf->sz = sz;
	buf->buffer = (void *)msg;
	s->head = s->tail = buf;

	event_write(p->efd, s->fd, s, true);

	return 1;
}

///////////////////////
static int
_cb(struct skynet_context * ctx, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
  struct tcpserver_pool * self=(struct tcpserver_pool *)ud;
	if (type == PTYPE_TEXT) {
		_ctrl(ctx, self , msg , (int)sz);
		return 0;
	} else if (type == PTYPE_CLIENT) {
	        return try_send(ctx, self, source, msg, sz);

	}

	assert(type == PTYPE_RESPONSE);
	int n = event_wait(self->efd, self->ev, 100); // timeout : 100ms
	int i;
	for (i=0;i<n;i++) {
		struct event *e = &self->ev[i];
		struct socket * s = e->s;
		if (s ==LISTENSOCKET ) {
			struct sockaddr_in remote_addr;
			socklen_t len = sizeof(struct sockaddr_in);
			int client_fd = accept(self->listen_fd , (struct sockaddr *)&remote_addr ,  &len);
			if (client_fd >= 0) {
				
			 //printf("TCPSERVER connect %s:%u (fd=%d)\n",inet_ntoa(remote_addr.sin_addr),ntohs(remote_addr.sin_port), client_fd);
			  int id=new_socket(self,client_fd,remote_addr);
			  if (id>=0) {
			    int cmd= TCP_OPEN;
			    char * buf= malloc(sizeof(*buf)*8);
			    if(buf){
			      memcpy(buf,&cmd,4);
			      memcpy(buf+4,&id,4);
			      //send socket open to service
			      skynet_send(ctx, 0, self->service, PTYPE_SYSTEM|PTYPE_TAG_DONTCOPY,session, buf, 8);
			    }
			  }
			}
		}else{
		if (e->read) {
		   forward(ctx,e->s,ud);
		}
		if (e->write) {
		  struct socket *s = e->s;
		  sendout(self, s);
		  if (s->status == STATUS_HALFCLOSE && s->head == NULL) {
		    force_close(s, self);
		    int cmd= TCP_CLOSED;
		    int id =s->id;
		    int len = 8;
		    char * buf= malloc(len);
		    if(buf){
		      memcpy(buf,&cmd,4);
		      memcpy(buf+4,&id,4);
		      //send socket close to service
		      skynet_send(ctx, 0, self->service, PTYPE_SYSTEM|PTYPE_TAG_DONTCOPY,session, buf, 8);
		    }
		  }
		}
		}
	}
	skynet_command(ctx, "TIMEOUT", "0");
    return 0;
}

int
tcpserver_init(struct tcpserver_pool *g , struct skynet_context * ctx, char * parm) {
	int port = 0;
	int max = 0;
	int buffer = 0;
	int sz = strlen(parm)+1;
	int service;
	char binding[sz];

	int n = sscanf(parm, "%s %d %d %d ",binding,&service , &max,&buffer);
	if (n<4) {
		skynet_error(ctx, "Invalid gate parm %s",parm);
		return 1;
	}
	if (max <=0 ) {
		skynet_error(ctx, "Need max conntection");
		return 1;
	}
	char * portstr = strchr(binding,':');
	uint32_t addr = INADDR_ANY;
	if (portstr == NULL) {
		port = strtol(binding, NULL, 10);
		if (port <= 0) {
			skynet_error(ctx, "Invalid gate address %s",parm);
			return 1;
		}
	} else {
		port = strtol(portstr + 1, NULL, 10);
		if (port <= 0) {
			skynet_error(ctx, "Invalid gate address %s",parm);
			return 1;
		}
		portstr[0] = '\0';
		addr=inet_addr(binding);
	}

	int r = tcpserver_pool_create(g,addr, port, max, buffer);
	if (r == 0) {
		skynet_error(ctx, "Create TcpServer %s failed",parm);
		return 1;
	}

	g->service = service;


	g->cap = max;
	g->max_connection = max;

	skynet_callback(ctx,g,_cb);

	return 0;
}




int
tcpserver_pool_create(struct tcpserver_pool *self,uint32_t addr, int port , int max , int buffer_size) {
	int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd == -1) {
		return 0;
	}
	if ( -1 == _set_nonblocking(listen_fd) ) {
		return 0;
	}

	int reuse = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int));

	struct sockaddr_in my_addr;
	memset(&my_addr, 0, sizeof(struct sockaddr_in));
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(port);
	my_addr.sin_addr.s_addr = addr;
//	printf("TCPSERVER bind %s:%u\n",inet_ntoa(my_addr.sin_addr),ntohs(my_addr.sin_port));
	if (bind(listen_fd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) == -1) {
		close(listen_fd);
		return 0;
	}
	if (listen(listen_fd, BACKLOG) == -1) {
		close(listen_fd);
		return 0;
	}

	int efd = event_init(max+1);
	if (efd == -1) {
		close(listen_fd);
		return 0;
	}

	if (event_add(efd, listen_fd, LISTENSOCKET) == -1) {
		close(listen_fd);
		close(efd);
		return 0;
	}
	self->listen_fd = listen_fd;
	self->efd = efd;
	self->max_connection = max;
	self->sockets = malloc(sizeof(struct socket) * max);
	memset(self->sockets, 0, sizeof(struct socket) * max);

	return 1;
}

void
tcpserver_release(struct tcpserver_pool *pool) {
	if (pool == NULL)
		return;
	if (pool->efd >= 0) {
		close(pool->efd);
	}
	int i;
	for (i=0;i<pool->cap;i++) {
		if (pool->sockets[i].status != STATUS_INVALID && pool->sockets[i].fd >=0) {
			close(pool->sockets[i].fd);
		}
	}
	free(pool->sockets);
	free(pool);

}
