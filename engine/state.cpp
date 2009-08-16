#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <string>
#include <map>
#include <vector>

using namespace std;

#include "engine/state.h"


bool State::Load()
{
	FILE *f = fopen("downloader.ini", "rt");
	if (!f)
	{
		InitDefault();
		return false;
	}

	vector <TCHAR> buf;
	buf.resize(0x1000);

	for ( ; ; )
	{
		if (!_fgetts(&buf[0], (int)buf.size() - 1, f))
			break;

		size_t len = _tcslen(&buf[0]);
		StlString line = StlString(buf.begin(), buf.end());
		line.resize(len);
		if (line.find_first_of(_T('\n')) == line.size() - 1)
			line.resize(line.size() - 1);
		size_t pos;
		if (StlString::npos != (pos = line.find(_T('='))))
		{
			StlString key = line.substr(0, pos);
			StlString value = line.substr(pos + 1);

			map_[key] = value;
		}
	}

	fclose(f);

	return true;
}

bool State::Save()
{
	FILE *f = fopen("downloader.ini", "wt");
	if (!f)
		return false;
	for (MapType::iterator iter = map_.begin(); iter != map_.end(); iter++) 
		_ftprintf(f, _T("%s=%s\n"), iter->first.c_str(), iter->second.c_str());
	fclose(f);
	return true;
}

void State::InitDefault()
{
//	map_[_T("current_file")] = _T("");
}

bool State::GetValue(const StlString &key_name, __out StlString &value)
{
	MapType::iterator iter = map_.find(key_name);

	if (map_.end() == iter)
		return false;

	value = iter->second;

	return true;
}

void State::SetValue(const StlString &key_name, const StlString &value)
{
	map_[key_name] = value;
}

