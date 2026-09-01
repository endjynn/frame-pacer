#define _GNU_SOURCE
#include "pacer_limit.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <utime.h>
#include <unistd.h>

static void write_limit(const char *path, const char *value)
{
    FILE *file = fopen(path, "we");
    assert(file);
    assert(fputs(value, file) >= 0);
    assert(!fclose(file));
    assert(!chmod(path, 0600));
}

static void write_padding(FILE *file, size_t bytes)
{
    char block[4096];

    memset(block, 'x', sizeof(block));
    while (bytes) {
        size_t chunk = bytes < sizeof(block) ? bytes : sizeof(block);

        assert(fwrite(block, 1, chunk, file) == chunk);
        bytes -= chunk;
    }
}

static void write_sized_limit(const char *path, size_t bytes, uint32_t fps)
{
    char prefix[64];
    int prefix_length = snprintf(prefix, sizeof(prefix),
                                 "global_fps_limit = %u\n#", fps);
    FILE *file;

    assert(prefix_length > 0 && (size_t)prefix_length <= bytes);
    file = fopen(path, "we");
    assert(file);
    assert(fwrite(prefix, 1, (size_t)prefix_length, file) ==
           (size_t)prefix_length);
    write_padding(file, bytes - (size_t)prefix_length);
    assert(!fclose(file));
    assert(!chmod(path, 0600));
}

static void write_large_limit(const char *path, size_t rules)
{
    FILE *file = fopen(path, "we");
    long length;

    assert(file);
    assert(fputs("global_fps_limit = 60\nhud = on\n\n", file) >= 0);
    for (size_t index = 0; index < rules; ++index)
        assert(fprintf(file, "[Game %zu]\nexecutable = \"game-%zu.exe\"\n"
                             "fps_limit = 60\nthread_cpu_limit = off\n\n",
                       index, index) > 0);
    assert(fputs("[Test process]\nexecutable = \"test_pacer_limit\"\n"
                 "fps_limit = 77\nthread_cpu_limit = off\n", file) >= 0);
    length = ftell(file);
    assert(length > 0 && (unsigned long)length < FRAME_PACER_CONFIG_MAX_BYTES);
    assert(!fclose(file));
    assert(!chmod(path, 0600));
}

static void fps_range_boundaries(void)
{
    char directory[] = "/tmp/frame-pacer-limit-range.XXXXXX";
    char state[1200], path[1200];
    struct frame_pacer_limit limit;
    bool changed;

    assert(mkdtemp(directory));
    assert(snprintf(state, sizeof(state), "%s/frame-pacer", directory) > 0);
    assert(!mkdir(state, 0700));
    assert(snprintf(path, sizeof(path), "%s/frame-pacer.conf", state) > 0);
    assert(!setenv("XDG_CONFIG_HOME", directory, 1));
    frame_pacer_limit_init(&limit);
    write_limit(path, "global_fps_limit = 999\n");
    assert(frame_pacer_limit_poll(&limit, 1, &changed) == 999 && changed);
    write_limit(path, "global_fps_limit = 1000\n");
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS + 2,
                                  &changed) == FRAME_PACER_FPS_LIMIT_OFF &&
           changed);
    frame_pacer_limit_destroy(&limit);
    assert(!unlink(path));
    assert(!rmdir(state));
    assert(!rmdir(directory));
}

static void same_timestamp_reload_behavior(void)
{
    char directory[] = "/tmp/frame-pacer-limit-timestamp.XXXXXX";
    char state[1200], path[1200];
    const struct utimbuf timestamp = {.actime = 1000000000, .modtime = 1000000000};
    struct frame_pacer_limit limit;
    bool changed;

    assert(mkdtemp(directory));
    assert(snprintf(state, sizeof(state), "%s/frame-pacer", directory) > 0);
    assert(!mkdir(state, 0700));
    assert(snprintf(path, sizeof(path), "%s/frame-pacer.conf", state) > 0);
    assert(!setenv("XDG_CONFIG_HOME", directory, 1));
    frame_pacer_limit_init(&limit);

    write_limit(path, "global_fps_limit = 60\nhud = on \n");
    assert(!utime(path, &timestamp));
    assert(frame_pacer_limit_poll(&limit, 1, &changed) == 60 && changed);
    assert(frame_pacer_limit_hud_enabled(&limit));

    write_limit(path, "global_fps_limit = 45\nhud = off\n");
    assert(!utime(path, &timestamp));
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS + 2,
                                  &changed) == 45 && changed);
    assert(!frame_pacer_limit_hud_enabled(&limit));

    frame_pacer_limit_destroy(&limit);
    assert(!unlink(path));
    assert(!rmdir(state));
    assert(!rmdir(directory));
}

