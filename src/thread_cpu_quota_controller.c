/* Runs outside a Steam Runtime mount namespace.  It is started only as a
 * transient --user service and accepts one already-created, delegated scope. */
#define _GNU_SOURCE
#include "thread_cpu_protocol.h"
#include "thread_cpu_systemd.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#define MAX_TIDS 1024U
#define PERIOD 100000U

static volatile sig_atomic_t stop_requested;

static void request_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static bool install_signal_handlers(void)
{
    struct sigaction action = {0};

    action.sa_handler = request_stop;
    if (sigemptyset(&action.sa_mask)) return false;
    return !sigaction(SIGTERM, &action, 0) &&
           !sigaction(SIGINT, &action, 0) &&
           !sigaction(SIGHUP, &action, 0);
}

static bool parse_pid(const char *text, pid_t *pid)
{
    char *end;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno || end == text || *end || value <= 1 || value > INT_MAX)
        return false;
    *pid = (pid_t)value;
    return true;
}

static bool join(char *out, size_t size, const char *a, const char *b)
{
    int written = snprintf(out, size, "%s/%s", a, b);

    return written >= 0 && (size_t)written < size;
}

static bool valid_paths(pid_t pid, const char *scope, const char *state)
{
    char user_prefix[80], process_suffix[64], expected_state[NAME_MAX + 1];
    const char *scope_name, *boot, *state_name, *state_parent_end;
    struct stat directory_status;
    size_t index, parent_length;
    int written;

    written = snprintf(user_prefix, sizeof(user_prefix),
                       "/user.slice/user-%ju.slice/", (uintmax_t)getuid());
    if (written < 0 || (size_t)written >= sizeof(user_prefix) ||
        strncmp(scope, user_prefix, (size_t)written) || strstr(scope, "//") ||
        strstr(scope, "/./") || strstr(scope, "/../") ||
        scope[strlen(scope) - 1] == '/')
        return false;
    for (index = 0; scope[index]; ++index)
        if (iscntrl((unsigned char)scope[index]) ||
            isspace((unsigned char)scope[index]))
            return false;
    scope_name = strrchr(scope, '/');
    if (!scope_name || !*++scope_name) return false;
    written = snprintf(user_prefix, sizeof(user_prefix),
                       "frame-pacer-thread-cpu-u%ju-b",
                       (uintmax_t)getuid());
    if (written < 0 || (size_t)written >= sizeof(user_prefix) ||
        strncmp(scope_name, user_prefix, (size_t)written))
        return false;
    boot = scope_name + written;
    if (strlen(boot) < 12)
        return false;
    for (index = 0; index < 12; ++index)
        if (!isxdigit((unsigned char)boot[index]) && boot[index] != '-')
            return false;
    written = snprintf(process_suffix, sizeof(process_suffix), "-p%ju.scope",
                       (uintmax_t)pid);
    if (written < 0 || (size_t)written >= sizeof(process_suffix) ||
        strcmp(boot + 12, process_suffix))
        return false;

    state_name = strrchr(state, '/');
    if (!state_name || state_name == state || !*++state_name) return false;
    written = snprintf(expected_state, sizeof(expected_state), "thread-cpu-%s",
                       scope_name);
    if (written < 0 || (size_t)written >= sizeof(expected_state) ||
        strcmp(state_name, expected_state))
        return false;
    state_parent_end = state_name - 1;
    parent_length = (size_t)(state_parent_end - state);
    if (parent_length < sizeof("/frame-pacer") - 1 ||
        strncmp(state_parent_end - (sizeof("/frame-pacer") - 1),
                "/frame-pacer", sizeof("/frame-pacer") - 1))
        return false;
    {
        char parent[PATH_MAX];

        if (parent_length >= sizeof(parent)) return false;
        memcpy(parent, state, parent_length);
        parent[parent_length] = '\0';
        if (lstat(parent, &directory_status) ||
            !S_ISDIR(directory_status.st_mode) ||
            directory_status.st_uid != getuid() ||
            (directory_status.st_mode & 0077))
            return false;
    }
    return true;
}

