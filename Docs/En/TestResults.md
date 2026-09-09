# UMGTransitions automation history

2026-09-09 — UE 5.7 / Mac arm64 Development: `Fit To Time` semantics accepted.
`Spring Force` no longer changes derived frequency when `Fit To Time` is enabled, leaving
duration, damping, and the internal relative tolerance as its controls. The expanded
`Spring.FitToTime` regression verifies that forces `1`, `160`, and `10000` have an identical
pre-deadline sample and reach target at the deadline. Runtime is 16/16. Two isolated
`Performance.ModeMatrix` runs measured 500 springs at `10.680 / 24.842` and
`10.554 / 25.224` microseconds/frame without/with binding (about 1.5% spread).
The change is startup-only because frequency is derived in `StartSpring`; it adds no tick work.
`SpringTargetUpdate`: 100/500 = `5.117 / 25.514` microseconds/frame. Logs:
`/tmp/UMGTransitionsFitRuntime.log`, `/tmp/UMGTransitionsFitModeMatrix.log`,
`/tmp/UMGTransitionsFitModeMatrixRepeat.log`, `/tmp/UMGTransitionsFitSpringTarget.log`.

2026-09-09 — controlled A/B `70e79cc` → `f3744b0`, UE 5.7 / Mac arm64 Development.
Each revision ran `Performance.ModeMatrix` twice in the same project with matching warm-up.
At 500 springs, baseline measured `10.518 / 10.358` microseconds/frame without binding and
`25.338 / 25.760` with binding; current measured `10.430 / 10.455` and `25.845 / 25.746`.
Baseline/current averages are `10.438 / 10.443` without binding (`+0.04%`) and
`25.549 / 25.796` with binding (`+0.97%`). This is within measurement noise: the Fit To Time
change has no measurable tick regression. Logs: `/tmp/UMGTransitionsABBaselineModeMatrix1.log`,
`/tmp/UMGTransitionsABBaselineModeMatrix2.log`, `/tmp/UMGTransitionsABCandidateModeMatrix1.log`,
`/tmp/UMGTransitionsABCandidateModeMatrix2.log`.

2026-09-09 — full system A/B `70e79cc` → `25387c9`, UE 5.7 / Mac arm64 Development.
Both revisions passed all 11 `UMGTransitions.WidgetTransition.Performance` tests. At 500 transitions:
ConcurrentTick was `20.308 → 20.367` microseconds/frame (`+0.3%`); ModeMatrix Linear was
`5.658/20.225 → 5.705/21.046`, Spring was `10.970/25.410 → 10.534/24.997`
(without/with binding); spring target updates were `27.126 → 25.805` microseconds/frame.
100 Updated callbacks without/with binding: `26.475/29.617 → 26.658/29.530` microseconds/frame.
Pipe at 500 handoffs: linear `0.817 → 0.761`, spring `0.856 → 0.761` microseconds/handoff.
Bidirectional changes up to ~11% are single-run noise: the revisions change only frequency derivation
at Fit To Time spring startup, not any measured hot path. No regression was detected. Logs:
`/tmp/UMGTransitionsABSystemBaselinePerformance.log`,
`/tmp/UMGTransitionsABSystemCurrentPerformance.log`.

2026-09-09 — UE 5.7 / Mac Development: callback review regression run.
All 13 `UMGTransitions.WidgetTransition.Runtime` tests passed, including
`NativeBuilder`, `CallbackCancellation`, `RepeatCallbackDelay` and `CallbackReentrancy`.
Log: `/tmp/UMGTransitionsCallbackReview.log`. No new performance measurements.

2026-09-09 — UE 5.7 / Mac arm64 Development: `Performance.PipeHandoff` passed.
It times only active completion → removal → queued successor startup (200 samples per case).
Linear successors at 1/20/100/500 concurrent properties: `0.748 / 15.346 / 172.105 /
3324.742 μs` per completion tick, or `0.748 / 0.767 / 1.721 / 6.649 μs` per handoff.
Spring successors: `0.756 / 15.217 / 167.746 / 3338.487 μs` per tick, or
`0.756 / 0.761 / 1.677 / 6.677 μs` per handoff. The 100→500 increase is queue/array
work; lazy spring-state creation is within measurement noise of the linear path.
Log: `/tmp/UMGTransitionsPipeHandoff.log`.

