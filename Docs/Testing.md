# UMGTransitions — тестирование Widget Transition

Документация разделена по назначению, чтобы спецификация теста, методика измерения и исторические результаты не смешивались.

| Документ | Содержание |
| --- | --- |
| [RuntimeTests.md](Testing/RuntimeTests.md) | Runtime- и editor-тесты: что защищают и почему они нужны. |
| [PerformanceTests.md](Testing/PerformanceTests.md) | Performance-тесты, их методика, границы интерпретации и текущие baseline. |
| [OptimizationHistory.md](Testing/OptimizationHistory.md) | Решения по оптимизации, принятые и отклонённые эксперименты. |
| [Experiments.md](Testing/Experiments.md) | Изолированные и исторические perf-эксперименты, не входящие в регулярный набор. |
| [ModeAndDirtyIndexExperiments.md](Testing/ModeAndDirtyIndexExperiments.md) | Гипотезы и методика экспериментов для индексов режимов и dirty widget. |
| [TestResults.md](TestResults.md) | Хронологический журнал завершённых запусков. |

Регулярные тесты находятся в `Source/UMGTransitions/Private/Tests/WidgetTransitionTests.cpp`; исторические эксперименты — в `Tests/Experiments/`. Все включены только при `WITH_DEV_AUTOMATION_TESTS`.

## Как запускать

В Unreal Editor откройте Automation и отфильтруйте `UMGTransitions.WidgetTransition`.

Для commandlet-прогона используйте `UnrealEditor-Cmd` с проектом и `-NullRHI`:

```text
Automation RunTests UMGTransitions.WidgetTransition.Runtime
Automation RunTests UMGTransitions.WidgetTransition.Editor
Automation RunTests UMGTransitions.WidgetTransition.Diagnostics
Automation RunTests UMGTransitions.WidgetTransition.Performance.<TestName>
Automation RunTests UMGTransitions.WidgetTransition.Experiments.<TestName>
```

Runtime и editor-тесты должны запускаться перед изменениями хранения или Blueprint metadata. Performance-тесты не являются pass/fail порогами: их запускают отдельно, на одинаковой машине и конфигурации, затем добавляют результат в журнал.
