#ifndef _DOWNLOADER_H_
#define _DOWNLOADER_H_

#include <tchar.h>
#include "common/types.h"
#include "engine/state.h"
#include <string>
#include <list>
#include <boost/serialization/access.hpp>
#include <boost/serialization/split_member.hpp>

typedef std::vector <std::string> UrlList;

class ProgressDialog;
class UnpackDialog;

// Flags for FileDescriptor.change_flags_
#define FC_THREAD_COUNT 0x00000001
#define FC_MD5          0x00000002

struct FileDescriptor {
	bool finished_;
	std::string url_;
	StlString file_name_;
	unsigned int thread_count_;
	std::list<std::string> md5_list_;
	unsigned int change_flags_;
	ULONG64 file_size_;
	FileDescriptor(std::string& url)
		: url_(url), thread_count_(0), change_flags_(0), 
		finished_(false), file_name_(_T("")), file_size_(0)
	{
	}
	FileDescriptor()
		: url_(""), thread_count_(0), change_flags_(0), 
		finished_(false), file_name_(_T("")), file_size_(0)
	{
	}
	void Update(unsigned int thread_count, std::list<std::string> md5_list);

	friend class boost::serialization::access;

	template<class Archive>
	void save(Archive & ar, const unsigned int version) const
	{
		ar & finished_;
		ar & url_;
		ar & file_name_;
		ar & thread_count_;
		ar & md5_list_;
		ar & file_size_;
	}

	template<class Archive>
	void load(Archive & ar, const unsigned int version)
	{
		if (version > 0)
			return;
		ar & finished_;
		ar & url_;
		ar & file_name_;
		ar & thread_count_;
		ar & md5_list_;
		ar & file_size_;
		change_flags_ = 0;
	}
	BOOST_SERIALIZATION_SPLIT_MEMBER()

};

typedef std::list <FileDescriptor> FileDescriptorList;

class WebFile;

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

	void DeleteChangedFiles();

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

	unsigned int PerformDownload(FileDescriptor& file_desc);
	unsigned int DownloadFile(std::string url, WebFile& file);

	bool CheckMd5(const std::string& url, const StlString& file_name);

	ProgressDialog *progress_dlg_;
	UnpackDialog *unpack_dlg_;

	void ShowProgress(const StlString& url, 
					  unsigned long long file_downloaded_size, 
					  unsigned long long downloaded_size_increment, 
					  unsigned long long file_size, 
					  const FILETIME& ft_start, const FILETIME& ft_current);

	unsigned int UnpackFile(const StlString& fname);

	ULONG64 EstimateTotalSize();

	bool LoadDownloadState(__out WebFile& file);
	bool SaveDownloadState(WebFile& file);
	void EraseDownloadState();

	bool GetFileNameFromUrl(const std::string& url, __out StlString& fname);

	void EstimateTotalProgressFromList();
};

#endif
