#include <windows.h>
#include <tchar.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <map>

#include "engine/downloader.h"
#include "engine/state.h"
#include "engine/file.h"
#include "gui/message.h"
#include "gui/progressdialog.h"
#include "gui/selectfolder.h"
#include "common/misc.h"
#include "engine/md5.h"
#include "common/logging.h"
#include "common/consts.h"

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

	return thread_count_read && md5_list.size() > 0;
}

static bool GetParameters(const std::string& url, __out unsigned int& thread_count, 
						  __out std::list<string>& md5_list)
{
	const std::string param_url = url + ".md5";

	size_t size;
	if (!HttpGetFileSize(param_url, size))
		return false;

	bool ret_val = false;

	vector <BYTE> buf;
	buf.resize(size);
	size_t read_size;
	if (HttpReadFileToBuffer(param_url, &buf[0], size, read_size))
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
bool Downloader::GetFileDescriptorList()
{
	//FIXME show message
	bool ret_val = false;

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
			ret_val = true;
		}
	}
	//FIXME close message
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
			Message::Show(_T("В выбанном каталоге недостаточно места для\r\n")
				_T("сохранения загруженных файлов. Пожалуйста,\r\n")
				_T("выберите другой каталог или освободите место на\r\n")
				_T("диске.")
				);
			if (!SelectFolderName())
				return;
		}
	}

	state_.Save();

	if (!GetFileDescriptorList())
	{
		// Nothing to do; get out
		Message::Show(_T("Ошибка: не удалось получить информацию о закачках. Выход."));
		return;
	}

	progress_dlg_ = new ProgressDialog(pause_event_, continue_event_);
	progress_dlg_->Create();
	progress_dlg_->Show(false);

	total_progress_size_ = 0;

	list <StlString> file_name_list;

	bool md5_check_failed = false;
	bool aborted = false;
	StlString broken_url;

	for (FileDescriptorList::iterator iter = file_desc_list_.begin();
		iter != file_desc_list_.end(); iter++) 
	{
		StlString file_name;
		FileDescriptor& file_desc = *iter;
		if (PerformDownload(file_desc.url_, file_desc.thread_count_, file_name))
		{
			if (CheckMd5(file_desc.url_, file_name))
				file_name_list.push_back(file_name);
			else
			{
				broken_url = StlString(file_desc.url_.begin(), file_desc.url_.end());
				md5_check_failed = true;
				break;
			}
		}
		else
			aborted = true;
	}

	progress_dlg_->Close();
	progress_dlg_->WaitForClosing(INFINITE);
	delete progress_dlg_;

	// If user pressed 'Exit' without downloading all files - save state and exit
	if (aborted)
	{
		// FIXME save state
		return;
	}

	if (!md5_check_failed)
	{
		bool unpack_success = true;
		// Process file_name list (try to unpack files)
		for (list<StlString>::iterator iter = file_name_list.begin();
			iter != file_name_list.end(); iter++) 
		{
			if (!IsPartOfMultipartArchive(*iter))
				unpack_success = unpack_success && UnpackFile(*iter);
			if (!unpack_success)
				break;
		}
		if (unpack_success)
			Message::Show(_T("Файлы распакованы успешно."));
		else
			Message::Show(_T("Ошибка при распаковке. Пожалуйста, перезапустите\r\n")
							_T(" программу для повторного скачивания."));
	}
	else
		Message::Show(
			StlString(_T("Целостность файла\r\n")) 
			+ broken_url
			+ StlString(_T("\r\nнарушена. Обратитесь в службу технической поддержки.")));
}

