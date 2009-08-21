#include <windows.h>
#include <tchar.h>
#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>

using namespace std;

#include "engine/state.h"

bool State::Load()
{
	bool ret_val = true;
	ifstream ifs;
	try {
		ifs.open("downloader.config", ios_base::in);
		boost::archive::text_iarchive ia(ifs);
		ia >> (*this);
		ifs.close();
	}
	catch (...) {
		ret_val = false;
	}
	return ret_val;
}

bool State::Save()
{
	bool ret_val = true;
	ofstream ofs;
	try {
		ofs.open("downloader.config", ios_base::out);
		boost::archive::text_oarchive oa(ofs);
		oa << (*this);
		ofs.close();
	}
	catch (...) {
		ret_val = false;
	}
	return ret_val;
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

