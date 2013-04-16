# Use make -f Makefile.gcc PREFIX=i686-w64-mingw32-
# for cross compilation
CC = $(PREFIX)gcc
AR = $(PREFIX)ar 
E=.exe

SHARED = -fPIC -shared

CFLAGS=$(CPPFLAGS) -ggdb -g -Wall --std=gnu89 -D_WIN32_WINNT=0x0600
LDFLAGS=-lm -Ilua52 -Llua52 -llua52
SKYNETFLAGS= $(LDFLAGS) -Llua -L./  -lskynet10 -Iskynet-src 
WIN_SRCS=$(wildcard $(SRCDIR)/src/win/*.c)
WIN_OBJS=$(WIN_SRCS:.c=.o)

OBJS += skynet-src/skynet_handle.o
OBJS += skynet-src/skynet_module.o
OBJS += skynet-src/skynet_mq.o
OBJS += skynet-src/skynet_server.o
OBJS += skynet-src/skynet_start.o
OBJS += skynet-src/skynet_timer.o
OBJS += skynet-src/skynet_error.o
OBJS += skynet-src/skynet_harbor.o
OBJS += skynet-src/skynet_multicast.o
OBJS += skynet-src/skynet_group.o
OBJS += skynet-src/skynet_env.o
OBJS += skynet-src/skynet_monitor.o
OBJS += skynet-src/skynet_port_win32.o
OBJS += luacompat/compat52.o

SNLUAOBJ += service-src/service_lua.c

all : \
  skynet10.dll \
  skynet.exe \
  service/harbor.dll \
  service/snlua.dll \
  service/logger.dll \
  service/gate.dll \
  service/client.dll \
  service/connection.dll \
  service/master.dll \
  service/multicast.dll \
  service/tunnel.dll \
  service/localcast.dll \
  luaclib/skynet.dll \
  luaclib/socket.dll \
  luaclib/int64.dll \
  luaclib/mcast.dll \
  client
  
luaclib:
	mkdir luaclib

service/tunnel.dll : service-src/service_tunnel.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ $(SKYNETFLAGS)

service/multicast.dll : service-src/service_multicast.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ $(SKYNETFLAGS)

service/master.dll : service-src/service_master.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ $(SKYNETFLAGS) -lwsock32 -lWs2_32

service/harbor.dll : service-src/service_harbor.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ $(SKYNETFLAGS) -lwsock32 -lWs2_32

service/logger.dll : skynet-src/skynet_logger.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ $(SKYNETFLAGS)

service/snlua.dll : service-src/service_lua.c
	gcc $(CFLAGS) $(SHARED) -Iluacompat $^ -o $@  $(SKYNETFLAGS)

service/gate.dll : gate/mread.c gate/ringbuffer.c gate/main.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ -Igate $(SKYNETFLAGS) -lwsock32 -lWs2_32

service/localcast.dll : service-src/service_localcast.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ $(SKYNETFLAGS) -L../lua52

luaclib/skynet.dll : lualib-src/lua-skynet.c lualib-src/lua-seri.c lualib-src/lua-remoteobj.c lualib-src/trace_service.c | luaclib
	gcc $(CFLAGS) $(SHARED) -Iluacompat $^ -o $@ $(SKYNETFLAGS) -Iservice-src -Ilualib-src -Ilualib-src

service/client.dll : service-src/service_client.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ $(SKYNETFLAGS) -lwsock32 -lWs2_32

service/connection.dll : connection/connection.c connection/main.c
	gcc $(CFLAGS) $(SHARED) $^ -o $@ $(SKYNETFLAGS) -Iconnection -lwsock32 -lWs2_32

luaclib/socket.dll : connection/lua-socket.c | luaclib
	gcc $(CFLAGS) $(SHARED) -Iluacompat $^ -o $@ $(SKYNETFLAGS) -Iconnection -lwsock32 -lWs2_32 

luaclib/int64.dll : lua-int64/int64.c | luaclib
	gcc $(CFLAGS) $(SHARED) -Iluacompat -O2 $^ -o $@ -Ilua52 -Llua52 -llua52

luaclib/mcast.dll : lualib-src/lua-localcast.c | luaclib
	gcc $(CFLAGS) $(SHARED) -Iluacompat $^ -o $@ $(SKYNETFLAGS) -Iservice-src -lwsock32 -lWs2_32

client : client-src/client.c
	gcc $(CFLAGS) $^ -o $@ -lwsock32 -lWs2_32
clean :
	del /q skynet skynet.exe skynet10.dll libskynet.a client.exe 
	del /q service\*.dll 
	del /q luaclib\*.dll
	del /q luaclib\*.o
	del /q service-src\*.o
	del /q skynet-src\*.o
	del /q lualib-src\*.o


libskynet.a:$(OBJS)
	$(AR) rcs $@ $^
	
skynet.exe: skynet-src/skynet_main.c
	gcc $(CFLAGS)   -o $@ $^ -Iluacompat -Iskynet-src $(SKYNETFLAGS) 

skynet3: $(OBJS)
	gcc $(CFLAGS)   -o $@ $^ -Iluacompat -Iskynet-src -Ilua52 -Llua52  $(LDFLAGS)
	

skynet10.dll: $(OBJS)
	gcc $(CFLAGS)  $(SHARED) -o $@ $^ -Iluacompat -Iskynet-src -Ilua52 -Llua52 -llua52 $(LDFLAGS) -lwsock32 -lWs2_32
	
skynet-src/%.o:skynet-src/%.c
	$(CC) $(CFLAGS) -c  $< -o $@ -Iskynet-src -Ilua52 -Llua52 -llua52 -Iluacompat -I../luacompat -I../lua52 $(LDFLAGS)
luacompat/%.o:luacompat/%.c
	$(CC) $(CFLAGS) -c  $< -o $@ -Iskynet-src -Ilua52 -Llua52 -llua52 -Iluacompat -I../luacompat -I../lua52 $(LDFLAGS)

test: clean skynet.dll service/snlua.dll
	

	
	
	
	
