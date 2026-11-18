#undef NDEBUG
#include <assert.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../src/runtime.h"
#include "../src/runtime_savedata_types.h"

typedef int32_t (ABI *Mount)(const OslSaveMount2 *, OslSaveMountResult *);
typedef int32_t (ABI *Unmount)(const OslSaveMountPoint *);
static Mount mount_slot;
static Unmount unmount_slot;
static const char guest_file[] = "/savedata0/slot.dat";

static OslSaveMountPoint mount_mode(unsigned mode) {
    OslSaveDirName name = {"DATA"};
    OslSaveMount2 request = {.user=1, .dir=&name, .blocks=16, .mode=mode};
    OslSaveMountResult result = {0};
    assert(mount_slot(&request, &result) == 0);
    return result.point;
}

static void write_guest(const char *bytes) {
    int fd = (int)runtime_file_open(guest_file, 0x602, 0600);
    assert(fd >= 3);
    assert(runtime_file_write(fd, bytes, strlen(bytes)) == (int64_t)strlen(bytes));
    assert(runtime_file_close(fd) == 0);
}

static void check_guest(const char *expected) {
    char bytes[64] = {0};
    int fd = (int)runtime_file_open(guest_file, 0, 0);
    assert(fd >= 3);
    assert(runtime_file_read(fd, bytes, sizeof(bytes)) == (int64_t)strlen(expected));
    assert(strcmp(bytes, expected) == 0 && runtime_file_close(fd) == 0);
}

static void check_host(const char *path, const char *expected) {
    char bytes[64] = {0};
    FILE *file = fopen(path, "rb");
    assert(file && fread(bytes, 1, sizeof(bytes), file) == strlen(expected));
    assert(strcmp(bytes, expected) == 0 && fclose(file) == 0);
}

static int remove_entry(const char *path, const struct stat *st, int type, struct FTW *ftw) {
    (void)st; (void)type; (void)ftw;
    return remove(path);
}

int main(void) {
    char root[] = "/tmp/odin-save-mac-XXXXXX";
    assert(mkdtemp(root));
    char host[512], slot[512], previous[512];
    snprintf(slot, sizeof(slot), "%s/savedata/1/TEST/DATA", root);
    snprintf(host, sizeof(host), "%s/slot.dat", slot);
    snprintf(previous, sizeof(previous), "%s/savedata/1/TEST/.osl-write-DATA/previous", root);
    runtime_file_configure(root, root);
    runtime_savedata_configure("TEST");
    int32_t (ABI *initialize)(const void *) = (void *)runtime_savedata_resolve("sceSaveDataInitialize");
    mount_slot = (Mount)runtime_savedata_resolve("sceSaveDataMount2");
    unmount_slot = (Unmount)runtime_savedata_resolve("sceSaveDataUmount");
    assert(initialize && mount_slot && unmount_slot && !initialize(NULL));

    OslSaveMountPoint point = mount_mode(2 | 32);
    write_guest("first complete save");
    check_guest("first complete save");
    assert(unmount_slot(&point) == 0);
    check_host(host, "first complete save");

    pid_t writer = fork();
    assert(writer >= 0);
    if (writer == 0) {
        mount_mode(2);
        write_guest("partial");
        check_guest("partial");
        _exit(91); // Process loss after a successful close and before save unmount.
    }
    int status;
    assert(waitpid(writer, &status, 0) == writer && WIFEXITED(status) && WEXITSTATUS(status) == 91);
    check_host(host, "first complete save");
    point = mount_mode(1);
    check_guest("first complete save");
    assert(unmount_slot(&point) == 0);

    struct rlimit original;
    assert(getrlimit(RLIMIT_FSIZE, &original) == 0);
    struct rlimit cap = original;
    cap.rlim_cur = 4;
    assert(signal(SIGXFSZ, SIG_IGN) != SIG_ERR);
    const char next[] = "second complete save";
    for (int retry = 0; retry < 2; ++retry) {
        point = mount_mode(2);
        int fd = (int)runtime_file_open(guest_file, 0x602, 0600);
        assert(fd >= 3 && setrlimit(RLIMIT_FSIZE, &cap) == 0);
        assert(runtime_file_write(fd, next, sizeof(next)-1) == 4);
        assert(setrlimit(RLIMIT_FSIZE, &original) == 0);
        if (retry) assert(runtime_file_write(fd, next+4, sizeof(next)-5) == (int64_t)sizeof(next)-5);
        assert(runtime_file_close(fd) == 0);
        if (!retry) {
            assert(unmount_slot(&point) != 0);
            check_host(host, "first complete save");
        }
    }
    check_guest("second complete save");
    check_host(host, "first complete save");
    assert(unmount_slot(&point) == 0);
    check_host(host, "second complete save");

    // Cold recovery at the interruption point between the two fallback renames.
    assert(rename(slot, previous) == 0);
    point = mount_mode(1);
    check_guest("second complete save");
    assert(unmount_slot(&point) == 0);
    check_host(host, "second complete save");
    assert(nftw(root, remove_entry, 16, FTW_PHYS | FTW_DEPTH) == 0);
    puts("Mac save transaction passed create, interrupted overwrite, retry and recovery");
}
