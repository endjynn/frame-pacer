#define _GNU_SOURCE
#include "thread_cpu_external.h"

#include "state_directory.h"
#include "thread_cpu_protocol.h"
#include "thread_cpu_systemd.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

static const char helper_anchor;

static bool helper_executable(const char *path)
{
    struct stat status;

    return !lstat(path, &status) && S_ISREG(status.st_mode) &&
           (status.st_uid == geteuid() || status.st_uid == 0) &&
           !(status.st_mode & 0022) && (status.st_mode & 0111) &&
           !access(path, X_OK);
}

static bool helper_path_from_library(const char *library, char *out,
                                     size_t size)
{
    static const char helper[] = "/frame-pacer-thread-cpu-controller";
    char absolute[PATH_MAX], directory[PATH_MAX];
    const char *filename;
    char *slash;
    unsigned int depth;

    if (!library || !*library || !out || !size)
        return false;
    if (library[0] != '/') {
        if (!realpath(library, absolute))
            return false;
        library = absolute;
    }
    filename = strrchr(library, '/');
    if (!filename || filename == library ||
        (size_t)(filename - library) >= sizeof(directory))
        return false;
    memcpy(directory, library, (size_t)(filename - library));
    directory[filename - library] = '\0';

    /*
     * Installed GL shims can live below ${LIB}, which is either lib, lib32,
     * or a nested multiarch directory. Search only that bounded runtime tree;
     * ownership and mode checks still apply to every candidate.
     */
    for (depth = 0; depth < 3; ++depth) {
        int written = snprintf(out, size, "%s%s", directory, helper);

        if (written > 0 && (size_t)written < size && helper_executable(out))
            return true;
        slash = strrchr(directory, '/');
        if (!slash || slash == directory)
            break;
        *slash = '\0';
    }
    return false;
}

static bool helper_path(char *out, size_t size)
{
    Dl_info info;
    char executable[PATH_MAX];
    ssize_t length;

#ifdef FRAME_PACER_TEST
    {
        const char *override = getenv("FRAME_PACER_TEST_THREAD_CPU_HELPER");
        int written;

        if (override && *override && helper_executable(override)) {
            written = snprintf(out, size, "%s", override);
            return written >= 0 && (size_t)written < size;
        }
    }
#endif

    if (dladdr(&helper_anchor, &info) && info.dli_fname &&
        helper_path_from_library(info.dli_fname, out, size))
        return true;
    length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length < 0 || (size_t)length >= sizeof(executable) - 1)
        return false;
    executable[length] = '\0';
    return helper_path_from_library(executable, out, size);
}

static bool write_all(int fd, const char *text, size_t size)
{
    size_t offset = 0;

    while (offset < size) {
        ssize_t written = write(fd, text + offset, size - offset);

        if (written > 0)
            offset += (size_t)written;
        else if (written < 0 && errno == EINTR)
            continue;
        else
            return false;
    }
    return true;
}

bool frame_pacer_thread_cpu_external_write(const char *path, bool enabled,
                                           uint32_t quota)
{
    char text[32], temporary[1240];
    int fd, temporary_length;
    size_t text_length;
    bool ok;

    if (!path || !frame_pacer_thread_cpu_format_state(text, sizeof(text),
                                                      enabled, quota))
        return false;
    text_length = strlen(text);
    temporary_length = snprintf(temporary, sizeof(temporary), "%s.tmp-%ld",
                                path, (long)getpid());
    if (temporary_length < 0 || (size_t)temporary_length >= sizeof(temporary))
        return false;
    (void)unlink(temporary);
    fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
              0600);
    if (fd < 0)
        return false;
    ok = write_all(fd, text, text_length) && !fchmod(fd, 0600) && !fsync(fd);
    if (close(fd))
        ok = false;
    if (ok)
        ok = !rename(temporary, path);
    if (!ok)
        (void)unlink(temporary);
    return ok;
}

