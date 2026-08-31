# Performance tests

Performance tests use repeated frame loops after warm-up and report microseconds per frame and per transition. They are diagnostic measurements rather than fixed thresholds.

| Test | Scope |
| --- | --- |
| `Performance.Callbacks` | 20/30/50/100 linear transitions with no callback, lifecycle callbacks, and Updated callbacks, with and without binding. |
| `Performance.UpdateInterval` | Updated callback cost at intervals 0, 0.033, 0.05, and 0.1 seconds. |
| `Performance.AsyncTextCounter` | Plain text writes versus async Updated output. |
| `Performance.FieldNotifyTextBinding` | FieldNotify push updates to a text widget at a configured interval. |
| `Performance.ModeMatrix` | Linear, CurveTable easing, and spring at 100 and 500 transitions. |
| `Performance.ConcurrentTick` | Scaling from 1 to 500 active transitions. |
| `Performance.Construction` | Direct construction, Create function, and full pure-node pipeline. |
| `Performance.FastBindings` | Direct adapters for common RenderOpacity, transform, and pivot properties. |

Dynamic Blueprint delegates are the dominant cost in Updated callbacks. `UpdateInterval` reduces dispatch frequency while preserving the final update. Direct adapters keep common widget-property writes out of reflection.
