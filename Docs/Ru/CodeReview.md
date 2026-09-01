# Ревью runtime-кода и план очистки

[English version](../En/CodeReview.md)

Здесь зафиксированы результаты ревью хранилища коллбеков и последующая очистка. Каждое независимо проверяемое изменение оформляется отдельным коммитом. Для изменений чувствительного runtime-пути обязательны regression-тесты и сравнение с записанными замерами.

## Очередь очистки

| Приоритет | Статус | Область | Наблюдение | Изменение |
| --- | --- | --- | --- | --- |
| High | Solved | Изоляция тестов | Automation-only `UCLASS` генерировались внутри runtime-модуля, в том числе для не-editor targets. Реализации были закрыты `WITH_DEV_AUTOMATION_TESTS`, что создавало риск несовпадения UHT-кода и линковки. | Automation-код и отражаемые тестовые типы перенесены в editor-only модуль `UMGTransitionsTests`. В runtime остались только узкие helpers под `WITH_DEV_AUTOMATION_TESTS`. |
| High | Solved | Update-коллбеки | `UpdateStates` уже был плотным массивом, а структурное удаление откладывалось на время dispatch, но `UpdateStateIndices` и `UpdateCallbackIndex` поддерживали второй слой идентичности. | Update states теперь итерируются напрямую. После `RemoveAtSwap` исправляется одна затронутая связь transition-to-state без дублирующего массива индексов. |
| Medium | Solved | Callback API | `HasBoundCallbacks`, дублирующиеся типы async multicast и `bBroadcastUpdateValue` больше не задавали отдельного поведения. | Неиспользуемый helper и кешированный флаг удалены; все три async-выхода используют один value-event type, а регистрация проверяет сам delegate. |
| Medium | Solved | Промежуточные данные tick | `FSample` хранил normalized/eased progress, которые после вычисления sample никто не читал. | Progress теперь локален внутри `SampleTransition`; в `FSample` остались только вычисленное значение и признак завершения. |
| Medium | Solved | Поверхность рефлексии | `EWidgetTransitionValueType` был помечен `BlueprintType`, хотя не использовался как enum-пин Blueprint. `FWidgetTransitionPropertyBinding` отражался как структура без отражаемых полей. | Оба стали обычными C++ типами. Отражаемые endpoints `FWidgetTransitionValue` и `FWidgetTransition` не изменились, editor-селектор продолжает работать с их native-полями. |
| Low | Solved | Module boilerplate | Runtime module class не имел логики startup/shutdown. | Модуль использует `FDefaultModuleImpl`; неиспользуемый публичный module header удалён. |
| Low | Solved | Build dependencies | Часть зависимостей runtime/editor модулей не имела прямого API- или include-использования. | Удалены runtime `DeveloperSettings`, `Slate`, `SlateCore` и editor `PropertyEditor`, `Settings`. `InputCore` оставлен: инстанцированные Slate list widgets линкуются с `EKeys`; editor target проверен полной сборкой. |
| Low | Solved | Документация spring | `FWidgetTransitionSpring` всё ещё был описан как heap-allocated после переноса springs в плотное хранилище subsystem. | Комментарий типа теперь описывает актуальный dense spring array. |
| Low | Solved | Поверхность private helpers | Material-binding helpers оставались объявлены в общем private header после удаления единственного внешнего caller. | Helpers стали implementation-local static-функциями в `WidgetTransition.cpp`. |
| Отдельный эксперимент | Реализован; automation pending | Lifecycle payload | Внутренние lifecycle callbacks передавали widget, поэтому async action хранил binding и target value. | Started, Updated и Finished теперь используют единый value-only payload. Из каждого async action убраны 88 B binding cache и 32 B копии target value; widget остаётся только в короткой lifecycle dispatch-записи, где он нужен для Remove From Parent. |

## Структуры, которые оставляем намеренно

- `CallbackLinks` — компактная связь transition с sidecar-хранилищами для удаления и dispatch.
- Раздельные lifecycle/update массивы не заставляют per-frame проход читать lifecycle-only делегаты.
- `PendingRemovalIndices` и очереди финальных update events нужны для безопасной callback reentrancy.
- `SpringTransitionIndices` сохраняет плотный проход по spring-состояниям и не встраивает владение в сам spring.

## Политика проверки

- Runtime-поведение: `Runtime.CallbackReentrancy`, `Runtime.RemoveAtSwap`, `Runtime.UpdateInterval`.
- Layout: `Diagnostics.StorageLayout`.
- Стоимость коллбеков: `Performance.Callbacks`, `Performance.UpdateInterval`.
- Изменения модулей: полная Development-сборка `ElasticUMGProjectEditor`.
