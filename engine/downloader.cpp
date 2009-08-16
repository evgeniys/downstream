#include <windows.h>
#include <tchar.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <algorithm>
#include <map>

#include "engine/downloader.h"
#include "engine/state.h"
#include "engine/file.h"
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
		disk_name = (StlString(curr_dir)).substr(0, 3);
	}
	else
		disk_name = folder_name_.substr(0, 3);
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

	if (first_start || !state_.GetValue(_T("folder_name"), folder_name_))
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

bool Downloader::PerformDownload(const string& url)
{
	StlString tmp, fname, wurl;



	wurl = StlString(url.begin(), url.end());

	size_t pos = wurl.find_last_of(_T('/'));

	if (-1 == pos || pos >= wurl.size() - 1)
	{
		Message::Show(StlString(_T("Ошибка: неверно задан URL ")) + wurl);
		return false;
	}

	tmp = wurl.substr(pos + 1);
	size_t x = folder_name_.size();
	fname = folder_name_;
	
	if (fname.substr(fname.size() - 1, 1) != StlString(_T("\\")))
		fname += _T("\\");
	fname += tmp;

	File file(wurl, fname);

	file.Start();

	for ( ; ; )
	{
		if (file.IsFinished())
			break;
		Sleep(1000);
	}

	return true;
}

