// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void sc2_scene_probe_skin(uint32_t model);
struct RecompD3dPresenterDrawCommand;
void sc2_scene_attach(struct RecompD3dPresenterDrawCommand *draw, uint32_t position_address);
#ifdef __cplusplus
}
#endif
