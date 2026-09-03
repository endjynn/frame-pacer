#define _GNU_SOURCE
#include "pacer_limit.h"

#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct fixture {
    char root[256];
    char directory[320];
    char path[384];
    struct frame_pacer_limit limit;
    uint64_t now;
};

static void fixture_init(struct fixture *fixture)
{
    char template[] = "/tmp/frame-pacer-limit-XXXXXX";

    assert(mkdtemp(template));
    assert(snprintf(fixture->root, sizeof(fixture->root), "%s", template) > 0);
    assert(snprintf(fixture->directory, sizeof(fixture->directory),
                    "%s/frame-pacer", template) > 0);
    assert(snprintf(fixture->path, sizeof(fixture->path), "%s/frame-pacer.conf",
                    fixture->directory) > 0);
    assert(!mkdir(fixture->directory, 0700));
    assert(!setenv("XDG_CONFIG_HOME", fixture->root, 1));
    frame_pacer_limit_init(&fixture->limit);
    frame_pacer_limit_set_reporting_enabled(&fixture->limit, true);
    fixture->now = 1;
}

static void fixture_destroy(struct fixture *fixture)
{
    char other[420];

    frame_pacer_limit_destroy(&fixture->limit);
    (void)snprintf(other, sizeof(other), "%s.other", fixture->path);
    (void)unlink(fixture->path);
    (void)unlink(other);
    (void)rmdir(fixture->path);
    (void)rmdir(fixture->directory);
    (void)rmdir(fixture->root);
}

static void write_bytes(const char *path, const void *bytes, size_t length)
{
    const unsigned char *cursor = bytes;
    size_t offset = 0;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

    assert(fd >= 0);
    assert(!fchmod(fd, 0600));
    while (offset < length) {
        ssize_t written = write(fd, cursor + offset, length - offset);

        assert(written > 0);
        offset += (size_t)written;
    }
    assert(!close(fd));
}

static void write_text(const char *path, const char *text)
{
    write_bytes(path, text, strlen(text));
}

static struct frame_pacer_effective_config
poll_snapshot(struct fixture *fixture)
{
    struct frame_pacer_effective_config snapshot;

    (void)frame_pacer_limit_poll(&fixture->limit, fixture->now);
    fixture->now += FRAME_PACER_CONFIG_POLL_NS + 1;
    assert(frame_pacer_limit_snapshot(&fixture->limit, &snapshot));
    return snapshot;
}

static void expect_failure(struct fixture *fixture, const char *text,
                           enum frame_pacer_config_reason reason, size_t line)
{
    struct frame_pacer_effective_config snapshot;

    write_text(fixture->path, text);
    snapshot = poll_snapshot(fixture);
    assert(snapshot.status == FRAME_PACER_CONFIG_MALFORMED);
    assert(snapshot.reason == reason);
    assert(snapshot.error_line == line);
    assert(snapshot.fps_limit == FRAME_PACER_FPS_LIMIT_OFF);
    assert(snapshot.fps_source == FRAME_PACER_SOURCE_DEFAULT);
    assert(snapshot.hud_enabled);
    assert(snapshot.hud_source == FRAME_PACER_SOURCE_DEFAULT);
    assert(!snapshot.thread_cpu_enabled);
    assert(snapshot.thread_cpu_source == FRAME_PACER_SOURCE_DEFAULT);
}

