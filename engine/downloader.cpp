#include <windows.h>
#include <tchar.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <map>
#include <boost/regex/mfc.hpp>
#include <fstream>
#include <exception>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>

#include "engine/downloader.h"
#include "engine/state.h"
#include "engine/webfile.h"
#include "engine/webfilesegment.h"
#include "gui/message.h"
#include "gui/progressdialog.h"
#include "gui/unpackdialog.h"
#include "gui/getfilesdialog.h"
#include "gui/selectfolder.h"
#include "common/misc.h"
#include "engine/md5.h"
#include "common/logging.h"
#include "common/consts.h"
#include "archive/unpacker.h"

using namespace std;

Downloader::Downloader(const UrlList &url_list, unsigned long long total_size)
: total_size_(total_size)
{
	url_list_.resize(url_list.size());
	copy(url_list.begin(), url_list.end(), url_list_.begin());
	pause_event_ = CreateEvent(NULL, TRUE, FALSE, NULL);
	continue_event_ = CreateEvent(NULL, TRUE, FALSE, NULL);
	stop_event_ = CreateEvent(NULL, TRUE, FALSE, NULL);
	progress_dlg_ = NULL;
	unpack_dlg_ = NULL;
	init_ok_ = (NULL != pause_event_ 
		&& NULL != continue_event_
		&& NULL != stop_event_);
}

Downloader::~Downloader(void)
{
	if (pause_event_)
		CloseHandle(pause_event_);
	if (continue_event_)
		CloseHandle(continue_event_);
	if (stop_event_)
		CloseHandle(stop_event_);
}

ULONG64 Downloader::EstimateTotalSize()
{
	ULONG64 total = 0;

	for (FileDescriptorList::iterator iter = file_desc_list_.begin();
		iter != file_desc_list_.end(); iter++)
	{
		ULONG64 size;
		if (HttpGetFileSize(iter->url_, size))
			total += size;
	}

	return total;
}

static bool ParseParameters(std::vector <BYTE> buf, __out unsigned int& thread_count, 
							__out std::list<string>& md5_list)
{
	string str(buf.begin(), buf.end());
	bool thread_count_read = false;
	md5_list.clear();
	const char newline[] = "\n";
	for (size_t pos = 0; pos < str.size(); )
	{
		size_t new_pos = str.find(newline, pos);
		if (-1 == new_pos)
			new_pos = str.size();
		if (!thread_count_read)
		{
			thread_count = atoi((str.substr(pos, new_pos - pos)).c_str());
			thread_count_read = true;
		}
		else
		{
			string md5_str = str.substr(pos, min(0x20, new_pos - pos));
			std::transform(md5_str.begin(), md5_str.end(), md5_str.begin(), std::toupper);
			md5_list.push_back(md5_str);
		}
		if (new_pos == str.size())
			break;
		pos = new_pos + sizeof(newline) - 1;
	}

	return thread_count_read && thread_count > 0 && md5_list.size() > 0;
}

static bool GetParameters(const std::string& url, __out unsigned int& thread_count, 
						  __out std::list<string>& md5_list)
{
	const std::string param_url = url + ".md5";

	bool ret_val = false;

	vector <BYTE> buf;

	// .md5 files can be generated by script and "Content-Length" HTTP
	// header can be missed. Use adaptive algorithm with resizing buffer
	// dynamically during download.
	 
	if (HttpReadFileDynamic(param_url, buf))
		ret_val = ParseParameters(buf, thread_count, md5_list);

	return ret_val;
}

void FileDescriptor::Update(unsigned int thread_count, std::list<std::string> md5_list)
{
	change_flags_ = 0;
	if (thread_count_ != 0)
	{
		if (thread_count != thread_count_)
			change_flags_ |= FC_THREAD_COUNT;
		if (md5_list.size() != md5_list_.size())
			change_flags_ |= FC_MD5;
		else
		{
			if (!std::equal(md5_list.begin(), md5_list.end(), md5_list_.begin()))
				change_flags_ |= FC_MD5;
		}
	}
	thread_count_ = thread_count;
	md5_list_.resize(md5_list.size());
	std::copy(md5_list.begin(), md5_list.end(), md5_list_.begin());
}

