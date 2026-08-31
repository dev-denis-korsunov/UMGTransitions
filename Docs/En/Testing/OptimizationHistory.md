# Runtime optimization history

## CurveTable easing

CurveTable rows are resolved when a transition is added. The cached `FRealCurve*` is evaluated directly during ticking, removing row lookup from the hot path.

## Fast widget-property adapters

Common properties (`RenderOpacity`, transform channels, and pivot) use direct UWidget accessors. Generic compatible properties retain the reflective fallback.

## Callback hot/cold split

Lifecycle callbacks are stored in a cold array. Updated callbacks and FieldNotify scheduling use a dense update-state array and an index array. Event queues defer delegate execution until after the transition pass. Callback links live in a sidecar parallel to `Transitions`; structural removals are queued during dispatch and flushed afterwards in descending index order. This removes callback indices and stable callback IDs from the hot transition record while keeping `RemoveAtSwap` repair O(1).

The first sidecar version pushed progress and value into update state every frame. A pull experiment reduced update state to `48 B`, but sampling again in the callback pass raised 100 Updated callbacks to `83.676/85.329 μs/frame` without/with binding. A hybrid then sampled once in the transition pass and stored the result only for transitions with update state.

The final API returns only `FWidgetTransitionValue` from Updated. It samples lazily at dispatch; a rare repeat-boundary override preserves the completed cycle value. Across two equivalent editor runs, 100 value-only Updated callbacks measured `83.256–83.812/84.086–86.959 μs/frame` versus one hybrid result of `79.678/80.243`. `AsyncTextCounter` consistently improved from `83.403` to `79.750–80.535 μs/frame` because the async action no longer reconstructs value from progress.

Current storage sizes are `256 B` for `FWidgetTransition`, `8 B` per parallel callback link, `72 B` for lifecycle state, and `80 B` for update state. No-callback cost remained within noise (`6.248–6.477 μs/frame` for 100 transitions). Reentrancy, swap removal, update-interval, and repeat-boundary value coverage pass.

## Spring and containers

Springs use a dense `TArray` and a separate owner-index array. Cached parameters, squared completion checks, and a shared clamped delta are accepted. `TSparseArray` and forced `ParallelFor` were measured slower for the expected workload.

## Async callback storage

The async action no longer keeps a permanent `FWidgetTransitionCallbacks` field. The temporary package is constructed in `Activate()` and moved into the subsystem, saving `96 B` per async action object. Further dispatcher work is tracked in [AsyncCallbackOptimization.md](AsyncCallbackOptimization.md).
