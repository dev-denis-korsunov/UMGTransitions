# UMGTransitions testing

This section separates correctness specifications, benchmark methodology, and historical results.

| Document | Contents |
| --- | --- |
| [RuntimeTests.md](Testing/RuntimeTests.md) | Runtime and editor test coverage. |
| [PerformanceTests.md](Testing/PerformanceTests.md) | Benchmark scenarios and interpretation. |
| [OptimizationHistory.md](Testing/OptimizationHistory.md) | Accepted and rejected optimizations. |
| [Experiments.md](Testing/Experiments.md) | Isolated historical experiments. |
| [ModeAndDirtyIndexExperiments.md](Testing/ModeAndDirtyIndexExperiments.md) | Mode and dirty-widget index experiments. |
| [AsyncCallbackOptimization.md](Testing/AsyncCallbackOptimization.md) | Async callback storage roadmap. |
| [TestResults.md](TestResults.md) | Chronological test log. |

Regular tests live in `Source/UMGTransitionsTests/Private/WidgetTransitionTests.cpp`; historical experiments live in `Source/UMGTransitionsTests/Private/Experiments/`. The editor-only `UMGTransitionsTests` module compiles them only when development automation tests are enabled.

## Running tests

In Unreal Editor, open Automation and filter for `UMGTransitions.WidgetTransition`.

Typical commandlet commands:

```text
Automation RunTests UMGTransitions.WidgetTransition.Runtime
Automation RunTests UMGTransitions.WidgetTransition.Editor
Automation RunTests UMGTransitions.WidgetTransition.Diagnostics
Automation RunTests UMGTransitions.WidgetTransition.Performance.<TestName>
Automation RunTests UMGTransitions.WidgetTransition.Experiments.<TestName>
```

On the verified macOS UE 5.7 installation, launch the executable inside the editor app bundle. The separate `Engine/Binaries/Mac/UnrealEditor-Cmd` bootstrap does not preserve this project's absolute path with spaces:

```bash
"/path/to/UE_5.7/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" \
  "/path/to/ElasticUMGProject.uproject" \
  -unattended -NullRHI -nop4 -nosplash \
  -ExecCmds="Automation RunTests UMGTransitions.WidgetTransition; Quit" \
  -TestExit="Automation Test Queue Empty" \
  -abslog="/tmp/UMGTransitionsAutomation.log"
```

Performance tests are comparative measurements, not hard pass/fail thresholds. Compare runs made with the same editor configuration.
