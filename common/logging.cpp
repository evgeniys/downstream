#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

void __cdecl Log(const char *fmt, ...)
{
	va_list args;

	va_start (args, fmt);

	char buf[0x2000];

	_vsnprintf(buf, _countof(buf), fmt, args);

	va_end(args);

	OutputDebugStringA(buf);
}

