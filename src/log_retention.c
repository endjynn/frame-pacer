#define _GNU_SOURCE
#include "log_retention.h"
#include "state_directory.h"

#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FRAME_PACER_LOG_RETENTION 10
#ifndef FRAME_PACER_LOG_LIMIT
#define FRAME_PACER_LOG_LIMIT (UINT64_C(64) * 1024 * 1024)
#endif

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

static void prune_logs(const char *directory, const char *prefix,
                       const char *protected_name)
{
    struct log_candidate retained[FRAME_PACER_LOG_RETENTION];
    struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET};
    DIR *stream;
    struct dirent *entry;
    int directory_fd, lock_fd;
    size_t retained_count = 0;
    size_t retention_limit = FRAME_PACER_LOG_RETENTION;

    stream = opendir(directory);
    if (!stream)
        return;
    directory_fd = dirfd(stream);
    if (directory_fd < 0) {
        (void)closedir(stream);
        return;
    }
    lock_fd = openat(directory_fd, ".frame-pacer-log-retention.lock",
                     O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock_fd < 0 ||
        !regular_user_file(directory_fd, ".frame-pacer-log-retention.lock") ||
        fcntl(lock_fd, F_SETLKW, &lock)) {
        if (lock_fd >= 0)
            (void)close(lock_fd);
        (void)closedir(stream);
        return;
    }
    if (protected_name && matches_log_name(protected_name, prefix) &&
        regular_user_file(directory_fd, protected_name))
        --retention_limit;

    while ((entry = readdir(stream))) {
        struct stat status;
        struct log_candidate candidate;
        size_t index;

        if ((protected_name && !strcmp(entry->d_name, protected_name)) ||
            !matches_log_name(entry->d_name, prefix) ||
            fstatat(directory_fd, entry->d_name, &status,
                    AT_SYMLINK_NOFOLLOW) ||
            !S_ISREG(status.st_mode) || status.st_uid != geteuid())
            continue;
        {
            int written = snprintf(candidate.name, sizeof(candidate.name), "%s",
                                   entry->d_name);

            if (written < 0 || (size_t)written >= sizeof(candidate.name))
                continue;
        }
        candidate.modified = status.st_mtim;
        for (index = 0; index < retained_count; index++)
            if (newer_than(&candidate, &retained[index]))
                break;
        if (index == retention_limit)
            continue;
        if (retained_count < retention_limit)
            retained_count++;
        if (index + 1 < retained_count)
            memmove(&retained[index + 1], &retained[index],
                    (retained_count - index - 1) * sizeof(retained[0]));
        retained[index] = candidate;
    }

    rewinddir(stream);
    while ((entry = readdir(stream))) {
        if (matches_log_name(entry->d_name, prefix) &&
            regular_user_file(directory_fd, entry->d_name) &&
            (!protected_name || strcmp(entry->d_name, protected_name)) &&
            !is_retained(entry->d_name, retained, retained_count))
            (void)unlinkat(directory_fd, entry->d_name, 0);
    }
    lock.l_type = F_UNLCK;
    (void)fcntl(lock_fd, F_SETLK, &lock);
    (void)close(lock_fd);
    (void)closedir(stream);
}

void frame_pacer_log_retention_prune(const char *directory, const char *prefix)
{
    prune_logs(directory, prefix, 0);
}

