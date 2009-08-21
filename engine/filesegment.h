#ifndef _DOWNLOADER_H_
#define _DOWNLOADER_H_

#include "common/types.h"
#include <boost/serialization/access.hpp>

class FileSegment
{
public:
	FileSegment(class File *file,
				const StlString& url, 
				size_t part_offset,
				size_t seg_offset, size_t size, 
				HANDLE pause_event,
				HANDLE continue_event,
				HANDLE stop_event);

	virtual ~FileSegment();

	void Restart();
	bool Start();
	bool IsFinished();

	StlString &GetUrl() { return url_; }
	size_t GetPartOffset() { return part_offset_; }
	size_t GetSegOffset() { return seg_offset_; }
	size_t GetSize() { return size_; }
	unsigned int GetStatus() { return download_status_; }

	int GetAttemptCount() { return attempt_count_; }

	HANDLE GetThreadHandle() { return thread_; } 

private:
	StlString url_;
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
	
	static unsigned __stdcall FileSegmentThread(void *arg);
	bool ReadChunk(HINTERNET url_handle, size_t position, size_t chunk_size, __out size_t& read_size);

	void SetStatus(unsigned int status);

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
