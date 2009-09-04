#ifndef _DOWNLOADER_H_
#define _DOWNLOADER_H_

#include "common/types.h"
#include "engine/state.h"
#include <string>
#include <list>

typedef std::vector <std::string> UrlList;

class ProgressDialog;

// Flags for FileDescriptor.change_flags_
#define FC_THREAD_COUNT 0x00000001
#define FC_MD5          0x00000002

struct FileDescriptor {
	bool finished_;
	std::string url_;
	unsigned int thread_count_;
	std::list<std::string> md5_list_;
	unsigned int change_flags_;
	FileDescriptor(std::string& url)
		: url_(url), thread_count_(0), change_flags_(0), finished_(false)
	{
	}
	void Update(unsigned int thread_count, std::list<std::string> md5_list);
};

typedef std::list <FileDescriptor> FileDescriptorList;

class Downloader
{
public:

	Downloader(const UrlList &url_list, unsigned long long total_size);

	virtual ~Downloader(void);

	void Run(void);

protected:


private:
	
	FileDescriptorList file_desc_list_;

	bool GetFileDescriptorList(); // Process url_list and create file_desc_list_

	FileDescriptorList::iterator FindDescriptor(const std::string& url);

	bool CheckFileDescriptors(const std::string& current_url, 
							  __out bool& md5_changed, 
							  __out bool& thread_count_changed, 
							  __out unsigned int& new_thread_count);

	bool CheckNotFinishedDownloads();

	FileDescriptorList::iterator FindChangedMd5Descriptor();

	UrlList url_list_;

	unsigned long long total_size_; // Total size preconfigures inside program
	unsigned long long total_size_http_; // Total size obtained via HTTP requests

	unsigned long long total_progress_size_;

	HANDLE pause_event_;
	HANDLE continue_event_;
	HANDLE stop_event_;

	bool init_ok_;

	StlString folder_name_;

	State state_;

	bool SelectFolderName(void);

	bool IsEnoughFreeSpace(void);

	unsigned int PerformDownload(const std::string& url, 
								 unsigned int thread_count,
								 __out StlString& file_name);

	bool CheckMd5(const std::string& url, const StlString& file_name);

	ProgressDialog *progress_dlg_;

	void ShowProgress(const StlString& url, unsigned long long downloaded_size, unsigned long long file_size,
					  const FILETIME& ft_start, const FILETIME& ft_current);

	unsigned int UnpackFile(const StlString& fname);

	ULONG64 EstimateTotalSize();
};

#endif
