#ifndef _FILE_H_
#define _FILE_H_

#include "common/types.h"
#include <list>
#include <boost/serialization/list.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/version.hpp>

#define DOWNLOAD_NOT_STARTED   0x0
#define DOWNLOAD_STARTED       0x1
#define DOWNLOAD_FAILURE       0x2
#define DOWNLOAD_FINISHED      0x3

class FileSegment;

class File
{
public:
	File(const StlString &url, const StlString& fname, 
		HANDLE pause_event, HANDLE continue_event, HANDLE stop_event);

	virtual ~File();

	bool Start();
	bool IsFinished();
	/**
	 *	Get status for currently downloaded file.
	 *	Called from Downloader.
	 */
	void GetDownloadStatus(__out unsigned int& status, 
						   __out size_t& downloaded_size);

	size_t GetSize() { return file_size_; }

protected:

	/**
	 *	Download notify callbacks. Called from FileSegment-s.
	 *	These methods are thread-safe.
	 */

	void NotifyDownloadProgress(FileSegment *sender, 
								size_t offset, 
								void *data, size_t size);

	void NotifyDownloadStatus(FileSegment *sender, 
							  unsigned int status);



private:
	lock_t lock_;
	StlString url_;
	StlString fname_;
	unsigned int thread_count_;
	size_t file_size_;
	std::list <std::string> md5_list_;
	std::vector <FileSegment *> segments_;

	unsigned int download_status_; // lock_ MUST be held when accessing this member
	size_t downloaded_size_; // lock_ MUST be held when accessing this member
	

	friend class FileSegment;

	HANDLE pause_event_;
	HANDLE continue_event_;
	HANDLE stop_event_;

	HANDLE part_file_handle_; // lock_ MUST be held when accessing this file
	HANDLE thread_handle_;

	bool GetDownloadParameters(__out bool& updated);

	static unsigned __stdcall FileThread(void *arg);

	bool DownloadPart(size_t part_num, size_t offset, size_t size);

	bool MergeParts();

	// Abort download if unrecoverable error occurred during download. 
	// Called from NotifyDownloadStatus.
	void AbortDownload(unsigned int status);

	/* Serialization */
	friend class boost::serialization::access;
	template<class Archive>
	void serialize(Archive & ar, const unsigned int version)
	{
		if (version > 0)
			return;
		ar & url_;
		ar & fname_;
		ar & thread_count_;
		ar & file_size_;
		ar & downloaded_size_;
		ar & md5_list_;
		//TODO: fix serialization
	}

};

#endif
