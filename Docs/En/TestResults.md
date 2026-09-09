# UMGTransitions automation history

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
