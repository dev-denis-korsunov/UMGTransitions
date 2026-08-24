# Performance-тесты

Все тесты ниже имеют `PerfFilter` и не являются обычными unit-тестами. Они измеряются в Development Editor на одной машине; сопоставлять можно только одинаковые commandlet-прогоны, потому что абсолютные значения чувствительны к нагрузке процесса и ОС.

## Набор и методика

| Тест | Что делает | Зачем нужен |
| --- | --- | --- |
| `Performance.Construction` | 100 000 раз сравнивает ручную сборку структуры, `Create Widget Transition` и полную pure-цепочку. | Показывает цену удобного Blueprint API до попадания transition в subsystem. |
| `Performance.ConcurrentTick` | После прогрева выполняет 300 кадров по `1/60` для 1, 10, 100 и 500 transition с `UImage.RenderOpacity` binding. | Даёт общую картину стоимости hot path вместе с применением свойства. |
| `Performance.ModeMatrix` | 300 кадров для Linear, CurveTable easing и Spring, с binding и без него, при 100 и 500 transition. CurveTable содержит `(0,0)`, `(0.5,0.2)`, `(1,1)`. | Главный сравнительный тест алгоритмов. Вариант без binding выделяет математику; с binding показывает цену типичного использования. |
| `Performance.FastBindings` | 500 linear transition для каждого direct adapter: opacity, translation, scale, shear, angle, pivot. | Не даёт fast paths незаметно деградировать до reflective fallback. |
| `Performance.SpringStorage` | 500 одинаковых spring, 300 кадров: dense `TArray`, заполненный `TSparseArray`, `TSparseArray` с 50% дырок. | Изолирует locality spring state от widget binding и transition loop. |
| `Performance.ArrayTransitionStorage` | Реальный tick 500 linear transition в `TArray` до и после 500 `RemoveAtSwap`; отдельно 50 000 plain `RemoveAtSwap`. | Подтверждает выбор плотного transition storage и измеряет цену удаления. |

`FRealCurve` резолвится при добавлении transition в subsystem. Уже запущенный transition не подхватывает изменения CurveTable: его надо создать заново. Для сравнения spring-вариантов используйте строку **without binding** — запись свойства добавляет шум, не относящийся к симуляции.

## Текущие ориентиры

Последний сопоставимый прогон после перехода на `TArray<FWidgetTransition>`:

| Сценарий, 500 transition | Без binding, μs/frame | С binding, μs/frame |
| --- | ---: | ---: |
| Linear | 2.340 | 5.499 |
| CurveTable easing | 2.351 | 5.493 |
| Spring | 6.402 | 9.297 |

Storage-измерения: `TArray` spring — 4.212 μs/frame против 4.670 у `TSparseArray`; transition `TArray` — 2.591 μs/frame, после 500 `RemoveAtSwap` — 2.563. Подробнее о причинах решений — в [OptimizationHistory.md](OptimizationHistory.md); полный журнал — в [TestResults.md](../TestResults.md).

## Правило обновления

1. Собрать Development Editor.
2. Запустить ровно один perf-тест в новом commandlet-процессе.
3. Сохранить в журнале все строки `AddInfo`, конфигурацию и дату.
4. Не считать разницу в единицы процентов регрессией без повторного прогона.

Новый benchmark добавляется только вместе с описанием измеряемого пути и ответом на вопрос, какое архитектурное решение он должен подтвердить или опровергнуть.
