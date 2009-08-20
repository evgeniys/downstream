#ifndef _DOWNLOADER_H_
#define _DOWNLOADER_H_

#include "common/types.h"

class FileSegment
{
public:
	FileSegment(class File *file,
				const StlString& url, 
				size_t offset, size_t size, 
				HANDLE pause_event,
				HANDLE continue_event,
				HANDLE stop_event);

	virtual ~FileSegment();
	
	bool Start();
	bool IsFinished();

	StlString &GetUrl() { return url_; }
	size_t GetOffset() { return offset_; }
	size_t GetSize() { return size_; }

	int GetAttemptCount() { return attempt_count_; }

	HANDLE GetThreadHandle() { return thread_; } 

private:
	StlString url_;
	size_t offset_;
	size_t size_;

	int attempt_count_;



	/**
	 *	Pause and Stop events are pulsed by Downloader when Pause or Exit 
	 *	button have been pressed by user. All FileSegment's must 
	 */
	HANDLE pause_event_;
	HANDLE continue_event_;
	HANDLE stop_event_;
	class File *file_;
	HANDLE thread_;
	
	static unsigned __stdcall FileSegmentThread(void *arg);
	bool ReadChunk(HINTERNET url_handle, size_t position, size_t chunk_size, __out size_t& read_size);
};


#endif
