#include <windows.h>
#include <commctrl.h>
#include <tchar.h>
#include <process.h>
#include <map>
#include <vector>
using namespace std;

#include "gui/progressdialog.h"
#include "resource1.h"

#define WM_USER_SETMESSAGE (WM_USER + 1)
#define WM_USER_SETPROGRESS (WM_USER + 3)
#define WM_USER_SHOWWND     (WM_USER + 4)
#define WM_USER_CLOSE (WM_USER + 5)
#define WM_TRAY_MESSAGE     (WM_USER + 6)

static ProgressDialog *dlg = NULL;


INT_PTR CALLBACK ProgressDialog::ProgressDialogProc(      
							HWND hwndDlg,
							UINT uMsg,
							WPARAM wParam,
							LPARAM lParam
							)
{
	switch (uMsg)
	{
	case WM_INITDIALOG:
		{
			// Set icon for dialog
			SendMessage(hwndDlg, WM_SETICON, ICON_SMALL, (LPARAM)dlg->icon_handle_);
			SendMessage(hwndDlg, WM_SETICON, ICON_BIG, (LPARAM)dlg->icon_handle_);
			// Create tray icon
			memset(&dlg->nid_, 0, sizeof(dlg->nid_));
			dlg->nid_.cbSize = sizeof(dlg->nid_);
			dlg->nid_.hWnd = hwndDlg;
			dlg->nid_.hIcon = dlg->icon_handle_;
			dlg->nid_.uID = 1;
			dlg->nid_.uCallbackMessage = WM_TRAY_MESSAGE;
			_tcsncpy(dlg->nid_.szTip, _T("Downloader"), _countof(dlg->nid_.szTip));
			dlg->nid_.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
			Shell_NotifyIcon(NIM_ADD, &dlg->nid_);
		}
		return TRUE;
	case WM_TRAY_MESSAGE:
		{
			if (1 == wParam)
			{
				switch(lParam)
				{
				case WM_LBUTTONDBLCLK:
				case WM_RBUTTONDBLCLK:
					ShowWindow(hwndDlg, SW_SHOW);
					return TRUE;
				}
			}

		}
		break;

	case WM_USER_SETMESSAGE:
		{
			TCHAR *text = (TCHAR*)wParam;
			SetDlgItemText(hwndDlg, IDC_STATIC_FILE_NAME, text);
		}
		return TRUE;
	case WM_USER_SETPROGRESS:
		{
			DWORD file_progress = (DWORD)wParam;
			DWORD total_progress = (DWORD)lParam;
			SendDlgItemMessage(hwndDlg, IDC_FILE_PROGRESS, PBM_SETPOS, file_progress, 0);
			SendDlgItemMessage(hwndDlg, IDC_TOTAL_PROGRESS, PBM_SETPOS, total_progress, 0);
		}
		return TRUE;
	case WM_USER_SHOWWND:
		ShowWindow(hwndDlg, (int)wParam);
		return TRUE;

	case WM_USER_CLOSE:
		Shell_NotifyIcon(NIM_DELETE, &dlg->nid_);
		DestroyWindow(hwndDlg); 
		PostQuitMessage(0);
		return TRUE;

	case WM_COMMAND:
		{
			switch(LOWORD(wParam))
			{
			case IDC_BUTTON_TRAY:
				ShowWindow(hwndDlg, SW_HIDE);
				return TRUE;
			case IDC_BUTTON_PAUSE:
				if (dlg)
				{
					dlg->Pause();
					SetDlgItemText(hwndDlg, IDC_BUTTON_PAUSE, dlg->IsPaused() ? _T("Resume") : _T("Pause"));
				}
				return TRUE;
			case IDCANCEL:
				Shell_NotifyIcon(NIM_DELETE, &dlg->nid_);
				DestroyWindow(hwndDlg); 
				PostQuitMessage(0);
				return TRUE;
			}
		}

	default:
		if (uMsg == dlg->msg_taskbar_created_)
			Shell_NotifyIcon(NIM_ADD, &dlg->nid_);
		return FALSE;
	}
	return FALSE;
}

