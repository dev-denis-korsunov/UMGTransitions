# ElasticUMG — журнал automation-тестов

Здесь записываются только завершённые прогоны. Описание покрытия находится в [Testing.md](Testing.md), методика perf-тестов — в [PerformanceTests.md](Testing/PerformanceTests.md), причины архитектурных решений — в [OptimizationHistory.md](Testing/OptimizationHistory.md).

Новая строка добавляется после успешного запуска с фактическими значениями из лога. `Perf`-тесты не считаются unit-тестами с жёстким порогом: их результаты сравниваются лишь с прогонами той же конфигурации.

| Дата | Конфигурация | Тест | Результат | Примечание |
| --- | --- | --- | --- | --- |
| 2026-08-02 | UE 5.7.4 / Mac arm64 Development | `Runtime.Easing.Endpoints` | Passed | Проверен linear fallback пустого easing handle. |
| 2026-08-02 | UE 5.7.4 / Mac arm64 Development | `Runtime.PropertyBinding.RenderOpacity` | Passed | `UImage.RenderOpacity` resolve/read/write. |
| 2026-08-02 | UE 5.7.4 / Mac arm64 Development | `Runtime.Spring.Converges` | Passed | Spring сходится к target. |
| 2026-08-02 | UE 5.7.4 / Mac arm64 Development | `Runtime.StorageLayout` | Passed | Исторические baseline: transition 440 B, затем 384 B и 224 B после уплотнений. |
| 2026-08-02 | UE 5.7.4 / Mac arm64 Development | `Runtime.EventRegistry` | Passed, historical | Lifecycle callbacks были вынесены в registry по `TransitionId`; отдельный тест впоследствии заменён текущими lifecycle-инвариантами. |
| 2026-08-23 | UE 5.7 / Mac arm64 Development | `Performance.Construction` | Passed | 100 000: direct 0.035 μs/transition; Create 0.034; full pure pipeline 0.121. |
| 2026-08-23 | UE 5.7 / Mac arm64 Development | `Performance.ConcurrentTick` | Passed | 1/10/100/500 `RenderOpacity`: 0.031/0.234/2.352/11.876 μs/frame. |
| 2026-08-23 | UE 5.7 / Mac arm64 Development | `Performance.ModeMatrix` | Passed | Initial 500: Linear 2.924/10.968; easing 26.301/34.491; spring 10.176/21.916 μs/frame (без/с binding). |
| 2026-08-23 | UE 5.7 / Mac arm64 Development | `Performance.ModeMatrix` | Passed | После CurveTable cache, 500: Linear 2.862/19.955; easing 2.831/19.801; spring 11.485/25.174. |
| 2026-08-23 | UE 5.7 / Mac arm64 Development | `Performance.ModeMatrix` | Passed | Spring cache parameters: 8.986 μs/frame без binding; squared completion check: 8.359. |
| 2026-08-23 | UE 5.7 / Mac arm64 Development | `Performance.FastBindings` | Passed | 500 transition после pivot: opacity 5.995; translation 6.078; scale 5.700; shear 6.224; angle 5.600; pivot 5.762 μs/frame. |
| 2026-08-23 | UE 5.7 / Mac arm64 Development | `Runtime.PropertyBinding.Channels` | Passed | Fast adapters для opacity, transform и pivot read/write. |
| 2026-08-23 | UE 5.7 / Mac arm64 Development | `Performance.ModeMatrix` | Passed, rejected | Отдельный sparse spring storage: 10.437 μs/frame без binding. |
| 2026-08-24 | UE 5.7 / Mac arm64 Development | `Performance.ModeMatrix` | Passed | Dense independent spring pass: 7.424, затем 7.368 μs/frame без binding после переноса delay в spring. |
| 2026-08-24 | UE 5.7 / Mac arm64 Development | `Performance.SpringStorage` | Passed | 500 spring: `TArray` 4.212; packed sparse 4.670; fragmented sparse 4.670 μs/frame. |
| 2026-08-24 | UE 5.7 / Mac arm64 Development | `Performance.SparseTransitionStorage` | Passed, historical | До перехода на `TArray`: packed sparse 3.202; 50% fragmented 3.469 μs/frame. |
| 2026-08-24 | UE 5.7 / Mac arm64 Development | `Runtime.RemoveAtSwap` | Passed | Swap-удаление transition и spring сохраняет согласованность индексов. |
| 2026-08-24 | UE 5.7 / Mac arm64 Development | `Performance.ArrayTransitionStorage` | Passed | 500 transition: packed 2.591; после 500 swap removals 2.563 μs/frame; 50 000 removals — 0.034 μs/removal. |
| 2026-08-24 | UE 5.7 / Mac arm64 Development | `Performance.ModeMatrix` | Passed | После `TArray` transition, 500: Linear 2.340/5.499; easing 2.351/5.493; spring 6.402/9.297 μs/frame. |
| 2026-08-24 | UE 5.7 / Mac arm64 Development | `Runtime.StorageLayout` | Passed, rejected experiment | Spring с `int32 TransitionIndex` внутри: 96 B/16 против 80 B/16 без него. |
| 2026-08-24 | UE 5.7 / Mac arm64 Development | `Performance.ModeMatrix` | Passed, rejected experiment | Owner-индекс внутри spring: 500 spring 6.558/9.461 μs/frame; разница по скорости в пределах шума, память хуже. |

## Актуальный baseline

Последний принятый вариант: `TArray<FWidgetTransition>`, dense `TArray<FWidgetTransitionSpring>` и отдельный `SpringTransitionIndices`.

| Тест | Ориентир |
| --- | --- |
| `Runtime.RemoveAtSwap` | Обязан проходить при любом изменении удаления или spring storage. |
| `Performance.ArrayTransitionStorage` | ~2.6 μs/frame на 500 linear transition без binding. |
| `Performance.SpringStorage` | Dense spring `TArray` быстрее sparse. |
| `Performance.ModeMatrix` | 500: Linear 2.340/5.499; easing 2.351/5.493; spring 6.402/9.297 μs/frame (без/с binding). |
