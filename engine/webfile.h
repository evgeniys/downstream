#ifndef _FILE_H_
#define _FILE_H_

#include "common/types.h"
#include <list>
#include <boost/serialization/list.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/version.hpp>

class WebFileSegment;

#define FILE_THREAD_COUNT_CHANGED 0x00000001

class WebFile
{
public:
	WebFile(const std::string &url, const StlString& fname, 
			unsigned int thread_count,
			HANDLE pause_event, HANDLE continue_event, HANDLE stop_event);

	virtual ~WebFile();

	bool Start();
	void Stop();
	bool WaitForFinish(DWORD timeout);
	bool Terminate();

	/**
	 *	Update thread count; restart currently downloaded part.
	 */
	void UpdateThreadCount(unsigned int thread_count);

	/**
	 *	Get status for currently downloaded file.
	 *	Called from Downloader.
	 */
	void GetDownloadStatus(__out unsigned int& status, 
						   __out size_t& downloaded_size, 
						   __out size_t& increment);

	unsigned long long GetSize() { return file_size_; }

protected:

	/**
	 *	Download notify callbacks. Called from WebFileSegment-s.
	 *	These methods are thread-safe.
	 */

	void NotifyDownloadProgress(WebFileSegment *sender, 
								unsigned long long offset, 
								void *data, size_t size);

	bool GetDownloadParameters(__out bool& updated);

private:
	lock_t lock_;
	std::string url_;
	StlString fname_;
	unsigned int thread_count_;
	unsigned long long file_size_;
	std::vector <WebFileSegment *> segments_;
	unsigned int flags_;

	size_t part_num_;

	unsigned int download_status_;

	size_t downloaded_size_; // lock_ MUST be held when accessing this member
	size_t increment_;

	friend class WebFileSegment;

	HANDLE pause_event_;
	HANDLE continue_event_;
	HANDLE stop_event_;

	HANDLE file_handle_; // lock_ MUST be held when accessing this file
	HANDLE thread_handle_;

	std::vector<HANDLE> thread_handles_;

	static unsigned __stdcall FileThread(void *arg);

	bool DownloadPart(size_t part_num, unsigned long long offset, 
					  unsigned long long size, unsigned int thread_count);

	void SetStatus(unsigned int status);

	/* Serialization */
	friend class boost::serialization::access;

	template<class Archive>
	void save(Archive & ar, const unsigned int version)
	{
		Lock(lock_);
		ar & url_;
		ar & fname_;
		ar & segments_.size();
		ar & file_size_;
		ar & download_status_;
		ar & downloaded_size_;
		ar & part_num_;
		for (int i = 0; i < segments_.size(); i++) 
			ar & *(segments_[i]);
		Unlock(lock_);
	}
	template<class Archive>
	void load(Archive & ar, const unsigned int version)
	{
		if (version > 0)
			return;
		Lock(lock_);
		ar & url_;
		ar & fname_;
		ar & thread_count_;
		ar & file_size_;
		unsigned int status;
		ar & status;
		SetStatus(status); // download_status_ should be set atomically
		ar & downloaded_size_;
		ar & part_num_;
		flags_ = 0;
		segments_.resize(thread_count_);
		for (int i = 0; i < segments_.size(); i++) 
			ar & *(segments_[i]);
		Unlock(lock_);
	}
};

#endif
