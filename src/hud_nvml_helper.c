#define _GNU_SOURCE
#include "hud_nvml_protocol.h"
#include "hud_nvml_provider.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define HELPER_IMAGE_FD 3
#define HELPER_SOCKET_FD 4
#define REQUIRED_SEALS (F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE)
#define SAMPLE_INTERVAL_NS UINT64_C(1000000000)

static volatile sig_atomic_t stopping;

static void stop_helper(int signal_number)
{
    (void)signal_number;
    stopping = 1;
}

static bool parse_pid(const char *text, pid_t *pid)
{
    char *end;
    unsigned long value;

    if (!text || !*text || !isdigit((unsigned char)*text)) return false;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno || *end || !value || value > (unsigned long)INT32_MAX)
        return false;
    *pid = (pid_t)value;
    return true;
}

static bool valid_pci(const char *pci)
{
    static const unsigned int hex_positions[] = { 0, 1, 2, 3, 5, 6,
                                                   8, 9, 11 };
    unsigned int index;

    if (!pci || strlen(pci) != 12 || pci[4] != ':' || pci[7] != ':' ||
        pci[10] != '.')
        return false;
    for (index = 0; index < sizeof(hex_positions) / sizeof(hex_positions[0]);
         ++index)
        if (!isxdigit((unsigned char)pci[hex_positions[index]])) return false;
    return true;
}

static uint64_t monotonic_ns(void)
{
    struct timespec value;

    if (clock_gettime(CLOCK_MONOTONIC, &value)) return 0;
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
           (uint64_t)value.tv_nsec;
}

static bool target_alive(pid_t pid)
{
    return !kill(pid, 0) || errno == EPERM;
}

static int helper_loop(pid_t target, const char *pci, const char *library,
                       int reject_fd)
{
    struct frame_pacer_nvml_provider provider;
    struct sigaction action;
    uint64_t next_sample = 0;
    uint32_t sequence = 0;
    bool selected = false;
    int flags;

    if ((fcntl(HELPER_IMAGE_FD, F_GET_SEALS) & REQUIRED_SEALS) !=
            REQUIRED_SEALS ||
        close(HELPER_IMAGE_FD) ||
        (reject_fd >= 0 &&
         (fcntl(reject_fd, F_GETFD) >= 0 || errno != EBADF)))
        return 2;
    flags = fcntl(HELPER_SOCKET_FD, F_GETFL);
    if (flags < 0 || fcntl(HELPER_SOCKET_FD, F_SETFL, flags | O_NONBLOCK))
        return 2;
    memset(&action, 0, sizeof(action));
    action.sa_handler = stop_helper;
    (void)sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, 0) || sigaction(SIGINT, &action, 0) ||
        signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        return 2;
    if (!frame_pacer_nvml_provider_init(&provider, library)) return 3;

    while (!stopping && target_alive(target)) {
        struct pollfd descriptor = {
            .fd = HELPER_SOCKET_FD,
            .events = POLLIN | POLLHUP | POLLERR,
        };
        uint64_t now = monotonic_ns();
        int timeout = 1000;
        int result;

        if (!now) break;
        if (!next_sample || now >= next_sample) {
            struct frame_pacer_nvml_message message;
            unsigned char encoded[FRAME_PACER_NVML_PROTOCOL_SIZE];
            ssize_t sent;

            if (!selected)
                selected = frame_pacer_nvml_provider_select_pci(&provider,
                                                                 pci);
            memset(&message, 0, sizeof(message));
            message.sequence = ++sequence;
            if (selected)
                (void)frame_pacer_nvml_provider_sample(&provider,
                                                       &message.sample);
            frame_pacer_nvml_protocol_encode(encoded, &message);
            sent = send(HELPER_SOCKET_FD, encoded, sizeof(encoded),
                        MSG_DONTWAIT | MSG_NOSIGNAL);
            if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
                errno != EINTR)
                break;
            if (sent >= 0 && sent != (ssize_t)sizeof(encoded)) break;
            next_sample = now + SAMPLE_INTERVAL_NS;
        }
        now = monotonic_ns();
        if (now && next_sample > now) {
            uint64_t remaining_ms = (next_sample - now) / UINT64_C(1000000);
            timeout = remaining_ms > 1000 ? 1000 : (int)remaining_ms;
            if (timeout < 1) timeout = 1;
        }
        result = poll(&descriptor, 1, timeout);
        if (result < 0 && errno != EINTR) break;
        if (result > 0 &&
            (descriptor.revents & (POLLHUP | POLLERR | POLLNVAL)))
            break;
        if (result > 0 && (descriptor.revents & POLLIN)) {
            unsigned char ignored[16];

            result = (int)recv(HELPER_SOCKET_FD, ignored, sizeof(ignored),
                               MSG_DONTWAIT);
            if (!result) break;
        }
    }
    frame_pacer_nvml_provider_destroy(&provider);
    return 0;
}

int main(int argc, char **argv)
{
    const char *pid_text = 0, *pci = 0, *library = 0;
    int reject_fd = -1;
    pid_t target;
    int index;

    for (index = 1; index < argc; ++index) {
        if (!strcmp(argv[index], "--pid") && index + 1 < argc)
            pid_text = argv[++index];
        else if (!strcmp(argv[index], "--pci") && index + 1 < argc)
            pci = argv[++index];
#ifdef FRAME_PACER_TEST
        else if (!strcmp(argv[index], "--library") && index + 1 < argc)
            library = argv[++index];
        else if (!strcmp(argv[index], "--reject-fd") && index + 1 < argc) {
            char *end;
            long value = strtol(argv[++index], &end, 10);

            if (*end || value < 5 || value > INT32_MAX) return 1;
            reject_fd = (int)value;
        }
#endif
        else
            return 1;
    }
    if (!parse_pid(pid_text, &target) || !valid_pci(pci)) return 1;
    return helper_loop(target, pci, library, reject_fd);
}