static bool read_owned_file(const char *path, char *out, size_t size)
{
    struct stat before, after;
    size_t offset = 0;
    int fd;

    if (!path || !out || !size)
        return false;
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 || fstat(fd, &before) || !S_ISREG(before.st_mode) ||
        before.st_uid != geteuid() || before.st_nlink != 1) {
        if (fd >= 0)
            (void)close(fd);
        return false;
    }
    while (offset < size) {
        ssize_t bytes = read(fd, out + offset, size - offset);

        if (bytes > 0)
            offset += (size_t)bytes;
        else if (!bytes)
            break;
        else if (errno != EINTR) {
            (void)close(fd);
            return false;
        }
    }
    bool valid = fstat(fd, &after) == 0;
    if (close(fd))
        valid = false;
    if (offset >= size || !valid || before.st_dev != after.st_dev ||
        before.st_ino != after.st_ino || before.st_uid != after.st_uid ||
        before.st_mode != after.st_mode || before.st_nlink != after.st_nlink ||
        before.st_size != after.st_size ||
        before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
        before.st_mtim.tv_nsec != after.st_mtim.tv_nsec)
        return false;
    out[offset] = '\0';
    return true;
}

bool frame_pacer_thread_cpu_external_confirmed(const char *state,
                                               uint32_t quota)
{
    char status[1240], text[64];
    int written = snprintf(status, sizeof(status), "%s.status", state);

    return written >= 0 && (size_t)written < sizeof(status) &&
           read_owned_file(status, text, sizeof(text)) &&
           frame_pacer_thread_cpu_parse_confirmation(text, quota);
}

void frame_pacer_thread_cpu_external_wait_off(const char *state)
{
    char status[1240], text[64];
    unsigned int attempt;
    int written;

    if (!state)
        return;
    written = snprintf(status, sizeof(status), "%s.status", state);
    if (written < 0 || (size_t)written >= sizeof(status))
        return;
    for (attempt = 0; attempt < 20; ++attempt) {
        struct timespec delay = {.tv_nsec = 50000000L};

        if (read_owned_file(status, text, sizeof(text)) &&
            !strcmp(text, "off\n"))
            return;
        (void)nanosleep(&delay, 0);
    }
}

static void remove_protocol_paths(const char *state)
{
    char path[1240];
    int written;

    if (!state || !*state)
        return;
    written = snprintf(path, sizeof(path), "%s.status", state);
    if (written >= 0 && (size_t)written < sizeof(path))
        (void)unlink(path);
    written = snprintf(path, sizeof(path), "%s.lock", state);
    if (written >= 0 && (size_t)written < sizeof(path))
        (void)unlink(path);
    (void)unlink(state);
}

void frame_pacer_thread_cpu_external_reap(
    struct frame_pacer_thread_cpu_quota *quota)
{
    pid_t result;

    if (!quota || quota->external_pid <= 0)
        return;
    do {
        result = waitpid(quota->external_pid, 0, WNOHANG);
    } while (result < 0 && errno == EINTR);
    if (result == quota->external_pid || (result < 0 && errno == ECHILD)) {
        quota->external_pid = 0;
        remove_protocol_paths(quota->external_state);
    }
}

struct external_request {
    char helper[1200];
    char pid[32];
    char scope[1200];
    bool state_existed;
};

static bool prepare(struct frame_pacer_thread_cpu_quota *quota,
                    const char *scope_name, uint32_t wanted,
                    struct external_request *request)
{
    char directory[1100], existing_state[32];
    int written;

    if (!quota || !scope_name || !request ||
        !helper_path(request->helper, sizeof(request->helper)) ||
        !frame_pacer_state_directory(directory, sizeof(directory), true))
        return false;
    memset(request->pid, 0, sizeof(request->pid));
    memset(request->scope, 0, sizeof(request->scope));
    written = snprintf(quota->external_state, sizeof(quota->external_state),
                       "%s/thread-cpu-%s", directory, scope_name);
    if (written < 0 || (size_t)written >= sizeof(quota->external_state))
        return false;
    request->state_existed = read_owned_file(
        quota->external_state, existing_state, sizeof(existing_state));
    if (!frame_pacer_thread_cpu_external_write(quota->external_state, true,
                                               wanted))
        goto fail;
    written = snprintf(request->pid, sizeof(request->pid), "%ju",
                       (uintmax_t)getpid());
    if (written < 0 || (size_t)written >= sizeof(request->pid))
        goto fail;
    if (quota->cgroup_proc[0]) {
        size_t suffix = strlen("/frame-pacer-thread-cpu");
        size_t length = strlen(quota->cgroup_proc);

        if (length <= suffix || length - suffix > (size_t)INT_MAX)
            goto fail;
        written = snprintf(request->scope, sizeof(request->scope), "%.*s",
                           (int)(length - suffix), quota->cgroup_proc);
        if (written < 0 || (size_t)written >= sizeof(request->scope))
            goto fail;
    } else {
        written = snprintf(request->scope, sizeof(request->scope),
                           "/user.slice/user-%ju.slice/%s", (uintmax_t)getuid(),
                           scope_name);
        if (written < 0 || (size_t)written >= sizeof(request->scope))
            goto fail;
    }
    return true;
fail:
    (void)unlink(quota->external_state);
    quota->external_state[0] = '\0';
    return false;
}

