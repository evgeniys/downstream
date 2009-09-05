#ifndef _FILESEGMENT_H_
#define _FILESEGMENT_H_

#include "common/types.h"
#include <boost/serialization/access.hpp>
#include "curl/curl.h"

class WebFileSegment
{
public:
	WebFileSegment(class WebFile *file,
				const std::string& url, 
				unsigned long long seg_offset, 
				unsigned long long size, 
				HANDLE pause_event,
				HANDLE continue_event,
				HANDLE stop_event);

	virtual ~WebFileSegment();

	void Restart();

	bool Start();

	bool IsFinished();

	bool Terminate();

	std::string &GetUrl() { return url_; }

	unsigned long long GetSegOffset() { return seg_offset_; }

	unsigned int GetStatus() { return download_status_; }

	HANDLE GetThreadHandle() { return thread_; } 

private:
	std::string url_;
	unsigned long long seg_offset_;
	unsigned long long size_;

	size_t downloaded_size_;
	unsigned int download_status_;


	/**
	 *	Pause,continue and stop events are set by Downloader 
	 *  when Pause/Continue or Exit button have been pressed by user.
	 */
	HANDLE pause_event_;
	HANDLE continue_event_;
	HANDLE stop_event_;
	class WebFile *file_;
	HANDLE thread_;
	void *http_handle_;
	lock_t lock_;

	static unsigned __stdcall FileSegmentThread(void *arg);

	void SetStatus(unsigned int status);

	// CURL callbacks
	static size_t DownloadWriteDataCallback(void *buffer, size_t size, size_t nmemb, void *userp);
	static int DebugCallback(CURL *handle, curl_infotype type, char *data, size_t size, void *userptr);
	static int ProgressCallback(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow);

	/* Serialization */
	friend class boost::serialization::access;

	template<class Archive>
	void save(Archive & ar, const unsigned int version)
	{
		Lock(&lock_);
		ar & download_status_;
		ar & seg_offset_;
		ar & downloaded_size_;
		Unlock(&lock_);
	}
	template<class Archive>
	void load(Archive & ar, const unsigned int version)
	{
		if (version > 0)
			return;
		Lock(&lock_);
		unsigned int status;
		ar & status;
		SetStatus(status); // download_status_ should be set atomically
		ar & seg_offset_;
		ar & downloaded_size_;
		Unlock(&lock_);
	}
};


#endif
