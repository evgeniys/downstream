#include <windows.h>
#include <tchar.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <algorithm>
#include <map>

#include "engine/downloader.h"
#include "engine/state.h"
#include "gui/message.h"
#include "gui/progressdialog.h"
#include "gui/selectfolder.h"

using namespace std;

Downloader::Downloader(const UrlList &url_list, unsigned long long total_size)
: total_size_(total_size)
{
	url_list_.resize(url_list.size());
	copy(url_list.begin(), url_list.end(), url_list_.begin());
	terminate_event_handle_ = CreateEvent(NULL, TRUE, FALSE, NULL);
	init_ok_ = (NULL != terminate_event_handle_);
}

Downloader::~Downloader(void)
{
	if (!init_ok_)
		return;
	CloseHandle(terminate_event_handle_);
}

bool IsValidFolder(StlString folder_name)
{
	bool ret_val;
	TCHAR curr_dir[MAX_PATH];
	GetCurrentDirectory(MAX_PATH, curr_dir);
	ret_val = (FALSE != SetCurrentDirectory(folder_name.c_str()));
	SetCurrentDirectory(curr_dir);
	return ret_val;	
}

bool Downloader::SelectFolderName(void)
{
	folder_name_ = _T("");	
	bool folder_name_selected;
	do 
	{
		folder_name_selected = state_.GetValue(_T("folder_name"), folder_name_);
		// Select folder for downloaded files if we started first time
		if (!folder_name_selected)
		{
			if (!SelectFolder::GetFolderName(folder_name_))
			{
				Message::Show(_T("Папка для скачиваемых файлов не выбрана. Выход"));
				return false;
			}
			state_.SetValue(_T("folder_name"), folder_name_);
			folder_name_selected = true;
		}
	} while(!folder_name_selected && !IsValidFolder(folder_name_));
	return true;		 
}

bool Downloader::IsEnoughFreeSpace()
{
	StlString disk_name;
	// Check path for drive letter
	if (folder_name_.substr(1, 2) != _T(":\\"))
	{
		TCHAR curr_dir[MAX_PATH];
		GetCurrentDirectory(MAX_PATH, curr_dir);
		disk_name = (StlString(curr_dir)).substr(0, 2);
	}
	else
		disk_name = folder_name_.substr(0, 2);
	DWORD sectors_per_cluster, bytes_per_sector, free_cluster_count, total_cluster_count;
	if (GetDiskFreeSpace(disk_name.c_str(), &sectors_per_cluster, &bytes_per_sector, &free_cluster_count, &total_cluster_count))
	{
		unsigned long long free_space = 
			(unsigned long long)free_cluster_count * sectors_per_cluster * bytes_per_sector;
		return free_space >= total_size_;
	}

	return false;
}

void Downloader::Run()
{
	
	if (!init_ok_)
	{
		Message::Show(_T("Ошибка инициализации приложения"));
		return;
	}

	// Load state.
	bool first_start = !state_.Load();	

	if (first_start)
	{
		if (!SelectFolderName())
			return;

		while(!IsEnoughFreeSpace())
		{
			Message::Show(_T("Недостаточно места (FIXME)"));
			if (!SelectFolderName())
				return;
		}
	}

	state_.Save();

//	UpdateStateFromServer();
	for (UrlList::iterator iter = url_list_.begin(); iter != url_list_.end(); iter++) 
		PerformDownload(*iter);


}

#if 0
void Downloader::UpdateStateFromServer()
{
	int thread_count = 20;//FIXME: get it from server

	TCHAR thread_count_str[0x100];
	_itot(thread_count, thread_count_str, 10);
	state_.SetValue(_T("thread_count"), StlString(&thread_count_str[0]));
}

void Downloader::DownloadThread(void *arg)
{
	Downloader *_this = (Downloader*)arg;

}
#endif

void Downloader::PerformDownload(const StlString& url)
{
	StlString tmp, fname;
	size_t pos = url.find_last_of(_T())
//	StlString fname = folder_name_ + 
}

