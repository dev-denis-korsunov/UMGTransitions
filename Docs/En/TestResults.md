# UMGTransitions automation history

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
