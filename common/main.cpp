#include <windows.h>
#include <tchar.h>
#include <vector>
#include <map>
using namespace std;

#include "common/types.h"
#include "engine/downloader.h"

int WINAPI WinMain(      
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    int nCmdShow
)
{
	UrlList url_list;
	url_list.push_back("http://sandbox.ivan4ik.ru/downloader/porn.dat");	

	Downloader d(url_list, 1000000000ULL);

	d.Run();
	
	return 0;
}
