/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The player's key and button layout (sc2_settings.cpp's table, as
   settings.ini chooses it) and which gamepad is which player's (the
   launcher's Controllers page: [controllers]) given to the runtime's host
   input. */
#include "sc2_settings.h"

#include "input_host_win32.h"

#include <cstdio>
#include <cstring>

namespace sc2 {

namespace {

recomp::PadChoices &PadChoices()
{
    static recomp::PadChoices choices;
    return choices;
}

/* (the host input's RecompInputHostAssign: called when a gamepad comes or
   goes) Those chosen for a player play that player's port, the rest the
   ports left in the order they were connected, so with nothing chosen each
   gamepad plays the port of its seat, as before; one chosen as nobody's
   plays nothing. */
void AssignPads(SDL_Gamepad *const *seats, uint32_t count, SDL_Gamepad **ports)
{
    SDL_Gamepad *players[recomp::kBindMaxPlayers] = {};
    const int seatCount = static_cast<int>(count);
    const int portCount = static_cast<int>(RECOMP_INPUT_PORT_COUNT) < recomp::kBindMaxPlayers
        ? static_cast<int>(RECOMP_INPUT_PORT_COUNT) : recomp::kBindMaxPlayers;
    for (int seat = 0; seat < seatCount; ++seat) {
        if (seats[seat] == nullptr) continue;
        const std::string identity = recomp::PadIdentity(SDL_GetGamepadID(seats[seat]));
        std::fprintf(stderr, "sc2 input: gamepad in seat %d is %s, chosen %s\n", seat, identity.c_str(),
            recomp::PadChoiceText(PadChoices().of(identity)).c_str());
    }
    recomp::AssignPads(PadChoices(), seats, seatCount, portCount, players);
    for (int port = 0; port < portCount; ++port) ports[port] = players[port];
}

} // namespace

void ApplyBindings(const std::string &path)
{
    static const struct { const char *name; RecompInputControl control; } kControls[] = {
        {"up", RECOMP_INPUT_CONTROL_UP}, {"down", RECOMP_INPUT_CONTROL_DOWN},
        {"left", RECOMP_INPUT_CONTROL_LEFT}, {"right", RECOMP_INPUT_CONTROL_RIGHT},
        {"start", RECOMP_INPUT_CONTROL_START}, {"back", RECOMP_INPUT_CONTROL_BACK},
        {"a", RECOMP_INPUT_CONTROL_A}, {"b", RECOMP_INPUT_CONTROL_B},
        {"x", RECOMP_INPUT_CONTROL_X}, {"y", RECOMP_INPUT_CONTROL_Y},
        {"black", RECOMP_INPUT_CONTROL_BLACK}, {"white", RECOMP_INPUT_CONTROL_WHITE},
        {"ltrigger", RECOMP_INPUT_CONTROL_LEFT_TRIGGER}, {"rtrigger", RECOMP_INPUT_CONTROL_RIGHT_TRIGGER},
    };
    const recomp::BindTable &table = BindingTable();
    const recomp::Bindings bindings = recomp::LoadBindings(path, table);
    RecompInputHostLayout layout;
    for (uint32_t port = 0; port < RECOMP_INPUT_PORT_COUNT; ++port) {
        for (uint32_t control = 0; control < RECOMP_INPUT_CONTROL_COUNT; ++control)
            layout.controls[port][control] = {-1, -1, -1};
    }
    for (int action = 0; action < table.count; ++action) {
        for (const auto &entry : kControls) {
            if (std::strcmp(entry.name, table.actions[action].name) != 0) continue;
            for (int player = 0; player < table.players; ++player) {
                RecompInputHostBinding &binding = layout.controls[player][entry.control];
                const SDL_Scancode key = bindings.key[player][action];
                binding.scancode = key != SDL_SCANCODE_UNKNOWN ? static_cast<int16_t>(key) : -1;
                binding.pad_button = static_cast<int16_t>(bindings.pad[player][action].button);
                binding.pad_axis = static_cast<int16_t>(bindings.pad[player][action].axis);
            }
        }
    }
    recomp_input_host_set_layout(&layout);
    PadChoices() = recomp::LoadPadChoices(path);
    recomp_input_host_set_assign(AssignPads);
}

} // namespace sc2
