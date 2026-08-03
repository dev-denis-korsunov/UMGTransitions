# ElasticUMG — журнал automation-тестов

Этот документ хранит результаты фактических прогонов. Новая строка добавляется только после завершённого запуска; тесты без прогона не помечаются как passed.

## Набор тестов

| Тест | Назначение |
| --- | --- |
| `ElasticUMG.WidgetTransition.Runtime.StorageLayout` | Печатает фактические `sizeof`/`alignof` runtime-структур для контроля оптимизаций. |
| `ElasticUMG.WidgetTransition.Runtime.Easing.Endpoints` | Проверяет границы и нормализацию cubic Bezier easing. |
| `ElasticUMG.WidgetTransition.Runtime.PropertyBinding.RenderOpacity` | Проверяет resolve/read/write типизированного binding к `UWidget.RenderOpacity`. |
| `ElasticUMG.WidgetTransition.Runtime.Spring.Converges` | Проверяет сходимость float- и vector-spring к target. |
| `ElasticUMG.WidgetTransition.Runtime.EventRegistry` | Проверяет хранение, вызов и удаление редкого lifecycle callback по transition ID. |

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

## Правило обновления

Для каждого запуска добавляется отдельная строка с полным именем теста. Для `StorageLayout` в примечание переносятся значения из `AddInfo`; это будет baseline для сравнения после каждого structural refactor.
