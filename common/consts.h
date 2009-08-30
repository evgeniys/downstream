#ifndef _CONSTS_H_
#define _CONSTS_H_

// Download status constants
#define STATUS_DOWNLOAD_NOT_STARTED   0x0
#define STATUS_DOWNLOAD_STARTED       0x1
#define STATUS_DOWNLOAD_FAILURE       0x2
#define STATUS_DOWNLOAD_FINISHED      0x3
#define STATUS_DOWNLOAD_MERGE_FAILURE 0x4
#define STATUS_INVALID_URL            0x5
#define STATUS_INIT_FAILED            0x6
#define STATUS_DOWNLOAD_STOPPED       0x7

// File part size (100 MB)
#define PART_SIZE (100 * 1024 * 1024)

#endif