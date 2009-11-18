#ifndef _MISC_H_
#define _MISC_H_

#include "common/types.h"

bool HttpGetFileSize(const std::string& url, __out unsigned long long& file_size);

bool GetDiskFileSize(const StlString& fname, __out unsigned long long& file_size);

bool HttpReadFileToBuffer(const std::string& url, void *buf, size_t size, __out size_t& read_size);

bool HttpReadFileDynamic(const std::string& url, std::vector<BYTE>& buf);

bool SetProxyForHttpHandle(void *http_handle);

HANDLE OpenOrCreate(const StlString& fname, DWORD access);

#endif