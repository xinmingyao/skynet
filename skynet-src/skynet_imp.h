#ifndef SKYNET_IMP_H
#define SKYNET_IMP_H

struct skynet_config {
	int thread;
	int harbor;
	const char * logger;
	const char * module_path;
	const char * master;
	const char * local;
	const char * start;
	const char * standalone;
};
#if defined(_WIN32)
#define SKYNET_API __declspec(dllexport)
#else
#define SKYNET_API 
#endif

SKYNET_API void skynet_start(struct skynet_config * config);

#undef SKYNET_API
#endif
