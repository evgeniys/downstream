#ifndef _CONSTS_H_
#define _CONSTS_H_

// Download status constants
#define STATUS_DOWNLOAD_NOT_STARTED   0
#define STATUS_DOWNLOAD_STARTED       1
#define STATUS_DOWNLOAD_FAILURE       2
#define STATUS_DOWNLOAD_FINISHED      3
#define STATUS_INVALID_URL            4
#define STATUS_INIT_FAILED            5
#define STATUS_DOWNLOAD_STOPPED       6
#define STATUS_FILE_CREATE_FAILURE    7
#define STATUS_MD5_CHANGED            8

// File part size (100 MB)
#define PART_SIZE (100 * 1024 * 1024)

// Unpack results
#define UNPACK_SUCCESS      0
#define UNPACK_NOT_ARCHIVE  1
#define UNPACK_NO_SPACE     2
#define UNPACK_INVALID_ARCHIVE 3
#define UNPACK_SYSTEM_ERROR 4
#define UNPACK_NO_PASSWORD  5

#endif
