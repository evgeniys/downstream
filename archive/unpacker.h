#ifndef _UNPACKER_H_
#define _UNPACKER_H_

#include "common/types.h"

class Unpacker 
{
public:
	Unpacker(const StlString& fname);
	unsigned int Unpack(const StlString& out_dir);

private:
	StlString fname_;

	unsigned int ZipUnpack(const StlString& out_dir);
	unsigned int RarUnpack(const StlString& out_dir);
};

#endif
