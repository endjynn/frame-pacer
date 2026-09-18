#define _GNU_SOURCE
#include "log_retention.h"

#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void create_log(const char *directory, const char *name, time_t second)
{
    char path[1024];
    struct timespec times[2] = {{.tv_sec = second}, {.tv_sec = second}};
    int fd;

    assert(snprintf(path, sizeof(path), "%s/%s", directory, name) <
           (int)sizeof(path));
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(fd >= 0);
    assert(!close(fd));
    assert(!utimensat(AT_FDCWD, path, times, 0));
}

static void runtime_log_write(struct frame_pacer_runtime_log *log,
                              const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    frame_pacer_runtime_log_vwrite(log, format, arguments);
    va_end(arguments);
}

static void runtime_log_lifecycle(void)
{
    struct frame_pacer_runtime_log log =
        FRAME_PACER_RUNTIME_LOG_INITIALIZER(16);
    char directory[] = "/tmp/frame-pacer-runtime-log.XXXXXX";
    char log_directory[1024], path[1024], text[512] = {0};
    FILE *file;

    assert(mkdtemp(directory));
    assert(!setenv("XDG_STATE_HOME", directory, 1));
    assert(!setenv("FRAME_PACER_LOG", "1", 1));
    assert(frame_pacer_runtime_log_activate(&log, "frame-pacer-test-", "s\n"));
    runtime_log_write(&log, "payload\n");
    size_t bytes = (size_t)frame_pacer_runtime_log_bytes(&log);
    assert(bytes > 2 + 8 && bytes < sizeof(text));
    frame_pacer_runtime_log_close(&log);
    assert(snprintf(log_directory, sizeof(log_directory), "%s/frame-pacer",
                    directory) > 0);
    assert(snprintf(path, sizeof(path), "%s/frame-pacer-test-%ld.log",
                    log_directory, (long)getpid()) > 0);
    file = fopen(path, "re");
    assert(file);
    assert(fread(text, 1, sizeof(text), file) == bytes);
    assert(!fclose(file));
    int64_t wall, mono;
    long wall_ns, mono_ns, tid;
    int consumed = 0;
    assert(sscanf(text,
                  "[wall=%" SCNd64 ".%ld mono=%" SCNd64 ".%ld tid=%ld] %n",
                  &wall, &wall_ns, &mono, &mono_ns, &tid, &consumed) == 5);
    assert(wall > 0 && mono > 0 && tid == (long)gettid());
    assert(wall_ns >= 0 && wall_ns < 1000000000L);
    assert(mono_ns >= 0 && mono_ns < 1000000000L);
    assert(!strncmp(text + consumed, "s\n[wall=", 8));
    assert(strstr(text + consumed, "] payload\n"));
    assert(!unlink(path));
    assert(snprintf(path, sizeof(path), "%s/.frame-pacer-log-retention.lock",
                    log_directory) > 0);
    assert(!unlink(path));
    assert(!rmdir(log_directory));
    assert(!rmdir(directory));
    assert(!unsetenv("XDG_STATE_HOME"));
}

static void runtime_log_never_exceeds_cap(void)
{
    static const char cap[] =
        "frame-pacer: log cap reached; pacing continues\n";
    struct frame_pacer_runtime_log log =
        FRAME_PACER_RUNTIME_LOG_INITIALIZER(64);
    char directory[] = "/tmp/frame-pacer-runtime-cap.XXXXXX";
    char log_directory[1024], path[1024], text[1024] = {0};
    struct stat status;
    FILE *file;

    assert(mkdtemp(directory));
    assert(!setenv("XDG_STATE_HOME", directory, 1));
    assert(!setenv("FRAME_PACER_LOG", "1", 1));
    assert(frame_pacer_runtime_log_activate(&log, "frame-pacer-cap-",
                                            "startup\n"));
    for (unsigned int i = 0; i < 20; ++i)
        runtime_log_write(&log, "0123456789\n");
    assert(log.capped);
    uint64_t bytes = frame_pacer_runtime_log_bytes(&log);
    assert(bytes <= 512 && bytes >= sizeof(cap) - 1);
    runtime_log_write(&log, "must not be written");
    assert(frame_pacer_runtime_log_bytes(&log) == bytes);
    frame_pacer_runtime_log_close(&log);
    assert(snprintf(log_directory, sizeof(log_directory), "%s/frame-pacer",
                    directory) > 0);
    assert(snprintf(path, sizeof(path), "%s/frame-pacer-cap-%ld.log",
                    log_directory, (long)getpid()) > 0);
    assert(!stat(path, &status) && status.st_size <= 512);
    file = fopen(path, "re");
    assert(file);
    assert(fread(text, 1, sizeof(text), file) == (size_t)status.st_size);
    assert(!fclose(file));
    assert(strstr(text, "] startup\n"));
    assert(!memcmp(text + bytes - (sizeof(cap) - 1), cap, sizeof(cap) - 1));
    assert(!unlink(path));
    assert(snprintf(path, sizeof(path), "%s/.frame-pacer-log-retention.lock",
                    log_directory) > 0);
    assert(!unlink(path));
    assert(!rmdir(log_directory));
    assert(!rmdir(directory));
    assert(!unsetenv("XDG_STATE_HOME"));
}

