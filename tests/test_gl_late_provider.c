#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static void *check_provider(int egl)
{
    void *provider =
        dlopen(egl ? "libEGL.so.1" : "libGLX.so.0", RTLD_NOW | RTLD_LOCAL);
    void (*(*resolve)(const char *))(void) = 0;
    void (*expected)(void) = 0;
    void *symbol;

    assert(provider);
    symbol = dlsym(provider, egl ? "eglGetProcAddress" : "glXGetProcAddress");
    assert(symbol);
    memcpy(&resolve, &symbol, sizeof(resolve));
    symbol = dlsym(provider, "frame_pacer_test_marker");
    assert(symbol);
    memcpy(&expected, &symbol, sizeof(expected));
    assert(resolve("glGetString") == expected);
    return provider;
}

int main(int argc, char **argv)
{
    void *first, *second;
    assert(argc == 2);
    first = check_provider(!strcmp(argv[1], "egl"));
    second = check_provider(strcmp(argv[1], "egl") != 0);
    assert(dlclose(second) == 0);
    assert(dlclose(first) == 0);
    puts("late GLX/EGL provider loading passed");
    return 0;
}
