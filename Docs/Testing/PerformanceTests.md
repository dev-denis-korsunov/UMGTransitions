# Performance-тесты

Все тесты ниже имеют `PerfFilter` и не являются обычными unit-тестами. Они измеряются в Development Editor на одной машине; сопоставлять можно только одинаковые commandlet-прогоны, потому что абсолютные значения чувствительны к нагрузке процесса и ОС.

## Набор и методика

| Тест | Что делает | Зачем нужен |
| --- | --- | --- |
| `Performance.Construction` | 100 000 раз сравнивает ручную сборку структуры, `Create Widget Transition` и полную pure-цепочку. | Показывает цену удобного Blueprint API до попадания transition в subsystem. |
| `Performance.ConcurrentTick` | После прогрева выполняет 300 кадров по `1/60` для 1, 10, 100 и 500 transition с `UImage.RenderOpacity` binding. | Даёт общую картину стоимости hot path вместе с применением свойства. |
| `Performance.Callbacks` | Сравнивает 20, 30, 50 и 100 linear transition с `Updated` dynamic delegate и без callback, с binding и без него; каждый случай измеряется 300 кадров после прогрева. | Изолирует цену callback registry, копирования delegate, dispatch и reentrancy-проверки в реалистичном диапазоне подписок. |
| `Performance.ModeMatrix` | 300 кадров для Linear, CurveTable easing и Spring, с binding и без него, при 100 и 500 transition. CurveTable содержит `(0,0)`, `(0.5,0.2)`, `(1,1)`. | Главный сравнительный тест алгоритмов. Вариант без binding выделяет математику; с binding показывает цену типичного использования. |
| `Performance.FastBindings` | 500 linear transition для каждого direct adapter: opacity, translation, scale, shear, angle, pivot. | Не даёт fast paths незаметно деградировать до reflective fallback. |

`FRealCurve` резолвится при добавлении transition в subsystem. Уже запущенный transition не подхватывает изменения CurveTable: его надо создать заново. Для сравнения spring-вариантов используйте строку **without binding** — запись свойства добавляет шум, не относящийся к симуляции.

## Callback baseline

Прогон 2026-08-25, UE 5.7.4 / Mac arm64 Development, 20–100 linear transition / 300 кадров:

| Count | Без binding, μs/frame | `Updated`, μs/frame | С `RenderOpacity`, μs/frame | `Updated` + binding, μs/frame |
| ---: | ---: | ---: | ---: | ---: |
| 20 | 0.109 | 5.443 | 0.200 | 5.359 |
| 30 | 0.150 | 8.283 | 0.317 | 8.689 |
| 50 | 0.253 | 13.221 | 0.548 | 14.015 |
| 100 | 0.503 | 27.532 | 1.106 | 27.917 |

Итоговая цена bound `Updated` — около `0.27 μs/transition/frame`; она линейно масштабируется с числом подписок. Предыдущий стресс-прогон на 500 transition дал 134.044 μs/frame без binding и 138.431 с binding.

Stress-case, 500 linear transition / 300 кадров:

| Binding | Без callbacks, μs/frame | С bound `Updated`, μs/frame | Добавленная цена |
| --- | ---: | ---: | ---: |
| Нет | 2.648 | 134.044 | 131.396 μs/frame, ~0.263 μs/transition |
| `RenderOpacity` | 5.424 | 138.431 | 133.007 μs/frame, ~0.266 μs/transition |

Это стоимость реального Blueprint dynamic delegate: lookup в registry, копирование delegate, dispatch и вызов receiver. `Started` и `Finished` редки и не входят в hot-path benchmark. Не добавляйте `Updated` массово без необходимости; 500 подписок заметно дороже самой интерполяции.

## Текущие ориентиры

Последний сопоставимый прогон после перехода на `TArray<FWidgetTransition>`:

| Сценарий, 500 transition | Без binding, μs/frame | С binding, μs/frame |
| --- | ---: | ---: |
| Linear | 2.340 | 5.499 |
| CurveTable easing | 2.351 | 5.493 |
| Spring | 6.402 | 9.297 |

Storage-измерения: `TArray` spring — 4.212 μs/frame против 4.670 у `TSparseArray`; transition `TArray` — 2.591 μs/frame, после 500 `RemoveAtSwap` — 2.563. Это исторические эксперименты; их исходники и статус — в [Experiments.md](Experiments.md). Подробнее о причинах решений — в [OptimizationHistory.md](OptimizationHistory.md); полный журнал — в [TestResults.md](../TestResults.md).

Изолированные эксперименты индексов: специализированные mode passes в трёх независимых прогонах — в среднем 1.268 против 1.537 μs/frame для mixed pass (−17.5%); prebuilt dirty widget groups — 1.656 против 1.707 μs/frame (−3.0%). Mode benchmark всё ещё не включает реальный tick spring, binding, callbacks и удаление. Их границы и решение описаны в [ModeAndDirtyIndexExperiments.md](ModeAndDirtyIndexExperiments.md).

## Правило обновления

1. Собрать Development Editor.
2. Запустить ровно один perf-тест в новом commandlet-процессе.
3. Сохранить в журнале все строки `AddInfo`, конфигурацию и дату.
4. Не считать разницу в единицы процентов регрессией без повторного прогона.

Новый benchmark добавляется только вместе с описанием измеряемого пути и ответом на вопрос, какое архитектурное решение он должен подтвердить или опровергнуть.
