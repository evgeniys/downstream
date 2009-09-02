#ifndef _UNPACKER_H_
#define _UNPACKER_H_

#include "common/types.h"

class Unpacker 
{
public:
	Unpacker(const StlString& fname);
	bool Unpack(const StlString& out_dir);

private:
	StlString fname_;

	bool ZipUnpack(const StlString& out_dir);
	bool RarUnpack(const StlString& out_dir);
};

#endif
