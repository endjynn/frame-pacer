#include "pacer_compatibility.h"

#include <strings.h>

struct frame_pacer_compatibility_rule {
    const char *executable;
    enum frame_pacer_quiet_submit_policy quiet_submit;
};

/* Add a rule only with a documented reproduction and focused, unfocused, and
 * refocus validation. Windows executable names are case-insensitive. */
static const struct frame_pacer_compatibility_rule rules[] = {
    {"nwn2.exe", FRAME_PACER_QUIET_SUBMIT_PACE_EVERY},
    {"Risk of Rain Returns.exe", FRAME_PACER_QUIET_SUBMIT_FORWARD},
};

enum frame_pacer_quiet_submit_policy
frame_pacer_compatibility_quiet_submit_policy(const char *executable)
{
    size_t index;

    if (!executable || !*executable)
        return FRAME_PACER_QUIET_SUBMIT_PACE_EVERY;
    for (index = 0; index < sizeof(rules) / sizeof(rules[0]); ++index) {
        if (!strcasecmp(executable, rules[index].executable))
            return rules[index].quiet_submit;
    }
    return FRAME_PACER_QUIET_SUBMIT_PACE_EVERY;
}