static void test_selection_and_revisions(void)
{
    struct frame_pacer_effective_config snapshot;
    struct fixture fixture;
    uint64_t revision;
    char matching[1200];

    fixture_init(&fixture);
    assert(atomic_is_lock_free(&fixture.limit.fps));
    assert(atomic_is_lock_free(&fixture.limit.thread_cpu_quota));
    assert(atomic_is_lock_free(&fixture.limit.hud_enabled));
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.revision == 1);
    assert(snapshot.status == FRAME_PACER_CONFIG_MISSING);
    assert(snapshot.reason == FRAME_PACER_REASON_MISSING_FILE);
    assert(snapshot.renderer[0]);
    revision = snapshot.revision;
    assert(frame_pacer_limit_poll(
               &fixture.limit, fixture.now - FRAME_PACER_CONFIG_POLL_NS) == 0);
    assert(frame_pacer_limit_revision(&fixture.limit) == revision);

    write_text(fixture.path, "global_fps_limit = 60\nhud = off\n");
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.revision == revision + 1);
    assert(snapshot.status == FRAME_PACER_CONFIG_VALID);
    assert(snapshot.reason == FRAME_PACER_REASON_NO_PER_GAME_RULES);
    assert(snapshot.fps_limit == 60);
    assert(snapshot.fps_source == FRAME_PACER_SOURCE_GLOBAL);
    assert(!snapshot.hud_enabled);
    assert(snapshot.hud_source == FRAME_PACER_SOURCE_GLOBAL);

    assert(snprintf(matching, sizeof(matching),
                    "global_fps_limit = 30\nhud = on\n"
                    "[Current renderer]\nexecutable = \"%s\"\n"
                    "fps_limit = 45\nthread_cpu_limit = 50%%\n",
                    fixture.limit.executable) > 0);
    write_text(fixture.path, matching);
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.fps_limit == 45);
    assert(snapshot.fps_source == FRAME_PACER_SOURCE_PER_GAME);
    assert(snapshot.reason == FRAME_PACER_REASON_NONE);
    assert(!strcmp(snapshot.matched_section, "Current renderer"));
    assert(!strcmp(snapshot.matched_executable, fixture.limit.executable));
    assert(snapshot.thread_cpu_enabled && snapshot.thread_cpu_percent == 50);
    assert(snapshot.thread_cpu_source == FRAME_PACER_SOURCE_PER_GAME);
    assert(frame_pacer_limit_hud_enabled(&fixture.limit));
    {
        bool enabled = false;
        assert(frame_pacer_limit_thread_cpu_quota(&fixture.limit, &enabled) ==
               50);
        assert(enabled);
    }

    write_text(fixture.path, "global_fps_limit = 30\n[Other]\n"
                             "executable = \"other.exe\"\nfps_limit = 45\n");
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.fps_limit == 30);
    assert(snapshot.fps_source == FRAME_PACER_SOURCE_GLOBAL);
    assert(snapshot.reason == FRAME_PACER_REASON_NO_EXECUTABLE_MATCH);
    assert(!snapshot.matched_section[0] && !snapshot.matched_executable[0]);

    write_text(fixture.path, "global_fps_limit = off\n");
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.status == FRAME_PACER_CONFIG_VALID);
    assert(snapshot.fps_source == FRAME_PACER_SOURCE_GLOBAL);
    assert(snapshot.reason == FRAME_PACER_REASON_EXPLICITLY_OFF);

    assert(snprintf(matching, sizeof(matching),
                    "global_fps_limit = 30\n[Disabled]\nexecutable = \"%s\"\n"
                    "fps_limit = off\nthread_cpu_limit = off\n",
                    fixture.limit.executable) > 0);
    write_text(fixture.path, matching);
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.fps_source == FRAME_PACER_SOURCE_PER_GAME);
    assert(snapshot.reason == FRAME_PACER_REASON_EXPLICITLY_OFF);
    assert(snapshot.thread_cpu_source == FRAME_PACER_SOURCE_PER_GAME);
    assert(!snapshot.thread_cpu_enabled);
    revision = snapshot.revision;
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.revision == revision);

    fixture_destroy(&fixture);
}

static void test_wine_launcher_handoff(void)
{
    static const char maps[] =
        "00400000-00401000 r--p 00001000 103:01 1 /games/ignored.exe\n"
        "140000000-140001000\tr--p\t00000000\t103:01 2\t"
        "/games/KINGDOM HEARTS FINAL MIX.exe\n"
        "6ffff000-70000000 r--p 00000000 103:01 3 /games/helper.dll\n";
    struct frame_pacer_effective_config snapshot;
    struct fixture fixture;
    char maps_template[] = "/tmp/frame-pacer-maps-XXXXXX";
    int fd = mkstemp(maps_template);

    assert(fd >= 0);
    assert(!close(fd));
    write_bytes(maps_template, maps, sizeof(maps) - 1);
    assert(!setenv("FRAME_PACER_TEST_PROC_MAPS", maps_template, 1));
    fixture_init(&fixture);
    assert(!strcmp(fixture.limit.executable, "KINGDOM HEARTS FINAL MIX.exe"));
    assert(snprintf(fixture.limit.executable_candidates[1],
                    FRAME_PACER_EXECUTABLE_MAX, "%s",
                    "KINGDOM HEARTS HD 1.5+2.5 ReMIX.exe") > 0);
    fixture.limit.executable_candidate_count = 2;

    write_text(fixture.path,
               "[Collection]\n"
               "executable = \"KINGDOM HEARTS HD 1.5+2.5 ReMIX.exe\"\n"
               "fps_limit = 60\n"
               "[Selected game]\n"
               "executable = \"KINGDOM HEARTS FINAL MIX.exe\"\n"
               "fps_limit = 45\n");
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.fps_limit == 45);
    assert(!strcmp(snapshot.renderer, "KINGDOM HEARTS FINAL MIX.exe"));
    assert(!strcmp(snapshot.matched_section, "Selected game"));
    assert(
        !strcmp(snapshot.matched_executable, "KINGDOM HEARTS FINAL MIX.exe"));

    write_text(fixture.path,
               "[Collection]\n"
               "executable = \"KINGDOM HEARTS HD 1.5+2.5 ReMIX.exe\"\n"
               "fps_limit = 60\n");
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.fps_limit == 60);
    assert(!strcmp(snapshot.renderer, "KINGDOM HEARTS FINAL MIX.exe"));
    assert(!strcmp(snapshot.matched_section, "Collection"));
    assert(!strcmp(snapshot.matched_executable,
                   "KINGDOM HEARTS HD 1.5+2.5 ReMIX.exe"));

    fixture_destroy(&fixture);
    assert(!unsetenv("FRAME_PACER_TEST_PROC_MAPS"));
    assert(!unlink(maps_template));
}

