#include <windows.h>
#include <tchar.h>
#include <process.h>
#include <assert.h>
#include <string>
#include <vector>
#include <list>
#include <algorithm>
#include <locale>
using namespace std;

#include "engine/webfilesegment.h"
#include "engine/webfile.h"
#include "common/consts.h"
#include "common/logging.h"
#include "common/misc.h"

WebFileSegment::WebFileSegment(WebFile *file, 
							   const std::string& url, 
							   unsigned long long seg_offset, 
							   unsigned long long size, 
							   HANDLE pause_event, 
							   HANDLE continue_event,
							   HANDLE stop_event)
: file_(file), url_(url), seg_offset_(seg_offset), 
 size_(size), pause_event_(pause_event), 
 continue_event_(continue_event), stop_event_(stop_event)
{
	thread_ = NULL;
	downloaded_size_ = 0;
	cached_downloaded_size_ = 0;
	SetStatus(STATUS_DOWNLOAD_NOT_STARTED);
}

WebFileSegment::WebFileSegment(class WebFile *file, 
							   const std::string& url, 
							   HANDLE pause_event, 
							   HANDLE continue_event, 
							   HANDLE stop_event)
 : file_(file), url_(url), seg_offset_(0), 
 size_(0), pause_event_(pause_event), 
 continue_event_(continue_event), stop_event_(stop_event)
{
	thread_ = NULL;
	downloaded_size_ = 0;
	cached_downloaded_size_ = 0;
	SetStatus(STATUS_DOWNLOAD_NOT_STARTED);
}

WebFileSegment::~WebFileSegment()
{
	if (thread_)
		CloseHandle(thread_);
}

bool WebFileSegment::Start()
{
	unsigned thread_id;
	thread_ = (HANDLE)_beginthreadex(NULL, 0, FileSegmentThread, this, 0, &thread_id);
	return thread_ != NULL;
}

void WebFileSegment::Restart()
{
	WaitForSingleObject(thread_, INFINITE);
	Start();
}

bool WebFileSegment::IsFinished()
{
	return WAIT_OBJECT_0 == WaitForSingleObject(thread_, 0);
}

bool WebFileSegment::Terminate()
{
	return 0 != TerminateThread(thread_, 0);
}

void WebFileSegment::SetStatus(unsigned int status)
{
	InterlockedExchange((volatile LONG*)&download_status_, status);
}

size_t WebFileSegment::DownloadWriteDataCallback(void *buffer, size_t size, size_t nmemb, void *userp)
{
	WebFileSegment *seg = (WebFileSegment*)userp;
	if (WAIT_OBJECT_0 == WaitForSingleObject(seg->stop_event_, 0))
	{
		seg->file_->SetStatus(STATUS_DOWNLOAD_STOPPED);
		return 0;
	}
	if (WAIT_OBJECT_0 == WaitForSingleObject(seg->pause_event_, 0))
		return CURL_WRITEFUNC_PAUSE;

	InterlockedExchange(
		(volatile LONG*)&seg->cached_downloaded_size_, seg->downloaded_size_);

	size_t nr_write = nmemb * size;
	ULONG64 position = seg->seg_offset_ + seg->downloaded_size_;
	LOG(("[DownloadWriteDataCallback] tid=0x%x, position=0x%x, size=0x%x\n", 
		GetCurrentThreadId(), position, nr_write));
	seg->file_->NotifyDownloadProgress(seg, position, buffer, nr_write);
	seg->downloaded_size_ += nr_write;
	return nmemb;		 
}

int WebFileSegment::DebugCallback(CURL *handle, curl_infotype type, char *data, size_t size, void *userptr)
{
	if (type != CURLINFO_DATA_IN && type != CURLINFO_DATA_OUT)
		LOG(("[DebugCallback] tid=0x%x, type=%u: %.*s\n", GetCurrentThreadId(), type, size, data));
	else
		LOG(("[DebugCallback] tid=0x%x, type=%u, size=0x%x\n", GetCurrentThreadId(), type, size));
	return 0;
}

