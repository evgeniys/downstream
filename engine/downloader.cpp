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
	terminate_event_ = CreateEvent(NULL, TRUE, FALSE, NULL);
	pause_event_ = CreateEvent(NULL, TRUE, FALSE, NULL);
	continue_event_ = CreateEvent(NULL, TRUE, FALSE, NULL);
	stop_event_ = CreateEvent(NULL, TRUE, FALSE, NULL);
	progress_dlg_ = NULL;
	init_ok_ = (NULL != terminate_event_);
}

Downloader::~Downloader(void)
{
	if (!init_ok_)
		return;
	if (terminate_event_)
		CloseHandle(terminate_event_);
	if (pause_event_)
		CloseHandle(pause_event_);
	if (continue_event_)
		CloseHandle(continue_event_);
	if (stop_event_)
		CloseHandle(stop_event_);
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
				Message::Show(_T("����� ��� ����������� ������ �� �������. �����"));
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

static bool IsPartOfMultipartArchive(const StlString& fname)
{
	return false;//FIXME
}

static bool UnpackFile(const StlString& fname)
{
	return false;//FIXME
}

void Downloader::Run()
{
	
	if (!init_ok_)
	{
		Message::Show(_T("������ ������������� ����������"));
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
			Message::Show(_T("������������ ����� (FIXME)"));
			if (!SelectFolderName())
				return;
		}
	}

	state_.Save();

	progress_dlg_ = new ProgressDialog(pause_event_, continue_event_);
	progress_dlg_->Create();
	progress_dlg_->Show(false);

	total_progress_size_ = 0;

	list <StlString> file_name_list;

	for (UrlList::iterator iter = url_list_.begin(); iter != url_list_.end(); iter++) 
	{
		StlString file_name;
		if (PerformDownload(*iter, file_name))
			file_name_list.push_back(file_name);
	}

	// Process file_name list (try to unpack files)
	for (list<StlString>::iterator iter = file_name_list.begin();
		iter != file_name_list.end(); iter++) 
	{
		if (IsPartOfMultipartArchive(*iter))
			continue;
		UnpackFile(*iter);
	}

	progress_dlg_->Close();
	progress_dlg_->WaitForClosing(INFINITE);
	delete progress_dlg_;
}

bool Downloader::PerformDownload(const string& url, __out StlString& file_name)
{
	StlString tmp, fname, wurl;

	wurl = StlString(url.begin(), url.end());

	size_t pos = wurl.find_last_of(_T('/'));

	if (-1 == pos || pos >= wurl.size() - 1)
	{
		Message::Show(StlString(_T("������: ������� ����� URL ")) + wurl);
		return false;
	}

	progress_dlg_->SetDisplayedData(wurl, 0, 0, 0);
	progress_dlg_->Show(true);

	tmp = wurl.substr(pos + 1);
	fname = folder_name_;
	
	if (fname.substr(fname.size() - 1, 1) != StlString(_T("\\")))
		fname += _T("\\");
	fname += tmp;

	file_name = fname;

	File file(wurl, fname, pause_event_, continue_event_, stop_event_);

	file.Start();

	SYSTEMTIME st_start, st_current;
	FILETIME ft_start, ft_current;
	GetSystemTime(&st_start);
	SystemTimeToFileTime(&st_current, &ft_current);

	unsigned int status;
	size_t downloaded_size;
	for ( ; ; )
	{
		GetSystemTime(&st_current);
		SystemTimeToFileTime(&st_current, &ft_current);
		file.GetDownloadStatus(status, downloaded_size);
		ShowProgress(wurl, downloaded_size, file.GetSize(), ft_start, ft_current);
		if (file.IsFinished())
			break;
		if (progress_dlg_->WaitForClosing(0))
			break;
		Sleep(100);
	}

	total_progress_size_ += file.GetSize();

	return true;
}

void Downloader::ShowProgress(const StlString& url, size_t downloaded_size, size_t file_size, 
							  const FILETIME& ft_start, const FILETIME& ft_current)
{
	unsigned int total_progress, file_progress, speed;

	total_progress = (unsigned int)((100 * total_progress_size_ + downloaded_size) / total_size_);

	file_progress = (unsigned int)(100 * downloaded_size / file_size);

	// Speed (KB/sec)
	speed = (unsigned int)((downloaded_size / 1024)
		/ (((*(ULONG64*)&ft_current - *(ULONG64*)&ft_start) * 10 * 1000000)));

	progress_dlg_->SetDisplayedData(url, speed, file_progress, total_progress);
}