static void global_off_behavior(void)
{
    char directory[] = "/tmp/frame-pacer-limit-off.XXXXXX";
    char state[1200], path[1200];
    struct frame_pacer_limit limit;
    bool changed;
    bool quota_enabled;

    assert(mkdtemp(directory));
    assert(snprintf(state, sizeof(state), "%s/frame-pacer", directory) > 0);
    assert(!mkdir(state, 0700));
    assert(snprintf(path, sizeof(path), "%s/frame-pacer.conf", state) > 0);
    assert(!setenv("XDG_CONFIG_HOME", directory, 1));
    frame_pacer_limit_init(&limit);

    write_limit(path, "global_fps_limit = off\n");
    assert(frame_pacer_limit_poll(&limit, 1, &changed) == FRAME_PACER_FPS_LIMIT_OFF &&
           !changed);
    assert(frame_pacer_limit_hud_enabled(&limit));

    write_limit(path, "[Test process]\n"
                      "executable = \"test_pacer_limit\"\n"
                      "fps_limit = 45\n"
                      "thread_cpu_limit = 50%\n");
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS + 2,
                                  &changed) == 45 && changed);
    assert(frame_pacer_limit_thread_cpu_quota(&limit, &quota_enabled) == 50 &&
           quota_enabled);

    write_limit(path, "global_fps_limit = off\n"
                      "[Other process]\n"
                      "executable = \"other.exe\"\n"
                      "fps_limit = 30\n");
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 2 + 3,
                                  &changed) == FRAME_PACER_FPS_LIMIT_OFF && changed);
    assert(!frame_pacer_limit_thread_cpu_quota(&limit, &quota_enabled) &&
           !quota_enabled);

    write_limit(path, "global_fps_limit = 30\n");
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 3 + 4,
                                  &changed) == 30 && changed);

    write_limit(path, "global_fps_limit = 30\n"
                      "[Test process]\n"
                      "executable = \"test_pacer_limit\"\n"
                      "fps_limit = off\n"
                      "thread_cpu_limit = 50%\n");
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 4 + 5,
                                  &changed) == FRAME_PACER_FPS_LIMIT_OFF && changed);
    assert(frame_pacer_limit_thread_cpu_quota(&limit, &quota_enabled) == 50 &&
           quota_enabled);

    write_limit(path, "hud = off\n");
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 5 + 6,
                                  &changed) == FRAME_PACER_FPS_LIMIT_OFF && !changed);
    assert(!frame_pacer_limit_hud_enabled(&limit));
    assert(!frame_pacer_limit_thread_cpu_quota(&limit, &quota_enabled) &&
           !quota_enabled);

    frame_pacer_limit_destroy(&limit);
    assert(!unlink(path));
    assert(!rmdir(state));
    assert(!rmdir(directory));
}

static void large_configuration_behavior(void)
{
    char directory[] = "/tmp/frame-pacer-limit-large.XXXXXX";
    char state[1200], path[1200];
    struct frame_pacer_limit limit;
    struct stat status;
    bool changed;
    bool quota_enabled;

    assert(mkdtemp(directory));
    assert(snprintf(state, sizeof(state), "%s/frame-pacer", directory) > 0);
    assert(!mkdir(state, 0700));
    assert(snprintf(path, sizeof(path), "%s/frame-pacer.conf", state) > 0);
    assert(!setenv("XDG_CONFIG_HOME", directory, 1));
    frame_pacer_limit_init(&limit);

    write_large_limit(path, 10000);
    assert(!stat(path, &status));
    assert(status.st_size > 750000 &&
           status.st_size < (off_t)FRAME_PACER_CONFIG_MAX_BYTES);
    assert(frame_pacer_limit_poll(&limit, 1, &changed) == 77 && changed);
    assert(!frame_pacer_limit_thread_cpu_quota(&limit, &quota_enabled) &&
           !quota_enabled);

    write_limit(path, "global_fps_limit = 60\n"
                      "hud = off\n"
                      "[Test process]\n"
                      "executable = \"test_pacer_limit\"\n"
                      "fps_limit = 77\n"
                      "thread_cpu_limit = 50%\n"
                      "[Incomplete rule]\n"
                      "executable = \"other.exe\"\n");
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS + 2,
                                  &changed) == FRAME_PACER_FPS_LIMIT_OFF && changed);
    assert(frame_pacer_limit_hud_enabled(&limit));
    assert(!frame_pacer_limit_thread_cpu_quota(&limit, &quota_enabled) &&
           !quota_enabled);

    write_sized_limit(path, FRAME_PACER_CONFIG_MAX_BYTES, 61);
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 2 + 3,
                                  &changed) == 61 && changed);

    write_sized_limit(path, FRAME_PACER_CONFIG_MAX_BYTES + 1U, 62);
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 3 + 4,
                                  &changed) == FRAME_PACER_FPS_LIMIT_OFF && changed);
    assert(frame_pacer_limit_hud_enabled(&limit));
    assert(!frame_pacer_limit_thread_cpu_quota(&limit, &quota_enabled) &&
           !quota_enabled);

    frame_pacer_limit_destroy(&limit);
    assert(!unlink(path));
    assert(!rmdir(state));
    assert(!rmdir(directory));
}

