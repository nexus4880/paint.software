# Large-canvas brush regression

Baseline: `683f8eba6840798e268e99c9dece3ff6c603cefd` (v1.1.75 source).
No AppImage, package, or user configuration changes are needed.

## Diagnosis, before production changes

Investigated three explanations: dimension-dependent dab geometry; full-image
work starving input processing; and display-only rasterization artifacts.
The brush's interpolation/spacing depends on pointer distance and brush size,
not document dimensions. It joins delivered samples with straight segments; it
does not reconstruct a curved path from sparse input. The measured bottleneck
was full-image work on the UI thread, not reduced antialiasing quality.

`tests/test_brush.cpp` was built/run against the unchanged production sources
before implementing the fix. Its 128-move circular replay failed all eight
stable-layer-storage checks. Measurements (Release build, offscreen, 512x512
viewport, fixed 12-pixel brush; press/release excluded):

| Mode / path | 512x512 document, ms/move | 3840x2160 document, ms/move |
|---|---:|---:|
| Normal, tool only | 0.059 | 4.683 |
| Normal, widget dispatch + render each move | 0.633 | 25.609 |
| Overwrite, tool only | 0.404 | 27.129 |
| Overwrite, widget dispatch + render each move | 0.858 | 36.350 |

The original brush copied the full base and blended the full stroke buffer each
move. Overwrite additionally converted and scanned the entire image. The canvas
then discarded the flattened document and recomposited all layers, and walked
checkerboard squares outside the viewport. This work grows with canvas area
although each new dab touches only a small area. It explains the size-dependent
latency and the increased exposure of straight-line segments between delivered
samples. Actual platform mouse-event coalescing was not measured by the synthetic
replay; that link to physical-pointer polygonality remains an inference.

A second measured bottleneck appeared with an elliptical selection: even after
the pixel-compositing fix, 4K widget replay took 42.058 ms/move versus 2.839 ms on
512x512. The selection outline was rescanned and simplified every paint. The
additional selected-canvas scaling test failed before outline caching was added.

## Implementation

- Accumulate conservative dab damage and restore only that rectangle from the
  immutable current-colour base, then blend accumulated coverage exactly once.
  Layer storage stays in place; opacity does not accumulate at overlaps.
- Overwrite retains the original straight-alpha interpolation and rounding, but
  converts/scans only the damage tile.
- Preserve segment-wide recomposition when opacity, blend mode or the selection
  changes during a stroke. Cache the brush selection region by mask cache key.
- Retain one undo command and the existing interpolation, spacing, hardness,
  antialiasing, pressure/size and colour-switch behaviour. Colour changes still
  bake the old segment into a new base.
- Opt the brush into regional canvas-cache updates; other tools retain full
  invalidation. Coalesce damage in a QRegion and flatten only those rectangles,
  using the existing layer compositor (including custom modes and offsets).
- Bound checkerboard iteration to the viewport. Cache the selection outline by
  mask cache key; dash animation still redraws normally.

The widget may still redraw the viewport for cursor/ruler overlays, but does not
reflatten the document on every brush move. Full-size stroke buffers, undo
snapshots, colour-switch snapshots, release/history notification and exceptional
option changes remain; these are not per-pointer-move full-document operations.
Very large dabs/long jumps naturally cost more because they touch more pixels.

## Coverage and commands

`tests/referencebrush.{h,cpp}` is the frozen baseline implementation, renamed only
for test linkage. Do not optimize this oracle alongside production code.
`tests/test_brush.cpp` compares every intermediate layer against it across all
15 brush modes, four style/AA/spacing configurations, varying pressure, partial
alpha, overlap, edge clipping, elliptical selections, colour switching,
mid-stroke opacity/blend/selection changes, and undo/redo. It also runs fresh
3840x2160 normal and overwrite comparisons against that independent oracle.
Regional layer rendering is compared pixel-for-pixel with full flattening for
all layer modes and positive/negative offsets. A 4K widget replay compares
incremental rendering with forced full invalidation at fractional zoom, including
coalesced moves and undo/redo.

The deterministic performance regression asserts that layer backing storage is
not replaced during ordinary moves. Broad scaling assertions supplement this
with timings; they allow `large < small * 5 + 2 ms` rather than fragile absolute
frame-rate requirements. The full suite's hardcoded `/tmp` image paths were
changed to `QDir::tempPath()` so all test artifacts can stay inside the checkout.

Commands, from the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPAINTSW_VERSION=1.1.75
cmake --build build --target paintdotnet paintdotnet_tests paintdotnet_brush_tests sample_sepia_plugin -j 8
mkdir -p build/test-config build/test-data build/test-tmp
QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME="$PWD/build/test-config" \
  XDG_DATA_HOME="$PWD/build/test-data" TMPDIR="$PWD/build/test-tmp" \
  ctest --test-dir build --output-on-failure
# Fresh focused replay, also prints timing data:
QT_QPA_PLATFORM=offscreen ./build/paintdotnet_brush_tests
```

Verified full suite: **431 passed, 0 failed**; the optional WebP codec test skipped
because that codec is not installed. CTest: **2/2 passed**. Focused regression:
**0 failures**. Existing Qt deprecation warnings and optional Vulkan discovery
warning do not prevent the build. No system-wide dependencies were installed.

Post-fix measurements from the full CTest run:

| Mode / path | 512x512 document, ms/move | 3840x2160 document, ms/move |
|---|---:|---:|
| Normal, tool only | 0.008 | 0.008 |
| Normal, widget dispatch + render each move | 0.373 | 0.374 |
| Overwrite, tool only | 0.010 | 0.011 |
| Overwrite, widget dispatch + render each move | 0.394 | 0.376 |
| Normal, widget + elliptical selection | 0.479 | 0.592 |

Raw local logs: `build/brush-baseline.log`, `build/brush-selected-before.log`,
`build/full-build-final.log`, `build/ctest-final.log`, and
`build/Testing/Temporary/LastTest.log`. These are ignored build artifacts, not
portable benchmark promises. The harness exercises real brush and widget paths,
but cannot reproduce device/driver event timing or assess hand-drawn circles.
