#define _GNU_SOURCE
#include "state_directory.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    char home[] = "/tmp/frame-pacer-state-home-XXXXXX";
    char xdg[] = "/tmp/frame-pacer-state-xdg-XXXXXX";
    char output[1024], expected[1024], target[1024];
    struct stat status;

    assert(!frame_pacer_state_directory(0, 0, false));
    assert(!frame_pacer_state_directory(output, 0, false));

    assert(mkdtemp(home));
    assert(!unsetenv("XDG_STATE_HOME"));
    assert(!setenv("HOME", home, 1));
    assert(frame_pacer_state_directory(output, sizeof(output), true));
    assert(snprintf(expected, sizeof(expected),
                    "%s/.local/state/frame-pacer", home) > 0);
    assert(!strcmp(output, expected));
    assert(!lstat(output, &status) && S_ISDIR(status.st_mode) &&
           !(status.st_mode & 0077));
    assert(!rmdir(output));
    assert(snprintf(expected, sizeof(expected), "%s/.local/state", home) > 0);
    assert(!rmdir(expected));
    assert(snprintf(expected, sizeof(expected), "%s/.local", home) > 0);
    assert(!rmdir(expected));
    assert(!rmdir(home));

    assert(mkdtemp(xdg));
    assert(!setenv("XDG_STATE_HOME", xdg, 1));
    assert(frame_pacer_state_directory(output, sizeof(output), false));
    assert(!chmod(output, 0755));
    assert(frame_pacer_state_directory(expected, sizeof(expected), false));
    assert(!frame_pacer_state_directory(expected, sizeof(expected), true));
    assert(!chmod(output, 0700));
    assert(!rmdir(output));

    assert(snprintf(target, sizeof(target), "%s/target", xdg) > 0);
    assert(!mkdir(target, 0700));
    assert(snprintf(output, sizeof(output), "%s/frame-pacer", xdg) > 0);
    assert(!symlink(target, output));
    assert(!frame_pacer_state_directory(expected, sizeof(expected), false));
    assert(!unlink(output));
    assert(!rmdir(target));
    assert(!rmdir(xdg));
    assert(!unsetenv("XDG_STATE_HOME"));
    return 0;
}
