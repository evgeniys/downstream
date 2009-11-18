#ifndef _GETFILESDIALOG_H_
#define _GETFILESDIALOG_H_

#include "common/types.h"
#include "engine/downloader.h"

class GetFilesDialog
{
public:
	GetFilesDialog();
	~GetFilesDialog();
	bool Create();
	bool Show(bool show);
	bool Close();
	bool WaitForClosing(DWORD timeout);

private:

	UINT msg_taskbar_created_;
	HICON icon_handle_;

	NOTIFYICONDATA nid_;

	HANDLE create_event_;
	HWND hwnd_;

	HANDLE thread_handle_;

	static INT_PTR CALLBACK GetFilesDialogProc(      
		HWND hwndDlg,
		UINT uMsg,
		WPARAM wParam,
		LPARAM lParam
		);
	static unsigned __stdcall GetFilesDialogThread(void *arg);
};

#endif