static bool write_file(const char *path, const char *text)
{
#ifdef FRAME_PACER_TEST
    const char *failure_trigger =
        getenv("FRAME_PACER_TEST_CONTROLLER_FAIL_WRITES_WHEN");

    if (failure_trigger && *failure_trigger && !access(failure_trigger, F_OK)) {
        errno = EIO;
        return false;
    }
#endif
    int fd = open(path, O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOFOLLOW);
    size_t size = strlen(text);
    bool written;
    bool closed;

    if (fd < 0) return false;
    do {
        ssize_t result = write(fd, text, size);

        written = result == (ssize_t)size;
        if (result < 0 && errno == EINTR) continue;
        break;
    } while (true);
    written = written && fsync(fd) == 0;
    closed = close(fd) == 0;
    return written && closed;
}

static bool read_file(const char *path, char *out, size_t size)
{
    struct stat before, after;
    size_t offset = 0;
    int fd;

    if (!path || !out || !size) return false;
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 || fstat(fd, &before) || !S_ISREG(before.st_mode) ||
        before.st_uid != getuid() || before.st_nlink != 1) {
        if (fd >= 0) (void)close(fd);
        return false;
    }
    while (offset < size) {
        ssize_t bytes = read(fd, out + offset, size - offset);

        if (bytes > 0) {
            offset += (size_t)bytes;
        } else if (!bytes) {
            break;
        } else if (errno != EINTR) {
            (void)close(fd);
            return false;
        }
    }
    if (offset == size || fstat(fd, &after) || close(fd) ||
        before.st_dev != after.st_dev || before.st_ino != after.st_ino ||
        before.st_uid != after.st_uid || before.st_mode != after.st_mode ||
        before.st_nlink != after.st_nlink || before.st_size != after.st_size ||
        before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
        before.st_mtim.tv_nsec != after.st_mtim.tv_nsec)
        return false;
    out[offset] = '\0';
    return true;
}

static bool tid_exists(pid_t pid, uint32_t tid)
{
    char path[80];
    int written = snprintf(path, sizeof(path), "/proc/%ld/task/%u",
                           (long)pid, tid);

    return written > 0 && (size_t)written < sizeof(path) &&
           !access(path, F_OK);
}

static unsigned collect(pid_t pid, uint32_t out[MAX_TIDS], bool *overflow)
{
    char path[64];
    DIR *directory;
    struct dirent *entry;
    unsigned int count = 0;
    int written;

    *overflow = false;
    written = snprintf(path, sizeof(path), "/proc/%ld/task", (long)pid);
    if (written < 0 || (size_t)written >= sizeof(path) ||
        !(directory = opendir(path)))
        return 0;
    while ((entry = readdir(directory))) {
        char *end;
        unsigned long value;

        if (!isdigit((unsigned char)entry->d_name[0])) continue;
        value = strtoul(entry->d_name, &end, 10);
        if (*end || !value || value > UINT32_MAX) continue;
        if (count == MAX_TIDS)
            *overflow = true;
        else
            out[count++] = (uint32_t)value;
    }
    (void)closedir(directory);
    return count;
}

static bool cgroup_of(pid_t pid, uint32_t tid, char *out, size_t size)
{
    char path[80], line[2048];
    FILE *file;
    int written;

    written = snprintf(path, sizeof(path), "/proc/%ld/task/%u/cgroup",
                       (long)pid, tid);
    if (written < 0 || (size_t)written >= sizeof(path) ||
        !(file = fopen(path, "re")))
        return false;
    if (!fgets(line, sizeof(line), file) ||
        (!strchr(line, '\n') && !feof(file))) {
        (void)fclose(file);
        return false;
    }
    (void)fclose(file);
    if (strncmp(line, "0::", 3)) return false;
    line[strcspn(line, "\r\n")] = '\0';
    written = snprintf(out, size, "%s", line + 3);
    return written >= 0 && (size_t)written < size;
}

static bool process_in_scope(pid_t pid, const char *scope_relative)
{
    char current[PATH_MAX];
    size_t length;

    if (!scope_relative ||
        !cgroup_of(pid, (uint32_t)pid, current, sizeof(current)))
        return false;
    length = strlen(scope_relative);
    return !strncmp(current, scope_relative, length) &&
           (current[length] == '\0' || current[length] == '/');
}

