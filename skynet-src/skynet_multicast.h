#ifndef SKYNET_MULTICAST_H
#define SKYNET_MULTICAST_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define SKYNET_API __declspec(dllexport)
#else
#define SKYNET_API 
#endif
struct skynet_multicast_message;
struct skynet_multicast_group;
struct skynet_context;

typedef void (*skynet_multicast_func)(void *ud, uint32_t source, const void * msg, size_t sz);

SKYNET_API struct skynet_multicast_message * skynet_multicast_create(const void * msg, size_t sz, uint32_t source);
SKYNET_API void skynet_multicast_copy(struct skynet_multicast_message *, int copy);
SKYNET_API void skynet_multicast_dispatch(struct skynet_multicast_message * msg, void * ud, skynet_multicast_func func);
SKYNET_API void skynet_multicast_cast(struct skynet_context * from, struct skynet_multicast_message *msg, const uint32_t *dests, int n);

SKYNET_API struct skynet_multicast_group * skynet_multicast_newgroup();
SKYNET_API void skynet_multicast_deletegroup(struct skynet_multicast_group * group);
SKYNET_API void skynet_multicast_entergroup(struct skynet_multicast_group * group, uint32_t handle);
SKYNET_API void skynet_multicast_leavegroup(struct skynet_multicast_group * group, uint32_t handle);
SKYNET_API int skynet_multicast_castgroup(struct skynet_context * from, struct skynet_multicast_group * group, struct skynet_multicast_message *msg);

#undef SKYNET_API 
#endif
