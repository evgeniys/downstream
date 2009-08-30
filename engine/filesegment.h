#ifndef _FILESEGMENT_H_
#define _FILESEGMENT_H_

#include "common/types.h"
#include <boost/serialization/access.hpp>
#include "curl/curl.h"

class FileSegment
{
public:
	FileSegment(class File *file,
		const std::string& url, 
				size_t part_offset,
				size_t seg_offset, size_t size, 
				HANDLE pause_event,
				HANDLE continue_event,
				HANDLE stop_event);

	virtual ~FileSegment();

	void Restart();
	bool Start();
	bool IsFinished();

	std::string &GetUrl() { return url_; }
	size_t GetPartOffset() { return part_offset_; }
	size_t GetSegOffset() { return seg_offset_; }
	size_t GetSize() { return size_; }
	unsigned int GetStatus() { return download_status_; }

	int GetAttemptCount() { return attempt_count_; }

	HANDLE GetThreadHandle() { return thread_; } 

private:
	std::string url_;
	size_t part_offset_;
	size_t seg_offset_;
	size_t size_;

	int attempt_count_;

	size_t downloaded_size_;
	unsigned int download_status_;


	/**
	 *	Pause,continue and stop events are set by Downloader 
	 *  when Pause/Continue or Exit button have been pressed by user.
	 */
	HANDLE pause_event_;
	HANDLE continue_event_;
	HANDLE stop_event_;
	class File *file_;
	HANDLE thread_;
	void *http_handle_;

	size_t position_;
	
	static unsigned __stdcall FileSegmentThread(void *arg);

	void SetStatus(unsigned int status);

	// CURL callbacks
	static size_t DownloadWriteDataCallback(void *buffer, size_t size, size_t nmemb, void *userp);
	static int DebugCallback(CURL *handle, curl_infotype type, char *data, size_t size, void *userptr);
	static int ProgressCallback(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow);

	/* Serialization */
	friend class boost::serialization::access;
	template<class Archive>
	void serialize(Archive & ar, const unsigned int version)
	{
		if (version > 0)
			return;
		ar & download_status_;
		ar & downloaded_size_;
		ar & attempt_count_;
	}
};


#endif
