# ElasticUMG — журнал automation-тестов

Этот документ хранит результаты фактических прогонов. Новая строка добавляется только после завершённого запуска; тесты без прогона не помечаются как passed.

## Набор тестов

| Тест | Назначение |
| --- | --- |
| `ElasticUMG.WidgetTransition.Runtime.StorageLayout` | Печатает фактические `sizeof`/`alignof` runtime-структур для контроля оптимизаций. |
| `ElasticUMG.WidgetTransition.Runtime.Easing.Endpoints` | Проверяет, что пустой `CurveTableRowHandle` выбирает linear interpolation. |
| `ElasticUMG.WidgetTransition.Runtime.PropertyBinding.Channels` | Проверяет resolve/read/write binding к `RenderOpacity` и число каналов для `RenderTransform.Scale`. |
| `ElasticUMG.WidgetTransition.Runtime.Spring.Converges` | Проверяет сходимость единого четырёхканального spring к target. |
| `ElasticUMG.WidgetTransition.Runtime.SpecBuilders` | Проверяет, что pure-функции сохраняют независимые параметры перехода. |
| `ElasticUMG.WidgetTransition.Performance.Construction` | Сравнивает ручную сборку, `Create Widget Transition` и полную pure-цепочку на 100 000 экземпляров. |
| `ElasticUMG.WidgetTransition.Performance.ConcurrentTick` | Измеряет горячий путь interpolation + `RenderOpacity` write при 1, 10, 100 и 500 активных переходах. |
| `ElasticUMG.WidgetTransition.Performance.ModeMatrix` | Сравнивает Linear, CurveTable easing и Spring с binding и без него при 100 и 500 активных переходах. |

## Методика performance-тестов

Оба теста имеют фильтр `Perf` и не выполняются как обычные unit-тесты. Измерения производятся в Development Editor на одной машине; сравнивать следует только результаты с одинаковой конфигурацией.

- `Construction`: 100 000 итераций каждого варианта; в лог выводятся суммарное время и микросекунды на transition.
- `ConcurrentTick`: после одного прогревочного кадра выполняются 300 кадров по `1/60` секунды. Каждый transition привязан к отдельному `UImage.RenderOpacity`, поэтому измеряется и вычисление значения, и запись binding. Лог выводит микросекунды на кадр и на transition для 1, 10, 100 и 500 одновременных переходов.
- `ModeMatrix`: 300 кадров по `1/60` секунды для Linear, CurveTable easing и Spring. Каждый режим запускается с `RenderOpacity` binding и без него, при 100 и 500 transition. CurveTable содержит три ключа: `(0, 0)`, `(0.5, 0.2)`, `(1, 1)`.

`FRealCurve` для easing резолвится один раз при добавлении transition в subsystem. Поэтому изменение CurveTable не меняет уже запущенные transition; их нужно добавить заново.

Для microbenchmark spring сравнение реализации выполняется по варианту без binding: reflection-write в `RenderOpacity` вносит заметный шум между отдельными commandlet-прогонами.

## Результаты прогонов

