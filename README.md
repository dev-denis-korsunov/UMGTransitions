# UMGTransitions

UMGTransitions is an Unreal Engine 5 plugin for runtime UMG transitions. It provides compact Blueprint nodes for animating widget properties, CurveTable easing, spring motion, lifecycle events, and selection of widgets from nested UMG hierarchies.

## Requirements

- Unreal Engine 5.7 or later
- C++ project, or a Blueprint project with a C++ toolchain available to build the plugin

## Installation

1. Copy `UMGTransitions` into your project's `Plugins/` directory.
2. Regenerate project files if Unreal asks for it, then build the project.
3. Enable **UMGTransitions** in **Edit → Plugins** and restart the editor.

The plugin includes runtime code and an editor module for the Widget Property picker.

## Widget transitions

Build a transition with pure nodes, then start it with **Add Widget Transition** or **Add Widget Transition Array**.

1. Create a `Transition Value` with **Make Float Transition Value**, **Make Vector2D Transition Value**, or **Make Color Transition Value**.
2. Use **Create Widget Transition** to choose a target `Widget` and `Widget Property`.
3. Expand **Create Widget Transition** for optional repeat, Yo Yo, repeat delay, ColorMix, removal, and event interval settings; compose **From**, **Easing**, and **Spring** only when needed.
4. Start the result with **Add Widget Transition**.

The Widget Property picker exposes supported numeric widget properties, slot properties, and material parameters for supported Image and Border widgets. Frequently used UMG fields (`RenderOpacity`, transform translation/scale/shear/angle, and pivot) use direct runtime adapters; other compatible float, `Vector2D`, and `LinearColor` properties use the property-path fallback.

`Repeat Count = -1` repeats indefinitely. Spring settings use normalized `Spring Force` and `Spring Damping` values in the range 0–1; `Spring Max Speed = 0` leaves velocity unrestricted. CurveTable rows are resolved when the transition is added, so changing a curve affects transitions created afterwards.

For execution pins, use **Add Widget Transition Async**. It exposes `Started`, `Updated`, and `Finished` while retaining the same transition definition.

## Widget Selector

`Widget Selector` is a Blueprint function library for traversing UMG hierarchies:

- get a User Widget's tree root or a named widget;
- select direct children, all descendants, a specific depth, or all levels through a depth;
- navigate to a parent or the full parent chain;
- find descendants by one or several names.

Traversal is depth-first and crosses nested `WidgetTree → UUserWidget` boundaries, so composed User Widgets behave as one hierarchy.

## Examples and tests

Example assets are included under `Content/TransitionExamples`.

Documentation is available in [English](Docs/En/README.md) and [Russian](Docs/Ru/README.md). Automation tests and benchmark notes are separate from historical performance experiments.
Confirmed runtime defects and their required regression coverage are tracked in [English](Docs/En/KnownIssues.md) and [Russian](Docs/Ru/KnownIssues.md).

## License

UMGTransitions is released under the [MIT License](LICENSE).