static bool write_tid(const char *path, uint32_t tid)
{
    char text[32];
    int written = snprintf(text, sizeof(text), "%u", tid);

    return written > 0 && (size_t)written < sizeof(text) &&
           write_file(path, text);
}

static bool file_is(const char *path, const char *want)
{
    char text[64];

    if (!read_file(path, text, sizeof(text))) return false;
    text[strcspn(text, "\r\n")] = '\0';
    return !strcmp(text, want);
}

static void prune_empty_children(const char *root)
{
    DIR *directory = opendir(root);
    struct dirent *entry;
    char path[PATH_MAX];

    if (!directory) return;
    while ((entry = readdir(directory))) {
        const char *cursor = entry->d_name;
        char threads_path[PATH_MAX];
        FILE *threads;

        if (strncmp(cursor, "t-", 2) ||
            !isdigit((unsigned char)cursor[2]))
            continue;
        for (cursor += 2; *cursor; ++cursor)
            if (!isdigit((unsigned char)*cursor)) break;
        if (*cursor || !join(path, sizeof(path), root, entry->d_name) ||
            !join(threads_path, sizeof(threads_path), path,
                  "cgroup.threads") ||
            !(threads = fopen(threads_path, "re")))
            continue;
        {
            bool empty = fgetc(threads) == EOF;

            (void)fclose(threads);
            if (empty) (void)rmdir(path);
        }
    }
    (void)closedir(directory);
}

static void remove_tree(pid_t pid, const char *scope, const char *root,
                        const char *root_relative)
{
    uint32_t tids[MAX_TIDS];
    bool overflow;
    unsigned int count = collect(pid, tids, &overflow);
    unsigned int index;
    char child[64], path[PATH_MAX], relative[PATH_MAX];

    (void)overflow;
    for (index = 0; index < count; ++index) {
        if (snprintf(child, sizeof(child), "t-%u", tids[index]) > 0 &&
            join(relative, sizeof(relative), root_relative, child) &&
            cgroup_of(pid, tids[index], path, sizeof(path)) &&
            !strcmp(path, relative) &&
            join(path, sizeof(path), scope, "cgroup.threads"))
            (void)write_tid(path, tids[index]);
    }
    for (index = 0; index < count; ++index) {
        if (snprintf(child, sizeof(child), "t-%u", tids[index]) > 0 &&
            join(path, sizeof(path), root, child))
            (void)rmdir(path);
    }
    prune_empty_children(root);
    (void)rmdir(root);
    if (join(path, sizeof(path), scope, "cgroup.subtree_control"))
        (void)write_file(path, "-cpu");
}

static void release_scope(pid_t pid, const char *scope,
                          const char *scope_relative)
{
    char parent[PATH_MAX], path[PATH_MAX], text[32];
    char *separator;
    int written;

    if (!scope ||
        (written = snprintf(parent, sizeof(parent), "%s", scope)) < 0 ||
        (size_t)written >= sizeof(parent) ||
        !(separator = strrchr(parent, '/')) || separator == parent)
        return;
    *separator = '\0';
    if (process_in_scope(pid, scope_relative)) {
        written = snprintf(text, sizeof(text), "%ju", (uintmax_t)pid);
        if (written < 0 || (size_t)written >= sizeof(text) ||
            !join(path, sizeof(path), parent, "cgroup.procs") ||
            !write_file(path, text))
            return;
    }
    (void)rmdir(scope);
}

