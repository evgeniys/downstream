#include <windows.h>
#include <tchar.h>

#include "gui/message.h"

void Message::Show(const StlString &message)
{
	MessageBox(NULL, message.c_str(), _T("Информация"), MB_OK);
}
