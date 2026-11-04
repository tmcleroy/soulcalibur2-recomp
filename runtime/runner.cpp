/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Soulcalibur II runner: DOA2U's runner without its game hooks (profile
   unlocks, Story finale, dedication card), with the options the launcher
   starts the game with:

     --disc IMAGE      the player's Xbox disc image, read in place (instead of
                       --xbe and --disc-root, a folder of the disc's files)
     --settings FILE   settings.ini: the features and the key and button layout
     --storage DIR     the hard disk (saves) and the save journal; without it,
                       <disc root>/.recomp-storage
     --disc-file NAME=FILE  the game reads FILE for the disc's NAME
     --guest-link FILE Link in the guest's place: FILE is the root.olk built
                       from the player's discs (tools/guests/build_guest.py)
     --guest-heihachi FILE  Heihachi in the guest's place, the same way
     --guests FILE     Link and Heihachi beside Spawn: FILE is the root.olk
                       built by tools/guests/roster.py
     --fullscreen  --widescreen (the game's own 16:9 mode)  --mute
     --free-camera     ` or the right stick's click flies the camera
     --quick-match P1:C1:P2:C2[:STAGE]  the first attract demo fights these
                       (ids as in the select grid: Link 27, Heihachi 28)
     --input-scene SCENE[#N]:POLL:KEY  presses KEY POLL polls into scene SCENE
     --host [PORT]  --join CODE|ADDRESS[:PORT]  online play (sc2_netplay.h), with
                       --net-name NAME --net-delay FRAMES --net-list --net-service URL|off
                     --net-invite TO:FROM --net-invite-name NAME (a player in the launcher's waiting room invited)
     --list-features   prints the features for the launcher and ends
     --menu-shot PRESENT:KEYS:FILE.bmp  tests the in-game menu (sc2_menu.h) */
#include "host_diagnostics.h"
#include "d3d8_adapter.h"
#include "dsound_api_adapter.h"
#include "free_camera_adapter.h"
#include "input_adapter.h"
#include "input_host_win32.h"
#include "input_pulse_source.h"
#include "runner_load.h"
#include "save_transaction.h"
#include "sc2_settings.h"
#include "sc2_graphics_inventory.h"
#include "d3d_latency_trace.h"
#include "sc2_menu.h"
#include "sc2_training.h"
#include "sc2_guest.h"
#include "sc2_hooks.h"
#include "sc2_netplay.h"
#include "sc2_version.h"
#include "sc2_xbe.h"
#include "../recomp-common/launcher/src/online.h"
#include "sc2_online.h"
#include "input_keyboard_win32.h"
#include "sc2_state.h"

extern "C" {
uint32_t recomp_d3d8_swap_counter(void);
void sc2_d3d8_set_swap_counter(uint32_t counter);
}
#include "overlay_text.h"
#include "audio_output.h"
#include "disc_image.h"
#include "d3d_vblank.h"
#include "xapi_time_adapter.h"
#include "xbox_memory_layout.h"
extern "C" {
#include "kernel_abi.h"
#include "program_manual.h"
#include "stop_report.h"
RecompFunction recomp_program_lookup(std::uint32_t address);
}

#include <SDL3/SDL.h>
#ifdef __ANDROID__
#include <SDL3/SDL_main.h> /* (main is the library's SDL_main, which the activity calls) */
#include "sc2_android.h"
#endif
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <future>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <cctype>
#include <string>
#include <vector>
#include <charconv>
#ifndef _WIN32
#include <strings.h>
#define _stricmp strcasecmp
#endif
#ifdef _WIN32
static int setenv(const char *name, const char *value, int overwrite)
{
    if (!overwrite && std::getenv(name) != nullptr) return 0;
    return _putenv_s(name, value);
}
#endif

static RecompInputPulseSource inputPulses;
static bool scriptedInputOnly;

/* --input-scene SCENE[#N]:POLL:KEY presses KEY (A, B, X, Y, start, back, up,
   down, left, right) for six polls, POLL polls after the game switched to
   SCENE for the Nth time (sc2_hooks.h). Timed from the scene, not from boot,
   a menu walk does not drift with load times. */
struct ScenePress {
    std::uint32_t scene, occurrence;
    std::uint64_t poll;
    std::uint16_t buttons;
    int analog;   // A, B, X or Y (RECOMP_INPUT_ANALOG_*), or -1
};
static std::vector<ScenePress> scenePresses;

static bool parseScenePress(const std::string &text, ScenePress &press)
{
    unsigned scene = 0, occurrence = 1;
    unsigned long long poll = 0;
    char key[16] = {};
    if (std::sscanf(text.c_str(), "%u#%u:%llu:%15s", &scene, &occurrence, &poll, key) != 4) {
        occurrence = 1;
        if (std::sscanf(text.c_str(), "%u:%llu:%15s", &scene, &poll, key) != 3) return false;
    }
    static const struct { const char *name; std::uint16_t buttons; int analog; } keys[] = {
        {"up", 0x1, -1}, {"down", 0x2, -1}, {"left", 0x4, -1}, {"right", 0x8, -1}, {"start", 0x10, -1},
        {"back", 0x20, -1}, {"A", 0, RECOMP_INPUT_ANALOG_A}, {"B", 0, RECOMP_INPUT_ANALOG_B},
        {"X", 0, RECOMP_INPUT_ANALOG_X}, {"Y", 0, RECOMP_INPUT_ANALOG_Y}};
    for (const auto &k : keys) {
        if (_stricmp(k.name, key) == 0) {
            press = {scene, occurrence, poll, k.buttons, k.analog};
            return true;
        }
    }
    return false;
}

static void applyScenePresses(RecompInputGamepad *gamepad)
{
    static std::uint32_t seenSwitches;
    /* Timed from when the scene became current ([0x398700], after its fade and load): a scene the game asks for
       (0x502A0) can turn out another (VS Battle's character select asks for 6 and is 10). */
    static std::uint32_t scene = ~0u;
    static std::uint64_t scenePoll;
    static std::map<std::uint32_t, std::uint32_t> entries;
    const std::uint32_t current = *recomp_memory_u32(0x398700u);
    if (current != scene || sc2_scene_switches != seenSwitches) {
        if (current != scene || (sc2_scene_switches != seenSwitches && sc2_scene == current)) {
            scene = current;
            scenePoll = 0;
            ++entries[scene];
            std::fprintf(stderr, "recomp input-scene: scene %u#%u\n", scene, entries[scene]);
        }
        seenSwitches = sc2_scene_switches;
    }
    for (const ScenePress &press : scenePresses) {
        if (press.scene != scene || press.occurrence != entries[scene] || scenePoll < press.poll ||
            scenePoll >= press.poll + 6u)
            continue;
        gamepad->buttons |= press.buttons;
        if (press.analog >= 0) gamepad->analog_buttons[press.analog] = 0xff;
    }
    ++scenePoll;
}

/* Online play (sc2_netplay.h): each read of controller 1 is a read of the match, whose controls are swapped with
   the other side's; controller 2 is the other player, as the same read had them. */
static std::unique_ptr<sc2::Netplay> netplay;
static std::uint64_t netReads;
static RecompInputGamepad netPlayers[2];

/* What the two games are compared by every Netplay::kHashEvery reads: the scene, both fighters' places, and the
   state of the game's two random number generators (rand at 0x3706F0; the Mersenne Twister at 0x372DF8, its
   place at 0x193C60). */
static std::uint32_t matchHash()
{
    std::uint32_t h = 2166136261u;
    const auto mix = [&h](const std::uint8_t *p, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 16777619u;
    };
    mix(reinterpret_cast<const std::uint8_t *>(&sc2_scene), 4u);
    /* (the fighters' places in a fight only: elsewhere they are what the last fight, the attract demo's say, left,
       which a host waiting in the attract mode has and the other does not; a fight sets them afresh) */
    if (*recomp_memory_u32(0x398700u) == 7u) {
        mix(recomp_memory(0x399220u + 0x2920u, 12u), 12u);
        mix(recomp_memory(0x399220u + 0x2920u + 0x10E60u, 12u), 12u);
    }
    mix(recomp_memory(0x3706F0u, 4u), 4u);
    mix(recomp_memory(0x193C60u, 4u), 4u);
    mix(recomp_memory(0x372DF8u, 624u * 4u), 624u * 4u);
    return h;
}

/* The match begins at the first read with the title screen up (scene 5 current): before that each game boots on its
   own, which takes its own time. There both seed the game's random numbers from the match's seed (rand's state,
   and the Mersenne Twister through its seeding routine 0x1B460), so the two are alike from that read on. */
static bool netLive;      // the match has begun (both games at the title screen, the state taken)
static bool netConnected; // the two games have met (the host waits in the background, its game playing)

/* The game's state that boot leaves different on each machine, by how long loading took there: the host's is
   taken by the other side as the match begins (Netplay::SyncState). rand's state; the Mersenne Twister's index;
   its state and the block after it (0x3737DC-0x373F74: the next seed, an order of three); the attract demo's
   counters; the scene's; the frame routine's frame counts (0x38FAC4-0x38FAEF, 0x38FB40-0x38FB57, 0x3CB418: the
   menus' timing follows them). With them the count of frames shown (the D3D adapter's, which the game copies). */
static const struct { std::uint32_t address, size; } kMatchState[] = {
    {0x3706F0u, 4u}, {0x193C60u, 4u}, {0x372DF8u, 0x373F80u - 0x372DF8u}, {0x398BF0u, 0x20u}, {0x398700u, 0x20u},
    {0x38FAC4u, 0x2Cu}, {0x38FB40u, 0x18u}, {0x3CB418u, 4u},
};

/* Online play from the in-game menu (sc2_online.h): the settings every match starts from (the player's storage, the
   build, the launcher's name), whether this hosting came from the menu (its game the player's own until someone
   joins: then to the title screen, once), and a note for the menu. What a match shows, and the chat and Esc's
   question while it goes on. */
static sc2::Netplay::Settings netBase;
static bool netFromMenu, netTitleAsked;
static std::string netNote;
static sc2::OnlineShown netShown;
static bool netTyping, netLeaving;
static std::string netTyped;
struct ChatLine { std::string text; std::uint64_t until; };
static std::vector<ChatLine> netChat;
constexpr std::uint64_t kChatMs = 8000;
constexpr std::size_t kChatLines = 5;

static std::uint64_t nowMs()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

/* Why the match ended, shown for a while after it (the reads left to show it) */
static std::string netEnded;
static unsigned netEndedShown;

/* Coming back after a drop (cps2recomp's way): the host, whose other player left or went quiet, hosts again with
   the same code while its game plays on (when they come, it starts over for the match, as hosting from the menu
   does); a joining player whose connection was lost starts again joining the same match (--net-again). */
static bool netAgain;          // (--net-again: this copy is one started again to come back)
static bool netFresh;          // (--net-fresh: this copy is the one started over for the match: it takes them in)
static bool netWaitingReturn;  // (hosting again, for the other player to come back)
[[noreturn]] static void rejoin();
static void hostAgain(const std::string &code, const std::string &secret);
[[noreturn]] static void startAgain(std::vector<std::string> args);
static std::vector<std::string> argsAgain();

/* The match over (the other player left or was lost, or the two games came apart): this game plays on by itself,
   its own controls on controller 1 again, and goes back to the title screen as the game's scene switch (0x502A0)
   does: the next scene asked for ([0x398704]) and the scene's task ([0x39871C]) ended. The other player's game
   plays on from the host's save in the match's folder, which it keeps until it is started again. Then each comes
   back if it can (above). */
static void endMatch()
{
    std::fprintf(stderr, "online: %s; playing on alone\n", netplay->ended.c_str());
    if (!netplay->lost) netplay->Leave(); // (a goodbye to a side gone quiet would tell it, when it is heard again, that this one left)
    netplay->Report(true);
    netEnded = netplay->ended;
    netEndedShown = 60u * 6u;
    const bool host = netplay->Local() == 0, met = netConnected;
    const bool again = met && (host ? netplay->left || netplay->lost : netplay->lost);
    const std::string code = netplay->Code(), secret = netplay->Secret();
    netplay.reset();
    if (again && !host) rejoin();
    if (netLive) {
        *recomp_memory_u32(0x398704u) = 5u;
        const std::uint32_t scene = *recomp_memory_u32(0x39871Cu);
        if (scene) *reinterpret_cast<std::uint16_t *>(recomp_memory(scene + 0x28u, 2u)) = 1u;
    }
    netLive = false;
    netConnected = false;
    netFromMenu = netTitleAsked = netTyping = netLeaving = false;
    netChat.clear();
    sc2_net_live = 0;
    sc2_net_frame = nullptr;
    sc2_fast_forward = 0;
    recomp_kernel_set_deterministic(false);
    if (again && host) hostAgain(code, secret);
}

static void endedOverlay()
{
    RecompOverlayText text;
    recomp_overlay_clear(&text);
    if (netEndedShown && --netEndedShown) {
        recomp_overlay_set_title(&text, "ONLINE");
        recomp_overlay_add_line(&text, "%s", netEnded.c_str());
        recomp_overlay_set_hint(&text, "The match is over; the game goes on here alone.");
    }
    recomp_overlay_publish(&text);
}

static void netFrame();
static std::uint64_t netVirtualBase; // (the kernel's virtual clock at the match's start)

/* As the match begins at the title, the host's game goes on by itself to VS Battle's character select: Start, then
   on the mode select A (Original), down (VS Battle, there with two players) and A, each a few frames into its scene.
   They are the host's controls, so both games take them as the match's. Over once the select is current (scene 10). */
static bool netRoute;

static void routeToSelect(RecompInputGamepad *pad)
{
    static const struct { std::uint32_t scene; unsigned at; std::uint16_t buttons; int analog; } kSteps[] = {
        {5u, 60u, RECOMP_INPUT_BUTTON_START, -1}, {6u, 80u, 0u, RECOMP_INPUT_ANALOG_A},
        {6u, 140u, 0x2u, -1}, {6u, 200u, 0u, RECOMP_INPUT_ANALOG_A}};
    static std::uint32_t scene = ~0u;
    static unsigned frames;
    const std::uint32_t current = *recomp_memory_u32(0x398700u);
    if (current == 10u) {
        netRoute = false;
        return;
    }
    if (current != scene) {
        scene = current;
        frames = 0;
    }
    for (const auto &step : kSteps) {
        if (step.scene != current || frames < step.at || frames >= step.at + 6u) continue;
        pad->buttons |= step.buttons;
        if (step.analog >= 0) pad->analog_buttons[step.analog] = 0xff;
    }
    ++frames;
}

static bool beginMatch()
{
    netLive = true;
    netReads = 0;
    sc2_net_live = 1;
    sc2_net_reads = 0;
    netPlayers[0] = netPlayers[1] = RecompInputGamepad{};
    sc2_net_frame = netFrame;
    netVirtualBase = recomp_kernel_interrupt_time();
    recomp_kernel_virtual_time = netVirtualBase;
    recomp_kernel_set_deterministic(true);
    std::vector<std::uint8_t> blob;
    if (netplay->Local() == 0) {
        for (const auto &region : kMatchState) {
            const std::uint8_t *p = recomp_memory(region.address, region.size);
            blob.insert(blob.end(), p, p + region.size);
        }
        const std::uint32_t shown = recomp_d3d8_swap_counter();
        blob.insert(blob.end(), reinterpret_cast<const std::uint8_t *>(&shown), reinterpret_cast<const std::uint8_t *>(&shown) + 4);
    }
    if (!netplay->SyncState(blob)) {
        endMatch();
        return false;
    }
    std::size_t at = 0;
    for (const auto &region : kMatchState) {
        if (at + region.size <= blob.size()) std::memcpy(recomp_memory(region.address, region.size), blob.data() + at, region.size);
        at += region.size;
    }
    if (at + 4u <= blob.size()) {
        std::uint32_t shown;
        std::memcpy(&shown, blob.data() + at, 4u);
        sc2_d3d8_set_swap_counter(shown);
    }
    netRoute = netplay->Local() == 0 && scenePresses.empty(); // (tests script their own way)
    std::fprintf(stderr, "online: the match begins at the title screen%s\n", netRoute ? "; on to VS Battle's character select" : "");
    return true;
}

/* What the screen says of the match: the code to give while hosting, then the ping */
/* The other player's chat taken in, and what is said kept for kChatMs, kChatLines at most */
static void hearChat()
{
    if (!netplay) return;
    const std::uint64_t now = nowMs();
    for (const std::string &line : netplay->TakeHeard()) {
        const std::string name = netplay->Name(1 - netplay->Local());
        std::fprintf(stderr, "online: chat: %s: %s\n", name.empty() ? "(no name)" : name.c_str(), line.c_str());
        netChat.push_back({(name.empty() ? "Them" : name) + ": " + line, now + kChatMs});
    }
    while (!netChat.empty() && (netChat.size() > kChatLines || netChat.front().until <= now))
        netChat.erase(netChat.begin());
}

/* A key's press (held now, not before) */
static bool keyPressed(SDL_Scancode code)
{
    static bool held[SDL_SCANCODE_COUNT];
    const bool now = recomp_input_scancode_held(code);
    const bool pressed = now && !held[code];
    held[code] = now;
    return pressed;
}

/* The match's keys, read at each of controller 1's reads: T opens the chat (letters, digits and space; Backspace;
   Return sends, Esc cancels); Esc asks whether to leave (Return or A: yes; Esc or B: no). The game is given no
   controls of this side's meanwhile (netFrame). */
static void matchKeys()
{
    static const struct { SDL_Scancode code; char lower, upper; } kTyped[] = {
        {SDL_SCANCODE_A, 'a', 'A'}, {SDL_SCANCODE_B, 'b', 'B'}, {SDL_SCANCODE_C, 'c', 'C'}, {SDL_SCANCODE_D, 'd', 'D'},
        {SDL_SCANCODE_E, 'e', 'E'}, {SDL_SCANCODE_F, 'f', 'F'}, {SDL_SCANCODE_G, 'g', 'G'}, {SDL_SCANCODE_H, 'h', 'H'},
        {SDL_SCANCODE_I, 'i', 'I'}, {SDL_SCANCODE_J, 'j', 'J'}, {SDL_SCANCODE_K, 'k', 'K'}, {SDL_SCANCODE_L, 'l', 'L'},
        {SDL_SCANCODE_M, 'm', 'M'}, {SDL_SCANCODE_N, 'n', 'N'}, {SDL_SCANCODE_O, 'o', 'O'}, {SDL_SCANCODE_P, 'p', 'P'},
        {SDL_SCANCODE_Q, 'q', 'Q'}, {SDL_SCANCODE_R, 'r', 'R'}, {SDL_SCANCODE_S, 's', 'S'}, {SDL_SCANCODE_T, 't', 'T'},
        {SDL_SCANCODE_U, 'u', 'U'}, {SDL_SCANCODE_V, 'v', 'V'}, {SDL_SCANCODE_W, 'w', 'W'}, {SDL_SCANCODE_X, 'x', 'X'},
        {SDL_SCANCODE_Y, 'y', 'Y'}, {SDL_SCANCODE_Z, 'z', 'Z'}, {SDL_SCANCODE_1, '1', '!'}, {SDL_SCANCODE_2, '2', '@'},
        {SDL_SCANCODE_3, '3', '#'}, {SDL_SCANCODE_4, '4', '$'}, {SDL_SCANCODE_5, '5', '%'}, {SDL_SCANCODE_6, '6', '^'},
        {SDL_SCANCODE_7, '7', '&'}, {SDL_SCANCODE_8, '8', '*'}, {SDL_SCANCODE_9, '9', '('}, {SDL_SCANCODE_0, '0', ')'},
        {SDL_SCANCODE_SPACE, ' ', ' '}, {SDL_SCANCODE_COMMA, ',', '<'}, {SDL_SCANCODE_PERIOD, '.', '>'},
        {SDL_SCANCODE_SLASH, '/', '?'}, {SDL_SCANCODE_APOSTROPHE, '\'', '"'}, {SDL_SCANCODE_MINUS, '-', '_'},
        {SDL_SCANCODE_SEMICOLON, ';', ':'},
    };
    const uint64_t pad = recomp_input_host_pad_buttons();
    static uint64_t padBefore;
    const auto padPressed = [&](SDL_GamepadButton b) { return (pad >> b & 1u) && !(padBefore >> b & 1u); };
    if (netTyping) {
        const bool shift = recomp_input_scancode_held(SDL_SCANCODE_LSHIFT) || recomp_input_scancode_held(SDL_SCANCODE_RSHIFT);
        for (const auto &key : kTyped)
            if (keyPressed(key.code) && netTyped.size() < sc2::Netplay::kChatMost) netTyped += shift ? key.upper : key.lower;
        if (keyPressed(SDL_SCANCODE_BACKSPACE) && !netTyped.empty()) netTyped.pop_back();
        if (keyPressed(SDL_SCANCODE_RETURN)) {
            if (!netTyped.empty()) {
                netplay->Say(netTyped);
                const std::string name = netplay->Name(netplay->Local());
                netChat.push_back({(name.empty() ? "You" : name) + ": " + netTyped, nowMs() + kChatMs});
            }
            netTyping = false;
        }
        if (keyPressed(SDL_SCANCODE_ESCAPE)) netTyping = false;
    } else if (netLeaving) {
        if (keyPressed(SDL_SCANCODE_RETURN) || padPressed(SDL_GAMEPAD_BUTTON_SOUTH)) {
            netLeaving = false;
            netplay->ended = "You left the match.";
            endMatch();
        } else if (keyPressed(SDL_SCANCODE_ESCAPE) || padPressed(SDL_GAMEPAD_BUTTON_EAST)) {
            netLeaving = false;
        }
    } else {
        const bool chat = keyPressed(SDL_SCANCODE_T); // (before the letters below, T among them, are taken as held)
        for (const auto &key : kTyped) (void)keyPressed(key.code); // (the letters held as the chat opens are not typed)
        if (chat) {
            netTyping = true;
            netTyped.clear();
        } else if (keyPressed(SDL_SCANCODE_ESCAPE) || padPressed(SDL_GAMEPAD_BUTTON_BACK)) {
            netLeaving = true;
        }
    }
    padBefore = pad;
}

/* What the screen says of online play: hosting, the code to give (and that it is in the lobby); then, in the match,
   the chat (its lines, the one being typed) and Esc's question above, and at the bottom the names, the ping and the
   input delay as the Online features have them, or that the other player has gone quiet. */
static void netOverlay()
{
    RecompOverlayText text;
    recomp_overlay_clear(&text);
    if (!netConnected && netWaitingReturn) {
        recomp_overlay_set_title(&text, "ONLINE: WAITING FOR THE OTHER PLAYER TO COME BACK");
        recomp_overlay_add_line(&text, "%s", netEnded.c_str());
        if (!netplay->code.empty()) recomp_overlay_add_line(&text, "Match code: %s", netplay->code.c_str());
        recomp_overlay_set_hint(&text, "They can come back with the same code; the game starts over for the match when they do. "
                                       "Play on meanwhile. The menu's Online page stops hosting.");
    } else if (!netConnected) {
        recomp_overlay_set_title(&text, "ONLINE: HOSTING");
        if (!netplay->code.empty())
            recomp_overlay_add_line(&text, "Match code: %s%s", netplay->code.c_str(), netplay->Listed() ? "  (in the lobby)" : "");
        recomp_overlay_add_line(&text, "%s", netplay->waiting.c_str());
        if (!netplay->Invited().empty()) {
            const std::string &said = netplay->Invitation();
            recomp_overlay_add_line(&text, "%s %s", netplay->Invited().c_str(),
                said == "declined" ? "said no (the code still joins)"
                : said == "away"   ? "couldn't be invited (they left the waiting room)"
                                   : "is invited: waiting for them to accept");
        }
        recomp_overlay_set_hint(&text, netFromMenu ? "Give the other player the code; play on meanwhile. The menu's Online page stops hosting."
                                                   : "Give the other player the code. Esc: the menu (Quit game stops hosting).");
    } else if (!netLive) {
        recomp_overlay_set_hint(&text, "ONLINE: the other player is here; the match begins at the title screen");
    } else {
        matchKeys();
        if (!netplay) return; // (left)
        hearChat();
        for (const ChatLine &line : netChat) recomp_overlay_add_line(&text, "%s", line.text.c_str());
        if (netTyping) recomp_overlay_add_line(&text, "> %s_   (Return: send, Esc: cancel)", netTyped.c_str());
        if (netLeaving) recomp_overlay_add_line(&text, "Leave the match?   Return or A: leave   Esc or B: stay");
        std::string hint;
        if (netplay->Silent() > 1000) {
            hint = "WAITING FOR THE OTHER PLAYER";
        } else {
            if (!netShown.hide_names) {
                hint = (netplay->Name(0).empty() ? std::string("P1") : netplay->Name(0)) + " vs " +
                       (netplay->Name(1).empty() ? std::string("P2") : netplay->Name(1));
            }
            char numbers[48] = "";
            if (netShown.ping && netShown.delay) std::snprintf(numbers, sizeof numbers, "PING %d ms   DELAY %d", netplay->Ping(), netplay->Delay());
            else if (netShown.ping) std::snprintf(numbers, sizeof numbers, "PING %d ms", netplay->Ping());
            else if (netShown.delay) std::snprintf(numbers, sizeof numbers, "DELAY %d", netplay->Delay());
            if (*numbers) hint += (hint.empty() ? "" : "   ") + std::string(numbers);
            if (!netTyping && !netLeaving) hint += (hint.empty() ? "" : "   ") + std::string("T: chat");
        }
        recomp_overlay_set_hint(&text, hint.c_str());
    }
    recomp_overlay_publish(&text);
}

namespace sc2 {

OnlineShown &OnlineDisplay() { return netShown; }
bool OnlineHosting() { return netplay && !netConnected; }
bool OnlineInMatch() { return netplay && netConnected; }
bool OnlineCanHost() { return !netplay; }
const char *OnlineNote() { return netNote.c_str(); }

void OnlineHost()
{
    if (netplay) return;
    sc2::Netplay::Settings settings = netBase;
    settings.hosting = true;
    settings.listed = !settings.name.empty();
    settings.restartOnHello = true;
    netplay = std::make_unique<sc2::Netplay>(settings);
    std::string error;
    if (!netplay->Begin(error)) {
        netNote = "Hosting did not start: " + error;
        std::fprintf(stderr, "online: %s\n", netNote.c_str());
        netplay.reset();
        return;
    }
    netNote.clear();
    netFromMenu = true;
    netTitleAsked = false;
    netConnected = netLive = false;
    sc2_net_seed = netplay->Seed();
    sc2_net_seeded = 1; // (the sound's random numbers its own from now, as a game started online has them: sc2_hooks.c)
    std::fprintf(stderr, "online: hosting from the menu\n");
}

void OnlineJoin(const std::string &match)
{
    std::vector<std::string> args = argsAgain();
    args.push_back("--join");
    args.push_back(match);
    if (!netBase.name.empty()) {
        args.push_back("--net-name");
        args.push_back(netBase.name);
    }
    if (netplay) {
        netplay->Leave();
        netplay.reset();
    }
    std::fprintf(stderr, "online: the game starts again to join %s\n", match.c_str());
    startAgain(args);
}

namespace {
std::future<std::vector<OpenMatch>> lobbyFetch;
std::vector<OpenMatch> lobbyMatches;
bool lobbyCame;
}

void OnlineLobbyFetch()
{
    if (lobbyFetch.valid()) return;
    lobbyFetch = std::async(std::launch::async, [] {
        std::vector<launcher::OpenMatch> found;
        std::vector<OpenMatch> matches;
        if (launcher::fetch_lobby(std::string(Netplay::kService) + "/v1/lobby/soulcalibur2?protocol=" +
                                      std::to_string(Netplay::kProtocol), found)) {
            for (const launcher::OpenMatch &one : found)
                matches.push_back({one.code, one.name, one.version.empty() ? std::string(SC2_VERSION) : one.version});
        }
        return matches;
    });
}

bool OnlineLobby(std::vector<OpenMatch> &matches)
{
    if (lobbyFetch.valid() && lobbyFetch.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        lobbyMatches = lobbyFetch.get();
        lobbyCame = true;
    }
    matches = lobbyMatches;
    return lobbyCame;
}

void OnlineStopHosting()
{
    if (!netplay || netConnected) return;
    netplay->Leave();
    netplay.reset();
    netFromMenu = netWaitingReturn = false;
    netNote = "Hosting stopped: the code no longer works.";
    std::fprintf(stderr, "online: hosting stopped from the menu\n");
    RecompOverlayText text;
    recomp_overlay_clear(&text);
    recomp_overlay_publish(&text);
}

} // namespace sc2

/* Rollback (netFrame): at the start of every frame (sc2_frame.cpp) the frame's controls are settled and a save state
   taken (sc2_state.h). In a fight, once its loading is done, the game runs on with the other player's controls
   guessed (their last) up to kRollbackFrames ahead of what has come; when theirs come and differ from the guess, the
   frame they were for is put back and the game runs on from there, unshown and unpaced (sc2_fast_forward), to the
   frame it was at. Elsewhere (menus, loading; the other threads busy) each frame waits for the other's controls. */
constexpr unsigned kRollbackFrames = 7, kStates = kRollbackFrames + 2;
constexpr std::uint32_t kFightScene = 7u, kFightSettled = 300u; // (scene 7 for 5 seconds: its loading is done)
static sc2::SaveState netStates[kStates];
static std::uint64_t netStateFrame[kStates];
static std::uint32_t netStateHash[kStates];
static RecompInputGamepad netGuessed[kStates];
static bool netGuess[kStates];
static std::uint64_t netFrameAt, netCheckedUpTo, netResimUntil, netHashedUpTo, netRollbacks, netResimFrames, netSaves;
static std::uint64_t netFightSince = UINT64_MAX;

static bool samePad(const RecompInputGamepad &a, const RecompInputGamepad &b)
{
    return std::memcmp(&a, &b, sizeof a) == 0;
}

static void netFrame()
{
    if (!netplay || !netLive) return;
    std::uint64_t frame = netFrameAt;
    if (!netplay->Poll()) {
        endMatch();
        return;
    }
    // (a guess that the other's controls have since shown wrong: back to that frame)
    const std::uint64_t known = std::min(netplay->RemoteUpTo(), frame);
    for (std::uint64_t at = netCheckedUpTo; at < known; ++at) {
        const unsigned slot = at % kStates;
        if (!netGuess[slot] || samePad(netplay->RemoteAt(at), netGuessed[slot])) continue;
        if (netStateFrame[slot] != at || !netStates[slot].valid) {
            netplay->ended = "The match could not go back to a frame (rollback).";
            netplay->failed = true;
            endMatch();
            return;
        }
        sc2::LoadFrame(netStates[slot]);
        netResimUntil = std::max(netResimUntil, frame);
        netResimFrames += frame - at;
        ++netRollbacks;
        frame = at;
        break;
    }
    netCheckedUpTo = std::min(netplay->RemoteUpTo(), frame);
    const bool live = frame >= netResimUntil;
    // (how far ahead of the other's controls this frame may run)
    /* (a fight, with no next scene asked for: once one is, its loading may begin on the other threads, which a frame
       put back would not undo; from then each frame waits, and the fight's next rollback needs the next scene's) */
    const bool switching = *recomp_memory_u32(0x398704u) != 0xFFFFFFFFu;
    const bool fight = *recomp_memory_u32(0x398700u) == kFightScene && !switching;
    if (!fight) netFightSince = UINT64_MAX;
    else if (netFightSince == UINT64_MAX) netFightSince = frame;
    const std::uint64_t ahead = fight && frame >= netFightSince + kFightSettled ? kRollbackFrames : 0u;
    if (live) {
        if (netplay->RemoteUpTo() + ahead <= frame && !netplay->WaitFor(frame + 1u - ahead)) {
            endMatch();
            return;
        }
        RecompInputGamepad mine{};
        recomp_input_pulse_source_sample(&inputPulses, &mine);
        if (!scenePresses.empty()) applyScenePresses(&mine);
        if (netRoute) routeToSelect(&mine);
        if (netTyping || netLeaving) mine = RecompInputGamepad{}; // (the keys are the chat's, or the question's)
        netplay->Post(frame, mine);
    }
    const unsigned slot = frame % kStates;
    netGuess[slot] = netplay->RemoteUpTo() <= frame;
    netGuessed[slot] = !netGuess[slot] ? netplay->RemoteAt(frame)
        : netplay->RemoteUpTo() ? netplay->RemoteAt(netplay->RemoteUpTo() - 1u) : RecompInputGamepad{};
    /* (the other threads run until each waits, once the frame's controls are in: after any wait for them, so the
       kernel's fallback for a thread spinning on the others, by the host's time, never mistakes that wait) */
    recomp_kernel_drain_others();
    netPlayers[netplay->Local()] = netplay->LocalAt(frame);
    netPlayers[1 - netplay->Local()] = netGuessed[slot];
    sc2_net_reads = frame;
    recomp_kernel_virtual_time = netVirtualBase + frame * 10000000ull / 60u;
    sc2_fast_forward = !live;
    /* (a frame is gone back to only when it ran on a guess of the other's controls that proved wrong: only those
       frames are saved, the whole of guest RAM each (~5 ms on a Steam Deck); a frame whose controls had come runs
       once) */
    if (netGuess[slot]) {
        sc2::SaveFrame(netStates[slot]);
        ++netSaves;
    } else {
        netStates[slot].valid = false;
    }
    netStateFrame[slot] = frame;
    netStateHash[slot] = matchHash();
    if (const char *dump = std::getenv("SC2_NET_DUMP")) {
        // (tests: SC2_NET_DUMP=FRAME:FILE saves guest RAM at the start of that frame)
        const unsigned long at = std::strtoul(dump, nullptr, 10);
        const char *file = std::strchr(dump, ':');
        if (file && frame == at) {
            if (FILE *out = std::fopen(file + 1, "wb")) {
                std::fwrite(recomp_memory(0u, RECOMP_XBOX_RAM_SIZE), 1, RECOMP_XBOX_RAM_SIZE, out);
                std::fclose(out);
            }
        }
    }
    // (the hashes of frames whose controls are all known and checked against the guesses)
    for (std::uint64_t at = netHashedUpTo + sc2::Netplay::kHashEvery; at <= std::min<std::uint64_t>(frame, netCheckedUpTo);
         at += sc2::Netplay::kHashEvery) {
        if (netStateFrame[at % kStates] != at) continue;
        netplay->Hash(at, netStateHash[at % kStates]);
        netHashedUpTo = at;
        if (std::getenv("SC2_NET_HASHLOG"))
            std::fprintf(stderr, "online hash %llu: %08x (rollbacks %llu, frames again %llu, saved %llu)\n",
                (unsigned long long)at, netStateHash[at % kStates], (unsigned long long)netRollbacks,
                (unsigned long long)netResimFrames, (unsigned long long)netSaves);
    }
    if (live) {
        // (tests: SC2_NET_SAY=FRAME:TEXT says TEXT in the chat at that frame of the match)
        static const char *say = std::getenv("SC2_NET_SAY");
        if (say && std::strtoull(say, nullptr, 10) == frame && std::strchr(say, ':')) {
            netplay->Say(std::strchr(say, ':') + 1);
            netChat.push_back({std::string("You: ") + (std::strchr(say, ':') + 1), nowMs() + kChatMs});
        }
    }
    netFrameAt = frame + 1u;
}

/* This program and its arguments, for hosting from the menu: when someone comes, the game starts over for the match
   (Netplay::Settings::restartOnHello), as a new copy hosting on the same port with the launcher's name; the other
   side's hello comes again, and that copy takes them in as --host does. */
static std::string runnerPath;
static std::vector<std::string> runnerArgs;

/* This copy replaced by a new one with these arguments (on Windows: a new process, this one ending) */
[[noreturn]] static void startAgain(std::vector<std::string> args)
{
    netplay.reset(); // (the port, for the new copy)
    std::fflush(stdout);
    std::fflush(stderr);
    std::vector<char *> argv;
    for (std::string &arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
#if defined(_WIN32)
    /* (_spawnv joins the arguments with spaces and no quotes, so a path with a space in it, the disc's
       "Soulcalibur II (USA).iso" among them, came apart and the new copy never started: each argument quoted as
       CommandLineToArgvW reads it, the program's output handed on) */
    (void)argv;
    const auto wide = [](const std::string &text) {
        std::wstring out(static_cast<std::size_t>(MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0)), L'\0');
        if (!out.empty()) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), static_cast<int>(out.size()));
        if (!out.empty()) out.pop_back();
        return out;
    };
    std::wstring line;
    for (const std::string &arg : args) {
        if (!line.empty()) line += L' ';
        line += L'"';
        std::size_t slashes = 0;
        for (const wchar_t c : wide(arg)) {
            if (c == L'\\') { ++slashes; continue; }
            line.append(c == L'"' ? slashes * 2u + 1u : slashes, L'\\');
            slashes = 0;
            line += c;
        }
        line.append(slashes * 2u, L'\\');
        line += L'"';
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(wide(runnerPath).c_str(), line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup,
            &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        std::_Exit(0);
    }
    std::fprintf(stderr, "online: the game could not start over (error %lu)\n", GetLastError());
    std::_Exit(3);
#elif defined(__ANDROID__)
    /* (no program to run: the activity starts the library again in a new process) */
    if (sc2_android_start_again(std::vector<std::string>(args.begin() + 1, args.end()))) std::_Exit(0);
    std::fprintf(stderr, "online: the game could not start over\n");
    std::_Exit(3);
#else
    execv(runnerPath.c_str(), argv.data());
    std::perror("online: the game could not start over");
    std::_Exit(3);
#endif
}

/* This program's arguments without those of online play's that a new copy is given afresh */
static std::vector<std::string> argsAgain()
{
    std::vector<std::string> args{runnerPath};
    for (std::size_t i = 0; i < runnerArgs.size(); ++i) {
        const std::string &arg = runnerArgs[i];
        if (arg == "--menu-shot") { ++i; continue; } // (a test's menu picture: once)
        if (arg == "--net-renew") { i += 2; continue; }
        if (arg == "--net-again" || arg == "--net-fresh") continue;
        if (arg == "--net-invite" || arg == "--net-invite-name") { ++i; continue; } // (they have the match's code)
        if (arg == "--host") {
            if (i + 1 < runnerArgs.size() && std::isdigit(static_cast<unsigned char>(runnerArgs[i + 1][0]))) ++i;
            continue;
        }
        args.push_back(arg);
    }
    return args;
}

[[noreturn]] static void restartForMatch()
{
    std::vector<std::string> args = argsAgain();
    args.push_back("--host");
    args.push_back(std::to_string(netBase.port));
    args.push_back("--net-fresh");
    if (!netBase.name.empty()) {
        args.push_back("--net-name");
        args.push_back(netBase.name);
        args.push_back("--net-list");
    }
    /* (the same code, so that a player coming back with it finds this copy) */
    if (netplay && !netplay->Code().empty()) {
        args.push_back("--net-renew");
        args.push_back(netplay->Code());
        args.push_back(netplay->Secret());
    }
    startAgain(args);
}

[[noreturn]] static void rejoin()
{
    std::fprintf(stderr, "online: the game starts again to come back to the match\n");
    std::vector<std::string> args = argsAgain();
    args.push_back("--net-again");
    startAgain(args);
}

static void hostAgain(const std::string &code, const std::string &secret)
{
    sc2::Netplay::Settings settings = netBase;
    settings.hosting = true;
    settings.listed = !settings.name.empty();
    settings.restartOnHello = true;
    settings.renewCode = code;
    settings.renewSecret = secret;
    netplay = std::make_unique<sc2::Netplay>(settings);
    std::string error;
    if (!netplay->Begin(error)) {
        std::fprintf(stderr, "online: hosting again did not start: %s\n", error.c_str());
        netplay.reset();
        return;
    }
    netFromMenu = netTitleAsked = true; // (sent to the title already)
    netWaitingReturn = true;
    sc2_net_seed = netplay->Seed();
    sc2_net_seeded = 1;
    std::fprintf(stderr, "online: hosting again with the same code, for the other player to come back\n");
}

static bool sampleNetplay(std::uint32_t port, RecompInputGamepad *gamepad)
{
    if (port > 1u) return false;
    if (port == 0u) netOverlay();
    if (!netConnected) {
        /* (hosting: the game is the player's own while the other player comes, their controls passed on as
           usual; it starts over for the match when they do) */
        if (port == 0u) {
            std::string error;
            const int state = netplay->Waiting(error);
            if (state < 0) {
                netplay->ended = error;
                netplay->failed = true;
                endMatch();
                return false;
            }
            if (state == 2) restartForMatch();
            if (state > 0) netConnected = true;
        }
        /* (the copy started over for the match (--net-fresh) waits hands off: the match is about to begin) */
        if (netFresh || netConnected) {
            *gamepad = RecompInputGamepad{};
            return true;
        }
        return false;
    }
    if (!netLive) {
        if (port == 0u) netplay->Flush();
        /* Both games begin the match at the title screen; one in the attract demo is sent back to it (Start), and a
           host whose game was its own (hosting from the menu) is sent there as a match's end does (endMatch) */
        const std::uint32_t current = *recomp_memory_u32(0x398700u);
        if (port == 0u && netFromMenu && !netTitleAsked && current != 5u && current != 7u) {
            netTitleAsked = true;
            *recomp_memory_u32(0x398704u) = 5u;
            const std::uint32_t scene = *recomp_memory_u32(0x39871Cu);
            if (scene) *reinterpret_cast<std::uint16_t *>(recomp_memory(scene + 0x28u, 2u)) = 1u;
        }
        if (port != 0u || sc2_scene != 5u || current != 5u) {
            *gamepad = RecompInputGamepad{};
            if (port == 0u && current == 7u && (netReads++ / 8u) % 2u == 0u) gamepad->buttons = RECOMP_INPUT_BUTTON_START;
            return true;
        }
        if (!beginMatch()) return false;
    }
    /* (the other guest threads, the loader among them, run until each is waiting before every read: their work
       between two reads is then the same on both machines) */
    if (port == 0u) recomp_kernel_drain_others();
    *gamepad = netPlayers[port];
    return true;
}

static bool sampleInput(std::uint32_t port, RecompInputGamepad *gamepad)
{
    if (netplay) {
        if (sampleNetplay(port, gamepad)) return true;
        if (netplay && netConnected) return false; // (no controllers 3 and 4 in a match)
        /* (hosting, no one yet: the player's own controls, below) */
    }
    if (netEndedShown && port == 0u) endedOverlay();
    /* Test aids (docs/bring-up.md, "Test runs"): SC2_TEST_ITEMS=FIRST-LAST owns those items (0x3BB368) at the
       title; SC2_TEST_KO=1 empties player 2's health every 240 reads of a fight. */
    if (const char *items = std::getenv("SC2_TEST_ITEMS"))
        if (port == 0u && *recomp_memory_u32(0x398700u) == 5u) {
            unsigned first = 0, last = 0;
            if (std::sscanf(items, "%u-%u", &first, &last) == 2 && last < 0x116u)
                std::memset(recomp_memory(0x3bb368u + first, last - first + 1u), 3, last - first + 1u);
        }
    if (port == 0u && std::getenv("SC2_TEST_KO") && *recomp_memory_u32(0x398700u) == 7u) {
        static std::uint64_t polls;
        if (++polls % 240u == 0u) {
            const float zero = 0.0f;
            std::memcpy(recomp_memory(0x39a77cu + 0x10E60u, 4u), &zero, 4u);
        }
    }
    if (port != 0u) {
        const bool sampled = !scriptedInputOnly && recomp_input_host_sample(port, gamepad);
        /* (after a match controller 2 stays plugged in, as it was, so the game does not stop to ask for it) */
        if (!sampled && port == 1u && !netEnded.empty()) {
            *gamepad = RecompInputGamepad{};
            return sc2::TrainingPad(port, gamepad, true);
        }
        return sc2::TrainingPad(port, gamepad, sampled);
    }
    bool sampled = recomp_input_pulse_source_sample(&inputPulses, gamepad);
    if (!scenePresses.empty()) {
        applyScenePresses(gamepad);
        sampled = true;
    }
    static bool startHeld;
    const bool start = sampled && (gamepad->buttons & RECOMP_INPUT_BUTTON_START) != 0u;
    if (start != startHeld) {
        std::fprintf(stderr, "recomp input: Start %s poll=%llu\n", start ? "down" : "up",
            static_cast<unsigned long long>(inputPulses.sample_count));
        startHeld = start;
    }
    const bool available = sc2::TrainingPad(0u, gamepad, sampled);
    if (available) recomp_latency::sample(*recomp_memory_u32(0x398700u));
    return available;
}

int main(int argc, char **argv)
{
#ifdef __ANDROID__
    sc2_android_start();
#endif
#ifdef _WIN32
    /* (Windows hands main its arguments in the system's code page; the program reads them as UTF-8, as the
       launcher writes them, and starts over with them so (startAgain)) */
    static std::vector<std::string> utf8Args;
    static std::vector<char *> utf8Argv;
    {
        int count = 0;
        if (LPWSTR *wideArgs = CommandLineToArgvW(GetCommandLineW(), &count)) {
            for (int i = 0; i < count; ++i) {
                const int size = WideCharToMultiByte(CP_UTF8, 0, wideArgs[i], -1, nullptr, 0, nullptr, nullptr);
                std::string arg(size > 0 ? static_cast<std::size_t>(size) : 1u, '\0');
                if (size > 0) WideCharToMultiByte(CP_UTF8, 0, wideArgs[i], -1, arg.data(), size, nullptr, nullptr);
                arg.pop_back();
                utf8Args.push_back(std::move(arg));
            }
            LocalFree(wideArgs);
            for (std::string &arg : utf8Args) utf8Argv.push_back(arg.data());
            utf8Argv.push_back(nullptr);
            argc = count;
            argv = utf8Argv.data();
        }
    }
#endif
    {
        std::error_code ignored;
        runnerPath = std::filesystem::absolute(std::filesystem::u8path(argv[0]), ignored).u8string();
        runnerArgs.assign(argv + 1, argv + argc);
    }
    recomp_install_host_diagnostics();
    recomp_d3d_frame_breakdown = recomp_kernel_thread_times;
    std::string xbePath, discRoot, expectStop, milestoneLog, settingsPath, storageRoot, discImage, menuShot;
    std::vector<std::string> discFiles;
    std::string guestLink, guestHeihachi, guests;
    sc2::Netplay::Settings netSettings;
    bool netWanted = false;
    recomp_input_pulse_source_init(&inputPulses, recomp_input_host_sample, 0u);
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--input-script-only") {
            scriptedInputOnly = true;
            inputPulses.base = nullptr;
            continue;
        }
        if (argument == "--skip-intro") {
            recomp_skip_intro = true;
            continue;
        }
        if (argument == "--list-features") {
            std::fputs(recomp::FeatureListText(sc2::Features()).c_str(), stdout);
            return 0;
        }
        if (argument == "--fullscreen" || argument == "--widescreen") {
            /* The presenter reads these when it starts. */
            setenv(argument == "--fullscreen" ? "RECOMP_D3D_FULLSCREEN" : "RECOMP_D3D_WIDESCREEN", "1", 1);
            continue;
        }
        if (argument == "--quick-match" && i + 1 < argc) {
            /* Read by the fight setup hook (sc2_hooks.c). */
            setenv("RECOMP_QUICK_MATCH", argv[++i], 1);
            continue;
        }
        if (argument == "--free-camera") {
            /* Read once, when the camera is installed (free_camera_adapter.c). */
            setenv("RECOMP_FREE_CAMERA", "1", 1);
            continue;
        }
        if (argument == "--mute") {
            /* Muted, not without sound: the in-game menu switches it back on. */
            recomp_audio_output_set_muted(1);
            continue;
        }
        if (argument == "--host") {
            netSettings.hosting = true;
            netWanted = true;
            if (i + 1 < argc && std::isdigit(static_cast<unsigned char>(argv[i + 1][0]))) netSettings.port = std::atoi(argv[++i]);
            continue;
        }
        if (argument == "--join" && i + 1 < argc) {
            netSettings.address = argv[++i];
            netWanted = true;
            continue;
        }
        if (argument == "--net-name" && i + 1 < argc) { netSettings.name = argv[++i]; continue; }
        if (argument == "--net-renew" && i + 2 < argc) {
            netSettings.renewCode = argv[++i];
            netSettings.renewSecret = argv[++i];
            continue;
        }
        if (argument == "--net-again") { netAgain = true; continue; }
        if (argument == "--net-fresh") { netFresh = true; continue; }
        if (argument == "--net-invite" && i + 1 < argc) {
            const std::string value = argv[++i];
            const std::size_t colon = value.find(':');
            if (colon != std::string::npos) {
                netSettings.invite = value.substr(0, colon);
                netSettings.from = value.substr(colon + 1);
            }
            continue;
        }
        if (argument == "--net-invite-name" && i + 1 < argc) { netSettings.inviteName = argv[++i]; continue; }
        if (argument == "--net-delay" && i + 1 < argc) { netSettings.delay = std::atoi(argv[++i]); continue; }
        if (argument == "--net-service" && i + 1 < argc) { netSettings.service = argv[++i]; continue; }
        if (argument == "--net-list") { netSettings.listed = true; continue; }
        if (argument == "--net-lag" && i + 1 < argc) { netSettings.fakeLag = std::atoi(argv[++i]); continue; }
        if (argument == "--input-scene" && i + 1 < argc) {
            ScenePress press;
            if (!parseScenePress(argv[++i], press)) {
                std::cerr << "recomp runner: --input-scene takes SCENE[#N]:POLL:KEY\n";
                return 64;
            }
            scenePresses.push_back(press);
            scriptedInputOnly = true;
            continue;
        }
        if (argument == "--input-start-pulse-at" && i + 1 < argc) {
            const std::string value = argv[++i];
            std::uint64_t poll = 0u;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), poll);
            if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size() ||
                !recomp_input_pulse_source_add_poll(&inputPulses, poll)) {
                std::cerr << "recomp runner: invalid Start poll or too many pulses\n";
                return 64;
            }
            continue;
        }
        if (argument == "--input-a-pulse-at" && i + 1 < argc) {
            const std::string value = argv[++i];
            std::uint64_t poll = 0u;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), poll);
            if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size() ||
                !recomp_input_pulse_source_add_a_poll(&inputPulses, poll)) {
                std::cerr << "recomp runner: invalid A poll or too many pulses\n";
                return 64;
            }
            continue;
        }
        if (argument == "--input-buttons-pulse-at" && i + 2 < argc) {
            const std::string value = argv[++i], maskValue = argv[++i];
            std::uint64_t poll = 0u;
            std::uint16_t mask = 0u;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), poll);
            const auto parsedMask = std::from_chars(maskValue.data(), maskValue.data() + maskValue.size(), mask);
            if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size() ||
                parsedMask.ec != std::errc() || parsedMask.ptr != maskValue.data() + maskValue.size() ||
                mask == 0u || (mask & 0xff00u) != 0u ||
                !recomp_input_pulse_source_add_buttons_poll(&inputPulses, poll, mask)) {
                std::cerr << "recomp runner: invalid button poll/mask or too many pulses\n";
                return 64;
            }
            continue;
        }
        if (argument == "--input-analog-pulse-at" && i + 2 < argc) {
            const std::string value = argv[++i], indexValue = argv[++i];
            std::uint64_t poll = 0u;
            std::uint32_t index = 0u;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), poll);
            const auto parsedIndex = std::from_chars(indexValue.data(), indexValue.data() + indexValue.size(), index);
            if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size() ||
                parsedIndex.ec != std::errc() || parsedIndex.ptr != indexValue.data() + indexValue.size() ||
                index >= RECOMP_INPUT_ANALOG_BUTTON_COUNT ||
                !recomp_input_pulse_source_add_analog_poll(&inputPulses, poll,
                    static_cast<std::uint8_t>(index), RECOMP_INPUT_ANALOG_PRESSED)) {
                std::cerr << "recomp runner: invalid analog poll/index or too many pulses\n";
                return 64;
            }
            continue;
        }
        if (argument == "--input-analog-pulse-every" && i + 4 < argc) {
            const std::string firstValue = argv[++i], periodValue = argv[++i], lastValue = argv[++i],
                indexValue = argv[++i];
            std::uint64_t first = 0u, period = 0u, last = 0u;
            std::uint32_t index = 0u;
            const auto parsedFirst = std::from_chars(firstValue.data(), firstValue.data() + firstValue.size(), first);
            const auto parsedPeriod = std::from_chars(periodValue.data(), periodValue.data() + periodValue.size(), period);
            const auto parsedLast = std::from_chars(lastValue.data(), lastValue.data() + lastValue.size(), last);
            const auto parsedIndex = std::from_chars(indexValue.data(), indexValue.data() + indexValue.size(), index);
            if (parsedFirst.ec != std::errc() || parsedFirst.ptr != firstValue.data() + firstValue.size() ||
                parsedPeriod.ec != std::errc() || parsedPeriod.ptr != periodValue.data() + periodValue.size() ||
                parsedLast.ec != std::errc() || parsedLast.ptr != lastValue.data() + lastValue.size() ||
                parsedIndex.ec != std::errc() || parsedIndex.ptr != indexValue.data() + indexValue.size() ||
                index >= RECOMP_INPUT_ANALOG_BUTTON_COUNT ||
                !recomp_input_pulse_source_add_analog_repeat(&inputPulses, first, period, last,
                    static_cast<std::uint8_t>(index))) {
                std::cerr << "recomp runner: invalid analog repeat or too many rules\n";
                return 64;
            }
            continue;
        }
        if ((argument != "--xbe" && argument != "--disc-root" && argument != "--settings" &&
             argument != "--disc" && argument != "--menu-shot" &&
             argument != "--storage" && argument != "--disc-file" && argument != "--guest-link" &&
             argument != "--guest-heihachi" && argument != "--guests" &&
             argument != "--expect-stop" && argument != "--milestone-log") || i + 1 >= argc) {
            std::cerr << "usage: recomp_program_runner (--disc <image> | --xbe <path> [--disc-root <path>])"
                " [--settings <settings.ini>] [--storage <dir>] [--disc-file <name>=<file>] [--fullscreen] [--widescreen] [--mute] [--free-camera]"
                " [--list-features]"
                " [--expect-stop <id>] [--milestone-log <path>]"
                " [--input-start-pulse-at <poll>] [--input-a-pulse-at <poll>]"
                " [--input-buttons-pulse-at <poll> <decimal-mask>]"
                " [--input-analog-pulse-at <poll> <button-index>]"
                " [--input-analog-pulse-every <first-poll> <period> <last-poll> <button-index>]"
                " [--input-script-only] [--skip-intro]\n";
            return 64;
        }
        const std::string value = argv[++i];
        if (argument == "--xbe") xbePath = value;
        else if (argument == "--disc-root") discRoot = value;
        else if (argument == "--settings") settingsPath = value;
        else if (argument == "--disc") discImage = value;
        else if (argument == "--menu-shot") menuShot = value;
        else if (argument == "--storage") storageRoot = value;
        else if (argument == "--disc-file") discFiles.push_back(value);
        else if (argument == "--guest-link") guestLink = value;
        else if (argument == "--guest-heihachi") guestHeihachi = value;
        else if (argument == "--guests") guests = value;
        else if (argument == "--expect-stop") expectStop = value;
        else milestoneLog = value;
    }
    if (xbePath.empty() == discImage.empty()) {
        std::cerr << "recomp runner: give --disc or --xbe\n";
        return 64;
    }
    if (!discImage.empty() && storageRoot.empty()) {
        std::cerr << "recomp runner: --disc needs --storage (nothing is written beside an image)\n";
        return 64;
    }
    recomp_stop_configure(expectStop.empty() ? nullptr : expectStop.c_str(),
        milestoneLog.empty() ? nullptr : milestoneLog.c_str());
    setenv("RECOMP_WINDOW_TITLE", "Soulcalibur II", 0);
    setenv("RECOMP_SCREENSHOT_NAME", "Soulcalibur II", 0); // (Pictures/Soulcalibur II/Soulcalibur II-....png)
    int shader = 0;
    bool graphics_prepare = false;
    std::string graphics_folder;
    if (!settingsPath.empty()) {
        const sc2::Settings settings = sc2::LoadSettings(settingsPath);
        shader = settings.shader;
        graphics_prepare = settings.graphics_prepare;
        const auto graphics = std::filesystem::u8path(settingsPath).parent_path() / "graphics-cache";
        std::error_code cache_error;
        std::filesystem::create_directories(graphics, cache_error);
        if (!cache_error) {
            graphics_folder = graphics.u8string();
            setenv("RECOMP_SHADER_CACHE", graphics_folder.c_str(), 0);
            setenv("RECOMP_GRAPHICS_CATALOG", graphics_folder.c_str(), 0);
        }
        setenv("RECOMP_GRAPHICS_PREPARE", graphics_prepare ? "1" : "0", 0);
        if (const char *catalog = std::getenv("RECOMP_GRAPHICS_CATALOG")) graphics_folder = catalog;
        if (const char *prepare = std::getenv("RECOMP_GRAPHICS_PREPARE")) graphics_prepare = std::strcmp(prepare, "0") != 0;
        sc2::TrainingState().health_refill = settings.health_refill;
        sc2::TrainingState().infinite_time = settings.infinite_time;
        sc2::TrainingState().input_display = settings.input_display;
        sc2::TrainingState().combo_readout = settings.combo_readout;
        sc2::TrainingState().hit_boxes = settings.hit_boxes;
        sc2::TrainingState().frame_data = settings.frame_data;
        sc2::TrainingState().no_damage[0] = settings.no_damage[0];
        sc2::TrainingState().no_damage[1] = settings.no_damage[1];
        sc2::TrainingState().one_hit = settings.one_hit;
        sc2::TrainingState().fps_counter = settings.fps_counter;
        if (!settings.render_fps.empty() && std::getenv("RECOMP_RENDER_FPS") == nullptr)
            setenv("RECOMP_RENDER_FPS", settings.render_fps.c_str(), 1);
        sc2::OnlineDisplay() = {settings.online_ping, settings.online_delay, settings.online_hide_names};
        if (settings.scale > 1u && std::getenv("RECOMP_D3D_SCALE") == nullptr)
            setenv("RECOMP_D3D_SCALE", std::to_string(settings.scale).c_str(), 1);
        sc2::ApplyBindings(settingsPath);
    }
    sc2::InstallMenu(settingsPath, menuShot);
    sc2::SetShader(shader);
    /* A disc image: its Default.xbe is read out of it, and the D: drive is
       served from it (disc_image.h). */
    std::vector<std::uint8_t> imageXbe;
    if (!discImage.empty()) {
        RecompDiscImageEntry entry;
        if (!recomp_disc_image_open(discImage.c_str()) ||
            !recomp_disc_image_lookup("default.xbe", &entry) || entry.directory || entry.size > (64u << 20)) {
            std::cerr << "recomp runner: the disc image has no Default.xbe\n";
            return 1;
        }
        imageXbe.resize(static_cast<std::size_t>(entry.size));
        if (recomp_disc_image_read(entry.offset, imageXbe.data(), imageXbe.size()) != imageXbe.size()) {
            std::cerr << "recomp runner: the disc image could not be read\n";
            return 1;
        }
    }
    const auto parsed = discImage.empty() ? doaxbv::XbeParser().parse(std::filesystem::u8path(xbePath))
                                          : doaxbv::XbeParser().parse(std::move(imageXbe));
    if (!parsed.ok()) {
        std::cerr << "recomp runner: " << parsed.errors.front() << '\n';
        return 1;
    }
    /* The port's media-patched copy, or the retail disc's (sc2_xbe.h). */
    const bool retailXbe = parsed.metadata.fileSha256 == SC2_RETAIL_XBE_SHA256;
    if (parsed.metadata.fileSha256 != RECOMP_XBE_SHA256 && !retailXbe) {
        std::cerr << "recomp runner: XBE SHA-256 " << parsed.metadata.fileSha256
                  << " is neither Soulcalibur II (USA) retail nor the program target\n";
        return 1;
    }
    std::vector<std::uint8_t> memory(RECOMP_XBOX_RAM_SIZE, 0u);
    std::string error;
    // The file was authenticated by SHA-256 above; the patched copy's .text digest is stale (docs/bring-up.md).
    if (!recomp_load_xbe(parsed, memory, error, true)) {
        std::cerr << "recomp runner: " << error << '\n';
        return 1;
    }
    if (retailXbe) {
        if (memory[SC2_MEDIA_PATCH_ADDRESS] != SC2_MEDIA_PATCH_RETAIL) {
            std::cerr << "recomp runner: the retail XBE's media check byte is not where it should be\n";
            return 1;
        }
        memory[SC2_MEDIA_PATCH_ADDRESS] = SC2_MEDIA_PATCH_BYTE;
    }
    // Generate the inventory in native code before any guest frame executes.
    // The executable was authenticated above; no Python installation is needed.
    if (graphics_prepare && !graphics_folder.empty())
        sc2::PrepareGraphicsInventory(memory, graphics_folder);
    const auto imageEnd = static_cast<std::uint64_t>(parsed.metadata.header.baseAddress) +
        parsed.metadata.header.sizeImage;
    if (!xbox_HeapSetImageEnd(static_cast<std::uint32_t>(imageEnd))) {
        std::cerr << "recomp runner: invalid heap image boundary\n";
        return 1;
    }
    if (!discImage.empty()) discRoot = std::filesystem::absolute(std::filesystem::u8path(discImage)).string();
    if (discRoot.empty()) discRoot = std::filesystem::absolute(std::filesystem::u8path(xbePath)).parent_path().string();
    const auto discPath = std::filesystem::u8path(discRoot);
    std::error_code filesystemError;
    if (discImage.empty() && !std::filesystem::is_directory(discPath, filesystemError)) {
        std::cerr << "recomp runner: disc root is not a readable directory\n";
        return 1;
    }
    recomp_disc_root_path = discRoot.c_str();
    if (!storageRoot.empty()) recomp_storage_root_path = storageRoot.c_str();
    if (!guests.empty()) discFiles.push_back("root.olk=" + guests);
    else if (!guestLink.empty()) discFiles.push_back("root.olk=" + guestLink);
    else if (!guestHeihachi.empty()) discFiles.push_back("root.olk=" + guestHeihachi);
    for (const std::string &file : discFiles) {
        const std::size_t equals = file.find('=');
        const std::string name = file.substr(0, equals);
        const std::string host = equals == std::string::npos ? std::string()
            : std::filesystem::absolute(std::filesystem::u8path(file.substr(equals + 1))).string();
        if (host.empty() || !std::filesystem::is_regular_file(std::filesystem::u8path(host), filesystemError) ||
            !recomp_disc_override(name.c_str(), host.c_str())) {
            std::cerr << "recomp runner: --disc-file " << file << " is not NAME=FILE of a readable file\n";
            return 64;
        }
    }
    std::string matchStorage;
    {
        /* Online: both play a match from the host's save, in a folder of the match's own beside the player's
           (sc2_netplay.h); the guests' file must be the same build. Kept for hosting from the menu too, with the
           launcher's name for the lobby (launcher.ini beside settings.ini). */
        const std::filesystem::path own = storageRoot.empty() ? discPath / ".recomp-storage"
                                                              : std::filesystem::u8path(storageRoot);
        netSettings.storage = own.u8string();
        matchStorage = (own.parent_path() / (own.filename().u8string() + "-online")).u8string();
        netSettings.matchStorage = matchStorage;
        std::error_code sizeError;
        /* (the guests' file by its size and the converter version stamped beside it, guest/build.h's
           GUESTS_VERSION: "0" for a file built before stamps) */
        std::string guestsStamp = "0";
        if (!guests.empty()) {
            std::ifstream stamp(std::filesystem::u8path(guests + ".version"));
            std::string word;
            int version = 0;
            if (stamp >> word >> version && word == "sc2-guests") guestsStamp = std::to_string(version);
        }
        netSettings.build = std::string(RECOMP_XBE_SHA256).substr(0, 16) + "/" +
            (guests.empty() ? std::string("-")
                            : std::to_string(std::filesystem::file_size(std::filesystem::u8path(guests), sizeError)) +
                                  "v" + guestsStamp);
        netBase = netSettings;
        if (netBase.name.empty() && !settingsPath.empty()) {
            recomp::Ini launcher((std::filesystem::u8path(settingsPath).parent_path() / "launcher.ini").u8string());
            launcher.load();
            netBase.name = launcher.get("online", "name").substr(0, sc2::Netplay::kNameMost);
        }
        std::atexit([] {
            if (netplay) {
                netplay->Leave();
                netplay->Report(true);
            }
        });
    }
    if (netWanted) {
        recomp_skip_intro = true; /* (online, both games go straight to the title: the match begins there) */
        /* (a host's game is the player's own while it waits, and goes where they take it: when someone comes it starts
           over for the match, as hosting from the menu does, and that copy (--net-fresh) takes them in) */
        if (netSettings.hosting && !netFresh) netSettings.restartOnHello = true;
        netplay = std::make_unique<sc2::Netplay>(netSettings);
        std::string netError;
        if (netSettings.hosting) {
            /* The host's game starts at once and waits in the background (sampleNetplay), showing its code; it
               plays the match from its own save, which the other side is sent. */
            if (!netplay->Begin(netError)) {
                std::fprintf(stderr, "online: %s\n", netError.c_str());
                return 3;
            }
        } else {
            /* The other player's game connects first: it plays the match from the host's save. Coming back after a
               drop (--net-again), it tries for a minute (the host hosts again once it has noticed), and then plays
               alone from the player's own save. */
            bool connected = netplay->Connect(netError);
            const auto began = std::chrono::steady_clock::now();
            while (!connected && netAgain && std::chrono::steady_clock::now() - began < std::chrono::seconds(60)) {
                std::fprintf(stderr, "online: not back yet (%s); trying again\n", netError.c_str());
                netplay.reset();
                std::this_thread::sleep_for(std::chrono::seconds(2));
                netplay = std::make_unique<sc2::Netplay>(netSettings);
                connected = netplay->Connect(netError);
            }
            if (!connected && !netAgain) {
                std::fprintf(stderr, "online: %s\n", netError.c_str());
                return 3;
            }
            if (connected) {
                netConnected = true;
                storageRoot = matchStorage;
                recomp_storage_root_path = storageRoot.c_str();
            } else {
                std::fprintf(stderr, "online: couldn't get back to the match (%s); playing alone\n", netError.c_str());
                netEnded = "Couldn't get back to the match: " + netError;
                netEndedShown = 60u * 8u;
                netplay.reset();
            }
        }
        if (netplay) {
            sc2_net_seed = netplay->Seed();
            sc2_net_seeded = 1;
        }
    }
    const auto saveInitialize = [&] {
        return storageRoot.empty() ? recomp_save_initialize(recomp_disc_root_path)
                                   : recomp_save_initialize_storage(recomp_storage_root_path);
    };
    bool saveReady = saveInitialize();
    /* (a copy started over or again for online play: on Windows the copy before it is still ending as this one
       starts, its hold on the save journal (opened for itself alone) with it, so this one asks again a while) */
    for (int tries = 0; !saveReady && (netFresh || netAgain) && tries < 100; ++tries) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        saveReady = saveInitialize();
        if (saveReady) std::fprintf(stderr, "online: the save journal opened after %d ms\n", (tries + 1) * 100);
    }
    if (!saveReady) {
        std::cerr << "recomp runner: save recovery failed; guest not started\n";
        return 2;
    }
    const RecompMemoryRegion region = {0u, memory.size(), memory.data()};
    recomp_runtime_init(&region, 1u, nullptr, 0u, nullptr, 0u);
    if (!guests.empty()) {
        if (!sc2::ApplyGuestRoster(guests.c_str())) std::cerr << "recomp runner: " << guests << " is not a guests' root.olk\n";
    } else if (!guestLink.empty()) sc2::ApplyGuestLink();
    else if (!guestHeihachi.empty()) sc2::ApplyGuestHeihachi();
    recomp_runtime_set_lookup(recomp_program_lookup);
    recomp_input_adapter_reset();
    recomp_input_adapter_set_source(sampleInput);
    /* The free camera and the scene freeze run only with RECOMP_FREE_CAMERA=1;
       off, input samples and task switches go unhooked. */
    recomp_free_camera_install();
    if (!scriptedInputOnly) recomp_input_adapter_set_feedback_sink(recomp_input_host_set_vibration);
    std::atexit(recomp_input_host_stop_vibration);
    recomp_stop_cleanup = recomp_d3d8_shutdown;
    std::atexit(recomp_d3d8_shutdown);
    std::atexit(recomp_dsound_api_shutdown);
    recomp_runtime.registers.esp = XBOX_STARTUP_STACK_TOP;
    std::fprintf(stderr, "recomp runner: entry=0x%08x image_end=0x%08x native-d3d8-dsound\n",
        parsed.metadata.selectedEntryPoint, static_cast<unsigned>(imageEnd));
    recomp_watchdog_start();
    recomp_runtime.registers.esp -= 4u;
    *recomp_memory_u32(recomp_runtime.registers.esp) = 0u;
    recomp_dispatch_indirect_site(parsed.metadata.selectedEntryPoint,
        XBOX_STARTUP_STACK_TOP, __FILE__, __LINE__);
    recomp_kernel_run_threads();
    recomp_stop(0, "program-returned");
}
