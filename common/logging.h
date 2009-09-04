#ifndef _LOGGING_H_
#define _LOGGING_H_

#ifdef _DEBUG
#define LOG(x) Log x
#else
#define LOG(x) do {} while(0)
#endif

#ifdef __cplusplus
extern "C" {
#endif

void __cdecl Log(const char *fmt, ...);

#ifdef __cplusplus
};
#endif

#endif