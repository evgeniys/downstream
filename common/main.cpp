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
//	url_list.push_back("http://localhost/distrib/bitdefender_totalsecurity_2010_32b-BETA2.exe");
//	url_list.push_back("http://kernel.org/pub/linux/kernel/v2.6/linux-2.6.30.5.tar.bz2");
	Downloader d(url_list, 1000000000ULL);

	d.Run();
	
	return 0;
}
