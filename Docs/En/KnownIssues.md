# Known issues

Confirmed runtime-logic issues are tracked here until fixed and covered by a regression test.

| Priority | Status | Issue | Resolution / next step |
| --- | --- | --- | --- |
| Critical | Solved | Inconsistent tick delta could desynchronise spring and transition time. | One clamped delta is shared by both passes; covered by `Runtime.TickDelta`. |
| Critical | Solved | Invalid CurveTable easing could finish at `FromValue`. | Missing rows fall back to linear with a warning; covered by `Runtime.InvalidEasingFallback`. |
| Critical | Solved | Callback reentrancy invalidated `TArray` transition references. | Delegates use local copies and event queues; structural removals are deferred until dispatch completes, then sidecar indices are repaired during `RemoveAtSwap`. Covered by `Runtime.CallbackReentrancy`. |
| High | Solved | Explicit `FromValue` was not applied before the first tick when Delay was zero. | `StartTransition` now writes a non-deferred explicit From immediately; covered by `Runtime.ExplicitFrom`. |
| High | Open | Deferred From and repeated cycles do not expose an exact cycle-start value for one frame. | Extract an `ApplyCycleStartValue` path for delayed starts and repeats. |
| High | Open | Async action does not finish when start fails. | Return start success/ID and finish the async action on failure. |
| Medium | Solved | Spring callback progress did not represent simulation progress. | Progress was removed from public `Updated`; the event now returns only the actual `Transition Value`. |

Regression coverage also includes FieldNotify interval delivery, repeat/YoYo behavior, binding channels, and `RemoveAtSwap` index consistency.
