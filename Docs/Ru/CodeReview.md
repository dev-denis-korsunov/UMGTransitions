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
| Medium | Planned | Поверхность рефлексии | `EWidgetTransitionValueType` помечен `BlueprintType`, хотя не используется как enum-пин Blueprint. `FWidgetTransitionPropertyBinding` отражается как структура без отражаемых полей. | Убрать необязательную рефлексию там, где она не нужна UHT и Blueprint; отдельно проверить editor-селектор свойств. |
| Low | Planned | Module boilerplate | Runtime module class не имеет логики startup/shutdown. | Заменить его на `FDefaultModuleImpl`. |
| Low | Planned | Build dependencies | Часть зависимостей runtime/editor модулей выглядит неиспользуемой. | Убирать по одной и подтверждать очистку полной Development-сборкой editor target. |
| Отдельный эксперимент | Deferred | Lifecycle payload | Внутренние lifecycle callbacks всё ещё передают widget, поэтому async action хранит binding и target value. | Отдельно прототипировать value-only lifecycle payload и оставить его только при улучшении читаемости, поведения, памяти и производительности. |

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
