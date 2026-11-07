/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SC2_HOOKS_H
#define SC2_HOOKS_H

#include "runtime.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Game hooks that wrap converted functions (sc2_hooks.c). */
RecompFunction recomp_sc2_hooks_lookup_manual(uint32_t address);

/* The scene the game last switched to (0x502A0: 5 title, 6 menus, 7 attract
   demo, 16 fight) and how many switches there have been, for scripted input
   timed from a scene's start (runner --input-scene). */
extern uint32_t sc2_scene;
extern uint32_t sc2_scene_switches;
/* How many times the fight has stepped (0x4A090): not while the game's own pause holds the fight */
extern uint32_t sc2_fight_steps;
/* A press asking the movie playing to end (sc2_menu.cpp), taken by the movie scene's update */
extern int sc2_movie_skip;

/* Online play: the host's seed for the game's random numbers, used in place of the clock the game seeds them from
   (sc2_netplay.h), when sc2_net_seeded. */
extern uint32_t sc2_net_seed;
extern int sc2_net_seeded;
/* Online, from the match's start: the reads of the controls so far, which the game's clock follows */
extern int sc2_net_live;
extern uint64_t sc2_net_reads;

/* The free camera's freeze this frame (scene_freeze_adapter.c): none, the frame held, or one frame let through */
enum { SC2_FREEZE_NONE, SC2_FREEZE_HOLD, SC2_FREEZE_STEP };
int sc2_freeze_frame_action(void);
/* The start of each frame, where save states are taken and put back (sc2_frame.cpp); online, the runner's step
   there (rollback, runner.cpp netFrame) */
void sc2_frame_begins(void);
extern void (*sc2_net_frame)(void);
/* Frames not shown and not paced (d3d8_adapter.c): online, those played again after a rollback; offline, those
   above full speed */
extern int sc2_fast_forward;
/* The game's main thread (recomp_kernel_current_thread_id), the one whose frames those are: only its waits for the
   display are let through then */
extern uint32_t sc2_frame_thread;

/* The host's side of what the game's run depends on, kept with a save state (sc2_state.h): the reads online (the
   clocks follow them) and the sound's random numbers */
typedef struct Sc2HostState {
    uint64_t net_reads;
    uint32_t own_rand;
} Sc2HostState;
void sc2_host_state_get(Sc2HostState *state);
/* Training's hit boxes, taken as the frame's 2D layer begins (sc2_training.cpp) */
void sc2_training_hit_boxes(void);
void sc2_host_state_set(const Sc2HostState *state);


#ifdef __cplusplus
}
#endif

#endif
