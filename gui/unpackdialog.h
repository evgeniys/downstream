#ifndef _UNPACKDIALOG_H_
#define _UNPACKDIALOG_H_

#include "common/types.h"
#include "engine/downloader.h"

class UnpackDialog
{
public:
	UnpackDialog();
	~UnpackDialog();
	bool SetDisplayedData(unsigned int progress);
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

	static INT_PTR CALLBACK UnpackDialogProc(      
		HWND hwndDlg,
		UINT uMsg,
		WPARAM wParam,
		LPARAM lParam
		);
	static unsigned __stdcall UnpackDialogThread(void *arg);
};

#endif
