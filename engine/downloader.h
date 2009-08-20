#ifndef _DOWNLOADER_H_
#define _DOWNLOADER_H_

#include "common/types.h"
#include "engine/state.h"

typedef std::vector <std::string> UrlList;

class ProgressDialog;

class Downloader
{
public:
	Downloader(const UrlList &url_list, unsigned long long total_size);
	virtual ~Downloader(void);
	void Run(void);

protected:


private:

	UrlList url_list_;
	unsigned long long total_size_;

	unsigned long long total_progress_size_;

	HANDLE terminate_event_;

	HANDLE pause_event_;
	HANDLE continue_event_;
	HANDLE stop_event_;

	bool init_ok_;

	StlString folder_name_;

	State state_;

	bool SelectFolderName(void);

	bool IsEnoughFreeSpace(void);

	bool PerformDownload(const std::string& url, __out StlString& file_name);

	ProgressDialog *progress_dlg_;

	void ShowProgress(const StlString& url, size_t downloaded_size, size_t file_size,
					  const FILETIME& ft_start, const FILETIME& ft_current);

};

#endif