static bool apply(pid_t pid, const char *scope, const char *root, const char *root_rel, unsigned quota)
{
    uint32_t tids[MAX_TIDS];
    bool overflow;
    unsigned int count = collect(pid, tids, &overflow);
    unsigned int index, verified = 0;
    char path[PATH_MAX], relative[PATH_MAX], child[64], child_path[PATH_MAX];
    char wanted[32];

    if (overflow || !count || quota < 1 || quota > 100) return false;
    if (mkdir(root, 0700) && errno != EEXIST) return false;
    if (!join(path, sizeof(path), root, "cgroup.type") ||
        (!file_is(path, "threaded") && !write_file(path, "threaded")))
        return false;
    if (!join(path, sizeof(path), scope, "cgroup.subtree_control") ||
        !write_file(path, "+cpu") ||
        !join(path, sizeof(path), root, "cgroup.subtree_control") ||
        !write_file(path, "+cpu"))
        return false;
    if (snprintf(wanted, sizeof(wanted), "%u %u", quota * 1000U, PERIOD) < 0)
        return false;
    for (index = 0; index < count; ++index) {
        if (snprintf(child, sizeof(child), "t-%u", tids[index]) < 0 ||
            !join(child_path, sizeof(child_path), root, child))
            return false;
        if (mkdir(child_path, 0700) && errno != EEXIST) {
            if (tid_exists(pid, tids[index])) return false;
            continue;
        }
        if (!join(path, sizeof(path), child_path, "cgroup.type") ||
            (!file_is(path, "threaded") && !write_file(path, "threaded")) ||
            !join(path, sizeof(path), child_path, "cgroup.threads") ||
            !write_tid(path, tids[index]) ||
            !join(path, sizeof(path), child_path, "cpu.max") ||
            !write_file(path, wanted)) {
            if (tid_exists(pid, tids[index])) return false;
            continue;
        }
    }
    prune_empty_children(root);
    for (index = 0; index < count; ++index) {
        if (snprintf(child, sizeof(child), "t-%u", tids[index]) < 0 ||
            !join(relative, sizeof(relative), root_rel, child) ||
            !cgroup_of(pid, tids[index], path, sizeof(path)) ||
            strcmp(path, relative) ||
            !join(child_path, sizeof(child_path), root, child) ||
            !join(path, sizeof(path), child_path, "cpu.max") ||
            !file_is(path, wanted))
            return false;
        ++verified;
    }
    return verified == count;
}

