#ifndef _DIALOG_H_
#define _DIALOG_H_

class Dialog
{
public:		
	void Create(DLGPROC dlg_proc);
	void Show(DWORD show);
	void Close();

protected:
	virtual INT_PTR CALLBACK DialogProcCallback(
		HWND hwndDlg,
		UINT uMsg,
		WPARAM wParam,
		LPARAM lParam
		);

private:


};


#endif
	
