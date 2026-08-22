#define _POSIX_C_SOURCE 200809L
#include "hud_nvml_client.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void wait_briefly(void)
{
    const struct timespec delay = { .tv_nsec = 10000000L };
    struct timespec remaining = delay;

    while (nanosleep(&remaining, &remaining) && errno == EINTR) {}
}

#if !defined(FRAME_PACER_TEST_EXPECT_FAILURE) && \
    !defined(FRAME_PACER_TEST_TARGET_EXIT)
static atomic_bool publishing;

static void *publish_snapshots(void *unused)
{
    static const struct frame_pacer_nvml_message messages[] = {
        { .sequence = 1,
          .sample = { .available = FRAME_PACER_NVML_GPU_USE |
                                   FRAME_PACER_NVML_GPU_TEMP,
                      .gpu_use_percent = 37,
                      .gpu_temp_celsius = 64 } },
        { .sequence = 2,
          .sample = { .available = FRAME_PACER_NVML_GPU_USE |
                                   FRAME_PACER_NVML_GPU_TEMP,
                      .gpu_use_percent = 83,
                      .gpu_temp_celsius = 105 } },
    };
    unsigned int iteration;

    (void)unused;
    for (iteration = 0; iteration < 100000; ++iteration)
        frame_pacer_nvml_client_test_publish(&messages[iteration & 1U]);
    atomic_store_explicit(&publishing, false, memory_order_release);
    return 0;
}

static void test_coherent_publication(void)
{
    struct frame_pacer_nvml_message snapshot;
    pthread_t writer;

    atomic_store_explicit(&publishing, true, memory_order_relaxed);
    assert(!pthread_create(&writer, 0, publish_snapshots, 0));
    while (atomic_load_explicit(&publishing, memory_order_acquire)) {
        if (!frame_pacer_nvml_client_snapshot(&snapshot)) continue;
        if (!snapshot.sample.available) continue;
        assert(snapshot.sample.available ==
               (FRAME_PACER_NVML_GPU_USE | FRAME_PACER_NVML_GPU_TEMP));
        assert((snapshot.sequence == 1 &&
                snapshot.sample.gpu_use_percent == 37 &&
                snapshot.sample.gpu_temp_celsius == 64) ||
               (snapshot.sequence == 2 &&
                snapshot.sample.gpu_use_percent == 83 &&
                snapshot.sample.gpu_temp_celsius == 105));
    }
    assert(!pthread_join(writer, 0));
}
#endif

int main(void)
{
    struct frame_pacer_nvml_message snapshot;
    unsigned int iteration;

#ifdef FRAME_PACER_TEST_TARGET_EXIT
    {
        pid_t target = fork();
        int status;

        assert(target >= 0);
        if (!target) {
            struct timespec delay = { .tv_nsec = 50000000L };

            while (nanosleep(&delay, &delay) && errno == EINTR) {}
            _exit(0);
        }
        assert(frame_pacer_nvml_client_acquire((unsigned int)target,
                                               "0000:01:00.0"));
        assert(waitpid(target, &status, 0) == target);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        for (iteration = 0; iteration < 200 &&
                            frame_pacer_nvml_client_test_attempts() < 3;
             ++iteration)
            wait_briefly();
        assert(frame_pacer_nvml_client_test_attempts() == 3);
        for (iteration = 0; iteration < 50; ++iteration) {
            if (!frame_pacer_nvml_client_snapshot(&snapshot) ||
                !snapshot.sample.available)
                break;
            wait_briefly();
        }
        assert(iteration < 50);
    }
#elif defined(FRAME_PACER_TEST_EXPECT_FAILURE)
    assert(frame_pacer_nvml_client_acquire((unsigned int)getpid(),
                                           "0000:01:00.0"));
    for (iteration = 0; iteration < 200 &&
                        frame_pacer_nvml_client_test_attempts() < 3;
         ++iteration)
        wait_briefly();
    assert(frame_pacer_nvml_client_test_attempts() == 3);
    for (iteration = 0; iteration < 20; ++iteration) wait_briefly();
    assert(frame_pacer_nvml_client_test_attempts() == 3);
    assert(!frame_pacer_nvml_client_snapshot(&snapshot) ||
           !snapshot.sample.available);
#else
    {
        int descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);

        assert(descriptor >= 0);
        assert(dup2(descriptor, 77) == 77);
        assert(!close(descriptor));
    }
    assert(!close(STDIN_FILENO));
    assert(frame_pacer_nvml_client_acquire((unsigned int)getpid(),
                                           "0000:01:00.0"));
    assert(frame_pacer_nvml_client_acquire((unsigned int)getpid(),
                                           "0000:01:00.0"));
    for (iteration = 0; iteration < 300; ++iteration) {
        if (frame_pacer_nvml_client_snapshot(&snapshot) &&
            snapshot.sample.available ==
                (FRAME_PACER_NVML_GPU_USE | FRAME_PACER_NVML_GPU_TEMP))
            break;
        wait_briefly();
    }
    assert(iteration < 300);
    assert(snapshot.sample.gpu_use_percent == 37);
    assert(snapshot.sample.gpu_temp_celsius == 64);
    assert(frame_pacer_nvml_client_test_child() > 0);
    frame_pacer_nvml_client_release();
    assert(!close(77));
#endif
    frame_pacer_nvml_client_release();
    assert(frame_pacer_nvml_client_test_child() < 0);
#if !defined(FRAME_PACER_TEST_EXPECT_FAILURE) && \
    !defined(FRAME_PACER_TEST_TARGET_EXIT)
    test_coherent_publication();
#endif
    assert(!kill(0, 0));
}
