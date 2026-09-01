# UMGTransitions — тестирование Widget Transition

[English documentation](../En/README.md)

Документация разделена по назначению, чтобы спецификация теста, методика измерения и исторические результаты не смешивались.

| Документ | Содержание |
| --- | --- |
| [RuntimeTests.md](Testing/RuntimeTests.md) | Runtime- и editor-тесты: что защищают и почему они нужны. |
| [PerformanceTests.md](Testing/PerformanceTests.md) | Performance-тесты, их методика, границы интерпретации и текущие baseline. |
| [OptimizationHistory.md](Testing/OptimizationHistory.md) | Решения по оптимизации, принятые и отклонённые эксперименты. |
| [Experiments.md](Testing/Experiments.md) | Изолированные и исторические perf-эксперименты, не входящие в регулярный набор. |
| [ModeAndDirtyIndexExperiments.md](Testing/ModeAndDirtyIndexExperiments.md) | Гипотезы и методика экспериментов для индексов режимов и dirty widget. |
| [TestResults.md](TestResults.md) | Хронологический журнал завершённых запусков. |

Регулярные тесты находятся в `Source/UMGTransitionsTests/Private/WidgetTransitionTests.cpp`; исторические эксперименты — в `Source/UMGTransitionsTests/Private/Experiments/`. Editor-only модуль `UMGTransitionsTests` компилирует их только при включённых development automation tests.

## Как запускать

В Unreal Editor откройте Automation и отфильтруйте `UMGTransitions.WidgetTransition`.

Команды для Automation:

```text
Automation RunTests UMGTransitions.WidgetTransition.Runtime
Automation RunTests UMGTransitions.WidgetTransition.Editor
Automation RunTests UMGTransitions.WidgetTransition.Diagnostics
Automation RunTests UMGTransitions.WidgetTransition.Performance.<TestName>
Automation RunTests UMGTransitions.WidgetTransition.Experiments.<TestName>
```

На проверенной установке UE 5.7 для macOS нужно запускать executable внутри app bundle. Отдельный bootstrap `Engine/Binaries/Mac/UnrealEditor-Cmd` теряет абсолютный путь этого проекта с пробелами:

```bash
"/path/to/UE_5.7/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" \
  "/path/to/ElasticUMGProject.uproject" \
  -unattended -NullRHI -nop4 -nosplash \
  -ExecCmds="Automation RunTests UMGTransitions.WidgetTransition; Quit" \
  -TestExit="Automation Test Queue Empty" \
  -abslog="/tmp/UMGTransitionsAutomation.log"
```

Runtime и editor-тесты должны запускаться перед изменениями хранения или Blueprint metadata. Performance-тесты не являются pass/fail порогами: их запускают отдельно, на одинаковой машине и конфигурации, затем добавляют результат в журнал.
