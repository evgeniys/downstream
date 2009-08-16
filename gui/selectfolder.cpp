#include <windows.h>
#include <stdlib.h>
#include <shlobj.h>
#include <tchar.h>
#include <vector>
using namespace std;

#include "gui/selectfolder.h"


bool SelectFolder::GetFolderName(__out StlString &folder_name)
{
	BROWSEINFO bi;
	std::vector <TCHAR> folder_name_buf;

	folder_name_buf.resize(MAX_PATH);

	memset(&bi, 0, sizeof(bi));
	bi.lpszTitle = folder_name.c_str();
	bi.pszDisplayName = &folder_name_buf[0];
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_SHAREABLE
		| BIF_EDITBOX | BIF_VALIDATE;
	bi.lpszTitle = _T("Выберите папку для скачиваемых файлов");
	
	LPITEMIDLIST item_id_list = SHBrowseForFolder(&bi);

	if (!item_id_list)
		return false;

	if (!SHGetPathFromIDList(item_id_list, &folder_name_buf[0]))
		return false;

	folder_name = StlString(folder_name_buf.begin(), folder_name_buf.end());
	
	return true;
}