static void test_parser_taxonomy(void)
{
    struct frame_pacer_effective_config snapshot;
    struct fixture fixture;
    char duplicate[1800];
    const unsigned char invalid_bytes[] = "global_fps_limit = 30\n#\0suffix\n";

    fixture_init(&fixture);
    expect_failure(&fixture, "[broken\n", FRAME_PACER_REASON_INVALID_SECTION,
                   1);
    expect_failure(&fixture, "global_fps_limit 30\n",
                   FRAME_PACER_REASON_MISSING_EQUALS, 1);
    expect_failure(&fixture, "unknown = 30\n", FRAME_PACER_REASON_UNKNOWN_KEY,
                   1);
    expect_failure(&fixture, "hud = on\nhud = off\n",
                   FRAME_PACER_REASON_DUPLICATE_KEY, 2);
    expect_failure(&fixture, "global_fps_limit = 1000\n",
                   FRAME_PACER_REASON_INVALID_VALUE, 1);
    expect_failure(&fixture,
                   "[Bad executable]\nexecutable = \"a/b\"\nfps_limit = 30\n",
                   FRAME_PACER_REASON_INVALID_EXECUTABLE, 2);
    expect_failure(&fixture, "[Incomplete]\nexecutable = \"other.exe\"\n",
                   FRAME_PACER_REASON_INCOMPLETE_RULE, 1);
    assert(snprintf(duplicate, sizeof(duplicate),
                    "[First]\nexecutable = \"%s\"\nfps_limit = 30\n"
                    "[Second]\nexecutable = \"%s\"\nfps_limit = 40\n",
                    fixture.limit.executable, fixture.limit.executable) > 0);
    expect_failure(&fixture, duplicate,
                   FRAME_PACER_REASON_DUPLICATE_MATCHING_RULE, 4);

    write_bytes(fixture.path, invalid_bytes, sizeof(invalid_bytes) - 1);
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.status == FRAME_PACER_CONFIG_MALFORMED);
    assert(snapshot.reason == FRAME_PACER_REASON_INVALID_BYTE);
    assert(snapshot.error_line == 2);
    fixture_destroy(&fixture);
}

static void test_file_taxonomy(void)
{
    struct frame_pacer_effective_config snapshot;
    struct fixture fixture;
    char other[420];
    int fd;

    fixture_init(&fixture);
    write_text(fixture.path, "global_fps_limit = 30\n");
    assert(!chmod(fixture.path, 0640));
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.status == FRAME_PACER_CONFIG_INSECURE);
    assert(snapshot.reason == FRAME_PACER_REASON_INSECURE_PERMISSIONS);
    assert(!unlink(fixture.path));

    assert(!mkdir(fixture.path, 0700));
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.reason == FRAME_PACER_REASON_NOT_REGULAR_FILE);
    assert(!rmdir(fixture.path));

    assert(!symlink("target", fixture.path));
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.reason == FRAME_PACER_REASON_SYMBOLIC_LINK);
    assert(!unlink(fixture.path));

    write_text(fixture.path, "global_fps_limit = 30\n");
    assert(snprintf(other, sizeof(other), "%s.other", fixture.path) > 0);
    assert(!link(fixture.path, other));
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.reason == FRAME_PACER_REASON_MULTIPLE_HARD_LINKS);
    assert(!unlink(other));
    assert(!unlink(fixture.path));

    write_bytes(fixture.path, "", 0);
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.status == FRAME_PACER_CONFIG_MALFORMED);
    assert(snapshot.reason == FRAME_PACER_REASON_EMPTY_FILE);
    assert(!unlink(fixture.path));

    fd = open(fixture.path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(fd >= 0);
    assert(!ftruncate(fd, (off_t)FRAME_PACER_CONFIG_MAX_BYTES + 1));
    assert(!close(fd));
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.reason == FRAME_PACER_REASON_FILE_TOO_LARGE);
    fixture_destroy(&fixture);
}

