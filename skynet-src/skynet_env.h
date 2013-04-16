#ifndef SKYNET_ENV_H
#define SKYNET_ENV_H

#if defined(_WIN32)
#define SKYNET_API __declspec(dllexport)
#else
#define SKYNET_API 
#endif

SKYNET_API const char * skynet_getenv(const char *key);
SKYNET_API void skynet_setenv(const char *key, const char *value);

SKYNET_API void skynet_env_init();

#undef SKYNET_API
#endif