static void active_log_is_always_retained(void)
{
    struct frame_pacer_runtime_log log =
        FRAME_PACER_RUNTIME_LOG_INITIALIZER(64);
    char directory[] = "/tmp/frame-pacer-active-log.XXXXXX";
    char log_directory[1024], active_name[128], path[1024];
    DIR *stream;
    struct dirent *entry;
    unsigned int index, count = 0;

    assert(mkdtemp(directory));
    assert(snprintf(log_directory, sizeof(log_directory), "%s/frame-pacer",
                    directory) > 0);
    assert(!mkdir(log_directory, 0700));
    assert(snprintf(active_name, sizeof(active_name),
                    "frame-pacer-active-%ld.log", (long)getpid()) > 0);
    create_log(log_directory, active_name, 1);
    for (index = 0; index < 10; ++index) {
        char name[128];

        assert(snprintf(name, sizeof(name), "frame-pacer-active-%u.log",
                        index + 1000) > 0);
        create_log(log_directory, name, 100 + (time_t)index);
    }
    assert(!setenv("XDG_STATE_HOME", directory, 1));
    assert(!setenv("FRAME_PACER_LOG", "1", 1));
    assert(frame_pacer_runtime_log_activate(&log, "frame-pacer-active-",
                                            "startup\n"));
    runtime_log_write(&log, "active\n");
    frame_pacer_runtime_log_close(&log);
    assert(snprintf(path, sizeof(path), "%s/%s", log_directory, active_name) >
           0);
    assert(!access(path, F_OK));
    stream = opendir(log_directory);
    assert(stream);
    while ((entry = readdir(stream)))
        if (!strncmp(entry->d_name, "frame-pacer-active-", 19) &&
            strstr(entry->d_name, ".log"))
            ++count;
    assert(!closedir(stream));
    assert(count == 10);
    stream = opendir(log_directory);
    assert(stream);
    while ((entry = readdir(stream))) {
        if (entry->d_name[0] == '.')
            continue;
        assert(snprintf(path, sizeof(path), "%s/%s", log_directory,
                        entry->d_name) > 0);
        assert(!unlink(path));
    }
    assert(!closedir(stream));
    assert(snprintf(path, sizeof(path), "%s/.frame-pacer-log-retention.lock",
                    log_directory) > 0);
    assert(!unlink(path));
    assert(!rmdir(log_directory));
    assert(!rmdir(directory));
    assert(!unsetenv("XDG_STATE_HOME"));
}

struct activation_context {
    struct frame_pacer_runtime_log *log;
};

static void *activate_runtime_log(void *argument)
{
    struct activation_context *context = argument;

    assert(frame_pacer_runtime_log_activate(
        context->log, "frame-pacer-concurrent-", "startup\n"));
    return 0;
}

static void concurrent_activation_writes_one_header(void)
{
    struct frame_pacer_runtime_log log =
        FRAME_PACER_RUNTIME_LOG_INITIALIZER(64);
    struct activation_context context = {.log = &log};
    char directory[] = "/tmp/frame-pacer-concurrent-log.XXXXXX";
    char log_directory[1024], path[1024], text[512] = {0};
    pthread_t threads[16];
    FILE *file;
    size_t index;

    assert(mkdtemp(directory));
    assert(!setenv("XDG_STATE_HOME", directory, 1));
    assert(!setenv("FRAME_PACER_LOG", "1", 1));
    for (index = 0; index < sizeof(threads) / sizeof(threads[0]); ++index)
        assert(!pthread_create(&threads[index], 0, activate_runtime_log,
                               &context));
    for (index = 0; index < sizeof(threads) / sizeof(threads[0]); ++index)
        assert(!pthread_join(threads[index], 0));
    runtime_log_write(&log, "payload\n");
    frame_pacer_runtime_log_close(&log);

    assert(snprintf(log_directory, sizeof(log_directory), "%s/frame-pacer",
                    directory) > 0);
    assert(snprintf(path, sizeof(path), "%s/frame-pacer-concurrent-%ld.log",
                    log_directory, (long)getpid()) > 0);
    file = fopen(path, "re");
    assert(file);
    assert(fread(text, 1, sizeof(text) - 1, file) > 16);
    assert(!fclose(file));
    char *header = strstr(text, "] startup\n");
    assert(header && !strstr(header + 1, "] startup\n"));
    assert(strstr(header, "] payload\n"));
    assert(!unlink(path));
    assert(snprintf(path, sizeof(path), "%s/.frame-pacer-log-retention.lock",
                    log_directory) > 0);
    assert(!unlink(path));
    assert(!rmdir(log_directory));
    assert(!rmdir(directory));
    assert(!unsetenv("XDG_STATE_HOME"));
}

