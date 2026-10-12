#ifndef FRAME_PACER_GL_DLSYM_H
#define FRAME_PACER_GL_DLSYM_H

typedef void *(*frame_pacer_dlsym_fn)(void *, const char *);

__attribute__((visibility("hidden"))) frame_pacer_dlsym_fn
frame_pacer_dlsym_target(void *handle, const char *name);

#endif
