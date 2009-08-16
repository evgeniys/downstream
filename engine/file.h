#ifndef _FILE_H_
#define _FILE_H_

#include "common/types.h"
#include <list>

#define DOWNLOAD_RECEIVED_DATA 0x1
#define DOWNLOAD_FAILURE       0x2
#define DOWNLOAD_FINISHED      0x3

class FileSegment;

class File
{
public:
	File(const StlString &url, const StlString &fname);

	virtual ~File();

	bool Start();
	bool IsFinished();
	/**
	 *	Get status for currently downloaded file.
	 *	Called from Downloader.
	 */
	bool GetDownloadStatus(__out unsigned int& status, 
						   __out size_t& downloaded_size);

protected:
	/**
	 *	Download notify callbacks. Called from FileSegment-s.
	 *	These methods are thread-safe.
	 */

	void NotifyDownloadProgress(FileSegment *sender, 
								size_t offset, 
								void *data, size_t size);

	void NotifyDownloadStatus(FileSegment *sender, 
							  unsigned int status, 
							  uintptr_t arg1, 
							  uintptr_t arg2);



private:
	lock_t lock_;
	StlString url_;
	StlString fname_;
	unsigned int thread_count_;
	size_t downloaded_size_;
	std::list <std::string> md5_list_;

	friend class FileSegment;

	std::list <class FileSegment *> gc_list_;
	lock_t gc_lock_;
	void GarbageCollect();

	bool GetDownloadParameters(__out bool& updated);

	unsigned __stdcall FileThread(void *arg);
};

#endif
