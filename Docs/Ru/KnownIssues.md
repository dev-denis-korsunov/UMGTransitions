# Известные проблемы

## Содержание

- [Как читать статусы](#как-читать-статусы)
- [Transition update path](#transition-update-path)
- [Открытое regression coverage](#открытое-regression-coverage)
- [Решённые callback-проблемы](#решённые-callback-проблемы)



## Как читать статусы

Страница содержит подтверждённые runtime/editor проблемы. Решённая запись остаётся как история качества, пока полезна для понимания инварианта и его regression-теста. Roadmap-функции сюда не попадают.

## Transition update path

| Приоритет | Статус | Проблема | Решение и покрытие |
| --- | --- | --- | --- |
| Critical | Solved | Spring получал полный `DeltaTime`, а transition time был ограничен 50 ms. | Один `EffectiveDeltaTime` передаётся обоим путям. `Runtime.TickDelta`. |
| Critical | Solved | Callback мог изменить `TArray` transitions, пока tick держал ссылку на его элемент. | Delegates dispatch-ятся из локальной копии, structural removals откладываются. `Runtime.CallbackReentrancy`. |
| High | Solved | Explicit From с нулевым delay не применялся до первого tick. | `StartTransition` сразу записывает non-deferred From. `Runtime.ExplicitFrom`. |
| High | Open | From после delay и повторные циклы не всегда показывают точное стартовое значение один кадр. | Выделить `ApplyCycleStartValue` с учётом `bIgnoreDelay`, repeat и Yo Yo. |
| High | Solved | Async action могла не закончиться при отказе запуска. | Start возвращает success, async освобождается при failure. Путь отказа виден в Activate; отдельный end-to-end тест invalid context/binding отсутствует в приведённом покрытии. |
| Medium | Solved | Spring callback progress выглядел как simulation progress, хотя физическая duration могла отличаться. | Public Updated передаёт только фактическое `Transition Value`. |

## Открытое regression coverage

| Домен | Что не подтверждено текущим тестом | Статус |
| --- | --- | --- |
| From / repeat | Точный стартовый кадр после delay и Yo Yo | Открытый runtime-вопрос |
| Async | Отказ старта при invalid context/binding через полный async node | Проверка кода, нет отдельного end-to-end regression |
| Easing | Полная точность binary 12 и точные endpoints | Есть лишь проверки с допуском и одной формы |
| ColorMix | Shortest path, gray endpoint, alpha, spring interaction | Выделенного набора нет |
| Composer | Реальная cached geometry для wave | GridWaveTranslation задаёт направления вручную |

Пропуск теста сам по себе не доказывает дефект. [Easing](CubicBezier.md#текущее-покрытие), [ColorMix](ColorMix.md#покрытие), [Composer](Composer.md#производительность-и-тестирование) описывают точные границы.

## Решённые callback-проблемы

| Приоритет | Статус | Проблема | Решение |
| --- | --- | --- | --- |
| High | Solved | Native-only Started и final Updated пропускались. | Единый dynamic delegate path; C++ builder принимает BindStart/BindUpdate/BindFinish. `Runtime.NativeBuilder`. |
| High | Solved | Отмена оставляла async action зарегистрированной. | Lifecycle sidecar хранит weak async owner и освобождает его без публичного Finished. `Runtime.CallbackCancellation`. |
| Medium | Solved | Endpoint repeat доставлялся после следующего delay. | Override endpoint dispatch-ится на границе цикла независимо от Event Interval. `Runtime.RepeatCallbackDelay`. |

Для устройства lifecycle и стоимости callbacks: [Events](Events.md). Для правил запуска и поиска regression: [Тестирование](Testing.md).
