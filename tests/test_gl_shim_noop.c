#include <assert.h>
#include <dlfcn.h>

int main(void)
{
    assert(dlsym(RTLD_DEFAULT, "setenv"));
    return 0;
}
