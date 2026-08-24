# ElasticUMG — тестирование Widget Transition

Документация разделена по назначению, чтобы спецификация теста, методика измерения и исторические результаты не смешивались.

| Документ | Содержание |
| --- | --- |
| [RuntimeTests.md](Testing/RuntimeTests.md) | Runtime- и editor-тесты: что защищают и почему они нужны. |
| [PerformanceTests.md](Testing/PerformanceTests.md) | Performance-тесты, их методика, границы интерпретации и текущие baseline. |
| [OptimizationHistory.md](Testing/OptimizationHistory.md) | Решения по оптимизации, принятые и отклонённые эксперименты. |
| [TestResults.md](TestResults.md) | Хронологический журнал завершённых запусков. |

Все тесты находятся в `Source/ElasticUMG/Private/Tests/WidgetTransitionTests.cpp` и включены только при `WITH_DEV_AUTOMATION_TESTS`.

## Как запускать

В Unreal Editor откройте Automation и отфильтруйте `ElasticUMG.WidgetTransition`.

Для commandlet-прогона используйте `UnrealEditor-Cmd` с проектом и `-NullRHI`:

```text
Automation RunTests ElasticUMG.WidgetTransition.Runtime
Automation RunTests ElasticUMG.WidgetTransition.Editor
Automation RunTests ElasticUMG.WidgetTransition.Performance.<TestName>
```

Runtime и editor-тесты должны запускаться перед изменениями хранения или Blueprint metadata. Performance-тесты не являются pass/fail порогами: их запускают отдельно, на одинаковой машине и конфигурации, затем добавляют результат в журнал.
