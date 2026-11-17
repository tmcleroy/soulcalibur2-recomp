# Soulcalibur II bring-up

Status and facts found, newest work last. Everything here was measured on the
supported executable unless it says otherwise.

## The disc and executable

- Soulcalibur II (USA), Xbox. Disc files: `Default.xbe` (3.6 MB), `root.olk`
  (694 MB, all game data), `movie.afs` (movies), `bgm.afs` (music).
- `Default.xbe` SHA-256 `7ce12851850ab46766f77c612b251afe7c8bd81f879e11b4836bdf766be542c6`
  (the port's target, media-patched) or `d7c2e1fc993350821e3f7e13cfd1c9d4ba6a7bf91011005bdd1f843899efb730`
  (retail); the launcher and runner take both (`runtime/sc2_xbe.h`).
  Every XDK library is version 5455: XAPILIB, D3D8, D3DX8, XGRAPHC, DSOUND,
  XBOXKRNL, LIBCPMT, LIBC. No XNet or XOnline.
- Movies use CRI Sofdec (`PSGSFD*` sections). Dolby section present.
- **The target's `.text` digest does not match its bytes** because that copy
  carries extract-xiso's media patch: the byte at file offset `0xf68c7`
  (virtual `0x1068c7`, the branch after `call` at `0x1068c0`) is `0xeb`
  (`jmp`) where the retail disc has `0x7d` (`jge`). Putting `0x7d` back makes
  every digest match and gives the retail file's SHA-256. The lift was made
  from the patched copy, so the recompiled code always jumps; for a retail
  file the runner writes `0xeb` into guest memory after loading, so both start
  from the same memory. The runner authenticates the whole file by SHA-256
  and loads it anyway (`recomp_load_xbe(..., fileAuthenticated)`).

## Lift

The pinned lifter (`tools/xboxrecomp`, NoRain211's fork at DOA2U's commit
`6537827`) lifts the whole executable on its first run: 7,588 functions,
about 486,000 instructions, 1.1 million lines of C, no failures and no
uninitialized EBP prologues. 18 instructions are emitted as no-ops: `cli`,
`sti`, `in`, `out`, `insb`, `outsd`, `sgdt`, `ljmp`, `enter`, `into`, all in
XAPI's kernel compatibility entry (`0x106def`, replaced by the host) or in
data the sweep decoded as code.

## Library functions

DOA2U binds about 120 library functions by address; none carry over to 5455.
`tools/xdk_symbols.py` names the XDK functions in an XBE from
XbSymbolDatabase's signatures (read as data from a local checkout). On DOA2U
it names 20 of the 21 D3D8 device functions DOA2U binds at the addresses DOA2U
verified, so its names are trusted as a starting point, not as verification.
On SC2 it names 251 functions and 53 globals; `D3D_g_pDevice` is `0x15e628`.

`tools/xbe_port.py` matches functions between two XBEs by code shape and maps
the data addresses aligned instructions use. It is good for exact matches and
weak for short getters (several land on one target), so its fuzzy results are
only hints.

The D3D8 device object moved between 5455 and 5849: most fields sit 0x418 to
0x420 bytes lower in 5455 (render target `0x15e4` against `0x1a04`, depth
`0x15e8` against `0x1a08`, vertex shader `0x37c` against `0x794`). Every DOA2U
adapter must have its offsets checked, not only its addresses.

## SC2 draws through its own push buffers

The game's own code calls `D3DDevice_BeginPush`/`EndPush` from 23 functions
and `DrawIndexedVertices`/`DrawVerticesUP` from one each: its renderer writes
NV2A methods directly. The DOA runtime replaces D3D8 at the API and draws from
semantic state; it has no NV2A method decoder. SC2 therefore needs one for
the methods its push blocks write (vertex data, transforms, texture and
combiner state). The shared render-state model already decodes NV2A Kelvin
enums, which is where such a decoder would feed.

## DirectSound

DSOUND 5455 matches 5849 instruction for instruction in the parts DOA2U
replaces: the device (root 0x2c bytes, settings 0xb4), the buffer (root 0x24,
settings 0xe0, public interface at +0x1c) and the effects image seeds. Only
addresses move: singleton `0x16a058`; vtables device `0x1899d8`, settings
`0x1899c4`, buffer `0x1899fc`; settings defaults `0x16969c`; seeds at
`0x167d02`/`0x167d07`. Each entry point was matched to DOA2U's by call tree
(`tools/xbe_deep.py`) and checked side by side (`tools/asm_pair.py`).
XbSymbolDatabase's names are wrong for some thunks here (it calls SC2's
CreateSoundBuffer `MapBufferData`); the call-tree match decides.

