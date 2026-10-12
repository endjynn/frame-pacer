#include <EGL/egl.h>
#include <GL/glx.h>
#include <string.h>

void frame_pacer_test_marker(void);
void frame_pacer_test_marker(void) {}

#ifdef FRAME_PACER_TEST_EGL
__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *name)
{
    return !strcmp(name, "glGetString") ? frame_pacer_test_marker : 0;
}
#else
__GLXextFuncPtr glXGetProcAddress(const GLubyte *name)
{
    return !strcmp((const char *)name, "glGetString") ? frame_pacer_test_marker
                                                      : 0;
}
#endif
