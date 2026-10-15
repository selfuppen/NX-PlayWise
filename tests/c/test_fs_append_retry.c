#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../platform/switch/fs_storage.h"

static const char *path = "build/host/fs-append-retry.txt";
static unsigned opens, writes, closes, sleeps, open_failures;
static int open_error;
static bool write_failure, close_failure;

FILE *ptc_test_fopen(const char *name, const char *mode)
{
    assert(strcmp(name, path) == 0 && strcmp(mode, "ab") == 0);
    ++opens;
    if (open_failures) { --open_failures; errno = open_error; return NULL; }
    return fopen(name, mode);
}

int ptc_test_fprintf(FILE *file, const char *format, ...)
{
    int result;
    va_list args;
    ++writes;
    if (write_failure) {
        assert(fputs("partial", file) >= 0);
        errno = EIO;
        return -1;
    }
    va_start(args, format);
    result = vfprintf(file, format, args);
    va_end(args);
    return result;
}

int ptc_test_fclose(FILE *file)
{
    ++closes;
    int result = fclose(file);
    if (close_failure) { errno = EIO; return EOF; }
    return result;
}

void svcSleepThread(int64_t nanoseconds)
{
    assert(nanoseconds == INT64_C(10000000));
    ++sleeps;
}

int fsdevCommitDevice(const char *name) { (void)name; return 0; }

static void reset(void)
{
    (void)remove(path);
    opens = writes = closes = sleeps = open_failures = 0;
    open_error = EBUSY;
    write_failure = close_failure = false;
}

static void contents(const char *expected)
{
    char data[128] = {0};
    FILE *file = fopen(path, "rb");
    assert(file);
    size_t got = fread(data, 1, sizeof(data) - 1, file);
    assert(!ferror(file) && fclose(file) == 0);
    assert(got == strlen(expected) && strcmp(data, expected) == 0);
}

int main(void)
{
    PtcFsStorage fs;
    ptc_fs_storage_init(&fs);
    PtcStorage *storage = ptc_fs_storage_as_storage(&fs);
    const int temporary[] = {EBUSY, EACCES, EAGAIN};
    for (unsigned i = 0; i < sizeof(temporary) / sizeof(temporary[0]); ++i) {
        reset(); open_failures = 2; open_error = temporary[i];
        assert(storage->vtable->append_line(storage, path, "one"));
        assert(opens == 3 && sleeps == 2 && writes == 1 && closes == 1);
        contents("one\n");
    }
    reset(); open_failures = 100;
    assert(!storage->vtable->append_line(storage, path, "one"));
    assert(opens == 51 && sleeps == 50 && writes == 0 && closes == 0);
    reset(); open_failures = 1; open_error = EIO;
    assert(!storage->vtable->append_line(storage, path, "one"));
    assert(opens == 1 && sleeps == 0 && writes == 0 && closes == 0);
    reset(); write_failure = true;
    assert(!storage->vtable->append_line(storage, path, "one"));
    assert(opens == 1 && writes == 1 && closes == 1 && sleeps == 0);
    contents("partial"); /* Never replay a possibly partial write. */
    reset(); close_failure = true;
    assert(!storage->vtable->append_line(storage, path, "one"));
    assert(opens == 1 && writes == 1 && closes == 1 && sleeps == 0);
    contents("one\n"); /* A failed close may already have persisted the row. */
    (void)remove(path);
    puts("PASS append open contention bounds and no replay after write/close failure");
    return 0;
}
