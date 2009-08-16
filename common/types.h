#ifndef _TYPES_H_
#define _TYPES_H_

#include <string>
#include <windows.h>

#ifdef UNICODE
typedef std::wstring StlString;
#else
typedef std::string StlString;
#endif	

typedef CRITICAL_SECTION lock_t;
#define Lock(lk) EnterCriticalSection(lk)
#define Unlock(lk) LeaveCriticalSection(lk)
#define InitLock(lk) InitializeCriticalSection(lk)
#define CloseLock(lk) DeleteCriticalSection(lk)

#endif

