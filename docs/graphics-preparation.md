# Shader preparation

On SDL GPU builds, **Rendering > Shader preparation > Before playing** prepares
known shaders and graphics pipelines before the game starts. This reduces stalls
when a character or effect first appears. **During play** skips this startup work.
The setting also appears in the in-game feature menu and takes effect next launch.
Pure disables preparation; Enhanced enables it. Worker count is automatic.
The separate Windows Direct3D 11 presenter does not implement this feature, so
its launcher and feature menu do not offer the switch. Windows SDL GPU builds use
`SC2_CMAKE_ARGS=-DRECOMP_WINDOWS_SDL_GPU=ON` for both game and launcher.

The native inventory builder reads the already authenticated executable and
combines its material and effect programs with supported renderer state families.
It does not require Python or a recorded playthrough at startup. The current
inventory has 235 shader recipes, plus the renderer's base shaders. Runtime
learning records additional combinations for later launches. This is conservative
coverage, not proof of every possible state: unknown combinations still compile
when encountered, and asset loading can still hitch.

Files live in `graphics-cache` beside `settings.ini`. Compiled binaries are keyed
by generated source, entry point, defines and target. They are reused on subsequent
launches; shaders only compile again when a required binary is absent or its key
changes. GPU shader and pipeline objects must be recreated each launch. Learned
catalogs are profiled by sample count, depth format and shader target. A checksum
and record size protect recipe reads; incomplete tails are discarded. Generated
catalogs and binaries are private data and must never be committed or distributed.

The shared runtime fills the binary cache using up to 16 compiler workers, bounded
by logical CPU count. GPU resource creation and event handling stay on the render
thread. Closing preparation joins the workers before teardown. A developer can
compare sequential and parallel startup with `RECOMP_SHADER_WORKERS=1..16`; this
is deliberately absent from the player's settings. `RECOMP_GRAPHICS_PREPARE=0`
and `RECOMP_GRAPHICS_CATALOG` are diagnostic overrides.

## Local checks

Run `python -m unittest discover -s tools -p test_graphics_inventory.py`.
Configure `tools/graphics-tests` with CMake, build, then run CTest in that build
folder. SDL3 and the companion runtime headers are required; `RECOMP_RUNTIME_DIR`
can select a runtime checkout. These tests use synthetic inputs and cover recipe
validation and settings behavior.

The optional `tools/build_graphics_inventory.py` is a developer reference and
asset-coverage audit, not a startup dependency. Its output and any game-derived
comparison inputs belong under `private/`. `recomp_graphics_inventory_test` in the
merged runtime prepares that private inventory without executing guest code;
`--cancel` checks early closure. No inventory is shipped in the source tree.

Local Windows/Vulkan validation generated the same 235 records with the C++ and
Python builders. An empty-cache preparation constructed 18,616 pipelines without
failure. On the test machine, one worker took 4.03 s, four took 1.63 s and automatic
16-worker selection took 1.14 s. A populated cache took 0.25 s. All runs produced
identical compiled binaries. These are local measurements, not cross-platform
performance guarantees; Metal, Android and low-power hardware remain untested.
