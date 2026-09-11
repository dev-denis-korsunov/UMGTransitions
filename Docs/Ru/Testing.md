# Тестирование

## Содержание

- [Методика сравнений](#методика-сравнений)
- [Общий A/B системы](#общий-ab-системы)
- [Масштабирование общей нагрузки](#масштабирование-общей-нагрузки)
- [Память и контейнеры](#память-и-контейнеры)
  - [Диагностические размеры](#диагностические-размеры)
  - [Плотность transition](#плотность-transition)
  - [Индексы режимов и dirty widgets](#индексы-режимов-и-dirty-widgets)
- [Карта тестов по доменам](#карта-тестов-по-доменам)
- [Запуск](#запуск)
- [Организация C++ тестов](#организация-c-тестов)
- [История](#история)


## Методика сравнений

| Поле отчёта | Что фиксируется |
| --- | --- |
| Ревизия | Commit каждого варианта; если не записан — дата серии |
| Окружение | UE, CPU/архитектура, Development Editor, открытый Editor или commandlet/NullRHI |
| Нагрузка | Количество widgets/transitions/callbacks, режим и binding |
| Граница таймера | Construction, tick, dispatch, handoff; что исключено |
| Повторы | Frames/samples, warm-up, число отдельных запусков |
| Результат | Единицы, A/B/C, причина решения и ограничения вывода |

**A/B в одном benchmark** сравнивает варианты под одной нагрузкой. **A/B ревизий** сравнивает сборки в одном окружении. **Исторические этапы** показывают развитие, но не доказывают причинность всей разницы. Одиночный сдвиг в несколько процентов нельзя автоматически объявлять ни ускорением, ни шумом.

Документация ниже использует сохранённые замеры 2026 года. Новых runtime-прогонов при редактуре не выполнялось. «Прошёл» относится к конкретной указанной серии.

## Общий A/B системы

09.09.2026, UE 5.7 / Mac arm64 Development. A: `70e79cc`, B: `25387c9`; по одному запуску всего Performance suite, 11/11 тестов у каждого. Это изменение семантики Fit To Time.

| Тест / режим | Нагрузка | Единицы | A | B |
| --- | --- | --- | ---: | ---: |
| ConcurrentTick, binding | 500 | мкс/кадр | 20.308 | 20.367 |
| ModeMatrix, linear без binding | 500 | мкс/кадр | 5.658 | 5.705 |
| ModeMatrix, linear с binding | 500 | мкс/кадр | 20.225 | 21.046 |
| ModeMatrix, spring без binding | 500 | мкс/кадр | 10.970 | 10.534 |
| ModeMatrix, spring с binding | 500 | мкс/кадр | 25.410 | 24.997 |
| SpringTargetUpdate | 500 | мкс/кадр | 27.126 | 25.805 |
| Updated без binding | 100 | мкс/кадр | 26.475 | 26.658 |
| Updated с binding | 100 | мкс/кадр | 29.617 | 29.530 |
| Pipe linear | 500 handoff | мкс/handoff | 0.817 | 0.761 |
| Pipe spring | 500 handoff | мкс/handoff | 0.856 | 0.761 |

**Вывод:** крупного общего ухудшения в этой серии нет. Разнонаправленные отличия одиночных запусков не доказывают ускорение отдельных путей. Для самого spring есть [повторный A/B](FitToTime.md#повторный-ab); для оценки кадра интерфейса нужно добавить Slate/layout/paint. Исходные логи: [журнал](History/TestResults.md).

## Масштабирование общей нагрузки

23.08.2026, UE 5.7 / Mac arm64 Development, ConcurrentTick: RenderOpacity, 300 кадров по 1/60 после прогрева.

| Переходов | 1 | 10 | 100 | 500 |
| --- | ---: | ---: | ---: | ---: |
| Мкс/кадр | 0.031 | 0.234 | 2.352 | 11.876 |

**Вывод:** в этой исторической версии стоимость растёт примерно с количеством активных переходов. Эти числа нельзя склеивать с A/B сентября: runtime изменился. Для production-бюджета измеряется нужная комбинация binding/events, а не только scalar tick.

## Память и контейнеры

### Диагностические размеры

Зафиксированные размеры arm64, а не повторный sizeof текущего HEAD:

| Record | Размер | Серия / условие |
| --- | ---: | --- |
| FWidgetTransitionPropertyBinding | 88 B | 26.08, FieldNotify |
| FWidgetTransition | 272 → 256 B | До/после callback sidecar |
| CallbackLinks | 8 B на transition | 01.09 |
| Lifecycle callbacks | 72 B | Только при наличии lifecycle |
| UpdateState | 80 B | При Updated или FieldNotify |
| Lifecycle event | 80 B | Временная запись |
| Async action | 432 B | 01.09, без GC/allocator overhead |

Исторический бюджет callback-cleanup (transition 256 + link 8; spring 80 + owner index 4), без reserve/allocator/widgets:

| Нагрузка | До, B | После, B |
| --- | ---: | ---: |
| 100 linear | 27 200 | 26 400 |
| 100 spring | 35 600 | 34 800 |
| 500 linear | 136 000 | 132 000 |
| 500 spring | 178 000 | 174 000 |

В более позднем Spring-описании встречался record 96 B; смешивать его с бюджетом 80 B нельзя. Актуальный размер устанавливает Diagnostics.StorageLayout. **Вывод:** sidecar экономит основной record, но обязательные links и optional states входят в бюджет. Подробнее: [расчёт cleanup](History/OptimizationHistory.md#итог-callback-cleanup).

### Плотность transition

24.08.2026, UE 5.7 / Mac arm64 Development, изолированные эксперименты:

| Нагрузка / контейнер | A | B | Единицы |
| --- | ---: | ---: | --- |
| 500 TSparseArray, packed / 50% fragmented | 3.202 | 3.469 | мкс/кадр |
| 500 TArray, packed / после 500 swap removals | 2.591 | 2.563 | мкс/кадр |
| 50 000 RemoveAtSwap | 0.034 | — | мкс/удаление |

**Вывод:** TArray сохраняет плотность после удаления; индексы нестабильны и должны чиниться у связанных records. Runtime.RemoveAtSwap защищает этот инвариант. Подробнее: [контейнеры](History/OptimizationHistory.md#контейнеры).

### Индексы режимов и dirty widgets

Изолированные compute/write-эксперименты, 500 переходов, 300 кадров; это не полный runtime tick.

| Вариант | A: прямой проход | B: индексный | Единицы |
| --- | ---: | ---: | --- |
| Общий evaluator по режимам | 1.438 | 1.368 | мкс/кадр |
| Специализированные mode passes, среднее 3 запусков | 1.537 | 1.268 | мкс/кадр |
| 500 writes / 50 widgets, группировка | 1.707 | 1.656 | мкс/кадр |

**Вывод:** специализация дала −17.5% в изолированной задаче; группировка dirty дала только −3.0% без доказанного сокращения setter/invalidation. Эти результаты не подтверждают ускорение полной системы с callbacks и spring. Подробнее: [индексные эксперименты](History/ModeAndDirtyIndexExperiments.md#результаты).

## Карта тестов по доменам

Все имена ниже относительно `UMGTransitions.WidgetTransition`, кроме Selector.

| Владелец | Проверки корректности | Замеры / подробные результаты |
| --- | --- | --- |
| Transition | Runtime.SpecBuilders, PropertyBinding.Channels, ExplicitFrom, AddMode, PipeOrder, YoYo | [Construction, binding, Pipe](Transition.md#производительность-и-тестирование) |
| CubicBezier | Runtime.CubicEasing | [ModeMatrix и solver A/B/C/D](CubicBezier.md#binary--lut--solvecubic) |
| Spring | Runtime.Spring.Converges, TickDelta | [Storage, ParallelFor, target update](Spring.md#производительность-и-тестирование) |
| FitToTime | Runtime.Spring.FitToTime | [Повторный A/B](FitToTime.md#повторный-ab) |
| Events | Runtime.CallbackCancellation, RepeatCallbackDelay, EventInterval, CallbackReentrancy, NativeBuilder | [Callbacks, интервалы, этапы dispatch](Events.md#производительность-и-тестирование) |
| Usage | Потребители числа в performance fixtures | [ExternalTextBinding, FieldNotifyTextBinding, AsyncTextCounter](Usage.md#стоимость-счётчика) |
| Composer | UMGTransitions.WidgetSelector.Runtime.Hierarchy и Diagnostics | [Покрытие и ограничения](Composer.md#производительность-и-тестирование) |
| Общие | Runtime.RemoveAtSwap, Editor.Metadata, Diagnostics.StorageLayout | ConcurrentTick, mode/dirty index experiments |
| ColorMix | Выделенного regression-набора нет | [Пробелы и стоимость по коду](ColorMix.md#производительность-и-тестирование) |

## Запуск

В Automation Editor фильтр — `UMGTransitions`. Текущие имена используют **EventInterval**; UpdateInterval/UpdateRate в архиве — прежние API.

```text
Automation RunTests UMGTransitions.WidgetTransition.Runtime
Automation RunTests UMGTransitions.WidgetTransition.Editor
Automation RunTests UMGTransitions.WidgetTransition.Diagnostics
Automation RunTests UMGTransitions.WidgetTransition.Performance
Automation RunTests UMGTransitions.WidgetTransition.Experiments
Automation RunTests UMGTransitions.WidgetSelector
```

На проверенной macOS-установке используется executable внутри app bundle; это сохраняет путь проекта с пробелами:

```bash
"/path/to/UE_5.7/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" \
  "/path/to/Project.uproject" \
  -unattended -NullRHI -nop4 -nosplash \
  -ExecCmds="Automation RunTests UMGTransitions.WidgetTransition.Runtime" \
  -TestExit="Automation Test Queue Empty" \
  -abslog="/tmp/UMGTransitionsAutomation.log"
```

Для сравнения скорости запускайте конкретный Performance-тест в отдельном процессе после одинакового warm-up. Exit code дополняется проверкой Test Completed в логе; не заменяет её.

## Организация C++ тестов

Исходники пока сохраняют существующие файлы и Automation paths. Последующее разделение удобно по владельцам из таблицы: общий fixture, Transition, Spring/FitToTime, Easing/ColorMix, Events, Selector и отдельные performance experiments.

- Общие factories/receivers переиспользуются.
- Automation paths сохраняются, чтобы команды не менялись.
- Междоменные TickDelta/RemoveAtSwap остаются общими.
- Диагностика отделяется от тестов pass/fail: размер record — измерение, не универсальный инвариант.

## История

[Журнал результатов](History/TestResults.md), [архитектурные этапы](History/OptimizationHistory.md), [исходная методика performance](History/PerformanceTests.md), [runtime coverage](History/RuntimeTests.md). Старые названия, размеры и числа в архиве принадлежат указанным этапам.
