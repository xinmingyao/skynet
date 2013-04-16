#ifndef SKYNET_H
#define SKYNET_H

#include <stddef.h>
#include <stdint.h>

#define PTYPE_TEXT 0
#define PTYPE_RESPONSE 1
#define PTYPE_MULTICAST 2
#define PTYPE_CLIENT 3
#define PTYPE_SYSTEM 4
#define PTYPE_HARBOR 5
#define PTYPE_TAG_DONTCOPY 0x10000
#define PTYPE_TAG_ALLOCSESSION 0x20000
#if defined(_WIN32)
#define SKYNET_API __declspec(dllexport)
#else
#define SKYNET_API 
#endif
struct skynet_context;

SKYNET_API void skynet_error(struct skynet_context * context, const char *msg, ...);
SKYNET_API const char * skynet_command(struct skynet_context * context, const char * cmd , const char * parm);
SKYNET_API uint32_t skynet_queryname(struct skynet_context * context, const char * name);
SKYNET_API int skynet_send(struct skynet_context * context, uint32_t source, uint32_t destination , int type, int session, void * msg, size_t sz);
SKYNET_API int skynet_sendname(struct skynet_context * context, const char * destination , int type, int session, void * msg, size_t sz);

SKYNET_API void skynet_forward(struct skynet_context *, uint32_t destination);
SKYNET_API int skynet_isremote(struct skynet_context *, uint32_t handle, int * harbor);

SKYNET_API typedef int (*skynet_cb)(struct skynet_context * context, void *ud, int type, int session, uint32_t source , const void * msg, size_t sz);
SKYNET_API void skynet_callback(struct skynet_context * context, void *ud, skynet_cb cb);
#undef SKYNET_API

#endif
