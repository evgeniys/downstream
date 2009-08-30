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

#include "engine/filesegment.h"
#include "engine/file.h"
#include "common/consts.h"
#include "common/logging.h"
#include "common/misc.h"

#define CHUNK_SIZE (16 * 1024)

FileSegment::FileSegment(File *file, 
						 const std::string& url, 
						 size_t part_offset,
						 size_t seg_offset, size_t size, 
						 HANDLE pause_event, 
						 HANDLE continue_event,
						 HANDLE stop_event)
: file_(file), url_(url), part_offset_(part_offset), seg_offset_(seg_offset), 
 size_(size), pause_event_(pause_event), 
 continue_event_(continue_event), stop_event_(stop_event)
{
	thread_ = NULL;
	attempt_count_ = 0;
	downloaded_size_ = 0;
	SetStatus(STATUS_DOWNLOAD_NOT_STARTED);
}

FileSegment::~FileSegment()
{
	if (thread_)
		CloseHandle(thread_);
}

bool FileSegment::Start()
{
	attempt_count_++;
	unsigned thread_id;
	thread_ = (HANDLE)_beginthreadex(NULL, 0, FileSegmentThread, this, 0, &thread_id);
	return thread_ != NULL;
}

void FileSegment::Restart()
{
	WaitForSingleObject(thread_, INFINITE);
	Start();
}

bool FileSegment::IsFinished()
{
	return WAIT_OBJECT_0 == WaitForSingleObject(thread_, 0);
}

void FileSegment::SetStatus(unsigned int status)
{
	InterlockedExchange((volatile LONG*)&download_status_, status);
}

size_t FileSegment::DownloadWriteDataCallback(void *buffer, size_t size, size_t nmemb, void *userp)
{
	FileSegment *seg = (FileSegment*)userp;
	if (WAIT_OBJECT_0 == WaitForSingleObject(seg->stop_event_, 0))
	{
		seg->file_->SetStatus(STATUS_DOWNLOAD_STOPPED);
		return 0;
	}
	if (WAIT_OBJECT_0 == WaitForSingleObject(seg->pause_event_, 0))
		return CURL_WRITEFUNC_PAUSE;
	size_t nr_write = nmemb * size;
	LOG(("[DownloadWriteDataCallback] tid=0x%x, position=0x%x, size=0x%x\n", 
		GetCurrentThreadId(), seg->position_, nr_write));
	seg->file_->NotifyDownloadProgress(seg, seg->position_, buffer, nr_write);
	seg->position_ += nr_write;
	return nmemb;		 
}

int FileSegment::DebugCallback(CURL *handle, curl_infotype type, char *data, size_t size, void *userptr)
{
	if (type != CURLINFO_DATA_IN && type != CURLINFO_DATA_OUT)
		LOG(("[DebugCallback] tid=0x%x, type=%u: %.*s\n", GetCurrentThreadId(), type, size, data));
	else
		LOG(("[DebugCallback] tid=0x%x, type=%u, size=0x%x\n", GetCurrentThreadId(), type, size));
	return 0;
}

int FileSegment::ProgressCallback(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow)
{
	FileSegment *seg = (FileSegment*)clientp;
	if (WAIT_OBJECT_0 == WaitForSingleObject(seg->continue_event_, 0))
		curl_easy_pause(seg->http_handle_, CURLPAUSE_CONT);
	LOG(("[ProgressCallback] tid=0x%x, dltotal=%lf, dlnow=%lf, ultotal=%lf, ulnow=%lf\n", 
		GetCurrentThreadId(), dltotal, dlnow, ultotal, ulnow));
	return 0;
}

unsigned __stdcall  FileSegment::FileSegmentThread(void *arg)
{
	FileSegment *seg = (FileSegment*)arg;

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
	_snprintf(range_header, _countof(range_header), "Range: bytes=%d-%d", 
		seg->seg_offset_, seg->seg_offset_ + seg->size_ - 1);
	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, range_header);
	if (!headers)
	{
		curl_easy_cleanup(seg->http_handle_);
		seg->SetStatus(STATUS_INIT_FAILED);
		goto __end;
	}
	curl_easy_setopt(seg->http_handle_, CURLOPT_HTTPHEADER, headers);

	seg->position_ = seg->seg_offset_;
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

