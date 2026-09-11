# UMGTransitions

UMGTransitions is an Unreal Engine 5 plugin for reusable runtime UMG property transitions. Build a Transition with compact Blueprint nodes, then apply it to widget properties, material parameters, text counters, and groups of widgets.

## What it includes

- property, transform, material scalar/vector, and color transitions;
- cubic Bézier easing editor and templates;
- analytical spring motion with optional Fit To Time;
- repeat, Yo Yo, Replace, Skip, and Pipe modes;
- async lifecycle events, FieldNotify-friendly updates, and configurable event interval;
- Widget Selector for hierarchy traversal, ordering, and stagger/wave choreography.

## Minimal Blueprint path

```text
Make Transition Value → Create Widget Transition → Add Widget Transition
```

## Performance

The runtime keeps transition work compact: common UMG properties use direct adapters, spring state is stored separately from linear transitions, and Pipe uses keyed FIFO queues. Actual cost depends primarily on active transition count, property binding, callbacks, and platform.

See the [testing methodology and current benchmarks](Docs/Ru/Testing.md) before comparing numbers.

## Requirements and installation

- Unreal Engine 5.7 or later.
- A C++ project, or a Blueprint project with a C++ toolchain for building the plugin.

Copy `UMGTransitions` to the project's `Plugins/` directory, enable it through **Edit → Plugins**, and restart the editor if requested.

## Documentation

- [Russian documentation](Docs/Ru/Index.md)
- [Getting started](Docs/Ru/Usage.md)
- [Transition](Docs/Ru/Transition.md)
- [Performance and Automation](Docs/Ru/Testing.md)
- [Known issues](Docs/Ru/KnownIssues.md)

## License

[MIT License](LICENSE)
