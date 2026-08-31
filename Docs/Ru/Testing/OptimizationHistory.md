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

Следующий этап заменил `TMap<TransitionId, Callbacks>` на dense registry с обратными индексами. `Updated` callback-и имеют собственный плотный массив индексов и единственный hot-pass. `Started` и `Finished` копируются в короткие очереди событий и вызываются после transition pass только при наступлении события. `RemoveAtSwap` обновляет обе стороны индексов; callback по-прежнему может удалить или добавить transition.

Текущий этап полностью убрал callback bookkeeping из `FWidgetTransition`. Пара `LifecycleIndex`/`UpdateStateIndex` хранится в параллельном `CallbackLinks`. Во время transition и callback pass удаления только добавляют индекс в очередь; после dispatch очередь обрабатывается по убыванию, а sidecar и owner-индексы чинятся вместе с `RemoveAtSwap`. Поэтому `TransitionId` и reentrancy-проверка по стабильному ID больше не нужны.

Первый sidecar-вариант каждый кадр отправлял progress и value из transition pass в update state. Pull-эксперимент уменьшил update state до `48 B`, но повторный sample в callback pass поднял стоимость 100 `Updated` до `83.676/85.329 μs/frame`. Гибрид вычислял sample один раз в transition pass и сохранял его только для transition с update state. Общий `bHasUpdateStates` fast-path не трогал `CallbackLinks`, когда update-sidecar пуст.

| 100 linear transition | Без binding, μs/frame | С `RenderOpacity`, μs/frame |
| --- | ---: | ---: |
| No callback | 6.224 | 7.667 |
| Lifecycle-only (`Started` + `Finished`) | 6.339 | 7.783 |
| Bound `Updated` | 79.678 | 80.243 |

Финальный API удалил widget и progress из `Updated`: событие возвращает только `FWidgetTransitionValue`, который лениво семплируется непосредственно перед dispatch. На границе repeat сохраняется редкий override snapshot, чтобы callback получил финальное значение завершённого цикла, а не начало следующего. В двух одинаковых Editor-прогонах 100 value-only `Updated` заняли `83.256–83.812/84.086–86.959 μs/frame` против `79.678/80.243` у единственного hybrid-прогона. `AsyncTextCounter` при этом стабильно улучшился с `83.403` до `79.750–80.535 μs/frame`, потому что async action больше не восстанавливает value из progress.

Решение принято как UX/архитектурный компромисс: `FWidgetTransition` уменьшен с `272` до `256 B`, а вместе с обязательным `8 B` link бюджет составляет `264 B` на transition. Lifecycle остаётся cold path, transition без callbacks не получил измеримой регрессии внутри одинаковой Editor-сессии (`6.248–6.477 μs/frame` на 100). `FWidgetTransitionUpdateState` занимает `80 B` только при наличии `Updated` или FieldNotify. Безопасность подтверждают `Runtime.CallbackReentrancy`, `Runtime.RemoveAtSwap`, `Runtime.UpdateInterval` и repeat-value regression внутри interval-теста.

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

`TArray` делает внутренние индексы нестабильными. При swap spring обновляется `SpringIndex` владельца, при swap transition обновляются `SpringTransitionIndices`, параллельный `CallbackLinks` и owner-индексы callback records. Внешний Blueprint API не публикует внутренний индекс transition.

## Индексы режимов и widget

| Эксперимент, 500 transition / 300 frames | Прямой обход, μs/frame | Индексный обход, μs/frame | Вывод |
| --- | ---: | ---: | --- |
| Mixed Linear/Easing/Spring → три mode pass, общий evaluator | 1.438 | 1.368 | Предварительный сигнал: проверки режима ещё остались. |
| Mixed Linear/Easing/Spring → специализированные mode pass, среднее 3 прогонов | 1.537 | 1.268 | −17.5%. Специализация окупает indirection в compute path; требуется runtime-интеграция. |
| 500 opacity writes / 50 widget → группы widget | 1.707 | 1.656 | Только небольшая locality-выгода; runtime пока не усложняется. |

Mode transition не меняет режим после `Add`, поэтому индексы `LinearTransitionIndices`, `EasingTransitionIndices` и `SpringTransitionIndices` не потребуют миграции по ходу жизни transition. При удалении остаётся обычный `RemoveAtSwap` bookkeeping для одной соответствующей группы. В отличие от этого, dirty widget grouping сам по себе не уменьшает число публичных setter или invalidation UE, поэтому его нельзя принимать лишь по этому микробенчмарку.
