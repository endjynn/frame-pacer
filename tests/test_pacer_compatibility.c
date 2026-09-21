#include "pacer_compatibility.h"

#include <assert.h>

int main(void)
{
    assert(frame_pacer_compatibility_shader_worker("fossilize_replay"));
    assert(frame_pacer_compatibility_shader_worker(
        "/steam/ubuntu12_64/fossilize_replay"));
    assert(frame_pacer_compatibility_shader_worker(
        "/steam/ubuntu12_32/fossilize_replay"));
    assert(!frame_pacer_compatibility_shader_worker("fossilize_replay_game"));
    assert(!frame_pacer_compatibility_shader_worker("fossilize_replay.exe"));
    assert(!frame_pacer_compatibility_shader_worker("FOSSILIZE_REPLAY"));
    assert(!frame_pacer_compatibility_shader_worker("/fossilize_replay/game"));
    assert(!frame_pacer_compatibility_shader_worker(""));
    assert(!frame_pacer_compatibility_shader_worker(0));
    assert(frame_pacer_compatibility_quiet_submit_policy("nwn2.exe") ==
           FRAME_PACER_QUIET_SUBMIT_PACE_EVERY);
    assert(frame_pacer_compatibility_quiet_submit_policy(
               "RISK OF RAIN RETURNS.EXE") == FRAME_PACER_QUIET_SUBMIT_FORWARD);
    assert(frame_pacer_compatibility_quiet_submit_policy("unknown-game") ==
           FRAME_PACER_QUIET_SUBMIT_PACE_EVERY);
    assert(frame_pacer_compatibility_quiet_submit_policy(0) ==
           FRAME_PACER_QUIET_SUBMIT_PACE_EVERY);
    return 0;
}
