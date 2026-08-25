# История оптимизаций runtime

Этот документ связывает измерения с принятыми решениями. Полные строки запусков хранятся в [TestResults.md](../TestResults.md).

## CurveTable easing

Раньше `FCurveTableRowHandle::Eval` искал row каждый tick. Теперь `FRealCurve*` резолвится при добавлении transition и затем вызывается напрямую.

| Этап | 500 easing без binding, μs/frame | Почему |
| --- | ---: | --- |
| Lookup каждый tick | 26.301 | Поиск CurveTable доминировал над lerp. |
| Кешированный `FRealCurve*` | 2.831 | Easing стал сопоставим с linear (2.862 в том прогоне). |

Решение принято: кривая — cold configuration, её lookup не должен быть частью hot path.

## Fast widget property adapters

`RenderOpacity`, поля `RenderTransform` и pivot распознаются при resolve и применяются через публичные `UWidget::Get/Set...`, а не reflective `FDynamicPropertyPath`.

| Путь | 500 opacity, μs/frame | Почему |
| --- | ---: | --- |
| Generic property path | 19.955 | Reflection и property-path traversal на каждом update. |
| Direct adapter | 5.995 | Убрана reflection из частого стандартного случая. |

Решение принято: fast adapters для частых стандартных свойств; произвольные свойства и material parameters сохраняют fallback.

## Callback hot/cold split

Lifecycle callbacks (`Started`, `Finished`) происходят один раз, а `Updated` — каждый кадр. Раньше один `bHasCallbacks` направлял все три случая через lookup в `Callbacks` и update ветвление каждого активного transition.

| 100 linear transition | Без binding, μs/frame | С `RenderOpacity`, μs/frame |
| --- | ---: | ---: |
| No callback | 0.503 | 1.143 |
| Lifecycle-only (`Started` + `Finished`) | 0.537 | 1.190 |
| Bound `Updated` | 27.771 | 28.043 |

Решение принято: хранить отдельные bitfield-флаги `bHasStartedCallback`, `bHasUpdatedCallback` и `bHasFinishedCallback`; async action привязывает внутренний delegate только для подключённого execution output. Lifecycle-only переходы не платят per-frame стоимость. `Updated` остаётся явной opt-in ценой Blueprint dynamic delegate — около 0.27–0.28 μs/transition/frame.

Временный unsafe-эксперимент без локальной копии `Updated` delegate дал для 100 callback 26.020 вместо 27.771 μs/frame (около −6.3%), но он позволяет callback удалить запись `TMap` во время её исполнения и потому отклонён. Это не heap allocation: `TScriptDelegate` в UE при копировании переносит только weak object pointer и `FName`. Основная цена остаётся в dynamic `ProcessEvent`; безопасная копия сохраняется.

## Spring

| Этап | 500 spring без binding, μs/frame | Решение |
| --- | ---: | --- |
| Параметры и completion check в tick | 11.485 | Baseline. |
| Кеширование frequency/damping | 8.986 | Принято. |
| Squared completion check | 8.359 | Принято: убирает `Sqrt`. |
| Independent dense spring pass | 7.424, затем 7.368 с delay внутри spring | Принято: spring state считается плотным первым проходом. |
| `TArray<FWidgetTransition>` + `RemoveAtSwap` | 6.402 | Принято: transition loop также стал плотным. |

### ParallelFor для dense spring pass

`Experiments.SpringParallelFor` измеряет только безопасный compute-pass `TArray<FWidgetTransitionSpring>`: каждый spring обновляет собственные значения и не обращается к `UObject`, `UWidget`, delegates или `Transitions`. После implicit barrier `ParallelFor` основной transition pass всё равно выполняется на Game Thread и применяет значения к widget.

Тест сравнивает sequential, принудительный `ParallelFor` и вариант с `ParallelSpringThreshold = 256` на 64, 128, 256, 500 и 1000 активных spring. Он также сверяет, что результат каждого spring совпадает с последовательным вычислением.

| Active spring | Sequential, μs/frame | Forced `ParallelFor`, μs/frame | Thresholded (256), μs/frame | Вывод |
| ---: | ---: | ---: | ---: | --- |
| 64 | 0.518 | 8.336 | 0.540 | Порог оставляет последовательный путь; overhead +4.1%. |
| 128 | 1.069 | 15.688 | 0.986 | Последовательный путь в пределах шума. |
| 256 | 2.129 | 25.802 | 24.543 | На пороге parallel путь в 11.5× медленнее. |
| 500 | 3.828 | 51.405 | 50.417 | Parallel путь в 13.2× медленнее. |
| 1000 | 8.212 | 98.268 | 95.632 | Parallel путь в 11.6× медленнее. |

Замер выполнен на MacBook Pro (Apple Silicon), UE 5.7, Development Editor. Решение: `ParallelFor` в runtime не включать и порог не хранить. Даже при 1000 spring стоимость scheduling и barrier значительно выше самостоятельного вычисления `Exp`/`Sin`/`Cos`; основная работа с widget всё равно должна остаться на Game Thread.

Отклонённые варианты:

- Отдельный `TSparseArray<FWidgetTransitionSpring>`: 10.437 μs/frame. Sparse lookup и лишние проходы перекрыли выгоду locality.
- `int32 TransitionIndex` внутри spring: скорость в пределах шума (6.558 против 6.402), но размер spring вырос с 80 до 96 B из-за выравнивания. Отдельный `SpringTransitionIndices` экономнее примерно на 6 KB при 500 spring.

## Контейнеры

| Эксперимент | Результат | Вывод |
| --- | --- | --- |
| Spring `TArray` vs `TSparseArray` | 4.212 vs 4.670 μs/frame | Spring хранится в dense `TArray`. |
| Transition sparse, packed vs 50% fragmented | 3.202 vs 3.469 μs/frame | Фрагментация имеет цену. |
| Transition `TArray`, packed vs после 500 swap removals | 2.591 vs 2.563 μs/frame | `RemoveAtSwap` сохраняет плотность и не ухудшает tick. |
| 50 000 `RemoveAtSwap` | 0.034 μs/removal | Удаление достаточно дешёвое для ожидаемого числа transition. |

`TArray` делает внутренние индексы нестабильными. Это компенсируется в двух helper-ах: при swap spring обновляется `SpringIndex` владельца, при swap transition обновляется `SpringTransitionIndices` владельца spring. Внешний API использует монотонный `TransitionId`, а не индекс массива.

## Индексы режимов и widget

| Эксперимент, 500 transition / 300 frames | Прямой обход, μs/frame | Индексный обход, μs/frame | Вывод |
| --- | ---: | ---: | --- |
| Mixed Linear/Easing/Spring → три mode pass, общий evaluator | 1.438 | 1.368 | Предварительный сигнал: проверки режима ещё остались. |
| Mixed Linear/Easing/Spring → специализированные mode pass, среднее 3 прогонов | 1.537 | 1.268 | −17.5%. Специализация окупает indirection в compute path; требуется runtime-интеграция. |
| 500 opacity writes / 50 widget → группы widget | 1.707 | 1.656 | Только небольшая locality-выгода; runtime пока не усложняется. |

Mode transition не меняет режим после `Add`, поэтому индексы `LinearTransitionIndices`, `EasingTransitionIndices` и `SpringTransitionIndices` не потребуют миграции по ходу жизни transition. При удалении остаётся обычный `RemoveAtSwap` bookkeeping для одной соответствующей группы. В отличие от этого, dirty widget grouping сам по себе не уменьшает число публичных setter или invalidation UE, поэтому его нельзя принимать лишь по этому микробенчмарку.