SC2 calls three entry points DOA2U never bound: GetEffectData (`0x161cec`,
read back from the guest's DSP payload copy), SetLFO (`0x161d79`) and SetEG
(`0x161d95`). The voice model has no LFO or envelope generator yet, so those
two are accepted unheard (logged once). 3D buffers stop at creation: SC2 uses
the light HRTF, whose 5455 tables are not located.

## D3D8 at 5455

SC2's own code calls 63 D3D, XGRAPHICS and D3DX entry points (the full list
comes from the lifter's cross references). Most are named by
`tools/xdk_symbols.py` and agree with the call-tree match; a few it misses
were checked side by side: SetTransform `0x14f890`, SetTexture `0x1508e0`,
Direct3D_CreateDevice `0x153730`, a GetRasterStatus-like `0x14fed0` (reads
an NV2A register through the device). `0x14f510` is not DOA2U's GetTexture.
New against DOA2U: BeginPush/EndPush, SetModelView, SetBackBufferScale,
Create/Load/SelectVertexShader, SetPixelShaderProgram, SetFlickerFilter,
GetProjectionViewportMatrix, FlushVertexCache, LogicOp, SetPushBufferSize.

The device object (`D3D_g_pDevice` `0x15e628`, object `0x15e630`, 0x2090
bytes against 5849's 0x24a0) keeps 5849's field order. Offsets below 0x135 are
unchanged, 0x764 to 0x970 sit 0x418 lower and 0xbc8 upward 0x420 lower:
`tools/xbe_offsets.py` votes this from every matched function pair, and all
offsets the DOA2U adapters use fall in voted ranges. D3D's globals keep their
order: before the device they sit 0x1f6ed0 lower, after it 0x1f72e0 lower.
SDK tables found by content: format table `0x15c2d0`, render-state defaults
`0x15c980`, simple-state encoding `0x176018`, deferred dirty bits `0x175ee8`.

**The presenter is not a general NV2A pixel pipeline.** Beyond fixed-function
stage 0 and a bounded vertex program, each combiner setup is a recognised
special case added for a DOA effect (bright pass, luminance tint, distortion,
bump water, stage-1 alpha masks, four-tap filter). SC2 sets its own pixel
shaders, so either each of its effects gets recognised the same way, or the
presenter gains a general register-combiner translation. That choice is open.

## Boot frontier

As of 2026-10-29 the runner boots through the kernel, DirectSound, D3D8 and
XInput, all replaced natively, and plays the opening movie on the Mac (Metal)
at about 60 fps: the Namco logo and the sword sequence render through the
general pixel program. It runs for minutes without a stop. Along the way:

- SC2 passes no frame buffers to CreateDevice (640x480, multisample 0x2012,
  swap effect 3, flags 0x120); the adapter allocates them as the SDK would.
- It clears its whole target as one rectangle and with a 0,16,640,448
  viewport (letterbox); the viewport case clears the whole target for now.
- Swap takes COPY/BYPASSCOPY/FINISH flags. COPY presents as the default; BYPASSCOPY and FINISH leave the
  picture on screen as it was (the presenter's `keep_shown`): while it loads, SC2 swaps 2, 4, 2, 4... and fades
  the held frame out with the gamma ramp, the next scene's first drawing already in the back buffer (#18).
- It spins on GetRasterStatus until the beam passes mid-frame, so the scan
  line is modelled from the guest clock (525 lines at 59.94 Hz).
- BeginPush(count) returns the pointer in 5455 (no out parameter).
- Functions reached only as immediates or through tables are seeded
  (`tools/sc2/seed_functions.json`): CRI thread entries and XPP callbacks.
- A `std::bad_alloc` throw (C++ exceptions are not dispatched) came from the
  arena fix above. C++ exception dispatch is still missing; SC2's only C++
  types are `exception` and `bad_alloc`.

The opening movie runs to the title logo (about 2.5 minutes) and Start does
not skip it: the movie scene (update `0x90280`) plays before the game
enumerates controllers. `--skip-intro` sets the scene's done flag (`+0xfc`)
on its first update, so it ends as a finished movie does (`sc2_hooks.c`).

## Title screen and attract mode

As of 2026-10-29, with `--skip-intro`, the runner reaches the title screen
(PRESS START) and runs the attract mode for minutes: the versus card, a
demonstration fight in a 3D stage with textured fighters and stencil shadows,
the character profiles and the arcade ranking. What it took:

- The general pixel program in the shared presenter (the "general
  translator"): D3DTOP fixed-function stages and NV2A register combiners from
  SetPixelShader, with the combiners' fog register as a constant (fog colour,
  factor one) so the SDK final combiner `FOG.a*R0 + (1-FOG.a)*FOG` keeps R0.
- Vertex programs (Load/SelectVertexShader and declaration-only handles), with
  declaration registers remapped to FVF order and multi-stream vertices
  gathered.
- SetModelView composites mapped from SDK screen space back to clip space.
- The SDL_GPU presenter binds a combiner program's sampled stages (it had
  bound only below the fixed-function stage count, so every combiner draw
  sampled white).
- Stencil shadow volumes write no colour (mask 0) and the shadow fill tests
  the stencil; both work in the presenter as is.
- Texture matrices on fixed-function vertices that a pixel shader shades
  (`transform_texcoords`, 2026-11-17, issue #23): stage meshes keep their UVs
  in 0..1 and tile, place and scroll them with `TEXTURETRANSFORMFLAGS` COUNT2
  and the stage's texture matrix (device `+0x8c0 + 0x40 * stage`). The
  combiner path passed the UV sets through, so every stage texture was drawn
  once across its mesh. The adapter now applies the matrix to a copy of the
  vertices. Checked against the GameCube disc in Dolphin (the same stage art).
- Generated texture coordinates on the same vertices (2026-11-17, issue #23):
  the shine on metal is a small environment map on stage 1 with
  `TEXCOORDINDEX` CAMERASPACENORMAL or CAMERASPACEREFLECTIONVECTOR and COUNT2,
  through the sphere map's matrix (0.5 g + 0.5, the stage's own texture matrix
  at `+0x8c0 + 0x40 * stage`; the `+0xce0` block the DOA games use is zero
  here). `transform_texcoords` makes the vector per vertex in camera space and
  gives the stage a UV set of its own after the vertices'.

Not yet: fixed-function lighting (lit draws use vertex colour), fog, specular
(V1 reads zero), the back-buffer scale, C++ exception dispatch.

## Input, saves and the menus

The gamepad type is `0x16dfac`, the one SC2 passes to XInputOpen: it tries
XInputOpen on every port without a handle each frame (`0x31a60`) and links no
XGetDeviceChanges. Its one XGetDevices call (`0x8bdbd`, type `0x16df30`) asks
for memory units: the mask tests bit port + 16 for a bottom slot. With the
type fixed, Start reaches the boot save check, which SC2 runs once a pad is
open:

- XCreateSaveGame (`0x104775`) with OPEN_EXISTING opens `SaveMeta.xbx` with
  OPEN_ALWAYS. The shared kernel used to create missing parent directories
  for a file, so an empty save appeared; and RtlNtStatusToDosError passed
  most statuses through raw, so the game saw neither ERROR_PATH_NOT_FOUND
  nor ERROR_FILE_NOT_FOUND and called the data unusable. Both are fixed in
  the shared kernel.
- With no save the game offers to create one (B, then Yes) and writes
  `SaveMeta.xbx`, `sc2_0.dat` and `sc2_1.dat` (16 KiB each) to
  `private/play-disc/.recomp-storage/partition1/UDATA/4e4d0003/903C2ED2E2B9/`.
  Later boots load it ("game data loaded").
- Then Start at the title opens Mode Select, and Arcade opens Character Select.

Two functions reached only through function tables right after data bytes
(`0x6e550`, `0xb17a0`) were seeded; a scan of `.rdata`/`.data` for pointers
to uncovered code that starts with an instruction found no others.

## Test runs

Button presses timed from boot (`--input-a-pulse-at` and the like) drift
with load times, so a walk through the menus lands somewhere else from run to
run. Two runner options avoid that:

- `--input-scene SCENE[#N]:POLL:KEY` presses KEY POLL polls after the game
  switches to SCENE for the Nth time. The scene switch is `0x502A0(scene,
  fade)`, which stores the next scene in `[0x398704]`; 34 is boot, 1 the
  opening, 5 the title, 7 the attract demo, 6 the menus (mode select, then
  character select as its second entry) and 16 a fight. With `--skip-intro`,
  Practice's character select is:
  `5:120:start 6:150:A`, four `down` 20 polls apart from `6:200`, then `6:320:A`.
- `--quick-match P1:C1:P2:C2[:STAGE]` gives the first attract demo (about
  30 s after boot with `--skip-intro` and no input) these fighters, costumes
  and stage: the demo's fight setup `0x3BED0` is called from `0x4E8DF` with
  its own picks, which a hook replaces.

`SC2_TEST_KEYS=SWAP:WHAT,...` does at those Swaps what a key or the menu would:
`save`, `load` (slot 1), `over` (start over), `record`, `replay`, `slow`, `turbo` (8x, for long menu walks; the save prompts at boot want `full` speed), `full`, `pause`
(the free camera's freeze, for pictures), `unlock`, `start` (Start held on controller 1 for six reads:
an Arcade win pose skipped at about Swap 3910 with `SC2_TEST_KO=1`).

Weapon Master's map takes Start for its Command Menu (Weapon Select, Character Select, Shop, Save Data, Exit);
its progress is saved only by Save Data. The yes/no prompts of the save screens open on No.

Two more test aids: `SC2_TEST_KO=1` empties player 2's health in fights (an
Arcade run to the ending takes about eight minutes headless), and
`SC2_TEST_ITEMS=FIRST-LAST` owns those items (the item states at 0x3BB368: 3 is
had) at the title. Arcade's and VS Battle's character select is current scene
10, a fight 7.

`--export-pictures SWAP[:FOLDER]` runs the menu's Export pictures at that Swap (to FOLDER, or `pictures/` beside
settings.ini), prints how many it wrote and ends the game (exit 1 when it failed): 72 from the US disc, 79 with
both guests (`runtime/sc2_pictures.cpp`).

## A match

As of 2026-10-29 Arcade plays a whole match on the Mac: character select,
the versus screen, the stage intro, the fight with its HUD and timer, the
round call, the replay and the result. On the way:

- Before the match SC2 binds an 8x8 offscreen target with the 640x960 main
  depth (D3D8 allows a larger depth) and samples that depth through a colour
  alias (format 0x41). Shortcuts, both logged once: the small target draws
  without depth, and the depth-alias stage samples white (far). A real
  version copies host depth into the D24S8 layout.
- In a match it clears the letterboxed viewport (0,16-640,464) as a rect;
  with no presenter rect clear, a rect covering the viewport clears the
  whole target, like the viewport case.

## Lighting

SC2's lit draws (fighters and stages, through register combiners) use
fixed-function lighting: three or four directional lights per object with
light ambient, COLORVERTEX with diffuse and ambient material from the vertex
color, material specular with LOCALVIEWER, and NORMALIZENORMALS. The lights
fade in from near zero when a scene starts (the versus portraits rise from
silhouettes). SC2 sets no VIEW transform; with SetModelView the model view
takes objects to the lights' space. The general path now evaluates this in
the vertex shader (`general_lighting` in the draw adapter, the shared general
shader) and passes specular as V1, which the combiners add. Without it every
lit draw used its raw vertex color (white) times the combiners' 2x and the
scenes washed out.

Not yet: fog, the back-buffer scale, C++ exception dispatch, two-sided
lighting and vertex-specular material sources (both stop), sound checked by
ear, a person playing.

## Fog, resolution and widescreen

- Fog (FOGENABLE with the table modes) is general too: the shader carries
  eye-space z, or a vertex program's oFog, and the factor fills the
  combiners' FOG alpha. Range fog and pretransformed fog (specular alpha)
  are not done: range fog stops, pretransformed draws go unfogged.
- `RECOMP_D3D_SCALE` (the shared presenter's internal resolution) works as
  is: a match at 3x renders at 1920x1440.
- SC2 has its own 16:9 mode, chosen by the Xbox video setting's widescreen
  bit; `RECOMP_D3D_WIDESCREEN=1` sets it (shared kernel_config) and the
  presenter shows 16:9. Menus, select screens, the HUD and matches lay
  themselves out for it (1707x960 at 2x).

Next: the launcher and in-game menu, then a training mode.

## Training facts

Found from RAM dumps of a match (`SC2_RAM_DUMP`/`SC2_RAM_DUMP_AT`) and a
write watch (`RECOMP_WATCH`):

- A fighter's health is a float, 240 at full: player 1's at `0x39a77c`,
  player 2's at `0x3ab5dc` (the records are `0x10e60` apart). The maximum is
  at +0xc. Damage is written by the function at `0xd8760` (called through
  `0x3ed70`); damage scaling makes fractional values.
- The round's seconds left: the integer at `0x3989d4`; the frames of the second
  at `0x3989d8` (59 down to 0 once the clock moves); the round's length (the
  Options setting) at `0x3989e8`, -1 in Practice; 60 (frames a second) at
  `0x3989c0`. The round's number is the byte at `0x398962` (from 1).
- In a fighter's record (player 1's at `0x399220`): the frames of hit stun left
  at +`0x1594` (26 or so after a hit, counting down; held while launched or
  down); a hit's reaction is going on while the word at +`0xf8` is not 0; its
  place at +`0x2920`; who plays it at +`0x50` (1 a controller, 3 the computer,
  5 Practice's dummy: 1 there and controller 2 plays it).
- Practice's fighting begins when the float at `0x39d2ac` turns from 0; it also
  does during Arcade's intros, so a round with a clock is taken by the clock.
- The fight's camera: its eye at `0x1967b0`, what it looks at `0x1968a0`; the
  picture's x runs the other way from the camera's right. The projection it is
  drawn with: pixels a unit at a unit's depth across at `0x197370` and down at
  `0x197384`, around (320, 240); across is down's times 1.1 in 4:3 (the
  factor at `0x1971ac`) and about 0.87 in the game's own 16:9 mode. The D3D
  view transform stays identity (the camera goes to the shaders as constants).
