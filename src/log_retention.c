#define _GNU_SOURCE
#include "log_retention.h"

#include <dirent.h>
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FRAME_PACER_LOG_RETENTION 10

struct log_candidate {
    char name[NAME_MAX + 1];
    struct timespec modified;
};

bool frame_pacer_log_enabled(void)
{
    const char *value = getenv("FRAME_PACER_LOG");

    return value && !strcmp(value, "1");
}

static bool matches_log_name(const char *name, const char *prefix)
{
    size_t name_length = strlen(name);
    size_t prefix_length = strlen(prefix);
    size_t index;

    if (name_length <= prefix_length + sizeof(".log") - 1 ||
        strncmp(name, prefix, prefix_length) ||
        strcmp(name + name_length - (sizeof(".log") - 1), ".log"))
        return false;
    for (index = prefix_length; index < name_length - (sizeof(".log") - 1);
         index++)
        if (!isdigit((unsigned char)name[index]))
            return false;
    return true;
}

static int newer_than(const struct log_candidate *left,
                      const struct log_candidate *right)
{
    if (left->modified.tv_sec != right->modified.tv_sec)
        return left->modified.tv_sec > right->modified.tv_sec;
    if (left->modified.tv_nsec != right->modified.tv_nsec)
        return left->modified.tv_nsec > right->modified.tv_nsec;
    return strcmp(left->name, right->name) > 0;
}

static bool is_retained(const char *name, const struct log_candidate *retained,
                        size_t retained_count)
{
    size_t index;

    for (index = 0; index < retained_count; index++)
        if (!strcmp(name, retained[index].name))
            return true;
    return false;
}

static bool regular_user_file(int directory_fd, const char *name)
{
    struct stat status;

    return !fstatat(directory_fd, name, &status, AT_SYMLINK_NOFOLLOW) &&
        S_ISREG(status.st_mode) && status.st_uid == geteuid();
}

void frame_pacer_log_retention_prune(const char *directory, const char *prefix)
{
    struct log_candidate retained[FRAME_PACER_LOG_RETENTION];
    struct flock lock = { .l_type = F_WRLCK, .l_whence = SEEK_SET };
    DIR *stream;
    struct dirent *entry;
    int directory_fd, lock_fd;
    size_t retained_count = 0;

    stream = opendir(directory);
    if (!stream)
        return;
    directory_fd = dirfd(stream);
    lock_fd = openat(directory_fd, ".frame-pacer-log-retention.lock",
                     O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock_fd < 0 || !regular_user_file(directory_fd,
                                           ".frame-pacer-log-retention.lock") ||
        fcntl(lock_fd, F_SETLKW, &lock)) {
        if (lock_fd >= 0)
            (void)close(lock_fd);
        (void)closedir(stream);
        return;
    }

    while ((entry = readdir(stream))) {
        struct stat status;
        struct log_candidate candidate;
        size_t index;

        if (!matches_log_name(entry->d_name, prefix) ||
            fstatat(directory_fd, entry->d_name, &status, AT_SYMLINK_NOFOLLOW) ||
            !S_ISREG(status.st_mode) || status.st_uid != geteuid())
            continue;
        if (snprintf(candidate.name, sizeof(candidate.name), "%s", entry->d_name) >=
            (int)sizeof(candidate.name))
            continue;
        candidate.modified = status.st_mtim;
        for (index = 0; index < retained_count; index++)
            if (newer_than(&candidate, &retained[index]))
                break;
        if (index == FRAME_PACER_LOG_RETENTION)
            continue;
        if (retained_count < FRAME_PACER_LOG_RETENTION)
            retained_count++;
        memmove(&retained[index + 1], &retained[index],
                (retained_count - index - 1) * sizeof(retained[0]));
        retained[index] = candidate;
    }

    rewinddir(stream);
    while ((entry = readdir(stream))) {
        if (matches_log_name(entry->d_name, prefix) &&
            regular_user_file(directory_fd, entry->d_name) &&
            !is_retained(entry->d_name, retained, retained_count))
            (void)unlinkat(directory_fd, entry->d_name, 0);
    }
    lock.l_type = F_UNLCK;
    (void)fcntl(lock_fd, F_SETLK, &lock);
    (void)close(lock_fd);
    (void)closedir(stream);
}
