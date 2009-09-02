#ifndef _MINIUNZ_H_
#define _MINIUNZ_H_

#ifdef __cplusplus
extern "C" {
#endif

int do_extract(unzFile uf, 
				int opt_extract_without_path,
				int opt_overwrite,
				const char* password);

#ifdef __cplusplus
};
#endif

#endif	