int WebFileSegment::ProgressCallback(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow)
{
	WebFileSegment *seg = (WebFileSegment*)clientp;
	HANDLE event_handles[2];
	event_handles[0] = seg->continue_event_;
	event_handles[1] = seg->stop_event_;
	DWORD wait_result = WaitForMultipleObjects(_countof(event_handles), event_handles, FALSE, 0);
	if (WAIT_OBJECT_0 == wait_result || WAIT_OBJECT_0 + 1 == wait_result)
		curl_easy_pause(seg->http_handle_, CURLPAUSE_CONT);
	LOG(("[ProgressCallback] tid=0x%x, dltotal=%lf, dlnow=%lf, ultotal=%lf, ulnow=%lf\n", 
		GetCurrentThreadId(), dltotal, dlnow, ultotal, ulnow));
	return 0;
}

unsigned __stdcall  WebFileSegment::FileSegmentThread(void *arg)
{
	WebFileSegment *seg = (WebFileSegment*)arg;

	if (WAIT_OBJECT_0 == WaitForSingleObject(seg->pause_event_, 0))
	{
		HANDLE event_handles[2];
		event_handles[0] = seg->continue_event_;
		event_handles[1] = seg->stop_event_;
		DWORD wait_result = WaitForMultipleObjects(
			_countof(event_handles), event_handles, FALSE, INFINITE);

		switch (wait_result)
		{
		case WAIT_OBJECT_0: // continue_event_
			goto __download;
		case WAIT_OBJECT_0 + 1: // stop_event_
		default:
			goto __end;
		}
	}

__download:
	seg->http_handle_ = curl_easy_init();
	if (!seg->http_handle_)
	{
		seg->SetStatus(STATUS_INIT_FAILED);
		goto __end;
	}

	// Set HTTP options
	curl_easy_setopt(seg->http_handle_, CURLOPT_URL, seg->url_.c_str());
	curl_easy_setopt(seg->http_handle_, CURLOPT_WRITEFUNCTION, DownloadWriteDataCallback);
	curl_easy_setopt(seg->http_handle_, CURLOPT_WRITEDATA, seg);
	curl_easy_setopt(seg->http_handle_, CURLOPT_VERBOSE, 1);
	curl_easy_setopt(seg->http_handle_, CURLOPT_DEBUGFUNCTION, DebugCallback);
	curl_easy_setopt(seg->http_handle_, CURLOPT_DEBUGDATA, seg);
	curl_easy_setopt(seg->http_handle_, CURLOPT_NOPROGRESS, 0);
	curl_easy_setopt(seg->http_handle_, CURLOPT_PROGRESSFUNCTION, ProgressCallback);
	curl_easy_setopt(seg->http_handle_, CURLOPT_PROGRESSDATA, seg);
	SetProxyForHttpHandle(seg->http_handle_);

	char *err_buffer = (char*)malloc(CURL_ERROR_SIZE);
	if (err_buffer)
		curl_easy_setopt(seg->http_handle_, CURLOPT_ERRORBUFFER, err_buffer);

	// Set file position
	CHAR range_header[1024];
	ULONG64 range_start = seg->seg_offset_ + seg->downloaded_size_;
	_snprintf(range_header, _countof(range_header), "Range: bytes=%lld-%lld", 
		range_start, seg->seg_offset_ + seg->size_ - 1);
	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, range_header);
	if (!headers)
	{
		curl_easy_cleanup(seg->http_handle_);
		seg->SetStatus(STATUS_INIT_FAILED);
		goto __end;
	}
	curl_easy_setopt(seg->http_handle_, CURLOPT_HTTPHEADER, headers);

	CURLcode download_result = curl_easy_perform(seg->http_handle_);
	if (0 == download_result)
		seg->SetStatus(STATUS_DOWNLOAD_FINISHED);
	else
	{
		LOG(("Error: %s\n", err_buffer));
		seg->SetStatus(STATUS_DOWNLOAD_FAILURE);
	}

	curl_slist_free_all(headers);
	curl_easy_cleanup(seg->http_handle_);
	if (err_buffer)
		free(err_buffer);

__end:
	_endthreadex(0);
	return 0;
}

