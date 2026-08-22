#define _GNU_SOURCE
#include "hud_nvml_client.h"

#include <stdint.h>

#if UINTPTR_MAX == UINT32_MAX

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <poll.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef FRAME_PACER_NVML_HELPER_IMAGE_HEADER
#define FRAME_PACER_NVML_HELPER_IMAGE_HEADER "hud_nvml_helper_image.h"
#endif
#include FRAME_PACER_NVML_HELPER_IMAGE_HEADER

#define HELPER_IMAGE_FD 3
#define HELPER_SOCKET_FD 4
#define HELPER_SOURCE_FD_MIN 200
#define REQUIRED_SEALS (F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE)
#ifndef FRAME_PACER_NVML_RETRY_ONE_NS
#define FRAME_PACER_NVML_RETRY_ONE_NS UINT64_C(5000000000)
#endif
#ifndef FRAME_PACER_NVML_RETRY_TWO_NS
#define FRAME_PACER_NVML_RETRY_TWO_NS UINT64_C(30000000000)
#endif

extern char **environ;

struct nvml_client_manager {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    bool active;
    bool stop;
    unsigned int references;
    unsigned int process_id;
    char pci_bus_id[16];
    pthread_t worker;
    int socket_fd;
    pid_t child;
    atomic_uint attempts;
    atomic_uint published_generation;
    atomic_uint published_sequence;
    atomic_uint published_available;
    atomic_uint published_use;
    atomic_uint published_temperature;
};

static struct nvml_client_manager manager = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .condition = PTHREAD_COND_INITIALIZER,
    .socket_fd = -1,
    .child = -1,
};

static uint64_t monotonic_ns(void)
{
    struct timespec value;

    if (clock_gettime(CLOCK_MONOTONIC, &value)) return 0;
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
           (uint64_t)value.tv_nsec;
}

static bool write_image(int descriptor)
{
    size_t offset = 0;

    while (offset < frame_pacer_nvml_helper_image_len) {
        ssize_t written = write(descriptor,
                                frame_pacer_nvml_helper_image + offset,
                                frame_pacer_nvml_helper_image_len - offset);

        if (written > 0) offset += (size_t)written;
        else if (written < 0 && errno == EINTR) continue;
        else return false;
    }
    return true;
}

static bool valid_image(void)
{
    return frame_pacer_nvml_helper_image_len >= EI_NIDENT &&
           frame_pacer_nvml_helper_image[EI_MAG0] == ELFMAG0 &&
           frame_pacer_nvml_helper_image[EI_MAG1] == ELFMAG1 &&
           frame_pacer_nvml_helper_image[EI_MAG2] == ELFMAG2 &&
           frame_pacer_nvml_helper_image[EI_MAG3] == ELFMAG3 &&
           frame_pacer_nvml_helper_image[EI_CLASS] == ELFCLASS64;
}

static char **filtered_environment(void)
{
    size_t count = 0, index, output = 0;
    char **result;

    while (environ[count]) ++count;
    result = calloc(count + 1, sizeof(*result));
    if (!result) return 0;
    for (index = 0; index < count; ++index) {
        if (!strncmp(environ[index], "LD_PRELOAD=", 11) ||
            !strncmp(environ[index], "LD_AUDIT=", 9))
            continue;
        result[output++] = environ[index];
    }
    return result;
}

static void close_descriptor(int *descriptor)
{
    if (*descriptor >= 0) {
        (void)close(*descriptor);
        *descriptor = -1;
    }
}

