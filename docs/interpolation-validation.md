# Interpolation contribution and latency validation

This contribution keeps simulation at 60 Hz and presents intermediate visual
frames from two owned render snapshots. It includes retained geometry, native
skin-pose capture, safe matching of reordered/repeated meshes, seam fixes, and
history reset rules for scene changes, save-state loads and photo-mode stepping.
The original rendering work used by readbacks and visibility queries still runs.

The launcher offers Off, Match monitor and Custom rate, using its existing
Rendering page. Pure and Enhanced leave interpolation off. It is currently
exposed only for Windows SDL GPU builds, where the renderer thread can service
independent presentation deadlines. Other backends/platforms need further work
and validation. The installed preview and the shader PR branches are separate.

## What the latency recording measures

`RECOMP_LATENCY_TRACE` enables a bounded in-memory diagnostic recording, written
as CSV on clean exit. The input callback records the latest sampled player-one
input timestamp and scene. Published command packets retain that provenance;
renderer dequeue, snapshot preparation and CPU submission record their own
monotonic timestamps. Interpolated submissions carry both snapshot identities
and the interpolation fraction. Failed swapchain acquisitions are counted and
excluded from submitted-frame timing statistics. Trace overflow is reported.
The recorder does not force GPU fences or synchronous readbacks. The CSV
`window_submitted` flag means a swapchain image was acquired and a CPU submission
was attempted; it is not a GPU-completion or displayed-frame acknowledgment.

The summaries distinguish packet queue waiting, latest-input-sample age at CPU
submission, and the weighted history age for matched interpolated geometry.
The latter is a timing proxy, not a claim that every pixel has that age: UI,
unmatched draws and discontinuities may use the current snapshot. Input
provenance does not prove a particular attack animation has responded. The
software recorder does not measure USB/controller delay, GPU completion,
compositor queues or physical scanout. A frame can be submitted and never shown.
Do not call these numbers input-to-photon latency.

The algorithm deliberately retains about one 60 Hz tick of visual history.
At 144 Hz, display opportunities are approximately 6.94 ms apart, but that does
not remove the history requirement. Report the measured software timing tradeoff
separately from frame pacing. True button-to-visible-response measurements need
an external input marker and high-speed camera or an optical latency sensor.

## Automated comparison

Build the isolated game and run `tools/benchmark_latency.py --help`. It takes
an executable, the user's disc, a read-only source save directory containing
`partition1`, and a new output directory under `private/`. It copies saves into
independent run folders and runs Off / 144 Hz / 144 Hz / Off sequentially using
the same scripted title-to-match inputs. It refuses to start while another game
or launcher is running, closes each process, and waits for its exit before the
next. It requires sufficient combat samples and rejects overflow or bad exits.

The default uses a normal visible window; this still does not provide physical
scanout timestamps. `--hidden` is diagnostic only: the hidden-window trial on
Windows received no swapchain images and was correctly rejected by the harness.
`tools/summarize_latency.py` can reanalyze existing CSVs. Reports include median,
p95 and p99, failed swapchain acquisitions and dropped trace records. Compare
repeated runs, keep display settings and workload constant, and disclose logging
overhead and uncontrolled background work. The recorder is opt-in and its mutex,
allocations and memory use can perturb the measurement. No zero-overhead claim.

## Submission status

Prepared in isolated branches on top of the shader-preparation contributions.
Do not publish or mark ready until the measured results, source/package review,
build checks and platform limits are included. Online rollback/input-delay
compatibility is not established by an offline scripted run. No game-derived
snapshots, saves, inventories or screenshots belong in the contribution.

## Render-only latency optimizations

Snapshots carry their production time before queue backpressure. Interpolation
samples one tick behind that timeline, so queue waiting and geometry preparation
do not restart the interpolation delay. Fractions remain bounded to the two
available snapshots; no future gameplay or pose is predicted. Cached draw
signatures avoid repeated hashing while preserving byte and topology checks.
The renderer finishes the last small command batch before replaying an older
snapshot. Input sampling, simulation, collision and original readback rendering
are unchanged.

The timing tests cover startup, invalid history, irregular intervals, preparation
delay and stalls. Cached matching is checked against the uncached path for
reordered, culled and ambiguous meshes. The benchmark logs window visibility and
minimization once per second to help diagnose unavailable swapchains.

The summary also reports source-time progression and endpoint clamping. These
use queue timestamps as an approximation, including backpressure; they do not
measure actual vertex motion or establish visual correctness. Compare them with
submission intervals and weighted sample age, rather than optimizing age alone.

## GPU and presentation diagnostics

`tools/benchmark_latency.py --gpu-timing` additionally records CPU swapchain
acquisition and submission durations, and nonblocking GPU-fence completion
bounds. The renderer polls only during its normal work/idle opportunities and
never waits on a diagnostic fence. The lower bound is the last unsuccessful
query (or submission start); the upper bound is the first successful query.
These include queueing and polling uncertainty, not pure GPU execution time.
The original render and its readbacks remain unchanged. Tracing can perturb
performance and must be compared against uninstrumented runs.

`--frames-in-flight 1|2|3` is a diagnostic override for interpolated rendering.
The normal SDL default remains unchanged. Reducing the limit may prevent
acquiring a swapchain image and skip a presentation, so a lower completion
bound alone is not grounds for enabling it by default. `--sequence 144 144`
selects repeated interpolation runs instead of the default off/on/on/off order.

`--presentmon PATH` optionally launches an independently supplied PresentMon
console executable filtered to the tested executable name (the harness refuses overlapping games). It records GPU/display events
externally and requires actual CSV output; a clean monitor exit without frame
data is rejected. Availability depends on Windows event tracing, permissions,
backend and drivers. Do not report display timing from the fallback fence trace.
`tools/summarize_gpu_timing.py` reports completion brackets, uncertainty, failed
acquisitions and incomplete fences. Keep raw recordings under `private/`.

The working capture format is PresentMon v1 metrics with `--qpc_time`.
`tools/summarize_presentation.py` joins those timestamps to the host combat
interval using the machine's QueryPerformanceFrequency. It reports displayed
and dropped frames separately and discloses time-span coverage. A short capture
must not be represented as a full matched run. These are Windows event-tracing
estimates, not optical input-to-photon measurements. The normal Vulkan path may
appear as DXGI in the Windows presentation events.

Metric definitions: https://github.com/GameTechDev/PresentMon/blob/v1.9.2/README.md#csv-columns
Queue-limit semantics: https://wiki.libsdl.org/SDL3/SDL_SetGPUAllowedFramesInFlight
