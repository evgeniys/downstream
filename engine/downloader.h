#ifndef _DOWNLOADER_H_
#define _DOWNLOADER_H_

#include "common/types.h"
#include "engine/state.h"

// File part size (100 MB)
#define PART_SIZE (100 * 1024 * 1024)

typedef std::vector <std::string> UrlList;

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

	HANDLE terminate_event_handle_;

	bool init_ok_;

	StlString folder_name_;

	State state_;

	bool SelectFolderName(void);

	bool IsEnoughFreeSpace(void);

	bool PerformDownload(const std::string& url);
};

#endif
