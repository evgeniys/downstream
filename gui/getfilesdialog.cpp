#include <windows.h>
#include <commctrl.h>
#include <tchar.h>
#include <process.h>
#include <map>
#include <vector>
using namespace std;

#include "gui/getfilesdialog.h"
#include "resource1.h"

#define WM_USER_SHOWWND     (WM_USER + 4)
#define WM_USER_CLOSE (WM_USER + 5)
#define WM_TRAY_MESSAGE     (WM_USER + 6)

static GetFilesDialog *dlg = NULL;

INT_PTR CALLBACK GetFilesDialog::GetFilesDialogProc(HWND hwndDlg,
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

GetFilesDialog::GetFilesDialog()
{
	if (dlg)
		return;
	create_event_ = NULL;
	hwnd_ = NULL;
	dlg = this;
}

GetFilesDialog::~GetFilesDialog()
{
	dlg = NULL;
}

unsigned __stdcall GetFilesDialog::GetFilesDialogThread(void *arg)
{
	if (!dlg)
		goto __end;
	HINSTANCE instance = GetModuleHandle(NULL);

	dlg->icon_handle_ = LoadIcon(instance,  MAKEINTRESOURCE(IDI_MAIN));
	dlg->msg_taskbar_created_ = RegisterWindowMessage(_T("TaskbarCreated"));

	dlg->hwnd_ = CreateDialogParam(instance, 
		MAKEINTRESOURCE(IDD_GETFILES_DIALOG),
		NULL, GetFilesDialogProc, 0);

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

bool GetFilesDialog::Create()
{
	if (!dlg)
		return false;
	create_event_ = CreateEvent(NULL, TRUE, FALSE, NULL);
	unsigned thread_id;
	thread_handle_ = (HANDLE)
		_beginthreadex(NULL, 0, GetFilesDialogThread, this, 0, &thread_id);
	WaitForSingleObject(create_event_, INFINITE);
	CloseHandle(create_event_);
	return true;
}

bool GetFilesDialog::Show(bool show)
{
	if (!hwnd_)
		return false;
	SendMessage(hwnd_, WM_USER_SHOWWND, show ? SW_SHOW : SW_HIDE, 0);
	return true;
}

bool GetFilesDialog::Close()
{
	if (!hwnd_)
		return false;
	SendMessage(hwnd_, WM_USER_CLOSE, 0, 0);
	return true;
}

bool GetFilesDialog::WaitForClosing(DWORD timeout)
{
	if (!dlg)
		return false;
	return WAIT_OBJECT_0 == WaitForSingleObject(thread_handle_, timeout);
}
