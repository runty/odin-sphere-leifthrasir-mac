# Combat profiling on macOS

The reference run uses CUSA05083 US base 1.00, an M5 Max on macOS 27.2.1,
Rosetta, Cocoa SDL 3.4.18 and KosmicKrisp 26.2.99. The window is 960x540 logical
pixels and the game renders at native 1920x1080. Audio remains enabled with a
256-frame CoreAudio period. State is copied before testing.

## Observed bottleneck and change

Combat issues hundreds of triangle-fan draws per frame. KosmicKrisp's fan path
converts their geometry through compute, repeatedly ending and starting Metal
encoders. CPU samples show this encoding cost. Lowering game resolution to
720p gives little benefit, consistent with a CPU command bottleneck.

The Mac renderer now expands eligible direct fans into triangle-list indices
on the CPU. It preserves vertex IDs, triangle order, winding and both provoking
vertex modes. The ordering follows the [Vulkan triangle-fan specification](https://docs.vulkan.org/spec/latest/chapters/drawing.html#drawing-triangle-fans).
The draw pipe is drained before inspecting buffer ownership and recording an
expanded draw. Converted draws use a triangle-list pipeline and their own
streamed index snapshot. Prepared fan pipelines are not reused for them.

Expansion is limited to 3–4096 input vertices, the ordinary vertex stage and
no stream-out or primitive restart. Indexed draws must have their complete
input range available. GPU-owned, large, indirect, restart-enabled and other
unsupported draws keep the existing driver path. The Mac frame lead is bounded
at two; frame reuse still waits on its presentation fence. Other platforms
retain their previous defaults.

## Measured result

The player repeated the early battlefield and confirmed smoother combat with
correct sprites, attacks and effects. The table selects five-second windows
with at least 500 draws per frame and no shader/pipeline compilation:

| Configuration | Windows | Median window fps | Median draws/frame | Median window p99 |
| --- | ---: | ---: | ---: | ---: |
| Original path, native 1080p | 23 | 34.1 | 626 | 33.34 ms |
| Two-frame lead, native 1080p | 10 | 38.55 | 615.5 | 33.35 ms |
| Two-frame lead, 720p | 10 | 39.55 | 582.5 | 33.35 ms |
| CPU fan expansion and two-frame lead, native 1080p | 9 | 60.0 | 632 | 16.68 ms |

These are medians of selected window measurements, not a deterministic replay
or a whole-session percentile. Routes and workloads vary. Optimized selected
windows range from 42.7 to 60.0 fps; instrumentation and occasional hitches
remain visible. Their median window p95 is 16.67 ms. This establishes an early
combat improvement, not a locked 60 fps result throughout the game or the M4
30-minute acceptance test. Classic mode and other profiles remain untested.

## Repeating the check

Use the build inputs in [MACOS_PORT.md](../docs/MACOS_PORT.md). Enable
OSL_FRAME_STATS=1 and OSL_PERF_DIAG=1, then load a copied profile and repeat the
same battle at the same render/window size. The log reports primitive counts,
CPU-expanded fans, render scopes, waits, Darwin thread CPU time and p95/p99
frame intervals. Keep compilation and scene-transition intervals identifiable.

For a fallback comparison, set OSL_CPU_TRIANGLE_FANS=0 and
OSL_GPU_FRAMES_AHEAD=1 before launch. On Mac the defaults are expansion on and
a two-frame lead. The lead override accepts 1–3. Keep captures, raw logs and
game/save data outside Git.

Build and run the synthetic geometry checks with:

```sh
cmake --build "$WORK/macos-build" --target osl-triangle-fan-tests
"$WORK/macos-build/bin/osl-triangle-fan-tests"
```

The suite passes 16,376 coverage, winding, provoking-vertex and output-bounds
cases for 16/32-bit indices. The runtime synchronization suite, 1,584 tiling
checks and real import check also pass. A final run with both performance
overrides unset logs the Mac defaults, presents 1,800 frames and exits zero.
