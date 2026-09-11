# Известные проблемы

## Содержание

1. [Как читать статусы](#как-читать-статусы)
2. [Transition update path](#transition-update-path)
3. [Открытое regression coverage](#открытое-regression-coverage)
4. [Решённые callback-проблемы](#решённые-callback-проблемы)

## Как читать статусы

Страница содержит подтверждённые runtime/editor проблемы. Решённая запись остаётся как история качества, пока полезна для понимания инварианта и его regression-теста. Roadmap-функции сюда не попадают.

## Transition update path

| Приоритет | Статус | Проблема | Решение и покрытие |
| --- | --- | --- | --- |
| Critical | Solved | Spring получал полный `DeltaTime`, а transition time был ограничен 50 ms. | Один `EffectiveDeltaTime` передаётся обоим путям. `Runtime.TickDelta`. |
| Critical | Solved | Callback мог изменить `TArray` transitions, пока tick держал ссылку на его элемент. | Delegates dispatch-ятся из локальной копии, structural removals откладываются. `Runtime.CallbackReentrancy`. |
| High | Solved | Explicit From с нулевым delay не применялся до первого tick. | `StartTransition` сразу записывает non-deferred From. `Runtime.ExplicitFrom`. |
| High | Open | From после delay и повторные циклы не всегда показывают точное стартовое значение один кадр. | Выделить `ApplyCycleStartValue` с учётом `bIgnoreDelay`, repeat и Yo Yo. |
| High | Solved | Async action могла не закончиться при отказе запуска. | Start возвращает success, async освобождается при failure. Покрыто async lifecycle tests. |
| Medium | Solved | Spring callback progress выглядел как simulation progress, хотя физическая duration могла отличаться. | Public Updated передаёт только фактическое `Transition Value`. |

## Открытое regression coverage

- Repeat с delay и Yo Yo должен явно проверить value на старте каждого цикла.
- Изменение apply-from semantics требует отдельного regression теста до исправления.

## Решённые callback-проблемы

| Приоритет | Статус | Проблема | Решение |
| --- | --- | --- | --- |
| High | Solved | Native-only Started и final Updated пропускались. | Единый dynamic delegate path; C++ builder принимает BindStart/BindUpdate/BindFinish. `Runtime.NativeBuilder`. |
| High | Solved | Отмена оставляла async action зарегистрированной. | Lifecycle sidecar хранит weak async owner и освобождает его без публичного Finished. `Runtime.CallbackCancellation`. |
| Medium | Solved | Endpoint repeat доставлялся после следующего delay. | Override endpoint dispatch-ится на границе цикла независимо от Event Interval. `Runtime.RepeatCallbackDelay`. |

Для устройства lifecycle и стоимости callbacks: [Events](Events.md). Для правил запуска и поиска regression: [Тестирование](Testing.md).
