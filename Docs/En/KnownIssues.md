# Known issues

Confirmed runtime-logic issues are tracked here until fixed and covered by a regression test.

| Priority | Status | Issue | Resolution / next step |
| --- | --- | --- | --- |
| Critical | Solved | Inconsistent tick delta could desynchronise spring and transition time. | One clamped delta is shared by both passes; covered by `Runtime.TickDelta`. |
| Critical | Solved | Callback reentrancy invalidated `TArray` transition references. | Delegates use local copies and event queues; structural removals are deferred until dispatch completes, then sidecar indices are repaired during `RemoveAtSwap`. Covered by `Runtime.CallbackReentrancy`. |
| High | Solved | Explicit `FromValue` was not applied before the first tick when Delay was zero. | `StartTransition` now writes a non-deferred explicit From immediately; covered by `Runtime.ExplicitFrom`. |
| High | Open | Apply From After Delay and repeated cycles do not expose an exact cycle-start value for one frame. | Extract an `ApplyCycleStartValue` path for delayed starts and repeats. |
| High | Solved | Async action does not finish when start fails. | Return start success/ID and finish the async action on failure. |
| Medium | Solved | Spring callback progress did not represent simulation progress. | Progress was removed from public `Updated`; the event now returns only the actual `Transition Value`. |

Regression coverage also includes FieldNotify interval delivery, repeat/YoYo behavior, binding channels, and `RemoveAtSwap` index consistency.

## Callback review — 2026-09-09

| Priority | Status | Issue | Resolution |
| --- | --- | --- | --- |
| High | Solved | Native-only Started and final Updated were skipped | Unified dynamic delegates. Builder exposes BindStart, BindUpdate and BindFinish; Runtime.NativeBuilder checks all three events. |
| High | Solved | Cancellation retained async actions | Weak async owner in lifecycle storage is released on removal without a successful Finished event. Runtime.CallbackCancellation covers clear, replacement and invalid widget. |
| Medium | Solved | Repeat endpoint arrived after next delay | Cycle endpoints bypass interval and delay. Runtime.RepeatCallbackDelay checks callbacks and FieldNotify property writes. |

StartTransition returns success; builder forwards the result and async releases itself on startup failure. C++ handlers use BindDynamic and UFUNCTION(FWidgetTransitionValue).
