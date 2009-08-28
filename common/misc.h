#ifndef _MISC_H_
#define _MISC_H_

#include "common/types.h"

std::string WideToAnsiString(const StlString& wstr);

bool HttpGetFileSize(const StlString& url, __out size_t& file_size);

bool HttpReadFileToBuffer(const StlString& url, void *buf, size_t size, __out size_t& read_size);

bool SetProxyForHttpHandle(void *http_handle);

#endif