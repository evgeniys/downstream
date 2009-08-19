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
#include "file.h"
#include "filesegment.h"

// File part size (100 MB)
#define PART_SIZE (100 * 1024 * 1024)

File::File(const StlString &url, const StlString& fname)
{
	InitLock(&lock_);
	InitLock(&gc_lock_);
	url_ = url;
	fname_ = fname;
	downloaded_size_ = 0;
	thread_count_ = 1;

	pause_event_ = CreateEvent(NULL, TRUE, FALSE, NULL);
	continue_event_ = CreateEvent(NULL, TRUE, FALSE, NULL);
	stop_event_ = CreateEvent(NULL, TRUE, FALSE, NULL);
}

File::~File()
{
	if (pause_event_)
		CloseHandle(pause_event_);
	if (continue_event_)
		CloseHandle(continue_event_);
	if (stop_event_)
		CloseHandle(stop_event_);

	CloseLock(&lock_);
	CloseLock(&gc_lock_);
}

bool File::Start()
{
	downloaded_size_ = 0;
	bool updated;

	if (!GetDownloadParameters(updated))
		return false;

	assert(!updated);

	unsigned thread_id;
	thread_ = (HANDLE)_beginthreadex(NULL, 0, FileThread, this, 0, &thread_id);
	
	return thread_ != NULL;
}

bool File::IsFinished()
{
	return WAIT_OBJECT_0 == WaitForSingleObject(thread_, 0);
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
	// Update total progress counter
	Lock(&lock_);
	downloaded_size_ += size;
	Unlock(&lock_);

	//FIXME: bitmap and resuming support is postponed
}

void File::NotifyDownloadStatus(FileSegment *sender, 
								unsigned int status, 
								uintptr_t arg1, 
								uintptr_t arg2)
{
	switch (status)
	{
	case DOWNLOAD_FINISHED:
		Lock(&gc_lock_);
		gc_list_.push_back(sender);
		Unlock(&gc_lock_);
		break;

	}
}

void File::GarbageCollect()
{
	Lock(&gc_lock_);
	while (!gc_list_.empty())
	{
		FileSegment	*seg = *(gc_list_.begin());
		gc_list_.pop_front();
		if (seg->IsFinished())
			delete seg;
	}
	Unlock(&gc_lock_);
}

unsigned __stdcall File::FileThread(void *arg)
{
	File *file = (File*)arg;

	// Every file is split into parts

	size_t part_count = file->file_size_ / PART_SIZE;

	for (size_t part_num = 0; part_count; part_num++) 
	{
		//FIXME
	}

	_endthreadex(0);
	return 0;
}