#define _GNU_SOURCE
#include <GL/glx.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    const char *probe = getenv("FRAME_PACER_TEST_MISSING_PRESENT");

    assert(dlsym(RTLD_DEFAULT, "setenv"));
    if (probe && !strcmp(probe, "glx")) {
        void *symbol = dlsym(RTLD_DEFAULT, "glXSwapBuffers");
        void (*swap)(Display *, GLXDrawable);

        assert(symbol);
        memcpy(&swap, &symbol, sizeof(swap));
        swap((Display *)1, 0);
    }
    return 0;
}
