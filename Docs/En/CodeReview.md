# Runtime code review and cleanup

This page tracks the cleanup work found during the callback-storage review. Each independently verifiable change is committed separately. Performance-sensitive changes must preserve runtime tests and be compared against the recorded benchmarks.

## Cleanup queue

| Priority | Status | Area | Finding | Intended change |
| --- | --- | --- | --- | --- |
| High | Solved | Test isolation | Automation-only `UCLASS` types were generated inside the runtime module, including non-editor targets. Their implementations were guarded by `WITH_DEV_AUTOMATION_TESTS`, which also created a potential link mismatch. | Automation code and reflected test types now live in the editor-only `UMGTransitionsTests` module. Runtime retains only narrow helpers guarded by `WITH_DEV_AUTOMATION_TESTS`. |
| High | Solved | Update callbacks | `UpdateStates` was already dense and structural removal was deferred during dispatch, but `UpdateStateIndices` and `UpdateCallbackIndex` maintained a second identity layer. | Update states are now iterated directly. `RemoveAtSwap` repairs the one affected transition-to-state link without maintaining a duplicate index array. |
| Medium | Solved | Callback API | `HasBoundCallbacks`, duplicate async multicast types, and `bBroadcastUpdateValue` no longer contributed distinct behavior. | The unused helper and cached flag are gone; all three async outputs share one value-event type, and callback registration checks the delegate directly. |
| Medium | Planned | Tick intermediates | `FSample` carries normalized and eased progress that no consumer observes after sampling. | Keep only the sampled value, completion flag, and cycle-completion flag. |
| Medium | Planned | Reflection surface | `EWidgetTransitionValueType` is marked `BlueprintType` without being exposed as a Blueprint enum pin. `FWidgetTransitionPropertyBinding` is a reflected struct without reflected members. | Remove reflection where Unreal Header Tool and Blueprint integration do not require it; verify editor property-selection code still works. |
| Low | Planned | Module boilerplate | The runtime module class has no startup or shutdown behavior. | Replace it with `FDefaultModuleImpl`. |
| Low | Planned | Build dependencies | Several runtime/editor dependencies appear unused. | Remove them one at a time and verify a full editor build after each module-level cleanup. |
| Separate experiment | Deferred | Lifecycle payload | Internal lifecycle callbacks still pass a widget, forcing the async action to retain binding and target-value data. | Prototype a value-only lifecycle payload separately and keep it only if readability, behavior, memory, and performance all improve. |

## Intentionally retained structures

- `CallbackLinks` is the compact transition-to-sidecar lookup used by removal and callback dispatch.
- Separate lifecycle and update callback arrays keep per-frame iteration away from lifecycle-only delegates.
- `PendingRemovalIndices` and final update-event queues are required for callback reentrancy safety.
- `SpringTransitionIndices` keeps spring simulation dense without embedding ownership into spring state.

## Verification policy

- Runtime behavior: `Runtime.CallbackReentrancy`, `Runtime.RemoveAtSwap`, and `Runtime.UpdateInterval`.
- Layout: `Diagnostics.StorageLayout`.
- Callback cost: `Performance.Callbacks` and `Performance.UpdateInterval`.
- Module changes: full `ElasticUMGProjectEditor` Development build.
