#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <string.h>

int frame_pacer_lookup_probe(void);
unsigned int eglTerminate(void *display);

unsigned int eglTerminate(void *display)
{
    unsigned int (*next)(void *) = 0;
    void *symbol = dlsym(RTLD_NEXT, "eglTerminate");

    assert(symbol);
    memcpy(&next, &symbol, sizeof(next));
    assert(next != eglTerminate);
    return next(display) + 1;
}

int frame_pacer_lookup_probe(void)
{
    int (*next)(void) = 0;
    void *symbol = dlsym(RTLD_NEXT, "frame_pacer_lookup_probe");

    assert(symbol);
    memcpy(&next, &symbol, sizeof(next));
    /* libjsig instead locks its mutex twice and hangs when next is itself. */
    assert(next != frame_pacer_lookup_probe);
    /* Known graphics symbols must follow NEXT, not substitute the renderer. */
    assert(eglTerminate(0) == (unsigned int)(next() + 1));
    (void)dlerror();
    assert(!dlsym(RTLD_NEXT, "frame_pacer_lookup_missing"));
    assert(dlerror());
    assert(!dlerror());
    return next() + 1;
}
