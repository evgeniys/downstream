#include <windows.h>
#include <stdio.h>
#include <vector>
#include <list>
#include <string>

using namespace std;

#include "archive/unpacker.h"
#include "archive/unrar/unrar.h"
#include "archive/unzip/unzip.h"
#include "archive/unzip/zip.h"
#include "archive/unzip/iowin32.h"
#include "archive/unzip/miniunz.h"
#include "common/logging.h"


static bool ReadFileToBuffer(const StlString& fname, void *buf, size_t size, __out size_t& read_size)
{
	HANDLE file_handle = CreateFile(fname.c_str(), GENERIC_READ, 
			FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (INVALID_HANDLE_VALUE == file_handle)
		return false;

	bool ret_val = (0 != ReadFile(file_handle, buf, (DWORD)size, (LPDWORD)&read_size, NULL));

	CloseHandle(file_handle);

	return ret_val;
}

Unpacker::Unpacker(const StlString& fname)
{
	fname_ = fname;
}

static bool IsZipFile(void *buf, size_t size)
{
	if (size < 4)
		return false;
	return *(ULONG32*)buf == 0x04034b50;
}

bool IsRarFile(void *buf, size_t size)
{
	if (size < 4)
		return false;
	return *(ULONG32*)buf == 0x21726152;
}

bool Unpacker::Unpack(const StlString& out_dir, __out bool& is_archive)
{
	is_archive = false;
	// Read header
	const size_t HEADER_SIZE = 0x10000;
	vector<BYTE> header_buf;
	header_buf.resize(HEADER_SIZE);
	size_t read_size;
	if (!ReadFileToBuffer(fname_, &header_buf[0], HEADER_SIZE, read_size))
		return false;

	TCHAR prev_dir[MAX_PATH];
	GetCurrentDirectory(MAX_PATH, prev_dir);
	SetCurrentDirectory(out_dir.c_str());

	bool ret_val = false;
	// Get archive type
	// Unpack depending on archive type
	is_archive = true;
	if (IsZipFile(&header_buf[0], read_size))
		ret_val = ZipUnpack(out_dir);
	else if (IsRarFile(&header_buf[0], read_size))
		ret_val = RarUnpack(out_dir);
	else
		is_archive = false;

	SetCurrentDirectory(prev_dir);

	return ret_val;
}

bool Unpacker::ZipUnpack(const StlString& out_dir)
{
	// Since MINIZIP works only with non-unicode file names,
	// we should copy input file to such file.
	char temp_file_name_buf[MAX_PATH];
	if (0 == GetTempFileNameA(".", "temp_archive", 0, temp_file_name_buf))
	{
		LOG(("[ZipUnpack] ERROR: cannot get temp file name\n"));
		return false;
	}

	string temp_file_name(temp_file_name_buf);
	if (!CopyFile(fname_.c_str(), 
		(StlString(temp_file_name.begin(), temp_file_name.end())).c_str(), FALSE))
	{
		LOG(("[ZipUnpack] ERROR: cannot copy archive to temp file\n"));
		return false;
	}

	zlib_filefunc_def ffunc;
	fill_win32_filefunc(&ffunc);

	bool ret_val = false;

	unzFile uf = unzOpen2(temp_file_name.c_str(), &ffunc);
	if (!uf)
		goto __end;

	int extract_result = do_extract(uf, 0, 0, NULL);
	ret_val = (0 == extract_result);

	unzClose(uf);

__end:
	DeleteFile((StlString(temp_file_name.begin(), temp_file_name.end())).c_str());

	return ret_val;
}

bool Unpacker::RarUnpack(const StlString& out_dir)
{
	bool ret_val = false;
	char comment_buf[1024];

	RAROpenArchiveDataEx archive_data;
	memset(&archive_data, 0, sizeof(archive_data));

#ifdef _UNICODE
	archive_data.ArcNameW = (wchar_t*)fname_.c_str();
#else
	archive_data.ArcName = (char*)fname_.c_str();
#endif
	archive_data.OpenMode = RAR_OM_EXTRACT;
	archive_data.CmtBuf = comment_buf;
	archive_data.CmtBufSize = sizeof(comment_buf);

	HANDLE archive_handle = RAROpenArchiveEx(&archive_data);
	if (archive_data.OpenResult != 0)
		return false;

	int rh_code;
	struct RARHeaderData header_data;
	header_data.CmtBuf = NULL;

	while (0 == (rh_code = RARReadHeader(archive_handle, &header_data)))
	{
		int pf_code = RARProcessFile(archive_handle, RAR_EXTRACT, NULL, NULL);//TODO add -W
		LOG(("RARProcessFile() returned %d\n", pf_code));
		ret_val = true; // Return true if at least one file has been unpacked
	}

	RARCloseArchive(archive_handle);

	return ret_val;
}
