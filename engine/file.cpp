#include <windows.h>
#include <tchar.h>
#include <wininet.h>
#include <assert.h>
#include <process.h>
#include <string>
#include <list>
#include <vector>
#include <algorithm>
#include <cctype>
using namespace std;

#include "engine/file.h"
#include "engine/filesegment.h"
#include "common/consts.h"

// File part size (100 MB)
#define PART_SIZE (100 * 1024 * 1024)

File::File(const StlString &url, const StlString& fname, 
		   HANDLE pause_event, HANDLE continue_event, HANDLE stop_event)
{
	InitLock(&lock_);
	url_ = url;
	fname_ = fname;
	downloaded_size_ = 0;
	thread_count_ = 1;
	part_file_handle_ = INVALID_HANDLE_VALUE;

	pause_event_ = pause_event;
	continue_event_ = continue_event;
	stop_event_ = stop_event;
	SetStatus(DOWNLOAD_NOT_STARTED);
}

File::~File()
{
	CloseLock(&lock_);
}

bool File::Start()
{
	downloaded_size_ = 0;
	bool updated;

	if (!GetDownloadParameters(updated))
		return false;

	assert(!updated);

	unsigned thread_id;
	thread_handle_ = (HANDLE)_beginthreadex(NULL, 0, FileThread, this, 0, &thread_id);
	
	return thread_handle_ != NULL;
}

bool File::IsFinished()
{
	return WAIT_OBJECT_0 == WaitForSingleObject(thread_handle_, 0);
}

static bool ParseParameters(std::vector <BYTE> buf, __out unsigned int& thread_count, 
							  __out std::list<string>& md5_list)
{
	string str(buf.begin(), buf.end());
	bool thread_count_read = false;
	md5_list.clear();
	const char newline[] = "\n";
	for (size_t pos = 0; pos < str.size(); )
	{
		size_t new_pos = str.find(newline, pos);
		if (-1 == new_pos)
			new_pos = str.size();
		if (!thread_count_read)
		{
			thread_count = atoi((str.substr(pos, new_pos - pos)).c_str());
			thread_count_read = true;
		}
		else
		{
			string md5_str = str.substr(pos, new_pos - pos);
			std::transform(md5_str.begin(), md5_str.end(), md5_str.begin(), std::toupper);
			md5_list.push_back(md5_str);
		}
		if (new_pos == str.size())
			break;
		pos = new_pos + sizeof(newline) - 1;
	}
	
	return thread_count_read && md5_list.size() > 0;
}

