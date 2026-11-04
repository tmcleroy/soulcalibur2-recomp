# The runtime dependency

The native runtime is the DOA ports' shared runtime, used as a dependency.
Until it has its own repository (doa3-re ADR 0001), `third_party/doa2u-re` pins
a commit of DOA2U, the port furthest ahead in the presenter and models, on
the `soulcalibur2-runtime` branch of `tmcleroy/doa2u-re`. Fixes to shared
files are made there and offered upstream; nothing shared is copied here.

`tools/build_game.py build` links every file of its `recomp-runtime/` into
`build/src/recomp-runtime/`, except where `runtime/` has a file of the same
name, which replaces it. Each replacement is game-specific:

| File | Why SC2 has its own |
| --- | --- |
| `program_manual.c` | Manual bindings at SC2's 5455 addresses; DOA2U's are its game hooks |
| `runner.cpp` | DOA2U's runner without its profile unlocks, Story finale and dedication card |
| `free_camera_adapter.c` | SC2's camera components (0x1CC50, 0x1CCB0) and its controls; the pause (P, . a frame) with the camera off too |
| `sc2_frame.cpp` | the start of each frame: the menu's save states, the free camera's freeze, the speeds above full |
| `scene_freeze_adapter.c` | the free camera's freeze (the frame put back each frame from a save state, `sc2_frame.cpp`, `sc2_state.cpp`) and clean view (the 2D layer 0x331B0, the menus' sprites 0x2ABC0 and boxes 0x2ED80 and 0x2EC90 left out, `sc2_hooks.c`) |
| `dsound_api_adapter.c`, `dsound_buffer_adapter.c`, `dsound_effects_adapter.c` | DOA2U's at SC2's 5455 addresses; SC2 adds GetEffectData, SetLFO and SetEG and links no 3D, stream or speaker-config API |
| `d3d8_adapter.c`, `d3d8_adapter.h`, `d3d8_state_adapter.c`, `d3d8_resource_adapter.c` | DOA2U's translated to 5455 by `tools/sc2/port_d3d.py` (device offsets, D3D globals, SDK tables); SC2's binding tables; SDK frame-buffer allocation, rect and viewport clears, swap flags |
| `d3d8_draw_adapter.c` | DOA2U's draw path translated the same way, plus SC2's push blocks (BeginPush/EndPush decoded, inline arrays drawn) and its new entry points; fixed-function draws go to the general pixel program |
| `input_adapter.c` | DOA2U's XInput at SC2's addresses; SC2 polls XGetDevices |
| `overlay_text.c`, `overlay_imgui_panel.cpp` | The shared overlay text plus SC2's training panels (`sc2_panel.h`: the input display and combo readout down each side, a note); drawn while either has something, laid out for SC2's life bars |
| `sc2_hooks.c` (new, `RECOMP_GAME_SOURCES`) | Hooks that wrap a converted game function and run it; left out of `--manual-functions` so the original stays |

## The runtime maintainer's rules

From NoRain211, who maintains the DOA ports (2026-10-29):

- The standalone runtime (doa3-re ADR 0001) takes the kernel, save journal
  and macOS/Linux hosts from doaxbv-re, and the presenter and the device,
  light and DirectSound models from DOA2U. Pinning DOA2U is fine for now (5849
  is the nearest XDK), but its kernel and hosts lag doaxbv-re's.
- The game copies still hold DOA-specific files. Build on nothing game-named:
  every `*_adapter.c` with DOA2U addresses, the free camera and scene freeze
  adapters, and anything named for a DOA game is replaced here, not used.
- XDK 5455 addresses, object layouts and state caches belong in SC2's own
  adapters. The shared presenter takes only semantic commands: no version
  checks or SC2 branches in shared code.
- Frame pacing is each game's choice; shared code must not impose one.
- Windows, macOS and Linux are all full targets (ADR 0002): a change to shared
  code must build and run on all three.
- The lifter gets an SC2 recipe branch, as DOA3 and DOA2U have theirs; general
  recompiler fixes go to its main branch as separate small PRs.
- Never patch the pinned copy in place: fixes go back as PRs, so they can be
  folded into the split. The pin may sit on a fork branch carrying exactly
  those PR commits until they merge.
- The Link and Heihachi extraction stays in this repo, built from the
  player's own discs. The runtime stays free of game data.

## Fixes sent back

Changes to shared files that SC2 needs, on the pinned branch, each to go
upstream as its own PR:

- `recomp_types.h`: packed `SQRTPS` and `RSQRTPS`, which SC2 uses and no
  DOA game did.
- `runner_load.{h,cpp}`: a caller that pinned the XBE's SHA-256 may load a
  section whose digest is stale (SC2's `.text`).
- `d3d_device_model.c` (+ tests): CreateDevice accepts any valid presentation
  instead of DOA2U's one mode.
- `guest_heap.c`, `kernel_memory.c`, `xbox_memory_layout.h`: freed blocks at
  either arena frontier return to the arena (SC2 probes and frees 49 MB of
  contiguous memory, which otherwise starved later heap allocations).
- `d3d_pixel_program.{h,cpp}`, `d3d_draw_shader.*`, `d3d_presenter.h`,
  `d3d_presenter_sdl_gpu.cpp`, `d3d_presenter_d3d11.cpp`: the general pixel
  program, fixed-function stages so far (register combiners next). The
  Direct3D 11 backend refuses such draws until it gains the same path; that
  and unit tests for the arena and the generator come before any PR.

The lifter gets every bound address as `--manual-functions`
(`tools/build_game.py`), so tail jumps into replaced SDK functions go
through the runtime's lookup instead of running the converted original.

## Animated fixed-function texture coordinates

The shared runtime pin supports independent affine COUNT2 UV transforms in
general fixed-function and combiner pixel programs. SC2 captures each sampled
stage's texture matrix from its 5455 device layout and passes semantic columns
to the runtime. Coordinates expand as (u,v,1,0): translation uses the third row.
Transforms remain per-stage even when stages read the same vertex UV set.
Matrices are dynamic constants, not shader-cache keys. Pretransformed and
programmable vertices are excluded; unsupported coordinate generation and
transform modes remain guarded. This restores atlas animation such as Necrid's
chest flame and hand effect without interpolation or gameplay changes.