static bool spawn_helper(pid_t *child, int *client_socket)
{
    int image = -1, sockets[2] = { -1, -1 }, image_source = -1;
    int socket_source = -1, result;
    char pid_text[32];
    char **environment = 0;
    posix_spawn_file_actions_t actions;
    bool actions_initialized = false;
    char *arguments[12];
    int argument_count = 0;

    *child = -1;
    *client_socket = -1;
    if (!valid_image() ||
        snprintf(pid_text, sizeof(pid_text), "%u", manager.process_id) <= 0)
        return false;
    image = memfd_create("frame-pacer-nvml-helper",
                         MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (image < 0 || !write_image(image) ||
        fcntl(image, F_ADD_SEALS, REQUIRED_SEALS) ||
        (fcntl(image, F_GET_SEALS) & REQUIRED_SEALS) != REQUIRED_SEALS ||
        socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets))
        goto done;
    image_source = fcntl(image, F_DUPFD_CLOEXEC, HELPER_SOURCE_FD_MIN);
    socket_source = fcntl(sockets[1], F_DUPFD_CLOEXEC,
                          HELPER_SOURCE_FD_MIN);
    if (image_source < 0 || socket_source < 0 ||
        posix_spawn_file_actions_init(&actions))
        goto done;
    actions_initialized = true;
    if (posix_spawn_file_actions_addclose(&actions, sockets[0]) ||
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null",
                                         O_RDONLY, 0) ||
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null",
                                         O_WRONLY, 0) ||
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null",
                                         O_WRONLY, 0) ||
        posix_spawn_file_actions_adddup2(&actions, image_source,
                                         HELPER_IMAGE_FD) ||
        posix_spawn_file_actions_adddup2(&actions, socket_source,
                                         HELPER_SOCKET_FD) ||
        posix_spawn_file_actions_addclosefrom_np(&actions, 5))
        goto done;
    environment = filtered_environment();
    if (!environment) goto done;
    arguments[argument_count++] = (char *)"frame-pacer-nvml-helper";
    arguments[argument_count++] = (char *)"--pid";
    arguments[argument_count++] = pid_text;
    arguments[argument_count++] = (char *)"--pci";
    arguments[argument_count++] = manager.pci_bus_id;
#ifdef FRAME_PACER_TEST_NVML_LIBRARY
    arguments[argument_count++] = (char *)"--library";
    arguments[argument_count++] = (char *)FRAME_PACER_TEST_NVML_LIBRARY;
#endif
#ifdef FRAME_PACER_TEST_REJECT_FD
    arguments[argument_count++] = (char *)"--reject-fd";
    arguments[argument_count++] = (char *)FRAME_PACER_TEST_REJECT_FD;
#endif
    arguments[argument_count] = 0;
    result = posix_spawn(child, "/proc/self/fd/3", &actions, 0, arguments,
                         environment);
    if (result) {
        errno = result;
        *child = -1;
        goto done;
    }
    *client_socket = sockets[0];
    sockets[0] = -1;
done:
    free(environment);
    if (actions_initialized)
        (void)posix_spawn_file_actions_destroy(&actions);
    close_descriptor(&image_source);
    close_descriptor(&socket_source);
    close_descriptor(&image);
    close_descriptor(&sockets[0]);
    close_descriptor(&sockets[1]);
    return *child > 0 && *client_socket >= 0;
}

static void publish(const struct frame_pacer_nvml_message *message)
{
    (void)atomic_fetch_add_explicit(&manager.published_generation, 1,
                                    memory_order_acq_rel);
    atomic_store_explicit(&manager.published_use,
                          message->sample.gpu_use_percent,
                          memory_order_relaxed);
    atomic_store_explicit(&manager.published_temperature,
                          message->sample.gpu_temp_celsius,
                          memory_order_relaxed);
    atomic_store_explicit(&manager.published_available,
                          message->sample.available, memory_order_relaxed);
    atomic_store_explicit(&manager.published_sequence, message->sequence,
                          memory_order_relaxed);
    (void)atomic_fetch_add_explicit(&manager.published_generation, 1,
                                    memory_order_release);
}

static void invalidate(void)
{
    (void)atomic_fetch_add_explicit(&manager.published_generation, 1,
                                    memory_order_acq_rel);
    atomic_store_explicit(&manager.published_available, 0,
                          memory_order_relaxed);
    (void)atomic_fetch_add_explicit(&manager.published_generation, 1,
                                    memory_order_release);
}

static bool should_stop(void)
{
    bool stop;

    (void)pthread_mutex_lock(&manager.mutex);
    stop = manager.stop;
    (void)pthread_mutex_unlock(&manager.mutex);
    return stop;
}

static void set_live_process(int socket_fd, pid_t child)
{
    (void)pthread_mutex_lock(&manager.mutex);
    manager.socket_fd = socket_fd;
    manager.child = child;
    (void)pthread_mutex_unlock(&manager.mutex);
}

static void clear_live_process(void)
{
    (void)pthread_mutex_lock(&manager.mutex);
    manager.socket_fd = -1;
    manager.child = -1;
    (void)pthread_mutex_unlock(&manager.mutex);
}

