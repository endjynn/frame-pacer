#define _GNU_SOURCE
#include "thread_cpu_systemd.h"

#include <dlfcn.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct sd_bus_error {
    const char *name;
    const char *message;
    int need_free;
};

static bool symbol(void *library, const char *name, void *output, size_t size)
{
    void *address = dlsym(library, name);

    if (!address || size != sizeof(address))
        return false;
    memcpy(output, &address, size);
    return true;
}

void frame_pacer_systemd_close(struct frame_pacer_systemd *systemd)
{
    if (!systemd)
        return;
    if (systemd->bus && systemd->bus_unref)
        systemd->bus_unref(systemd->bus);
    if (systemd->library)
        (void)dlclose(systemd->library);
    memset(systemd, 0, sizeof(*systemd));
}

bool frame_pacer_systemd_open(struct frame_pacer_systemd *systemd)
{
    char address[80];
    int written;

    if (!systemd)
        return false;
    memset(systemd, 0, sizeof(*systemd));
    systemd->library = dlopen("libsystemd.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!systemd->library)
        return false;
#define LOAD(member_, name_)                                                   \
    if (!symbol(systemd->library, name_, &systemd->member_,                    \
                sizeof(systemd->member_)))                                     \
    goto fail
    LOAD(open_user, "sd_bus_open_user");
    LOAD(bus_new, "sd_bus_new");
    LOAD(bus_set_address, "sd_bus_set_address");
    LOAD(bus_start, "sd_bus_start");
    LOAD(bus_unref, "sd_bus_unref");
    LOAD(message_new_method_call, "sd_bus_message_new_method_call");
    LOAD(message_append, "sd_bus_message_append");
    LOAD(message_open_container, "sd_bus_message_open_container");
    LOAD(message_close_container, "sd_bus_message_close_container");
    LOAD(message_unref, "sd_bus_message_unref");
    LOAD(bus_call, "sd_bus_call");
    LOAD(error_free, "sd_bus_error_free");
#undef LOAD
    if (systemd->open_user(&systemd->bus) >= 0)
        return true;
    if (systemd->bus) {
        systemd->bus_unref(systemd->bus);
        systemd->bus = 0;
    }
    written = snprintf(address, sizeof(address), "unix:path=/run/user/%ju/bus",
                       (uintmax_t)getuid());
    if (written < 0 || (size_t)written >= sizeof(address) ||
        systemd->bus_new(&systemd->bus) < 0 || !systemd->bus ||
        systemd->bus_set_address(systemd->bus, address) < 0 ||
        systemd->bus_start(systemd->bus) < 0)
        goto fail;
    return true;
fail:
    frame_pacer_systemd_close(systemd);
    return false;
}

static bool property_prefix(const struct frame_pacer_systemd *systemd,
                            struct sd_bus_message *message, const char *name,
                            const char *signature)
{
    return systemd->message_open_container(message, 'r', "sv") >= 0 &&
           systemd->message_append(message, "s", name) >= 0 &&
           systemd->message_open_container(message, 'v', signature) >= 0;
}

static bool property_bool(const struct frame_pacer_systemd *systemd,
                          struct sd_bus_message *message, const char *name,
                          int value)
{
    return property_prefix(systemd, message, name, "b") &&
           systemd->message_append(message, "b", value) >= 0 &&
           systemd->message_close_container(message) >= 0 &&
           systemd->message_close_container(message) >= 0;
}

static bool property_string(const struct frame_pacer_systemd *systemd,
                            struct sd_bus_message *message, const char *name,
                            const char *value)
{
    return property_prefix(systemd, message, name, "s") &&
           systemd->message_append(message, "s", value) >= 0 &&
           systemd->message_close_container(message) >= 0 &&
           systemd->message_close_container(message) >= 0;
}

static bool property_pid(const struct frame_pacer_systemd *systemd,
                         struct sd_bus_message *message, pid_t process_id)
{
    uint32_t pid;

    if (process_id <= 0 || (uintmax_t)process_id > UINT32_MAX)
        return false;
    pid = (uint32_t)process_id;
    return property_prefix(systemd, message, "PIDs", "au") &&
           systemd->message_open_container(message, 'a', "u") >= 0 &&
           systemd->message_append(message, "u", pid) >= 0 &&
           systemd->message_close_container(message) >= 0 &&
           systemd->message_close_container(message) >= 0 &&
           systemd->message_close_container(message) >= 0;
}

static bool property_exec(const struct frame_pacer_systemd *systemd,
                          struct sd_bus_message *message, const char *path,
                          const char *const arguments[])
{
    unsigned int index;

    if (!property_prefix(systemd, message, "ExecStart", "a(sasb)") ||
        systemd->message_open_container(message, 'a', "(sasb)") < 0 ||
        systemd->message_open_container(message, 'r', "sasb") < 0 ||
        systemd->message_append(message, "s", path) < 0 ||
        systemd->message_open_container(message, 'a', "s") < 0)
        return false;
    for (index = 0; arguments[index]; ++index)
        if (systemd->message_append(message, "s", arguments[index]) < 0)
            return false;
    return systemd->message_close_container(message) >= 0 &&
           systemd->message_append(message, "b", 0) >= 0 &&
           systemd->message_close_container(message) >= 0 &&
           systemd->message_close_container(message) >= 0 &&
           systemd->message_close_container(message) >= 0 &&
           systemd->message_close_container(message) >= 0;
}

static bool call_start(struct frame_pacer_systemd *systemd,
                       struct sd_bus_message *message)
{
    struct sd_bus_message *reply = 0;
    struct sd_bus_error error = {0};
    bool started =
        systemd->bus_call(systemd->bus, message, 500000, &error, &reply) >= 0;

    systemd->error_free(&error);
    if (reply)
        systemd->message_unref(reply);
    return started;
}

bool frame_pacer_systemd_start_scope(struct frame_pacer_systemd *systemd,
                                     const char *name, pid_t process_id)
{
    struct sd_bus_message *message = 0;
    bool started = false;

    if (!systemd || !systemd->bus || !name)
        return false;
    if (systemd->message_new_method_call(
            systemd->bus, &message, "org.freedesktop.systemd1",
            "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager",
            "StartTransientUnit") >= 0 &&
        systemd->message_append(message, "ss", name, "fail") >= 0 &&
        systemd->message_open_container(message, 'a', "(sv)") >= 0 &&
        property_pid(systemd, message, process_id) &&
        property_bool(systemd, message, "Delegate", 1) &&
        property_bool(systemd, message, "CPUAccounting", 1) &&
        property_string(systemd, message, "CollectMode",
                        "inactive-or-failed") &&
        systemd->message_close_container(message) >= 0 &&
        systemd->message_open_container(message, 'a', "(sa(sv))") >= 0 &&
        systemd->message_close_container(message) >= 0)
        started = call_start(systemd, message);
    if (message)
        systemd->message_unref(message);
    return started;
}

bool frame_pacer_systemd_start_service(struct frame_pacer_systemd *systemd,
                                       const char *unit, const char *path,
                                       const char *const arguments[])
{
    struct sd_bus_message *message = 0;
    bool started = false;

    if (!systemd || !systemd->bus || !unit || !path || !arguments)
        return false;
    if (systemd->message_new_method_call(
            systemd->bus, &message, "org.freedesktop.systemd1",
            "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager",
            "StartTransientUnit") >= 0 &&
        systemd->message_append(message, "ss", unit, "fail") >= 0 &&
        systemd->message_open_container(message, 'a', "(sv)") >= 0 &&
        property_exec(systemd, message, path, arguments) &&
        property_string(systemd, message, "Type", "exec") &&
        property_string(systemd, message, "CollectMode",
                        "inactive-or-failed") &&
        systemd->message_close_container(message) >= 0 &&
        systemd->message_open_container(message, 'a', "(sa(sv))") >= 0 &&
        systemd->message_close_container(message) >= 0)
        started = call_start(systemd, message);
    if (message)
        systemd->message_unref(message);
    return started;
}