| Дата | Конфигурация | Тест | Результат | Примечание |
| --- | --- | --- | --- | --- |
| 2026-08-02 | UE 5.7.4 / Mac arm64 Development | `ElasticUMG.WidgetTransition.Runtime.Easing.Endpoints` | Passed | Проверены начало, конец и нормализация cubic Bezier. |
| 2026-08-02 | UE 5.7.4 / Mac arm64 Development | `ElasticUMG.WidgetTransition.Runtime.PropertyBinding.RenderOpacity` | Passed | `UImage.RenderOpacity` успешно resolve/read/write. |
| 2026-08-02 | UE 5.7.4 / Mac arm64 Development | `ElasticUMG.WidgetTransition.Runtime.Spring.Converges` | Passed | Float и Vector2D spring сходятся к target. |
| 2026-08-02 | UE 5.7.4 / Mac arm64 Development | `ElasticUMG.WidgetTransition.Runtime.StorageLayout` | Passed | `FWidgetTransition` 440 B/8; binding 72 B/8; value 24 B/8; easing 32 B/8; float spring 28 B/4; vector spring 64 B/8; каждый dynamic delegate 32 B/8. |
| 2026-08-02 | UE 5.7.4 / Mac arm64 Development | `ElasticUMG.WidgetTransition.Runtime.Easing.Endpoints` | Passed | Повторный прогон после вынесения lifecycle events в subsystem registry. |
| 2026-08-02 | UE 5.7.4 / Mac arm64 Development | `ElasticUMG.WidgetTransition.Runtime.PropertyBinding.RenderOpacity` | Passed | Повторный прогон после вынесения lifecycle events в subsystem registry. |
| 2026-08-02 | UE 5.7.4 / Mac arm64 Development | `ElasticUMG.WidgetTransition.Runtime.Spring.Converges` | Passed | Повторный прогон после вынесения lifecycle events в subsystem registry. |
| 2026-08-02 | UE 5.7.4 / Mac arm64 Development | `ElasticUMG.WidgetTransition.Runtime.StorageLayout` | Passed | `FWidgetTransition` 384 B/8 (−56 B); остальные измеренные типы без изменений. |
| 2026-08-02 | UE 5.7.4 / Mac arm64 Development | `ElasticUMG.WidgetTransition.Runtime.Easing.Endpoints` | Passed | Повторный прогон после hot-data уплотнения. |
| 2026-08-02 | UE 5.7.4 / Mac arm64 Development | `ElasticUMG.WidgetTransition.Runtime.PropertyBinding.RenderOpacity` | Passed | Binding работает без дублированного weak widget pointer. |
| 2026-08-02 | UE 5.7.4 / Mac arm64 Development | `ElasticUMG.WidgetTransition.Runtime.Spring.Converges` | Passed | Spring state переведён на единый optional `TUniquePtr`. |
| 2026-08-02 | UE 5.7.4 / Mac arm64 Development | `ElasticUMG.WidgetTransition.Runtime.EventRegistry` | Passed | Lifecycle callback сохраняется, вызывается и удаляется по `TransitionId`. |
| 2026-08-02 | UE 5.7.4 / Mac arm64 Development | `ElasticUMG.WidgetTransition.Runtime.StorageLayout` | Passed | `FWidgetTransition` 224 B/8 (−160 B от предыдущего baseline, −216 B от исходного); binding 64 B/8; update callback payload 40 B/8; spring state payload 72 B/8. |
| 2026-08-23 | UE 5.7 / Mac arm64 Development | `ElasticUMG.WidgetTransition.Performance.Construction` | Passed | 100 000: direct 0.035 μs/transition; `Create Widget Transition` 0.034 μs; full pure pipeline 0.121 μs. |
| 2026-08-23 | UE 5.7 / Mac arm64 Development | `ElasticUMG.WidgetTransition.Performance.ConcurrentTick` | Passed | 300 кадров, `RenderOpacity` write: 1 — 0.031 μs/frame (0.031 μs/transition); 10 — 0.234 (0.023); 100 — 2.352 (0.024); 500 — 11.876 (0.024). |
| 2026-08-23 | UE 5.7 / Mac arm64 Development | `ElasticUMG.WidgetTransition.Performance.ModeMatrix` | Passed | 500 transition, μs/frame (без/с binding): Linear 2.924/10.968; CurveTable easing 26.301/34.491; Spring 10.176/21.916. На 100: Linear 0.569/2.272; easing 5.478/7.061; spring 1.879/4.309. |
| 2026-08-23 | UE 5.7 / Mac arm64 Development | `ElasticUMG.WidgetTransition.Performance.Construction` | Passed | После кеширования CurveTable: direct 0.031 μs/transition; `Create Widget Transition` 0.032 μs; full pure pipeline 0.128 μs. |
| 2026-08-23 | UE 5.7 / Mac arm64 Development | `ElasticUMG.WidgetTransition.Performance.ConcurrentTick` | Passed | После кеширования CurveTable: 1 — 0.040 μs/frame; 10 — 0.409; 100 — 3.816; 500 — 19.410 (0.039 μs/transition). Сравнивать с матрицей режимов того же прогона. |
| 2026-08-23 | UE 5.7 / Mac arm64 Development | `ElasticUMG.WidgetTransition.Performance.ModeMatrix` | Passed | После кеширования CurveTable, 500 transition, μs/frame (без/с binding): Linear 2.862/19.955; easing 2.831/19.801; spring 11.485/25.174. На 100: Linear 0.581/3.933; easing 0.589/3.951; spring 2.180/5.015. |
| 2026-08-23 | UE 5.7 / Mac arm64 Development | `ElasticUMG.WidgetTransition.Performance.ModeMatrix` | Passed | Spring experiment A — cached frequency/damping: 500 без binding 8.986 μs/frame (0.018 μs/transition), было 11.485. С binding 18.163 μs/frame. |
| 2026-08-23 | UE 5.7 / Mac arm64 Development | `ElasticUMG.WidgetTransition.Runtime.Spring.Converges` | Passed | После перехода на squared completion check spring сохранил сходимость к target. |
| 2026-08-23 | UE 5.7 / Mac arm64 Development | `ElasticUMG.WidgetTransition.Performance.ModeMatrix` | Passed | Spring experiment B — cached parameters + squared completion check: 500 без binding 8.359 μs/frame (0.017 μs/transition), ещё −7%; с binding 23.081 μs/frame, значение шумное между commandlet-прогонами. |

## История оптимизаций runtime

### CurveTable easing

Сравнение использует `500 CurveTable easing` из одинаковой mode matrix: active transition читает одну и ту же кривую с тремя ключами.

| Этап | Изменение | Без binding, μs/frame | С binding, μs/frame | Commit |
| --- | --- | ---: | ---: | --- |
| Baseline | `FCurveTableRowHandle::Eval` ищет curve table row на каждом tick. | 26.301 | 34.491 | `0fb420e` |
| Cached curve | `FRealCurve*` резолвится при добавлении transition и затем вызывается напрямую. | 2.831 | 19.801 | `0fb420e` |

Это убрало отдельную стоимость easing из hot-path: без binding она совпала с linear (`2.862 μs/frame`).

### Spring

Эта таблица использует только вариант `500 Spring, without binding`: он исключает нестабильную цену reflective `RenderOpacity` write и позволяет сравнить именно вычисление spring.

| Этап | Изменение | μs/frame | От исходного | Commit |
| --- | --- | ---: | ---: | --- |
| Baseline | Аналитический четырёхканальный spring, параметры и completion check вычисляются в tick. | 11.485 | — | `0fb420e` |
| A | `Frequency`, damping ratio и damped frequency кешируются при создании spring. | 8.986 | −22% | `4f57a25` |
| B | Completion check сравнивает квадраты длин, без двух `Sqrt` на tick. | 8.359 | −27% | `4f57a25` |

## Правило обновления

Для каждого запуска добавляется отдельная строка с полным именем теста. Для `StorageLayout` в примечание переносятся значения из `AddInfo`; это будет baseline для сравнения после каждого structural refactor.

Для performance-тестов в примечание переносятся все строки `AddInfo`, включая число simultaneous transitions. Это позволяет строить историю отдельно для 1, 10, 100 и 500 без смешивания абсолютной и удельной стоимости.