static void inactive_log_creates_nothing(void)
{
    struct frame_pacer_runtime_log log =
        FRAME_PACER_RUNTIME_LOG_INITIALIZER(64);
    char directory[] = "/tmp/frame-pacer-inactive-log.XXXXXX";
    char path[1024];

    assert(mkdtemp(directory));
    assert(!setenv("XDG_STATE_HOME", directory, 1));
    assert(!unsetenv("FRAME_PACER_LOG"));
    assert(!frame_pacer_runtime_log_activate(&log, "frame-pacer-inactive-",
                                             "startup\n"));
    frame_pacer_runtime_log_close(&log);
    assert(snprintf(path, sizeof(path), "%s/frame-pacer", directory) > 0);
    assert(access(path, F_OK) == -1);
    assert(!rmdir(directory));
    assert(!unsetenv("XDG_STATE_HOME"));
}

int main(void)
{
    char directory[] = "/tmp/frame-pacer-log-retention.XXXXXX";
    char name[1024];
    unsigned int index;

    assert(!unsetenv("FRAME_PACER_LOG"));
    assert(!frame_pacer_log_enabled());
    {
        struct frame_pacer_runtime_log inactive =
            FRAME_PACER_RUNTIME_LOG_INITIALIZER(32);

        assert(!frame_pacer_runtime_log_active(&inactive));
        atomic_store_explicit(&inactive.fd, 7, memory_order_relaxed);
        assert(frame_pacer_runtime_log_active(&inactive));
        atomic_store_explicit(&inactive.fd, -1, memory_order_relaxed);
        assert(!frame_pacer_runtime_log_active(&inactive));
    }
    assert(!setenv("FRAME_PACER_LOG", "0", 1));
    assert(!frame_pacer_log_enabled());
    assert(!setenv("FRAME_PACER_LOG", "1", 1));
    assert(frame_pacer_log_enabled());
    assert(!setenv("FRAME_PACER_LOG", "yes", 1));
    assert(!frame_pacer_log_enabled());
    assert(mkdtemp(directory));
    for (index = 0; index < 12; index++) {
        assert(snprintf(name, sizeof(name), "frame-pacer-%u.log", index) <
               (int)sizeof(name));
        create_log(directory, name, 100 + (time_t)index);
    }
    create_log(directory, "frame-pacer-gl-keep.log", 200);
    frame_pacer_log_retention_prune(directory, "frame-pacer-");
    for (index = 0; index < 2; index++) {
        assert(snprintf(name, sizeof(name), "%s/frame-pacer-%u.log", directory,
                        index) < (int)sizeof(name));
        assert(access(name, F_OK) == -1);
    }
    for (index = 2; index < 12; index++) {
        assert(snprintf(name, sizeof(name), "%s/frame-pacer-%u.log", directory,
                        index) < (int)sizeof(name));
        assert(!access(name, F_OK));
    }
    assert(snprintf(name, sizeof(name), "%s/frame-pacer-gl-keep.log",
                    directory) < (int)sizeof(name));
    assert(!access(name, F_OK));
    for (index = 2; index < 12; index++) {
        assert(snprintf(name, sizeof(name), "%s/frame-pacer-%u.log", directory,
                        index) < (int)sizeof(name));
        assert(!unlink(name));
    }
    assert(snprintf(name, sizeof(name), "%s/frame-pacer-gl-keep.log",
                    directory) < (int)sizeof(name));
    assert(!unlink(name));
    for (index = 0; index < 3; ++index) {
        assert(snprintf(name, sizeof(name), "frame-pacer-partial-%u.log",
                        index) < (int)sizeof(name));
        create_log(directory, name, 300 - (time_t)index);
    }
    frame_pacer_log_retention_prune(directory, "frame-pacer-partial-");
    for (index = 0; index < 3; ++index) {
        assert(snprintf(name, sizeof(name), "%s/frame-pacer-partial-%u.log",
                        directory, index) < (int)sizeof(name));
        assert(!access(name, F_OK));
        assert(!unlink(name));
    }
    assert(snprintf(name, sizeof(name), "%s/.frame-pacer-log-retention.lock",
                    directory) < (int)sizeof(name));
    assert(!unlink(name));
    assert(!rmdir(directory));
    runtime_log_lifecycle();
    runtime_log_never_exceeds_cap();
    active_log_is_always_retained();
    concurrent_activation_writes_one_header();
    inactive_log_creates_nothing();
    return 0;
}
