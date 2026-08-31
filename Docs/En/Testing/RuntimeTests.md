# Runtime tests

Runtime tests protect transition semantics and data-structure invariants. The suite covers property channels, spring convergence, invalid easing fallback, tick-delta clamping, update intervals, callback reentrancy, and `RemoveAtSwap` repairs. Tests use transient widgets and do not require project content.

Run the full group with:

```text
Automation RunTests UMGTransitions.WidgetTransition.Runtime
```

The most important invariant is callback safety: a `Started`, `Updated`, or `Finished` callback may clear, add, or replace transitions while the subsystem is dispatching events.