ProgressDialog::ProgressDialog(HANDLE pause_event, 
							   HANDLE continue_event)
	
{
	if (dlg)
		return;
	paused_ = false;
	pause_event_ = pause_event;
	continue_event_ = continue_event;
	create_event_ = NULL;
	hwnd_ = NULL;
	dlg = this;
}

ProgressDialog::~ProgressDialog()
{
	dlg = NULL;
}

unsigned __stdcall ProgressDialog::ProgressDialogThread(void *arg)
{
	if (!dlg)
		goto __end;
	HINSTANCE instance = GetModuleHandle(NULL);

	dlg->icon_handle_ = LoadIcon(instance,  MAKEINTRESOURCE(IDI_MAIN));
	dlg->msg_taskbar_created_ = RegisterWindowMessage(_T("TaskbarCreated"));

	dlg->hwnd_ = CreateDialogParam(instance, 
		MAKEINTRESOURCE(IDD_PROGRESS_DIALOG),
		NULL, ProgressDialogProc, 0);

	ShowWindow(dlg->hwnd_, SW_HIDE);
	UpdateWindow(dlg->hwnd_);

	SetEvent(dlg->create_event_);

	MSG msg;
	BOOL bRet;

	while ((bRet = GetMessage(&msg, NULL, 0, 0)) != 0) 
	{ 
		if (bRet == -1)
			break;
		if(!IsDialogMessage(dlg->hwnd_, &msg))
		{ 
			TranslateMessage(&msg); 
			DispatchMessage(&msg); 
		} 
	}

__end:
	_endthreadex(0);
	return 0;
}

bool ProgressDialog::Create()
{
	if (!dlg)
		return false;
	create_event_ = CreateEvent(NULL, TRUE, FALSE, NULL);
	unsigned thread_id;
	thread_handle_ = (HANDLE)
		_beginthreadex(NULL, 0, ProgressDialogThread, this, 0, &thread_id);
	WaitForSingleObject(create_event_, INFINITE);
	CloseHandle(create_event_);
	return true;
}

bool ProgressDialog::Show(bool show)
{
	if (!hwnd_)
		return false;
	SendMessage(hwnd_, WM_USER_SHOWWND, show ? SW_SHOW : SW_HIDE, 0);
	return true;
}

bool ProgressDialog::Close()
{
	if (!hwnd_)
		return false;
	SendMessage(hwnd_, WM_USER_CLOSE, 0, 0);
	return true;
}

static StlString GetUserMessage(const StlString &fname, double speed)
{
	TCHAR msg_str[1024];
	if (0.0f == speed)
		_sntprintf(msg_str, _countof(msg_str), _T("%s"), fname.c_str());
	else
		_sntprintf(msg_str, _countof(msg_str), _T("%s (%.0lf Kb/sec)"), fname.c_str(), speed);
	StlString str = StlString(msg_str);
	return str;
}

bool ProgressDialog::SetDisplayedData(const StlString &fname, 
									  double speed,
									  unsigned int file_progress, 
									  unsigned int total_progress)
{
	if (!hwnd_)
		return false;
	SendMessage(hwnd_, WM_USER_SETMESSAGE, (WPARAM)GetUserMessage(fname, speed).c_str(), 0);
	SendMessage(hwnd_, WM_USER_SETPROGRESS, file_progress, total_progress);
	return true;
}

bool ProgressDialog::Pause()
{
	if (!hwnd_)
		return false;
	if (paused_)
	{
		ResetEvent(pause_event_);
		SetEvent(continue_event_);
	}
	else
	{
		ResetEvent(continue_event_);
		SetEvent(pause_event_);
	}
	paused_ = !paused_;
	return true;
}

bool ProgressDialog::IsPaused()
{
	return paused_;
}

bool ProgressDialog::WaitForClosing(DWORD timeout)
{
	if (!dlg)
		return false;
	return WAIT_OBJECT_0 == WaitForSingleObject(thread_handle_, timeout);
}