static bool GetInternetFileSize(const StlString &url, __out size_t& file_size)
{
	const StlString header = _T("Accept: */*");
	HINTERNET inet_handle = 
		InternetOpen(_T("Mozilla/5.0"),
		INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
	if (!inet_handle)
		return false;

	bool ret_val = false;

	HINTERNET url_handle = InternetOpenUrl(inet_handle, url.c_str(), 
			NULL, 0, 0, 0);
	if (url_handle)
	{
		TCHAR buf[100];
		DWORD sz = sizeof(buf);
		if (HttpQueryInfo(url_handle, HTTP_QUERY_CONTENT_LENGTH,
			buf, &sz, NULL))
		{
			file_size = _ttoi(buf);
			ret_val = true;
		}
		InternetCloseHandle(url_handle);
	}

	InternetCloseHandle(inet_handle);

	return ret_val;
}

static bool GetParameters(const StlString &url, __out unsigned int& thread_count, 
						  __out std::list<string>& md5_list, 
						  __out size_t& file_size)
{
	StlString param_url = url + _T(".md5");
	const StlString header = _T("Accept: */*");
	HINTERNET inet_handle = 
		InternetOpen(_T("Mozilla/5.0"),
		PRE_CONFIG_INTERNET_ACCESS, NULL, NULL, 0);
	if (!inet_handle)
		return false;

	size_t size;
	if (!GetInternetFileSize(param_url, size))
		return false;

	bool ret_val = false;

	HINTERNET url_handle = InternetOpenUrl(inet_handle, param_url.c_str(), header.c_str(), (DWORD)header.size(), 0, 0);
	if (url_handle)
	{
		vector <BYTE> buf;
		buf.resize(size);
		DWORD read_size;

		if (InternetReadFile(url_handle, &buf[0], (DWORD)size, &read_size))
			ret_val = ParseParameters(buf, thread_count, md5_list);

		InternetCloseHandle(url_handle);
	}

	InternetCloseHandle(inet_handle);

	if (ret_val)
		ret_val = GetInternetFileSize(url, file_size);

	return ret_val;
}

/**
 *	Connect to the server and obtain download parameters for this file: 
 *	- MD5 hashes for entire file 
 *	- MD5 hashes for each file part
 *	- thread count
 */
bool File::GetDownloadParameters(__out bool& updated)
{
	unsigned int thread_count;
	list<string> md5_list;
	size_t file_size;
	bool get_param_result = GetParameters(url_, thread_count, md5_list, file_size);
	if (!get_param_result)
		return false;

	updated = false;

	if (md5_list_.size() != 0)
	{
		// If parameters already exist: check new parameters against existing ones
		updated = (file_size != file_size_ 
			|| thread_count != thread_count_ || md5_list.size() != md5_list_.size());
		if (!updated)
		{
			list<string>::iterator i1, i2;
			for (i1 = md5_list.begin(), i2 = md5_list_.begin(); 
				i1 != md5_list.end(); i1++, i2++) 
			{
				if (*i1 != *i2)
				{
					updated = true;
					break;
				}
			}
		}
	}

	// Update parameters 
	md5_list_.assign(md5_list.begin(), md5_list.end());
	thread_count_ = thread_count;
	file_size_ = file_size;

	return true;
}


void File::NotifyDownloadProgress(FileSegment *sender, size_t offset, void *data, size_t size)
{
	Lock(&lock_);
	// Update total progress counter
	downloaded_size_ += size;
	// Write data to file
	SetFilePointer(part_file_handle_, sender->GetOffset() + offset, NULL, FILE_BEGIN);
	DWORD nr_written;
	WriteFile(part_file_handle_, data, size, &nr_written, NULL);
	assert((size_t)nr_written == size);
	Unlock(&lock_);

	//FIXME: resuming support is postponed
}

void File::NotifyDownloadStatus(FileSegment *sender, 
								unsigned int status)
{
	switch (status)
	{
	case DOWNLOAD_FAILURE:
		// Try to restart this segment
#		define MAX_ATTEMPT_COUNT 10
		if (sender->GetAttemptCount() < MAX_ATTEMPT_COUNT)
			sender->Start();
		else
		{
			AbortDownload();
			SetStatus(status);
		}
		break;
	}
}

unsigned __stdcall File::FileThread(void *arg)
{
	File *file = (File*)arg;

	// Every file is split into parts

	size_t part_count = file->file_size_ / PART_SIZE;
	size_t offset = 0;

	for (size_t part_num = 0; part_num < part_count; part_num++, offset += PART_SIZE) 
		file->DownloadPart(part_num, offset, PART_SIZE);

	// Now merge all parts
	if (!file->MergeParts())
		file->SetStatus(DOWNLOAD_MERGE_FAILURE);

	_endthreadex(0);
	return 0;
}

static HANDLE OpenOrCreate(const StlString& fname, DWORD access)
{
	return CreateFile(fname.c_str(), access, 
		FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
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

			if (part_num != part_count - 1 && part_size != PART_SIZE)
			{
				ret_val = FALSE;
				break;
			}

			ret_val = ret_val && ReadFile(part_file_handle, &buf[0], part_size, (LPDWORD)&part_size, NULL);
			if (ret_val)
				ret_val = ret_val && 
					WriteFile(file_handle, &buf[0], part_size, (LPDWORD)&part_size, NULL);
			CloseHandle(part_file_handle_);
			if (!ret_val)
				break;
		}
		DeleteFile(part_fname.c_str());
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
		FileSegment *seg = new FileSegment(this, url_, 
			offset + seg_offset, seg_size, pause_event_, continue_event_, stop_event_);
		seg->Start();
		segments_[i] = seg;
	}

	SetStatus(DOWNLOAD_STARTED);

	vector<HANDLE> thread_handles;
	thread_handles.resize(thread_count_);
	for (unsigned i = 0; i < thread_count_; i++) 
		thread_handles[i] = segments_[i]->GetThreadHandle();

#if 1
	WaitForMultipleObjects(thread_count_, &thread_handles[0], TRUE, INFINITE);
#else
	for (;;) 
	{
		bool finished = true;
		//FIXME: заменить на WaitForMultipleObjects
		for (unsigned i = 0; i < thread_count_; i++)
		{
			FileSegment *seg = segments_[i];
			if (!seg->IsFinished())
			{
				finished = false;
				break;
			}
		}
		if (finished)
			break;
		Sleep(500);
	}
#endif

	for (unsigned i = 0; i < thread_count_; i++) 
	{
		FileSegment *seg = segments_[i];
		delete seg;
	}

	CloseHandle(part_file_handle_);

	return true;
}

void File::GetDownloadStatus(__out unsigned int& status, __out size_t& downloaded_size)
{
	status = download_status_;
	Lock(&lock_);
	downloaded_size = downloaded_size_;
	Unlock(&lock_);
}

void File::AbortDownload()
{
	// Set stop event to finish all running FileSegment-s
	PulseEvent(stop_event_);
}

void File::SetStatus(unsigned int status)
{
	InterlockedExchange((volatile LONG*)&download_status_, status);
}