bool Downloader::PerformDownload(const string& url, 
								 unsigned int thread_count,
								 __out StlString& file_name)
{
	StlString tmp, fname, wurl;

	wurl = StlString(url.begin(), url.end());

	size_t pos = wurl.find_last_of(_T('/'));

	if (-1 == pos || pos >= wurl.size() - 1)
	{
		Message::Show(StlString(_T("Ошибка: неверно задан URL ")) + wurl);
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

	File file(url, fname, thread_count, pause_event_, continue_event_, stop_event_);

	bool ret_val = false;

	if (file.Start())
	{
		SYSTEMTIME st_start, st_current;
		FILETIME ft_start, ft_current;
		GetSystemTime(&st_start);
		SystemTimeToFileTime(&st_start, &ft_start);

		unsigned int status;
		size_t downloaded_size;
		for ( ; ; )
		{
			GetSystemTime(&st_current);
			SystemTimeToFileTime(&st_current, &ft_current);
			size_t increment;
			file.GetDownloadStatus(status, downloaded_size, increment);
			total_progress_size_ += increment;
			if (WAIT_OBJECT_0 == WaitForSingleObject(pause_event_, 0))
			{
				// Re-initialize data for speed calculation, if paused
				GetSystemTime(&st_start);
				SystemTimeToFileTime(&st_start, &ft_start);
				downloaded_size = 0;
			}
			else
			{
				// Do not update progress if paused
				ShowProgress(wurl, downloaded_size, file.GetSize(), ft_start, ft_current);
			}
			if (file.WaitForFinish(0))
			{
				ret_val = true;
				break;
			}
			if (progress_dlg_->WaitForClosing(0))
			{
				file.Stop();
				if (!file.WaitForFinish(1000))
					file.Terminate();
				break;
			}
			Sleep(100);
		}
	}

	return ret_val;
}
	
bool Downloader::CheckMd5(const std::string& url, const StlString& file_name)
{
	FileDescriptorList::iterator file_desc_iter = FindDescriptor(url);
	if (file_desc_iter == file_desc_list_.end())
	{
		LOG(("[CheckMd5] ERROR: File descriptor not found ffor URL %s\n", url.c_str()));
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

#define SMALL_CHUNKS 0
	vector <BYTE> buf;

	const unsigned int BUF_SIZE = 1024 * 1024;
#if SMALL_CHUNKS
	buf.resize(BUF_SIZE);
#else
	buf.resize(PART_SIZE);
#endif

	ULARGE_INTEGER offset, size;
	size.LowPart = GetFileSize(file_handle, &size.HighPart);
	list<string>::iterator md5_iter = file_desc_iter->md5_list_.begin();
	for (offset.QuadPart = 0; offset.QuadPart < size.QuadPart; 
								offset.QuadPart += PART_SIZE, md5_iter++) 
	{
		DWORD read_size;

		MD5 part_md5;
		part_md5.reset();

		// TODO: Measure which variant works faster: with SMALL_CHUNKS or without it
#if SMALL_CHUNKS

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
#else

		if (ReadFile(file_handle, &buf[0], PART_SIZE, &read_size, NULL))
		{
			file_md5.append(&buf[0], read_size);
			MD5 part_md5;
			part_md5.reset();
			part_md5.append(&buf[0], read_size);
			part_md5.finish();
			string md5_str = part_md5.getFingerprint();
			if (md5_str != *md5_iter)
			{
				// MD5 is wrong
				ret_val = false;
				break;
			}
		}
#endif
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

void Downloader::ShowProgress(const StlString& url, size_t downloaded_size, size_t file_size, 
							  const FILETIME& ft_start, const FILETIME& ft_current)
{
	unsigned int total_progress, file_progress;
	double speed;

	total_progress = (unsigned int)((100 * total_progress_size_ + downloaded_size) / total_size_);

	file_progress = (unsigned int)((100 * (ULONG64)downloaded_size) / file_size);

	// Time, seconds
	ULONG64 time = (*(ULONG64*)&ft_current - *(ULONG64*)&ft_start) / 10000000;

	// Speed (KB/sec)
	if (0 == time)
		speed = 0.0f;
	else
		speed = ((double)downloaded_size) / (1024 * time);

	progress_dlg_->SetDisplayedData(url, speed, file_progress, total_progress);
}

