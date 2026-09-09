# Async callback optimization map

This document covers `UWidgetTransitionAsyncAction` and the callback runtime storage it uses. The goal is to reduce memory and update cost while preserving the standard Blueprint execution outputs `Started`, `Updated`, and `Finished`.

## Current layout

`Activate()` creates a temporary callback package and moves it into the subsystem:

| Data | Storage | Purpose |
| --- | --- | --- |
| `Started`, `Finished` | `FWidgetTransitionLifecycleCallbacks` | Rare lifecycle events |
| `Updated`, interval, latest sample, FieldNotify state | `FWidgetTransitionUpdateState` | Dense update pass |
| Lifecycle/update indices | `FWidgetTransitionCallbackLinks` | Parallel transition sidecar |
| `FWidgetTransitionCallbacks` | Temporary creation package | Moves delegates from the async action into runtime |

The async action retains only its pending transition, current event value, and world context. It no longer owns a permanent `FWidgetTransitionCallbacks` member, saving `96 B` per action object. Started and Finished now receive `FWidgetTransitionValue` directly from runtime, removing another `120 B` of native fields: an `88 B` property-binding cache and a `32 B` target-value copy.

All three Blueprint outputs use the same value-only contract. The runtime transition provides From Value to Started, the sampled value to Updated, and the final To Value to Finished. Lifecycle dispatch records retain only their callback and value.

## Runtime callback split

Callback bookkeeping is absent from `FWidgetTransition`. The subsystem owns a parallel link array and dense lifecycle/update arrays. Removal during callback dispatch is deferred, then processed in descending transition-index order so `RemoveAtSwap` can repair all reverse indices safely.

Three sampling variants were measured:

| Variant | Result | Decision |
| --- | --- | --- |
| Push every frame | Avoided duplicate sampling but touched callback storage for every transition | Reworked |
| Pull on dispatch | Reduced update state to `48 B`, but duplicated transition sampling and increased Updated cost | Rejected |
| Hybrid push with empty-sidecar fast path | Samples once, stores only when update states exist, and skips sidecar access for ordinary workloads | Performance reference |
| Value-only lazy dispatch | Returns one value pin, samples at dispatch, and snapshots only repeat boundaries | Accepted |

The accepted layout uses `256 B` for a transition, `8 B` for its parallel link, `72 B` for lifecycle state, and `80 B` for update state. Across the final repeated runs, 100-transition no-callback cost was `6.248–6.477/7.482–7.539 μs/frame` and Updated cost was `83.256–83.812/84.086–86.959 μs/frame` without/with binding. The end-to-end async text benchmark improved to `79.750–80.535 μs/frame`.

## Verification

Every callback-storage change must run:

- `Performance.AsyncTextCounter`;
- `Performance.Callbacks`;
- `Performance.FieldNotifyTextBinding`;
- `Diagnostics.StorageLayout`;
- `Runtime.CallbackReentrancy`;
- `Runtime.RemoveAtSwap`;
- `Runtime.UpdateInterval`.

The value-only lifecycle path passes the full 23-test suite, including reentrancy, swap removal, intervals, repeat boundaries, and Started/Finished value assertions. Performance values should only be compared within the same editor session, configuration, and warm-up state.

`Diagnostics.StorageLayout` reports `UWidgetTransitionAsyncAction` at `432 B` and the transient `FWidgetTransitionLifecycleEvent` at `80 B`, so resident UObject savings and rare queue-record cost stay visible together.

## Deferred options

A specialized async dispatcher could replace three dynamic delegates with a weak async-action reference and event mask. This is deferred because it complicates UObject lifetime and GC behavior; it should only be accepted after a measured gain that exceeds benchmark noise and maintenance cost.
