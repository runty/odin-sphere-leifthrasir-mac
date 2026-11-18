/* Writable Mac mounts use a private copy until sceSaveDataUmount. Existing
 * descriptors and directory operations all see the same copy while mounted. */
#include <copyfile.h>
#include <sys/file.h>

#define OSL_DRAFT_PREFIX ".osl-write-"
typedef struct {
    int lock_fd;
    char pending[800], previous[800];
} SaveDraft;
static SaveDraft drafts[SLOTS];

static int discard_path(const char *path) {
    struct stat st;
    if (lstat(path, &st)) return errno == ENOENT ? 0 : -1;
    return remove_tree(path);
}

static void close_draft(SaveDraft *draft) {
    if (!draft->pending[0]) return;
    close(draft->lock_fd);
    memset(draft, 0, sizeof(*draft));
}

static int restore_missing_slot(const char *host, const char *previous) {
    struct stat st;
    if (!lstat(host, &st)) return 0;
    if (errno != ENOENT) return -1;
    if (lstat(previous, &st)) return errno == ENOENT ? 0 : -1;
    return rename(previous, host);
}

/* A filesystem without directory exchange uses two ordinary renames. If
 * the process stopped between them, put the old slot back before lookup. */
static int recover_draft(const char *base, const char *name) {
    char dir[700], previous[800], host[700];
    snprintf(dir, sizeof(dir), "%s/%s%s", base, OSL_DRAFT_PREFIX, name);
    snprintf(previous, sizeof(previous), "%s/previous", dir);
    snprintf(host, sizeof(host), "%s/%s", base, name);
    struct stat st;
    if (!lstat(host, &st)) return 0;
    if (errno != ENOENT) return -1;
    if (lstat(previous, &st)) return errno == ENOENT ? 0 : -1;
    int fd = open(dir, O_DIRECTORY | O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return -1;
    int result = flock(fd, LOCK_EX);
    if (!result) result = restore_missing_slot(host, previous);
    close(fd);
    return result;
}

static int recover_listed_drafts(DIR *dir, const char *base) {
    const size_t prefix = sizeof(OSL_DRAFT_PREFIX) - 1;
    for (struct dirent *entry; (entry = readdir(dir));) {
        if (strncmp(entry->d_name, OSL_DRAFT_PREFIX, prefix)) continue;
        const char *name = entry->d_name + prefix;
        if (valid_name(name, 32) && recover_draft(base, name)) return -1;
    }
    rewinddir(dir);
    return 0;
}

static int lock_draft(const char *dir, int *owner) {
    if (mkdir(dir, 0700) && errno != EEXIST) return ERR_INTERNAL;
    int fd = open(dir, O_DIRECTORY | O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return ERR_INTERNAL;
    if (flock(fd, LOCK_EX | LOCK_NB)) {
        int result = errno == EWOULDBLOCK ? ERR_BAD_MOUNTED : ERR_INTERNAL;
        close(fd);
        return result;
    }
    *owner = fd;
    return 0;
}

static int begin_draft(SaveDraft *draft, const char *base, const char *name, const char *host) {
    char dir[700];
    snprintf(dir, sizeof(dir), "%s/%s%s", base, OSL_DRAFT_PREFIX, name);
    int result = lock_draft(dir, &draft->lock_fd);
    if (result) return result;
    snprintf(draft->pending, sizeof(draft->pending), "%s/pending", dir);
    snprintf(draft->previous, sizeof(draft->previous), "%s/previous", dir);
    /* host exists here, so a previous tree belongs to an already published
       fallback transaction. An unfinished pending tree was never published. */
    if (restore_missing_slot(host, draft->previous) || discard_path(draft->previous) ||
        discard_path(draft->pending) ||
        copyfile(host, draft->pending, NULL,
                 COPYFILE_ALL | COPYFILE_RECURSIVE | COPYFILE_CLONE | COPYFILE_NOFOLLOW)) {
        close_draft(draft);
        return ERR_INTERNAL;
    }
    return 0;
}

static int publish_draft(SaveDraft *draft, const char *host) {
    if (restore_missing_slot(host, draft->previous)) return -1;
    if (!renamex_np(draft->pending, host, RENAME_SWAP)) {
        discard_path(draft->pending); /* Old version. Cleanup can retry on the next mount. */
    } else {
        if (errno != ENOTSUP && errno != EINVAL && errno != ENOSYS) return -1;
        if (rename(host, draft->previous)) return -1;
        if (rename(draft->pending, host)) {
            rename(draft->previous, host); /* Startup recovery also knows this name. */
            return -1;
        }
        discard_path(draft->previous);
    }
    close_draft(draft);
    return 0;
}
