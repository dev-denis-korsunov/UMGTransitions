# Runtime optimization history

## Controlled Fit To Time A/B

Fit To Time no longer incorporates `Spring Force` when deriving frequency: duration and damping fully define its trajectory, while Force remains a physical-mode parameter. The calculation happens only while creating spring state in `StartSpring`, not during ticking.

`70e79cc` and `25387c9` were compared in the same UE 5.7 / Mac arm64 Development project configuration. Both revisions passed all 11 `Performance` automation tests. A separate two-run Mode Matrix comparison confirmed that 500-spring cost stayed within noise: `10.438 → 10.443 μs/frame` without binding and `25.549 → 25.796 μs/frame` with binding.

| Hot path, 500 transitions | Baseline, μs/frame | Current, μs/frame | Conclusion |
| --- | ---: | ---: | --- |
| Concurrent linear tick | 20.308 | 20.367 | +0.3%, noise. |
| Mode Matrix spring, without binding | 10.970 | 10.534 | No regression. |
| Mode Matrix spring, with binding | 25.410 | 24.997 | No regression. |
| Spring with per-tick target update | 27.126 | 25.805 | No regression. |
| 100 Updated callbacks, without/with binding | 26.475 / 29.617 | 26.658 / 29.530 | Within noise. |
| Pipe spring, 500 successors | 0.856 μs/handoff | 0.761 μs/handoff | Within single-run noise. |

Decision: Fit To Time semantics are corrected with no measurable runtime-system cost. Spring API details and next priorities are documented in [Spring.md](../../Ru/Spring.md).

## CurveTable easing

CurveTable rows are resolved when a transition is added. The cached `FRealCurve*` is evaluated directly during ticking, removing row lookup from the hot path.

## Fast widget-property adapters

Common properties (`RenderOpacity`, transform channels, and pivot) use direct UWidget accessors. Generic compatible properties retain the reflective fallback.

## Callback hot/cold split

Lifecycle callbacks are stored in a cold array. Updated callbacks and FieldNotify scheduling use a dense update-state array and an index array. Event queues defer delegate execution until after the transition pass. Callback links live in a sidecar parallel to `Transitions`; structural removals are queued during dispatch and flushed afterwards in descending index order. This removes callback indices and stable callback IDs from the hot transition record while keeping `RemoveAtSwap` repair O(1).

The first sidecar version pushed progress and value into update state every frame. A pull experiment reduced update state to `48 B`, but sampling again in the callback pass raised 100 Updated callbacks to `83.676/85.329 μs/frame` without/with binding. A hybrid then sampled once in the transition pass and stored the result only for transitions with update state.

The final API returns only `FWidgetTransitionValue` from Updated. It samples lazily at dispatch; a rare repeat-boundary override preserves the completed cycle value. Across two equivalent editor runs, 100 value-only Updated callbacks measured `83.256–83.812/84.086–86.959 μs/frame` versus one hybrid result of `79.678/80.243`. `AsyncTextCounter` consistently improved from `83.403` to `79.750–80.535 μs/frame` because the async action no longer reconstructs value from progress.

Current storage sizes are `256 B` for `FWidgetTransition`, `8 B` per parallel callback link, `72 B` for lifecycle state, and `80 B` for update state. No-callback cost remained within noise (`6.248–6.477 μs/frame` for 100 transitions). Reentrancy, swap removal, update-interval, and repeat-boundary value coverage pass.

### Callback cleanup outcome

Memory compared with the original layout that kept callback bookkeeping in the transition record:

| Workload | Before | After | Difference |
| --- | ---: | ---: | ---: |
| `FWidgetTransition` | 272 B | 256 B | −16 B (−5.9%) |
| 100 linear | 27,200 B | 26,400 B | −800 B (−2.9%) |
| 100 spring | 35,600 B | 34,800 B | −800 B (−2.2%) |
| 500 linear | 136,000 B | 132,000 B | −4,000 B (−2.9%) |
| 500 spring | 178,000 B | 174,000 B | −4,000 B (−2.2%) |

The resident budget is now a `256 B` transition plus its mandatory `8 B` callback link. `FWidgetTransitionLifecycleCallbacks` (`72 B`) exists only for lifecycle events, `FWidgetTransitionUpdateState` (`80 B`) only for `Updated` or FieldNotify, and `FWidgetTransitionSpring` (`80 B`) only for spring transitions. `UWidgetTransitionAsyncAction` is `432 B`; removing its permanent callback package, binding, and target-value data gives a reconstructed reduction from approximately `560` to `432 B` (about `128 B`, or 23%, per action). The old action size is derived from removed fields and 16-byte alignment rather than a historical direct `sizeof` measurement.

The closest clean command-line baseline and the isolated post-cleanup run used the same kind of launch. This is not a strict same-environment two-binary benchmark, but it is more representative than values collected in an open editor:

| 100 linear transitions | Before, μs/frame | After, μs/frame | Change |
| --- | ---: | ---: | ---: |
| No binding or callbacks | 0.513 | 0.476 | −7% |
| Lifecycle, no binding | 2.883 | 0.489 | −83% |
| `Updated`, no binding | 28.403 | 26.633 | −6% |
| Binding, no callbacks | 1.098 | 1.091 | within noise |
| Binding + lifecycle | 3.544 | 1.138 | −68% |
| Binding + `Updated` | 29.309 | 28.200 | −4% |

Lifecycle now has almost no steady-state cost because delegates run only from the rare start/finish queues. The remaining `Updated` cost is dominated by Blueprint dynamic multicast dispatch (`0.26–0.28 μs` per transition), not interpolation or sidecar lookup. `Callback Update Interval` is therefore the practical control for this cost; packing `FWidgetTransition` further would not remove Blueprint callback overhead.

Open-editor results (around `0.8 μs` per `Updated`) must not be compared directly with headless command-line runs: Slate, Asset Registry, background editor tasks, and warm-up state can change the absolute result by several times. Optimization comparisons require an identical launch method.

Decision: keep the hot/cold split. `FWidgetTransition` owns only hot animation state, springs use a separate dense pass, lifecycle and update callback state belong to the subsystem, and the async action remains a Blueprint adapter. The complete post-cleanup suite passed 23 of 23 tests.

## Spring and containers

Springs use a dense `TArray` and a separate owner-index array. Cached parameters, squared completion checks, and a shared clamped delta are accepted. `TSparseArray` and forced `ParallelFor` were measured slower for the expected workload.

## Async callback storage

The async action no longer keeps a permanent `FWidgetTransitionCallbacks` field. The temporary package is constructed in `Activate()` and moved into the subsystem, saving `96 B` per async action object. Further dispatcher work is tracked in [AsyncCallbackOptimization.md](AsyncCallbackOptimization.md).
