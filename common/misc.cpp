#include <windows.h>
#include <tchar.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <algorithm>
#include "curl/curl.h"
#include "common/types.h"
#include "common/misc.h"

using namespace std;

static char convert_to_char(wchar_t wc)
{	
	return (char)wc;
}

static size_t FileSizeHeaderCallback(void *ptr, size_t size, size_t nmemb, void *stream)
{
	string header_str((char*)ptr, size * nmemb);
	const string content_header_str = "Content-Length:";

	if (header_str.substr(0, content_header_str.size()) == content_header_str)
	{
		size_t *file_size = (size_t*)stream;
		*file_size = atoi(header_str.substr(content_header_str.size()).c_str());
	}
	return nmemb;
}

static size_t FileSizeWriteData(void *buffer, size_t size, size_t nmemb, void *userp)
{
	// Do nothing (output -> /dev/null)
	return nmemb;
}

/**
 *	Get HTTP file size using cURL. 
 *	NOTE: curl_global_init() must be issued prior calling this routine.
 */
bool HttpGetFileSize(const std::string& url, __out unsigned long long& file_size)
{
	size_t size = -1;
	bool ret_val = false;

	CURL *http_handle = curl_easy_init();

	if (!http_handle)
		return false;

	curl_easy_setopt(http_handle, CURLOPT_URL, url.c_str());
	curl_easy_setopt(http_handle, CURLOPT_HEADER, 1);
	curl_easy_setopt(http_handle, CURLOPT_NOBODY, 1);
	curl_easy_setopt(http_handle, CURLOPT_MAXREDIRS, 500);
	curl_easy_setopt(http_handle, CURLOPT_FOLLOWLOCATION, 1);

	curl_easy_setopt(http_handle, CURLOPT_WRITEFUNCTION, FileSizeWriteData); 
	curl_easy_setopt(http_handle, CURLOPT_WRITEDATA, NULL); 

	curl_easy_setopt(http_handle, CURLOPT_HEADERFUNCTION, FileSizeHeaderCallback);
	curl_easy_setopt(http_handle, CURLOPT_WRITEHEADER, &size);
	SetProxyForHttpHandle(http_handle);

	if (0 == curl_easy_perform(http_handle) && -1 != size)
	{
		file_size = size;
		ret_val = true;
	}

	curl_easy_cleanup(http_handle);

	return ret_val;
}

typedef struct _HTTP_READ_DATA {
	void *buf_;
	size_t size_;
	size_t position_;
} HTTP_READ_DATA, *PHTTP_READ_DATA;


static size_t HttpWriteData(void *buffer, size_t size, size_t nmemb, void *userp)
{
	PHTTP_READ_DATA rd = (PHTTP_READ_DATA)userp;
	size_t nr_write = nmemb * size;
	if (rd->position_ + nr_write > rd->size_)
		nr_write = rd->size_ - rd->position_;

	memcpy((char*)rd->buf_ + rd->position_, buffer, nr_write);

	rd->position_ += nr_write;

	return nmemb;
}

bool HttpReadFileToBuffer(const std::string& url, void *buf, size_t size, __out size_t& read_size)
{
	bool ret_val = false;
	HTTP_READ_DATA rd;

	rd.buf_ = buf;
	rd.size_ = size;
	rd.position_ = 0;

	CURL *http_handle = curl_easy_init();

	if (!http_handle)
		return false;

	curl_easy_setopt(http_handle, CURLOPT_URL, url.c_str());
	curl_easy_setopt(http_handle, CURLOPT_MAXREDIRS, 500);
	curl_easy_setopt(http_handle, CURLOPT_FOLLOWLOCATION, 1);

	curl_easy_setopt(http_handle, CURLOPT_WRITEFUNCTION, HttpWriteData); 
	curl_easy_setopt(http_handle, CURLOPT_WRITEDATA, &rd);
	SetProxyForHttpHandle(http_handle);

	ret_val = (0 == curl_easy_perform(http_handle));

	if (ret_val)
		read_size = rd.position_;

	curl_easy_cleanup(http_handle);

	return ret_val;
}

/**
 *	Gets HTTP proxy server from system registry.
 *	@param	proxy [out]		Proxy server in format "proxy[:port]"
 *	@return true if proxy is present and enabled, false otherwise
 */
static bool HttpGetSystemProxy(__out std::string& proxy)
{
	HKEY key_handle;
	LONG result;

	result = RegOpenKeyEx(HKEY_CURRENT_USER, 
			_T("Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings"), 
			0, KEY_READ, &key_handle);
	if (ERROR_SUCCESS != result)
		return false;

	bool ret_val = false;

	DWORD proxy_enabled, size, type;
	size = sizeof(proxy_enabled);
	type = REG_DWORD;
	result = RegQueryValueEx(key_handle, _T("ProxyEnable"), 0, &type, (LPBYTE)&proxy_enabled, &size);
	if (ERROR_SUCCESS == result)
	{
		if (proxy_enabled)
		{
			BYTE bogus_buf;
			size = sizeof(bogus_buf);
			const CHAR val_name[] = "ProxyServer";
			type = REG_SZ;
			result = RegQueryValueExA(key_handle, val_name, 0, &type, &bogus_buf, &size);
			if (type == REG_SZ && (ERROR_SUCCESS == result || ERROR_MORE_DATA ==result))
			{
				vector<BYTE> buf;
				buf.resize(size);

				result = RegQueryValueExA(key_handle, val_name, 0, &type, &buf[0], &size);
				if (REG_SZ == type && ERROR_SUCCESS == result)
				{
					proxy = std::string(buf.begin(), buf.end());
					ret_val = true;
				}
			}
		}
	}

	RegCloseKey(key_handle);

	return ret_val;
}

/**
 *	Sets proxy for cURL handle, if necessary.
 */
bool SetProxyForHttpHandle(void *http_handle)
{
	std::string proxy;
	if (!HttpGetSystemProxy(proxy))
		return false;

	curl_easy_setopt(http_handle, CURLOPT_PROXY, proxy.c_str());
	return true;
}

HANDLE OpenOrCreate(const StlString& fname, DWORD access)
{
	return CreateFile(fname.c_str(), access, 
		FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
}
