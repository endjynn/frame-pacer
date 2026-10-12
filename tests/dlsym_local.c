#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>

int frame_pacer_local_value = 17;
int frame_pacer_local_probe(void);

int frame_pacer_local_probe(void)
{
    int *value = dlsym(RTLD_DEFAULT, "frame_pacer_local_value");
    assert(value == &frame_pacer_local_value);
    return *value;
}
