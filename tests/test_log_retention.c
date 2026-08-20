#define _GNU_SOURCE
#include "log_retention.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void create_log(const char *directory, const char *name, time_t second)
{
    char path[1024];
    struct timespec times[2] = { { .tv_sec = second }, { .tv_sec = second } };
    int fd;

    assert(snprintf(path, sizeof(path), "%s/%s", directory, name) <
           (int)sizeof(path));
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(fd >= 0);
    assert(!close(fd));
    assert(!utimensat(AT_FDCWD, path, times, 0));
}

int main(void)
{
    char directory[] = "/tmp/frame-pacer-log-retention.XXXXXX";
    char name[1024];
    unsigned int index;

    assert(!unsetenv("FRAME_PACER_LOG"));
    assert(!frame_pacer_log_enabled());
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
        create_log(directory, name, (time_t)(100 + index));
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
    assert(snprintf(name, sizeof(name), "%s/frame-pacer-gl-keep.log", directory) <
           (int)sizeof(name));
    assert(!access(name, F_OK));
    for (index = 2; index < 12; index++) {
        assert(snprintf(name, sizeof(name), "%s/frame-pacer-%u.log", directory,
                        index) < (int)sizeof(name));
        assert(!unlink(name));
    }
    assert(snprintf(name, sizeof(name), "%s/frame-pacer-gl-keep.log", directory) <
           (int)sizeof(name));
    assert(!unlink(name));
    assert(snprintf(name, sizeof(name), "%s/.frame-pacer-log-retention.lock",
                    directory) < (int)sizeof(name));
    assert(!unlink(name));
    assert(!rmdir(directory));
    return 0;
}
