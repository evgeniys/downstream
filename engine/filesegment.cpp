#include <windows.h>
#include <tchar.h>
#include <WinInet.h>
#include <process.h>
#include <string>
#include <vector>
#include <list>
using namespace std;

#include "engine/filesegment.h"
#include "engine/file.h"
#include "common/consts.h"
#include "common/logging.h"

#define CHUNK_SIZE (16 * 1024)

FileSegment::FileSegment(File *file, 
						 const StlString& url, 
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
	SetStatus(DOWNLOAD_NOT_STARTED);
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

bool FileSegment::ReadChunk(HINTERNET url_handle, size_t position, size_t chunk_size, __out size_t& read_size)
{
	vector <BYTE> buf;
	buf.resize(CHUNK_SIZE);
	size_t avail_size;

	if (!InternetQueryDataAvailable(url_handle, (LPDWORD)&avail_size, 0, 0))
	{
		LOG(("InternetQueryDataAvailable() FAILURE, thread=0x%p, error=%d\n", GetCurrentThreadId(), GetLastError()));
		SetStatus(DOWNLOAD_FAILURE);
		return false;
	}

	if (chunk_size > avail_size)
		chunk_size = avail_size;

	if (InternetReadFile(url_handle, &buf[0], (DWORD)chunk_size, (LPDWORD)&read_size))
	{
		LOG(("[ReadChunk] OK, thread=0x%x, read_size=0x%x\n", 
			GetCurrentThreadId(), read_size));
		file_->NotifyDownloadProgress(this, position, &buf[0], read_size);
		downloaded_size_ += read_size;
		return true;
	}
	else
	{
		LOG(("InternetReadFile() FAILURE, thread=0x%p, error=%d\n", GetCurrentThreadId(), GetLastError()));
		SetStatus(DOWNLOAD_FAILURE);
		return false;
	}
}
	
unsigned __stdcall  FileSegment::FileSegmentThread(void *arg)
{
	FileSegment *seg = (FileSegment*)arg;
	const StlString header = _T("Accept: */*");

	HINTERNET inet_handle = InternetOpen(_T("Mozilla/5.0"),
		PRE_CONFIG_INTERNET_ACCESS, NULL, NULL, 0);
	unsigned long timeout = 30000;
	InternetSetOption(inet_handle, 
		INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOption(inet_handle, 
		INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOption(inet_handle, 
		INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

	HINTERNET url_handle = InternetOpenUrl(inet_handle, 
		seg->url_.c_str(), header.c_str(), (DWORD)header.size(), 
		INTERNET_FLAG_RELOAD|INTERNET_FLAG_EXISTING_CONNECT|INTERNET_FLAG_PASSIVE, 0);

	if (url_handle)
	{
		seg->SetStatus(DOWNLOAD_STARTED);

		size_t position = seg->seg_offset_;

		DWORD set_ptr_result = InternetSetFilePointer(url_handle, (LONG)position, NULL, FILE_BEGIN, 0);
		if (position == set_ptr_result)
		{
			for ( ; position < seg->seg_offset_ + seg->size_; )
			{
				size_t read_size, nr_to_read;

				nr_to_read = CHUNK_SIZE;
				if (position + CHUNK_SIZE > seg->seg_offset_ + seg->size_)
					nr_to_read = seg->size_ - (position - seg->seg_offset_);

				bool read_chunk_result = seg->ReadChunk(url_handle, position, nr_to_read, read_size);

				if (!read_chunk_result)
					goto __finish;

				//MSDN: To ensure all data is retrieved, an application must continue 
				//to call the InternetReadFile function until the function returns 
				// TRUE and the lpdwNumberOfBytesRead parameter equals zero.
				if (read_size == 0)
					break;

				// Check for pause and stop event
				if (WAIT_OBJECT_0 == WaitForSingleObject(seg->pause_event_, 0))
				{
					// Freeze download loop 
					DWORD wait_result;
					HANDLE events[] = {seg->continue_event_, seg->stop_event_};
					do
					{
						wait_result = WaitForMultipleObjects(2, events, FALSE, 50);
					} while (!(WAIT_OBJECT_0 == wait_result || WAIT_OBJECT_0 + 1 == wait_result));
				}

				if (WAIT_OBJECT_0 == WaitForSingleObject(seg->stop_event_, 0))
					break;

				position += read_size;
			}


			seg->SetStatus(DOWNLOAD_FINISHED);
		}
		else
		{
			LOG(("InternelSetFilePointer() FAILURE, thread=0x%p, error=%d\n", GetCurrentThreadId(), GetLastError()));
			seg->SetStatus(DOWNLOAD_FAILURE);
		}
__finish:
		InternetCloseHandle(url_handle);
	}
	else
		LOG(("InternelOpenUrl() FAILURE, thread=0x%p, error=%d\n", GetCurrentThreadId(), GetLastError()));

	InternetCloseHandle(inet_handle);

	_endthreadex(0);
	return 0;
}

