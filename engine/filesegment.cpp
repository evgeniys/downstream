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
	file_->NotifyDownloadStatus(this, status);
}

size_t FileSegment::DownloadWriteDataCallback(void *buffer, size_t size, size_t nmemb, void *userp)
{
	FileSegment *seg = (FileSegment*)userp;
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
	LOG(("[ProgressCallback] tid=0x%x, dltotal=%lf, dlnow=%lf, ultotal=%lf, ulnow=%lf\n", 
		GetCurrentThreadId(), dltotal, dlnow, ultotal, ulnow));
	return 0;
}

unsigned __stdcall  FileSegment::FileSegmentThread(void *arg)
{
	FileSegment *seg = (FileSegment*)arg;

	CURL *http_handle = curl_easy_init();
	if (!http_handle)
	{
		seg->SetStatus(STATUS_INIT_FAILED);
		goto __end;
	}

	// Set HTTP options
	curl_easy_setopt(http_handle, CURLOPT_URL, seg->url_.c_str());
	curl_easy_setopt(http_handle, CURLOPT_WRITEFUNCTION, DownloadWriteDataCallback);
	curl_easy_setopt(http_handle, CURLOPT_WRITEDATA, seg);
	curl_easy_setopt(http_handle, CURLOPT_VERBOSE, 1);
	curl_easy_setopt(http_handle, CURLOPT_DEBUGFUNCTION, DebugCallback);
	curl_easy_setopt(http_handle, CURLOPT_DEBUGDATA, seg);
	curl_easy_setopt(http_handle, CURLOPT_NOPROGRESS, 0);
	curl_easy_setopt(http_handle, CURLOPT_PROGRESSFUNCTION, ProgressCallback);
	curl_easy_setopt(http_handle, CURLOPT_PROGRESSDATA, seg);
	SetProxyForHttpHandle(http_handle);

	char *err_buffer = (char*)malloc(CURL_ERROR_SIZE);
	if (err_buffer)
		curl_easy_setopt(http_handle, CURLOPT_ERRORBUFFER, err_buffer);

	// Set file position
	CHAR range_header[1024];
	_snprintf(range_header, _countof(range_header), "Range: bytes=%d-%d", 
		seg->seg_offset_, seg->seg_offset_ + seg->size_ - 1);
	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, range_header);
	if (!headers)
	{
		curl_easy_cleanup(http_handle);
		seg->SetStatus(STATUS_INIT_FAILED);
		goto __end;
	}
	curl_easy_setopt(http_handle, CURLOPT_HTTPHEADER, headers);

	seg->position_ = seg->seg_offset_;
	CURLcode download_result = curl_easy_perform(http_handle);
	if (0 == download_result)
		seg->SetStatus(STATUS_DOWNLOAD_FINISHED);
	else
	{
		LOG(("Error: %s\n", err_buffer));
		seg->SetStatus(STATUS_DOWNLOAD_FAILURE);
	}

	curl_slist_free_all(headers);
	curl_easy_cleanup(http_handle);
	if (err_buffer)
		free(err_buffer);

__end:
	_endthreadex(0);
	return 0;
}