static void write_locked(struct frame_pacer_runtime_log *log, int fd,
                         char *buffer, size_t bytes)
{
    static const char cap[] =
        "frame-pacer: log cap reached; pacing continues\n";
    size_t offset = 0;
    size_t remaining;

    if (log->capped || log->bytes >= FRAME_PACER_LOG_LIMIT) {
        log->capped = true;
        return;
    }
    remaining = (size_t)(FRAME_PACER_LOG_LIMIT - log->bytes);
    if (bytes > remaining || sizeof(cap) - 1 > remaining - bytes) {
        bytes = sizeof(cap) - 1 < remaining ? sizeof(cap) - 1 : remaining;
        memcpy(buffer, cap, bytes);
        log->capped = true;
    }
    while (offset < bytes) {
        ssize_t written = write(fd, buffer + offset, bytes - offset);

        if (written > 0) {
            offset += (size_t)written;
            log->bytes += (uint64_t)written;
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
}

bool frame_pacer_runtime_log_activate(struct frame_pacer_runtime_log *log,
                                      const char *filename_prefix,
                                      const char *startup_message)
{
    char directory[1100], path[1200];
    char startup[1024];
    struct stat status;
    size_t capacity, bytes;
    int fd, length, written;
    bool active = false;

    if (!log || !filename_prefix || !*filename_prefix || !startup_message ||
        !*startup_message || !log->message_capacity ||
        !frame_pacer_log_enabled())
        return false;
    (void)pthread_mutex_lock(&log->mutex);
    if (atomic_load_explicit(&log->fd, memory_order_relaxed) >= 0) {
        active = true;
        goto done;
    }
    if (!frame_pacer_state_directory(directory, sizeof(directory), false))
        goto done;
    written = snprintf(path, sizeof(path), "%s/%s%ld.log", directory,
                       filename_prefix, (long)getpid());
    if (written < 0 || (size_t)written >= sizeof(path))
        goto done;

    fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW,
              0600);
    if (fd < 0 || fstat(fd, &status) || !S_ISREG(status.st_mode) ||
        status.st_uid != geteuid() || status.st_nlink != 1 ||
        fchmod(fd, 0600)) {
        if (fd >= 0)
            (void)close(fd);
        goto done;
    }
    log->bytes = status.st_size > 0 ? (uint64_t)status.st_size : 0;
    log->capped = log->bytes >= FRAME_PACER_LOG_LIMIT;
    capacity = log->message_capacity < sizeof(startup) ? log->message_capacity
                                                       : sizeof(startup);
    length = snprintf(startup, capacity, "%s", startup_message);
    if (length >= 0) {
        bytes = (size_t)length < capacity ? (size_t)length : capacity - 1;
        write_locked(log, fd, startup, bytes);
    }
    prune_logs(directory, filename_prefix, strrchr(path, '/') + 1);
    atomic_store_explicit(&log->fd, fd, memory_order_relaxed);
    active = true;
done:
    (void)pthread_mutex_unlock(&log->mutex);
    return active;
}

void frame_pacer_runtime_log_vwrite(struct frame_pacer_runtime_log *log,
                                    const char *format, va_list arguments)
{
    char buffer[1024];
    size_t capacity, bytes;
    int length;
    int fd;

    if (!log || !format ||
        atomic_load_explicit(&log->fd, memory_order_relaxed) < 0)
        return;
    (void)pthread_mutex_lock(&log->mutex);
    fd = atomic_load_explicit(&log->fd, memory_order_relaxed);
    if (fd < 0 || log->capped)
        goto done;
    capacity = log->message_capacity < sizeof(buffer) ? log->message_capacity
                                                      : sizeof(buffer);
    length = vsnprintf(buffer, capacity, format, arguments);
    if (length < 0)
        goto done;
    bytes = (size_t)length < capacity ? (size_t)length : capacity - 1;
    write_locked(log, fd, buffer, bytes);
done:
    (void)pthread_mutex_unlock(&log->mutex);
}

uint64_t frame_pacer_runtime_log_bytes(struct frame_pacer_runtime_log *log)
{
    uint64_t bytes;

    if (!log)
        return 0;
    (void)pthread_mutex_lock(&log->mutex);
    bytes = log->bytes;
    (void)pthread_mutex_unlock(&log->mutex);
    return bytes;
}

void frame_pacer_runtime_log_close(struct frame_pacer_runtime_log *log)
{
    if (!log)
        return;
    (void)pthread_mutex_lock(&log->mutex);
    {
        int fd = atomic_load_explicit(&log->fd, memory_order_relaxed);

        if (fd >= 0) {
            atomic_store_explicit(&log->fd, -1, memory_order_relaxed);
            (void)close(fd);
        }
    }
    (void)pthread_mutex_unlock(&log->mutex);
}