- The mode: the byte at `0x39895e` (1 Arcade, 7 Practice).
- The opening: scene 1 (the namco card) and scene 29 (the Sofdec and ADX
  logos) are a task (`[0x39871c]`) whose update (+`0x18`: 0x4fef0, 0x50190)
  steps through its loading at +`0x5c`, then shows the logo for 0x78 or 0xb4
  frames counted at +`0x44`: a press puts the count past its end once loaded
  (asking for the title from them crashes the game; running them unshown
  stalls them). The movie (scene 11) ends by its done flag (+`0xfc`).
- Unshown frames (the speeds above full, rollback): only the main
  thread's BlockUntilVerticalBlank may return at once. The sound server's
  threads (0x10a340, 0x10a3b0) loop on it, and in a fight one never waited
  again and the main thread never ran again (`sc2_frame_thread`,
  `d3d8_adapter.c`).
- Collision: each fighter's shapes are a list from +`0x1a80` (+`0x1a7c` of
  them, linked at +`0x18`, size at +`0x1c`, kind at +`0x16`). Spheres (kind
  0, 0x60 bytes): local offset +`0x20`, world centre +`0x30`, last frame's
  +`0x40`, radius +`0x50`, the waist's large one the pushing sphere. Capsules
  (kind 1, 0x90 bytes): local ends +`0x20`/+`0x30`, world ends +`0x50`/
  +`0x60`, along the weapon and limbs. The shapes' methods test them (vtable
  0x17b768; 0x76bd0 sphere to sphere, (r1 + r2)² against the distance²) every
  frame; contacts go into the defender's masks at +`0x1870` (8 bytes a part),
  resolved by 0x498e0 (under the fight step 0x4a090) into the contact result
  +`0x15f8` that the fighter's update 0x4b630 acts on. A fighter's attack is
  live while the byte at +`0xc1` is 1 (whiffs too; it ends on contact).
- Frame data: the byte at +`0xa0` is set while a fighter is not free (its own
  move, from the first frame to the last; or reeling from a hit or a guard);
  guard stun counts down at +`0x1594` as hit stun does, and +`0x24c` is 6 while
  guarding. (Not yet checked against published frame data.)
- The unlockables: the item states at `0x3bb368` (3: had).
- The endless battle sets +`0x50` to 3 (the computer) for both fighters in any fight: the game's own AI plays
  player 1 too, in Arcade as in Practice.

Training (`runtime/sc2_training.cpp`) reads and writes these between frames, a
shortcut until the damage and timer routines are rewritten: the refill and the
clock; a combo is the hits (falls in health) while the one hit is still
reacting; the input display's sides come from the camera's right against the
line between the fighters; start over is a save state (`sc2_state.h`) taken on
the first frame a round's clock moves (in Practice, as its fighting begins),
once a round; the dummy's recording is player 2's controls frame by frame.
