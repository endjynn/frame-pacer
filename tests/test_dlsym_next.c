#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>

int main(int argc, char **argv)
{
    int (*probe)(void) = 0;
    void *symbol;
    void *local;
    const struct rlimit no_core = {0, 0};

    assert(setrlimit(RLIMIT_CORE, &no_core) == 0);
    assert(argc == 2);

    (void)dlerror();
    symbol = dlsym(RTLD_DEFAULT, "frame_pacer_lookup_probe");
    assert(symbol);
    memcpy(&probe, &symbol, sizeof(probe));
    assert(probe() == 42);
    assert(!dlerror());
    assert(!dlsym(RTLD_DEFAULT, "frame_pacer_lookup_missing"));
    assert(dlerror());
    assert(!dlerror());
    local = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    assert(local);
    symbol = dlsym(local, "frame_pacer_local_probe");
    assert(symbol);
    memcpy(&probe, &symbol, sizeof(probe));
    assert(probe() == 17);
    assert(!dlerror());
    assert(!dlsym(RTLD_DEFAULT, "frame_pacer_local_value"));
    assert(dlerror());
    assert(dlclose(local) == 0);
    puts("caller-relative dlsym chain passed");
    return 0;
}
