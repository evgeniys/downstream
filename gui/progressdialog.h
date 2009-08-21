#ifndef _PROGRESSDIALOG_H_
#define _PROGRESSDIALOG_H_

#include "common/types.h"
#include "engine/downloader.h"

class ProgressDialog
{
public:
	ProgressDialog(HANDLE pause_event, 
				   HANDLE continue_event);
	~ProgressDialog();
	bool SetDisplayedData(const StlString &fname, 
						  unsigned int speed,
						  unsigned int file_progress, 
						  unsigned int total_progress);
	bool Create();
	bool Show(bool show);
	bool Close();
	bool WaitForClosing(DWORD timeout);

private:

	HANDLE pause_event_;
	HANDLE continue_event_;
	HANDLE create_event_;
	HWND hwnd_;

	HANDLE thread_handle_;

	bool paused_;

	static INT_PTR CALLBACK ProgressDialogProc(      
		HWND hwndDlg,
		UINT uMsg,
		WPARAM wParam,
		LPARAM lParam
		);
	static unsigned __stdcall ProgressDialogThread(void *arg);

	bool Pause();

	bool IsPaused();

};

#endif