2026-09-09 — UE 5.7 / Mac arm64 Development: keyed Pipe FIFO optimisation accepted.
The global queue is now independent `(Widget, Widget Property)` queues with a head index; the startup pass
snapshots active keys instead of repeatedly scanning the active transition array. Runtime is 15/15,
including `PipeOrder`, which preserves `A→B→C→D` while another property's queue is replaced.
`PipeHandoff`, 200 samples: linear 1/20/100/500 = `0.870 / 13.036 / 71.567 / 380.379 μs` per tick;
spring = `0.954 / 14.007 / 70.268 / 381.599 μs`. At 500 this is 0.761/0.763 μs per handoff,
down from 6.754/6.826 μs. Log: `/tmp/UMGTransitionsPipeQueuePerformanceOptimized.log`.

2026-09-09 — `Remove From Parent` was removed from the public Create API, native builder,
runtime transition state, and lifecycle dispatch records. Explicit widget removal remains the caller's responsibility.

This file records completed runs. Test descriptions are in [Testing.md](Testing.md), benchmark methodology in [PerformanceTests.md](Testing/PerformanceTests.md), and architectural decisions in [OptimizationHistory.md](Testing/OptimizationHistory.md).

The current accepted baseline is described in the latest entries of the Russian history and is reproduced in the English optimization documents. Performance values must be compared only between runs with the same machine, editor session, build configuration, and warm-up state.

## Latest correctness verification

UE 5.7.4, Mac arm64 Development, 2026-09-01: all 23 tests under `UMGTransitions.WidgetTransition` passed with automation exit code `0`. This includes runtime, editor metadata, diagnostics, performance tests, and historical experiments. The value-only lifecycle regression verifies From Value on Started, To Value on Finished, and callback reentrancy.

Current deterministic layout: `FWidgetTransition` 256 B, callback links 8 B, lifecycle state 72 B, update state 80 B, transient lifecycle event 80 B, and `UWidgetTransitionAsyncAction` 432 B.

Performance numbers from this full cold run are not a new baseline because Asset Registry/background editor startup work overlapped the early benchmarks. Performance comparisons still use isolated, warmed runs.

## Latest stable performance baseline

UE 5.7, Mac arm64 Development, Run 4:

| Test | Result |
| --- | --- |
| Runtime suite (`TickDelta`, `CallbackReentrancy`, `RemoveAtSwap`, easing fallback, spring convergence, intervals) | Passed |
| `Diagnostics.StorageLayout` | Passed |
| `Performance.Callbacks` | Passed |
| `Performance.AsyncTextCounter` | Passed |
| `Performance.FieldNotifyTextBinding` | Passed |
| `Performance.ModeMatrix` | Passed |
| `Performance.UpdateInterval` | Passed |

Key measurements: 100 plain text counters `11.341 μs/frame`; 100 async counters `83.403 μs/frame`; 100 FieldNotify text bindings `41.107 μs/frame`; 100 Updated callbacks with intervals `0 / 0.033 / 0.05 / 0.1 s` = `75.941 / 43.802 / 32.163 / 20.748 μs/frame`.

## Callback sidecar experiment

UE 5.7, Mac arm64 Development, 2026-08-31:

| Variant | Storage | 100 no callback, without/with binding | 100 Updated, without/with binding | Decision |
| --- | --- | ---: | ---: | --- |
| Pull | update state `48 B` | `6.104 / 7.354` μs/frame | `83.676 / 85.329` μs/frame | Rejected: callback pass sampled twice. |
| Hybrid with empty-sidecar fast path | transition `256 B`, link `8 B`, update state `80 B` | `6.224 / 7.667` μs/frame | `79.678 / 80.243` μs/frame | Accepted. |
| Value-only lazy dispatch | transition `256 B`, link `8 B`, update state `80 B` | `6.248 / 7.539` μs/frame | `83.256 / 84.086` μs/frame | Accepted as the final public API; async text improved to `80.535 μs/frame`. |

A repeated Run 12 measured no-callback `6.477/7.482`, Updated `83.812/86.959`, and async text `79.750 μs/frame`. Runtime callback tests passed again.

`Runtime.CallbackReentrancy`, `Runtime.RemoveAtSwap`, and `Runtime.UpdateInterval` passed for the accepted hybrid.
