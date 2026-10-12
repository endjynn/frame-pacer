int frame_pacer_lookup_probe(void);
unsigned int eglTerminate(void *display);

int frame_pacer_lookup_probe(void)
{
    return 40;
}

unsigned int eglTerminate(void *display)
{
    (void)display;
    return 40;
}
