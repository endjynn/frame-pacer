#define _GNU_SOURCE
#include "thread_cpu_quota.h"

#include <assert.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static size_t open_descriptor_count(void)
{
    struct dirent *entry;
    DIR *directory = opendir("/proc/self/fd");
    size_t count = 0;

    assert(directory);
    while ((entry = readdir(directory))) {
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) ++count;
    }
    assert(closedir(directory) == 0);
    return count;
}

static void failed_write_closes_stream(void)
{
    char text[16384];
    size_t before;

    memset(text, 'x', sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';
    before = open_descriptor_count();
    assert(!frame_pacer_thread_cpu_quota_test_write_text("/dev/full", text));
    assert(open_descriptor_count() == before);
}

static void external_controller_is_disabled_on_destroy(void)
{
    struct frame_pacer_thread_cpu_quota quota;
    char directory[] = "/tmp/frame-pacer-quota-XXXXXX";
    char state[sizeof(quota.external_state)];
    char text[16];
    FILE *file;

    assert(mkdtemp(directory));
    assert(snprintf(state, sizeof(state), "%s/state", directory) > 0);
    file = fopen(state, "we");
    assert(file);
    assert(fputs("on 50\n", file) >= 0);
    assert(!fclose(file));

    frame_pacer_thread_cpu_quota_init(&quota);
    assert(quota.worker_started);
    assert(!pthread_mutex_lock(&quota.mutex));
    quota.external = true;
    assert(snprintf(quota.external_state, sizeof(quota.external_state), "%s",
                    state) > 0);
    assert(!pthread_mutex_unlock(&quota.mutex));
    frame_pacer_thread_cpu_quota_destroy(&quota);

    file = fopen(state, "re");
    assert(file);
    assert(fgets(text, sizeof(text), file));
    assert(!fclose(file));
    assert(!strcmp(text, "off\n"));
    assert(!unlink(state));
    assert(!rmdir(directory));
}

static void external_confirmation_is_exact(void)
{
    assert(frame_pacer_thread_cpu_quota_test_parse_confirmation(
        "confirmed 1\n", 1));
    assert(frame_pacer_thread_cpu_quota_test_parse_confirmation(
        "confirmed 100\n", 100));
    assert(!frame_pacer_thread_cpu_quota_test_parse_confirmation(
        "confirmed 0\n", 1));
    assert(!frame_pacer_thread_cpu_quota_test_parse_confirmation(
        "confirmed +50\n", 50));
    assert(!frame_pacer_thread_cpu_quota_test_parse_confirmation(
        "confirmed 050\n", 50));
    assert(!frame_pacer_thread_cpu_quota_test_parse_confirmation(
        "confirmed 50", 50));
    assert(!frame_pacer_thread_cpu_quota_test_parse_confirmation(
        "confirmed 50\nextra", 50));
    assert(!frame_pacer_thread_cpu_quota_test_parse_confirmation(
        "confirmed 42949672960\n", 50));
}

static void external_state_is_atomically_replaced(void)
{
    char directory[] = "/tmp/frame-pacer-state-XXXXXX";
    char state[1024], target[1024], text[32];
    struct stat status;
    FILE *file;

    assert(mkdtemp(directory));
    assert(snprintf(state, sizeof(state), "%s/state", directory) > 0);
    assert(snprintf(target, sizeof(target), "%s/target", directory) > 0);
    file = fopen(target, "we");
    assert(file && fputs("sentinel\n", file) >= 0 && !fclose(file));

    assert(!symlink(target, state));
    assert(frame_pacer_thread_cpu_quota_test_write_external_state(
        state, true, 50));
    file = fopen(target, "re");
    assert(file && fgets(text, sizeof(text), file) && !fclose(file));
    assert(!strcmp(text, "sentinel\n"));
    file = fopen(state, "re");
    assert(file && fgets(text, sizeof(text), file) && !fclose(file));
    assert(!strcmp(text, "on 50\n"));
    assert(!lstat(state, &status) && S_ISREG(status.st_mode) &&
           status.st_nlink == 1 && !(status.st_mode & 0077));

    assert(!unlink(state));
    assert(!link(target, state));
    assert(frame_pacer_thread_cpu_quota_test_write_external_state(
        state, false, 0));
    file = fopen(target, "re");
    assert(file && fgets(text, sizeof(text), file) && !fclose(file));
    assert(!strcmp(text, "sentinel\n"));
    file = fopen(state, "re");
    assert(file && fgets(text, sizeof(text), file) && !fclose(file));
    assert(!strcmp(text, "off\n"));
    assert(!unlink(state));
    assert(!unlink(target));
    assert(!rmdir(directory));
}

static void helper_lookup_supports_build_and_install_layouts(void)
{
    char directory[] = "/tmp/frame-pacer-helper-XXXXXX";
    char build[1024], installed[1024], architecture[1024];
    char helper[1024], resolved[1024];
    FILE *file;

    assert(mkdtemp(directory));
    assert(snprintf(build, sizeof(build), "%s/build", directory) > 0);
    assert(!mkdir(build, 0700));
    assert(snprintf(helper, sizeof(helper),
                    "%s/frame-pacer-thread-cpu-controller", build) > 0);
    file = fopen(helper, "we");
    assert(file);
    assert(!fclose(file));
    assert(!chmod(helper, 0700));
    assert(snprintf(architecture, sizeof(architecture),
                    "%s/lib/x86_64-linux-gnu/libframe_pacer_gl.so", build) > 0);
    assert(frame_pacer_thread_cpu_quota_test_helper_path(
        architecture, resolved, sizeof(resolved)));
    assert(!strcmp(helper, resolved));
    assert(!unlink(helper));

    assert(snprintf(installed, sizeof(installed), "%s/frame-pacer",
                    directory) > 0);
    assert(!mkdir(installed, 0700));
    assert(snprintf(helper, sizeof(helper),
                    "%s/frame-pacer-thread-cpu-controller", installed) > 0);
    file = fopen(helper, "we");
    assert(file);
    assert(!fclose(file));
    assert(!chmod(helper, 0700));
    assert(snprintf(architecture, sizeof(architecture),
                    "%s/i386/libVkLayer_frame_pacer.so", installed) > 0);
    assert(frame_pacer_thread_cpu_quota_test_helper_path(
        architecture, resolved, sizeof(resolved)));
    assert(!strcmp(helper, resolved));
    assert(!chmod(helper, 0720));
    assert(!frame_pacer_thread_cpu_quota_test_helper_path(
        architecture, resolved, sizeof(resolved)));
    assert(!unlink(helper));
    assert(!rmdir(installed));
    assert(!rmdir(build));
    assert(!rmdir(directory));
}

static void proc_identity_parsing_is_exact(void)
{
    assert(frame_pacer_thread_cpu_quota_test_parse_host_pid_line(
        "NSpid:\t123\n"));
    assert(frame_pacer_thread_cpu_quota_test_parse_host_pid_line(
        "NSpid: 123\r\n"));
    assert(!frame_pacer_thread_cpu_quota_test_parse_host_pid_line(
        "NSpid:\t123 45\n"));
    assert(!frame_pacer_thread_cpu_quota_test_parse_host_pid_line(
        "NSpid:\t+123\n"));
    assert(!frame_pacer_thread_cpu_quota_test_parse_host_pid_line(
        "NSpid:\t123junk\n"));
    assert(!frame_pacer_thread_cpu_quota_test_parse_host_pid_line(
        "NSpid:\t123"));
    assert(frame_pacer_thread_cpu_quota_test_valid_boot_id(
        "01234567-89ab-cdef-0123-456789abcdef"));
    assert(!frame_pacer_thread_cpu_quota_test_valid_boot_id(
        "01234567-89ab-cdef-0123-456789abcdeg"));
    assert(!frame_pacer_thread_cpu_quota_test_valid_boot_id(
        "0123456789ab-cdef-0123-456789abcdef"));
    assert(!frame_pacer_thread_cpu_quota_test_valid_boot_id("short"));
}

int main(void)
{
    struct frame_pacer_thread_cpu_quota quota;
    uint32_t percent = 99;

    frame_pacer_thread_cpu_quota_init(&quota);
    assert(!frame_pacer_thread_cpu_quota_confirmed(&quota, &percent));
    assert(percent == 0);
    frame_pacer_thread_cpu_quota_publish(&quota, true, 75);
    assert(!frame_pacer_thread_cpu_quota_confirmed(&quota, &percent));
    assert(percent == 0);
    /* Confirmation is worker-owned and never asserted by hot-path callers. */
    assert(!frame_pacer_thread_cpu_quota_confirmed(&quota, &percent));
    assert(percent == 0);
    frame_pacer_thread_cpu_quota_publish(&quota, false, 0);
    assert(!frame_pacer_thread_cpu_quota_confirmed(&quota, &percent));
    frame_pacer_thread_cpu_quota_destroy(&quota);
    failed_write_closes_stream();
    external_controller_is_disabled_on_destroy();
    external_confirmation_is_exact();
    external_state_is_atomically_replaced();
    helper_lookup_supports_build_and_install_layouts();
    proc_identity_parsing_is_exact();
    return 0;
}
