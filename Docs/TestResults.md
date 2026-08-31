# UMGTransitions — журнал automation-тестов

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
| 2026-08-24 | UE 5.7 / Mac arm64 Development | `Performance.ModeIndices` | Passed | 500 mixed transition / 300 frames: single pass 1.438; три индексных pass 1.368 μs/frame (−4.9%). |
| 2026-08-24 | UE 5.7 / Mac arm64 Development | `Performance.DirtyWidgetIndices` | Passed | 500 `RenderOpacity` writes / 50 widget / 300 frames: direct 1.707; группы 1.656 μs/frame (−3.0%). |
| 2026-08-24 | UE 5.7 / Mac arm64 Development | `Performance.ModeIndices` | Passed | Специализированные Linear/Easing/Spring passes, 3 прогона: mixed 1.571/1.520/1.520; indexed 1.291/1.256/1.256 μs/frame. Среднее −17.5%. |
| 2026-08-24 | UE 5.7 / Mac arm64 Development | `WidgetSelector.Runtime.Hierarchy` | Passed | DFS, уровни, имена, parent chain и переход через вложенный `WidgetTree → UUserWidget`. |
| 2026-08-25 | UE 5.7.4 / Mac arm64 Development | `Performance.Callbacks` | Passed | 500 linear: without binding 2.648 → 134.044 μs/frame с bound `Updated`; with binding 5.424 → 138.431. Dynamic callback добавляет ~0.263 μs/transition/frame. |
| 2026-08-25 | UE 5.7.4 / Mac arm64 Development | `Performance.Callbacks` | Passed | Реалистичный диапазон 20/30/50/100 `Updated`: 5.443/8.283/13.221/27.532 μs/frame без binding; 5.359/8.689/14.015/27.917 с binding. Цена callback ~0.27 μs/transition/frame. |
| 2026-08-25 | UE 5.7.4 / Mac arm64 Development | `Performance.Callbacks` | Passed | После разделения lifecycle/update флагов 100 `Started+Finished`: 0.537 μs/frame без binding и 1.190 с binding, в пределах baseline 0.503/1.143. 100 `Updated`: 27.771/28.043. |
| 2026-08-25 | UE 5.7.4 / Mac arm64 Development | `Performance.AsyncTextCounter` | Passed | 100 plain text-счётчиков: 11.002 μs/frame; async без `Widget Property`: 36.604. Overhead async: 25.602 μs/frame, 0.256 μs/counter. |
| 2026-08-25 | UE 5.7.4 / Mac arm64 Development | `Performance.SystemUpdateById` | Passed, rejected experiment | 100 subsystem-wide update event + `TMap` lookup по `TransitionId`: 35.844 μs/frame, 0.358 μs/transition. Выигрыш ~2% против async-счётчика — в пределах шума; код удалён. |
| 2026-08-25 | UE 5.7.4 / Mac arm64 Development | `Runtime.UpdateRate` | Passed, historical | Frame-based API впоследствии заменён на `Update Interval` в секундах. |
| 2026-08-25 | UE 5.7.4 / Mac arm64 Development | `Performance.UpdateRate` | Passed, historical | Удалённый frame rate 1/2/3/6 = 27.515/14.094/9.291/5.102 μs/frame. |
| 2026-08-26 | UE 5.7.4 / Mac arm64 Development | `Runtime.UpdateInterval` | Passed | Интервал 0.033 s dispatch-ит `Updated` по накопленному времени; completed transition отправляет final update независимо от interval. |
| 2026-08-26 | UE 5.7.4 / Mac arm64 Development | `Performance.UpdateInterval` | Passed | 100 `Updated`: interval 0/0.033/0.050/0.100 s = 28.333/15.140/10.233/5.298 μs/frame. |
| 2026-08-26 | UE 5.7.4 / Mac arm64 Development | `Performance.ExternalTextBinding` | Passed | 100 external `CounterValue` reflective binding + `UTextBlock.TextDelegate` pull: 30.956 μs/frame, 0.310 μs/widget. Slate layout/paint не входят в headless замер. |
| 2026-08-26 | UE 5.7.4 / Mac arm64 Development | `Performance.FieldNotifyTextBinding` | Passed, per-tick baseline | 100 FieldNotify `CounterValue` push binding: 20.956 μs/frame, 0.210 μs/widget, ~32% быстрее polling `TextDelegate`; все 30 000 notification доставлены. Последующий вариант с `UpdateInterval` требует отдельного прогона. |
| 2026-08-26 | UE 5.7.4 / Mac arm64 Development | `Diagnostics.StorageLayout` | Passed | После FieldNotify без per-transition `FFieldId`: binding 88 B, transition 272 B. |
| 2026-08-31 | UE 5.7.4 / Mac arm64 Development | `Performance.Callbacks` | Passed | Dense callback registry: 100 `Updated` 26.137/26.680 μs/frame без/с binding вместо 27.771/28.043 (−5.9%/−4.9%); lifecycle 0.528/1.093 остаётся в пределах baseline. |
| 2026-08-31 | UE 5.7.4 / Mac arm64 Development | `Runtime.CallbackReentrancy` | Passed | Callback безопасно удаляет свой transition и расширяет transition array при плотном callback registry и `RemoveAtSwap`. |
| 2026-08-31 | UE 5.7.4 / Mac arm64 Development | `Runtime.UpdateInterval` | Passed | Callback-local interval сохраняет throttling и обязательный final update. |

## Актуальный baseline

Последний принятый вариант: `TArray<FWidgetTransition>`, dense `TArray<FWidgetTransitionSpring>` и отдельный `SpringTransitionIndices`.

| Тест | Ориентир |
| --- | --- |
| `Runtime.RemoveAtSwap` | Обязан проходить при любом изменении удаления или spring storage. |
| `Performance.ArrayTransitionStorage` | ~2.6 μs/frame на 500 linear transition без binding. |
| `Performance.SpringStorage` | Dense spring `TArray` быстрее sparse. |
| `Performance.ModeMatrix` | 500: Linear 2.340/5.499; easing 2.351/5.493; spring 6.402/9.297 μs/frame (без/с binding). |
