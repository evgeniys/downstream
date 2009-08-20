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


#define CHUNK_SIZE (16 * 1024)

FileSegment::FileSegment(File *file, 
						 const StlString& url, 
						 size_t offset, size_t size, 
						 HANDLE pause_event, 
						 HANDLE continue_event,
						 HANDLE stop_event)
: file_(file), url_(url), offset_(offset), 
 size_(size), pause_event_(pause_event), 
 continue_event_(continue_event), stop_event_(stop_event)
{
	thread_ = 0;
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
	if (InternetReadFile(url_handle, &buf[0], (DWORD)chunk_size, (LPDWORD)&read_size))
	{
		file_->NotifyDownloadProgress(this, position, &buf[0], read_size);
		downloaded_size_ += read_size;
		return true;
	}
	else
	{
		SetStatus(DOWNLOAD_FAILURE);
		return false;
	}
}

unsigned __stdcall  FileSegment::FileSegmentThread(void *arg)
{
	FileSegment *seg = (FileSegment*)arg;
	const StlString header = _T("Accept: */*");

	HINTERNET inet_handle = 
		InternetOpen(_T("Mozilla/5.0"),
		PRE_CONFIG_INTERNET_ACCESS, NULL, NULL, 0);
	if (!inet_handle)
	{
		seg->SetStatus(DOWNLOAD_FAILURE);
		goto __exit_thread;
	}

	HINTERNET url_handle = InternetOpenUrl(inet_handle, 
		seg->url_.c_str(), header.c_str(), (DWORD)header.size(), 0, 0);

	if (url_handle)
	{
		seg->SetStatus(DOWNLOAD_STARTED);

		size_t position = seg->offset_;

		DWORD set_ptr_result = InternetSetFilePointer(url_handle, (LONG)position, NULL, FILE_BEGIN, 0);
		if (position != set_ptr_result)
		{
			size_t read_size_counter = 0;
			for ( ; position < seg->offset_ + seg->size_; position += CHUNK_SIZE)
			{
				size_t read_size, nr_to_read;

				nr_to_read = CHUNK_SIZE;
				if (seg->size_ - position != nr_to_read)
					nr_to_read = seg->size_ - position;

				bool read_chunk_result = seg->ReadChunk(url_handle, position, nr_to_read, read_size);
				if (read_chunk_result)
					read_size_counter += read_size;

				if (!read_chunk_result || read_size != nr_to_read)
				{
					seg->SetStatus(DOWNLOAD_FAILURE);
					goto __finish;
				}

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
			}
			seg->SetStatus(DOWNLOAD_FINISHED);
		}
		else
			seg->SetStatus(DOWNLOAD_FAILURE);
__finish:
		InternetCloseHandle(url_handle);
	}

	InternetCloseHandle(inet_handle);

__exit_thread:
	_endthreadex(0);
	return 0;
}

