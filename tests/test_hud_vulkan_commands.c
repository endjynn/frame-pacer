#include "hud_vulkan_commands.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static unsigned int missing_command = FRAME_PACER_HUD_REQUIRED_COMMAND_COUNT;
static unsigned int calls;

static PFN_vkVoidFunction VKAPI_CALL fake_get_proc(VkDevice device,
                                                   const char *name)
{
    unsigned int index;

    (void)device;
    for (index = 0; index < FRAME_PACER_HUD_REQUIRED_COMMAND_COUNT; ++index) {
        if (!strcmp(name, frame_pacer_hud_required_command_name(index))) {
            ++calls;
            return index == missing_command
                       ? 0
                       : (PFN_vkVoidFunction)(uintptr_t)(index + 1);
        }
    }
    assert(0);
    return 0;
}

int main(void)
{
    struct frame_pacer_hud_commands commands;
    VkDevice device = (VkDevice)(uintptr_t)1;

    calls = 0;
    assert(frame_pacer_hud_resolve_commands(&commands, fake_get_proc, device));
    assert(calls == FRAME_PACER_HUD_REQUIRED_COMMAND_COUNT);
    assert(commands.functions[0]);
    assert(commands.functions[FRAME_PACER_HUD_REQUIRED_COMMAND_COUNT - 1]);

    missing_command = FRAME_PACER_HUD_COMMAND_CREATE_SHADER_MODULE;
    calls = 0;
    assert(!frame_pacer_hud_resolve_commands(&commands, fake_get_proc, device));
    assert(calls == missing_command + 1);
    assert(!commands.functions[0]);
    return 0;
}
