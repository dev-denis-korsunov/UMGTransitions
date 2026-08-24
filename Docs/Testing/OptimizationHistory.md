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

## Spring

| Этап | 500 spring без binding, μs/frame | Решение |
| --- | ---: | --- |
| Параметры и completion check в tick | 11.485 | Baseline. |
| Кеширование frequency/damping | 8.986 | Принято. |
| Squared completion check | 8.359 | Принято: убирает `Sqrt`. |
| Independent dense spring pass | 7.424, затем 7.368 с delay внутри spring | Принято: spring state считается плотным первым проходом. |
| `TArray<FWidgetTransition>` + `RemoveAtSwap` | 6.402 | Принято: transition loop также стал плотным. |

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
