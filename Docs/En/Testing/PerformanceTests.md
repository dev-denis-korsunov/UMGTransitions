# Performance tests

Performance tests use repeated frame loops after warm-up and report microseconds per frame and per transition. They are diagnostic measurements rather than fixed thresholds.

| Test | Scope |
| --- | --- |
| `Performance.Callbacks` | 20/30/50/100 linear transitions with no callback, lifecycle callbacks, and Updated callbacks, with and without binding. |
| `Performance.UpdateInterval` | Updated callback cost at intervals 0, 0.033, 0.05, and 0.1 seconds. |
| `Performance.AsyncTextCounter` | Plain text writes versus async Updated output. |
| `Performance.FieldNotifyTextBinding` | FieldNotify push updates to a text widget at a configured interval. |
| `Performance.ModeMatrix` | Linear, CurveTable easing, and spring at 100 and 500 transitions. |
| `Performance.PipeHandoff` | Completion of active Pipe predecessors and startup of their queued linear or spring successors at 1/20 s. |
| `Performance.ConcurrentTick` | Scaling from 1 to 500 active transitions. |
| `Performance.Construction` | Direct construction, Create function, and full pure-node pipeline. |
| `Performance.FastBindings` | Direct adapters for common RenderOpacity, transform, and pivot properties. |

Dynamic Blueprint delegates are the dominant cost in Updated callbacks. `UpdateInterval` reduces dispatch frequency while preserving the final update. Direct adapters keep common widget-property writes out of reflection.

## Pipe handoff

`Performance.PipeHandoff` measures the exact completion frame of `Pipe`: the active transition reaches its target, is removed, and the queued successor for the same widget property is admitted in that same `Tick`. Setup and queue insertion happen before the timer. Each configuration takes 200 independent samples for 1, 20, 100, and 500 properties.

The test covers two meaningful successors:

- Linear: removal, property-value handoff, binding resolution, and activation.
- Spring: the same path plus lazy creation of the spring simulation state. A queued spring must not allocate `FWidgetTransitionSpring` before its predecessor completes.

The reported total is per completion tick; the per-handoff figure divides it by the number of properties. This benchmark intentionally includes array removal and queue consumption, because both are part of the user-visible boundary between two piped transitions. It excludes construction and `StartTransition` calls used to prepare the active and queued pairs.