static bool write_all(int fd, const char *text, size_t size)
{
    size_t offset = 0;

    while (offset < size) {
        ssize_t written = write(fd, text + offset, size - offset);

        if (written > 0) {
            offset += (size_t)written;
            continue;
        }
        if (written < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

static bool discover_scope(pid_t pid, const char *scope_name,
                           char *output, size_t size)
{
    char candidate[PATH_MAX], prefix[80];
    unsigned int attempt;
    int written = snprintf(prefix, sizeof(prefix),
                           "/user.slice/user-%ju.slice/",
                           (uintmax_t)getuid());

    if (!scope_name || !output || !size || written < 0 ||
        (size_t)written >= sizeof(prefix))
        return false;
    for (attempt = 0; attempt < 50; ++attempt) {
        const char *name;
        struct timespec pause = {.tv_nsec = 10000000L};

        if (cgroup_of(pid, (uint32_t)pid, candidate, sizeof(candidate)) &&
            !strncmp(candidate, prefix, strlen(prefix)) &&
            (name = strrchr(candidate, '/')) && !strcmp(name + 1, scope_name)) {
            written = snprintf(output, size, "%s", candidate);
            return written >= 0 && (size_t)written < size;
        }
        (void)nanosleep(&pause, 0);
    }
    return false;
}

static void publish_status(const char *path, const char *text)
{
    char temporary[PATH_MAX];
    int fd, written;
    bool complete;

    written = snprintf(temporary, sizeof(temporary), "%s.tmp-%ld", path,
                       (long)getpid());
    if (written < 0 || (size_t)written >= sizeof(temporary)) return;
    (void)unlink(temporary);
    fd = open(temporary,
              O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return;
    complete = write_all(fd, text, strlen(text)) && !fchmod(fd, 0600) &&
               !fsync(fd);
    if (close(fd)) complete = false;
    if (!complete || rename(temporary, path)) (void)unlink(temporary);
}

static int lock_controller(const char *state, char *path, size_t size)
{
    struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET};
    struct stat status;
    int fd;
    int written = snprintf(path, size, "%s.lock", state);

    if (written < 0 || (size_t)written >= size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return -1;
    if (fstat(fd, &status)) {
        int saved = errno;

        (void)close(fd);
        errno = saved;
        return -1;
    }
    if (!S_ISREG(status.st_mode) || status.st_uid != geteuid() ||
        status.st_nlink != 1) {
        (void)close(fd);
        errno = EPERM;
        return -1;
    }
    if (fchmod(fd, 0600) || fcntl(fd, F_SETLK, &lock)) {
        int saved = errno;

        (void)close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}

int main(int argc, char **argv)
{
    pid_t pid;
    const char *scope_relative, *state;
    char discovered_scope[PATH_MAX], scope[PATH_MAX], root[PATH_MAX],
        root_relative[PATH_MAX];
    char status[PATH_MAX], lock_path[PATH_MAX], buffer[64], last_status[64] = "";
    bool active = false;
    bool bootstrap = argc == 7 && !strcmp(argv[6], "--bootstrap");
    bool owned_scope = bootstrap ||
                       (argc == 7 && !strcmp(argv[6], "--owned-scope"));
    int lock_fd;

    if ((argc != 6 && !owned_scope) || strcmp(argv[1], "--pid") ||
        strcmp(argv[3], "--scope"))
        return 64;
    scope_relative = argv[4];
    state = argv[5];
    if (!parse_pid(argv[2], &pid) || !valid_paths(pid, scope_relative, state))
        return 64;
    if (!install_signal_handlers()) return 1;
    if (bootstrap) {
        struct frame_pacer_systemd systemd = {0};
        const char *scope_name = strrchr(scope_relative, '/');
        bool started;

        if (!scope_name || !*++scope_name ||
            !frame_pacer_systemd_open(&systemd)) {
            fputs("frame-pacer controller: systemd bootstrap unavailable\n",
                  stderr);
            (void)unlink(state);
            return 1;
        }
        started = frame_pacer_systemd_start_scope(&systemd, scope_name, pid);
        frame_pacer_systemd_close(&systemd);
        if (!started &&
            !discover_scope(pid, scope_name, discovered_scope,
                            sizeof(discovered_scope))) {
            fputs("frame-pacer controller: scope bootstrap failed\n", stderr);
            (void)unlink(state);
            return 1;
        }
        if (!discover_scope(pid, scope_name, discovered_scope,
                            sizeof(discovered_scope)) ||
            !valid_paths(pid, discovered_scope, state)) {
            fputs("frame-pacer controller: scope did not appear\n", stderr);
            (void)unlink(state);
            return 1;
        }
        scope_relative = discovered_scope;
    }
    if (snprintf(scope, sizeof(scope), "/sys/fs/cgroup%s", scope_relative) < 0 ||
        strlen(scope_relative) + strlen("/sys/fs/cgroup") >= sizeof(scope) ||
        !join(root, sizeof(root), scope, "frame-pacer-thread-cpu") ||
        !join(root_relative, sizeof(root_relative), scope_relative,
              "frame-pacer-thread-cpu") ||
        snprintf(status, sizeof(status), "%s.status", state) < 0 ||
        strlen(state) + strlen(".status") >= sizeof(status))
        return 64;
    lock_fd = lock_controller(state, lock_path, sizeof(lock_path));
    if (lock_fd < 0) {
        perror("frame-pacer controller: state lock");
        return 1;
    }

    while (!stop_requested &&
           (owned_scope ? process_in_scope(pid, scope_relative) :
                          kill(pid, 0) == 0)) {
        unsigned int quota = 0;
        bool enabled = false;
        struct timespec pause = {.tv_nsec = 250000000L};

        if (!read_file(state, buffer, sizeof(buffer))) break;
        if (!frame_pacer_thread_cpu_parse_state(buffer, &enabled, &quota))
            enabled = false;
        if (enabled) {
            bool confirmed = apply(pid, scope, root, root_relative, quota);

            (void)frame_pacer_thread_cpu_format_status(
                last_status, sizeof(last_status), confirmed, quota);
            active = true;
        } else {
            if (active)
                remove_tree(pid, scope, root, root_relative);
            (void)frame_pacer_thread_cpu_format_status(
                last_status, sizeof(last_status), false, 0);
            active = false;
        }
        publish_status(status, last_status);
        (void)nanosleep(&pause, 0);
    }
    if (active)
        remove_tree(pid, scope, root, root_relative);
    if (owned_scope)
        release_scope(pid, scope, scope_relative);
    (void)unlink(status);
    (void)unlink(state);
    (void)close(lock_fd);
    (void)unlink(lock_path);
    return 0;
}
