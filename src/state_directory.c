#define _GNU_SOURCE
#include "state_directory.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static bool create_directory(const char *path)
{
    return !mkdir(path, 0700) || errno == EEXIST;
}

static bool usable_directory(const char *path, bool require_private)
{
    struct stat status;
    mode_t forbidden = require_private ? 0077 : 0022;

    return !lstat(path, &status) && S_ISDIR(status.st_mode) &&
           status.st_uid == geteuid() && !(status.st_mode & forbidden);
}

bool frame_pacer_state_directory(char *output, size_t size,
                                 bool require_private)
{
    const char *state = getenv("XDG_STATE_HOME");
    char local[1024], fallback[1024];
    int written;

    if (!output || !size)
        return false;
    output[0] = '\0';
    if (!state || !*state) {
        const char *home = getenv("HOME");

        if (!home || !*home)
            return false;
        written = snprintf(local, sizeof(local), "%s/.local", home);
        if (written < 0 || (size_t)written >= sizeof(local) ||
            !create_directory(local))
            return false;
        written = snprintf(fallback, sizeof(fallback), "%s/state", local);
        if (written < 0 || (size_t)written >= sizeof(fallback) ||
            !create_directory(fallback))
            return false;
        state = fallback;
    }
    written = snprintf(output, size, "%s/frame-pacer", state);
    if (written < 0 || (size_t)written >= size ||
        !create_directory(output) ||
        !usable_directory(output, require_private)) {
        output[0] = '\0';
        return false;
    }
    return true;
}
