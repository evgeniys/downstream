#ifndef _STATE_H_
#define _STATE_H_

#include "common/types.h"

class State
{
public:

	/**
	 *	Load state from INI file. If no INI file yet then state is initialized. 
	 *	@return true if state was read from disk, false if it was initialized
	 */
	bool Load();

	bool Save();

	bool GetValue(const StlString &key_name, __out StlString &value);

	void SetValue(const StlString &key_name, const StlString &value);

private:

	typedef std::map<StlString, StlString> MapType;

	MapType map_;

	void InitDefault();
};

#endif