static void test_injected_failures(void)
{
    static const struct {
        enum frame_pacer_limit_test_failure failure;
        enum frame_pacer_config_status status;
        enum frame_pacer_config_reason reason;
    } cases[] = {
        {FRAME_PACER_TEST_FAILURE_METADATA, FRAME_PACER_CONFIG_UNREADABLE,
         FRAME_PACER_REASON_METADATA_FAILED},
        {FRAME_PACER_TEST_FAILURE_WRONG_OWNER, FRAME_PACER_CONFIG_INSECURE,
         FRAME_PACER_REASON_WRONG_OWNER},
        {FRAME_PACER_TEST_FAILURE_OPEN, FRAME_PACER_CONFIG_UNREADABLE,
         FRAME_PACER_REASON_OPEN_FAILED},
        {FRAME_PACER_TEST_FAILURE_FSTAT, FRAME_PACER_CONFIG_UNREADABLE,
         FRAME_PACER_REASON_METADATA_FAILED},
        {FRAME_PACER_TEST_FAILURE_FINAL_FSTAT, FRAME_PACER_CONFIG_UNREADABLE,
         FRAME_PACER_REASON_METADATA_FAILED},
        {FRAME_PACER_TEST_FAILURE_READ, FRAME_PACER_CONFIG_UNREADABLE,
         FRAME_PACER_REASON_READ_FAILED},
        {FRAME_PACER_TEST_FAILURE_CHANGED, FRAME_PACER_CONFIG_UNREADABLE,
         FRAME_PACER_REASON_CHANGED_DURING_READ},
        {FRAME_PACER_TEST_FAILURE_CLOSE, FRAME_PACER_CONFIG_UNREADABLE,
         FRAME_PACER_REASON_CLOSE_FAILED},
        {FRAME_PACER_TEST_FAILURE_ALLOC, FRAME_PACER_CONFIG_UNREADABLE,
         FRAME_PACER_REASON_OUT_OF_MEMORY}};
    struct frame_pacer_effective_config snapshot;
    struct fixture fixture;
    size_t index;

    fixture_init(&fixture);
    write_text(fixture.path, "global_fps_limit = 30\n");
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        frame_pacer_limit_test_fail_at(&fixture.limit, cases[index].failure);
        snapshot = poll_snapshot(&fixture);
        assert(snapshot.status == cases[index].status);
        assert(snapshot.reason == cases[index].reason);
    }
    frame_pacer_limit_test_fail_at(&fixture.limit,
                                   FRAME_PACER_TEST_FAILURE_NONE);
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.status == FRAME_PACER_CONFIG_VALID &&
           snapshot.fps_limit == 30);

    fixture.limit.path[0] = '\0';
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.status == FRAME_PACER_CONFIG_UNREADABLE);
    assert(snapshot.reason == FRAME_PACER_REASON_CONFIG_PATH_UNAVAILABLE);
    fixture_destroy(&fixture);
}

