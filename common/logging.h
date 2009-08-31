#ifndef _LOGGING_H_
#define _LOGGING_H_

#ifdef _DEBUG
#define LOG(x) Log x
#else
#define LOG(x) do {} while(0)
#endif

void __cdecl Log(const char *fmt, ...);


#endif