# Runtime tests

Runtime tests protect transition semantics and data-structure invariants. The suite covers property channels, spring convergence, invalid easing fallback, tick-delta clamping, update intervals, callback reentrancy, and `RemoveAtSwap` repairs. Tests use transient widgets and do not require project content.

Run the full group with:

```text
Automation RunTests UMGTransitions.WidgetTransition.Runtime
```

The most important invariant is callback safety: a `Started`, `Updated`, or `Finished` callback may clear, add, or replace transitions while the subsystem is dispatching events.

`WidgetSelector.Diagnostics.GridWaveTranslation` creates a deterministic 5×5 grid of equal `UImage` slots and verifies `RenderTransform.Translation = WaveDirection * 40`: the center stays still and every other cell has the same translation length. This isolates wave-direction math from Delay and traversal order.

`WidgetSelector.Diagnostics.NoDuplicateDescendants` verifies that adding the same widget to a panel twice does not produce a duplicate entry and that `GetWidgetDescendants` returns each widget pointer exactly once.
