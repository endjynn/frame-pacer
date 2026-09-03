#ifndef FRAME_PACER_THREAD_CPU_SYSTEMD_H
#define FRAME_PACER_THREAD_CPU_SYSTEMD_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

struct sd_bus;
struct sd_bus_message;

struct frame_pacer_systemd {
    void *library;
    struct sd_bus *bus;
    int (*open_user)(struct sd_bus **);
    int (*bus_new)(struct sd_bus **);
    int (*bus_set_address)(struct sd_bus *, const char *);
    int (*bus_start)(struct sd_bus *);
    struct sd_bus *(*bus_unref)(struct sd_bus *);
    int (*message_new_method_call)(struct sd_bus *, struct sd_bus_message **,
                                   const char *, const char *, const char *,
                                   const char *);
    int (*message_append)(struct sd_bus_message *, const char *, ...);
    int (*message_open_container)(struct sd_bus_message *, char, const char *);
    int (*message_close_container)(struct sd_bus_message *);
    struct sd_bus_message *(*message_unref)(struct sd_bus_message *);
    int (*bus_call)(struct sd_bus *, struct sd_bus_message *, uint64_t, void *,
                    struct sd_bus_message **);
    void (*error_free)(void *);
};

__attribute__((visibility("hidden"))) bool
frame_pacer_systemd_open(struct frame_pacer_systemd *);
__attribute__((visibility("hidden"))) void
frame_pacer_systemd_close(struct frame_pacer_systemd *);
__attribute__((visibility("hidden"))) bool
frame_pacer_systemd_start_scope(struct frame_pacer_systemd *, const char *name,
                                pid_t process_id);
__attribute__((visibility("hidden"))) bool
frame_pacer_systemd_start_service(struct frame_pacer_systemd *,
                                  const char *unit, const char *path,
                                  const char *const arguments[]);

#endif