static void wait_until(uint64_t deadline)
{
    struct timespec absolute;
    uint64_t now = monotonic_ns();
    uint64_t remaining;

    if (!now || now >= deadline) return;
    remaining = deadline - now;
    if (clock_gettime(CLOCK_REALTIME, &absolute)) return;
    absolute.tv_sec += (time_t)(remaining / UINT64_C(1000000000));
    absolute.tv_nsec += (long)(remaining % UINT64_C(1000000000));
    if (absolute.tv_nsec >= 1000000000L) {
        ++absolute.tv_sec;
        absolute.tv_nsec -= 1000000000L;
    }
    (void)pthread_mutex_lock(&manager.mutex);
    if (!manager.stop)
        (void)pthread_cond_timedwait(&manager.condition, &manager.mutex,
                                     &absolute);
    (void)pthread_mutex_unlock(&manager.mutex);
}

static void terminate_child(pid_t child)
{
    int status;
    pid_t result;

    if (child <= 0) return;
    do {
        result = waitpid(child, &status, WNOHANG);
    } while (result < 0 && errno == EINTR);
    if (result == child || (result < 0 && errno == ECHILD)) return;
    if (result < 0) return;
    (void)kill(child, SIGTERM);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
}

static void run_connection(int socket_fd, pid_t child)
{
    uint32_t previous_sequence = 0;
    bool have_previous = false;
    bool reaped = false;

    set_live_process(socket_fd, child);
    while (!should_stop()) {
        struct pollfd descriptor = {
            .fd = socket_fd,
            .events = POLLIN | POLLHUP | POLLERR,
        };
        int status;
        int result = poll(&descriptor, 1, 250);

        if (result < 0 && errno != EINTR) break;
        if (waitpid(child, &status, WNOHANG) == child) {
            reaped = true;
            break;
        }
        if (result > 0 &&
            (descriptor.revents & (POLLHUP | POLLERR | POLLNVAL)))
            break;
        if (result > 0 && (descriptor.revents & POLLIN)) {
            unsigned char encoded[FRAME_PACER_NVML_PROTOCOL_SIZE + 1];
            ssize_t size;

            do {
                struct frame_pacer_nvml_message message;

                size = recv(socket_fd, encoded, sizeof(encoded), MSG_DONTWAIT);
                if (size > 0 && frame_pacer_nvml_protocol_decode(
                                    encoded, (size_t)size, previous_sequence,
                                    have_previous, &message)) {
                    previous_sequence = message.sequence;
                    have_previous = true;
                    publish(&message);
                }
            } while (size > 0);
            if (!size) break;
        }
    }
    invalidate();
    (void)shutdown(socket_fd, SHUT_RDWR);
    (void)close(socket_fd);
    if (!reaped) terminate_child(child);
    clear_live_process();
}

static void *worker_main(void *unused)
{
    static const uint64_t retry_delays[] = {
        0, FRAME_PACER_NVML_RETRY_ONE_NS, FRAME_PACER_NVML_RETRY_TWO_NS,
    };
    uint64_t started = monotonic_ns();

    (void)unused;
    while (!should_stop() &&
           atomic_load_explicit(&manager.attempts,
                                memory_order_relaxed) < 3) {
        pid_t child;
        int socket_fd;
        unsigned int attempt = atomic_load_explicit(
            &manager.attempts, memory_order_relaxed);

        wait_until(started + retry_delays[attempt]);
        if (should_stop()) break;
        (void)atomic_fetch_add_explicit(&manager.attempts, 1,
                                        memory_order_relaxed);
        if (spawn_helper(&child, &socket_fd))
            run_connection(socket_fd, child);
    }
    return 0;
}

