#include <windows.h>
#include <tchar.h>
#include <assert.h>
#include <process.h>
#include <string>
#include <vector>
using namespace std;

#include "engine/webfile.h"
#include "engine/webfilesegment.h"
#include "common/consts.h"
#include "common/misc.h"

WebFile::WebFile(const std::string& url, const StlString& fname, 
				 unsigned int thread_count,
				 HANDLE pause_event, HANDLE continue_event, HANDLE stop_event)
{
	InitLock(&lock_);
	url_ = url;
	fname_ = fname;
	downloaded_size_ = 0;
	increment_ = 0;
	thread_count_ = thread_count;
	file_handle_ = INVALID_HANDLE_VALUE;

	pause_event_ = pause_event;
	continue_event_ = continue_event;
	stop_event_ = stop_event;
	flags_ = 0;
	part_num_ = 0;
	SetStatus(STATUS_DOWNLOAD_NOT_STARTED);
}

WebFile::~WebFile()
{
	CloseLock(&lock_);
}

bool WebFile::Start()
{
	if (!HttpGetFileSize(url_, file_size_))
		return false;

	unsigned thread_id;
	thread_handle_ = (HANDLE)_beginthreadex(NULL, 0, FileThread, this, 0, &thread_id);
	
	return thread_handle_ != NULL;
}

void WebFile::Stop()
{
	SetEvent(this->stop_event_);
}

bool WebFile::Terminate()
{
	Lock(&lock_);
	for (size_t i = 0; i < segments_.size(); i++)
		(segments_[i])->Terminate();
	Unlock(&lock_);
	return 0 != TerminateThread(thread_handle_, 0);
}

bool WebFile::WaitForFinish(DWORD timeout)
{
	return WAIT_OBJECT_0 == WaitForSingleObject(thread_handle_, timeout);
}

void WebFile::NotifyDownloadProgress(WebFileSegment *sender, unsigned long long offset, void *data, size_t size)
{
	Lock(&lock_);
	// Update total progress counter
	downloaded_size_ += size;
	increment_ += size;
	// Write data to file
	LARGE_INTEGER tmp;
	tmp.QuadPart = offset;
	SetFilePointer(file_handle_, tmp.LowPart, &tmp.HighPart, FILE_BEGIN);
	DWORD nr_written;
	WriteFile(file_handle_, data, (DWORD)size, &nr_written, NULL);
	assert((size_t)nr_written == size);
	Unlock(&lock_);
}

void WebFile::UpdateThreadCount(unsigned int thread_count)
{
	flags_ |= FILE_THREAD_COUNT_CHANGED;
	thread_count_ = thread_count;
	SetEvent(stop_event_);
}

unsigned __stdcall WebFile::FileThread(void *arg)
{
	WebFile *file = (WebFile*)arg;

	// Every file is split into parts

//	size_t part_count = (size_t)(file->file_size_ / PART_SIZE);
//	if (file->file_size_ % PART_SIZE)
//		part_count++;
	unsigned long long offset = 0;

	file->file_handle_ = OpenOrCreate(file->fname_, GENERIC_WRITE);
	if (INVALID_HANDLE_VALUE == file->file_handle_)
	{
		file->SetStatus(STATUS_FILE_CREATE_FAILURE);
		goto __end;
	}

	file->SetStatus(STATUS_DOWNLOAD_STARTED);

	for (size_t part_num = file->part_num_; offset < file->file_size_; ) 
	{
		unsigned long long part_size = PART_SIZE;
		if (offset + PART_SIZE >= file->file_size_)
			part_size = file->file_size_ - offset;
		file->DownloadPart(part_num, offset, part_size, file->thread_count_);
		if (file->flags_ & FILE_THREAD_COUNT_CHANGED)
		{
			// Thread count has been changed. Do not restart whole file, restart
			// current part only.
			file->flags_ &= ~FILE_THREAD_COUNT_CHANGED;
			ResetEvent(file->stop_event_);
			continue;
		}
		part_num++;
		offset += PART_SIZE;
	}

	unsigned int status;
	size_t size, increment;
	file->GetDownloadStatus(status, size, increment);
	if (STATUS_DOWNLOAD_FAILURE != status)
		file->SetStatus(STATUS_DOWNLOAD_FINISHED);

	CloseHandle(file->file_handle_);

__end:
	_endthreadex(0);
	return 0;
}

bool WebFile::DownloadPart(size_t part_num, unsigned long long offset, 
						   unsigned long long size, unsigned int thread_count)
{
	TCHAR part_num_str[10];
	_itot((int)part_num + 1, part_num_str, 10);
	StlString part_fname = fname_ + _T(".part") + part_num_str;

	// Divide part into segments (1 segment per thread)
	unsigned long long seg_size = size / thread_count;

	Lock(&lock_);
	segments_.resize(thread_count);
	unsigned long long seg_offset = 0;
	for (unsigned i = 0; i < thread_count; i++, seg_offset += seg_size) 
	{
		WebFileSegment *seg = new WebFileSegment(this, url_, 
			offset + seg_offset, i == thread_count - 1 ? size - seg_offset : seg_size, 
			pause_event_, continue_event_, stop_event_);
		seg->Start();
		segments_[i] = seg;
	}

	thread_handles_.resize(thread_count);
	for (unsigned i = 0; i < thread_count; i++) 
		thread_handles_[i] = segments_[i]->GetThreadHandle();

	Unlock(&lock_);

	WaitForMultipleObjects(thread_count, &thread_handles_[0], TRUE, INFINITE);

	for (unsigned i = 0; i < thread_count; i++) 
	{
		WebFileSegment *seg = segments_[i];
		if (seg->GetStatus() == STATUS_DOWNLOAD_FAILURE)
			SetStatus(STATUS_DOWNLOAD_FAILURE);
		delete seg;
	}

	segments_.resize(0);
	thread_handles_.resize(0);

	return true;
}

void WebFile::GetDownloadStatus(__out unsigned int& status, __out size_t& downloaded_size, __out size_t& increment)
{
	status = download_status_;
	Lock(&lock_);
	downloaded_size = downloaded_size_;
	increment = increment_;
	increment_ = 0;
	Unlock(&lock_);
}

void WebFile::SetStatus(unsigned int status)
{
	InterlockedExchange((volatile LONG*)&download_status_, status);
}
