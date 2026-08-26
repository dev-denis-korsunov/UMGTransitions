# Performance-тесты

Все тесты ниже имеют `PerfFilter` и не являются обычными unit-тестами. Они измеряются в Development Editor на одной машине; сопоставлять можно только одинаковые commandlet-прогоны, потому что абсолютные значения чувствительны к нагрузке процесса и ОС.

## Набор и методика

| Тест | Что делает | Зачем нужен |
| --- | --- | --- |
| `Performance.Construction` | 100 000 раз сравнивает ручную сборку структуры, `Create Widget Transition` и полную pure-цепочку. | Показывает цену удобного Blueprint API до попадания transition в subsystem. |
| `Performance.ConcurrentTick` | После прогрева выполняет 300 кадров по `1/60` для 1, 10, 100 и 500 transition с `UImage.RenderOpacity` binding. | Даёт общую картину стоимости hot path вместе с применением свойства. |
| `Performance.Callbacks` | Сравнивает 20, 30, 50 и 100 linear transition без callback, с lifecycle-only (`Started` + `Finished`) и с `Updated` dynamic delegate, с binding и без него; каждый случай измеряется 300 кадров после прогрева. | Проверяет hot/cold split callback-ов и изолирует цену registry, копирования delegate, dispatch и reentrancy-проверки. |
| `Performance.UpdateInterval` | 100 linear transition с bound `Updated`, без binding property, при интервале 0, `1/30`, `1/20` и `0.1` секунды. | Проверяет, что time-based throttling callback dispatch масштабирует hot-path стоимость ожидаемо и не зависит от FPS. |
| `Performance.AsyncTextCounter` | Измеряет 100 async transition без `Widget Property`: output `Updated` интерполирует float `0→100`, а receiver записывает целое значение в `UTextBlock`. | Показывает цену пользовательского обновления счётчика без property binding плагина. |
| `Performance.ModeMatrix` | 300 кадров для Linear, CurveTable easing и Spring, с binding и без него, при 100 и 500 transition. CurveTable содержит `(0,0)`, `(0.5,0.2)`, `(1,1)`. | Главный сравнительный тест алгоритмов. Вариант без binding выделяет математику; с binding показывает цену типичного использования. |
| `Performance.FastBindings` | 500 linear transition для каждого direct adapter: opacity, translation, scale, shear, angle, pivot. | Не даёт fast paths незаметно деградировать до reflective fallback. |

`FRealCurve` резолвится при добавлении transition в subsystem. Уже запущенный transition не подхватывает изменения CurveTable: его надо создать заново. Для сравнения spring-вариантов используйте строку **without binding** — запись свойства добавляет шум, не относящийся к симуляции.

## Callback baseline

Прогон 2026-08-25, UE 5.7.4 / Mac arm64 Development, 20–100 linear transition / 300 кадров:

| Count | No callback | Lifecycle-only | `Updated` | No callback + binding | Lifecycle-only + binding | `Updated` + binding |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 20 | 0.117 | 0.107 | 5.479 | 0.213 | 0.215 | 5.741 |
| 30 | 0.147 | 0.147 | 8.229 | 0.315 | 0.316 | 8.435 |
| 50 | 0.239 | 0.254 | 13.698 | 0.558 | 0.555 | 14.186 |
| 100 | 0.503 | 0.537 | 27.771 | 1.143 | 1.190 | 28.043 |

Значения — μs/frame. Отдельные флаги `Started`, `Updated` и `Finished` убирают map lookup и dynamic delegate из hot path lifecycle-only transition: их результат в пределах шума совпадает с no-callback baseline. Цена bound `Updated` остаётся около `0.27–0.28 μs/transition/frame` и линейно масштабируется с числом подписок. Предыдущий стресс-прогон на 500 transition дал 134.044 μs/frame без binding и 138.431 с binding.

Stress-case, 500 linear transition / 300 кадров:

| Binding | Без callbacks, μs/frame | С bound `Updated`, μs/frame | Добавленная цена |
| --- | ---: | ---: | ---: |
| Нет | 2.648 | 134.044 | 131.396 μs/frame, ~0.263 μs/transition |
| `RenderOpacity` | 5.424 | 138.431 | 133.007 μs/frame, ~0.266 μs/transition |

Это стоимость реального Blueprint dynamic delegate: lookup в registry, копирование delegate, dispatch и вызов receiver. `Started` и `Finished` редки и не входят в hot-path benchmark. Не добавляйте `Updated` массово без необходимости; 500 подписок заметно дороже самой интерполяции.

## Исторический frame-rate baseline

Прогон 2026-08-25, UE 5.7.4 / Mac arm64 Development, 100 linear transition без property binding / 300 кадров. Удалённый `Update Every N Frames` ограничивал только bound `Updated`; вычисление transition и запись property не менялись.

| Frames | μs/frame | μs/transition |
| ---: | ---: | ---: |
| 1 | 27.515 | 0.275 |
| 2 | 14.094 | 0.141 |
| 3 | 9.291 | 0.093 |
| 6 | 5.102 | 0.051 |

Rate `2` почти вдвое сокращал hot-path стоимость callback. API заменён на `Update Interval` в секундах, поскольку frame rate зависит от FPS. Завершающий `Updated` выполняется принудительно, чтобы consumer всегда получил target, даже если transition закончился до следующего интервала.

## Update interval baseline

Прогон 2026-08-26, UE 5.7.4 / Mac arm64 Development, 100 linear transition без property binding / 300 кадров. `Update Interval` ограничивает только bound `Updated`; `0` сохраняет update каждый Tick, default async-ноды — `0.033` секунды.

| Interval, s | μs/frame | μs/transition |
| ---: | ---: | ---: |
| 0 | 28.333 | 0.283 |
| 0.033 (async default) | 15.140 | 0.151 |
| 0.050 (`1/20`) | 10.233 | 0.102 |
| 0.100 | 5.298 | 0.053 |

При 60 FPS default `0.033` выполняет примерно 30 callback/сек на transition и почти вдвое уменьшает цену `Updated` относительно per-frame режима. При перегруженном кадре система не догоняет пропущенные события: один callback получает актуальное значение. Завершающий `Updated` принудителен.

## Counter update baseline

Прогон 2026-08-25, UE 5.7.4 / Mac arm64 Development, 100 `UWidgetTransitionAsyncAction::HandleUpdated` / 300 кадров, `Widget Property = None`, float `0→100` и `UTextBlock::SetText(FText::AsNumber(...))` в receiver:

| Сценарий | μs/frame | μs/counter |
| --- | ---: | ---: |
| Plain counter: direct `SetText` | 11.002 | 0.110 |
| Async text counter без binding | 36.604 | 0.366 |
| Async path overhead | 25.602 | 0.256 |

Plain и async сценарии используют одинаковые 100 `UTextBlock`, последовательность `0→100`, `FText::AsNumber` и `SetText`. Async path дополнительно интерполирует `FromValue`/`ToValue` и dispatch-ит Blueprint dynamic multicast. Его разница с plain — 0.256 μs/counter — совпадает с core `Updated` callback baseline (~0.27 μs), значит binding здесь действительно ни при чём.

## Отклонённые эксперименты

`SystemUpdateById` проверял один subsystem-wide dynamic delegate, который передаёт id и progress, а receiver ищет `UTextBlock` в `TMap<TransitionId, UTextBlock>`. Для 100 transition он показал 35.844 μs/frame (0.358 μs/transition) против 36.604 μs/frame у async-счётчика. Выигрыш ~2% находится в пределах шума, поэтому test hook и benchmark удалены из кода. Центральный dispatcher не устраняет Blueprint `ProcessEvent` и `SetText`; возвращаться к этой архитектуре стоит только при отдельной выгоде для API или владения данными.

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
