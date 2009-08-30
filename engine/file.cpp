#include <windows.h>
#include <tchar.h>
#include <assert.h>
#include <process.h>
#include <string>
#include <vector>
using namespace std;

#include "engine/file.h"
#include "engine/filesegment.h"
#include "common/consts.h"
#include "common/misc.h"

File::File(const std::string& url, const StlString& fname, 
		   unsigned int thread_count,
		   HANDLE pause_event, HANDLE continue_event, HANDLE stop_event)
{
	InitLock(&lock_);
	url_ = url;
	fname_ = fname;
	downloaded_size_ = 0;
	increment_ = 0;
	thread_count_ = thread_count;
	part_file_handle_ = INVALID_HANDLE_VALUE;

	pause_event_ = pause_event;
	continue_event_ = continue_event;
	stop_event_ = stop_event;
	SetStatus(STATUS_DOWNLOAD_NOT_STARTED);
}

File::~File()
{
	CloseLock(&lock_);
}

bool File::Start()
{
	if (!HttpGetFileSize(url_, file_size_))
		return false;

	unsigned thread_id;
	thread_handle_ = (HANDLE)_beginthreadex(NULL, 0, FileThread, this, 0, &thread_id);
	
	return thread_handle_ != NULL;
}

void File::Stop()
{
	SetEvent(this->stop_event_);
}

bool File::Terminate()
{
	return 0 != TerminateThread(thread_handle_, 0);
}

bool File::WaitForFinish(DWORD timeout)
{
	return WAIT_OBJECT_0 == WaitForSingleObject(thread_handle_, timeout);
}

void File::NotifyDownloadProgress(FileSegment *sender, size_t offset, void *data, size_t size)
{
	Lock(&lock_);
	// Update total progress counter
	downloaded_size_ += size;
	increment_ += size;
	// Write data to file
	SetFilePointer(part_file_handle_, offset - sender->GetPartOffset(), NULL, FILE_BEGIN);
	DWORD nr_written;
	WriteFile(part_file_handle_, data, size, &nr_written, NULL);
	assert((size_t)nr_written == size);
	Unlock(&lock_);

	//FIXME: resuming support is postponed
}

unsigned __stdcall File::FileThread(void *arg)
{
	File *file = (File*)arg;

	// Every file is split into parts

	size_t part_count = file->file_size_ / PART_SIZE;
	if (file->file_size_ % PART_SIZE)
		part_count++;
	size_t offset = 0;

	file->SetStatus(STATUS_DOWNLOAD_STARTED);

	for (size_t part_num = 0; offset < file->file_size_; part_num++, offset += PART_SIZE) 
	{
		size_t part_size = PART_SIZE;
		if (offset + PART_SIZE >= file->file_size_)
			part_size = file->file_size_ - offset;
		file->DownloadPart(part_num, offset, part_size);
	}

	unsigned int status;
	size_t size, increment;
	file->GetDownloadStatus(status, size, increment);
	if (STATUS_DOWNLOAD_FAILURE != status)
	{
		// Now merge all parts
		if (!file->MergeParts())
			file->SetStatus(STATUS_DOWNLOAD_MERGE_FAILURE);
		else
			file->SetStatus(STATUS_DOWNLOAD_FINISHED);
	}

	_endthreadex(0);
	return 0;
}

/**
 *	Merge parts of downloaded file and delete them
 */
bool File::MergeParts()
{
	HANDLE file_handle = OpenOrCreate(fname_, GENERIC_WRITE);
	if (INVALID_HANDLE_VALUE == file_handle)
		return false;

	size_t part_count = file_size_ / PART_SIZE;
	if (file_size_ % PART_SIZE)
		part_count++;
	size_t offset = 0;
	size_t part_size = PART_SIZE;
	vector <BYTE> buf;
	buf.resize(part_size);
	BOOL ret_val = TRUE;
	for (size_t part_num = 0; part_num < part_count; part_num++, offset += PART_SIZE) 
	{
		TCHAR part_num_str[10];
		_itot(part_num + 1, part_num_str, 10);
		StlString part_fname = fname_ + _T(".part") + part_num_str;
		HANDLE part_file_handle = OpenOrCreate(part_fname, GENERIC_READ);
		if (INVALID_HANDLE_VALUE != part_file_handle)
		{
			part_size = GetFileSize(part_file_handle, NULL);

			if (part_num != part_count - 1 && part_size < PART_SIZE)
			{
				ret_val = FALSE;
				break;
			}

			if (part_num != part_count - 1 && part_size > PART_SIZE)
				part_size = PART_SIZE;

			ret_val = ret_val && ReadFile(part_file_handle, &buf[0], part_size, (LPDWORD)&part_size, NULL);
			if (ret_val)
				ret_val = ret_val && 
					WriteFile(file_handle, &buf[0], part_size, (LPDWORD)&part_size, NULL);
			CloseHandle(part_file_handle);
		}

		DeleteFile(part_fname.c_str());

		if (!ret_val)
			break;
	}

	CloseHandle(file_handle);

	return (FALSE != ret_val);
}

bool File::DownloadPart(size_t part_num, size_t offset, size_t size)
{
	TCHAR part_num_str[10];
	_itot(part_num + 1, part_num_str, 10);
	StlString part_fname = fname_ + _T(".part") + part_num_str;

	part_file_handle_ = OpenOrCreate(part_fname, GENERIC_WRITE);
	if (INVALID_HANDLE_VALUE == part_file_handle_)
		return false;

	// Divide part into segments (1 segment per thread)
	size_t seg_size = size / thread_count_;
	segments_.resize(thread_count_);
	size_t seg_offset = 0;
	for (unsigned i = 0; i < thread_count_; i++, seg_offset += seg_size) 
	{
		FileSegment *seg = new FileSegment(this, url_, offset,
			offset + seg_offset, i == thread_count_ - 1 ? size - seg_offset : seg_size, 
			pause_event_, continue_event_, stop_event_);
		seg->Start();
		segments_[i] = seg;
	}

	vector<HANDLE> thread_handles;
	thread_handles.resize(thread_count_);
	for (unsigned i = 0; i < thread_count_; i++) 
		thread_handles[i] = segments_[i]->GetThreadHandle();

	WaitForMultipleObjects(thread_count_, &thread_handles[0], TRUE, INFINITE);

	for (unsigned i = 0; i < thread_count_; i++) 
	{
		FileSegment *seg = segments_[i];
		if (seg->GetStatus() == STATUS_DOWNLOAD_FAILURE)
			SetStatus(STATUS_DOWNLOAD_FAILURE);
		delete seg;
	}

	CloseHandle(part_file_handle_);

	return true;
}

void File::GetDownloadStatus(__out unsigned int& status, __out size_t& downloaded_size, __out size_t& increment)
{
	status = download_status_;
	Lock(&lock_);
	downloaded_size = downloaded_size_;
	increment = increment_;
	increment_ = 0;
	Unlock(&lock_);
}

void File::AbortDownload()
{
	// Set stop event to finish all running FileSegment-s
	SetEvent(stop_event_);
}

void File::SetStatus(unsigned int status)
{
	InterlockedExchange((volatile LONG*)&download_status_, status);
}