bool frame_pacer_nvml_client_acquire(unsigned int process_id,
                                     const char *pci_bus_id)
{
    bool result = false;

    if (!process_id || !pci_bus_id || strlen(pci_bus_id) >=
                                      sizeof(manager.pci_bus_id))
        return false;
    (void)pthread_mutex_lock(&manager.mutex);
    if (manager.active) {
        if (!manager.stop && manager.process_id == process_id &&
            !strcmp(manager.pci_bus_id, pci_bus_id)) {
            ++manager.references;
            result = true;
        }
        (void)pthread_mutex_unlock(&manager.mutex);
        return result;
    }
    manager.active = true;
    manager.stop = false;
    manager.references = 1;
    manager.process_id = process_id;
    atomic_store_explicit(&manager.attempts, 0, memory_order_relaxed);
    manager.socket_fd = -1;
    manager.child = -1;
    (void)snprintf(manager.pci_bus_id, sizeof(manager.pci_bus_id), "%s",
                   pci_bus_id);
    atomic_store_explicit(&manager.published_sequence, 0,
                          memory_order_relaxed);
    atomic_store_explicit(&manager.published_generation, 0,
                          memory_order_relaxed);
    atomic_store_explicit(&manager.published_available, 0,
                          memory_order_relaxed);
    if (pthread_create(&manager.worker, 0, worker_main, 0)) {
        manager.active = false;
        manager.references = 0;
    } else {
        result = true;
    }
    (void)pthread_mutex_unlock(&manager.mutex);
    return result;
}

void frame_pacer_nvml_client_release(void)
{
    pthread_t worker;
    int socket_fd;

    (void)pthread_mutex_lock(&manager.mutex);
    if (!manager.active || !manager.references) {
        (void)pthread_mutex_unlock(&manager.mutex);
        return;
    }
    if (--manager.references) {
        (void)pthread_mutex_unlock(&manager.mutex);
        return;
    }
    manager.stop = true;
    socket_fd = manager.socket_fd;
    worker = manager.worker;
    (void)pthread_cond_broadcast(&manager.condition);
    (void)pthread_mutex_unlock(&manager.mutex);
    if (socket_fd >= 0) (void)shutdown(socket_fd, SHUT_RDWR);
    (void)pthread_join(worker, 0);
    (void)pthread_mutex_lock(&manager.mutex);
    manager.active = false;
    manager.stop = false;
    manager.process_id = 0;
    manager.pci_bus_id[0] = '\0';
    atomic_store_explicit(&manager.attempts, 0, memory_order_relaxed);
    (void)pthread_mutex_unlock(&manager.mutex);
    invalidate();
}

bool frame_pacer_nvml_client_snapshot(struct frame_pacer_nvml_message *message)
{
    uint32_t before, after;
    unsigned int attempt;

    if (!message) return false;
    for (attempt = 0; attempt < 3; ++attempt) {
        before = atomic_load_explicit(&manager.published_generation,
                                      memory_order_acquire);
        if (before & 1U) continue;
        message->sequence = atomic_load_explicit(
            &manager.published_sequence, memory_order_relaxed);
        if (!message->sequence) return false;
        message->sample.available = atomic_load_explicit(
            &manager.published_available, memory_order_relaxed);
        message->sample.gpu_use_percent = atomic_load_explicit(
            &manager.published_use, memory_order_relaxed);
        message->sample.gpu_temp_celsius = atomic_load_explicit(
            &manager.published_temperature, memory_order_relaxed);
        after = atomic_load_explicit(&manager.published_generation,
                                     memory_order_acquire);
        if (before == after && !(after & 1U)) return true;
    }
    return false;
}

#ifdef FRAME_PACER_TEST
unsigned int frame_pacer_nvml_client_test_attempts(void)
{
    return atomic_load_explicit(&manager.attempts, memory_order_relaxed);
}

int frame_pacer_nvml_client_test_child(void)
{
    int child;

    (void)pthread_mutex_lock(&manager.mutex);
    child = (int)manager.child;
    (void)pthread_mutex_unlock(&manager.mutex);
    return child;
}

void frame_pacer_nvml_client_test_publish(
    const struct frame_pacer_nvml_message *message)
{
    if (message) publish(message);
}
#endif

#else

bool frame_pacer_nvml_client_acquire(unsigned int process_id,
                                     const char *pci_bus_id)
{
    (void)process_id;
    (void)pci_bus_id;
    return false;
}

void frame_pacer_nvml_client_release(void) {}

bool frame_pacer_nvml_client_snapshot(struct frame_pacer_nvml_message *message)
{
    (void)message;
    return false;
}

#ifdef FRAME_PACER_TEST
unsigned int frame_pacer_nvml_client_test_attempts(void) { return 0; }
int frame_pacer_nvml_client_test_child(void) { return -1; }
void frame_pacer_nvml_client_test_publish(
    const struct frame_pacer_nvml_message *message)
{
    (void)message;
}
#endif

#endif
