# Тестирование

## Содержание

1. [Принципы](#принципы)
2. [Карта тестов по доменам](#карта-тестов-по-доменам)
3. [Как запускать](#как-запускать)
4. [Производительность](#производительность)
5. [Диагностика](#диагностика)
6. [История результатов](#история-результатов)

## Принципы

Automation разделён по назначению:

- **Runtime** проверяет публичное поведение и regression-инварианты.
- **Editor** проверяет Blueprint metadata и editor-facing API.
- **Diagnostics** фиксирует свойства хранения и selector-инварианты.
- **Performance** измеряет hot paths, но не имеет жёстких pass/fail порогов.
- **Experiments** — изолированные архитектурные гипотезы; они не являются текущим production contract.

Перед изменением runtime storage, callback dispatch или Blueprint metadata запускайте Runtime и Editor. После изменения hot path снимайте сопоставимый performance run на той же машине и конфигурации.

## Карта тестов по доменам

| Домен | Runtime / editor | Performance / diagnostics | Подробности |
| --- | --- | --- | --- |
| Transition | `SpecBuilders`, `PropertyBinding.Channels`, `ExplicitFrom`, `AddMode`, `PipeOrder`, `YoYo` | `Construction`, `ConcurrentTick`, `PipeHandoff`, `FastBindings` | [Transition](Transition.md) |
| Easing | `CubicEasing` | `ModeMatrix`; isolated Bézier experiments | [Easing](Easing.md), [CubicBezier](CubicBezier.md) |
| Spring | `Spring.Converges`, `Spring.FitToTime`, `TickDelta` | `ModeMatrix`, `SpringTargetUpdate`, storage experiments | [Spring](Spring.md), [Fit To Time](FitToTime.md) |
| Events | `CallbackCancellation`, `RepeatCallbackDelay`, `EventInterval`, `CallbackReentrancy`, `NativeBuilder` | `Callbacks`, `EventInterval`, text binding and async counter | [Events](Events.md) |
| Composer | `WidgetSelector.Runtime.Hierarchy` | `GridWaveTranslation`, `NoDuplicateDescendants` | [Composer](Composer.md) |
| Runtime storage | `RemoveAtSwap`, `StorageLayout` | mode/dirty index and storage experiments | [История оптимизаций](History/OptimizationHistory.md) |

## Как запускать

В Unreal Editor откройте Automation и отфильтруйте `UMGTransitions`.

```text
Automation RunTests UMGTransitions.WidgetTransition.Runtime
Automation RunTests UMGTransitions.WidgetTransition.Editor
Automation RunTests UMGTransitions.WidgetTransition.Diagnostics
Automation RunTests UMGTransitions.WidgetTransition.Performance.<TestName>
Automation RunTests UMGTransitions.WidgetTransition.Experiments.<TestName>
Automation RunTests UMGTransitions.WidgetSelector
```

На проверенной установке UE 5.7/macOS используйте executable внутри app bundle:

```bash
"/path/to/UE_5.7/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" \
  "/path/to/Project.uproject" \
  -unattended -NullRHI -nop4 -nosplash \
  -ExecCmds="Automation RunTests UMGTransitions; Quit" \
  -TestExit="Automation Test Queue Empty" \
  -abslog="/tmp/UMGTransitionsAutomation.log"
```

`UnrealEditor-Cmd` на этой конфигурации может потерять абсолютный путь проекта с пробелами; app executable надёжнее.

## Производительность

Сравнивайте только прогоны с одинаковыми:

- ревизией UE, платформой, архитектурой и build configuration;
- числом active transitions, binding и callbacks;
- warm-up, количеством frames/samples и test scenario;
- режимом linear/easing/spring и значениями параметров.

Небольшая разница одиночных запусков может быть шумом. Сначала повторите замер, затем проверяйте, изменился ли измеряемый hot path. Не переносите историческое число в README как универсальную гарантию.

Текущие ориентиры и полные методики предыдущих замеров: [Performance tests](History/PerformanceTests.md). Принятые и отклонённые архитектурные варианты: [История оптимизаций](History/OptimizationHistory.md).

## Диагностика

`StorageLayout` выводит размер и alignment runtime records. Эти данные помогают оценивать память и cache locality, но не заменяют end-to-end benchmark. Selector diagnostics проверяют geometry-wave и повторение descendants.

## История результатов

Каждая завершённая сессия с фактическим логом фиксируется в [журнале результатов](History/TestResults.md). Исторические отчёты сохранены намеренно: доменные страницы содержат вывод, а archive — исходный контекст, конфигурацию и числа.