static void test_semantic_revisions(void)
{
    struct frame_pacer_effective_config snapshot;
    struct fixture fixture;
    struct timespec times[2] = {{.tv_nsec = UTIME_NOW}, {.tv_nsec = UTIME_NOW}};
    char config[2400];
    uint64_t revision;

    fixture_init(&fixture);
    assert(snprintf(config, sizeof(config),
                    "[Current]\nexecutable = \"%s\"\nfps_limit = 30\n",
                    fixture.limit.executable) > 0);
    write_text(fixture.path, config);
    snapshot = poll_snapshot(&fixture);
    revision = snapshot.revision;

    assert(snprintf(config, sizeof(config),
                    "# formatting-only edit\n\n[Current]  \n"
                    " executable = \"%s\"\n fps_limit = 30\n",
                    fixture.limit.executable) > 0);
    write_text(fixture.path, config);
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.revision == revision);
    assert(!utimensat(AT_FDCWD, fixture.path, times, 0));
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.revision == revision);

    assert(snprintf(config, sizeof(config),
                    "[Irrelevant]\nexecutable = \"other.exe\"\nfps_limit = 90\n"
                    "[Current]\nexecutable = \"%s\"\nfps_limit = 30\n",
                    fixture.limit.executable) > 0);
    write_text(fixture.path, config);
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.revision == revision);

    assert(
        snprintf(config, sizeof(config),
                 "hud = off\n[Current]\nexecutable = \"%s\"\nfps_limit = 30\n",
                 fixture.limit.executable) > 0);
    write_text(fixture.path, config);
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.revision == ++revision && !snapshot.hud_enabled);

    assert(
        snprintf(config, sizeof(config),
                 "hud = off\n[Current]\nexecutable = \"%s\"\nfps_limit = 30\n"
                 "thread_cpu_limit = 50%%\n",
                 fixture.limit.executable) > 0);
    write_text(fixture.path, config);
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.revision == ++revision && snapshot.thread_cpu_enabled);

    write_text(fixture.path, "global_fps_limit = 30\nhud = off\n");
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.revision == ++revision);
    assert(snapshot.fps_limit == 30 &&
           snapshot.fps_source == FRAME_PACER_SOURCE_GLOBAL);

    assert(snprintf(fixture.limit.executable_candidates[1],
                    FRAME_PACER_EXECUTABLE_MAX, "%s", "launcher.exe") > 0);
    fixture.limit.executable_candidate_count = 2;
    write_text(fixture.path,
               "[Launcher]\nexecutable = \"launcher.exe\"\nfps_limit = 30\n");
    snapshot = poll_snapshot(&fixture);
    assert(snapshot.revision == ++revision);
    assert(!strcmp(snapshot.matched_executable, "launcher.exe"));
    fixture_destroy(&fixture);
}

static void test_reporting_disabled_fast_path(void)
{
    struct frame_pacer_effective_config snapshot;
    struct fixture fixture;
    bool enabled;

    fixture_init(&fixture);
    frame_pacer_limit_set_reporting_enabled(&fixture.limit, false);
    write_text(fixture.path, "global_fps_limit = 61\n"
                             "hud = off\n");
    assert(frame_pacer_limit_poll(&fixture.limit, fixture.now) == 61);
    assert(!frame_pacer_limit_hud_enabled(&fixture.limit));
    assert(frame_pacer_limit_thread_cpu_quota(&fixture.limit, &enabled) == 0);
    assert(!enabled);
    assert(frame_pacer_limit_revision(&fixture.limit) == 0);
    assert(!frame_pacer_limit_snapshot(&fixture.limit, &snapshot));

    frame_pacer_limit_set_reporting_enabled(&fixture.limit, true);
    fixture.now += FRAME_PACER_CONFIG_POLL_NS + 1;
    assert(frame_pacer_limit_poll(&fixture.limit, fixture.now) == 61);
    assert(frame_pacer_limit_snapshot(&fixture.limit, &snapshot));
    assert(snapshot.revision == 1);
    fixture_destroy(&fixture);
}

struct concurrent_context {
    struct frame_pacer_limit *limit;
    _Atomic bool failed;
};

static void *snapshot_thread(void *opaque)
{
    struct concurrent_context *context = opaque;
    unsigned int index;

    for (index = 0; index < 10000; ++index) {
        struct frame_pacer_effective_config snapshot;

        if (!frame_pacer_limit_snapshot(context->limit, &snapshot) ||
            !snapshot.revision || snapshot.fps_limit != 30 ||
            snapshot.status != FRAME_PACER_CONFIG_VALID)
            atomic_store_explicit(&context->failed, true, memory_order_relaxed);
    }
    return 0;
}

static void test_concurrent_snapshots(void)
{
    struct concurrent_context context;
    struct fixture fixture;
    pthread_t threads[8];
    size_t index;

    fixture_init(&fixture);
    write_text(fixture.path, "global_fps_limit = 30\n");
    (void)poll_snapshot(&fixture);
    context.limit = &fixture.limit;
    atomic_init(&context.failed, false);
    for (index = 0; index < sizeof(threads) / sizeof(threads[0]); ++index)
        assert(!pthread_create(&threads[index], 0, snapshot_thread, &context));
    for (index = 0; index < sizeof(threads) / sizeof(threads[0]); ++index)
        assert(!pthread_join(threads[index], 0));
    assert(!atomic_load_explicit(&context.failed, memory_order_relaxed));
    fixture_destroy(&fixture);
}

int main(void)
{
    test_selection_and_revisions();
    test_wine_launcher_handoff();
    test_parser_taxonomy();
    test_file_taxonomy();
    test_injected_failures();
    test_semantic_revisions();
    test_reporting_disabled_fast_path();
    test_concurrent_snapshots();
    puts("pacer limit tests passed");
    return 0;
}