int main(void)
{
    char directory[] = "/tmp/frame-pacer-limit.XXXXXX";
    char config[1200], state[1200], path[1200], target[1200];
    struct frame_pacer_limit limit;
    bool changed;
    bool quota_enabled;
    bool hud_enabled;

    global_off_behavior();
    large_configuration_behavior();
    assert(mkdtemp(directory));
    assert(snprintf(config, sizeof(config), "%s/config", directory) > 0);
    assert(!mkdir(config, 0700));
    assert(snprintf(state, sizeof(state), "%s/frame-pacer", config) > 0);
    assert(!mkdir(state, 0700));
    assert(snprintf(path, sizeof(path), "%s/frame-pacer.conf", state) > 0);
    assert(!setenv("XDG_CONFIG_HOME", config, 1));
    frame_pacer_limit_init(&limit);
    assert(frame_pacer_limit_poll(&limit, 1, &changed) == FRAME_PACER_FPS_LIMIT_OFF &&
           !changed);
    assert(frame_pacer_limit_hud_enabled(&limit));
    write_limit(path, "# global cap\n"
                      "global_fps_limit = 30\n\n"
                      "[unrelated game]\n"
                      "executable = \"other-game\"\n"
                      "fps_limit = 40\n\n"
                      "[Test process]\n"
                      "fps_limit = 45\n"
                      "executable = \"test_pacer_limit\"\n");
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS + 2, &changed) == 45 &&
           changed);
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS + 3, &changed) == 45 &&
           !changed);
    assert(!frame_pacer_limit_thread_cpu_quota(&limit, &quota_enabled) && !quota_enabled);
    write_limit(path, "global_fps_limit = 30\n"
                      "hud = off\n"
                      "[Test process]\n"
                      "executable = \"test_pacer_limit\"\n"
                      "fps_limit = 45\n");
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 2 + 3, &changed) == 45 &&
           !changed);
    hud_enabled = frame_pacer_limit_hud_enabled(&limit);
    assert(!hud_enabled);
    write_limit(path, "global_fps_limit = 30\n"
                      "[Test process]\n"
                      "executable = \"test_pacer_limit\"\n"
                      "fps_limit = 45\n"
                      "thread_cpu_limit = 75%\n");
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 3 + 4, &changed) == 45 &&
           !changed);
    assert(frame_pacer_limit_hud_enabled(&limit));
    assert(frame_pacer_limit_thread_cpu_quota(&limit, &quota_enabled) == 75 && quota_enabled);
    write_limit(path, "global_fps_limit = 30\n"
                      "[Test process]\n"
                      "executable = \"test_pacer_limit\"\n"
                      "fps_limit = 45\n"
                      "thread_cpu_limit = off\n");
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 4 + 5, &changed) == 45 &&
           !changed);
    assert(!frame_pacer_limit_thread_cpu_quota(&limit, &quota_enabled) && !quota_enabled);
    write_limit(path, "global_fps_limit = 30\n"
                      "[Test process]\n"
                      "executable = \"test_pacer_limit\"\n"
                      "fps_limit = 45\n"
                      "thread_cpu_limit = 75%\n"
                      "thread_cpu_limit = off\n");
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 5 + 6, &changed) ==
           FRAME_PACER_FPS_LIMIT_OFF && changed);
    assert(snprintf(limit.executable_candidates[1], FRAME_PACER_EXECUTABLE_MAX,
                    "%s", "launcher.exe") > 0);
    limit.executable_candidate_count = 2;
    write_limit(path, "global_fps_limit = 30\n"
                      "[launcher]\nexecutable = \"launcher.exe\"\nfps_limit = 40\n"
                      "[Test process]\n"
                      "executable = \"test_pacer_limit\"\nfps_limit = 45\n");
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 6 + 7, &changed) == 45 &&
           changed);
    write_limit(path, "global_fps_limit = 30\n"
                      "[launcher]\nexecutable = \"launcher.exe\"\nfps_limit = 40\n");
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 7 + 8, &changed) == 40 &&
           changed);
    write_limit(path, "global_fps_limit = 30\n"
                      "[unrelated game]\nexecutable = \"other-game.exe\"\nfps_limit = 40\n");
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 8 + 9, &changed) == 30 &&
           changed);
    write_limit(path, "global_fps_limit = 30\n"
                      "[bad rule]\nexecutable = \"test_pacer_limit\"\nfps_limit = 45\n"
                      "[duplicate]\nexecutable = \"test_pacer_limit\"\nfps_limit = 50\n");
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 9 + 10, &changed) ==
           FRAME_PACER_FPS_LIMIT_OFF && changed);
    write_limit(path, "fps_limit = 30\n");
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 10 + 11, &changed) ==
           FRAME_PACER_FPS_LIMIT_OFF && !changed);
    write_limit(path, "global_fps_limit = 30\nthread_cpu_quota = 50%\n");
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 11 + 12, &changed) ==
           FRAME_PACER_FPS_LIMIT_OFF && !changed);
    assert(!frame_pacer_limit_thread_cpu_quota(&limit, &quota_enabled) && !quota_enabled);
    write_limit(path, "global_fps_limit = 30\n[Test process]\nexecutable = \"test_pacer_limit\"\n"
                      "fps_limit = 45\ncpu_quota = 100%\n");
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 12 + 13, &changed) ==
           FRAME_PACER_FPS_LIMIT_OFF && !changed);
    write_limit(path, "global_fps_limit = 30\n[Test process]\nexecutable = \"test_pacer_limit\"\n"
                      "fps_limit = 45\nthread_cpu_limit = 0%\n");
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 13 + 14, &changed) ==
           FRAME_PACER_FPS_LIMIT_OFF && !changed);
    write_limit(path, "global_fps_limit = 30\n[Test process]\nexecutable = \"test_pacer_limit\"\n"
                      "fps_limit = 45\nthread_cpu_limit = 101%\n");
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 14 + 15, &changed) ==
           FRAME_PACER_FPS_LIMIT_OFF && !changed);
    write_limit(path, "global_fps_limit = invalid\n");
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 15 + 16, &changed) ==
           FRAME_PACER_FPS_LIMIT_OFF && !changed);
    assert(!chmod(path, 0644));
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 16 + 17, &changed) ==
           FRAME_PACER_FPS_LIMIT_OFF && !changed);
    assert(!chmod(path, 0600));
    write_limit(path, "global_fps_limit = 30\n");
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 17 + 18,
                                  &changed) == 30 && changed);
    assert(!chmod(path, 0644));
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 18 + 19,
                                  &changed) == FRAME_PACER_FPS_LIMIT_OFF && changed);
    assert(!chmod(path, 0600));
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 19 + 20,
                                  &changed) == 30 && changed);
    assert(snprintf(target, sizeof(target), "%s/target.conf", directory) > 0);
    write_limit(target, "global_fps_limit = 40\n");
    assert(!unlink(path));
    assert(!symlink(target, path));
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 20 + 21,
                                  &changed) == FRAME_PACER_FPS_LIMIT_OFF && changed);
    assert(!unlink(path));
    assert(!link(target, path));
    assert(frame_pacer_limit_poll(&limit, FRAME_PACER_CONFIG_POLL_NS * 21 + 22,
                                  &changed) == FRAME_PACER_FPS_LIMIT_OFF && !changed);
    frame_pacer_limit_destroy(&limit);
    frame_pacer_limit_destroy(&limit);
    assert(!unlink(path));
    assert(!unlink(target));
    assert(!rmdir(state));
    assert(!rmdir(config));
    assert(!rmdir(directory));
    {
        char *oversized = malloc(2000);

        assert(oversized);
        memset(oversized, 'x', 1999);
        oversized[1999] = '\0';
        assert(!setenv("XDG_CONFIG_HOME", oversized, 1));
        frame_pacer_limit_init(&limit);
        assert(limit.initialized);
        assert(!limit.path[0]);
        assert(frame_pacer_limit_poll(&limit, 1, &changed) ==
               FRAME_PACER_FPS_LIMIT_OFF);
        frame_pacer_limit_destroy(&limit);
        free(oversized);
    }
    fps_range_boundaries();
    same_timestamp_reload_behavior();
    return 0;
}
