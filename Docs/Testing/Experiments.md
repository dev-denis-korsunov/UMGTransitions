# Historical performance experiments

These `PerfFilter` tests preserve the evidence behind decisions that are already made or not yet integrated into runtime. They are intentionally separate from the regular performance suite and should be run only when revisiting the corresponding architecture.

| Test | Source | Status |
| --- | --- | --- |
| `Experiments.SpringStorage` | `Tests/Experiments/WidgetTransitionStorageExperiments.cpp` | Historical: dense `TArray` was selected over sparse storage. |
| `Experiments.SpringParallelFor` | `Tests/Experiments/WidgetTransitionStorageExperiments.cpp` | Historical: `ParallelFor` was 11.5–16.1× slower on the isolated dense spring pass; sequential loop retained. |
| `Experiments.ArrayTransitionStorage` | `Tests/Experiments/WidgetTransitionStorageExperiments.cpp` | Historical: dense transition storage and `RemoveAtSwap` were selected. |
| `Experiments.ModeIndices` | `Tests/Experiments/WidgetTransitionIndexExperiments.cpp` | Pending integration: specialized passes were faster in an isolated compute benchmark. |

`DirtyWidgetIndices` was removed from source after the experiment. Its result remains in [ModeAndDirtyIndexExperiments.md](ModeAndDirtyIndexExperiments.md): grouping alone does not reduce safe UE invalidation work.
