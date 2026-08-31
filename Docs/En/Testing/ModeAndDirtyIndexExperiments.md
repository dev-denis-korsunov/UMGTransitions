# Mode and dirty-widget index experiments

Mode indices group immutable transitions into Linear, Easing, and Spring passes. Dirty-widget indices group writes by widget. The measurements showed a possible gain for specialized mode passes, while dirty-widget grouping provided only a small locality improvement and did not remove UMG setter or invalidation costs.

These ideas remain experimental. Any production adoption must preserve `RemoveAtSwap` bookkeeping and prove a benefit in a representative workload.