bool frame_pacer_thread_cpu_external_start_service(
    struct frame_pacer_thread_cpu_quota *quota,
    struct frame_pacer_systemd *systemd, const char *scope_name,
    uint32_t wanted)
{
    struct external_request request;
    char unit[180];
    const char *arguments[8];
    bool started;
    int written;

    if (!prepare(quota, scope_name, wanted, &request))
        return false;
    written = snprintf(unit, sizeof(unit),
                       "frame-pacer-thread-cpu-controller-u%ju-p%ju.service",
                       (uintmax_t)getuid(), (uintmax_t)getpid());
    if (written < 0 || (size_t)written >= sizeof(unit))
        goto fail;
    arguments[0] = request.helper;
    arguments[1] = "--pid";
    arguments[2] = request.pid;
    arguments[3] = "--scope";
    arguments[4] = request.scope;
    arguments[5] = quota->external_state;
    arguments[6] = "--owned-scope";
    arguments[7] = 0;
    started = frame_pacer_systemd_start_service(systemd, unit, request.helper,
                                                arguments);
    if (started || request.state_existed) {
        quota->external = true;
        return true;
    }
fail:
    (void)unlink(quota->external_state);
    quota->external_state[0] = '\0';
    return false;
}

bool frame_pacer_thread_cpu_external_start_native(
    struct frame_pacer_thread_cpu_quota *quota, const char *scope_name,
    uint32_t wanted, const char **failure_stage)
{
    struct external_request request;
    char *arguments[8];
    pid_t process_id;
    int result;

    if (failure_stage)
        *failure_stage = 0;
    if (quota && quota->external_pid > 0) {
        frame_pacer_thread_cpu_external_reap(quota);
        if (quota->external_pid > 0) {
            quota->external = true;
            return frame_pacer_thread_cpu_external_write(quota->external_state,
                                                         true, wanted);
        }
    }
    if (!prepare(quota, scope_name, wanted, &request)) {
        if (failure_stage)
            *failure_stage = "prepare native external controller";
        return false;
    }
    arguments[0] = request.helper;
    arguments[1] = "--pid";
    arguments[2] = request.pid;
    arguments[3] = "--scope";
    arguments[4] = request.scope;
    arguments[5] = quota->external_state;
    arguments[6] = "--bootstrap";
    arguments[7] = 0;
    result = posix_spawn(&process_id, request.helper, 0, 0, arguments, environ);
    if (result == EINVAL || result == ENOSYS) {
        process_id = fork();
        if (!process_id) {
            (void)execve(request.helper, arguments, environ);
            _exit(127);
        }
        result = process_id < 0 ? errno : 0;
    }
    if (result) {
        (void)unlink(quota->external_state);
        quota->external_state[0] = '\0';
        errno = result;
        if (failure_stage)
            *failure_stage = "spawn native external controller";
        return false;
    }
    quota->external_pid = process_id;
    quota->external = true;
    return true;
}

#ifdef FRAME_PACER_TEST
bool frame_pacer_thread_cpu_quota_test_helper_path(const char *library,
                                                   char *out, size_t size)
{
    return helper_path_from_library(library, out, size);
}

bool frame_pacer_thread_cpu_quota_test_runtime_helper_path(char *out,
                                                           size_t size)
{
    return helper_path(out, size);
}

bool frame_pacer_thread_cpu_quota_test_parse_confirmation(const char *text,
                                                          uint32_t quota)
{
    return frame_pacer_thread_cpu_parse_confirmation(text, quota);
}

bool frame_pacer_thread_cpu_quota_test_write_external_state(const char *path,
                                                            bool enabled,
                                                            uint32_t quota)
{
    return frame_pacer_thread_cpu_external_write(path, enabled, quota);
}
#endif
