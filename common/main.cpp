#include <windows.h>
#include <tchar.h>
#include <vector>
#include <map>
#include "curl/curl.h"
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
	curl_global_init(CURL_GLOBAL_ALL);

	UrlList url_list;
//	url_list.push_back("http://sandbox.ivan4ik.ru/downloader/porn.dat");
	url_list.push_back("http://localhost/bitdefender_totalsecurity_2010_32b-BETA2.exe");
	url_list.push_back("http://localhost/livecd.iso");
	Downloader d(url_list, 1000000000ULL);

	d.Run();

	curl_global_cleanup();

	return 0;
}