FileDescriptorList::iterator Downloader::FindDescriptor(const string& url)
{
	FileDescriptorList::iterator iter;
	for (iter = file_desc_list_.begin(); iter != file_desc_list_.end(); iter++) 
	{
		if (iter->url_ == url)
			return iter;
	}
	return file_desc_list_.end();
}

/**
 *	Try to get download information for URL-s specified in the program.
 *	@return true if any download details were retrieved, false otherwise
 */
bool Downloader::GetFileDescriptorList(bool show_dialog)
{
	bool ret_val = false;
	GetFilesDialog *get_files_dlg;

	if (show_dialog)
	{
		get_files_dlg = new GetFilesDialog();
		get_files_dlg->Create();
		get_files_dlg->Show(true);
	}

	for (UrlList::iterator url_iter = url_list_.begin(); url_iter != url_list_.end(); url_iter++) 
	{
		unsigned int thread_count;
		list<string> md5_list;
		if (GetParameters(*url_iter, thread_count, md5_list))
		{
			FileDescriptorList::iterator file_desc_iter = FindDescriptor(*url_iter);
			if (file_desc_iter != file_desc_list_.end())
				file_desc_iter->Update(thread_count, md5_list);
			else
			{
				FileDescriptor file_desc(*url_iter);
				file_desc.Update(thread_count, md5_list);
				file_desc_list_.push_back(file_desc);
			}
			if (show_dialog && get_files_dlg->WaitForClosing(0))
			{
				ret_val = false;
				goto __end;
			}
			ret_val = true;
		}
	}

__end:
	if (show_dialog)
	{
		get_files_dlg->Close();
		get_files_dlg->WaitForClosing(INFINITE);
		delete get_files_dlg;
	}

	return ret_val;
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
		if (!folder_name_selected || !IsValidFolder(folder_name_))
		{
			if (!SelectFolder::GetFolderName(folder_name_))
			{
				Message::Show(_T("Folder for downloaded files is not selected. Exiting"));
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
	StlString fname_nopath;
	size_t last_slash_pos = fname.find_last_of(_T('\\'));
	if (fname.size() == last_slash_pos)
	{
		LOG(("[IsPartOfMultipartArchive] ERROR: wrong file name: %S\n", 
			(wstring(fname.begin(), fname.end())).c_str()));
		return false;
	}
	fname_nopath = (StlString::npos == last_slash_pos) ? fname : fname.substr(last_slash_pos + 1);

	boost::tregex re(_T("(.*)(\\.part)([0-9]+)(\\.rar)"), 
		boost::tregex::perl|boost::tregex::icase); 


	bool ret_val = false;

	if (boost::regex_match(fname_nopath, re))
	{
		size_t pos;
		pos = fname_nopath.find_last_of(_T('.'));
		if (StlString::npos != pos)
		{
			StlString tmp = fname_nopath.substr(0, pos);
			pos = tmp.find_last_of(_T(".part"));
			if (StlString::npos != pos && pos < tmp.size() - 1)
			{
				tmp = tmp.substr(pos + 1);
				int part_num = _ttoi(tmp.c_str());
				
				ret_val = (part_num != 1);
			}
		}
	}

	return ret_val;
}

unsigned int Downloader::UnpackFile(const StlString& fname)
{
	Unpacker u(fname);
	return u.Unpack(folder_name_);
}

FileDescriptorList::iterator Downloader::FindChangedMd5Descriptor()
{
	FileDescriptorList::iterator iter;

	for (iter = file_desc_list_.begin(); iter != file_desc_list_.end(); iter++) 
	{
		if (iter->change_flags_ & FC_MD5)
			return iter;
	}

	return iter;
}

void Downloader::DeleteChangedFiles()
{
	FileDescriptorList::iterator iter;

	for (iter = file_desc_list_.begin(); iter != file_desc_list_.end(); iter++) 
	{
		if (iter->change_flags_ & FC_MD5)
			DeleteFile(iter->file_name_.c_str());
	}
}

bool Downloader::CheckNotFinishedDownloads()
{
	for (FileDescriptorList::iterator iter = file_desc_list_.begin();
		iter != file_desc_list_.end(); iter++) 
	{
		if (!iter->finished_)
			return true;
	}
	return false;
}

void Downloader::EstimateTotalProgressFromList()
{
	for (FileDescriptorList::iterator iter = file_desc_list_.begin();
		iter != file_desc_list_.end(); iter++) 
	{
		if (iter->finished_)
			total_progress_size_ += iter->file_size_;
	}
}

void Downloader::Run()
{
	if (!init_ok_)
	{
		Message::Show(_T("������ ������������� ����������"));
		return;
	}

	// Load state.
	bool state_okay = state_.Load();

	if (state_okay && state_.GetValue(_T("folder_name"), folder_name_))
		state_okay = IsValidFolder(folder_name_);

	if (!state_okay)
	{
		if (!SelectFolderName())
			return;

		while(!IsEnoughFreeSpace())
		{
			Message::Show(_T("Insufficient disk space in the selected directory.\r\n\r\n")
				_T("Please choose another directory or free up the disk spacer\r\n")
				_T("in the selected one.\r\n")
				);
			if (!SelectFolderName())
				return;
		}
	}

	state_.Save();

	if (!GetFileDescriptorList(true))
	{
		// Nothing to do; get out
		Message::Show(_T("Error: could not get download indofmation. Exiting."));
		return;
	}

	total_size_http_ = EstimateTotalSize();

	progress_dlg_ = new ProgressDialog(pause_event_, continue_event_);
	progress_dlg_->Create();
	progress_dlg_->Show(true);

	total_progress_size_ = 0;

	bool abort = false;

	// Try to load download state. If successfully loaded, restart downloading
	// at the saved point.
	FileDescriptorList::iterator iter;
	WebFile loaded_file(pause_event_, continue_event_, stop_event_);
	if (LoadDownloadState(loaded_file))
	{
		EstimateTotalProgressFromList();
		unsigned int status;
		unsigned long long downloaded_size, increment;
		loaded_file.GetDownloadStatus(status, downloaded_size, increment);
		total_progress_size_ += downloaded_size;
		iter = FindDescriptor(loaded_file.GetUrl());
		if (iter != file_desc_list_.end())
		{
			StlString wurl(iter->url_.begin(), iter->url_.end());

			progress_dlg_->SetDisplayedData(wurl, 0, 0, 0);

			if (DownloadFile(iter->url_, loaded_file))
			{
				if (CheckMd5(iter->url_, iter->file_name_))
				{
					iter->finished_ = true;
					GetDiskFileSize(iter->file_name_, iter->file_size_);
				}
			}
		}
	}

__restart:

	for (iter = file_desc_list_.begin(); iter != file_desc_list_.end(); )
	{
		StlString file_name;
		if (iter->finished_)
			goto __next_iteration;

		if (!GetFileNameFromUrl(iter->url_, iter->file_name_))
			return;

		unsigned int download_status = PerformDownload(*iter);
		if (STATUS_DOWNLOAD_FINISHED == download_status)
		{
			if (CheckMd5(iter->url_, iter->file_name_))
			{
				iter->finished_ = true;
				GetDiskFileSize(iter->file_name_, iter->file_size_);
			}
			else
			{
				StlString broken_url = StlString(iter->url_.begin(), iter->url_.end());
				Message::Show(
					StlString(_T("File \r\n")) 
					+ broken_url
					+ StlString(_T("\r\n is corrupted. Please contact support service.")));
				abort = true;
				break;
			}
		}
		else if (STATUS_INIT_FAILED == download_status)
		{
			Message::Show(_T("Internal program error. Please contact support service.\r\n"));
			abort = true;
			break;
		}
		else if (STATUS_INVALID_URL == download_status)
		{
			LOG(("Invalid URL: %s. Try to download this file next time\r\n", 
				iter->url_));
		}
		else if (STATUS_DOWNLOAD_FAILURE == download_status)
		{
			LOG(("Download failure for URL: %s. Try to download this file next time\r\n", 
				iter->url_));
		}
		else if (STATUS_FILE_CREATE_FAILURE == download_status)
		{
			Message::Show(StlString(_T("Unable to create file for URL "))
				+ StlString(iter->url_.begin(), iter->url_.end()));
			abort = true;
			break;
		}
		else if (STATUS_DOWNLOAD_STOPPED == download_status) // Stopped by user
		{
			abort = true; // Silently exit
			break;
		}
		else if (STATUS_MD5_CHANGED == download_status)
		{
			Message::Show(
				StlString(_T("Certain files have been changed on the server during \r\n")
				_T("the download process. They will be redownloaded.")));

			DeleteChangedFiles();

			// Roll back to changed MD5.
			iter = FindChangedMd5Descriptor();
			continue;
		}

__next_iteration:
		iter++;
	}

	// Restart until all files are downloaded
	if (!abort && CheckNotFinishedDownloads())
	{
		Sleep(100);
		goto __restart;
	}

	progress_dlg_->Close();
	progress_dlg_->WaitForClosing(INFINITE);
	delete progress_dlg_;

	// If user pressed 'Exit' without downloading all files, exit
	if (abort)
		return;

	EraseDownloadState();

	// Process file_name list (try to unpack files)
	bool unpack_success = true;
	bool at_least_one_archive = false;

	unpack_dlg_ = new UnpackDialog();
	unpack_dlg_->Create();
	unpack_dlg_->Show(true);

	unsigned int total_file_count = 0, file_num = 0;
	for (FileDescriptorList::iterator iter = file_desc_list_.begin(); 
			iter != file_desc_list_.end(); iter++)
		if (iter->finished_ && !IsPartOfMultipartArchive(iter->file_name_))
			total_file_count++;

	for (FileDescriptorList::iterator iter = file_desc_list_.begin(); 
		iter != file_desc_list_.end(); iter++)
	{
		if (iter->finished_ && !IsPartOfMultipartArchive(iter->file_name_))
		{
			unsigned int unpack_result;
			unpack_result = UnpackFile(iter->file_name_);

			// Show progress
			file_num++;
			unsigned int unpack_progress = (file_num * 100) / total_file_count;
			unpack_dlg_->SetDisplayedData(unpack_progress);

			// Check if unpack dialog is closed
			if (unpack_dlg_->WaitForClosing(0))
			{
				unpack_dlg_->Close();
				unpack_dlg_->WaitForClosing(INFINITE);
				delete unpack_dlg_;
				return;
			}

			if (UNPACK_NO_SPACE == unpack_result)
			{
				Message::Show(_T("Insufficient disk space to perform file\r\n")
					_T("extraction. Please free up the space and restart this\r\n")
					_T("downloader, thus it can extract the files downloaded."));
				return;
			}
			if (UNPACK_NOT_ARCHIVE != unpack_result)
			{
				at_least_one_archive = true;
				unpack_success = unpack_success && (UNPACK_SUCCESS == unpack_result);
			}
		}
		if (!unpack_success)
			break;
	}

	unpack_dlg_->Close();
	unpack_dlg_->WaitForClosing(INFINITE);
	delete unpack_dlg_;

	if (at_least_one_archive)
	{	
		if (unpack_success)
			Message::Show(_T("Files have been unpacked successfully."));
		else
			Message::Show(_T("Error while unpacking files\r\n.")
						  _T("\r\n. Please restart program to download files again"));
	}

}

static void GetTime(__out FILETIME& ft)
{
	SYSTEMTIME st;
	GetSystemTime(&st);
	SystemTimeToFileTime(&st, &ft);
}

// Get time difference in milliseconds
static DWORD64 GetTimeDiff(const FILETIME& ft)
{
	FILETIME ft_current;
	GetTime(ft_current);
	DWORD64 time = (*(DWORD64*)&ft_current - *(DWORD64*)&ft) / 10000;
	return time;
}

bool Downloader::CheckFileDescriptors(const std::string& current_url, 
									  __out bool& md5_changed, 
									  __out bool& thread_count_changed, 
									  __out unsigned int& new_thread_count)
{
	md5_changed = false;
	thread_count_changed = false;
	if (GetFileDescriptorList(false))
	{
		bool md5_changed = false;
		for (FileDescriptorList::iterator iter = file_desc_list_.begin(); 
			iter != file_desc_list_.end(); iter++) 
		{
			if (iter->change_flags_ & FC_MD5)
				md5_changed = true;
			if (iter->url_ == current_url)
			{
				if (iter->change_flags_ & FC_THREAD_COUNT)
				{
					thread_count_changed = true;
					new_thread_count = iter->thread_count_;
				}
				break;
			}
		}
		return md5_changed || thread_count_changed;
	}
	else
		return false;
}

bool Downloader::GetFileNameFromUrl(const std::string& url, __out StlString& fname)
{
	StlString tmp, wurl;

	wurl = StlString(url.begin(), url.end());

	size_t pos = wurl.find_last_of(_T('/'));

	if (-1 == pos || pos >= wurl.size() - 1)
	{
		Message::Show(StlString(_T("Error: invalid URL\r\n")) + wurl);
		return false;
	}

	tmp = wurl.substr(pos + 1);
	fname = folder_name_;

	if (fname.substr(fname.size() - 1, 1) != StlString(_T("\\")))
		fname += _T("\\");
	fname += tmp;

	return true;
}

unsigned int Downloader::DownloadFile(std::string url, WebFile& file)
{
	bool md5_changed = false;
	// Check .md5 updates every 10 min. Save download state every 1 sec.
	const DWORD64 SAVE_PERIOD = 1000, MD5_CHECK_PERIOD = 10 * 60 * 1000; 
	unsigned int ret_val = STATUS_DOWNLOAD_FAILURE;

	if (!file.Start())
		return ret_val;

	FILETIME ft_start, ft_current, ft_md5_check, ft_save;
	GetTime(ft_save);
	GetTime(ft_md5_check);
	GetTime(ft_start);

	unsigned int status;
	unsigned long long downloaded_size, download_size_increment = 0;
	for ( ; ; )
	{
		if (GetTimeDiff(ft_save) >= SAVE_PERIOD)
		{
			GetTime(ft_save);
			SaveDownloadState(file);
		}
		if (GetTimeDiff(ft_md5_check) >= MD5_CHECK_PERIOD)
		{
			GetTime(ft_md5_check);
			bool thread_count_changed;
			unsigned int new_thread_count;
			if (CheckFileDescriptors(url, md5_changed, thread_count_changed, new_thread_count))
			{
				if (!md5_changed && thread_count_changed)
				{
					// Changed thread count for current file.
					// Restart current part of file.
					file.UpdateThreadCount(new_thread_count);
				}
				else 
				{
					// Changed MD5 for one of earlier downloaded files or for this file.
					// Stop downloading this file.
					file.Stop();
					if (!file.WaitForFinish(1000))
						file.Terminate();
					ResetEvent(stop_event_);
					// Show message if redownloading
					ret_val = STATUS_MD5_CHANGED;
					break;
				}
			}
		}
		GetTime(ft_current);
		unsigned long long increment;
		file.GetDownloadStatus(status, downloaded_size, increment);
		download_size_increment += increment;
		total_progress_size_ += increment;
		if (WAIT_OBJECT_0 == WaitForSingleObject(pause_event_, 0))
		{
			// Re-initialize data for speed calculation, if paused
			GetTime(ft_start);
			download_size_increment = 0;
		}
		else
		{
			// Do not update progress if paused
			ShowProgress(StlString(url.begin(), url.end()), downloaded_size, 
				download_size_increment, file.GetSize(), ft_start, ft_current);
		}
		if (file.WaitForFinish(0))
		{
			file.GetDownloadStatus(ret_val, downloaded_size, increment);
			if (STATUS_DOWNLOAD_FINISHED != ret_val)
				LOG(("[PerformDownload] File is not downloaded. URL: %s, download status: 0x%x\n", 
				url.c_str(), status));
			break;
		}
		if (progress_dlg_->WaitForClosing(0))
		{
			file.Stop();
			if (!file.WaitForFinish(1000))
				file.Terminate();
			ret_val = STATUS_DOWNLOAD_STOPPED;
			break;
		}
		Sleep(100);
	}

	return ret_val;

}

unsigned int Downloader::PerformDownload(FileDescriptor& file_desc)
{
	StlString tmp, fname, wurl;

	wurl = StlString(file_desc.url_.begin(), file_desc.url_.end());

	progress_dlg_->SetDisplayedData(wurl, 0, 0, 0);

	WebFile file(file_desc.url_, file_desc.file_name_, file_desc.thread_count_, 
		pause_event_, continue_event_, stop_event_);

	return DownloadFile(file_desc.url_, file);
}

bool Downloader::CheckMd5(const std::string& url, const StlString& file_name)
{
	FileDescriptorList::iterator file_desc_iter = FindDescriptor(url);
	if (file_desc_iter == file_desc_list_.end())
	{
		LOG(("[CheckMd5] ERROR: File descriptor not found for URL %s\n", url.c_str()));
		return false;
	}

	HANDLE file_handle = OpenOrCreate(file_name, GENERIC_READ);
	if (INVALID_HANDLE_VALUE == file_handle)
	{
		LOG(("[CheckMd5] ERROR: Could not open file: %S\n", 
			std::wstring(file_name.begin(), file_name.end()).c_str()));
		return false;
	}

	MD5 file_md5;
	file_md5.reset();

	bool ret_val = true;

	vector <BYTE> buf;

	const unsigned int BUF_SIZE = 1024 * 1024;
	buf.resize(BUF_SIZE);

	ULARGE_INTEGER offset, size;
	size.LowPart = GetFileSize(file_handle, &size.HighPart);
	list<string>::iterator md5_iter = file_desc_iter->md5_list_.begin();
	for (offset.QuadPart = 0; offset.QuadPart < size.QuadPart; 
								offset.QuadPart += PART_SIZE, md5_iter++) 
	{
		DWORD read_size;

		MD5 part_md5;
		part_md5.reset();

		ULONG64 pos = 0;
		do {
			if (ReadFile(file_handle, &buf[0], BUF_SIZE, &read_size, NULL))
			{
				LOG(("Read OK, pos=0x%llx, read_size=0x%x\n", pos, read_size));
				file_md5.append(&buf[0], read_size);
				part_md5.append(&buf[0], read_size);

				pos += read_size;

				if (pos == PART_SIZE || read_size != BUF_SIZE)
				{
					part_md5.finish();
					string md5_str = part_md5.getFingerprint();
					if (md5_str != *md5_iter)
					{
						// MD5 is wrong
						ret_val = false;
						goto __end;
					}
					break;
				}
			}
			else
			{
				ret_val = false;
				goto __end;
			}
		} while (read_size == BUF_SIZE);

		if (distance(md5_iter, file_desc_iter->md5_list_.end()) == 1)
		{
			// MD5 list is too short
			ret_val = false;
			goto __end;
		}
	}

	file_md5.finish();

	if (ret_val && md5_iter != file_desc_iter->md5_list_.end())
	{
		// Check total MD5
		if (file_md5.getFingerprint() != *md5_iter)
			ret_val = false;
	}
__end:
	CloseHandle(file_handle);

	return ret_val;
}

void Downloader::ShowProgress(const StlString& url, 
							  unsigned long long file_downloaded_size, 
							  unsigned long long downloaded_size_increment, 
							  unsigned long long file_size, 
							  const FILETIME& ft_start, const FILETIME& ft_current)
{
	unsigned int total_progress, file_progress;
	double speed;

	unsigned long long tmp_total_size = total_size_http_ ? total_size_http_ : total_size_;

	total_progress = (unsigned int)((100 * total_progress_size_ /* + file_downloaded_size */) / tmp_total_size);

	file_progress = (unsigned int)((100 * file_downloaded_size) / file_size);

	// Time, microseconds
	ULONG64 time = (*(ULONG64*)&ft_current - *(ULONG64*)&ft_start) / 10;

	// Speed (KB/sec)
	if (0 == time)
		speed = 0.0f;
	else
		speed = 1.0e+6 * (((double)downloaded_size_increment) / (1024 * time));

	progress_dlg_->SetDisplayedData(url, speed, file_progress, total_progress);
}

bool Downloader::LoadDownloadState(__out WebFile& file)
{
	bool ret_val = true;
	ifstream ifs;
	try {
		ifs.open("downloader.state", ios_base::in);
		boost::archive::text_iarchive ia(ifs);
		ia >> file_desc_list_;
		file.Down();
		ia >> file;
		file.Up();
		ifs.close();
	}
	catch (boost::archive::archive_exception& ) {
		ret_val = false;
	}
	return ret_val;
}

void Downloader::EraseDownloadState()
{
	DeleteFileA("downloader.state");
}

bool Downloader::SaveDownloadState(WebFile& file)
{
	bool ret_val = true;
	ofstream ofs;
	
	try {
		ofs.open("downloader.state", ios_base::out);
		boost::archive::text_oarchive oa(ofs);
		oa << file_desc_list_;
		file.Down();
		oa << file;
		file.Up();
		ofs.close();
	}
	catch (boost::archive::archive_exception& ) {
		ret_val = false;
	}

	return ret_val;
}
