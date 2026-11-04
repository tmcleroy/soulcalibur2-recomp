// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "d3d_warmup_recipe.h"
#include <string>
#include <vector>
namespace sc2 {
// Call only after the runner authenticates and loads the supported executable.
std::vector<RecompWarmShader> GraphicsInventory(const std::vector<uint8_t> &memory);
bool PrepareGraphicsInventory(const std::vector<uint8_t> &memory, const std::string &folder);
}
