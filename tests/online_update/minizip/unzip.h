#ifndef PTC_TEST_UPDATE_UNZIP_H
#define PTC_TEST_UPDATE_UNZIP_H
#include <stdint.h>
typedef void *unzFile;
typedef unsigned long uLong;
typedef struct { uint64_t number_entry; } unz_global_info64;
typedef struct {
    unsigned long size_filename, flag, compression_method, disk_num_start, external_fa;
    uint64_t compressed_size, uncompressed_size;
} unz_file_info64;
#define UNZ_OK 0
#define UNZ_END_OF_LIST_OF_FILE (-100)
unzFile unzOpen64(const char *path);
int unzClose(unzFile file);
int unzGetGlobalInfo64(unzFile file, unz_global_info64 *info);
int unzGoToFirstFile(unzFile file);
int unzGoToNextFile(unzFile file);
int unzGetCurrentFileInfo64(unzFile file, unz_file_info64 *info, char *name, uLong name_size,
    void *extra, uLong extra_size, char *comment, uLong comment_size);
int unzOpenCurrentFile(unzFile file);
int unzReadCurrentFile(unzFile file, void *buffer, unsigned length);
int unzCloseCurrentFile(unzFile file);
#endif
