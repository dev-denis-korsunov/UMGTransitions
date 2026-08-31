# Experiments

This document is reserved for isolated experiments that are not part of the regular runtime baseline. Experiments must report the machine, engine version, build configuration, workload, and whether the result was accepted or rejected.

Rejected directions include forced `ParallelFor` for spring updates, sparse spring storage, and system-wide callback lookup by transition ID. They either added scheduling/indirection cost or did not produce a repeatable gain for the expected number of active transitions.
